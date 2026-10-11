/** @file  menu_gtk.c
 *  @brief implements the GTK2 specific part of the context-menus.
 */

#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>                     /* malloc(), free() */
#include <sys/stat.h>                   /* S_ISDIR() */

#include "gui/common/session.h"         /* noir_sess_get_selection() */
#include "gui/common/session_private.h" /* struct _noir_sess  (ns->sortflags) */
#include "gui/common/menu.h"            /* struct menu_action */
#include "gui/common/menu_private.h"    /* struct selection_action */
#include "gui/common/alert.h"           /* form_alert() */
#include "gui/common/tooltip.h"         /* tooltip_cancel() */

#include "gui/gtk2/common/notebook.h"   /* notebook_go2view() */

#include "mime.h"
#include "meta.h"            /* meta_in_group(), meta_add(), meta_del_all() */
#include "util.h"            /* mainloop_once() */
#include "action.h"          /* action_context_*(), action_file_*() */
#include "file.h"            /* file_exists(), file_run(),
                                file_get_meta, file_get_mime() */
#include "setup.h"           /* get_path_file() */



extern int go2view_mapping[];



/******************************************************************************/

/* THE CONTEXT-MENU.
 * g_object_*_data(menu_item, "data", meta **meta_data_anchor)
 */


/**
 * closing menu. release all admin data.
 */
static void menu_done(GtkMenuShell *menushell,gpointer data) {
  action_context *ac=(action_context *)data;
  action_context_add_flags(ac,AF_MENUDONE); }



/******************************************************************************/
/* context-menu for the current selection */



/**
 * apply action from context-menu to a single file.
 *
 * called from menu_selection_apply().
 * @param file  the file to apply the action to
 * @param item  the GTK menu item
 * @param menu  the action to be applied to the file
 * @param ac    the action context
 */
static inline gint menu_selection_apply_one(const char *file,GtkWidget *item,menu_action *menu,action_context *ac) {
  gint          ret=FALSE;
  noir_md      *md=NULL;
  noir_sess    *ns=noir_sess_get_by_serial(action_context_get_serial(ac));
  action_file  *sin,*sel=NULL;

  sin=action_context_get_single(ac);
  if(strcmp(action_file_get_tag(sin),file))   /* try single */
    sin=NULL;

  sel=action_context_get_selection(ac);
  while(sel&&strcmp(action_file_get_tag(sel),file))     /* try selection */
    sel=action_file_get_next(sel);

  /* use file from selection if a) desired file for isn't the file that the
     context-menu was called up from, or b) if it doesn't bring meta-data,
     but the entry in the selection does.
     if neither have meta-data, or the desired file is in neither set,
     we'll read the meta-data on the fly. */
  if(sin&&sel&&!action_file_get_meta(sin))
    sin=NULL;
  else if(sin)
    sel=NULL;

  if(sel)
    md=action_file_get_meta(sel);
  else if(sin)
    md=action_file_get_meta(sin);

  if(!md) {                          /* still no meta-data? read them now! */
    int r2;
    E(1,"reading MD for \"%s\"...\n",file);
    if((r2=file_get_meta_only(ns,file,&md))!=NOIR_SUCC)  /* read MD if we really must */
      E(0,"could not get meta-data for \"%s\"... (%d)\n",file,r2); }
  else
    E(1,"already have MD for \"%s\"...\n",file);

  if(md)
    ret=(menu_action_by_entry(action_context_get_serial(ac),menu,md,ac)==NOIR_SUCC);

  if(md) { /* if we allocated meta-data on the fly... */
    if(sin)   /* save it if we have somewhere to save it to... */
      action_file_set_meta(sin,md);
    else if(sel)
      action_file_set_meta(sel,md);
    else                /* otherwise release it */
      meta_dst(&md); }

  return ret; }



/**
 * apply action from context-menu (to one or many).
 *
 * @param item - the GTK menu-item
 * @param data - the action-context
 */
