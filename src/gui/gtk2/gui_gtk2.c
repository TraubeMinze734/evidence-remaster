/** @file  gui_gtk2.c
 *  @brief the default GUI.  GTK2, several views (icon-, tree-, browser-, shelf)
 */

#include "config.h"

#include <stdlib.h>

#include "model.h"          /* for mode_private.h */
#include "model_private.h"  /* client_funs */

#include "util.h"           /* timeout_add(), now() */

#include "file.h"           /* file_*() */
#include "file_lcl.h"       /* file_dirname() */

#include "setup.h"          /* get_setup_file() */



#ifdef HAVE_ECORE
#  include <Ecore.h>
#endif
#ifdef HAVE_EDJE
#  include <Edje.h>
#endif
#ifdef HAVE_EVAS2
#  include <Evas.h>
#endif



#include "gui/common/session_private.h"
#include "gui/common/session.h"
#include "gui/common/typebuffer.h"
#include "gui/common/menu.h"
#include "gui/common/desktop.h"
#include "gui/common/icon_path.h"      /* icon_path_init(), icon_path_exit() */

#include "gui/efl/common/theme.h"

#ifdef HAVE_EVAS2
#  include "icon_view/gevas_light.h"
#  include "icon_view/gevas_bind.h"
#endif
#ifdef HAVE_GNOMECANVAS
#  include "icon_view/gnome-icon-list.h"
#  include "icon_view/gnome-icon-list.h"
#endif

#ifdef HAVE_GTK2
#  include <gtk/gtk.h>

#  include "common/keysyms.h"   /* GDK_* */

#  include "common/icon.h"      /* icon_init(), icon_exit() */
#  include "common/shelf.h"     /* struct noir_shelf */

#  include "tree_view/tree_view.h"      /* struct noir_treeview, tree_model_setup() */
#  include "browser_view/browser_view.h"   /* struct noir_browser */
#  include "icon_view/icon_view.h"      /* struct noir_iconview */

#  include "about/about.h"          /* about_init() */
#  include "common/dnd.h"            /* dnd_init() */

#  include "common/clipboard.h"      /* selection_update(), clipboard_get/set() */
#  include "common/notebook.h"       /* notebook_setup() */

#  include "common/session_gtk2.h"
#endif

#include "gui/common/alert.h"          /* form_alert(), form_yes_no(), form_dial(), info_init(), info_exit() */
#include "gui/common/options.h"        /* view_name[], parse_options() */
#include "gui/common/menu.h"           /* menu_root_context(), menu_lib_gc() */
#include "gui/common/tooltip.h"        /* tooltip_cancel() */

#include <gdk/gdkx.h>



#include <string.h>
#include <stdio.h>

#include "thumbnailer/thumbnail.h"      /* thumbnail_init/exit() */

#include "mime.h"           /* MIME_INTL_MKD */



int go2view_mapping[START_IN_CREDITS+1];



static int set_app_icon(noir_sess *ns);



/*****************************************************************************/
/* X helpers */



#ifdef HAVE_X

/**
 * get X display.
 */
Display *get_display(void) {
#if GTK_CHECK_VERSION(3,0,0)
  GdkDisplay *gd=gdk_display_get_default();
  return GDK_IS_X11_DISPLAY(gd)?gdk_x11_display_get_xdisplay(gd):NULL;
#else
  return gdk_x11_display_get_xdisplay(gdk_display_get_default());
#endif
}



/**
 * get X root window.
 */
Window get_root_window(void) {
  Display *d=get_display();
  return d?DefaultRootWindow(d):None; }



/**
 * get X window from GTK-widget.
 * @param w - the widget
 */
Window get_widget_window(void *w) {
  return GDK_WINDOW_XID(gtk_widget_get_window((GtkWidget *)w)); }

#endif






/*****************************************************************************/



/**
 * callback for GTK notebook's "switch-page" signal.
 * @param notebook - the GTK notebook, or NULL
 * @param page_ptr - the GTK notebook page, or NULL
 * @param page_index - the page we're switching to
 * @param data - the session
 */
static int go2view_activate_page(GtkNotebook *notebook,gpointer page_ptr,
                                 guint page_index,gpointer data) {
  noir_sess     *ns=(noir_sess *)data;
  noir_treeview *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);
  noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
  noir_browser  *bv=(noir_browser *) noir_sess_client_get_data(ns,CLIENT_BROWSERVIEW);
  client_funs   *cf=NULL;
  int            page_no=(int)page_index;

  E(2,"page no. %d, %s\n",page_no,
    ((page_no>=0)&&(page_no<4))?view_name[page_no]:"???");

  if(tv)  tv->client->flags|=CF_PAUSED;
  if(bv)  bv->client->flags|=CF_PAUSED;
  if(iv)  iv->client->flags|=CF_PAUSED;

  if(page_no==go2view_mapping[START_IN_CREDITS])
    return NOIR_SUCC;

  if(page_no==go2view_mapping[START_IN_TREEVIEW])
    cf=tv?tv->client:NULL;
  else if(page_no==go2view_mapping[START_IN_BROWSER])
    cf=bv?bv->client:NULL;
  else if(page_no==go2view_mapping[START_IN_ICONVIEW])
    cf=iv?iv->client:NULL;
  else
    E(0,"unknown page #%d!?\n",page_no);

  ns->client=cf;
  if(cf) {
    cf->flags&=(~CF_PAUSED);
    if(cf->set_selection)
      cf->set_selection((void *)ns); }

  return noir_sess_focus(ns,noir_sess_get_cwd(ns)); }



/**
 * setup go2view sub-system.
 * @param ns - the session
 */
void go2view_setup(noir_sess *ns) {
#ifdef WITH_TREEVIEW
  noir_treeview *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);
#endif
#ifdef WITH_ICONVIEW
  noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
#endif
#ifdef WITH_BROWSERVIEW
  noir_browser  *bv=(noir_browser *) noir_sess_client_get_data(ns,CLIENT_BROWSERVIEW);
