/** @file  tree_view.c
 *  @brief implements tree-view
 *
 * Too much dark magic happens in here -- throw out tree-tagging.
 */

#define DEBUG

#include "config.h"

#include "src/gui/common/session.h"

#include "src/gui/gtk2/common/session_gtk2.h"

#include <gtk/gtk.h>
#include <unistd.h>       /* geteuid */
#include <sys/types.h>    /* uid_t */
#include <string.h>
#include <stdio.h>        /* stderr */
#include <stdlib.h>
#include <sys/stat.h>     /* S_ISDIR() */

#include "gui/gtk2/common/keysyms.h"
#include "gui/gtk2/common/dnd.h"          /* file_get_from_drop(), file_drop() */
#include "gui/gtk2/common/clipboard.h"    /* selection_update(), clipboard_get() */

#include "gui/gtk2/common/tree/tree.h"         /* FILE_..., N_COLUMNS, SORT_BY_... */
#include "gui/gtk2/common/tree/cr_date.h"
#include "gui/gtk2/common/tree/cr_size.h"

#include "gui/gtk2/tree_view/tree_view.h"    /* CLIENT_TREEVIEW */

#include "gui/common/alert.h"        /* form_alert() */
#include "gui/common/menu.h"         /* menu_file_context(), menu_root_context() */
#include "gui/common/typebuffer.h"   /* typebuffer_tab() */
#include "gui/common/tooltip.h"      /* tooltip_cancel() */

#include "model.h"        /* file_info_fetch() */
#include "file.h"         /* file_activate() */
#include "monitors.h"     /* file_monitor_add(), file_monitor_remove() */
#include "file_lcl.h"     /* file_to_URL() */
#include "mime.h"         /* MIME_INTL_INF */

#include "model_private.h"  /* ### FIXME */



typedef enum {
  FIDD_NOTFOUND=0,
  FIDD_FOUND=1,
  FIDD_EMPTY=2 } fidd_result;

typedef enum {
  FID_NOTFOUND=0,
  FID_FOUND=1,
  FID_CREATED=2 } fid_result;



#ifdef WITH_TREEVIEW
static fid_result tree_get_iter(noir_sess *,const char *);
static int tree_view_evt(file_info *,model_event,file_flags,void *);



/****************************************************************************/



/**
 * set up an empty GTK tree-model for tree-view
 * @return a GTK tree-store
 * @param tv  the tree-view this store is for
 */
GtkTreeStore *tree_model_setup(noir_treeview *tv) {
  static GtkTreeStore *store=NULL;
  static GtkTreeIter   it;

  if(!store) {
    store=gtk_tree_store_new(N_COLUMNS,
                             G_TYPE_STRING,  /* name */
                             G_TYPE_STRING,  /* file */
                             G_TYPE_INT,     /* mode */
                             G_TYPE_POINTER, /* stat */
                             G_TYPE_POINTER, /* info */
                             G_TYPE_STRING); /* MIME */

    if(tv&&tv->session) {
      gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store),SORT_BY_NAME,tree_sort,noir_sess_get_sortflags(tv->session),NULL);
      gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store),tree_sort,noir_sess_get_sortflags(tv->session),NULL);
      gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,GTK_SORT_ASCENDING); }

    gtk_tree_store_append(GTK_TREE_STORE(store),&it,NULL);
    gtk_tree_store_set(GTK_TREE_STORE(store),&it,FILE_NAME,get_hostname(),FILE_FILE,"/",-1); }

  if(tv&&tv->pi)
    memmove(tv->pi,&it,sizeof(GtkTreeIter));

  return store; }






/****************************************************************************/
/* Selections */
/**************/



static int tree_view_zoom(noir_sess *,char *);

/**
 * "selection-changed" callback for tree-view.
 * @param sel   the new selection
 * @param data  the session
 */
static void tree_selection_changed(GtkTreeSelection *sel,gpointer data) {
  noir_sess      *ns=(noir_sess *)data;
  noir_treeview  *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);
  GList          *list;
  action_context *ac=NULL;

  if(gtk_notebook_page_num(GTK_NOTEBOOK(noir_sess_get_notebook(ns)),tv->tree_scroll)!=gtk_notebook_get_current_page(GTK_NOTEBOOK(noir_sess_get_notebook(ns))))
    return;

  sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(tv->tree_view));

  if((list=gtk_tree_selection_get_selected_rows(sel,(GtkTreeModel **)&(tv->store)))) {
    int             n=g_list_length(list),m;
    GtkTreePath    *tp;
    GtkTreeIter     it;
    gchar          *file;

    if(!(ac=action_context_new(noir_sess_get_serial(ns),noir_sess_get_cwd(ns))))
      return;

    for(m=0;m<n;m++) {
      tp=(GtkTreePath *)g_list_nth_data(list,m);
      if(tp) {
        if(gtk_tree_model_get_iter(GTK_TREE_MODEL(tv->store),&it,tp)) {
          gtk_tree_model_get(GTK_TREE_MODEL(tv->store),&it,FILE_FILE,&file,-1);
          if(file) {
            action_file_new(ac,file);
            g_free(file); }}
        gtk_tree_path_free(tp); }}

    g_list_free(list); }

  /* update selection in session */
  noir_sess_set_selection(ns,ac);

  /* propagate new selection to other views in session */
  selection_update(ns,noir_sess_client_get(ns,CLIENT_TREEVIEW)); }



