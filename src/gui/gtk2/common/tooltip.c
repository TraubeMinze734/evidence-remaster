/** @file  tooltip.c
 *  @brief GTK2 file-info tooltips
 */

#include "config.h"

#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>       /* malloc(), free() */
#include <stdio.h>

#include "errors.h"
#include "debug.h"

#include "gui/common/session.h"
#include "gui/common/tooltip.h"

#include "session_gtk2.h"

#include "file.h"         /* file_get_meta_only() */
#include "icon.h"         /* pixbuf_get_by_path_and_resize() */
#include "util.h"         /* timeout_*(), estring */
#include "model.h"        /* file_info_fetch() */

#ifdef WITH_ICONVIEW
#  include "gui/gtk2/icon_view/icon_view.h"    /* struct noir_iconview */
#  include "gui/gtk2/icon_view/gevas_light.h"  /* gevas_queue_redraw() */
#  include "gui/gtk2/icon_view/gevas_bind.h"   /* ICON_LIST_GET_FROM_WIDGET() */
#endif
#ifdef HAVE_EVAS2
#  include "gui/efl/common/theme.h"
#endif



struct _noir_tooltip {
  GtkWidget      *vbox,    /**< child of tooltip_window, container for label & icon */
                 *label,   /**< info-text for tooltip */
                 *icon;    /**< icon for tooltip */
  gint            x,y;     /**< desired location for tooltip (root-cordinates) */
  char           *file;    /**< file to show tooltip for */
  GtkWidget      *window;  /**< window for GTK-tooltip */
  void           *efl;     /**< evas: object containing tooltip */
  void           *timeout; /**< handle for timeout: show tooltip if mouse hasn't left icon after a while */
  tooltip_open    open;    /**< any tooltips open right now? */
};






/******************************************************************************/
/* GTK2-specific */



/**
 * callback for expose events on tooltip
 * @return FALSE
 * @param tooltip  the tooltip
 */
static gint tooltip_gtk2_expose(noir_tooltip *tooltip) {
  gtk_paint_flat_box(tooltip->window->style,
                     tooltip->window->window,
                     GTK_STATE_NORMAL,GTK_SHADOW_OUT,
                     NULL,GTK_WIDGET(tooltip->window),
                     "tooltip bg",0,0,-1,-1);
  if(!gdk_pointer_grab(tooltip->window->window,FALSE,
                       GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_LEAVE_NOTIFY_MASK,
                       NULL,NULL,GDK_CURRENT_TIME))
    gtk_grab_add(tooltip->window);
  /* return FALSE so text gets drawn too */
  return FALSE; }



/**
 * callback for leave events of session's window
 * @return TRUE
 * @param data   the tooltip-widget
 */
static gboolean tooltip_gtk2_hide(gpointer data) {
  noir_tooltip *tt=(noir_tooltip *)data;
  GtkWidget    *widget=tt->window;

  if(widget&&GTK_WIDGET_VISIBLE(widget)) {
    gtk_grab_remove(widget); /* this shouldn't be necessary */
    gdk_pointer_ungrab(GDK_CURRENT_TIME);
    gtk_widget_hide(widget);
    tt->open=TTO_NONE; }
  return TRUE; }



/**
 * mouse was moved in time, forget about ever showing the tooltip
 * @return FALSE
 * @param data   the tooltip, or NULL
 */
static gboolean tooltip_gtk2_leave(gpointer data) {
  noir_tooltip *tooltip=(noir_tooltip *)data;
  if(tooltip)
    timeout_remove(&tooltip->timeout);
  return FALSE; }



/**
 * create GtkWindow et al. for GTK2 tooltip; connect signals
 * @return an error-code
 * @param ns  the session
 */