static gint menu_selection_apply_many(GtkWidget *item,gpointer data) {
  menu_action    *m=(menu_action *)g_object_get_data(G_OBJECT(item),"data");
  action_context *ac=(action_context *)data;
  action_file    *af=NULL;

  if(!ac||!(af=action_context_get_selection(ac))) {
    E(0,"insufficient data: ac %p, af %p\n",ac,af);
    return FALSE; }

  action_context_add_flags(ac,AF_SELECTION); /* we have more than one file, set "selection" flag */
  action_context_clr_flags(ac,AF_LAST);      /* we have more than one file, clear "last" flag */

  while(af) {
    if(!action_context_tst_flags(ac,AF_CANCEL_ALL)) {
      if(!action_file_get_next(af))       /* if it's the last item, flag it */
        action_context_add_flags(ac,AF_LAST);
      menu_selection_apply_one(action_file_get_tag(af),item,m,ac); }
    if((action_context_tst_flags(ac,AF_LAST))||!action_context_get_selection(ac))  /* action may steal selection */
      return FALSE;
    af=action_context_remove_and_dst_file(ac,af); }

  return FALSE; }



/**
 * add action 'title' to context-menu 'menu' for current selection.
 *
 * called from menu_selection_create().
 * @param ac     the action-context
 * @param menu   the GTK menu to add to
 * @param title  the title for the menu entry
 * @param data   the data to set on the entry (an action (menu_action *), or NULL)
 */
static GtkWidget *menu_selection_add(action_context *ac,GtkWidget *menu,char *title,gpointer data) {
  GtkWidget *item=gtk_menu_item_new_with_label(title);
  if(!item) {
    E(0,"failed to create item for \"%s\"!?\n",title);
    return NULL; }
  g_signal_connect(item,"activate",G_CALLBACK(menu_selection_apply_many),ac);
  g_object_set_data(G_OBJECT(item),"data",data);
  gtk_menu_shell_append((GtkMenuShell *)menu,item);
  gtk_widget_show(item);
  gtk_widget_set_sensitive(item,TRUE);
  return item; }



/**
 * set menu item insensitve (disabled)
 * @param item  the GTK menu-item to disable
 */
static GtkWidget *menu_selection_insensitive(GtkWidget *item) {
  GdkColor     c;
  GtkStateType t=GTK_STATE_INSENSITIVE|GTK_STATE_NORMAL;
  gdk_color_parse("blue",&c);
  gtk_widget_modify_fg(gtk_bin_get_child(GTK_BIN(item)),t,&c);
  gtk_widget_modify_fg(item,t,&c);
  gtk_widget_set_sensitive(item,FALSE);
  return item; }



/**
 * create a sub-menu
 * @return the sub-menu, or NULL
 * @param menu  the menu to add the sub-menu to
 * @param mime  the MIME-type the sub-menu is for
 */
static GtkWidget *menu_submenu_new(GtkWidget *menu,char *mime) {
  GtkWidget *sub=gtk_menu_new(),*item;
  if(sub) {
    char *smt=menu_submenu_title_from_mime_type(mime);
    if(smt) {
      if((item=gtk_menu_item_new_with_label(smt))) {
        gtk_menu_shell_append((GtkMenuShell *)menu,item);
        gtk_widget_show(item);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),sub); }
      free(smt); }}
  return sub; }



/**
 * create context-menu for current selection.
 * @return a GTK menu-item
 * @param ac  the action-context
 */