/**
 * set selection in tree-view.
 * @return an error-code
 * @param data  the session
 */
int tree_view_set_selection(void *data) {
  noir_sess        *ns=(noir_sess *)data;
  noir_treeview    *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);
  GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(tv->tree_view));

  if(sel) {
    action_file *af;

    gtk_tree_selection_unselect_all(sel);

    if((af=action_context_get_selection(noir_sess_get_selection(ns)))) {
      while(af) {
        E(0,"selecting \"%s\"\n",action_file_get_tag(af));
        if(tree_get_iter(ns,action_file_get_tag(af))!=FID_NOTFOUND)
          gtk_tree_selection_select_iter(sel,tv->ci);
        af=action_file_get_next(af); }
      return NOIR_SUCC; }}
  else
    E(0,"no selection!?\n");

  return NOIR_FAIL; }






/****************************************************************************/
/* Drag'N'Drop */
/***************/



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
static void tree_drag_data_received(GtkWidget *widget,GdkDragContext *dc,gint x,gint y,GtkSelectionData *selection,guint info,guint timestamp,gpointer data) {
  noir_sess   *ns=(noir_sess *)data;
  GtkTreePath *tp;
  GtkTreeIter it;
  gint mode;
  GtkTreeViewDropPosition type;
  gchar *d,*a;
  gchar *tgt=NULL,*file;
  noir_treeview    *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);

  if(!ns)
    return;

  if(gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget),x,y,&tp,&type)) {
    if(gtk_tree_model_get_iter(GTK_TREE_MODEL(tv->store),&it,tp)) {
      gtk_tree_model_get(GTK_TREE_MODEL(tv->store),&it,FILE_FILE,&tgt,FILE_MODE,&mode,-1);
      if((type==GTK_TREE_VIEW_DROP_BEFORE)||(type==GTK_TREE_VIEW_DROP_AFTER)||!S_ISDIR(mode)) {
        if(tgt)
          g_free(tgt);
        tgt=NULL;
        gtk_tree_path_up(tp);
        if(gtk_tree_model_get_iter(GTK_TREE_MODEL(tv->store),&it,tp)) {
          gtk_tree_model_get(GTK_TREE_MODEL(tv->store),&it,FILE_FILE,&tgt,FILE_MODE,&mode,-1); }}}
    gtk_tree_path_free(tp); }

  if(tgt) {
    if(!selection||(selection->length<1)) {
      E(0,"no data (%p,%d)\n",selection,selection?selection->length:-1234); }
    else if((selection->length>=0)&&(selection->format==8)) {
      if((a=d=malloc(selection->length+1))) {
        strncpy(a,selection->data,selection->length);
        a[selection->length]='\0';
        E(0,"\"%s\" for \"%s\"\n",a,tgt);
        while((file=dnd_get_file_from_drop(&d)))
          file_drop(ns,dc->action,info,file,tgt);
        g_free(a); }}
    g_free(tgt); }}



/**
 * DND: drop target asks for data. collect and send.
 * @param widget         the widget that was dragged from, or NULL
 * @param dc             the drag-context
 * @param selectionData  the received data
 * @param info           type of the data as per GtkTargetList
 * @param timestamp      when the event happened
 * @param data           session
 */
static void tree_drag_data_get(GtkWidget *widget,GdkDragContext *dc,GtkSelectionData *selectionData,guint info,guint timestamp,gpointer data) {
  noir_sess        *ns=data;
  GtkTreeSelection *selection=NULL;
  noir_treeview    *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);

  if(!ns)
    return;

  if(selectionData&&(selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(tv->tree_view)))) {
    gchar *reply=NULL;

    if((info==DRAGDROP_TARGET_STRING)||(info==DRAGDROP_TARGET_TEXT)) {
      gtk_tree_selection_selected_foreach(selection,tree_send_string,&reply); }
    else if((info==DRAGDROP_TARGET_NETSCAPE_URL)||(info==DRAGDROP_TARGET_URL)) {
      gtk_tree_selection_selected_foreach(selection,tree_send_URL,&reply); }
    if(reply) {
      gtk_selection_data_set(selectionData,selectionData->target,8,reply,strlen(reply));
      g_free(reply); }}}






/****************************************************************************/
/* Keys'N'Clicks */
/*****************/



/**
 * display only the segment of the tree that contains 'dir' (^X-n-n : )
 * @return an error-code
 * @param ns   the session
 * @param dir  the directory to "zoom" on
 */
static int tree_view_zoom(noir_sess *ns,char *dir) {
  if(!strcmp(dir,"/"))
    form_alert(ns,"Unzoom -- not implemented yet... : (");
  else
    form_alert(ns,"Zoom -- not implemented yet... : (");
  return NOIR_SUCC; }



/**
 * callback for key-events
 * @return TRUE for handled (allow no further handlers), FALSE otherwise
 * @param widget  the widget this event is for
 * @param ev      the GDK key-event
 * @param data    the session
 */
