/** @file  icon_view.c
 *  @brief implements icon-view (shelf in shelf.c)
 *  independent of the actual icon-list (evas/icon_list or gnome-icon-list)
 *  used.
 */

#include "config.h"
#include <stdint.h>     /* intptr_t for icon-index -> pointer casts */

#ifdef HAVE_BTRACE
#  include <btrace.h>
#endif

#include "gui/common/session_private.h"
#include "gui/common/icon_list.h"
#include "gui/common/menu.h"     /* menu_file_context(), menu_root_context() */
#include "gui/common/icon_path.h"
#include "thumbnailer/thumbnail.h"      /* thumbnail_enqueue() */
#include "gui/common/tooltip.h"     /* info_tooltip_cancel() */

#include "gui/gtk2/common/dnd.h"
#include "gui/gtk2/common/gtk_compat.h"

#include "gui/gtk2/common/tree/tree.h"

#include "gui/gtk2/tree_view/tree_view.h"

#include "mime.h"
#include "file.h"
#include "file_lcl.h"
#include "util.h"        /* now() */
#include "setup.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include "icon_view.h"
#include "gui/efl/common/theme.h"

#if defined(HAVE_EVAS2)
#  include "gui/efl/common/evas_macros.h"
#  include "gui/efl/icon_view/evas_bind.h"
#  include "gevas_light.h"
#  include "gevas_bind.h"
#endif

/* ### FIXME */
#define DIR_PARENT ".."



#define MODULE "icon-view"

#define IV_UPDATE_EVERY 2000

#ifdef HAVE_EVAS2
typedef enum { IV_BG, IV_PADX, IV_PADY, IV_PADW, IV_PADH, IV_SPATIAL } iv_tag_type;
#endif



/****************************************************************************/



/**
 * set session's selection in icon-view
 * @return an error-code
 * @param data  the session
 */
int icon_view_set_selection(void *data) {
#ifdef HAVE_EVAS2
  noir_sess     *ns=(noir_sess *)data;
  noir_iconview *iv;
  icon_list     *il;

  if(!ns)
    return NOIR_FAIL;

  if(!(iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW))||
     !iv->icon_view) {
    E(0,"cannot set selection on non-existant icon-view...\n");
    return NOIR_FAIL; }

  if((il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view)))
    return icon_list_set_selection(il);

  E(0,"icon-view has no icon-list!?\n");
  return NOIR_FAIL;

#else
  return NOIR_IGNORED;
#endif
}




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
static gboolean iv_bg_created=FALSE;   /* one-shot startup fix scheduled */

static gboolean iv_startup_bg_fix(gpointer data) {
  icon_list *il=(icon_list *)data;
  noir_theme *theme;
  if(!il||!(theme=il->theme))
    return FALSE;
  /* runs once, from the idle loop AFTER the startup draw cycle: at the
     time of the very first draw the widget allocation is often not final
     yet, so creating the background and laying out the icons from inside
     the draw handler left a grey, misplaced view that only a manual
     window resize would heal -- the resize was merely the first event
     that re-ran the layout with the real dimensions. */
  theme->bg_new(theme,il,theme->iconview_bg);
  gevas_icon_list_force(il);          /* re-layout at the final size */
  gtk_widget_queue_draw(GTK_WIDGET(il->canvas));
  return FALSE; }

