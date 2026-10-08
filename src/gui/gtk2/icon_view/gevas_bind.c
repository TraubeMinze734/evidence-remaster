/** @file  gevas_bind.c
 *  @brief implements the parts of an icon-list specific to gevas.
 *  generic parts are in icon_list.  evas-specific parts are in evas2.
 */

#include "config.h"

#include <Evas.h>

#include "gui/common/session_private.h"
#include "gui/common/icon_list.h"
#include "gui/common/menu.h"               /* menu_root_context(), menu_file_context() */
#include "gui/common/desktop.h"
#include "gui/common/icon_path.h"          /* icon_path_get*() */
#include "gui/common/tooltip.h"            /* gevas_tooltip_cancel() */

#include "gui/efl/common/theme.h"
#include "gui/efl/common/evas_macros.h"

#include "gui/efl/icon_view/evas_bind.h"

#include "gui/gtk2/common/session_gtk2.h"
#include "gui/gtk2/common/dnd.h"
#include "gui/gtk2/common/icon.h"               /* pixbuf_get_by_path_no_resize() */
#include "gui/gtk2/common/shelf.h"              /* noir_shelf */
#include "gui/gtk2/common/clipboard.h"
#include "gui/gtk2/common/notebook.h"           /* START_IN_ICONVIEW */
#include "gui/gtk2/common/keysyms.h"

#include "gevas_light.h"
#include "gevas_bind.h"         /* ICON_LIST_GET_FROM_WIDGET() */
#include "icon_view.h"          /* noir_iconview */

#include "errors.h"
#include "debug.h"
#include "file.h"
#include "mime.h"               /* MIME_INTL_INF */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "model_private.h"  /* ### FIXME (DIR_PARENT, DIR_DOT) */

#ifdef HAVE_PCRE
#  include <pcre.h>
#endif


#define MOD_ANY          (GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_MOD1_MASK)
#define MOD_SELECT_RANGE  GDK_SHIFT_MASK
#define MOD_SELECT_MULTI  GDK_CONTROL_MASK
#define MOD_DESKTOP       GDK_SHIFT_MASK
#define MOD_NEW_WINDOW   (GDK_MOD1_MASK|GDK_CONTROL_MASK)



static GtkTargetList *target_list=NULL;

typedef enum {
  DRAG_NONE=0,
  DRAG_PRESSED,
  DRAG_DRAGGING
} drag_flag;



/*****************************************************************************/



/**
 * get Evas-pointer from an icon-list.
 * portability helper so plugins don't need to know about gevas.
 * @param il - icon-list
 */
Evas *get_evas_from_iconlist(icon_list *il) {
  return gevas_get_evas((GtkgEvas *)il->canvas); }

/**
 * queue a redraw of the canvas.
 * portability helper so plugins don't need to know about gevas.
 * @param il - icon-list
 */
void evas_queue_redraw(icon_list *il) {
  gevas_queue_redraw(il->canvas); }






/*****************************************************************************/
/* tooltips */
/************/



/**
 * cancel queued tooltip, or remove already displayed tooltip
 * @param ns the session
 * @param redraw redraw the canvas after removing tooltip?
 */
static void gevas_tooltip_cancel(noir_sess *ns,int redraw) {
  /* redirect to icon-view canvas if it originated in shelf */
  noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
  Evas          *evas=gevas_get_evas((GtkgEvas *)iv->icon_view);

  if((tooltip_cancel(ns)==NOIR_SUCC)&&redraw)
    evas_render(evas); }



/**
 * mouse-over tooltip
 * @param ns the session
 * @param file the path to the file to get info on
 * @param x where to display the tooltip
 * @param y where to display the tooltip
 * @param widget the gevas
 */
static inline void gevas_tooltip_by_mouseover(noir_sess *ns,char *file,int x,int y,GtkWidget *widget) {
  noir_shelf *sv=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);

  if(typebuffer_is_open(ns))
    return;

  if(tooltip_get_file(ns)&&!strcmp(file,tooltip_get_file(ns)))
    return;

  /* if request originated in shelf, center in icon-view */
  if(sv&&(widget==sv->shelf_view))
    x=y=-1;
  ns->with_tooltips|=TOOLTIP_MOUSE;
  tooltip_maybe(ns,file,x,y); }



/**
 * keyboard-induced tooltip
 * @param ns the session
 * @param widget the gevas the tooltip should be displayed in (icon-view or shelf)
 * @param i the icon the tooltip is for
 */
static void gevas_tooltip_by_request(noir_sess *ns,GtkWidget *widget,icon *i) {
  if(i) {
    /* use scroll-offsets if request originated in icon-view, center if it originated
       in shelf */
    noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
    icon_list *il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view);
    gint       x1,y1;
    gevas_tooltip_cancel(ns,TRUE);
    gdk_window_get_origin(widget->window,&x1,&y1);
    if(widget==iv->icon_view) {
      x1+=i->x+i->w/2-il->sx;
      y1+=i->y+i->h/2-il->sy; }
    else
      x1=y1=-1;
    ns->with_tooltips&=(~TOOLTIP_MOUSE);
    tooltip_show_for(ns,ICON_GET_FILENAME(i),x1,y1); }}



/*****************************************************************************/
/* callbacks: window */
/*********************/



/**
 * callback: window was (partially?) obscured before, exposed now.
 * I'm gevas exposed callback.  Without me, gevas would have no scrollbar.
 * @return FALSE so further handlers can be processed
 * @param widget the gevas
 * @param ev the expose-event
 * @param data the icon-list shown in this window
 */