static gboolean tree_keyed(GtkWidget *widget,GdkEventKey *ev,gpointer data) {
  noir_sess         *ns=(noir_sess *)data;
  GtkTreePath       *tp;
  GtkTreeViewColumn *fc;
  GtkTreeIter        myiter;
  gchar             *file=NULL,*mime=NULL;
  gint               isdir,ret=FALSE;
  noir_treeview    *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);

  if(!ns||!tv->tree_view)
    return FALSE;

  gtk_tree_view_get_cursor(GTK_TREE_VIEW(tv->tree_view),&tp,&fc);
  if(!tp) { /* no cursor yet? */
    tp=gtk_tree_path_new_first(); }

  E(1,"%d %.*s, state %d\n",ev->keyval,ev->length,ev->string,ev->state);

  if(gtk_tree_model_get_iter(GTK_TREE_MODEL(tv->store),&myiter,tp)) {
 /* GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(tv->tree_view)); */
    gtk_tree_model_get(GTK_TREE_MODEL(tv->store),&myiter,FILE_FILE,&file,FILE_MODE,&isdir,FILE_MIME,&mime,-1);
    if(file) {
      if(!isdir)
        isdir=file_exists(file);

      if(ev->state&GDK_CONTROL_MASK) {
        gtk_tree_path_free(tp);
        tp=NULL;

        switch(ev->keyval) {
        case GDK_Z:
          tree_view_zoom(ns,"/");
          ret=TRUE;
          break;
        case GDK_z:
          if(!S_ISDIR(isdir)) {
            char *p=strrchr(file,'/');
            if(p) {
              if(p>file)
                *p='\0';
              else
                p[1]='\0'; }}
          tree_view_zoom(ns,file);
          ret=TRUE;
          break;
        case GDK_r: {   /* ^R - rename dialog */
          int ret;
          if((ret=menu_action_by_mime(noir_sess_get_serial(ns),MIME_INTL_REN,file,noir_sess_get_selection(ns)))!=NOIR_SUCC)
            E(0,"could not open rename dialog (type \"" MIME_INTL_REN "\"): %d\n",ret); }
          ret=TRUE;
          break;
        case GDK_p: {   /* ^P - properties dialog */
          int ret;
          if((ret=menu_action_by_mime(noir_sess_get_serial(ns),MIME_INTL_INF,file,noir_sess_get_selection(ns)))!=NOIR_SUCC)
            E(0,"could not open properties dialog (type \"" MIME_INTL_INF "\"): %d\n",ret); }
          ret=TRUE;
          break;
        case GDK_i: {
          gint x,y;
          gdk_window_get_pointer(gtk_widget_get_window(widget),&x,&y,NULL);
          tooltip_show_for(ns,file,x,y); }
          ret=TRUE;
          break;
        case GDK_m:
          menu_file_context(ns,noir_sess_get_cwd(ns),file);
          ret=TRUE;
          break; }}

      else {
        switch(ev->keyval) {
        case GDK_Menu:
          menu_file_context(ns,noir_sess_get_cwd(ns),file);
          ret=TRUE;
          break;
        case GDK_F2:     /* F2 - rename dialog */
          if(!(ev->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK))) {
            int res;
            if((res=menu_action_by_mime(noir_sess_get_serial(ns),MIME_INTL_REN,file,noir_sess_get_selection(ns)))!=NOIR_SUCC)
              E(0,"could not open rename dialog (type \"" MIME_INTL_REN "\"): %d\n",res);
            ret=TRUE; }
          break;
        case GDK_Left:
          if((ev->state&GDK_SHIFT_MASK)&&tp) {
            if(!S_ISDIR(isdir)||!gtk_tree_view_row_expanded(GTK_TREE_VIEW(tv->tree_view),tp)) {
              gtk_tree_path_up(tp);
              gtk_tree_view_set_cursor(GTK_TREE_VIEW(tv->tree_view),tp,NULL,FALSE); }
            else
              gtk_tree_view_collapse_row(GTK_TREE_VIEW(tv->tree_view),tp);
            ret=TRUE; }
          break;
        case GDK_Right:
          if((ev->state&GDK_SHIFT_MASK)&&tp) {
            if(S_ISDIR(isdir)) {
              if(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tv->tree_view),tp)) {
                gtk_tree_path_down(tp);
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(tv->tree_view),tp,NULL,FALSE); }
              else
                gtk_tree_view_expand_row(GTK_TREE_VIEW(tv->tree_view),tp,FALSE); }
            else {
              gtk_tree_path_next(tp);
              gtk_tree_view_set_cursor(GTK_TREE_VIEW(tv->tree_view),tp,NULL,FALSE); }
            ret=TRUE; }
          break;
        case GDK_Tab:
          if(typebuffer_is_open(ns)) {
            typebuffer_tab(ns);
            ret=TRUE; }
          break;
        case GDK_Return:
          if(S_ISDIR(isdir)) {
            if(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tv->tree_view),tp))
              gtk_tree_view_collapse_row(GTK_TREE_VIEW(tv->tree_view),tp);
            else {
              gtk_tree_view_expand_row(GTK_TREE_VIEW(tv->tree_view),tp,FALSE);
           /* noir_sess_focus(ns,file); */
            }}
          else {
            char *dn=file_dirname(file);
            if(dn) {
              noir_sess_set_cwd(ns,dn);
              free(dn); }
            file_activate(ns,file,mime,NULL); }
          ret=TRUE;
          break; }}
      g_free(file); }
    if(mime)
      g_free(mime); }
  if(tp)
    gtk_tree_path_free(tp);
  return ret; }



