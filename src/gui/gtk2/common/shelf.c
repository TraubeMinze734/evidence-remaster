/** @file  shelf.c
 *  @brief implements shelf (icon-view in icon_view.c)
 *  independent of the actual icon-list (evas/icon_list or gnome-icon-list)
 *  used.
 */

#include "config.h"

#include "thumbnailer/thumbnail.h"

#include "gui/common/session.h"
#include "gui/common/session_private.h"

#if defined(HAVE_GNOMECANVAS)||defined(HAVE_EVAS2)
#  include "gui/gtk2/icon_view/icon_view.h"
#  include "gui/gtk2/tree_view/tree_view.h"
#  include "gui/gtk2/common/shelf.h"
#  include "gui/gtk2/common/dnd.h"
#  include "gui/common/icon_path.h"
#  include "gui/common/alert.h"         /* form_alert() */
#  include "file.h"          /* file_get_mime(), file_exists() */
#  include "gui/gtk2/common/tree/tree.h"
#  include "file_lcl.h"      /* file_exists(), file_if_local() */
#  include "setup.h"         /* get_path_file(), get_setup_file() */
#  include "util.h"          /* estring_* */
#  include "properties.h"
#  include "gui/efl/common/theme.h"

#  include <stdio.h>
#  include <string.h>
#  include <sys/stat.h>
#  include <stdlib.h>

#  ifdef HAVE_GNOMECANVAS
#    include "gui/common/menu.h" /* menu_root_context(), menu_file_context() */
#    include <gtk/gtk.h>
#    include "gui/gtk2/icon_view/gnome-icon-list.h"
#    include "icon.h"               /* pixbuf_get_by_path_and_resize() */
/* #    include "info.h" */              /* info_action_menu() */
#    include "tooltip.h"            /* tooltip_show_for() */
#  else
#    include "gui/common/icon_list.h"
#    include "gui/gtk2/icon_view/gevas_bind.h"
#    include "gui/gtk2/icon_view/gevas_light.h"   /* GtkgEvas */

typedef enum { SH_BG, SH_PADX, SH_PADY, SH_PADW, SH_PADH } sh_tag_type;
#  endif

#  define MODULE "shelf"



static int shelf_load(noir_sess *ns) {
  noir_shelf *sv=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);
  char       *fn=NULL;                          /* replay saved shelf */
  int         ret=NOIR_IGNORED;

  esprintf(&fn,"%s/shelf",path_setup);
  if(fn) {
    char       *sd,*t;
    const char *h;

    if((ret=file_load(fn,&sd))!=NOIR_SUCC)
      E(0,"error #%d loading shelf...\n",ret);
    else {
      ret=0;
      t=sd;
      while((h=dnd_get_file_from_drop(&t))) {
        if(strlen(h)) {
          const char *mime=file_get_mime(h);
          const char *name=file_name(h);
          char       *thumb=NULL;
          file_info  *fi;

          /* do we even want thumbnails? */
          if(ns->thumbnails_in_shelf) {
            /* do we have the file in the store? */
            if((fi=file_info_fetch(NULL,h)))
              /* got it. if it has a thumb-nail, use that. */
              thumb=(char *)file_info_get_thumbfile(fi);

            /* do we have a thumb yet? */
            if(!thumb)
              /* no. maybe, there's still one in the disk-cache? */
              thumbnail_find_for(h,&thumb); }

#  ifdef HAVE_GNOMECANVAS
        { GdkPixbuf  *pb=NULL;
          const char *tag=file_info_get_tag(fi),*mime=file_info_get_mime(fi);
          if(ns->thumbnails_in_shelf) {
            if(!(pb=pixbuf_get_by_path_and_resize(tag,ICON_SIZE_IN_LIST,GDK_INTERP_BILINEAR)))
              pb=pixbuf_get_by_path_and_resize(icon_path_get_always(tag,mime),ICON_SIZE_IN_LIST,GDK_INTERP_BILINEAR); }
          else
            pb=pixbuf_get_by_path_no_resize(icon_path_get_always(tag,mime));
          if(pb)
            gnome_icon_list_append_pixbuf(GNOME_ICON_LIST(sv->shelf_view),pb,h,name); }
#  else
          icon_list_icon_append(ICON_LIST_GET_FROM_WIDGET(sv->shelf_view),
                                icon_path_get_always(h,mime),
                                h,
                                name,
                                thumb,
                                NULL,NULL,TRUE);
#  endif
          ret++;
          E(2,"adding \"%s\" to shelf...\n",h); }}
      free(sd); }
    free(fn); }
  else
    ret=NOIR_NOFILE;
  return ret; }