gboolean gevas_exposed(GtkWidget *widget,GdkEventExpose *ev,gpointer data) {
  icon_list *il=ICON_LIST_GET_FROM_WIDGET(widget);

  if(il&&ev&&widget) {
    if(!il->sb_v) {
      noir_theme *t=((noir_theme *)il->theme);
      Evas       *evas=gevas_get_evas((GtkgEvas *)il->canvas);

      if(t->sb_new) {
        if(!(il->sb_v=t->sb_new(t,evas)))
          E(0,"failed to make scrollbar!?\n");
        else
          t->sb_set(evas,(icon *)il->sb_v,il->width,il->height,il->vis_h,il->sy); }
      else  /* this is normal for desktop windows */
        E(3,"no scrollbar factory\n"); }

    gtk_widget_grab_focus(widget);
    gevas_queue_redraw((GtkgEvas *)widget); }
  return FALSE; }



/**
 * callback: window was destroyed
 * @param widget the gevas
 * @param il icon-list shown in this window
 */
static void gevas_destroyed(GtkWidget *widget,icon_list *il) {
  /* reget il from widget to see whether it has gone away since the signal
     was connected! */
  if(widget&&il&&(il=ICON_LIST_GET_FROM_WIDGET(widget))) {
    g_object_set_data(G_OBJECT(widget),"user_data",NULL);
    il->canvas=NULL;
    E(2,"evas for icon-list \"%s\" destroyed...\n",il->name);
    icon_list_destroy(il); }}



/**
 * catch X error occurring while updating canvas-background (play along with pseudo-transparency plugins)
 * @param display the display the error is related to
 * @param error the error
 */
#ifdef HAVE_X
static int gevas_bg_update_x_error(Display *display,XErrorEvent *error) {
  E(1,"caught X error during evas_bg_update.\n");
  return 0; }
#endif



/**
 * update canvas-background (play along with pseudo-transparency plugins)
 *
 * if present, call bg_upd() in the background plugin to let the plugin
 * know the  window was moved/resized, the wallpaper was changed, or whatever.
 * plugins that supply pseudo-transparency may then update the PT pixmap 
 * for the given evas icon-list.  the plug-in may update the background it
 * whatever manner it sees fit.
 * @param il the icon-list
 * @param w the new width
 * @param h the new height
 */
int gevas_bg_update(icon_list *il,int w,int h) {
  GtkWidget *widget=il->canvas;

  if((w>0)&&(h>0)&&
     ((noir_theme *)il->theme)->bg_upd&&
     widget&&widget->window) {
    gint x,y;
#ifdef HAVE_X
    int (* x_error_old)(Display *,XErrorEvent *);
    x_error_old=XSetErrorHandler(gevas_bg_update_x_error);
#endif
    gdk_window_get_origin(widget->window,&x,&y);
    E(7,"update evas in widget \"%s\" => %d,%d %dx%d... \n",gtk_widget_get_name(widget),x,y,w,h);
    ((noir_theme *)il->theme)->bg_upd(il,x,y,w,h);
#ifdef HAVE_X
    XSetErrorHandler(x_error_old);
#endif
    return NOIR_SUCC; }
  return NOIR_IGNORED; }



/**
 * callback: window was resized
 * @param widget the gevas
 * @param allocation the new size of the canvas
 * @param data the session
 */
void gevas_icon_list_resized(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
  noir_sess *ns=(noir_sess *)data;

  if(widget&&allocation) {
    icon_list *il=ICON_LIST_GET_FROM_WIDGET(widget);
    Evas      *e;

    if(!il)
      E(0,"broken, no icon-list given when connected!?\n");
    else if(il->bg&&il->canvas&&(e=gevas_get_evas((GtkgEvas *)il->canvas))) {
      file_info *fi=NULL;
      char      *cwd=NULL;

      il_flags flags=il->flags;
      il->flags|=ILF_NOSCROLL;

      gevas_tooltip_cancel(ns,FALSE);
      icon_list_resized(il,allocation->width,allocation->height);

      /* force GTK to redraw on grow, too */
      if((il->vis_h<allocation->height)||(il->vis_w<allocation->width))
        gtk_widget_queue_draw(widget);
      il->vis_w=allocation->width;
      il->vis_h=allocation->height;
      evas_output_size_set(e,il->vis_w,il->vis_h);
      evas_output_viewport_set(e,0,0,il->vis_w,il->vis_h);
      gevas_bg_update(il,allocation->width,allocation->height);
      il->flags=flags;

      sb_update(il,TRUE);

      if(!strcmp(il->name,"icon-view")&& /* bit of a hack, really */
        (cwd=noir_sess_get_cwd(il->sess))&&(fi=file_info_fetch(NULL,cwd)))
        evas_ext_upd(il,fi); }}}



/**
 * force a resize-event on widget containing given icon-list
 * @param il  the icon-list
 */
void gevas_icon_list_force(icon_list *il) {
  GtkAllocation a;
  a.x=
  a.y=0;
  a.width =il->vis_w;
  a.height=il->vis_h;
  gevas_icon_list_resized(il->canvas,&a,il->sess); }





/******************************************************************************/
/* callbacks: "user-input" */
/***************************/



/**
 * create drag-icon
 * @return an error-code
 * @param ns  the session
 * @param il  the icon-list
 * @param i   the icon
 */
static inline int gevas_drag_icon_new(noir_sess *ns,icon_list *il,icon *i) {
  int         w,h,ret=NOIR_SUCC;
  const char *icon_file=NULL;
  GdkPixbuf  *di=NULL;

  /* if there's more than one file selected, try to find a "selection" icon */
  if(action_context_get_seletion_size(noir_sess_get_selection(ns))>1) {
    if(!(icon_file=icon_path_get_always(NULL,"selection")))
      ret=NOIR_NOTFOUND; }

  /* if we don't have an icon yet (no selection, or no selection icon)... */
  if(!icon_file) {
    file_info *fi;
    if((fi=i->user2)&&file_info_get_thumbfile(fi))  /* do we have a thumbnail (yet)? */
      icon_file=file_info_get_thumbfile(fi); /* use thumbnail */
    else
      icon_file=i->name; }     /* use MIME-icon */

  /* try to load the icon... */
  if((di=pixbuf_get_by_path_no_resize(icon_file))) {
    noir_sess_set_dragicon(ns,di);
    w=gdk_pixbuf_get_width(di);
    h=gdk_pixbuf_get_height(di);
    gtk_drag_set_icon_pixbuf(il->dc,di,w/2,h/2); }

  else { /* if we couldn't get the icon, use the default */
    noir_sess_set_dragicon(ns,NULL);
    gtk_drag_set_icon_default(il->dc);
    if(ret==NOIR_SUCC)
      ret=NOIR_NOTFOUND; }

  return ret; }