#endif
  int            c;

  for(c=0;c<=START_IN_CREDITS;c++)
    go2view_mapping[c]=-1;

  c=0;

#ifdef WITH_TREEVIEW
  if(tv&&tv->tree_view) {
    go2view_mapping[START_IN_TREEVIEW]=c++;
    E(2,"tree-view enabled...  (%d)\n",c-1); }
  else
    E(0,"could not enable tree-view... (%p,%p)\n",tv,tv?tv->tree_view:NULL);
#endif

#ifdef WITH_BROWSERVIEW
  if(bv&&bv->browser_view) {
    go2view_mapping[START_IN_BROWSER]=c++;
    E(2,"browser-view enabled...  (%d)\n",c-1); }
  else
    E(0,"could not enable browser-view... (%p,%p)\n",bv,bv?bv->browser_view:NULL);
#endif

#ifdef WITH_ICONVIEW
  if(iv&&iv->icon_scroll) {
    go2view_mapping[START_IN_ICONVIEW]=c++;
    E(2,"icon-view enabled...  (%d)\n",c-1); }
  else
    E(0,"could not enable icon-view... (%p,%p)\n",iv,iv?iv->icon_scroll:NULL);
#endif

  if(noir_sess_get_about(ns)) {
    go2view_mapping[START_IN_CREDITS]=c++;
    E(2,"credits-screen enabled...  (%d)\n",c-1); }}



/**
 * go to a different page in the notebook of a given session.
 * @return TRUE for success, FALSE otherwise.
 * @param ns - the session
 * @param thepage - what page in notebook to go to
 */
int notebook_go2view(noir_sess *ns,int thepage) {
  int page=-1;

  if(thepage>START_IN_CREDITS) {
    E(0,"(%d): parameter out of range (0..%d)\n",thepage,START_IN_CREDITS);
    return FALSE; }
  page=go2view_mapping[thepage];

  if((page<0)||
     (page>gtk_notebook_page_num(GTK_NOTEBOOK(noir_sess_get_notebook(ns)),noir_sess_get_about(ns)))) {
      E(0,"(sess-no: #%ld, view-no: #%d-\"%s\") failed (%d-\"%s\")...\n",ns->serial,thepage,view_name[thepage],page,(page==-1)?"view not available":"unknown error");
    return FALSE; }

  ns->start_in=thepage;
  gtk_notebook_set_current_page(GTK_NOTEBOOK(noir_sess_get_notebook(ns)),page);

#ifdef HAVE_EVAS2
  { noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
    if((thepage==START_IN_ICONVIEW)&&(iv->icon_view)&&
       gtk_widget_get_mapped(iv->icon_view))
      gtk_widget_grab_focus(iv->icon_view); }
#endif

  return TRUE; }






/*****************************************************************************/



/**
 * end program by destroying all its windows.
 */
void gui_quit(void) {
  noir_sess *a=noir_sess_get_1st(),*b;

  while(a) {
    b=a->next;
    if(noir_sess_get_window(a))
      gtk_widget_destroy(noir_sess_get_window(a));
    a=b; }}






/*****************************************************************************/
/* various event-handlers */


static int evidence_shutdown(void *data) {
  char           *p=strrchr(temp,'-');
  action_context *ac=NULL;

  /* delete tmp-dirs, if any */
  if(p) {
    while(*(++p))
      *p='?';
    if((ac=file_glob(temp,NULL))) {
      action_file *af;
      for(af=action_context_get_selection(ac);af;af=action_file_get_next(af))
	file_rm(action_file_get_tag(af));
      action_context_dst(ac); }}
  else
    file_rm(temp); /* synch */

  free(temp);

  /* expiry old thumbnails */
  if(!(random()%8))
    thumbnail_disk_gc();

  mainloop_quit();

  return FALSE; }



/**
 * callback for a window's "destroy" event.
 * @param w - the widget that was destroyed
 * @param ns - the session it belonged to
 */
void closed(GtkWidget *w,noir_sess *ns) {
  tooltip_cancel(ns);

  if(!ns) {
    E(0,"destroyed window that belonged to no session??\n");
    return; }

  /* the window is already being destroyed, so drop window-reference to it --
     otherwise, noir_sess_drop() will try to be helpful and kill it for us. */
  noir_sess_set_window(ns,NULL);
  if(noir_sess_drop(ns)==NOIR_SUCC)
    timeout_add(0,evidence_shutdown,NULL); }



/**
 * fallback callback for mouse-clicks that no better handler exists for.
 * @param widget - the widget that received the event
 * @param ev - the actual event
 * @param data - the session
 */
static gboolean win_clicked(GtkWidget *widget,GdkEventButton *ev,gpointer data) {
  noir_sess     *ns=(noir_sess *)data;
#ifdef HAVE_GNOMECANVAS
  noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
#endif

  if((ev->type!=GDK_BUTTON_RELEASE)&&(ev->type!=GDK_BUTTON_PRESS))
    return FALSE;

  if(ns&&ev) {
    if(ev->button==2) {
      if(typebuffer_is_open(ns))
        clipboard_get(GDK_SELECTION_PRIMARY,ns);
      else
        menu_path(ns,ev->button,ev->time);
      return TRUE; }

    else if((ev->type==GDK_BUTTON_RELEASE)&&(ev->button==3)) {
      if(ev->state&GDK_CONTROL_MASK) {
        menu_root_context(ns,noir_sess_get_cwd(ns),3);
        return TRUE; }
#ifdef HAVE_GNOMECANVAS
      if(gtk_widget_get_mapped(iv->icon_scroll)) {
        gint no=-1;
        if((no=gnome_icon_list_get_icon_at(GNOME_ICON_LIST(iv->icon_view),ev->x,ev->y))>=0) {
          icon_view_selected(GNOME_ICON_LIST(iv->icon_view),no,(GdkEvent *)ev,(gpointer)ns);
          return TRUE; }}
#endif
/*    menu_root_context(ns,noir_sess_get_cwd(ns),ev->button); */
      return TRUE; }}

  return FALSE; }