#  ifdef HAVE_GNOMECANVAS

/**
 * shelf/GNOME: callback for icon-selection events
 * @param il    the icon-list
 * @param no    the index of the icon that was selected
 * @param ev    the event that led to the selection of this icon
 * @param data  the session
 */
#    ifdef HAVE_GNOMECANVAS
static void shelf_selected(GnomeIconList *il,gint no,GdkEvent *event,gpointer data) {
#    else
static void shelf_selected(icon_list *il,gint no,GdkEvent *event,gpointer data) {
#    endif
  noir_sess *ns=(noir_sess *)data;

  if(!ns)
    return;

  if(event) {
#    ifdef HAVE_GNOMECANVAS
    char *file=strdup(gnome_icon_list_get_icon_filename(il,no));
#    else
    char *file=strdup(ICON_GET_FILENAME(icon_list_get_icon_by_index(ICON_LIST_GET_FROM_WIDGET(il),no)));
#    endif
    int is_dir;
    if(((GdkEventButton *)event)->button==1) {
      if((is_dir=file_exists(file))>0) {
        if(event->type==GDK_2BUTTON_PRESS) {
          if(S_ISDIR(is_dir)) {
            E(1,"focusing on %s\n",file);
            noir_sess_focus((noir_sess *)data,file); }
          else
            file_activate(ns,file,NULL,NULL); }
        else if(((GdkEventButton*)event)->button==3)
          menu_file_context(ns,noir_sess_get_cwd(ns),file); }
      else
        E(0,"file \"%s\" (%d) not found!?\n",file,no);
      free(file); }}}



/**
 * shelf/GNOME: callback for mouse-button events
 * @param widget  reserved
 * @param event   the button-event
 * @param data    the session
 */
static gboolean shelf_clicked(GtkWidget *widget,GdkEventButton *event,gpointer data) {
  noir_sess *ns=(noir_sess *)data;

  if(!ns)
    return FALSE;

  if(event&&(event->type==GDK_BUTTON_RELEASE)&&(((GdkEventButton *)event)->button==2)) {
    noir_shelf *sv=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);
#    ifdef HAVE_GNOMECANVAS
    int no=gnome_icon_list_get_icon_at(GNOME_ICON_LIST(sv->shelf_view),(gint)((GdkEventButton *)event)->x,(gint)((GdkEventButton *)event)->y);
#    else
    int no=icon_list_get_index_at(ICON_LIST_GET_FROM_WIDGET(sv->shelf_view),(gint)((GdkEventButton *)event)->x,(gint)((GdkEventButton *)event)->y);
#    endif
    if(no>=0) {
#    ifdef HAVE_GNOMECANVAS
      char *file=strdup(gnome_icon_list_get_icon_filename(GNOME_ICON_LIST(sv->shelf_view),no));
#    else
      char *file=strdup(ICON_GET_FILENAME(icon_list_get_icon_by_index(ICON_LIST_GET_FROM_WIDGET(sv->shelf_view),no)));
#    endif
      if(file) {
        ns->tooltip_x=(gint)((GdkEventButton *)event)->x_root;
        ns->tooltip_y=(gint)((GdkEventButton *)event)->y_root;
        tooltip_show_for(ns,file);
        free(file); }}
    else if((event->button!=2)&&(event->type==GDK_BUTTON_RELEASE)) {
      E(0,"root menu for shelf...\n");
      menu_root_context(ns,NULL,event->button); }}
  return FALSE; }

#  endif



/****************************************************************************/