/**
 * hilight a given icon in the icon-list, lowlight the rest
 * @param il  the icon-list
 * @param j   an icon to highlight, or NULL for none
 */
static inline void gevas_hilight_icon(icon_list *il,icon *j) {
  icon *i=il->icons;
  while(i) {
    if(i==j) {
      if(!(j->state&IE_HI))
        evas_icon_set_state(il,j,j->state|IE_HI); }
    else if(i->state&IE_HI)
      evas_icon_set_state(il,i,i->state&(~IE_HI));
    i=i->next; }
  gevas_view_redraw(il->canvas); }



/**
 * make logical modifier from physical one
 * @return logical modifier
 * @param st physical modifiers (bit-vector)
 */
static inline theme_area_event_modifiers gevas_modifier_xlate(int st) {
  theme_area_event_modifiers m=EV_MOD_NONE;
  st&=MOD_ANY;
  if((st&MOD_SELECT_RANGE)==MOD_SELECT_RANGE)
    m=EV_MOD_SELECT_RANGE;
  else if((st&MOD_SELECT_MULTI)==MOD_SELECT_MULTI)
    m=EV_MOD_SELECT_MULTI;
  else if((st&MOD_NEW_WINDOW)==MOD_NEW_WINDOW)
    m=EV_MOD_NEW_WINDOW;
  return m; }



/**
 * callback: mouse was moved across canvas
 * @param widget the gevas
 * @param ev the motion-event
 * @param data the session
 */
static gboolean gevas_motioned(GtkWidget *widget,GdkEventMotion *ev,gpointer data) {
  noir_sess *ns=(noir_sess *)data;
  int        ret=FALSE;
  icon_list *il;
  icon      *j;
  int        sx=0,sy=0;

  if(ns&&ev&&widget&&(il=ICON_LIST_GET_FROM_WIDGET(widget))) {
    Evas           *evas=gevas_get_evas((GtkgEvas *)il->canvas);
    il_flags        flags=il->flags;

    sx=il->sx;
    sy=il->sy;

    ret=TRUE;

    if(tooltip_is_open(ns)!=TTO_NONE) { /* ToolTip *open* for display?
                                           Check if still over same icon */
      j=icon_list_get_icon_at(il,ev->x,ev->y);
      if(j&&!strcmp(tooltip_get_file(ns),ICON_GET_FILENAME(j)))
        return ret;                     /* icon for same file, do nothing. */

      /* mouse moved outside of icon we have a tooltip open for.  close TT. */
      gevas_tooltip_cancel(ns,TRUE);
      evas_render(evas); }



    else if(il->dragflag==DRAG_PRESSED) {         /* Drag Icon (hand to GTK) */
      if((ev->x<(il->mxb-FUZZ))||
        (ev->x>(il->mxb+FUZZ))||
        (ev->y<(il->myb-FUZZ))||
        (ev->y>(il->myb+FUZZ))) {
          gint  no=il?icon_list_get_index_at(il,ev->x,ev->y):-1;
          icon *i=(il&&(no>=0))?icon_list_get_icon_by_index(il,no):NULL;
          if(i) {
            if(!(i->state&IE_SEL)) { /* dragged icon previously unselected? */
              E(4,"making \"%s\" the selection...\n",ICON_GET_LABEL(i));
              j=il->icons;
              while(j) {                /* ...yes => make selection */
                if((j!=i)&&(j->state&IE_SEL))
                  evas_icon_set_state(il,j,j->state&(~IE_SEL));
                j=j->next; }
              evas_icon_set_state(il,i,i->state|IE_SEL);
              icon_list_get_selection(ns,il);
              evas_render(evas); }

            il->mb=0;
            if((il->dc=gtk_drag_begin((GtkWidget *)il->canvas,target_list,GDK_ACTION_LINK|GDK_ACTION_COPY|GDK_ACTION_MOVE,1,(GdkEvent *)ev))) {
              il->dragflag=DRAG_DRAGGING;
              gevas_drag_icon_new(ns,il,i); }
            else
              E(0,"could not iniate drag sequence!?\n"); }}}



    else if(il->rubber) {      /* pulling rubber-band, give visual feedback */
      int dx,dy,x1,y1,x2,y2,f,mx,my;
      GdkModifierType mk;

      il->flags|=ILF_NOSCROLL;

      do {
        il->mxe=ev->x;
        il->mye=ev->y;
        dx=dy=f=0;

        if(il->vis_w<il->mxe) {
          f=1;
          dx=il->mxe-il->vis_w;
          il->mxe=il->vis_w; }
        else if(il->mxe<0) {
          f=1;
          dx=il->mxe;
          il->mxe=0; }

        if(il->vis_h<il->mye) {
          f=1;
          dy=il->mye-il->vis_h;
          il->mye=il->vis_h; }
        else if(il->mye<0) {
          f=1;
          dy=il->mye;
          il->mye=0; }

        if(dx||dy) {
          evas_damage_rectangle_add(evas,0,0,il->vis_w,il->vis_h);
          icon_list_scroll_rel(il,dx,dy); }

        sx=il->sx;
        sy=il->sy;

        il->mxe+=il->sx;
        il->mye+=il->sy;
        if(il->mxb>il->mxe) {       /* make x1<=x2, y1<=y2 */
          x1=il->mxe;
          x2=il->mxb; }
        else {
          x1=il->mxb;
          x2=il->mxe; }
        if(il->myb>il->mye) {
          y1=il->mye;
          y2=il->myb; }
        else {
          y1=il->myb;
          y2=il->mye; }
        il->rx1=x1; il->ry1=y1; il->rx2=x2; il->ry2=y2;
        ((noir_theme *)ns->theme)->rubber_set(il,x1-sx,y1-sy,x2-sx,y2-sy);

        gdk_window_get_pointer(widget->window,&mx,&my,&mk);
        mk&=GDK_BUTTON1_MASK;
        if((ev->x!=mx)||(ev->y!=my))
          f=0;
        gevas_view_redraw(il->canvas);
      } while(f&&mk); }



    else if(il->mb==1&&((noir_theme *)ns->theme)->rubber_new&&((noir_theme *)ns->theme)->rubber_set) {  /* open rubber-band */
      il->rubber=((noir_theme *)ns->theme)->rubber_new(il);
      ((noir_theme *)ns->theme)->rubber_set(il,
                            il->mxb-sx,il->myb-sy,
                            il->mxe-sx,il->mye-sy); }



    else if(il->ss==SB_BAR) {  /* scroll */
      int dy=(il->mye=ev->y)-il->myb+il->mys;
      il->flags|=ILF_NOSCROLL;
      icon_list_scroll_abs(il,0,(il->height*dy)/il->vis_h);
/*      gevas_view_redraw(il->canvas); */
    }



    else if((j=icon_list_get_icon_at(il,ev->x,ev->y))) {  /* mouse-over */
      gevas_hilight_icon(il,j);
      if((ns->with_tooltips))
        gevas_tooltip_by_mouseover(ns,ICON_GET_FILENAME(j),(gint)ev->x_root,(gint)ev->y_root,widget); }



    else {                                                /* over backdrop */
      gevas_hilight_icon(il,NULL);
      if(evas_ext_evt(il,ev->x,ev->y,0,EV_MOTIONED,gevas_modifier_xlate(ev->state),ns,NULL,NULL))
        ret=TRUE;
      else
        gevas_tooltip_cancel(ns,TRUE); }

    il->flags=flags; }

  gdk_window_get_pointer(widget->window,NULL,NULL,NULL);

  return ret; }