/**
 * callback handler for button events in the tree.
 * double-clicks should unfold a level.
 * @return TRUE for handled (allow no further handlers), FALSE otherwise
 * @param widget  the widget this event is for
 * @param ev      the GDK button-event
 * @param data    the session
 */
static gboolean tree_clicked(GtkWidget *widget,GdkEventButton *event,gpointer data) {
  int            ret=FALSE;
  noir_sess     *ns=(noir_sess *)data;
  noir_treeview *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);

  if(ns&&(tv->tree_view)) {
    gint dummy;
    GtkTreePath *path=NULL;
    if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tv->tree_view),event->x,event->y,&path,NULL,&dummy,&dummy)) {
      gchar *file;
      gint isdir;
      GtkTreeIter myiter;
      if(gtk_tree_model_get_iter(GTK_TREE_MODEL(tv->store),&myiter,path)) {
        gtk_tree_model_get(GTK_TREE_MODEL(tv->store),&myiter,FILE_FILE,&file,FILE_MODE,&isdir,-1);
        if(file) {
          if(!isdir)
            isdir=file_exists(file);
          ret=TRUE;
          switch(event->button) {
          case 1:
            if(event->type==GDK_2BUTTON_PRESS) {
              if(S_ISDIR(isdir)) {
                if(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tv->tree_view),path))
                  gtk_tree_view_collapse_row(GTK_TREE_VIEW(tv->tree_view),path);
                else {
                  if(event->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK))
                    noir_sess_create(SESSION_COPY,ns,file);
                  else {
                    gtk_tree_view_expand_row(GTK_TREE_VIEW(tv->tree_view),path,FALSE);
                 /* noir_sess_focus(ns,file); */
                  }}}
              else {
                char *dn=file_dirname(file);
                if(dn) {
                  noir_sess_set_cwd(ns,dn);
                  free(dn); }
                file_activate(ns,file,NULL,NULL); }}
            else
              ret=FALSE;
            break;
          case 2:
            if(event->type==GDK_BUTTON_RELEASE) {
              if(typebuffer_is_open(ns))
                clipboard_get(GDK_SELECTION_PRIMARY,ns);
              else
                tooltip_show_for(ns,file,(int)event->x_root,(int)event->y_root); }
            break;
          case 3:
            if(event->type==GDK_BUTTON_RELEASE) {
              if(event->state&GDK_CONTROL_MASK)
                menu_root_context(ns,noir_sess_get_cwd(ns),3);
              else {
                GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(tv->tree_view));
                if(sel)
                  tree_selection_changed(sel,ns);
                menu_file_context(ns,noir_sess_get_cwd(ns),file); }}
            break;
          default:
            ret=FALSE; }
          g_free(file); }}
      if(path)
        gtk_tree_path_free(path); }}
  return ret; }






/****************************************************************************/
/* Expand'N'Collapse */
/*********************/



/**
 * "close branch"-callback
 * @return FALSE (allow folding)
 * @param treeview  the tree-view
 * @param arg_iter  the iterator denoting the element to fold
 * @param arg_path  the treepath denoting the element to fold (or NULL)
 * @param data      the session
 */
static int tree_view_collapse(GtkTreeView *treeview,GtkTreeIter *arg_iter,GtkTreePath *arg_path,gpointer data) {
  noir_sess     *ns=(noir_sess *)data;
  noir_treeview *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);
  GValue         v;
  char          *fn;

  if(!ns)
    return FALSE;

  memset(&v,0,sizeof(GValue));

  gtk_tree_model_get_value(GTK_TREE_MODEL(tv->store),arg_iter,FILE_FILE,&v);
  fn=g_value_peek_pointer(&v);

  if(fn&&*fn)
    file_monitor_remove((noir_sess *)data,fn);

  g_value_unset(&v);

  return FALSE; }



/**
 * "open branch"-callback
 * we might have to JIT-load that directory's contents here. 
 * @return FALSE (allow folding) if level can be expanded, TRUE otherwise
 * @param treeview  the tree-view
 * @param arg_iter  the iterator denoting the element to fold
 * @param arg_path  the treepath denoting the element to fold (or NULL)
 * @param data      the session
 */
int tree_view_expand(GtkTreeView *treeview,GtkTreeIter *arg_iter,GtkTreePath *arg_path,gpointer data) {
  noir_sess     *ns=(noir_sess *)data;
  noir_treeview *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);
  char          *f1;
  int            ret=TRUE;

  gtk_tree_model_get(GTK_TREE_MODEL(tv->store),arg_iter,FILE_FILE,&f1,-1);

  if(f1) {
    file_info *fi=file_info_fetch(NULL,f1);
    if(fi) {
      if((fi->have&FF_FIRST)&&!fi->child)
        form_alert(ns,"Sorry, directory \"%s\" is empty.\n"
                      "There is nothing to see here.",fi->name);
      else {
/*        if(!(fi->have&FF_SEMA)) { */
        if(tv->sema_unfold<=0) {
          E(2,"loading %s...\n",f1);
          tv->realized=0;
          noir_sess_focus(ns,f1); }
        ret=FALSE; }}
    free(f1); }

  return ret; }