static gint tooltip_gtk2_create(noir_sess *ns) {
  noir_tooltip *tooltip=noir_sess_get_tooltip(ns);

  if(!tooltip)
    return NOIR_FAIL;

  if(!tooltip->window) {
    tooltip->window=gtk_window_new(GTK_WINDOW_POPUP);

    gtk_widget_set_app_paintable(tooltip->window,TRUE);
    gtk_window_set_policy(GTK_WINDOW(tooltip->window),FALSE,FALSE,TRUE);
    gtk_widget_set_name(tooltip->window,"tooltip");
    gtk_container_set_border_width(GTK_CONTAINER(tooltip->window),4);

    tooltip->vbox=gtk_vbox_new(FALSE,0);
    tooltip->label=gtk_label_new(NULL);
    gtk_label_set_line_wrap(GTK_LABEL(tooltip->label),TRUE);
    gtk_misc_set_alignment(GTK_MISC(tooltip->label),0.5,0.5);
      
    gtk_container_add(GTK_CONTAINER(tooltip->window),tooltip->vbox);
    gtk_box_pack_start(GTK_BOX(tooltip->vbox),tooltip->label,TRUE,TRUE,0);

    gtk_signal_connect_object(GTK_OBJECT(tooltip->window),"expose_event",
                              GTK_SIGNAL_FUNC(tooltip_gtk2_expose),tooltip);

    gtk_signal_connect_object(GTK_OBJECT(noir_sess_get_window(ns)),"leave-notify-event",
                              G_CALLBACK(tooltip_gtk2_leave),tooltip);

    gtk_signal_connect_object(GTK_OBJECT(tooltip->window),"leave-notify-event",
                              G_CALLBACK(tooltip_gtk2_hide),tooltip);

    gtk_signal_connect_object(GTK_OBJECT(tooltip->window),"button_press_event",
                              G_CALLBACK(tooltip_gtk2_hide),tooltip);

    gtk_signal_connect_object(GTK_OBJECT(tooltip->window),"key-press-event",
                              G_CALLBACK(tooltip_gtk2_hide),tooltip);

    gtk_signal_connect(GTK_OBJECT(tooltip->window),"destroy",
                       GTK_SIGNAL_FUNC(gtk_widget_destroyed),
                       &tooltip->window);

    return NOIR_SUCC; }
  else
    return NOIR_IGNORED; }






/******************************************************************************/



#ifdef HAVE_EVAS2

/**
 * close evas tooltip, if one is open
 * @return an error-code
 * @param ns  the session
 */
static int tooltip_evas_cancel(noir_sess *ns) {
  noir_tooltip *tooltip=noir_sess_get_tooltip(ns);
  int           c;
  icon_el      *e;
  Evas         *evas;
  Evas_Object  *o=NULL;

  if(tooltip&&tooltip->efl) {
    if((e=((icon *)tooltip->efl)->els))
      o=e->data; }

  if(!o)
    return NOIR_FAIL;

  evas=evas_object_evas_get(o);

  /* if the evas-tooltip has a gradient (to low-light the view before
     the actual tooltip is drawn), fade it out before deleting it. */
  while(e&&
        strcmp(evas_object_type_get((Evas_Object *)e->data),"gradient"))
    e=e->next;

  if(e) {
    o=(Evas_Object *)e->data;         /* fade out */
    for(c=7;c>=0;c-=2) {
      evas_object_gradient_colors_clear(o);
      evas_object_gradient_color_add(o,255,255,255, c*4,     63);
      evas_object_gradient_color_add(o,255,255,255, 31+c*32,  7);
      evas_render(evas); }}
  else
    E(1,"evas_motioned: no gradient in tooltip, skipping fade-out sequence...\n");

  ((noir_theme *)noir_sess_get_theme(ns))->tt_dst(&(tooltip->efl));

  return NOIR_SUCC; }

#endif






/******************************************************************************/



/**
 * mouse was moved in time, forget about ever showing the tooltip
 * @return an error-code
 */
int tooltip_cancel(noir_sess *ns) {
  noir_tooltip *tooltip;
  int           ret=NOIR_IGNORED;

  if(!(tooltip=noir_sess_get_tooltip(ns)))
    return NOIR_IGNORED;

  noir_sess_set_tooltip_flags(ns,noir_sess_get_tooltip_flags(ns)&(~TOOLTIP_MOUSE));

  switch(tooltip->open) {
    case TTO_NONE:
      break;
    case TTO_GTK2:
      ret=NOIR_SUCC;
      tooltip_gtk2_hide(tooltip);
      break;
    case TTO_EVAS:
#ifdef HAVE_EVAS2
      ret=NOIR_SUCC;
      tooltip_evas_cancel(ns);
#else
      ret=NOIR_NOTSUPP;
#endif
      break;
    default:
      E(0,"unknown tooltip type %d!?\n",tooltip->open);
      break; }

  tooltip->open=TTO_NONE;

  if(tooltip->timeout) {
    timeout_remove(&tooltip->timeout);
    if(ret==NOIR_IGNORED)
      ret=NOIR_PARTIAL; }

  return ret; }