/**
 * callback: key was pressed while canvas was focused.
 * w/ imwheel, mouse-wheel events go here, as well.
 * @param widget the gevas
 * @param ev the scroll-event
 * @param data the session
 */
static gboolean gevas_keyed(GtkWidget *widget,GdkEventKey *ev,gpointer data) {
  noir_sess  *ns=(noir_sess *)data;
  icon_list  *il=ICON_LIST_GET_FROM_WIDGET(widget); /* survives widget==NULL */
  int         ret=FALSE;

  if(widget&&ev&&il&&(il->icons)) {
    int     line=((noir_theme *)ns->theme)->icon_height+il->hpad;
    int     t;
    size_t  l=strlen(il->find);
    icon   *i=icon_list_get_icon_by_index(il,il->ki),*j;
    Evas   *e=gevas_get_evas((GtkgEvas *)il->canvas);

    gevas_tooltip_cancel(ns,TRUE);

    ret=TRUE;

    /* if no icon was select using the keyboard, but exactly one was selected
       with the mouse, we'll operate on that instead. */
    if(!i) {
#if 1
      i=evas_find_first_selected(il);
#else
      if((i=evas_find_first_selected(il))&&(j=i->next)) {
        while(j) {
          if(j->state&IE_SEL) {
            if(ev->length)
              E(0,"will not run keyboard-induced action on more than one file -- this is pretty arbitrary; if people want this, we can change it.\n");
            i=j=NULL; }
          else
            j=j->next; }}
#endif
    }

    switch(ev->keyval) {
    case GDK_Delete: {
      int ret;
      if(i) {
        if((ret=menu_action_by_mime(noir_sess_get_serial(ns),MIME_INTL_DEL,i->file,noir_sess_get_selection(ns)))!=NOIR_SUCC)
          E(0,"could not open delete dialog (type \"" MIME_INTL_DEL "\") for %s: %d\n",i->file?i->file:"???",ret); }}
      break;

    case GDK_Find:
      E(0,"find: forthcoming, use typebuffer for now...\n");
      break;

    case GDK_Select:
    case GDK_space:
      if(l) {    /* is type-to-find active? */
        if(ev->state&GDK_CONTROL_MASK) {    /* Control-space toggles select */
          for(i=il->icons;i;i=i->next)
            if(i->state&IE_KBD)
              evas_icon_set_state(il,i,(i->state&IE_SEL)?(i->state&(~IE_SEL)):(i->state|IE_SEL));
          icon_list_get_selection(ns,il);
          gevas_queue_redraw((GtkgEvas *)il->canvas); }
        else {                              /* space alone added to type-to-find */
          if(evas_find_icon_by_string_chk(il,&l)==NOIR_IGNORED) {
            if((l+(ev->length))<ICON_LIST_FIND_MAX) {
              strcat(il->find,ev->string);
              evas_find_icon_by_string(il); }}
          else {
            for(j=il->icons;j;j=j->next)
              evas_icon_set_state(il,j,(j->state&IE_KBD)
                                  ?((j->state&(~IE_KBD))|IE_SEL|
                                    ((j->index==il->ki)?IE_KBD:IE_DK))
                                  :(j->state&(~IE_SEL)));
            icon_list_get_selection(ns,il);
            gevas_queue_redraw((GtkgEvas *)il->canvas); }}}

      else {    /* type-to-find is inactive */
        if(ev->state&GDK_CONTROL_MASK) {   /* Control-space toggles select */
          for(i=il->icons;i;i=i->next)
            if(i->state&IE_KBD)
              evas_icon_set_state(il,i,(i->state&IE_SEL)?(i->state&(~IE_SEL)):(i->state|IE_SEL)); }
        else if(ev->state&GDK_SHIFT_MASK)  /* ignore Shift-space for now */
          ;
        else                               /* space alone makes icon the selection */
          for(j=il->icons;j;j=j->next)
            evas_icon_set_state(il,j,(j->state&IE_KBD)
                                ?((j->state&(~IE_KBD))|IE_SEL|
                                  ((j->index==il->ki)?IE_KBD:IE_DK))
                                :(j->state&(~IE_SEL)));
        icon_list_get_selection(ns,il);
        gevas_queue_redraw((GtkgEvas *)il->canvas); }
      break;

    case GDK_KP_Enter:
    case GDK_Return: {
      char *file=evas_get_file_from_icon(ns,i);
      int   isdir=file?file_exists(file):0;

      if(file) {
        il->dragflag=DRAG_NONE;
        il->mb      =0;

        ICON_LIST_CLEAR_FIND(il);

        if(S_ISDIR(isdir)) {
          if(ev->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK))
            noir_sess_create(SESSION_COPY,ns,file);
          else
            evas_open_folder(ns,il,i,e); }
        else if(isdir)
          evas_file_activate(il,i,e);

        free(file); }}
      break;

    case GDK_Left:
      evas_keyed_icon(il,(il->ki<0)?0:il->ki-1);
      if(i&&(ev->state&GDK_SHIFT_MASK)) {
        evas_icon_set_state(il,i,i->state|IE_SEL);
        i=icon_list_get_icon_by_index(il,il->ki);
        evas_icon_set_state(il,i,i->state|IE_SEL);
        icon_list_get_selection(ns,il);
        gevas_queue_redraw((GtkgEvas *)il->canvas); }
      break;
    case GDK_Right:
      if((t=il->ki+1)<il->num_icons) {
        evas_keyed_icon(il,t);
        if(i&&(ev->state&GDK_SHIFT_MASK)) {
          evas_icon_set_state(il,i,i->state|IE_SEL);
          i=icon_list_get_icon_by_index(il,il->ki);
          evas_icon_set_state(il,i,i->state|IE_SEL);
          icon_list_get_selection(ns,il);
          gevas_queue_redraw((GtkgEvas *)il->canvas); }}
      break;
    case GDK_Up:
      if(ev->state&GDK_CONTROL_MASK)
        icon_list_scroll_rel(il,0,-line);
      else if(i) {
        icon *c=il->icons,*f;
        f=c;
        while(c&&(c->y<i->y)) {
          if(f->y!=c->y) {
            f=c; }
          c=c->next; }
        while(f&&f->next&&((f->x+f->w)<i->x)&&(f->next->y==f->y))
          f=f->next;
        if(f) {
          if(i&&(ev->state&GDK_SHIFT_MASK)) {
            icon *n=i;
            i=f;
            while(i&&i!=n) {
              evas_icon_set_state(il,i,i->state|IE_SEL);
              i=i->next; }
            evas_icon_set_state(il,n,n->state|IE_SEL);
            icon_list_get_selection(ns,il);
            gevas_queue_redraw((GtkgEvas *)il->canvas); }
          evas_keyed_icon(il,f->index); }}
      else
        evas_keyed_icon(il,0);
      break;
    case GDK_Down:
      if(ev->state&GDK_CONTROL_MASK)
        icon_list_scroll_rel(il,0,line);
      else if(i) {
        icon *n=i;
        while(n&&(n->y==i->y))
          n=n->next;
        while(n&&n->next&&((n->x+n->w)<i->x)&&(n->next->y==n->y))
          n=n->next;
        if(n) {
          if(i&&(ev->state&GDK_SHIFT_MASK)) {
            while(i&&i!=n) {
              evas_icon_set_state(il,i,i->state|IE_SEL);
              i=i->next; }
            evas_icon_set_state(il,n,n->state|IE_SEL);
            icon_list_get_selection(ns,il);
            gevas_queue_redraw((GtkgEvas *)il->canvas); }
          evas_keyed_icon(il,n->index); }}
      else
        evas_keyed_icon(il,0);
      break;

    case GDK_Page_Up:
      E(1,"wheel event (up), imwheel\n");
      icon_list_scroll_pg(il,0,-1);
      break;
    case GDK_Page_Down:
      E(1,"wheel event (dn), imwheel\n");
      icon_list_scroll_pg(il,0,1);
      break;

    case GDK_Begin:
    case GDK_Home:
      icon_list_scroll_abs(il,0,0);
      evas_keyed_icon(il,0);
      break;
    case GDK_End:
      icon_list_scroll_abs(il,0,il->height-il->vis_h);
      evas_keyed_icon(il,il->num_icons-1);
      break;

    case GDK_BackSpace:
      evas_find_icon_by_string_chk(il,&l);
      if(l) {
        il->find[--l]='\0';
        evas_find_icon_by_string(il); }
      break;

    case GDK_F2:     /* F2 - rename item */
      if(!(ev->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK)))
        evas_rename_icon(ns,i);
      else
        ret=FALSE;
      break;

    case GDK_Menu: {   /* Menu - context menu */
      char *f=evas_get_file_from_icon(ns,i);
      if(f) {
        menu_file_context(ns,noir_sess_get_cwd(ns),f);
        free(f); }
      else
        menu_root_context(ns,noir_sess_get_cwd(ns),3); }
      break;

    default:
      if(ev->state&GDK_CONTROL_MASK) {
        switch(ev->keyval) {
        case GDK_a:     /* ^a - select all */
        case GDK_A:     /* ^A - deselect all */
          j=il->icons;
          if(ev->state&GDK_CONTROL_MASK) {
            int clear=(ev->state&GDK_SHIFT_MASK);
            while(j) {
              if(j->label&&strcmp(j->label,DIR_PARENT)&&strcmp(j->label,DIR_DOT))
                evas_icon_set_state(il,j,clear?(j->state&(~IE_SEL)):(j->state|IE_SEL));
              j=j->next; }
            icon_list_get_selection(ns,il);
            gevas_queue_redraw((GtkgEvas *)il->canvas); }
          break;
        case GDK_r:     /* ^R - rename item */
          evas_rename_icon(ns,i);
          break;
        case GDK_m: {   /* ^M - context menu */
          char *f=evas_get_file_from_icon(ns,i);
          if(f) {
            menu_file_context(ns,noir_sess_get_cwd(ns),f);
            free(f); }
          else
            menu_root_context(ns,noir_sess_get_cwd(ns),3); }
          break;
        case GDK_p: {   /* ^P - properties dialog */
          int ret;
          if(i||(i=evas_find_first_selected(il))) {
            if((ret=menu_action_by_mime(noir_sess_get_serial(ns),MIME_INTL_INF,i->file,noir_sess_get_selection(ns)))!=NOIR_SUCC)
              E(0,"could not open properties dialog (type \"" MIME_INTL_INF "\"): %d\n",ret); }}
          break;
        case GDK_i:     /* ^I - info tooltip */
          gevas_tooltip_by_request(ns,widget,i);
          break; }}

      else if(ev->length) {  /* type-to-find */
        evas_find_icon_by_string_chk(il,&l);
        if((l+(ev->length))<ICON_LIST_FIND_MAX) {
          strcat(il->find,ev->string);
          evas_find_icon_by_string(il); }}

      else
        ret=FALSE; }}

  else
    E(1,"no data -- %s %s %s %s\n",
      widget?"widget":"NO_WIDGET",
      ev?"event":"NO_EVENT",
      il?"iconlist":"NO_ICONLIST",
      (il&&il->icons)?"icons":"NO_ICONS");

  return ret; }