void *menu_selection_create(action_context *ac) {
  noir_sess        *ns=noir_sess_get_by_serial(action_context_get_serial(ac)); /* ### */
  GtkWidget        *menu=gtk_menu_new(),*item,*sub=NULL;
  char             *m;
  action_file      *af=action_context_get_selection(noir_sess_get_selection(ns));
  const char       *w;
  selection_action *e=NULL,*f;
  int               with_submenus=TRUE;

  if((item=menu_selection_add(ac,menu,"Selection",(gpointer)NULL)))
    menu_selection_insensitive(item);

  if(af) {
    /* determine available actions from files in selection. */
    while(af) {
      menu_selection_file_add(ac,action_file_get_tag(af),&e);
      af=action_file_get_next(af); }

    /* add actions to menu. */

    f=e;
    if(with_submenus)
      sub=menu_submenu_new(menu,f->menu->mime);
    while(f) {
      E(2,"%s; %s; %s\n",f->menu->title,f->menu->mime,f->menu->file);
      e=f->next;
      if(!menu_selection_add(ac,sub,f->menu->title,(gpointer)f->menu))
        E(0,"failed to add \"%s\" for \"%s\"\n",f->menu->title,f->menu->mime);
      if(!e||(e->section!=f->section)) {
        if(with_submenus) {
          if(e)
            sub=menu_submenu_new(menu,e->menu->mime); }
        else if((item=gtk_separator_menu_item_new()))
          gtk_menu_shell_append((GtkMenuShell *)menu,item); }

      free(f);
      f=e; }}

  /* add entry "SelectedFilesName only" */
  w=strrchr(action_file_get_tag(action_context_get_single(ac)),'/');
  w=w?w+1:action_file_get_tag(action_context_get_single(ac));
  m=NULL;
  esprintf(&m,"\"%s\" only",w);
  if(m) {
    item=menu_selection_add(ac,menu,m,(gpointer)NULL);
    free(m); }
  else
    item=menu_selection_add(ac,menu,"Current file only",(gpointer)NULL);
  menu_selection_insensitive(item);

  return menu; }



/*****************************************************************************/



/**
 * wrapper for menu_selection_apply_one so it can be called by GTK.
 *
 * this is called when a menu-entry pertaining to a single file (rather than
 * a selection of files) is activated; see menu_add.  in this case, NULL will
 * be passed to menu_selected_do in lieu of an action_context.
 * @param item  the menu-item that was selected. its "data" property will contain
 *              the anchor of the file's meta-data.
 * @param data  the menu-action.
 */
static gint menu_single_apply(GtkWidget *item,gpointer data) {
  menu_action    *m=(menu_action *)g_object_get_data(G_OBJECT(item),"data");
  action_context *ac=(action_context *)data;
  action_file    *af=action_context_get_single(ac);
  return menu_selection_apply_one(action_file_get_tag(af),item,m,ac); }



/**
 * add action to context-menu.
 * @param menu  the GTK menu
 * @param m     the menu-action
 * @param ac    the action-context
 */
static void menu_add(GtkWidget *menu,menu_action *m,action_context *ac) {
  GtkWidget *item=gtk_menu_item_new_with_label(m->title);
  if(!item)
    return;
  g_signal_connect(item,"activate",G_CALLBACK(menu_single_apply),ac);
  g_object_set_data(G_OBJECT(item),"data",m);
  gtk_menu_shell_append((GtkMenuShell *)menu,item); }



/**
 * count items in context-menu.
 * @param widget  an item in the menu (ignored)
 * @param data    a pointer to the counter-variable
 */
static void menu_count_items(GtkWidget *widget,gpointer data) {
  int *nc=(int *)data;
  *nc=(*nc+1); }



/**
 * create and display context-menu (according to item this menu is for).
 * @return <0 (error-code) or >=0 (# of items in menu; 0==none,
 * meaning that no menu is shown)
 * @param ac    the action-context
 * @param menu  menu-object (may hand-in previously made menu with extra items;
 *              created inside if NULL)
 */