/**
 * fallback callback for keystrokes that no better handler exists for.
 * @param widget - the widget that received the event
 * @param ev - the actual event
 * @param data - the session
 */
static gboolean win_keyed(GtkWidget *widget,GdkEventKey *ev,gpointer data) {
  gboolean   ret=FALSE;
  noir_sess *ns=(noir_sess *)data;

  if(!ns)
    return FALSE;

  if(ev->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK)) {
    if((ev->keyval>=GDK_F1)&&(ev->keyval<=GDK_F10))
      return notebook_go2view(ns,ev->keyval-GDK_F1);

    ret=TRUE;

    switch(ev->keyval) {

    /* need to handle in case there is no note-book */
    case GDK_i:
      if((ev->state&GDK_CONTROL_MASK))
        return FALSE;
      notebook_go2view(ns,START_IN_ICONVIEW);
      break;
    case GDK_t:
      if((ev->state&GDK_CONTROL_MASK))
        return FALSE;
      notebook_go2view(ns,START_IN_TREEVIEW);
      break;
    case GDK_a:
      if((ev->state&GDK_CONTROL_MASK))
        return FALSE;
      notebook_go2view(ns,START_IN_CREDITS);
      break;

    case GDK_m:    /* ^M - menu */
      menu_root_context(ns,noir_sess_get_cwd(ns),3);
      break;

    case GDK_z:    /* ^Z - iconify */
/*    if(!ns->fullscreen) */
      gtk_window_iconify(GTK_WINDOW(noir_sess_get_window(ns)));
      break;

    case GDK_r:   /* ^R - search in typebuffer */
      if(!typebuffer_is_open(ns)) {
        E(2,"^R opening typebuffer.\n");
        typebuffer_open(ns);
        typebuffer_add_flags(ns,TB_RSEARCH); }
      else if(typebuffer_get_flags(ns)&TB_RSEARCH) { /* ^R ^R */
        E(2,"^R ^R in typebuffer.\n");
        typebuffer_add_flags(ns,TB_RSEARCH|TB_RRSEARCH);
        typebuffer_history_search(noir_sess_get_typebuffer(ns),NULL); }
      else {
        E(2,"^R in typebuffer. ($%x)\n",typebuffer_get_flags(ns));
        typebuffer_add_flags(ns,TB_RSEARCH);
        typebuffer_set_tab(ns,typebuffer_get(ns));
        typebuffer_history_search(noir_sess_get_typebuffer(ns),NULL); }
      typebuffer_update(ns);
      return TRUE;
      break;

    case GDK_n: { /* ^N - new folder */
      int r2;
      if((r2=menu_action_by_mime(noir_sess_get_serial(ns),MIME_INTL_MKD,noir_sess_get_cwd(ns),NULL))!=NOIR_SUCC)
        E(0,"could not open properties dialog (type \"" MIME_INTL_MKD "\"): %d\n",r2); }
      break;

    case GDK_d:    /* ^D - duplicate session */
      noir_sess_create(SESSION_COPY,ns,NULL);
      break;

    case GDK_x:    /* ^X - cut */
      if(typebuffer_is_open(ns)) {
        clipboard_set(GDK_SELECTION_CLIPBOARD,typebuffer_get(ns));
        typebuffer_set(ns,"\0");
        typebuffer_update(ns); }
      else if(noir_sess_get_selection(ns)) {
        /* put the files on the CLIPBOARD selection as a cuttable file-list */
        clipboard_files_set(noir_sess_get_selection(ns),1);
        noir_sess_set_selection(ns,NULL);
        selection_update(ns,NULL); }
      else
        form_alert(ns,"No selection to cut to clipboard...");
      break;
    case GDK_c:    /* ^C - copy */
      if(typebuffer_is_open(ns))
        clipboard_set(GDK_SELECTION_CLIPBOARD,typebuffer_get(ns));
      else if(noir_sess_get_selection(ns)) {
        /* put the files on the CLIPBOARD selection in a format other
           file-managers (Dolphin, Nautilus, ...) understand */
        clipboard_files_set(noir_sess_get_selection(ns),0); }
      else
        form_alert(ns,"No selection to copy to clipboard...");
      break;
    case GDK_b:
    case GDK_v:
      if((ev->state&GDK_CONTROL_MASK)) {  /* ^V  - paste */
        if(typebuffer_is_open(ns))
          clipboard_get((ev->keyval==GDK_b)?GDK_SELECTION_PRIMARY:GDK_SELECTION_CLIPBOARD,ns);
        else if(ev->keyval==GDK_v)
          clipboard_paste(ns,GDK_SELECTION_CLIPBOARD); }
      else if(ev->keyval==GDK_b)
        notebook_go2view(ns,START_IN_BROWSER);
      else
        return FALSE;
      break;

    case GDK_l:    /* ^L - location */
    case GDK_o:    /* ^O - open */
      if(typebuffer_open(ns)==NOIR_SUCC) {
        tooltip_cancel(ns);
        typebuffer_update(ns); }
      break;

    case GDK_q:    /* ^Q - quit */
      if(form_yes_no(ns,"Exit file-manager?")==NOIR_SUCC)
        gui_quit();
      break;
    case GDK_u: {  /* ^U - close window */
      int but=1;
      noir_sess *s=noir_sess_get_1st();

      if(!s||!noir_sess_get_next(s))    /* last window */
        but=form_dial(ns,ALERT_QUESTION,ALERT_BUTTONS_CUSTOM,"You are about to close the file-manager's last window.\nWould you really like to exit evidence?","_Quit evidence|_Iconify window|_Cancel");

      else if(ns->fullscreen) {
        if((but=form_dial(ns,ALERT_QUESTION,ALERT_BUTTONS_CUSTOM,"You are about to close the file-manager's desktop.","_Quit evidence|_Remove desktop|_Iconify desktop|_Cancel"))==1) {
          gui_quit();
          return TRUE; }
        but--;      }

      switch(but) {
        case 1:
          noir_sess_close(ns);
          break;
        case 2:
          gtk_window_iconify(GTK_WINDOW(noir_sess_get_window(ns)));
          break; }}
      break;

    case GDK_w:    /* ^W - next Window */
      if(noir_sess_get_next(ns))
        gtk_window_present(GTK_WINDOW(noir_sess_get_window(noir_sess_get_next(ns))));
      else
        gtk_window_present(GTK_WINDOW(noir_sess_get_window(noir_sess_get_1st())));
      break;

      default:
        ret=FALSE; }}

  /***************************************************************************/

  else if(typebuffer_is_open(ns)) {      /* TYPEBUFFER ("µshell") */
    char   *t=(typebuffer_get_flags(ns)&TB_RSEARCH)
              ?(char *)typebuffer_get_tab(ns)
              :(char *)typebuffer_get(ns);
    size_t  l=strlen(t);

    if(ev->keyval==GDK_Tab)           /* tab-completion */
      typebuffer_tab(ns);
    else {                            /* editing */
      switch(ev->keyval) {
        case GDK_Page_Up:
        case GDK_Up:
          typebuffer_history_traverse(noir_sess_get_typebuffer(ns),-1);
          break;
        case GDK_Page_Down:
        case GDK_Down:
          typebuffer_history_traverse(noir_sess_get_typebuffer(ns),1);
          break;

        case GDK_BackSpace:
          if(l)
            t[l-1]='\0';
          break;
        case GDK_Return:
          typebuffer_do(ns);
          typebuffer_close(ns);
          break;
        case GDK_Escape:
          typebuffer_close(ns);
          break;
        default:                      /* input */
          if(ev->length) {
            char *n=NULL;
            esprintf(&n,"%s%s",t,ev->string);
            if(n) {
              E(3,"ev->length: %d,  strlen(): %d\n",ev->length,strlen(ev->string));
              if(typebuffer_get_flags(ns)&TB_RSEARCH)
                typebuffer_set_tab(ns,n);
              else
                typebuffer_set(ns,n);
              free(n); }}
          break; }

      if(typebuffer_is_open(ns)) {
        if(l>=0)
          typebuffer_tab_reset(ns,l);
        typebuffer_update(ns); }

      return TRUE; }}



  /* typebuffer isn't open, open it! */
  else if((ev->keyval==GDK_Escape)||(ev->keyval==GDK_less)||
          (ev->keyval==GDK_slash)||
          (ev->keyval==GDK_percent)||
          (ev->keyval==GDK_apostrophe)) {
    ret=TRUE;
    if(typebuffer_open(ns)==NOIR_SUCC) {
      tooltip_cancel(ns);
      /* special case, this annoyed me no end */
      if(ev->keyval==GDK_slash)
        typebuffer_set(ns,"/");
      else if(ev->keyval==GDK_apostrophe)
        typebuffer_set(ns,"'");
      else if(ev->keyval==GDK_percent)
        typebuffer_set(ns,"%");
      typebuffer_update(ns); }}

  else if(ev->keyval==GDK_asciitilde) {
    noir_sess_focus(ns,home);
    ret=TRUE; }

  else if(ev->keyval==GDK_Menu) { /* menu */
    menu_root_context(ns,noir_sess_get_cwd(ns),3);
    ret=TRUE; }

  else if(ev->keyval&0x10000000) { /* keys unknown to GDK */
    const char *keysym=gdk_keyval_name(ev->keyval);
    E(1,"special key %s\n",keysym);
    if(keysym) {
      if(!strcmp(keysym,"XF86Search")) {         /* rubber-key "Magnifier" */
        ret=TRUE;
        if(typebuffer_open(ns)==NOIR_SUCC) {
          tooltip_cancel(ns);
          typebuffer_set(ns,"%");
          typebuffer_update(ns); }}

      else if(!strcmp(keysym,"XF86HomePage")) {  /* rubber-key "Home" */
        ret=TRUE;
        noir_sess_focus(ns,home); }
    }}

  E(3,"state: %d, key: 0x%x, str: %s\n",ev->state,ev->keyval,ev->length?ev->string:gdk_keyval_name(ev->keyval));

  return ret; }