/**
 * callback: canvas was scrolled
 * without imwheel, we get here.  w/ imwheel, we'll get a key-event instead.
 * @param widget the gevas
 * @param ev the scroll-event
 * @param data the session
 */
gboolean gevas_scrolled(GtkWidget *widget,GdkEventScroll *ev,gpointer data) {
  noir_sess  *ns=(noir_sess *)data;
  icon_list  *il=ICON_LIST_GET_FROM_WIDGET(widget);
  noir_theme *t=(noir_theme *)il->theme;
  int         w,h;
  int         shift=(ev->state&(GDK_MOD1_MASK|GDK_SHIFT_MASK));
  int        (*scroll)(icon_list *il,int x,int y);

  E(1,"wheel event, no imwheel\n");

  gevas_tooltip_cancel(ns,FALSE);

  if(shift) {
    scroll=icon_list_scroll_rel;
    w=t->icon_width;
    h=t->icon_height; }
  else {
    scroll=icon_list_scroll_pg;
    w=h=1; }

  switch(ev->direction) {
    case GDK_SCROLL_UP:
      scroll(il,0,-h);
      break;
    case GDK_SCROLL_DOWN:
      scroll(il,0,h);
      break;
    case GDK_SCROLL_LEFT:
      scroll(il,-w,0);
      break;
    case GDK_SCROLL_RIGHT:
      scroll(il,w,0);
      break; }

  return TRUE; }