static int menu_create(action_context *ac,GtkWidget *menu) {
  menu_action *m;
  char        *p,*mime=NULL;
  int          ret=0,internal=0,internal_wd=0;
  noir_meta   *mm;
  char        *mi=NULL;
  GtkWidget   *sub=NULL;

  if(!ac)
    return NOIR_FAIL;

  if(!menu) {
    if(!(menu=gtk_menu_new()))
      return NOIR_OOM; }

  if(action_context_get_single(ac)) {
    if((mm=meta_in_group(action_file_get_meta(action_context_get_single(ac)),"MIME",MY_NAME)))
      mi=(char *)meta_get_val(mm);

    if(mi) {
      if(!(mime=strdup(mi))) {
        g_object_unref(menu);
        return NOIR_OOM; }

      if(!strcmp(mime,"unknown"))
        *mime='\0';

      /* handlers for this MIME-type: text/plain */
    mc_add_for_mime:
      if(strlen(mime)) {
        m=menu_action_get_anchor();
        if(menu_action_by_mime_first(&m,mime))
          sub=menu_submenu_new(menu,mime);
        while(m) {
          ret++;
          menu_add(sub,m,ac);
          menu_action_by_mime_next(&m,mime); }

        /* handlers for this MIME-group: text/ */
        if((p=strchr(mime,'/'))) {
          internal_wd=!strcmp(mime,MIME_INTL_MB3);
          *(++p)='\0';
          if(strcmp(mime,MIME_INTL)) {
            m=menu_action_get_anchor();
            if(menu_action_by_mime_first(&m,mime))
              sub=menu_submenu_new(menu,mime);
            while(m) {
              ret++;
              menu_add(sub,m,ac);
              menu_action_by_mime_next(&m,mime); }}
          else {
            internal=1;
            if(internal_wd) {
              char *m;
              if((m=strdup(MIME_FOLDER))) {
                free(mime);
                mime=m;
                goto mc_add_for_mime; }}}}
        *mime='\0'; }

      /* wild-card handlers */
      if(!internal) {
        m=menu_action_get_anchor();
        if(menu_action_by_mime_first(&m,mime))
          sub=menu_submenu_new(menu,mime);
        while(m) {
          ret++;
          menu_add(sub,m,ac);
          menu_action_by_mime_next(&m,mime); }}}}

  if(ret>=0) {
    /* count items in menu. can't use ret for that as the caller might have
       handed in a non-empty menu:  count actual items, theirs and ours. */
    gtk_container_foreach(GTK_CONTAINER(menu),menu_count_items,(gpointer)&ret);
    if(ret>0) {
      gtk_widget_show_all(menu);
      g_signal_connect(G_OBJECT(menu),"selection-done",G_CALLBACK(menu_done),(gpointer)ac);
#if GTK_CHECK_VERSION(3,0,0)
      gtk_menu_popup_at_pointer(GTK_MENU(menu),NULL);
#else
      gtk_menu_popup(GTK_MENU(menu),NULL,NULL,NULL,NULL,0,0L);
#endif
  }}

  if(mime)
    free(mime);

  return ret; }



/**
 * handle a menu.
 *
 * (called from menu_file_context() for context-menus and from root_menu() for
 * "root"-menus.)
 * @param ac    the action-context
 * @param menu  a pre-made GTK-menu to append entries to, or NULL to create a new menu
 */
int menu_run(action_context *ac,void *menu) {
  noir_sess *ns=noir_sess_get_by_serial(action_context_get_serial(ac));
  int        ret;

  tooltip_cancel(ns);

  ret=menu_create(ac,(GtkWidget *)menu);

  if(ret<=0) {
    form_alert(ns,"No entries in menu.\nDid you 'make install'?");
    ret=NOIR_NOTFOUND; }
  else {
    while(!action_context_tst_flags(ac,AF_MENUDONE))
      mainloop_once();
    ret=NOIR_SUCC; }

  return ret; }






/******************************************************************************/
/* "root-menus" */



/**
 * switch sort-order for icon-view.
 * @param item  the GTK menu-item that was activated.
 *              "data" prop holds sort-flags to set.
 * @param data  the session.
 */
static gint root_menu_sort(GtkWidget *item,gpointer data) {
  noir_sess **sess=(noir_sess **)data;
  gpointer    para=g_object_get_data(G_OBJECT(item),"data");
  int         act=gtk_check_menu_item_get_active((GtkCheckMenuItem *)item);

  if(act)
    (*sess)->sortflags|=GPOINTER_TO_INT(para);
  else
    (*sess)->sortflags&=~GPOINTER_TO_INT(para);

  E(1,"resort with 0x%x...\n",(*sess)->sortflags);

  /* force resort... */
  noir_sess_redraw_files((*sess));

  return FALSE; }