/*****************************************************************************/



/**
 * update all transparency pixmaps in a window.
 *
 * for now, this will be zero or more of icon-view and shelf, ie whatever is
 * present and uses evas.  evas_bg_update() will call bg_upd() if present in
 * the background plug-in; the plug-in may update the background in whatever
 * manner it sees fit. if it supports pseudo-transparency, it may call
 * transparency_get_pixmap() in evas2.c to obtain a suitable pixmap.
 * @param ns - the session
 */
static inline int window_trans_update(noir_sess *ns) {
#ifdef HAVE_EVAS2
  { icon_list *il;
    GtkWidget *widget;
    noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
    noir_shelf    *sv=(noir_shelf *)   noir_sess_client_get_data(ns,CLIENT_SHELF);

    if(iv&&(widget=iv->icon_view)&&(il=ICON_LIST_GET_FROM_WIDGET(widget))) {
      /* must... force.... redraw...   (gevas_q not enough) */
      gevas_bg_update(il,il->vis_w,il->vis_h);
      evas_obscured_rectangle_add(gevas_get_evas((GtkgEvas *)il->canvas),0,0,il->vis_w,il->vis_h);
      evas_obscured_clear((gevas_get_evas((GtkgEvas *)il->canvas)));
      if(gtk_widget_get_mapped(widget))
        evas_render(gevas_get_evas((GtkgEvas *)il->canvas)); }

    if(sv&&(widget=sv->shelf_view)&&(il=ICON_LIST_GET_FROM_WIDGET(widget))) {
      /* must... force.... redraw...   (gevas_q not enough) */
      gevas_bg_update(il,il->vis_w,il->vis_h);
      evas_obscured_rectangle_add(gevas_get_evas((GtkgEvas *)il->canvas),0,0,il->vis_w,il->vis_h);
      evas_obscured_clear((gevas_get_evas((GtkgEvas *)il->canvas)));
      if(gtk_widget_get_mapped(widget))
        evas_render(gevas_get_evas((GtkgEvas *)il->canvas)); }}
#endif
  return NOIR_SUCC; }