/**
 * DND: drop source sent data. act upon them.
 * @param widget     the widget
 * @param dc         the drag-context
 * @param x          where the data was dropped
 * @param y          where the data was dropped
 * @param selection  the received data
 * @param info       type of the data as per GtkTargetList
 * @param timestamp  when the event happened
 * @param data       session
 */
static void shelf_drag_data_received(GtkWidget *widget,GdkDragContext *dc,gint x,gint y,GtkSelectionData *selection,guint info,guint timestamp,gpointer data) {
  noir_sess  *ns=data;
  gchar      *d,*a;
  const char *mime;
  char       *file,*name;
  noir_shelf *sv=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);

  if(!ns||!dc)
    return;

  if(!selection||(selection->length<1)) {
    E(0,"shelf_drag_data_received: no data (%p,%d)\n",selection,selection?selection->length:-1234); }
  else if(selection->format==8) {
#  ifdef HAVE_GNOMECANVAS
    GdkPixbuf *pb;
    int no=gnome_icon_list_get_icon_at(GNOME_ICON_LIST(sv->shelf_view),x,y);
#  else
    int no=icon_list_get_index_at(ICON_LIST_GET_FROM_WIDGET(sv->shelf_view),x,y);
#  endif
    gchar *_target=gdk_atom_name(selection->target);

    if(!(a=d=malloc(selection->length+1)))
      return;

    strncpy(a,selection->data,selection->length);
    a[selection->length]='\0';

    if(no<0) {
      while((file=dnd_get_file_from_drop(&d))) {
        mime=file_get_mime(file);

        E(1,"shelf_drag_data_received: add (%s => %s) \"%s\"\n",_target,mime?mime:"<unknown>",file);

#  ifdef HAVE_GNOMECANVAS
        pb=NULL;
        if(ns->thumbnails_in_shelf) {
          if(!(pb=pixbuf_get_by_path_and_resize(file,ICON_SIZE_IN_LIST,GDK_INTERP_BILINEAR)))
            pb=pixbuf_get_by_path_and_resize(icon_path_get_always(file,mime),ICON_SIZE_IN_LIST,GDK_INTERP_BILINEAR); }
        else
          pb=pixbuf_get_by_path_no_resize(icon_path_get_always(file,mime));
        if(pb) {
          name=(name=strrchr(file,'/'))?name+1:file;
          gnome_icon_list_append_pixbuf(GNOME_ICON_LIST(sv->shelf_view),pb,file,name); }
#  else
        name=(name=strrchr(file,'/'))?name+1:file;
        icon_list_icon_append(ICON_LIST_GET_FROM_WIDGET(sv->shelf_view),icon_path_get_always(file,mime),file,name,ns->thumbnails_in_shelf?file:NULL,NULL,NULL,TRUE);
#  endif
        }}
    else {
#  ifdef HAVE_GNOMECANVAS
      char *dt=gnome_icon_list_get_icon_filename(GNOME_ICON_LIST(sv->shelf_view),no);
#  else
      char *dt=strdup(ICON_GET_FILENAME(icon_list_get_icon_by_index(ICON_LIST_GET_FROM_WIDGET(sv->shelf_view),no)));
#  endif
      if(dt&&*dt)
        file_drop(ns,dc->action,info,a,dt);
      else
        E(0,"shelf_drag_data_received: breakage in 'use'!\n"); }
    g_free(a); }
  else if(selection->format==16) {                       /* GIMP colour drop */
    char buf[8];
    guint16 *color_vals=(guint16 *)selection->data;
    guchar r,g,b;
    r=color_vals[0]>>8;  g=color_vals[1]>>8;  b=color_vals[2]>>8;
    snprintf(buf,8,"#%02x%02x%02x",r,g,b);
    prop_bag_prop_set(((noir_theme *)ns->theme)->data,MODULE "backgroundtintcolour",buf);
    E(0,"setting " MODULE "-background-tint-colour to %s...\n",buf); }}



#  if defined(HAVE_GNOMECANVAS)||defined(HAVE_EVAS2)