/**
 * switch views from "root-menu".
 * @param item  the GTK menu-item that was activated.
 *              "data" prop holds index of view to activate.
 * @param data  the session.
 */
#if 0
static gint root_menu_grid(GtkWidget *item,gpointer data) {
  noir_sess **sess=(noir_sess **)data;
  gpointer    para=g_object_get_data(G_OBJECT(item),"data");
  noir_theme *theme=noir_sess_get_theme(*sess);

  /* force resort... */

  if(theme_el_set(&theme->data,"iconlistgrid",)==NOIR_SUCC)
    noir_sess_redraw_files((*sess));
  return FALSE; }
#endif



/**
 * switch views from "root-menu".
 * @param item  the GTK menu-item that was activated.
 *              "data" prop holds index of view to activate.
 * @param data  the session.
 */
static gint root_menu_switch2(GtkWidget *item,gpointer data) {
  noir_sess **sess=(noir_sess **)data;
  gpointer    para=g_object_get_data(G_OBJECT(item),"data");
  notebook_go2view(*sess,GPOINTER_TO_INT(para));
  return FALSE; }



/**
 * edit preferences (call examine)
 * @param item  the GTK menu-item that was activated.
 *              "data" prop holds index of view to activate.
 * @param data  the session.
 */
static gint root_menu_preferences(GtkWidget *item,gpointer data) {
  noir_sess **sess=(noir_sess **)data;
  noir_sess  *ns=sess?*sess:NULL;
  const char *cwd=ns?noir_sess_get_cwd(ns):NULL;
  char       *exa=get_path_file(cwd,"examine");
  int         ret;

  if(exa) {
    E(0,"starting %s...\n",exa);
    if((ret=file_run(ns,exa,"evidence",NULL))!=NOIR_SUCC)
      form_alert(ns,"Failed to open preferences dialog? (%s)\n(%s evidence)",ret,exa);
    free(exa); }
  else
    form_alert(ns,"Failed to find preferences dialog (\"examine\")?");

  return FALSE; }



/**
 * add an item to a "root-menu".
 * @param sess     the session
 * @param menu     the GTK menu
 * @param title    the label for the new entry
 * @param data     the value for the "data" prop (normally index of view to activate)
 * @param handler  the function to call when this item is activated
 */
static GtkMenuItem *root_menu_add(noir_sess **sess,GtkWidget *menu,char *title,gpointer data,gint (*handler)(GtkWidget *,gpointer)) {
  GtkWidget *item;

  if(!title)
    item=gtk_separator_menu_item_new();
  else {
    item=gtk_menu_item_new_with_mnemonic(title);
    if(handler)
      g_signal_connect(item,"activate",G_CALLBACK(handler),sess); }

  g_object_set_data(G_OBJECT(item),"data",data);
  gtk_menu_shell_append((GtkMenuShell *)menu,item);
  return (GtkMenuItem *)item; }



/**
 * add an check-item to a "root-menu".
 * @param sess     the session
 * @param menu     the GTK menu
 * @param title    the label for the new entry
 * @param data     the value for the "data" prop (normally index of view to activate)
 * @param handler  the function to call when this item is activated
 */
static GtkMenuItem *root_sort_add(noir_sess **sess,GtkWidget *menu,char *title,gpointer data,gint (*handler)(GtkWidget *,gpointer)) {
  GtkWidget *item;

  if(!title)
    item=gtk_separator_menu_item_new();
  else {
    item=gtk_check_menu_item_new_with_mnemonic(title);
    gtk_check_menu_item_set_active((GtkCheckMenuItem *)item,(((*sess)->sortflags)&GPOINTER_TO_INT(data))); }

  g_object_set_data(G_OBJECT(item),"data",data);
  gtk_menu_shell_append((GtkMenuShell *)menu,item);
  if(handler)
    g_signal_connect(item,"toggled",G_CALLBACK(handler),sess);
  return (GtkMenuItem *)item; }