/**
 * listen for properites on the root-window changing.
 *
 * for now, we're only interested in the rootmap ("wallpaper") changing,
 * because we need to update our transparency pixmap when that happens.
 * this will cycle through all windows we have as we don't get this event
 * on a per-window basis.  (event-filter)
 * @param x - the X-event, or NULL
 * @param ev - the property event
 * @param data - NULL
 */
static gboolean root_propertied(GdkXEvent *x,GdkEventProperty *ev,gpointer data) {
  char *atom;
  if((atom=gdk_atom_name(ev->atom))) {
    if(!strcmp(atom,"_XROOTPMAP_ID")) {
      noir_sess *sess=noir_sess_get_1st();
      E(3,"called\n");
      while(sess) {
        /* maybe check whether the window actually has transparency relative
           to this root pixmap here */
        window_trans_update(sess);
        sess=sess->next; }}
    g_free(atom); }
  return GDK_FILTER_CONTINUE; }


#ifdef HAVE_X

/**
 * keep track of which window was last focused (session->focused).
 *
 * event-filter.
 * @param gx - the X-event
 * @param ev - the property event, or NULL
 * @param data - the session
 */
static gboolean win_event(GdkXEvent *gx,GdkEvent *ev,gpointer data) {
  noir_sess *ns=(noir_sess *)data;
  XAnyEvent *xe=(XAnyEvent *)gx;

  if(xe->type==FocusIn)
    ns->focused=xe->serial;

#if 0
  else if(xe->type==ButtonPress)
    E(0,"press\n");
  else if(xe->type==ButtonRelease)
    E(0,"release\n");

  else if(xe->type==PropertyNotify)
    E(2,"prop!  %d  %s\n",xe->type,XGetAtomName(xe->display,((XPropertyEvent *)xe)->atom));
#endif

  return GDK_FILTER_CONTINUE; }

#endif



/**
 * callback for configure-events on the main window.
 *
 * for now, we're only interested because we'll need to update the transparency
 * pixmap when the window is moved.
 * @param widget - the widget that received the event
 * @param ev - the actual event
 * @param data - the session
 */
static gboolean win_configured(GtkWidget *widget,GdkEventConfigure *ev,gpointer data) {
  window_trans_update((noir_sess *)data);
  return FALSE; }






/*****************************************************************************/



/**
 * pre-selected items at start-up.
 *
 * called from main.
 * @param ns - the session
 * @param sel - the selection to make.
 */
static inline int view_sel(noir_sess *ns,action_context *sel) {
  char        *dn,*dp=NULL;
  int          virt=0;
  action_file *af=action_context_get_selection(sel);

  if(!af)
    goto fail;

  /* check whether all files are in the same directory */
  while(af) {
    if((dn=file_dirname(action_file_get_tag(af)))) {
      if(dp) {
        if(strcmp(dn,dp))
          virt=1;
        free(dp); }
      dp=dn; }
    af=action_file_get_next(af); }

  if(!dp)
    goto fail;

  if(virt) {
    E(2,"selecting files from multiple directories => making virt-folder...\n");
    E(0,"sorry, selecting files from multiple directories would require virtual folders, which are just around the corner...\n");
    goto fail; }
  else {
    E(2,"selecting files from single directory => selecting in \"%s\"...\n",dp);
    noir_sess_set_selection(ns,sel);
    selection_update(ns,NULL);
    noir_sess_focus(ns,dp); }

  free(dp);

  return NOIR_SUCC;

 fail:
  action_context_dst(sel);
  return NOIR_FAIL; }






/*****************************************************************************/



/**
 * create new session.
 * @return positive serial number of the new session, or a (negative) error-code
 * @param sm - mode: SESSION_CREATE -- use preinit'd struct referred to by template||
 *             SESSION_COPY   -- allocate, init using template
 * @param template - template to copy (SESSION_COPY), or struct to work in (SESSION_CREATE)
 * @param dir - working dir for the new session
 */