/**
 * DND: drop target asks for data. collect and send.
 * @param widget         the widget that was dragged from, or NULL
 * @param dc             the drag-context
 * @param selectionData  the selection we're to set the data on
 * @param info           type of the data as per GtkTargetList
 * @param timestamp      when the event happened
 * @param data           session
 */
static void shelf_drag_data_get(GtkWidget *widget,GdkDragContext *dc,GtkSelectionData *selectionData,guint info,guint timestamp,gpointer data) {
  noir_sess      *ns=data;
  action_context *ac=NULL;

  if(!ns)
    return;

  if(selectionData) {
    gchar *reply=NULL,*file;
    noir_shelf *sv=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);

    ac=action_context_new(noir_sess_get_serial(ns),noir_sess_get_cwd(ns));

#  ifdef HAVE_GNOMECANVAS
    GList *l;
    for(l=gnome_icon_list_get_selection(GNOME_ICON_LIST(sv->shelf_view));l;l=l->next) {
      file=gnome_icon_list_get_icon_filename(GNOME_ICON_LIST(sv->shelf_view),GPOINTER_TO_INT(l->data));
#  elif defined(HAVE_EVAS2)
    icon_list *il=ICON_LIST_GET_FROM_WIDGET(sv->shelf_view);
    icon *i=il->icons;

    while(i) {
      if(i->state&IE_SEL) {
        file=ICON_GET_FILENAME(i);
#  endif

      action_file_new(ac,file);

      if((info==DRAGDROP_TARGET_STRING)||(info==DRAGDROP_TARGET_TEXT))
        reply=dnd_send_string(reply,file);
      else if((info==DRAGDROP_TARGET_NETSCAPE_URL)||(info==DRAGDROP_TARGET_URL))
        reply=dnd_send_URL(reply,file);
#  ifdef HAVE_EVAS2
      }
      i=i->next; }

 /* gdk_drag_context_unref(il->dc); */
    il->dc=NULL;
#  else
    }
#  endif

    if(action_context_get_1st(ac)) {
      gtk_selection_data_set(selectionData,selectionData->target,8,reply,strlen(reply));
      file_drop_remember(ns,dc,ac); }
    else
      E(0,"got nothing to say\n");

    if(reply)
      free(reply); }}

#  else

/**
 * DND: drop target asks for data. collect and send.
 * @param widget         the widget that was dragged from, or NULL
 * @param dc             the drag-context
 * @param selectionData  the selection we're to set the data on
 * @param info           type of the data as per GtkTargetList
 * @param timestamp      when the event happened
 * @param data           session
 */
static void shelf_drag_data_get(GtkWidget *widget,GdkDragContext *dc,GtkSelectionData *selectionData,guint info,guint t,gpointer data) {
}

#  endif



/**
 * DND:
 * @return TRUE for success, FALSE for failure
 * @param widget         the widget that was dragged from, or NULL
 * @param dc             the drag-context
 * @param x              where the drop occurred
 * @param y              where the drop occurred
 * @param time           when the event happened
 * @param data           session
 */
static gboolean shelf_drop(GtkWidget *widget,GdkDragContext *dc,gint x,gint y,guint time,gpointer data) {
  noir_sess *ns=(noir_sess *)data;
  int no,move=FALSE,ms,ret=TRUE;
  GdkAtom ta;
  noir_shelf *sv;

  if(!ns||!dc)
    return FALSE;

  sv=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);

#  ifdef HAVE_GNOMECANVAS
  no=gnome_icon_list_get_icon_at(GNOME_ICON_LIST(sv->shelf_view),x,y);
#  else
  no=icon_list_get_index_at(ICON_LIST_GET_FROM_WIDGET(sv->shelf_view),x,y);