/**
 * create and show "root-menu".
 * @param ns   session
 * @param cwd  current work-dir (may differ from session's if view can show more than one directory at a time)
 * @param mb   the mouse-button that was pressed to open the menu
 */
int menu_root_context(noir_sess *ns,const char *cwd,int mb) {
  action_context *ac;
  action_file    *af=NULL;
  int             ret=NOIR_FAIL;

  E(2,"menu in \"%s\"...\n",cwd?cwd:"");

  if(!(ac=action_context_new(noir_sess_get_serial(ns),cwd)))
    return NOIR_OOM;
  if(cwd&&!(af=action_file_new(NULL,cwd))) {
    action_context_dst(ac);
    return NOIR_OOM; }
  action_context_set_single(ac,af);

  if(mb==3) {  /* built-in root menu */
    noir_sess   *sess=ns;
    GtkWidget   *menu=gtk_menu_new();
    GtkWidget   *sort=NULL;
    GtkMenuItem *si=NULL;

    if(!noir_sess_get_desktop(ns)) {
      if(go2view_mapping[START_IN_ICONVIEW]>=0)
        root_menu_add(&sess,menu,"_Icon-view",(gpointer)START_IN_ICONVIEW,root_menu_switch2);
      if(go2view_mapping[START_IN_BROWSER]>=0)
        root_menu_add(&sess,menu,"_Browser",  (gpointer)START_IN_BROWSER, root_menu_switch2);
      if(go2view_mapping[START_IN_TREEVIEW]>=0)
        root_menu_add(&sess,menu,"_Tree-view",(gpointer)START_IN_TREEVIEW,root_menu_switch2);
      if(go2view_mapping[START_IN_CREDITS]>=0)
        root_menu_add(&sess,menu,"_Credits",(gpointer)START_IN_CREDITS,root_menu_switch2); }

#if 0
    root_menu_add(&sess,menu,NULL,NULL,NULL);
    root_sort_add(&sess,menu,"Align using _grid",GINT_TO_POINTER(ns),root_menu_grid);
#endif

    if(cwd) {
      if(!noir_sess_get_desktop(ns))
        root_menu_add(&sess,menu,NULL,NULL,NULL);

      if((si=root_menu_add(&sess,menu,"Sort",(gpointer)NULL,NULL))) {
        sort=gtk_menu_new(); }
      else
        sort=menu;

      root_sort_add(&sess,sort,"Show _unsorted",GINT_TO_POINTER(SORT_UNSORTED),root_menu_sort);
      root_sort_add(&sess,sort,"_abc = ABC",GINT_TO_POINTER(SORT_CASEFOLD),root_menu_sort);
      root_sort_add(&sess,sort,"_Folders first",(gpointer)SORT_DIRS1ST,root_menu_sort);
      root_sort_add(&sess,sort,"_The Smiths = Smiths",(gpointer)SORT_BANDNAMES,root_menu_sort);
      root_sort_add(&sess,sort,"by _meta-data",(gpointer)SORT_BYMETA,root_menu_sort);
      root_sort_add(&sess,sort,"by _user",(gpointer)SORT_BYUSER,root_menu_sort);
      root_sort_add(&sess,sort,"by _size",(gpointer)SORT_BYSIZE,root_menu_sort);
      root_sort_add(&sess,sort,"by _date",(gpointer)SORT_BYDATE,root_menu_sort);
      root_sort_add(&sess,sort,"by _MIME",(gpointer)SORT_BYMIME,root_menu_sort);

      if(menu!=sort)
        gtk_menu_item_set_submenu(si,sort); }

    root_menu_add(&sess,menu,NULL,NULL,NULL);
    root_menu_add(&sess,menu,"_Preferences",(gpointer)ns,root_menu_preferences);

    if(af) {
      root_menu_add(&sess,menu,NULL,NULL,NULL);

      meta_add(action_file_get_meta(af),META_RO,META_STR,"Tag","File's name",MY_NAME,cwd?cwd:"/",NULL);
      meta_add(action_file_get_meta(af),META_RO,META_STR,"MIME","File's MIME-type",MY_NAME,MIME_INTL_MB3,NULL); }

    menu_run(ac,menu);

    ret=NOIR_SUCC; }

  else {       /* user-def root menu */
    meta_add(action_file_get_meta(af),META_RO,META_STR,"Tag","File's name",MY_NAME,cwd,NULL);
    meta_add(action_file_get_meta(af),META_RO,META_STR,"MIME","File's MIME-type",MY_NAME,MIME_INTL_MB1,NULL);
    ret=menu_run(ac,NULL); }

  action_context_dst(ac);

  return ret; }