/****************************************************************************/



/**
 * find a given item in a given level of a certain tree-view
 * the level is indicated by the tree-view's parent-iter (tv->pi)
 * called from tree_view_find_in_dir() and tree_view_retag_tree()
 * @return a fidd_result (found, notfound, empty, ...)
 * @param fi  the file to find
 * @param tv  the tree-view
 */
static inline fidd_result tree_view_find_in_dir_do(file_info *fi,noir_treeview *tv) {
  fidd_result same=FIDD_NOTFOUND;
  if(gtk_tree_model_iter_children(GTK_TREE_MODEL(tv->store),tv->ci,tv->pi)) {
    gchar *tag;
    do {
      gtk_tree_model_get(GTK_TREE_MODEL(tv->store),tv->ci,FILE_FILE,&tag,-1);
      if(tag) {
        same=strcmp(tag,fi->tag)?FIDD_NOTFOUND:FIDD_FOUND;
/* E(0,"%s || %s\n",tag,fi->tag); */
        g_free(tag); }
      else
        same=FIDD_EMPTY;
    } while((same==FIDD_NOTFOUND)&&gtk_tree_model_iter_next(GTK_TREE_MODEL(tv->store),tv->ci)); }
  return same; }



/**
 * walk down to file fi->tag, iter-tag all dirs along the way.
 * needed when focusing on a newly-loaded dir.
 * @return an error-code
 * @param fi  deepest item in the path to tag
 * @param tv  the tree-view
 */
int tree_view_retag_tree(file_info *fi,noir_treeview *tv) {
  file_info   *tfi;
  char        *tp=strdup(fi->tag),*p,*e;
  int          ret=NOIR_NOTFOUND;
  GtkTreeIter *it;

  if(!tp)
    return NOIR_OOM;

  if((p=strrchr(tp,'/')))
    *p='\0';

  p=tp+1;

  if(!(tfi=file_info_fetch(NULL,"/"))) {
    E(0,"(\"%s\") => could not get root fi!?\n",fi->tag?fi->tag:"<no tag>");
    goto fail; }

  if(!(it=(GtkTreeIter *)dcd_get_data(tfi,tv->client))) {
    fidd_result rx;
    it=tv->pi;
    tv->pi=NULL;
    rx=tree_view_find_in_dir_do(tfi,tv);
    tv->pi=it;
    if(rx==FIDD_FOUND) {
      dcd_set(tfi,tv->client,(void *)(it=tv->ci));
      if(!*tp) {
        goto succeed; }}
    else {
      goto fail; }}

  memcpy(tv->pi,it,sizeof(GtkTreeIter));

  while(*p) {
    if((e=strchr(p,'/')))
      *e='\0';
    if(!(tfi=file_info_fetch(NULL,tp))) {
      goto fail; }
    if(tree_view_find_in_dir_do(tfi,tv)!=FIDD_FOUND) {
E(0,"could not locate iter for \"%s\" in tree...\n",tp);
      goto fail; }
    dcd_set(tfi,tv->client,(void *)tv->ci);
/* E(0,"success for \"%s\"...\n",tp); */
    if(e) {
      memcpy(tv->pi,tv->ci,sizeof(GtkTreeIter));
      *(e++)='/';
      p=e; }
    else
      p+=strlen(p); }

 succeed:
  ret=NOIR_SUCC;

 fail:
  free(tp);
  return ret; }



/**
 * find a given item in a given level of a certain tree-view
 * sets tree-view's parent-iter (tv->pi)
 * @return a fid_result (found, notfound, created, ...)
 * @param fi      the file to find
 * @param tv      the tree-view
 * @param create  create item if it doesn't already exist
 */
static fid_result tree_view_find_in_dir(file_info *fi,noir_treeview *tv,int create) {
  fidd_result  same=FIDD_NOTFOUND;
  int          root;
  file_info   *pa=fi->parent;

  if(!pa)
    return FID_NOTFOUND;

  if((root=!strcmp(fi->tag,"/")))
    pa=fi;

  { GtkTreeIter *ti=(GtkTreeIter *)dcd_get_data(pa,tv->client);
    if(!ti) {
      tree_view_retag_tree(fi,tv);
      ti=(GtkTreeIter *)dcd_get_data(pa,tv->client); }
    if(!ti) {
      E(0,"(\"%s\"): could not get iter!??\n",fi->tag);
      return FID_NOTFOUND; }
    else {
      memcpy(tv->pi,ti,sizeof(GtkTreeIter)); }}

  if(((same=tree_view_find_in_dir_do(fi,tv))!=FIDD_FOUND)&&create) {
    if(!root) {
      if(same==FIDD_EMPTY) {
        gtk_tree_model_iter_children(GTK_TREE_MODEL(tv->store),tv->ci,tv->pi);
	gtk_tree_store_set(GTK_TREE_STORE(tv->store),tv->ci,FILE_NAME,fi->name,FILE_FILE,fi->tag,-1); }
      else {
        gtk_tree_store_append(GTK_TREE_STORE(tv->store),tv->ci,tv->pi);
	gtk_tree_store_set(GTK_TREE_STORE(tv->store),tv->ci,FILE_NAME,fi->name,FILE_FILE,fi->tag,-1);

        if(gtk_widget_get_realized(tv->tree_view)&&tv->realized&&
           tv->ci&&tv->ci->user_data) {
          GtkTreePath *tp=gtk_tree_model_get_path(GTK_TREE_MODEL(tv->store),tv->ci);

          { char *n,*t;
            gtk_tree_model_get(GTK_TREE_MODEL(tv->store),tv->pi,FILE_NAME,&n,FILE_FILE,&t,-1);
            E(0,"Adding %s (%s) to %s (%s)  => %p\n",fi->name,fi->tag,n,t,tp);
            free(n); }

          if(tp) {
E(0,"INSERTED\n\n");
            gtk_tree_model_row_inserted(GTK_TREE_MODEL(tv->store),tp,tv->ci);
            gtk_tree_path_free(tp); }}}}

    return FID_CREATED; }
/* E(0,"TV: %s \"%s\"...\n",same?"found":"couldn't find",fi->tag); */
  return same?FID_FOUND:FID_NOTFOUND; }