#if GTK_CHECK_VERSION(3,0,0)
gboolean icon_view_exposed(GtkWidget *widget,cairo_t *cr,gpointer data) {
#else
gboolean icon_view_exposed(GtkWidget *widget,GdkEventExpose *ev,gpointer data) {
#endif
#if defined(HAVE_EVAS2)
  noir_sess *ns=(noir_sess *)data;
  if(ns) {
#ifdef HAVE_EVAS2
    { icon_list *il=ICON_LIST_GET_FROM_WIDGET(widget);
      if(il&&il->canvas) {
        gevas_bg_update(il,il->vis_w,il->vis_h);
        evas_obscured_rectangle_add(gevas_get_evas((GtkgEvas *)il->canvas),0,0,il->vis_w,il->vis_h);
        evas_obscured_clear((gevas_get_evas((GtkgEvas *)il->canvas)));
        if(gtk_widget_get_mapped(widget)) {
          /* the 2005 code re-ran the delayed background creation on
             EVERY draw until it succeeded; when it never succeeded
             (pseudo-transparency without wallpaper) the hide/show/
             grab_focus dance in it queued ever more draws and fed the
             GTK3 repaint storm at 100% CPU.  now it runs ONCE, as an
             idle after the startup draw cycle, when the allocation is
             final; bg_new() failing then simply means no background. */
          if(!il->bg&&!iv_bg_created) {
            iv_bg_created=TRUE;
            g_idle_add(iv_startup_bg_fix,il); }
          { GtkgEvas *gv=GTK_GEVAS(il->canvas);
            gv->in_paint++;              /* this render was GTK's idea, too -- */
            evas_render(gevas_get_evas(gv));  /* don't answer it with a queue_draw */
            gv->in_paint--; } }}}
#endif
  }
#endif
  return FALSE; }



/****************************************************************************/



#if defined(HAVE_EVAS2)

/**
 * get path at given position
 * @param ns  the session
 * @param x   mouse-x
 * @param y   mouse-y
 */
static char *icon_path_at(noir_sess *ns,int x,int y) {
  noir_iconview *iv;
  int            no;
#ifdef HAVE_EVAS2
  icon_list *il;
#endif

  if(!ns||!(iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW)))
    return NULL;

  il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view);
  no=il?icon_list_get_index_at(il,x,y):-1;

  if(no<0)                                          /* dropped on background */
    return ns->cwd;
  else {                                                  /* dropped on icon */
#if defined(HAVE_EVAS2)
    icon *i=(il>=0)?icon_list_get_icon_by_index(il,no):NULL;
    return ICON_GET_FILENAME(i);
#endif
  }}



/**
 * DND: drag completed, negotiate content-type.
 * @param widget     the widget
 * @param dc         the drag-context
 * @param x          where the data was dropped
 * @param y          where the data was dropped
 * @param timestamp  when the event happened
 * @param data       session
 */
gboolean icon_drag_drop(GtkWidget *widget,GdkDragContext *dc,gint x,gint y,guint timestamp,gpointer data) {
  return dnd_drag_drop(widget,dc,timestamp,icon_path_at(data,x,y)); }



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
static void icon_drag_data_received(GtkWidget *widget,GdkDragContext *dc,gint x,gint y,GtkSelectionData *selection,guint info,guint timestamp,gpointer data) {
  noir_sess     *ns=(noir_sess *)data;
  gchar         *d,*a;
  noir_iconview *iv;

  if(!ns)
    return;

  iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);

  if(!selection||(evis_seldata_length(selection)<1)) {
    E(0,"no data (%p,%d)\n",selection,selection?evis_seldata_length(selection):-1234); }
  else if(evis_seldata_format(selection)==8) {
    icon_list *il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view);
    int        no;
    void      *rp;

    if((no=evas_ext_evt(il,x,y,1,EV_DROPPED,EV_MOD_NONE,ns,NULL,&rp))!=NOIR_NOTFOUND) {
      file_info  *fi=(file_info *)rp;
      const char *tag=file_info_get_tag(fi);
      E(4,"dropped on extension: %d for \"%s\".\n",no,tag?tag:"<NULL>");
      if((no==NOIR_SUCC)&&tag)
        file_drop(ns,evis_drag_action(dc),info,evis_seldata_data(selection),tag);
      return; }
    else
      no=il?icon_list_get_index_at(il,x,y):-1;

    if(info==DRAGDROP_TARGET_XDS) {
#ifdef WITH_XDS
      char *r=evis_seldata_data(selection);
      switch(*r) {
        case 'S':
          E(0,"XDS: success!\n");
          break;
        case 'F':
          E(0,"XDS: the application can't save in remote location, it's up to us.\n");
          xds_set_prop(dc,"");  /* we can't remote-safe, neither -- for now */
          break;
        case 'E':
          E(0,"XDS: the application refuses to save there.\n");
          break;
        default:
          E(0,"XDS: invalid op-code '%c'!?\n",*r);
          break; }
#endif
      return; }

    if(!(a=d=malloc(evis_seldata_length(selection)+1)))
      return;

    strncpy(a,evis_seldata_data(selection),evis_seldata_length(selection));
    a[evis_seldata_length(selection)]='\0';