/******************************************************************************/
/* path menu -- a menu containing all paths leading up to this one */



/**
 * a path was selected from path-menu, cd to it.
 * @param item - the selected entry, "data" prop holds index into session->cwd
 * @param data - the session
 */
static gint path_menu_selected(GtkWidget *item,gpointer data) {
  noir_sess *ns=(noir_sess *)data;
  noir_sess_focus(ns,file_info_get_tag(g_object_get_data(G_OBJECT(item),"data")));
  return TRUE; }



/**
 * add a path to the path-menu.
 * @param ns - session
 * @param menu - the GTK menu
 * @param fi - the file to make an entry for
 */
static void path_menu_add_path(noir_sess *ns,GtkWidget *menu,file_info *fi) {
  GtkWidget *item=gtk_menu_item_new_with_label(file_info_get_name(fi));
  g_object_set_data(G_OBJECT(item),"data",fi);
  g_signal_connect(item,"activate",G_CALLBACK(path_menu_selected),ns);
  gtk_menu_shell_append((GtkMenuShell *)menu,item); }



/**
 * show menu containing all paths leading up to cwd.
 * @param ns - session
 * @param button - what mouse-button caused the menu to open?
 * @param when - when did the click occur?
 */
int menu_path(noir_sess *ns,int button,long when) {
  GtkWidget *menu;
  file_info *fi,*cwd;
  int        lvls=0,lvl,l;

  if(!(cwd=file_info_fetch(NULL,noir_sess_get_cwd(ns))))
    return NOIR_NODATA;
  for(fi=cwd;fi&&(fi=file_info_get_parent(fi));lvls++)
    ;
  if(!lvls)
    return NOIR_IGNORED;
  menu=gtk_menu_new();
  for(lvl=0;lvl<=lvls;lvl++) {
    for(l=lvls,fi=cwd;l>lvl;l--)
      fi=file_info_get_parent(fi);
    path_menu_add_path(ns,menu,fi); }

  gtk_widget_show_all(menu);
#if GTK_CHECK_VERSION(3,0,0)
  gtk_menu_popup_at_pointer(GTK_MENU(menu),NULL);
#else
  gtk_menu_popup(GTK_MENU(menu),NULL,NULL,NULL,NULL,button,when);
#endif

  return NOIR_SUCC; }






/*****************************************************************************/
/* subdirs menu -- a menu containing all folders contained in this one */



/**
 * show menu containing all paths leading up to cwd.
 * @param ns - session
 * @param pa - the directory the sub-folders of which to offer
 * @param button - what mouse-button caused the menu to open?
 */
int menu_subdirs(noir_sess *ns,file_info *pa,int button) {
  GtkWidget *menu;
  file_info *fi;

  if(!pa)
    return NOIR_FAIL;
  if(!(fi=file_info_get_1st_child(pa)))
    return NOIR_NODATA;

  menu=gtk_menu_new();
  while(fi) {
    if((file_info_get_mode(fi)>=0)&&(S_ISDIR(file_info_get_mode(fi))))
      path_menu_add_path(ns,menu,fi);
    fi=file_info_get_next(fi); }

  gtk_widget_show_all(menu);
#if GTK_CHECK_VERSION(3,0,0)
  gtk_menu_popup_at_pointer(GTK_MENU(menu),NULL);
#else
  gtk_menu_popup(GTK_MENU(menu),NULL,NULL,NULL,NULL,button,0);
#endif

  return NOIR_SUCC; }






/*****************************************************************************/