long noir_sess_create(sn_mode sm,noir_sess *template,const char *dir) {
  noir_sess *ns=NULL;
  int        page,nodeco;
  char      *cwd=NULL; /* ### */
  GtkWidget *container; /* window's top child: paned if there is a shelf, notebook otherwise */

  E(1,"opening (%c) \"%s\"...\n",sm==SESSION_COPY?'C':'N',dir?dir:(template->cwd?template->cwd:"<none!?>"));

  if(!template)
    return NOIR_NODATA;

  if(sm==SESSION_COPY) {
    if(!(ns=noir_sess_new()))
      return NOIR_OOM;
    noir_sess_copy(ns,template); }
  else
    ns=template;

  if(dir&&(dir!=ns->cwd)) {
    if(ns->cwd)
      free(ns->cwd);
    ns->cwd=strdup(dir); }

  if(!ns->cwd) {
    E(0,"no diretory!?\n");
    if((sm==SESSION_COPY)&&ns)
      free(ns);
    return NOIR_NODATA; }

  /* main-window */
  noir_sess_set_window(ns,gtk_window_new(GTK_WINDOW_TOPLEVEL));

/* #ifdef HAVE_EVAS2 */
  if(ns->theme) {
    if(!((noir_theme *)template->theme)->name||!(((noir_theme *)ns->theme)->name=strdup(((noir_theme *)template->theme)->name)))
      goto free_theme;
    else if(!((noir_theme *)template->theme)->path||(((noir_theme *)ns->theme)->path=strdup(((noir_theme *)template->theme)->path)))
      goto view_load_theme;
    free(((noir_theme *)ns->theme)->name);
   free_theme:
    free(ns->theme);
    ns->theme=NULL; }

 view_load_theme:
  if(!ns->theme||(theme_init(ns,cwd)!=NOIR_SUCC)) {
    if(ns&&ns->theme&&((noir_theme *)ns->theme)->name&&strcmp(((noir_theme *)ns->theme)->name,"default")) {
      E(0,"could not load theme \"%s\" (%s) {%s}, trying to fall back to default...\n",
        (ns&&ns->theme&&((noir_theme *)ns->theme)->name)?((noir_theme *)ns->theme)->name:"???",
        (ns->theme&&((noir_theme *)ns->theme)->path)?((noir_theme *)ns->theme)->path:"???",
        cwd?cwd:"");

/*    free(((noir_theme *)ns->theme)->name); */
      ((noir_theme *)ns->theme)->name=strdup("default");
      goto view_load_theme; }

    ns->start_in=START_IN_BROWSER;

    E(0,"could not load theme \"%s\" (%s) {%s}, disabling canvas...\n",(ns&&ns->theme&&((noir_theme *)ns->theme)->name)?((noir_theme *)ns->theme)->name:"???",(ns->theme&&((noir_theme *)ns->theme)->path)?((noir_theme *)ns->theme)->path:"???",cwd); }

  else
/* #endif */
  {
#if defined(HAVE_EVAS2)||defined(HAVE_GNOMECANVAS)

#  ifdef HAVE_EVAS2
    if(ns->fullscreen)
      ((noir_theme *)ns->theme)->sb_new=NULL;
    E(2,"theme \"%s\" (%s) initialized...\n",((noir_theme *)ns->theme)->name,((noir_theme *)ns->theme)->path);
#  endif

    /* icon-view */
    icon_view_init(ns);

    /* shelf */
    if(!(ns->without_shelf||ns->fullscreen))
      shelf_init(ns);
#endif
  }

  /* tree */
#ifdef WITH_TREEVIEW
  tree_view_init(ns);
#endif

  /* browser */
#ifdef WITH_BROWSERVIEW
  browser_view_init(ns);
#endif

#ifdef HAVE_EVAS2
  { char *about=NULL;
    if(ns->theme&&((noir_theme *)ns->theme)->path&&((noir_theme *)ns->theme)->name) {
      char  *exts[]={ ".png", ".jpg", ".gif", NULL };
      char **ext=exts;
      while(!noir_sess_get_about(ns)&&*ext) {
        if((about=theme_ms(ns->theme,((noir_theme *)ns->theme)->path,((noir_theme *)ns->theme)->name,"","about",*ext))) {
          noir_sess_set_about(ns,about_init(ns,about));
          free(about); }
        ext++; }}
    if(!noir_sess_get_about(ns))
      noir_sess_set_about(ns,about_init(ns,NULL)); }
#else
  noir_sess_set_about(ns,about_init(ns,NULL));
#endif

  /* assemble main-window */

  container=notebook_setup(ns,noir_sess_get_about(ns));
  noir_sess_set_notebook(ns,container);
  if(ns->without_notebook)
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(noir_sess_get_notebook(ns)),FALSE);
  gtk_notebook_set_show_border(GTK_NOTEBOOK(noir_sess_get_notebook(ns)),FALSE);

  g_signal_connect(G_OBJECT(noir_sess_get_notebook(ns)),"switch-page",G_CALLBACK(go2view_activate_page),(gpointer)ns);

  { noir_shelf *sv=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);
    /* setup paned */
    if(sv&&sv->shelf_scroll) {
      GtkWidget *paned=gtk_vpaned_new();
      if(paned) {
        container=paned;
        gtk_paned_pack2(GTK_PANED(paned),sv->shelf_scroll,TRUE,TRUE);
        gtk_widget_set_size_request(sv->shelf_scroll,-1,10);
        gtk_paned_pack1(GTK_PANED(paned),noir_sess_get_notebook(ns),TRUE,FALSE);
        gtk_widget_set_size_request(noir_sess_get_notebook(ns),-1,88); }}}

  gtk_container_add(GTK_CONTAINER(noir_sess_get_window(ns)),container);

  g_signal_connect(G_OBJECT(container),                "button_press_event",   G_CALLBACK(win_clicked),   (gpointer)ns); /* yes, notebook is exactly what we want to connect it to. trust me. */
  g_signal_connect(G_OBJECT(container),                "button_release_event", G_CALLBACK(win_clicked),   (gpointer)ns); /* yes, notebook is exactly what we want to connect it to. trust me. */

  g_signal_connect(G_OBJECT(noir_sess_get_window(ns)), "destroy",              G_CALLBACK(closed),        (gpointer)ns);
  g_signal_connect(G_OBJECT(noir_sess_get_window(ns)), "key-press-event",      G_CALLBACK(win_keyed),     (gpointer)ns);

  g_signal_connect(G_OBJECT(noir_sess_get_window(ns)), "configure-event",      G_CALLBACK(win_configured),(gpointer)ns);

  gdk_window_set_events(gdk_get_default_root_window(), GDK_PROPERTY_CHANGE_MASK);
  gdk_window_add_filter(gdk_get_default_root_window(), (GdkFilterFunc)root_propertied,NULL);

  if(!ns->geometry||!gtk_window_parse_geometry(GTK_WINDOW(noir_sess_get_window(ns)),ns->geometry))
    ns->geometry=0;

  nodeco=ns->without_decorations;
  if(ns->fullscreen) {
    nodeco=TRUE;
    gtk_window_set_default_size(GTK_WINDOW(noir_sess_get_window(ns)),gdk_screen_get_width(gdk_screen_get_default()),gdk_screen_get_height(gdk_screen_get_default()));
    gtk_window_move(GTK_WINDOW(noir_sess_get_window(ns)),0,0);
#if !GTK_CHECK_VERSION(3,0,0)
    gtk_window_set_wmclass(GTK_WINDOW(noir_sess_get_window(ns)),"desktop_window","evidence");
#endif
    gtk_window_maximize(GTK_WINDOW(noir_sess_get_window(ns))); }
  else
    gtk_window_set_default_size(GTK_WINDOW(noir_sess_get_window(ns)),800,600);

  gtk_window_set_decorated(GTK_WINDOW(noir_sess_get_window(ns)),!nodeco);

  /* we need to realize the widget so we actually have a corresponding
     Xwindow to set the hints on.  on the other hand, we can't just
     ..._show_all() and then set the hints, because that confuses e16.
     thanks to RbdPngn for working this out. */
  gtk_widget_realize(noir_sess_get_window(ns));

  /* set application icon */
  set_app_icon(ns);

  if(ns->fullscreen)