E(0,"%s |%s|\n",target[info].target,a);

    if(no<0)                                        /* dropped on background */
      file_drop(ns,evis_drag_action(dc),info,a,ns->cwd);
    else {                                                /* dropped on icon */
#if defined(HAVE_EVAS2)
      icon *i=(il>=0)?icon_list_get_icon_by_index(il,no):NULL;
      char *dt=ICON_GET_FILENAME(i);
#endif

      if(dt&&*dt)
        file_drop(ns,evis_drag_action(dc),info,a,dt);
      else
        E(0,"breakage in 'use'!\n"); }
    free(a); }

  /* GIMP colour drop */
  else if((evis_seldata_format(selection)==16)&&(info==DRAGDROP_TARGET_COLOUR)) {
    char     buf[8];
    guint16 *color_vals=(guint16 *)evis_seldata_data(selection);
    guchar   r,g,b;

    r=color_vals[0]>>8;
    g=color_vals[1]>>8;
    b=color_vals[2]>>8;

    snprintf(buf,8,"#%02x%02x%02x",r,g,b);
    prop_bag_prop_set(((noir_theme *)ns->theme)->data,MODULE  "backgroundtintcolour",buf);
    E(0,"setting " MODULE "-background-tint-colour to %s...\n",buf); }}



/**
 * DND: drop target asks for data. collect and send.
 * @param widget         the widget that was dragged from, or NULL
 * @param dc             the drag-context
 * @param selectionData  the selection we're to set the data on
 * @param info           type of the data as per GtkTargetList
 * @param timestamp      when the event happened
 * @param data           session
 */
static void icon_drag_data_get(GtkWidget *widget,GdkDragContext *dc,GtkSelectionData *selectionData,guint info,guint timestamp,gpointer data) {
  noir_sess      *ns=(noir_sess *)data;
  noir_iconview  *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
  action_context *ac=NULL;

  if(!ns)
    return;

  if(selectionData) {
    gchar *file,*reply=NULL;

    ac=action_context_new(noir_sess_get_serial(ns),noir_sess_get_cwd(ns));

#if defined(HAVE_EVAS2)
    icon_list *il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view);
    icon      *i=il->icons;

    while(i) {
      if(i->state&IE_SEL) {
        file=ICON_GET_FILENAME(i);
#endif

      action_file_new(ac,file);

      if((info==DRAGDROP_TARGET_STRING)||(info==DRAGDROP_TARGET_TEXT))
        reply=dnd_send_string(reply,file);
      else if((info==DRAGDROP_TARGET_NETSCAPE_URL)||(info==DRAGDROP_TARGET_URL))
        reply=dnd_send_URL(reply,file);
      else if(info==DRAGDROP_TARGET_COLOUR)
        E(0,"peer requests colour.  not quite sure what to do about that (yet), come up with something, or stop offering colours and only accept them!\n"); /* ### FIXME */
      else
        E(0,"peer requests data we don't handle but did offer!?  (%d)\n",info);
#ifdef HAVE_EVAS2
      }
      i=i->next; }

 /* gdk_drag_context_unref(il->dc); */
    il->dc=NULL;
#else
    }
#endif

    if(reply&&action_context_get_1st(ac)) {
      gtk_selection_data_set(selectionData,evis_seldata_target(selectionData),8,reply,strlen(reply));
      file_drop_remember(ns,dc,ac); }
    if(reply)
      free(reply); }
  else
    E(0,"no selection data!?\n"); }



/****************************************************************************/



/**
 * set callbacks for icon-view
 * @param ns  the session
 */