#  endif

  if(no<0)
    ret=move?FALSE:TRUE;
  else {
#  ifdef HAVE_GNOMECANVAS
    char *dt=gnome_icon_list_get_icon_filename(GNOME_ICON_LIST(sv->shelf_view),no);
#  else
    char *dt=strdup(ICON_GET_FILENAME(icon_list_get_icon_by_index(ICON_LIST_GET_FROM_WIDGET(sv->shelf_view),no)));
#  endif
    if(!dt||!*dt||!(ms=file_exists(dt))||(!S_ISDIR(ms)&&move))
      ret=FALSE; }

  gdk_drop_reply(dc,ret,time);

  if(ret)
    gtk_drag_get_data(widget,dc,ta=gtk_drag_dest_find_target(widget,dc,gtk_drag_dest_get_target_list(widget)),time);
  else
    form_alert(ns,"You can move items to a folder that is represented on the shelf,\nbut you cannot move items to the shelf nor to files on the shelf.");

  gtk_drag_finish(dc,ret,(move&&ret),time);

  return ret; }



/****************************************************************************/



/**
 * callback for "exposed"-event
 * the other views don't update our thumbnails for reasons of speed,
 * so if the directory-focus was changed in another view, we'll have
 * to get the thumbnails NOW.
 * @return FALSE
 * @param widget  the gEvas
 * @param ev      the GDK expose-event
 * @param data    the session
 */
gboolean shelf_exposed(GtkWidget *widget,GdkEventExpose *ev,gpointer data) {
  noir_sess *ns=(noir_sess *)data;
  if(ns) {
#  ifdef HAVE_EVAS2
    { icon_list *il=ICON_LIST_GET_FROM_WIDGET(widget);
      if(il) {
        gevas_bg_update(il,il->vis_w,il->vis_h);
        evas_obscured_rectangle_add(gevas_get_evas((GtkgEvas *)il->canvas),0,0,il->vis_w,il->vis_h);
        evas_obscured_clear((gevas_get_evas((GtkgEvas *)il->canvas)));
        if(GTK_WIDGET_MAPPED(widget)) {
          if(!il->bg&&il->theme&&((noir_theme *)il->theme)->bg_new) {
            noir_theme *theme=il->theme;
            gtk_widget_hide(widget); gtk_widget_show(widget);
            theme->bg_new(theme,il,theme->shelf_bg);
            shelf_load(ns);
            E(0,"delayed background creation %s\n",il->bg?"succeeded.":"failed!?"); }
          evas_render(gevas_get_evas((GtkgEvas *)il->canvas)); }}}
#  endif
  }
  return FALSE; }



#endif



/****************************************************************************/



/**
 * handle model-event
 * @return an error-code 
 * @param fi       the fs object this event is about
 * @param type     event-type
 * @param changed  what parts of 'fi' changed
 * @param data     the icon-view
 */
static int shelf_evt(file_info *fi,model_event type,file_flags changed,void *data) {
/*  noir_shelf *sv=(noir_shelf *)data;
  puts("SHELF-dst"); */
  return NOIR_IGNORED; }



/**
 * destruct shelf
 * @return an error-code
 * @param data  the shelf
 */
static int shelf_dst(void **data) {
  noir_shelf *sv=*((noir_shelf **)data);
  icon_list  *il;
  int         ret=NOIR_FAIL;

  if(sv&&(il=ICON_LIST_GET_FROM_WIDGET(sv->shelf_view))) {
    noir_sess *ns=il->sess;
    icon      *i=il->icons;
    char      *cwd=noir_sess_get_cwd(ns),*fn=NULL,*p;
    estring   *msg=estring_new(2048);

    esprintf(&fn,"%s/shelf",path_setup);

    if(!fn)
      goto destall;

    if(!cwd)
      cwd="";

    while(i) {
      if((p=ICON_GET_FILENAME(i))) {
        E(2,"shelf-icon: { \"%s\", \"%s\" };\n",cwd,p);
        estring_appendf(msg,"%s\n",p); }
      i=i->next; }

    if(estring_length(msg)&&(p=ESTRING_GET_CSTRING(msg)))
      ret=file_save(fn,-1,estring_length(msg),p);

    free(fn);

  destall:
    estring_free(msg,TRUE);

    file_partial(NOIR_SUCC,&ret);

    theme_canvas_destall(il);
    memset(sv,0,sizeof(noir_shelf));
    free(sv); }

  return ret; }



#ifdef HAVE_EVAS2