#ifdef HAVE_X
/*  gdk_window_set_type_hint(noir_sess_get_window(ns)->window,GDK_WINDOW_TYPE_HINT_DESKTOP); */
    { Display *d=get_display();
      if(d)
        window_as_desk(d,
                   GDK_WINDOW_XID(gtk_widget_get_window(noir_sess_get_window(ns))),
                   gdk_screen_get_width(gdk_screen_get_default()),gdk_screen_get_height(gdk_screen_get_default())); }
#else
    E(0,"cannot flag window as desktop, compiled with --enable-x=no\n");
#endif
  gtk_widget_show_all(noir_sess_get_window(ns));

  if(ns->serial==1)
    go2view_setup(ns);

#ifdef HAVE_EVAS2
  if(ns->theme) {
    page=template->start_in;
    if(!notebook_go2view(ns,START_IN_ICONVIEW)) {
      E(0,"go2view failed, critical at this point...  : (\n"); }
    else
      theme_idle_start(ns);
    template->start_in=page; }
#endif

  /* go */
  if(go2view_activate_page(NULL,NULL,go2view_mapping[ns->start_in],ns)==NOIR_NOTFOUND) {
    E(0,"couldn't activate desired view %d\n",ns->start_in);
    if(noir_sess_focus(ns,ns->cwd)!=NOIR_SUCC) {
      if(noir_sess_focus(ns,cwd)!=NOIR_SUCC) {
        if(noir_sess_focus(ns,home)!=NOIR_SUCC)
          noir_sess_focus(ns,"/"); }}}

  /* desktop is always in iconview, but if the user gave some other
     mode, we'll save that for further windows... */
  page=ns->start_in;
#if defined(HAVE_EVAS2)||defined(HAVE_GNOMECANVAS)
  if(ns->fullscreen)
    ns->start_in=START_IN_ICONVIEW;

  if(ns->start_in==START_IN_ICONVIEW)
#if GTK_CHECK_VERSION(3,0,0)
    icon_view_exposed(((noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW))->icon_view,(cairo_t *)NULL,ns);
#else
    icon_view_exposed(((noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW))->icon_view,NULL,ns);
#endif
#endif

  notebook_go2view(ns,template->start_in);

  ns->start_in=page;

#ifdef HAVE_X
  gdk_window_add_filter(gtk_widget_get_window(noir_sess_get_window(ns)),(GdkFilterFunc)win_event,(gpointer)ns);
#endif

  E(1,"new session: %ld-|%s|\n",ns->serial,ns->cwd);

  return ns->serial; }






/*****************************************************************************/



/**
 * set application icon on a session.
 * @return NOIR_SUCC, or an error-code
 * @param ns - the session
 */
static int set_app_icon(noir_sess *ns) {
  int     ret=NOIR_FAIL;
  gchar  *ip=get_setup_file("filemanager_indigo.png");  /* "evidence_icon.png" */

  if(ip) {
    GdkPixbuf *pb;
    E(2,"using \"%s\" as app-icon...\n",ip);
    if((pb=gdk_pixbuf_new_from_file(ip,NULL))) {
      if(ns&&noir_sess_get_window(ns)) {
        gtk_window_set_icon(GTK_WINDOW(noir_sess_get_window(ns)),pb);
        ret=NOIR_SUCC; }
      else
        E(0,"PixBuf %p, Session %p, Window %p\n",pb,ns,(void *)noir_sess_get_window(ns));
      /* g_object_unref(pb); */
    }
    else
      E(0,"could not create pixbuf from my icon??\n");
    g_free(ip); }
  else
    E(0,"could not find my icon...\n");

  return ret; }



/*****************************************************************************/



#ifdef HAVE_ECORE

static Eina_Bool ecore_gtk_event_handler(void *data,Ecore_Fd_Handler *fd_handler) {
  while(gdk_events_pending())
    g_main_context_iteration(NULL,TRUE);
  return EINA_TRUE; }

static int ecore_gtk_update_handler(void *data) {
#if 0
  { noir_sess     *ns=noir_sess_get_1st();
    noir_iconview *iv;
    icon_list     *il;

    while(ns) {
      if((iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW))&&
         (il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view))) {
        E(0,"updating %ld\n",noir_sess_get_serial(ns));

        evas_damage_rectangle_add(il->canvas,0,0,il->vis_w,il->vis_h);
        evas_obscured_rectangle_add(il->canvas,0,0,il->vis_w,il->vis_h);
        evas_obscured_clear(il->canvas);
        evas_render(il->canvas);

        gtk_widget_queue_draw_area(iv->icon_view,0,0,il->vis_w,il->vis_h);
      }
      ns=noir_sess_get_next(ns); }}
#endif

  g_main_context_iteration(NULL,FALSE);

  return TRUE; }