/**
 * deinit tooltip for this session
 * @return an error-code
 * @param ns  the session
 */
int tooltip_exit(noir_sess *ns) {
  noir_tooltip *tt=noir_sess_get_tooltip(ns);

  if(tt) {
    tooltip_cancel(ns);
    noir_sess_set_tooltip(ns,NULL);
    if(tt->window)
      gtk_widget_destroy(tt->window);
    if(tt->file)
      free(tt->file);
    memset(tt,0,sizeof(noir_tooltip));
    free(tt);
    return NOIR_SUCC; }
  else
    return NOIR_FAIL; }



/**
 * initialize GTK2 tooltip subsystem for this session
 * @return an error-code
 * @param ns the session
 */
int tooltip_init(noir_sess *ns) {
  noir_tooltip *tooltip=noir_sess_get_tooltip(ns);

  if(!tooltip) {
    if(!(tooltip=malloc(sizeof(noir_tooltip))))
      return NOIR_OOM;
    memset(tooltip,0,sizeof(struct _noir_tooltip));
    noir_sess_set_tooltip(ns,tooltip);
    return NOIR_SUCC; }
  else
    return NOIR_IGNORED; }




/**
 * find file in vfolder (search-result, or what-not)
 * @return the file-info, or NULL
 * @param vfolder  the tag of the folder to search in
 * @param virtual  the tag of the file to find
 */
static file_info *tooltip_find_in_virtual(const char *vfolder,const char *virtual) {
  file_info *vfi,*fi=NULL;
  if(!vfolder||!virtual)
    return NULL;
  if((vfi=file_info_fetch(NULL,vfolder))&&(fi=file_info_get_1st_child(vfi))) {
    while(fi) {
      if(!strcmp(virtual,file_info_get_tag(fi))) {
        E(0,"found, but not by file_info_fetch()!?\n");
        return fi; }
      fi=file_info_get_next(fi); }}
  return NULL; }



/**
 * show tooltip *now* (evas or GTK2)
 * @param ns   the session
 * @param file the file to show the tooltip for
 * @param x    where to show the tooltip
 * @param y    where to show the tooltip
 */
int tooltip_show_for(noir_sess *ns,char *file,int x,int y) {
  GtkRequisition  requisition;
  noir_tooltip   *tooltip=noir_sess_get_tooltip(ns);
  gint            w,h,scr_w,scr_h,ret;
  char           *info=NULL;
#ifdef WITH_ICONVIEW
  noir_iconview  *iv;
#endif
  file_info      *fi;

  if(!tooltip)
    return NOIR_FAIL;

  tooltip->x=x;
  tooltip->y=y;

  /* check whether file still points anywhere, session may have disappeared */
  if(!file) {
    E(0,"no file-name given!?\n");
    return NOIR_NOFILE; }

  else if(file!=tooltip->file) {
    if(tooltip->file)
      free(tooltip->file);
    if(!(tooltip->file=strdup(file)))
      return NOIR_OOM; }

  if(!(fi=file_info_fetch(NULL,file))&&
          !(fi=tooltip_find_in_virtual(noir_sess_get_cwd(ns),file))) {
    E(0,"no file-info for \"%s\"!?\n",file);
    return NOIR_NOTFOUND; }

  else if(!file_info_get_meta(fi)) {
    noir_md   *md=NULL;
    E(1,"file-info for \"%s\" has no meta-data (yet), getting it now...\n",file);
    if((ret=file_get_meta_only(ns,file_info_get_tag(fi),&md))!=NOIR_SUCC) {
      E(0,"file_get_meta_only(%s) failed (2): %d\n",file,ret);
      return NOIR_FAIL; }
    file_info_set_meta(fi,md); }



  /* show the tooltip I - EFL */

#if defined(WITH_ICONVIEW)&&defined(HAVE_EVAS2)
  iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);

  if(iv&&iv->icon_scroll&&GTK_WIDGET_MAPPED(iv->icon_scroll)) {
    int        doshow=0;
    gint       mx=0,my=0;
    icon_list *il=ICON_LIST_GET_FROM_WIDGET(iv->icon_scroll);

    if((noir_sess_get_tooltip_flags(ns)&TOOLTIP_MOUSE)) {
      icon *i=icon_list_get_icon_by_file(il,file);
      if(i)
        doshow=1; }
    else
      doshow=1;

    if(doshow) {
      noir_theme *theme=(noir_theme *)noir_sess_get_theme(ns);

      tooltip->open=TTO_EVAS;

      gdk_window_get_origin(iv->icon_scroll->window,&mx,&my);
      tooltip->efl=theme->tt_new(il,x-mx,y-my,fi);
      gevas_queue_redraw((GtkgEvas *)iv->icon_view); }

    noir_sess_set_tooltip_flags(ns,noir_sess_get_tooltip_flags(ns)&(~TOOLTIP_MOUSE)); }

  else