/**
 * listen for changes in our numeric properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int shelf_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  long           val=prop_get_int(e);
  const char    *key=prop_get_key(e);
  icon_list     *il=(icon_list *)data;

  switch(tag) {
    case SH_PADX:
      il->xpad=val;
      icon_list_layout_all(il);
      break;
    case SH_PADY:
      il->ypad=val;
      icon_list_layout_all(il);
      break;
    case SH_PADW:
      il->wpad=val;
      icon_list_layout_all(il);
      break;
    case SH_PADH:
      il->hpad=val;
      icon_list_layout_all(il);
      break;
    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld, %p)\n",key,val,data);

  return NOIR_SUCC; }



/**
 * listen for changes in our string-properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int shelf_str_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  char          *val=prop_get_string(e);
  const char    *key=prop_get_key(e);
  noir_sess     *ns=(noir_sess *)data;
  noir_theme    *theme=((noir_theme *)ns->theme);
  noir_shelf    *sh=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);
  icon_list     *il=ICON_LIST_GET_FROM_WIDGET(sh->shelf_view);
  char          *x=NULL;

  if(val) {
    void *o=il->bg;

    if((theme->bg_new==NULL)||(theme->bg_dst==NULL))
      return NOIR_FAIL;

    if(val&&*val&&!file_exists(val)) {
      const char *y=file_if_local(val);
      if(y==NULL)   /* not a local file */
        return NOIR_NOTSUPP;
      if(file_if_absolute(y))  /* abs path, but file_exists() failed */
        return NOIR_NOTFOUND;
      if(!(x=get_path_file(NULL,y))) {
        if(!(x=get_setup_file(y)))
          return NOIR_NOTFOUND; }
      val=x; }

    E(1,"(%s->%s::=\"%s\", %p)\n",
      ((noir_theme *)theme)->name,key,val?val:"<null>",data);

    if((GTK_WIDGET_MAPPED(sh->shelf_view))&&theme->bg_new&&!theme->bg_new(theme,il,val))
      il->bg=o;
    else {
      if(theme->shelf_bg)
        free(theme->shelf_bg);
      theme->shelf_bg=strdup(val);
      if(theme->bg_dst)
        theme->bg_dst(&o); }

    E(1,"(%s->%s::=\"%s\", %p): $%p/$%p\n",
      ((noir_theme *)theme)->name,key,val?val:"<null>",data,il->bg,o);

    gevas_icon_list_force(il);

    if(x)
      free(x);

    return NOIR_SUCC; }

  return NOIR_IGNORED; }
#endif



/**
 * create shelf in given session
 * @return an error-code
 * @param ns  the session
 */
int shelf_init(noir_sess *ns) {
  noir_shelf *sv=(noir_shelf *)malloc(sizeof(noir_shelf));
  if(!sv)
    return NOIR_OOM;
  memset(sv,0,sizeof(noir_shelf));

  if(!ns->without_shelf) {
#ifdef HAVE_GNOMECANVAS
    GtkAdjustment  *va=(GtkAdjustment*)gtk_adjustment_new(0,0,100,1,10,10);
    GtkWidget      *shelf=gnome_icon_list_new(ICON_SIZE_IN_LIST,va,0);
    GtkWidget      *scroll=gtk_scrolled_window_new(NULL,va);

    gnome_icon_list_set_selection_mode(GNOME_ICON_LIST(shelf),GTK_SELECTION_MULTIPLE);
    gnome_icon_list_set_separators(GNOME_ICON_LIST(shelf),"-. _");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll),shelf);

    gtk_signal_connect(GTK_OBJECT(shelf),"button_press_event",G_CALLBACK(shelf_clicked),(gpointer)ns);
    gtk_signal_connect(GTK_OBJECT(shelf),"button_release_event",G_CALLBACK(shelf_clicked),(gpointer)ns);
    gtk_signal_connect(GTK_OBJECT(shelf),"select-icon",G_CALLBACK(shelf_selected),(gpointer)ns);

    gtk_drag_source_set(shelf,GDK_BUTTON1_MASK|GDK_SHIFT_MASK|GDK_CONTROL_MASK,target,targets,GDK_ACTION_LINK|GDK_ACTION_COPY|GDK_ACTION_MOVE);

    sv->shelf_view=shelf;
    sv->shelf_scroll=scroll;