void icon_view_connect(noir_sess *ns) {
  noir_iconview  *iv=NULL;

  if(!ns||!(iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW))) {
    E(0,"failed! %p, %p\n",ns,iv);
    return; }

  g_signal_connect(G_OBJECT(iv->icon_view),
                     "drag_data_get",G_CALLBACK(icon_drag_data_get),(gpointer)ns);
  g_signal_connect(G_OBJECT(iv->icon_view),
                     "drag_data_received",G_CALLBACK(icon_drag_data_received),(gpointer)ns);
  g_signal_connect(G_OBJECT(iv->icon_view),
                     "drag_drop",G_CALLBACK(icon_drag_drop),(gpointer)ns);
  g_signal_connect(G_OBJECT(iv->icon_view),
                     "drag_data_delete",G_CALLBACK(file_drop_delete_from_iconlist),(gpointer)ns);
  g_signal_connect(G_OBJECT(iv->icon_view),
                     "drag-end",G_CALLBACK(file_drop_free),(gpointer)ns);

  gtk_drag_dest_set(GTK_WIDGET(iv->icon_view),GTK_DEST_DEFAULT_ALL,target,targets_xds,GDK_ACTION_LINK|GDK_ACTION_COPY|GDK_ACTION_MOVE);
#if defined(HAVE_EVAS2)
  { /* icon_list *il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view);
    sb_update(il,TRUE); */
  }
#endif
 }



/****************************************************************************/




#endif



/****************************************************************************/



/**
 * rerender icon-view
 * (should be inline'd once debugged)
 * @param iv      the icon-view
 * @param il      the icon-list
 * @param caller  debug-info
 */
static inline void icon_view_rerender(noir_iconview *iv,icon_list *il,const char *caller) {
#ifdef HAVE_EVAS2
  Evas *evas;

  if(il&&(evas=get_evas_from_iconlist(il))) {
    E(2,"%s  (%p, %p)\n",caller,il,evas);
    iv->next_update=now(IV_UPDATE_EVERY);
    icon_list_layout_all(il); }
  else
    E(0,"evas has disappeared, not rendering : )\n");
#endif
}



/**
 * voluntary rerender?
 * @param iv  icon-view
 * @param il  icon-list
 */
static inline void icon_view_rerender_maybe(noir_iconview *iv,icon_list *il) {
  if(iv->next_update<now(0)) {
    E(2,"VOLUNTARY RERENDER!  (every %1.3fs)  @%lu\n",((float)IV_UPDATE_EVERY)/1000,now(0));
    icon_view_rerender(iv,il,__FUNCTION__); }}



/**
 * let plugins request an update of the visual
 * @return an error-code
 * @param il  the icon-list to update
 */
int icon_view_render_updates(icon_list *il) {
  noir_sess     *ns=il->sess;
  noir_iconview *iv;

  if((iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW))) {
    gevas_queue_redraw((GtkgEvas *)iv->icon_view);
    return NOIR_SUCC; }
  return NOIR_FAIL; }






/****************************************************************************/



/**
 * handle model-event -- any locks must be handled outside of this
 * @return an error-code 
 * @param fi       the fs object this event is about
 * @param type     event-type
 * @param changed  what parts of 'fi' changed
 * @param iv       the icon-view
 * @param pa       parent of 'fi'
 * @param cf       client
 */
static int icon_view_evt_do(file_info *fi,model_event type,file_flags changed,noir_iconview *iv,file_info *pa,client_funs *cf) {
  icon_list     *il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view);
#ifdef HAVE_EVAS2
  icon          *i=icon_list_get_icon_by_file(il,file_info_get_tag(fi));