/**
 * forward mouse-click to desktop
 * when evidence handles a desktop, it can forward clicks to the "original"
 * background (to call up window-manager menus etc.)
 * @param mx      mouse x
 * @param my      mouse y
 * @param mb      mouse buttons (X format)
 * @param state   modifiers (shift etc., X format)
 * @param serial  last X event seen
 * @param timestamp  when did the event happen?
 */
static inline void gevas_desktop_click(int mx,int my,int mb,int state,unsigned long serial,unsigned long timestamp) {
  desktop_click(mx,my,mb,1,state,serial,timestamp);
  desktop_click(mx,my,mb,0,state,serial,timestamp); }



/**
 * callback: canvas was clicked on
 * @return TRUE if event was handled conclusively
 * @param widget the gevas
 * @param ev the click-event
 * @param data the session
 */
static gboolean gevas_clicked(GtkWidget *widget,GdkEventButton *ev,gpointer data) {
  noir_sess                    *ns=(noir_sess *)data;
  int                           ret=FALSE;
  char                         *file=NULL;
  theme_area_event_type         t=EV_NONE;
  theme_area_event_modifiers    m=EV_MOD_NONE;
  static theme_area_event_type  prvs_click=EV_NONE;

  if(ns&&ev) {
    icon_list             *il=ICON_LIST_GET_FROM_WIDGET(widget);
    Evas                  *evas=gevas_get_evas((GtkgEvas *)il->canvas);

    gevas_tooltip_cancel(ns,TRUE);   /* cancel any tooltips on click */


    /********************************************************** EXTENSIONS ***/
    switch(ev->type) {
      case GDK_BUTTON_RELEASE: t=EV_RELEASED; break;
      case GDK_BUTTON_PRESS:   t=EV_SINGLE;   break;
      case GDK_2BUTTON_PRESS:  t=EV_DOUBLE;   break;
      case GDK_3BUTTON_PRESS:  t=EV_TREBLE;   break;
      default:                 t=EV_NONE; }

    m=gevas_modifier_xlate(ev->state);

/*  E(0,"click: buttons %d, type %d\n",ev->button,t); */

    if((t!=EV_NONE)&&
       (evas_ext_evt(il,ev->x,ev->y,ev->button,t,m,ns,NULL,NULL)!=NOIR_NOTFOUND)) {
      if((t!=EV_RELEASED)||(ev->button!=1)||!il->rubber) {
/*    if(!il->rubber) {*/
        return TRUE; }}



    /************************************************* TOOLTIP / PATH-MENU ***/
    if(ev->button==2) {
      if(t==EV_DOUBLE) {
        E(0,"middle/double\n"); }
      else if(t==EV_RELEASED) {
        icon *j=icon_list_get_icon_at(il,ev->x,ev->y);
        if(typebuffer_is_open(ns))
          clipboard_get(GDK_SELECTION_PRIMARY,ns);
        else if(j)
          gevas_tooltip_by_request(ns,widget,j);
        else if((prvs_click!=EV_DOUBLE)&&noir_sess_get_desktop(ns)&&!(ev->state&MOD_DESKTOP))
          gevas_desktop_click(ev->x_root,ev->y_root,ev->button,ev->state,0L,ev->time);
        else
          menu_path(ns,ev->button,ev->time); }
      return TRUE; }



    /*************************************************************************/
    else if(GTK_WIDGET_MAPPED(widget)) {  /* ns->icon_scroll */
      gint       no=il?icon_list_get_index_at(il,ev->x,ev->y):-1;
      icon      *i=(il>=0)?icon_list_get_icon_by_index(il,no):NULL;

      if(!il) {
        E(0,"no icon-list set on widget!?\n");
        return FALSE; }

      file=evas_get_file_from_icon(ns,i);



      /****************************************************** CONTEXT-MENU ***/
      switch(ev->button) {
      case 3:
        if(t==EV_DOUBLE) {
          E(0,"right/double\n");
          menu_root_context(ns,noir_sess_get_cwd(ns),ev->button); }
        else if(t==EV_RELEASED) {
          if(no<0) {
            if((prvs_click!=EV_DOUBLE)&&noir_sess_get_desktop(ns)&&!(ev->state&MOD_DESKTOP))
              gevas_desktop_click(ev->x_root,ev->y_root,ev->button,ev->state,0L,ev->time);
            else
              menu_root_context(ns,noir_sess_get_cwd(ns),ev->button); }
          else
            menu_file_context(ns,noir_sess_get_cwd(ns),file); }
        ret=TRUE;
        goto ffae;
        break; /* NOTREACHED */



      case 1:
        ret=TRUE;

        /* if any icon was select using the keyboard, clear that selection
           now -- user needs to be clear on what selection the context-menu
           will work on */
        if(il->ki>=0)
          evas_keyed_icon(il,-1);

        /**************************************************** DOUBLE-CLICK ***/
        if(ev->type==GDK_2BUTTON_PRESS) { /* double click */
          int        isdir;

          il->dragflag=DRAG_NONE;
          il->mb=0;

          if(file&&(isdir=file_exists(file))) {
            if(pointer_on_iconlabel_p(i,ev->x+il->sx,ev->y+il->sy))
              evas_rename_icon(ns,i);
            else if(S_ISDIR(isdir)) {
              int newwin=(ev->state&MOD_NEW_WINDOW)?1:0;

              if(il->flags&ILF_SPATIAL)
                newwin=1-newwin;

              if(newwin)
                noir_sess_create(SESSION_COPY,ns,file);
              else
                evas_open_folder(ns,il,i,evas); }

            else if(isdir)
              evas_file_activate(il,i,evas);
            else
              ret=FALSE; }
          else
            ret=FALSE; }



        /**************************************************** SINGLE-CLICK ***/
        else {
          icon *j=il->icons;

          il->mb=0;                         /* reset button state so release + */
                                            /* motion won't trigger a rubberband */
          il->ss=SB_NONE;                   /* same for scrollbars */

          if(ev->type==GDK_BUTTON_PRESS) {  /* save state in case we'll need a */
                                            /* rubber-band later */
            switch((il->ss=sb_chk(il,il->sb_v,ev->x,ev->y))) {
            case SB_PGDN:
              icon_list_scroll_pg(il,0,-1);
              goto ffae;
            case SB_PGUP:
              icon_list_scroll_pg(il,0,1);
              goto ffae;
            case SB_BAR:
              il->myb=ev->y;
              il->mys=(il->vis_h*il->sy)/il->height;
              goto ffae; }

            if(i&&!(ev->state&(MOD_SELECT_MULTI|MOD_SELECT_RANGE)))
              il->dragflag=DRAG_PRESSED;

            il->mxe=il->mxb=ev->x+il->sx;
            il->mye=il->myb=ev->y+il->sy;
            il->mb=ev->button; }



          /************************************************** RELEASE-LEFT ***/
          else if(il->rubber) {             /* rubber-band completed */
            ((noir_theme *)ns->theme)->rubber_dst(&(il->rubber));      /* reset marker */

            if(!(ev->state&(MOD_SELECT_MULTI|MOD_SELECT_RANGE))) { /* no modifiers */
              while(j) {
                evas_icon_set_state(il,j,j->state&(~IE_SEL));
                j=j->next; }}

            j=il->icons;
            while(j) {
              if(icon_list_icon_in_rubber(il,j)) {
                if(ev->state&MOD_SELECT_MULTI)       /* toggle in selection */
                  evas_icon_set_state(il,j,(j->state&IE_SEL)?(j->state&(~IE_SEL)):(j->state|IE_SEL));
                else
                  evas_icon_set_state(il,j,j->state|IE_SEL); } /* add to sel */
              j=j->next; }}



          else {                            /* non-rubbery release */
            if(sb_chk(il,il->sb_v,ev->x,ev->y)!=SB_NONE)
              ;  /* scrolling shouldn't clear section */

            else if(il->dragflag==DRAG_DRAGGING) {    /* not while dragging! */
              /* this is here so we can ignore the first release event after
                 dragging. otherwise, it would clear the selection before we
                 can send it to the XDND recipient. il->dragflag will be reset
                 below. */
              evas_damage_rectangle_add(evas,0,0,il->vis_w,il->vis_h); }

            else if(!(ev->state&(MOD_SELECT_MULTI|MOD_SELECT_RANGE))) { /* single click */
              if(no<0) {                       /* on background, clear sel */
                int dac=0;  /* how many icons did we deselect? */

                while(j) {  /* reset selection */
                  if(j->state&IE_SEL) {
                    dac++;
                    evas_icon_set_state(il,j,j->state&(~IE_SEL)); }
                  j=j->next; }

                /* if we're in desktop-mode and no selection was cleared, pass
                   through single clicks */
                if(!dac&&(prvs_click!=EV_DOUBLE)&&noir_sess_get_desktop(ns)&&!(ev->state&MOD_DESKTOP))
                  gevas_desktop_click(ev->x_root,ev->y_root,ev->button,ev->state,0L,ev->time); }

              else {                           /* on icon, make it the sel */
                while(j) {
                  if(i==j)
                    evas_icon_set_state(il,j,j->state|IE_SEL);
                  else
                    evas_icon_set_state(il,j,j->state&(~IE_SEL));
                  j=j->next; }}}

            else if(no>=0) {                   /* single click w/ modifiers */
              if(ev->state&MOD_SELECT_MULTI)   /* toggle in selection */
                evas_icon_set_state(il,i,(i->state&IE_SEL)?(i->state&(~IE_SEL)):(i->state|IE_SEL));
              else {                           /* make range */
                /* j=il->icons; i=icon-clicked */
                int lo=-1,hi=-1;
                /* find first select icon (i, or any prvsly selected icon) */
                while(j&&!(j->state&IE_SEL)&&!(i==j))
                  j=j->next;
                lo=j->index;

                /* newly-clicked icon is new start of range ("lo"); scan for
                   next selected icon from there onwards and make it "hi" */
                if(i==j) {
                  j=j->next;
                  while(j&&!(j->state&IE_SEL))
                    j=j->next;
                  hi=j?j->index:lo; }
                /* newly-clicked icon is new end of range ("hi"); scan for last
                   selected icon before newly-clicked one and make it "lo" */
                else {
                  j=j->next;
                  while(j&&(j!=i)) {
                    if(j->state&IE_SEL)
                      lo=j->index;
                    j=j->next; }
                  hi=i?i->index:lo; }

                /* select all icons between "lo" and "hi" */
                j=il->icons;
                while(j) {
                  if((j->index>=lo)&&(j->index<=hi))
                    evas_icon_set_state(il,j,j->state|IE_SEL);
                  j=j->next; }}}
            else
              ret=FALSE;

            il->dragflag=DRAG_NONE; }

          if(ev->type!=GDK_BUTTON_PRESS)
            icon_list_get_selection(ns,il);

          evas_render(evas); }
        break;
      default:
        E(0,"evas_clicked: button %d\n",ev->button);
        break; }}}

ffae:
  prvs_click=t;

  if(file)
    free(file);
  return ret; }