/**
 * find 'file' in tree-store, set parent-iter and child-iter (tv->pi/tv->ci)
 * called from tree_view_set_selection()
 * @return a fid_result
 * @param ns    the session
 * @param file  the tag of the file to look for
 */
static fid_result tree_get_iter(noir_sess *ns,const char *file) {
  file_info      fs;
  noir_treeview *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);
  memset(&fs,0,sizeof(file_info));
  fs.tag=(char *)file;
  return (tree_view_find_in_dir(&fs,tv,FALSE)==FID_NOTFOUND)?NOIR_FAIL:NOIR_SUCC; }



/**
 * stick a faux item inside a not-yet-loaded directory
 * so we'll get an "unfold"-button. ;-(  when said button is clicked, we'll
 * load the directory's contents.
 * @param tv  the tree-view
 */
static void tree_view_add_faux(noir_treeview *tv) {
  if(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tv->store),
                                    (!tv->ci||!tv->ci->user_data)?NULL:tv->ci)<1) {
    GtkTreeIter xx;
    gtk_tree_store_append(tv->store,&xx,tv->ci);
    gtk_tree_store_set(tv->store,&xx,FILE_NAME,DIR_EMPTY_MARKER,-1); }}



/**
 * automatically unfold a given directory in tree-view
 * @param tv    the tree-view
 * @param dir   the directory to unfold
 * @param sema  FALSE: last item in path, TRUE otherwise (reserved)
 */
static inline void unfold(noir_treeview *tv,char *dir,int sema) {
  file_info   *fi=file_info_fetch(NULL,dir);
  GtkTreePath *tp=NULL;
  fid_result   found;

  if(!fi)
    return;

/*  fi->have|=FF_SEMA; */
  tv->sema_unfold++;

  found=tree_view_find_in_dir(fi,tv,FALSE);

  if(found==FID_NOTFOUND) {
    tp=gtk_tree_path_new_first(); }
  else {
    tp=gtk_tree_model_get_path(GTK_TREE_MODEL(tv->store),tv->ci); }
  if(tp) {
    if(!gtk_tree_view_row_expanded(GTK_TREE_VIEW(tv->tree_view),tp)) {
      E(2,"FAUX    UNFOLD  %s   %p: %ld\n",fi->tag,tv->ci,tv->ci?*((long *)(tv->ci)):0);
      tree_view_add_faux(tv);
      gtk_tree_view_expand_row(GTK_TREE_VIEW(tv->tree_view),tp,FALSE); }
    gtk_tree_path_free(tp); }
/*  fi->have&=~FF_SEMA; */
  tv->sema_unfold--; }



/**
 * replay tree-view from model rather than the fs.  if we're in the
 * middle of reading the directory in question from the file-system,
 * we'll click in what we already have in the store, then let the
 * events from the backend do the rest.  there is room for
 * race-conditions here since all backend-events from now on will add
 * files to the column, and this routine might catch up and try to add
 * them to the column again.  this has been accounted for.
 * called from tree_view_evt().
 * @return an error-code
 * @param tv  the tree-view
 * @param fp  the directory to process
 */
static inline int tree_view_replay_maybe(noir_treeview *tv,file_info *fp) {
  tree_view_evt(fp,MODEL_UPD,fp->have,tv);
/* if(fp->have&FF_FIRST) { */
  if(fp->child) {
    /* if(FF_FIRST && !FF_LAST), we're in the middle of reading that
       dir.  we'll click in what we already have in the store, then
       let the events from the backend do the rest.  there is room
       for race-conditions here since all backend-events from now on
       will add files to column, and this routine might catch up and
       try to add them to the column again.  this has been accounted
       for. */
    if(!(fp->have&FF_LAST))
      E(1,"replaying %s while still loading it...\n",fp->tag);
    else
      E(1,"replaying %s...\n",fp->tag);

    fp=fp->child;
    while(fp) {
      tree_view_evt(fp,MODEL_UPD,fp->have,tv);
      fp=fp->next; }
    E(1,"replay done.\n");
    return NOIR_SUCC; }
  else {
    E(2,"nothing to replay in %s...  (%s)\n",fp->tag,fp->child?fp->child->tag:"");
    if((fp=fp->child))
      tree_view_evt(fp,MODEL_UPD,fp->have,tv); }
  return NOIR_IGNORED; }