#endif
  static int     warned_nofallback=0;

  switch(type) {
    case MODEL_CWD:
      if(changed==FF_NONE) {   /* pre */
#ifdef HAVE_EVAS2
        if(il) {
          icon *oi;

          if(pa) {
            int succ=FALSE;
            oi=il->icons;
            while(oi) {
              if(((oi->x+oi->w)>il->sx)&&((oi->y+oi->h)>il->sy)) {
                succ=TRUE;
                dcd_set(pa,cf,(void *)(intptr_t)oi->index);
                oi=NULL; }
              else
                oi=oi->next; }
            if(!succ)
              dcd_set(pa,cf,NULL); }

          iv->next_update=now(IV_UPDATE_EVERY/4);
          icon_list_rst(il);
          if(iv->cwd_shown)
            free(iv->cwd_shown);
          iv->cwd_shown=strdup(file_info_get_tag(fi)); }

#endif
      }

      else if(!(fi=file_info_fetch(NULL,file_info_get_tag(fi))))
        E(0,"MODEL_POST -- no item!?\n");
      else {                      /* post */
#ifdef HAVE_EVAS2
        file_info *fc;

        /* view was paused during the cd (another notebook page was
           active) so the pre-CWD (icon_list_rst) never ran: clear the
           stale folder before replaying, or both folders' icons end
           up in the same view */
        { const char *tag=file_info_get_tag(fi);
          if(!iv->cwd_shown||strcmp(iv->cwd_shown,tag)) {
            E(2,"icon-view missed pre-CWD for \"%s\" (was paused?), clearing stale view...\n",tag);
            icon_list_rst(il);
            if(iv->cwd_shown) {
              free(iv->cwd_shown);
              iv->cwd_shown=NULL; } } }

        /* update ".." icon */
        if((pa=file_info_get_parent(fi))) {
          const char *tag=file_info_get_tag(pa);
          icon_list_icon_insert_sorted(il,
                                       icon_path_get_always(tag,DIR_PARENT),
                                       tag,DIR_PARENT,NULL,
                                       file_info_get_intrinsic(pa),pa,FALSE); }

        /* replay from store */
        if((fc=file_info_get_1st_child(fi))) {
          int         cnt=0,evnt,scr;
          icon       *oi;

          E(1,"replaying \"%s\"...\n",file_info_get_tag(fi));
          while(fc) {
            icon_view_evt_do(fc,MODEL_UPD,file_info_get_infobits(fc),iv,fi,cf);
            if((evnt=gdk_events_pending())||(cnt++>64)) {
              cnt=0;
              icon_view_rerender_maybe(iv,il); }
            fc=file_info_get_next(fc); }
          /* re-visit retry:  files that were read on a previous visit of
             this folder never get FF_UNSEEN again, so the model will not
             re-request thumbnails for them.  if a thumbnail is missing
             (failed, or deleted from the cache), ask for it once more;
             the FAIL_ME cache keeps broken files from being retried on
             every single visit. */
          if(iv->session->thumbnails_in_iconlist) {
            for(fc=file_info_get_1st_child(fi);fc;fc=file_info_get_next(fc)) {
              if(!(file_info_get_infobits(fc)&FF_THUMB)&&
                 !S_ISDIR(file_info_get_mode(fc)))
                thumbnail_enqueue(fc); } }

          if((scr=GPOINTER_TO_INT(dcd_get_data(fi,cf)))&&(oi=icon_list_get_icon_by_index(il,scr)))
            icon_list_scroll_abs(il,0,oi->y); }

        icon_list_set_selection(il);
        icon_view_rerender(iv,il,"MODEL_CWD");


        /* extensions -- toolbars etc. */
        if(il) {
          icon *i=((noir_theme *)il->theme)->areas;
          while(i) {
            ((theme_area *)i->user1)->ext_upd(il,i,(void *)fi);
            i=i->next; }}
#endif
      }
      break;



    case MODEL_MSC:
      if(changed&(FF_NAME_|FF_THUMB_|FF_META_)) {
        E(2,"\n\nMODEL_MSC %s %x\n\n",file_info_get_tag(fi),changed);
#ifdef HAVE_EVAS2
        if(changed&FF_META_&&il->sortflags&&(*il->sortflags&SORT_BYMETA))
          icon_list_resort(il);
#endif

        icon_view_set_selection(iv->session);

#ifdef HAVE_EVAS2
        { icon *i;
          for(i=il->icons;i;i=i->next)
            evas_icon_set_state(il,i,i->state); }
#endif

        icon_view_rerender(iv,il,"MODEL_MSC"); }
      break;



    case MODEL_UPD:
/*    E(0,"UPD %s, $%x, %s\n",type==MODEL_UPD?"UPD":"???",changed,fi&&fi->tag?fi->tag:"???"); */

      /* icon does not yet exist, create & add */
      if((changed&FF_NAME)
#ifdef HAVE_EVAS2
         &&!i
#endif
        ) {
        const char *tag=file_info_get_tag(fi),*mime=file_info_get_mime(fi);
        char       *ifile=icon_path_get_always(tag,mime);
        const char *name=file_info_get_name(fi);

/*      E(0,"%s: %p, $%x  %d\n",fi->tag,pa,pa?pa->have:0,0); */

        if(!ifile||!*ifile) {
          E(0,"no icon set for \"%s\"/\"%s\"...  your " MIME_ICONS " has no icon set for the \"default\" and \"unknown\" fallbacks.\n",file_info_get_tag(fi),file_info_get_mime(fi)); }
        else if(!warned_nofallback&&!file_exists(ifile)) {
          warned_nofallback++;
          E(0,"icon-file \"%s\" not found...\n",ifile); }

#ifdef HAVE_EVAS2
        /* update icon for file */
        i=icon_list_icon_insert_sorted(il,ifile,file_info_get_tag(fi),name,file_info_get_thumbfile(fi),file_info_get_intrinsic(fi),fi,FALSE);

#endif
        goto done; }


#ifdef HAVE_EVAS2
      if((changed&(FF_STAT|FF_MIME))&&i) {
        if((evas_icon_chg_event(il,i,fi)!=NOIR_FAIL)&&
           (!pa||(file_info_get_infobits(pa)&FF_MIME_))) {
/* E(0,"%s, %x (%s, $%x)\n",fi->tag,fi->have,pa?pa->tag:"???",pa?pa->have:0); */
          icon_view_rerender(iv,il,"MODEL_UPD -- STAT/MIME"); }}
#endif

      if(changed&FF_THUMB) {
#ifdef HAVE_EVAS2
        /* are thumbnails enabled?  and is this file supposed to have one? */
//      if(iv->session->thumbnails_in_iconlist&&(file_info_get_infobits(fi)&FF_THUMB)) {
        if(iv->session->thumbnails_in_iconlist) {
          /* find pre-existing icon for this file. */
          if((i=icon_list_get_icon_by_file(il,file_info_get_tag(fi)))) {
            /* try to modify icon. */
            if(evas_icon_chg_event(il,i,fi)!=NOIR_FAIL) {
              /* did we actually get a thumbnail? */
              if(i->state&IE_THMB) {
                /* if this is a monitor-event rather than the initial read so
                   we don't expect more of the same soon, update view now. */
                /* throttle to at most one relayout per IV_UPDATE_EVERY:
                   in image-heavy dirs a thumbnail finishes every few
                   ms during the initial read; an unthrottled full
                   icon_list_layout_all() per event starved the GUI
                   (click-to-highlight lagged by seconds on 50+ files) */
                if(!pa||(file_info_get_infobits(pa)&FF_THUMB_))
                  icon_view_rerender_maybe(iv,il); }

              /* got no thumbnail, so don't pretend to have one. */
              else {
                if(!S_ISDIR(file_info_get_mode(fi))) {
                  E(1,"failed to get thumbnail for \"%s\"? ($%x)\n",file_info_get_tag(fi),i->state); }
                file_info_clr_infobits(fi,FF_THUMB); }}}
          /* event was unrelated to this view!? */
          else
            E(0,"FF_THUMB ignored for \"%s\"!?\n",file_info_get_tag(fi)); }
#endif
      }
      break;



    case MODEL_DEL:
#ifdef HAVE_EVAS2
      if((i=icon_list_get_icon_by_file(il,file_info_get_tag(fi)))) {
/*      info_tooltip_cancel(NULL,NULL,NULL); */
        icon_list_icon_remove(il,i,TRUE);
        icon_view_rerender(iv,il,"MODEL_DEL"); }
      break;
#endif

    default:
      break; }

 done:

#ifdef HAVE_EVAS2
  evas_damage_rectangle_add(get_evas_from_iconlist(il),0,0,il->vis_w,il->vis_h);
#endif

#if 1
  if(pa&&!(file_info_get_flags(pa)&&FF_NAME_))
    icon_view_rerender_maybe(iv,il);
#endif

  return NOIR_SUCC; }



/**
 * take care of locks, then handle model-event
 * @return an error-code 
 * @param fi       the fs object this event is about
 * @param type     event-type
 * @param changed  what parts of 'fi' changed
 * @param data     the icon-view
 */
static int icon_view_evt(file_info *fi,model_event type,file_flags changed,void *data) {
  noir_iconview *iv=(noir_iconview *)data;
  file_info     *pa=file_info_get_parent(fi);
  client_funs   *cf;
  int            ret;

#if 0
#ifdef HAVE_BTRACE
  fprintf(stderr,"\n");
  fflush(stderr);
  btrace_print();
#endif
  { char *mon[]={ "???", "UPD", "DEL", "CWD", "MSC" };
    E(0,"%s/$%06x   \"%s\"\n",mon[type],changed,file_info_get_tag(fi)); }
#endif

  /* extra mutex to detect nested calls */
  if((ret=pthread_mutex_trylock(&iv->viewlock))) {
    if(ret==EBUSY) {
      E(2,"discarding nested event\n");
      return NOIR_MUTEX; }
    E(0,"failed to lock mutex: #%d -- %s\n",ret,strerror(ret));
    return NOIR_FAIL; }

  if(!(cf=noir_sess_client_lock(iv->serial,CLIENT_ICONVIEW))) {
    E(4,"ignoring event for newly-destroyed view...\n");
    ret=NOIR_IGNORED;
    goto done; }

  /* temporary fix so we'll only get events for stuff in the CWD.
     at a later date, we'll only get what we register for, which
     will make this kludge unnecessary. */
  if(type!=MODEL_CWD) {       /* CWD may refer to any item */
    if(type==MODEL_MSC)       /* MISC may pertain to current dir */
      pa=fi;
    if(pa!=iv->session->cd) { /* anything else must be *in* current dir */
      ret=NOIR_IGNORED;       /* or else... */
      goto done; }}

  ret=icon_view_evt_do(fi,type,changed,data,pa,cf);

 done:
  noir_sess_client_unlock(cf);
  pthread_mutex_unlock(&iv->viewlock);

  return ret; }