/******************************************************************************/



/**
 * constructor for the canvas-specific part of icon-list
 * @return a GTK-widget
 * @param ns the session
 * @param name the name for this icon-view
 * @param wpad horizontal padding between icons
 * @param hpad vertical padding   between icons
 * @param flags flags for icon-list (reserved, 0)
 */
GtkWidget *gevas_icon_list_new(noir_sess *ns,const char *name,int wpad,int hpad,int flags) {
  icon_list     *il;
  GtkWidget     *gevas=NULL;
  Evas          *evas;

  if(!(gevas=gevas_new(prop_get_string(prop_bag_prop_get(props_global,"evas-engine"))))||
     !(il=icon_list_new(name,(void *)((noir_theme *)ns->theme),(void *)gevas,wpad,hpad,flags)))
    return NULL;

  gtk_widget_set_name(gevas,name);

  if(!target_list)
    target_list=gtk_target_list_new((const GtkTargetEntry *)target,targets);

  il->sess=ns;
  il->sortflags=&ns->sortflags;

  gtk_widget_set_usize(gevas,il->width=256,200);
  gevas_set_size_request_x(gevas,200);
  gevas_set_size_request_y(gevas,200);

  g_object_set_data(G_OBJECT(gevas),"user_data",il);

  if(ns->start_in==START_IN_ICONVIEW)
    gtk_widget_show(gevas);

  gtk_signal_connect(GTK_OBJECT(gevas),"size-allocate",     GTK_SIGNAL_FUNC(gevas_icon_list_resized),ns);

  g_signal_connect(G_OBJECT(gevas), "button_press_event",   G_CALLBACK(gevas_clicked),    (gpointer)ns);
  g_signal_connect(G_OBJECT(gevas), "button_release_event", G_CALLBACK(gevas_clicked),    (gpointer)ns);
  g_signal_connect(G_OBJECT(gevas), "motion-notify-event",  G_CALLBACK(gevas_motioned),   (gpointer)ns);
  g_signal_connect(G_OBJECT(gevas), "expose-event",         G_CALLBACK(gevas_exposed),    (gpointer)ns);
  g_signal_connect(G_OBJECT(gevas), "destroy",              G_CALLBACK(gevas_destroyed),  (gpointer)il);
  g_signal_connect(G_OBJECT(gevas), "key-press-event",      G_CALLBACK(gevas_keyed),      (gpointer)ns);

  g_signal_connect(G_OBJECT(gevas), "scroll-event",         G_CALLBACK(gevas_scrolled),   (gpointer)ns);

  GTK_WIDGET_SET_FLAGS(gevas,GTK_CAN_FOCUS);

  if((evas=gevas_get_evas((GtkgEvas *)gevas))) {
    evas_image_cache_set(evas,2048*1024);
    evas_font_cache_set(evas,256*1024); }
  else
    E(0,"possibly fatal, couldn't get evas from newly-created gevas!?\n");

  return GTK_WIDGET(gevas); }



/******************************************************************************/