/**
 * callback for file-model events.
 * @return an error-code
 * @param fi       the file the event refers to
 * @param type     the type of the event
 * @param changed  what changed
 * @param data     the tree-view
 */
static int tree_view_evt(file_info *fi,model_event type,file_flags changed,void *data) {
  noir_treeview *tv=(noir_treeview *)data;
  fid_result     found;
  GtkTreePath   *tp=NULL;

  switch(type) {
    case MODEL_CWD:
      if(changed==FF_NONE) {   /* pre */
      }
      else {                   /* post */
        file_info *f2=file_info_fetch(NULL,"/");
        char *pc=strdup(fi->tag),*b,*e;

        if(!f2)
          return NOIR_NODATA;

        tree_view_replay_maybe(tv,f2);

        if(!pc)
          return NOIR_OOM;

        unfold(tv,"/",TRUE);
        b=pc+1;

        do {
          if((e=strchr(b,'/')))
            *e='\0';

          if((f2=file_info_fetch(NULL,pc))) {
            tree_view_replay_maybe(tv,f2);
            found=tree_view_find_in_dir(f2,tv,TRUE);
            memcpy(tv->pi,tv->ci,sizeof(GtkTreeIter)); }
          else
            E(0,"no %s\n",pc);

          unfold(tv,pc,(e!=NULL));
          if(e)
            *(e++)='/';
          b=e;
        } while(b);
        free(pc); }

      break;

    case MODEL_UPD:
/* E(0,"UPD %s\n",fi->tag); */
      found=tree_view_find_in_dir(fi,tv,TRUE);
      if(changed&FF_STAT) {
        gtk_tree_store_set(tv->store,tv->ci,FILE_MODE,fi->intrinsic->mode,
                                            FILE_STAT,fi->intrinsic,
                                            FILE_INFO,fi,-1);
        if(fi->intrinsic&&S_ISDIR(fi->intrinsic->mode)) {
          tree_view_add_faux(tv);
          dcd_set(fi,tv->client,tv->ci); }} /* dir accelerator */

      if((changed&FF_MIME)&&(fi->mime))
        gtk_tree_store_set(tv->store,tv->ci,FILE_MIME,fi->mime,-1);

      /* AFTER */
      break;

    case MODEL_DEL:
      if((found=tree_view_find_in_dir(fi,tv,FALSE))==FID_FOUND) {
        if((tp=gtk_tree_model_get_path(GTK_TREE_MODEL(tv->store),tv->ci))) {
/*        tv->realized=1; */
          gtk_tree_store_remove(GTK_TREE_STORE(tv->store),tv->ci); }}
      break;

    case MODEL_MSC:
      tree_view_set_selection(tv->session);
      break;

    default:
      break; }

  if(tp)
    gtk_tree_path_free(tp);

  return NOIR_SUCC; }



/**
 * callback: destruct directory client-data
 * @return an error-code
 * @param data  pointer to the data
 */
static int tree_view_dst_dcd(void **data) {
  if(data&&*data) {
//  gtk_tree_iter_free((GtkTreeIter *)*data);
    *data=NULL;
    return NOIR_SUCC; }
  return NOIR_IGNORED; }



/**
 * callback: set directory client-data
 * @return an error-code
 * @param data  pointer to the data
 */
static void *tree_view_set_dcd(void *data) {
  return (void *)gtk_tree_iter_copy((GtkTreeIter *)data); }



/**
 * callback: destruct administrative data of a tree-view
 * @return an error-code
 * @param data  the tree-view
 */
static int tree_view_dst(void **data) {
  noir_treeview *tv=*((noir_treeview **)data);

  *data=NULL;

  if(tv->tree_path)
    gtk_tree_path_free(tv->tree_path);

  if(tv->pi)  free(tv->pi);
  if(tv->ci)  free(tv->ci);
  if(tv->di)  free(tv->di);

  memset(tv,0,sizeof(noir_treeview));
  free(tv);

  return NOIR_SUCC; }



/**
 * initialize a session's tree-view.
 * @param ns  the session
 */