#elif defined(HAVE_EVAS2)
    GtkWidget  *wtoy,*shelf;
    icon_list  *il;
    noir_theme *t;

    if(!(wtoy=gevas_icon_list_new(ns,MODULE, 24,16, 0)))
      return NOIR_FAIL;

    il=ICON_LIST_GET_FROM_WIDGET(wtoy);

/*  ((noir_theme *)ns->theme)->bg_new(((noir_theme *)ns->theme),il,((noir_theme *)ns->theme)->shelf_bg); */

    shelf=sv->shelf_view=il->canvas;
    sv->shelf_scroll=wtoy;

    t=(noir_theme *)il->theme;
    if(t->sb_new) {
      if(!il->sb_v)
	il->sb_v=t->sb_new(t,il->canvas);
      t->sb_set(il->canvas,(icon *)il->sb_v,il->width,il->height,il->vis_h,il->sy); }
#else
    sv->shelf_view=NULL;
    sv->shelf_scroll=NULL;
#endif

#if defined(HAVE_EVAS2)||defined(HAVE_GNOMECANVAS)
    gtk_signal_connect(GTK_OBJECT(shelf),"expose-event",G_CALLBACK(shelf_exposed),(gpointer)ns);

    gtk_signal_connect(GTK_OBJECT(shelf),"drag-drop",G_CALLBACK(shelf_drop),(gpointer)ns);
    gtk_signal_connect(GTK_OBJECT(shelf),"drag_data_get",G_CALLBACK(shelf_drag_data_get),(gpointer)ns);
    gtk_signal_connect(GTK_OBJECT(shelf),"drag_data_received",G_CALLBACK(shelf_drag_data_received),(gpointer)ns);
    gtk_signal_connect(GTK_OBJECT(shelf),"drag_data_delete",G_CALLBACK(file_drop_delete_from_iconlist),(gpointer)ns);
    gtk_signal_connect(GTK_OBJECT(shelf),"drag-end",G_CALLBACK(file_drop_free),(gpointer)ns);

    gtk_drag_dest_set(shelf,GTK_DEST_DEFAULT_HIGHLIGHT|GTK_DEST_DEFAULT_MOTION,target,targets,GDK_ACTION_LINK|GDK_ACTION_COPY|GDK_ACTION_MOVE);

    gtk_widget_set_name(sv->shelf_scroll,"shelf_scroll");
    gtk_widget_set_name(sv->shelf_view,"shelf");
#endif
    noir_sess_client_register_full(ns,CLIENT_SHELF,(void *)sv,shelf_evt,shelf_dst,NULL);

#ifdef HAVE_EVAS2
    prop_bag_prop_default(((noir_theme *)ns->theme)->data,MODULE "-pad-x","12",-64,64,1);
    prop_bag_prop_default(((noir_theme *)ns->theme)->data,MODULE "-pad-y", "8",-64,64,1);
    prop_bag_prop_default(((noir_theme *)ns->theme)->data,MODULE "-pad-w","24",-32,32,1);
    prop_bag_prop_default(((noir_theme *)ns->theme)->data,MODULE "-pad-h","16",-32,32,1);

#  define LISTENER "shelf{evas}"

    prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE,shelf_str_listener,SH_BG,ns);
    prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE "-pad-x",shelf_num_listener,SH_PADX,il);
    prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE "-pad-y",shelf_num_listener,SH_PADY,il);
    prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE "-pad-w",shelf_num_listener,SH_PADW,il);
    prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE "-pad-h",shelf_num_listener,SH_PADH,il);
#endif
  }

  return NOIR_SUCC; }



/**
 * drop shelf in given session
 * icons are destroyed in gevas' call-back (cf. gevas_icon_list_new())
 * @return an error-code
 * @param ns  the session
 */
int shelf_exit(noir_sess *ns) {
  return NOIR_SUCC; }