#endif



  /* show the tooltip II - GTK2 */

  { gsize              foo;
    gchar             *utf8;

    tooltip_gtk2_create(ns);

    if(!tooltip->window)
      goto done;

    if(!info&&!(info=meta_to_string(file_info_get_meta(fi))))
      goto done;

    if((utf8=g_locale_to_utf8(info,-1,&foo,&foo,NULL))) {
      free(info);
      info=utf8; }

    gtk_label_set_text(GTK_LABEL(tooltip->label),info);

    if(tooltip->icon)
      gtk_container_remove(GTK_CONTAINER(tooltip->vbox),tooltip->icon);

    if((tooltip->icon=gtk_image_new_from_pixbuf(pixbuf_get_by_path_and_resize(file,TOOLTIP_ICON_SIZE,GDK_INTERP_HYPER))))
      gtk_box_pack_start(GTK_BOX(tooltip->vbox),tooltip->icon,TRUE,TRUE,0);

    gtk_widget_show_all(tooltip->vbox);
    gtk_widget_size_request(tooltip->window,&requisition);
    w=requisition.width;
    h=requisition.height;

    scr_w=gdk_screen_width()-w; scr_h=gdk_screen_height()-h;
    x-=w/2;                     y-=h/2;

    if(x<0) x=0;                if(y<0) y=0;
    if(x>scr_w) x=scr_w;        if(y>scr_h) y=scr_h;

    tooltip->open=TTO_GTK2;

    gtk_window_move(GTK_WINDOW(tooltip->window),x,y);
    gtk_widget_show_all(tooltip->window); }

 done:
  if(info)
    free(info);

  return NOIR_SUCC; }



/**
 * mouse was not moved in time, show tooltip
 * @param data the session
 */
static gint tooltip_timeout(gpointer data) {
  noir_sess    *ns=(noir_sess *)data;
  noir_tooltip *tt=noir_sess_get_tooltip(ns);

  GDK_THREADS_ENTER();
  tt->timeout=NULL; /* timeout expired by itself; don't free it. */
  tooltip_show_for(ns,tt->file,tt->x,tt->y);
  GDK_THREADS_LEAVE();

  return FALSE; }



/**
 * Q tip: if mouse doesn't leave this item soon, show tooltip for it.
 * @param ns   the session
 * @param file the file for which to show the info
 * @param x    the coordinate where the mouse must more or less remain
 * @param y    the coordinate where the mouse must more or less remain
 */
int tooltip_maybe(noir_sess *ns,char *file,int x,int y) {
  noir_tooltip *tooltip=noir_sess_get_tooltip(ns);

  if(!tooltip)
    return NOIR_FAIL;

  if(tooltip->file)
    free(tooltip->file);
  tooltip->file=strdup(file);

  tooltip->x=x;
  tooltip->y=y;

  tooltip_cancel(ns);

  tooltip->timeout=timeout_add(TOOLTIP_DELAY,tooltip_timeout,ns);

  return NOIR_PARTIAL; }



/**
 * enquire what the current file for the tooltip (shown or not) is
 * @return a pointer to the tag, or NULL
 * @param ns  the session
 */
const char *tooltip_get_file(noir_sess *ns) {
  noir_tooltip *tt=noir_sess_get_tooltip(ns);
  return tt?tt->file:NULL; }



tooltip_open tooltip_is_open(noir_sess *ns) {
  noir_tooltip *tt=noir_sess_get_tooltip(ns);
  return tt?tt->open:TTO_NONE; }






/******************************************************************************/