int tree_view_init(noir_sess *ns) {
   GtkTreeViewColumn *column;
   GtkCellRenderer   *renderer;
   GtkTreeStore      *store=NULL;
   GtkWidget         *tree,*scroll;
   GtkAdjustment     *ha,*va;
   GtkTreeSelection  *selection;
   noir_treeview     *tv;

   if(!ns)
     return NOIR_FAIL;

   tv=(noir_treeview *)malloc(sizeof(noir_treeview));
   if(!tv)
     return NOIR_OOM;
   memset(tv,0,sizeof(noir_treeview));

   if((tv->pi=(GtkTreeIter *)malloc(sizeof(GtkTreeIter)))) {
     if((tv->ci=(GtkTreeIter *)malloc(sizeof(GtkTreeIter)))) {
       if((tv->di=(GtkTreeIter *)malloc(sizeof(GtkTreeIter)))) {
         tv->session=ns;
         tv->store=store=tree_model_setup(tv);
         tv->client=noir_sess_client_register_full(ns,CLIENT_TREEVIEW,(void *)tv,tree_view_evt,tree_view_dst,tree_view_set_selection);
         if(tv->client) {
           tv->client->dcd_set_data=tree_view_set_dcd;
           tv->client->dcd_dst_data=tree_view_dst_dcd; }
         else
           goto free_ci; }
       else
         goto free_ci; }
     else
       goto free_pi; }
   else
     goto free_tv;

#define TREE_MAX_FILES 16000
   ha=(GtkAdjustment*)gtk_adjustment_new(0,0,N_COLUMNS-1,1,3,3);
   va=(GtkAdjustment*)gtk_adjustment_new(0,0,TREE_MAX_FILES,1,10,10);

   tv->tree_scroll=scroll=gtk_scrolled_window_new(ha,va);

   /* Create a view */

   tv->tree_view=tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

   renderer=gtk_cell_renderer_text_new();
   g_object_set(G_OBJECT(renderer),"foreground","red",NULL);
   column=gtk_tree_view_column_new_with_attributes("File",renderer,
                                                   "text",FILE_NAME,
                                                   NULL);
   gtk_tree_view_column_set_resizable(column,TRUE);
   gtk_tree_view_column_set_reorderable(column,FALSE);
   gtk_tree_view_column_set_alignment(column,0.5);
   gtk_tree_view_column_set_sort_indicator(column,TRUE);
   gtk_tree_view_column_set_sort_order(column,GTK_SORT_ASCENDING);

   gtk_tree_view_column_set_sort_column_id(column,SORT_BY_NAME);

   gtk_tree_view_append_column(GTK_TREE_VIEW(tree),column);



/*****************************************************************************/



   renderer=gtk_cell_renderer_text_new();
   column=gtk_tree_view_column_new_with_attributes("MIME",renderer,
                                                   "text",FILE_MIME,
                                                   NULL);
   gtk_tree_view_column_set_resizable(column,TRUE);
   gtk_tree_view_column_set_reorderable(column,TRUE);
   gtk_tree_view_column_set_alignment(column,0.5);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree),column);



   renderer=noir_cell_renderer_size_new();
   column=gtk_tree_view_column_new_with_attributes("Size",renderer,
                                                   "stat",FILE_STAT,
                                                   NULL);
   gtk_tree_view_column_set_resizable(column,TRUE);
   gtk_tree_view_column_set_reorderable(column,TRUE);
   gtk_tree_view_column_set_alignment(column,0.5);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree),column);



   renderer=noir_cell_renderer_date_new();
   column=gtk_tree_view_column_new_with_attributes("Modified",renderer,
                                                   "stat",FILE_STAT,
                                                   NULL);
   gtk_tree_view_column_set_resizable(column,TRUE);
   gtk_tree_view_column_set_reorderable(column,TRUE);
   gtk_tree_view_column_set_alignment(column,0.5);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree),column);



/*****************************************************************************/



   gtk_tree_view_set_hadjustment(GTK_TREE_VIEW(tree),ha);
   gtk_tree_view_set_vadjustment(GTK_TREE_VIEW(tree),va);

   gtk_tree_view_set_search_column(GTK_TREE_VIEW(tree),0);
   gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree),TRUE);

   gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tree),TRUE);

   g_signal_connect(G_OBJECT(tree),"test-expand-row",G_CALLBACK(tree_view_expand),ns);
   g_signal_connect(G_OBJECT(tree),"test-collapse-row",G_CALLBACK(tree_view_collapse),ns);

   gtk_container_add(GTK_CONTAINER(scroll),tree);

   selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
   gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);

   g_signal_connect(G_OBJECT(tree),"button_press_event",    G_CALLBACK(tree_clicked),(gpointer)ns);
   g_signal_connect(G_OBJECT(tree),"button_release_event",  G_CALLBACK(tree_clicked),(gpointer)ns);
   g_signal_connect(G_OBJECT(tree),"key-press-event",       G_CALLBACK(tree_keyed),(gpointer)ns);

   gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(tree),1,target,targets,GDK_ACTION_LINK|GDK_ACTION_COPY|GDK_ACTION_MOVE);
   gtk_tree_view_enable_model_drag_dest(GTK_TREE_VIEW(tree),target,targets,GDK_ACTION_LINK|GDK_ACTION_COPY|GDK_ACTION_MOVE);

   g_signal_connect(G_OBJECT(tree),"drag_data_get",         G_CALLBACK(tree_drag_data_get),(gpointer)ns);
   g_signal_connect(G_OBJECT(tree),"drag_data_received",    G_CALLBACK(tree_drag_data_received),(gpointer)ns);

   { GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
     g_signal_connect(G_OBJECT(sel),"changed",               G_CALLBACK(tree_selection_changed),(gpointer)ns); }

   gtk_widget_set_name(tree,"tree_view");
   gtk_widget_set_name(scroll,"tree_scroll");

// g_object_unref(G_OBJECT(store));

   return NOIR_SUCC;

  free_ci:
   free(tv->ci);
  free_pi:
   free(tv->pi);
  free_tv:
   free(tv);
   return NOIR_OOM; }



/**
 * deinitialize a session's tree-view
 * @param ns  the session
 */
int tree_view_exit(noir_sess *ns) {
/* client_unregister(noir_sess_client_get(ns,CLIENT_TREEVIEW)); */
  return NOIR_SUCC; }
#endif


/****************************************************************************/