static Eina_Bool ecore_exit(void *data,int ev_type,void *ev) {
  (void)data; (void)ev_type; (void)ev;
  ecore_main_loop_quit();
  return EINA_TRUE; }
#endif



/*****************************************************************************/



/**
 * create an instance of evidence
 */
noir_sess *gui_init(int argc,char *argv[]) {
  noir_sess *ns;

  if(!(ns=noir_sess_new())) {
    E(0,"out of memory...\n");
    return NULL; }



  /***************************************************************************/
  /* init toolkit */

#ifdef HAVE_GTHREAD
  g_thread_init(NULL);
  prop_bag_prop_set(theme_global,"feature-gthread","engaged");
#endif

  /* this break horribly with GTK>2.2.x for some reason */
#if 0
  if(!XInitThreads()) {
    E(0,"XInitThreads() failed...\n");
    return NULL; }
#endif

  gtk_init(&argc,&argv);
  prop_bag_prop_set(props_global,"feature-gtk2","engaged");

#if defined(HAVE_ECORE)
  ecore_init();
  ecore_app_args_set(argc,(const char **)argv);
  prop_bag_prop_set(props_global,"feature-ecore","engaged");
#endif
#ifdef HAVE_EVAS2
  /* modern EFL requires evas_init() before any Evas canvas is created */
  evas_init();
  prop_bag_prop_set(props_global,"feature-evas","engaged");
#endif

  return ns; }






/*****************************************************************************/



/**
 * run GUI
 */
int gui_start(noir_sess *ns) {
  action_context *sel=NULL,*dirs=NULL;

#ifdef HAVE_ECORE
  Ecore_Fd_Handler    *ecore_gtk_event_handle=NULL;
  void                *ecore_gtk_update_handle=NULL;
  Ecore_Event_Handler *ecore_exit_event_handle=NULL;
#endif



#ifndef HAVE_ECORE
  { struct sigaction  sa;

    sa.sa_sigaction=util_sigchld_handler;
    sa.sa_flags=SA_RESTART|SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD,&sa,NULL); }
/* signal(SIGCHLD,util_sigchld_handler); */
#endif



#ifdef WITH_TREEVIEW
  /* set up tree model */
  { noir_treeview tv;
    memset(&tv,0,sizeof(noir_treeview));
    tv.session=ns;
    tree_model_setup(&tv); }
#endif

  dnd_init(NULL);
  typebuffer_init();

  /* open all requested directories */
  { int          c=0;
    action_file *af=action_context_get_selection(dirs);

    noir_sess_create(SESSION_CREATE,ns,NULL);
    while(af) {
      if(c)
        noir_sess_create(SESSION_COPY,ns,action_file_get_tag(af));
      c++;
      af=action_file_get_next(af); }
    action_context_dst(dirs); }

  view_sel(ns,sel);




  /***************************************************************************/
  /* mainloop */

#if defined(HAVE_ECORE)&&defined(HAVE_X)&&!GTK_CHECK_VERSION(3,0,0)
  prop_bag_prop_const(props_global,"mainloop","eCore");

  { Display *d=get_display();
    if((ecore_gtk_event_handle=ecore_main_fd_handler_add(ConnectionNumber(d),ECORE_FD_READ,ecore_gtk_event_handler,d,NULL,NULL))) {
#if 0
      prop_bag_prop_const(props_global,"widgetset","EWL");
      setenv("WIDGETSET","EWL",1);
#else
      prop_bag_prop_const(props_global,"widgetset","GTK2");
      setenv("WIDGETSET","GTK2",1);
#endif
      ecore_gtk_update_handle=idle_enter_add(0,ecore_gtk_update_handler,NULL);
      ecore_exit_event_handle=ecore_event_handler_add(ECORE_EVENT_SIGNAL_EXIT,ecore_exit,NULL);
      ecore_main_loop_begin(); }
    else
      E(0,"FAIL ECORE\n"); }

#else
#  ifdef HAVE_GTHREAD
  gdk_threads_enter();
#  endif

  setenv("WIDGETSET","GTK2",1);
  prop_bag_prop_const(props_global,"widgetset","GTK2");
  prop_bag_prop_const(props_global,"mainloop","GLib2");

  gtk_main();

#  ifdef HAVE_GTHREAD
  gdk_threads_leave();
#  endif
#endif


  /***************************************************************************/
  /* exit views */

  while(noir_sess_get_1st()) {
    noir_sess *sa=noir_sess_get_1st();
    E(1,"destroying remaining session #%ld\n",sa->serial);
    noir_sess_close(sa); }



  /***************************************************************************/
  /* exit toolkit */

#ifdef HAVE_ECORE
#  ifdef HAVE_EDJE
  { char *edje_state=prop_get_string(prop_bag_prop_get(props_global,"feature-edje"));
    if(edje_state&&!strcmp(edje_state,"engaged")) {
      edje_shutdown();
      prop_bag_prop_set(props_global,"feature-edje","disengaged"); }}
#  endif
  ecore_event_handler_del(ecore_exit_event_handle);
  ecore_main_fd_handler_del(ecore_gtk_event_handle);
  idle_enter_remove(&ecore_gtk_update_handle);
#endif

#if !defined(HAVE_GNOMECANVAS)&&!defined(HAVE_EVAS2)
#  warning compiling without a canvas
#endif

  return NOIR_SUCC; }






/*****************************************************************************/



/**
 * exit GUI
 */
int gui_exit(void) {
  icon_exit();
  dnd_exit(NULL);
  typebuffer_exit();

#ifdef HAVE_ECORE
  /* do this last, as it will unregister all left-over timeouts.
     if we do this first and /then/ have the individual sub-systems
     try to do their own unregistering in their respective cleanup
     routines, eCore will complain. */
  ecore_shutdown();
#endif

#ifdef HAVE_EVAS2
  evas_shutdown();
#endif

  unsetenv("WIDGETSET");

  icon_path_exit();
  paths_exit();

  return NOIR_SUCC; }