/****************************************************************************/



/**
 * destruct icon-view
 * @return an error-code
 * @param data  the icon-view
 */
static int icon_view_dst(void **data) {
  noir_iconview *iv=*((noir_iconview **)data);

  if(iv) {
    icon_list *il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view);
    theme_canvas_destall(il);
    pthread_mutex_destroy(&iv->viewlock);
    memset(iv,0,sizeof(noir_iconview));
    free(iv);
    return NOIR_SUCC; }
  else
    return NOIR_IGNORED; }



#ifdef HAVE_EVAS2

/**
 * listen for changes in our numeric properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int icon_view_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  long           val=prop_get_int(e);
  const char    *key=prop_get_key(e);
  icon_list     *il=(icon_list *)data;

  switch(tag) {
    case IV_SPATIAL:
      if(val)
        il->flags|=ILF_SPATIAL;
      else
        il->flags&=(~ILF_SPATIAL);
      break;
    case IV_PADX:
      il->xpad=val;
      icon_list_layout_all(il);
      break;
    case IV_PADY:
      il->ypad=val;
      icon_list_layout_all(il);
      break;
    case IV_PADW:
      il->wpad=val;
      icon_list_layout_all(il);
      break;
    case IV_PADH:
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
static int icon_view_str_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  char          *val=prop_get_string(e);
  const char    *key=prop_get_key(e);
  noir_sess     *ns=(noir_sess *)data;
  noir_theme    *theme=(noir_theme *)((noir_theme *)ns->theme);
  noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
  icon_list     *il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view);
  char          *x=NULL;

  if(val) {
    void *o=il->bg;

    if((theme->bg_new==NULL)||(theme->bg_dst==NULL))
      return NOIR_FAIL;

    /* empty values are permissable, invalid file-paths are not */
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

    if((gtk_widget_get_mapped(iv->icon_view))&&theme->bg_new&&!theme->bg_new(theme,il,val)) {
      E(0,"failed to update background...\n");
      il->bg=o; }
    else {
      if(theme->iconview_bg)
        free(theme->iconview_bg);
      theme->iconview_bg=strdup(val);
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
 * create icon-view in given session
 * @return an error-code
 * @param ns  the session
 */
int icon_view_init(noir_sess *ns) {
  noir_iconview *iv;
#if defined(HAVE_EVAS2)
  GtkWidget  *wtoy;
  icon_list  *il;

  if(!(iv=(noir_iconview *)malloc(sizeof(noir_iconview))))
    return NOIR_OOM;
  memset(iv,0,sizeof(noir_iconview));

  if(!ns||!(wtoy=gevas_icon_list_new(ns,MODULE, 24,16, 0))) {
    free(iv);
    return NOIR_FAIL; }

  il=ICON_LIST_GET_FROM_WIDGET(wtoy);

/*  ((noir_theme *)ns->theme)->bg_new(((noir_theme *)ns->theme),il,((noir_theme *)ns->theme)->iconview_bg); */

  iv->icon_view=il->canvas;
  iv->icon_scroll=wtoy;
  iv->session=ns;
  iv->serial=noir_sess_get_serial(ns);

  gtk_widget_set_name(iv->icon_scroll,"icon_scroll");
  gtk_widget_set_name(iv->icon_view,  "icon_view");

#if GTK_CHECK_VERSION(3,0,0)
  g_signal_connect(G_OBJECT(iv->icon_scroll),"draw",G_CALLBACK(icon_view_exposed),(gpointer)ns);
#else
  g_signal_connect(G_OBJECT(iv->icon_scroll),"expose-event",G_CALLBACK(icon_view_exposed),(gpointer)ns);
#endif

  iv->client=noir_sess_client_register_full(ns,CLIENT_ICONVIEW,(void *)iv,icon_view_evt,icon_view_dst,icon_view_set_selection);

  icon_view_connect(ns);

  prop_bag_prop_default(((noir_theme *)ns->theme)->data,MODULE "-pad-x",  "12",-64,64,1);
  prop_bag_prop_default(((noir_theme *)ns->theme)->data,MODULE "-pad-y",   "8",-64,64,1);
  prop_bag_prop_default(((noir_theme *)ns->theme)->data,MODULE "-pad-w",  "24",-32,32,1);
  prop_bag_prop_default(((noir_theme *)ns->theme)->data,MODULE "-pad-h",  "16",-32,32,1);
  prop_bag_prop_default(((noir_theme *)ns->theme)->data,MODULE "-spatial", "0",  0, 1,1);

#  define LISTENER "icon_view{evas}"
  prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE,           icon_view_str_listener,IV_BG,  ns);
  prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE "-pad-x",  icon_view_num_listener,IV_PADX,   il);
  prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE "-pad-y",  icon_view_num_listener,IV_PADY,   il);
  prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE "-pad-w",  icon_view_num_listener,IV_PADW,   il);
  prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE "-pad-h",  icon_view_num_listener,IV_PADH,   il);
  prop_bag_prop_listen(((noir_theme *)ns->theme)->data,LISTENER,MODULE "-spatial",icon_view_num_listener,IV_SPATIAL,il);

  il->extensions=(void *)((noir_theme *)ns->theme)->areas;

  return NOIR_SUCC;
#else
  return NOIR_FAIL;
#endif
}



/**
 * drop icon-view in given session
 * @return an error-code
 * @param ns  the session
 */
int icon_view_exit(noir_sess *ns) {
  /* icons are destroyed in gevas' call-back (cf. gevas_icon_list_new()) */
  return NOIR_SUCC; }
