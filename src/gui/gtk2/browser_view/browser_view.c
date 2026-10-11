/** @file  browser_view.c
 *  @brief handle the GTK browser-view.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>       /* malloc(), free() */
#include <sys/stat.h>     /* S_ISDIR */

#include "src/gui/common/session.h"

#ifdef WITH_BROWSERVIEW
#  include "file.h"         /* file_get_from_drop(), file_monitor_remove_all(), file_activate() */
#  include "file_lcl.h"     /* file_to_URL() */
#  include "monitors.h"     /* file_monitor_remove_all() */

#  include "gui/common/menu.h"         /* menu_root_context(), menu_file_context() */
#  include "gui/common/typebuffer.h"   /* typebuffer_tab() */

#  include "gui/gtk2/common/keysyms.h"
#  include "gui/gtk2/common/clipboard.h"    /* selection_update(), clipboard_get() */
#  include "gui/common/tooltip.h"           /* tooltip_show_for() */
#  include "gui/gtk2/common/icon.h"         /* icon_get_by_file() */
#  include "gui/gtk2/common/dnd.h"          /* file_drop() */
#  include "gui/gtk2/common/session_gtk2.h" /* noir_sess_get_notebook() */

#  include "gui/gtk2/common/tree/tree.h"    /* FILE_FILE, FILE_MODE, SORT_BY_NAME */
#  include "gui/gtk2/common/tree/cr_arrow.h"     /* noir_cell_renderer_arrow_new() */

#  include "browser_view.h" /* noir_column */
#  include "mime.h"         /* MIME_INTL_INF */

#  include "model_private.h"  /* ### FIXME */


static int browser_view_evt(file_info *,model_event,file_flags,void *);



/****************************************************************************/



/**
 * "selection-changed" callback for browser-view.
 * @param sel   the new selection for the column
 * @param data  the column the selection of which has changed
 */
static void browser_selection_changed(GtkTreeSelection *sel,gpointer data) {
  noir_column      *nc=(noir_column *)data;
  noir_sess        *ns=nc->session;
  GList            *list;
  noir_browser     *bv=(noir_browser *)noir_sess_client_get_data(ns,CLIENT_BROWSERVIEW);
  action_context   *ac=NULL;

  if(gtk_notebook_page_num(GTK_NOTEBOOK(noir_sess_get_notebook(ns)),bv->browser_scroll)!=gtk_notebook_get_current_page(GTK_NOTEBOOK(noir_sess_get_notebook(ns))))
    return;

  /* a click in any but the last column *clears* the selection! */
  if(!nc->next&&(list=gtk_tree_selection_get_selected_rows(sel,(GtkTreeModel **)&(nc->store)))) {
    int             n=g_list_length(list),m;
    GtkTreePath    *tp;
    GtkTreeIter     it;
    gchar          *file;

    if(!(ac=action_context_new(noir_sess_get_serial(ns),noir_sess_get_cwd(ns))))
      return;

    for(m=0;m<n;m++) {
      tp=(GtkTreePath *)g_list_nth_data(list,m);
      if(tp) {
        if(gtk_tree_model_get_iter(GTK_TREE_MODEL(nc->store),&it,tp)) {
          gtk_tree_model_get(GTK_TREE_MODEL(nc->store),&it,FILE_FILE,&file,-1);
          if(file) {
            action_file_new(ac,file);
            g_free(file); }}
        gtk_tree_path_free(tp); }}

    g_list_free(list); }

  /* update selection in session */
  noir_sess_set_selection(ns,ac);

  /* propagate new selection to other views in session */
  selection_update(ns,noir_sess_client_get(ns,CLIENT_BROWSERVIEW)); }



/**
 * set selection in browser-view.
 * @return an error-code
 * @param data  session
 */
int browser_view_set_selection(void *data) {
  noir_sess        *ns=(noir_sess *)data;
  noir_browser     *bv=(noir_browser *)noir_sess_client_get_data(ns,CLIENT_BROWSERVIEW);
  GtkTreeSelection *sel;
  noir_column      *nc=bv->columns;

  if(!nc) {
    E(2,"no browser.\n");
    return NOIR_FAIL; }

  /* go to rightmost column */
  while(nc->next)
    nc=nc->next;

  if((sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(nc->view)))) {
    action_file *af;

    gtk_tree_selection_unselect_all(sel);

    if((af=action_context_get_selection(noir_sess_get_selection(ns)))) {
      GtkTreeIter  it;
      char        *file;
      int          done;

      while(af) {
        if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(nc->store),&it)) {
          done=FALSE;
          do {
            gtk_tree_model_get(GTK_TREE_MODEL(nc->store),&it,FILE_FILE,&file,-1);
            if(file) {
              if(!strcmp(file,action_file_get_tag(af))) {
                gtk_tree_selection_select_iter(sel,&it);
                done=TRUE; }
              g_free(file); }
          } while(!done&&gtk_tree_model_iter_next(GTK_TREE_MODEL(nc->store),&it)); }
        af=action_file_get_next(af); }

      return NOIR_SUCC; }

    else
      E(2,"no selection to set...\n"); }

  else
    E(0,"no selection in list!?\n");

  return NOIR_FAIL; }



/****************************************************************************/



/** DND: drop source sent data. act upon them.
 * @param widget     the widget
 * @param dc         the drag-context
 * @param x          where the data was dropped
 * @param y          where the data was dropped
 * @param selection  the received data
 * @param info       type of the data as per GtkTargetList
 * @param timestamp  when the event happened
 * @param data       column
 */
static void icon_path_drag_data_received(GtkWidget *widget,GdkDragContext *dc,gint x,gint y,GtkSelectionData *selection,guint info,guint timestamp,gpointer data) {
  noir_column *nc=(noir_column *)data;
  noir_sess   *ns;
  gchar       *d,*a;
  char        *tgt;
  char        *file;

  if(!nc||!widget||!(ns=nc->session))
    return;

  tgt=(char *)g_object_get_data(G_OBJECT(widget),"user_data");

  if(!selection||(selection->length<1)) {
    E(0,"icon_path_drag_data_received: no data (%p,%d)\n",selection,selection?selection->length:-1234); }
  else if((selection->length>=0)&&(selection->format==8)) {
    char *n;

    if(!(n=strdup(noir_sess_get_cwd(ns))))
      return;

    if(!(a=d=malloc(selection->length+1))) {
      free(n);
      return; }

    strncpy(a,selection->data,selection->length);
    a[selection->length]='\0';

    E(0,"browser_drag_data_received \"%s\" for \"%s\"\n",a,tgt);

    while((file=dnd_get_file_from_drop(&d))) {
      if(dc->action==GDK_ACTION_MOVE) {
        size_t l=strlen(file);
        if(!strcmp(n,file)||(!strncmp(n,file,l)&&(n[l]==DIR_DELIMITER))) {
          char *p;
          do {
            n[l--]='\0';
          } while(n[l]==DIR_DELIMITER);
          if((p=strrchr(n,DIR_DELIMITER)))
            *p='\0'; }}
      file_drop(ns,dc->action,info,file,tgt); }

    noir_sess_redraw_window(ns);
    free(n);
    free(a); }}



/**
 * DND: drop target asks for data. collect and send.
 * @param widget         the widget that was dragged from, or NULL
 * @param dc             the drag-context
 * @param selectionData  the selection to set our data on
 * @param info           type of the data as per GtkTargetList
 * @param timestamp      when the event happened
 * @param data           column
 */
static void icon_path_drag_data_get(GtkWidget *widget,GdkDragContext *dc,GtkSelectionData *selectionData,guint info,guint timestamp,gpointer data) {
  noir_column *nc=(noir_column *)data;
  noir_sess *ns;

  if(!nc||!(ns=nc->session))
    return;

  if(selectionData) {
    gchar *reply=NULL;

    if((info==DRAGDROP_TARGET_STRING)||(info==DRAGDROP_TARGET_TEXT))
      reply=g_strdup((gchar *)g_object_get_data(G_OBJECT(widget),"user_data"));
    else if((info==DRAGDROP_TARGET_NETSCAPE_URL)||(info==DRAGDROP_TARGET_URL))
      reply=file_to_URL((gchar *)g_object_get_data(G_OBJECT(widget),"user_data"));

    if(reply) {
      gtk_selection_data_set(selectionData,selectionData->target,8,reply,strlen(reply));
      g_free(reply); }}}



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
 * @param data       column
 */
static void browser_drag_data_received(GtkWidget *widget,GdkDragContext *dc,gint x,gint y,GtkSelectionData *selection,guint info,guint timestamp,gpointer data) {
  noir_column *nc=(noir_column *)data;
  noir_sess   *ns;
  GtkTreePath *tp;
  GtkTreeIter it;
  gint mode;
  GtkTreeViewDropPosition type;
  gchar *d,*a;
  gchar *tgt=NULL,*file;

  if(!nc||!(ns=nc->session))
    return;

  if(gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget),x,y,&tp,&type)) {
    if(gtk_tree_model_get_iter(GTK_TREE_MODEL(nc->store),&it,tp)) {
      gtk_tree_model_get(GTK_TREE_MODEL(nc->store),&it,FILE_FILE,&tgt,FILE_MODE,&mode,-1);
      if((type==GTK_TREE_VIEW_DROP_BEFORE)||(type==GTK_TREE_VIEW_DROP_AFTER)||!S_ISDIR(mode)) {
        if(tgt)
          g_free(tgt);
        tgt=g_strdup(nc->path); }}
    gtk_tree_path_free(tp); }

  if(tgt) {
    if(!selection||(selection->length<1)) {
      E(0,"browser_drag_data_received: no data (%p,%d)\n",selection,selection?selection->length:-1234); }
    else if((selection->length>=0)&&(selection->format==8)) {
      char *n;

      if((n=strdup(noir_sess_get_cwd(ns)))&&(a=d=g_strdup(selection->data))) {
        E(0,"browser_drag_data_received \"%s\" for \"%s\"\n",a,tgt);

        while((file=dnd_get_file_from_drop(&d))) {
          if(dc->action==GDK_ACTION_MOVE) {
            size_t l=strlen(file);
            if(!strcmp(n,file)||(!strncmp(n,file,l)&&(n[l]==DIR_DELIMITER))) {
              char *p;
              do {
                n[l--]='\0';
              } while(n[l]==DIR_DELIMITER);
              if((p=strrchr(n,DIR_DELIMITER)))
                *p='\0'; }}
          file_drop(ns,dc->action,info,file,tgt); }

        noir_sess_redraw_window(ns);
        free(n);
        if(a) free(a); }}

    g_free(tgt); }}



/**
 * DND: drop target asks for data. collect and send.
 * @param widget         the widget that was dragged from, or NULL
 * @param dc             the drag-context
 * @param selectionData  the received data
 * @param info           type of the data as per GtkTargetList
 * @param timestamp      when the event happened
 * @param data           column
 */
static void browser_drag_data_get(GtkWidget *widget,GdkDragContext *dc,GtkSelectionData *selectionData,guint info,guint timestamp,gpointer data) {
  noir_column *nc=data;
  noir_sess *ns;
  GtkTreeSelection *selection=NULL;

  if(!nc||!(ns=nc->session))
    return;

  if(selectionData&&(selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(nc->view)))) {
    gchar *reply=NULL;

    if((info==DRAGDROP_TARGET_STRING)||(info==DRAGDROP_TARGET_TEXT)) {
      gtk_tree_selection_selected_foreach(selection,tree_send_string,&reply); }
    else if((info==DRAGDROP_TARGET_NETSCAPE_URL)||(info==DRAGDROP_TARGET_URL)) {
      gtk_tree_selection_selected_foreach(selection,tree_send_URL,&reply); }
    if(reply) {
      gtk_selection_data_set(selectionData,selectionData->target,8,reply,strlen(reply));
      g_free(reply); }}}



/***************************************************************************************/



/**
 * callback for button-click events on the "blinds" (neither
 * file-list nor column folder).  open a menu or, if middle button is clicked
 * while type-buffer is open, past selection.
 * @return TRUE for handled (allow no further handlers), FALSE otherwise
 * @param widget  the column (GTK-treeview)
 * @param event   the actual event
 * @param data    the column (administrative struct)
 */
static gboolean blind_clicked(GtkWidget *widget,GdkEventButton *event,gpointer data) {
  noir_sess *ns=(noir_sess *)data;

  if((event->type!=GDK_BUTTON_RELEASE)&&(event->type!=GDK_BUTTON_PRESS))
    return FALSE;

  if(event->type!=GDK_BUTTON_RELEASE) {
    if(event->button==2) {
      if(typebuffer_is_open(ns))
        clipboard_get(GDK_SELECTION_PRIMARY,ns);
      else
        menu_path(ns,2,event->time); }
    else
      menu_root_context(ns,noir_sess_get_cwd(ns),event->button); }
  return TRUE; }



/**
 * callback for button-click events on the folder of a
 * column.  double-clicks should focus on that folder.
 * @return TRUE for handled (allow no further handlers), FALSE otherwise
 * @param view   the column (GTK-treeview)
 * @param event  the actual event
 * @param data   the column (administrative struct)
 */
static gboolean icon_path_clicked(GtkWidget *view,GdkEventButton *event,gpointer data) {
  int ret=FALSE;
  noir_column *nc=(noir_column *)data;

  if(view&&nc&&nc->session) {
    noir_sess    *ns=nc->session; /* we need to remeber the session as the column might go away when focus changes!! */
    char         *file=strdup(nc->path);
    file_info    *fi=file_info_fetch(NULL,file);

    ret=TRUE;
    switch(event->button) {
    case 3:
      if(event->type==GDK_BUTTON_RELEASE) {
        if(event->state&GDK_CONTROL_MASK)
          menu_root_context(ns,nc->path,3);
        else if(fi&&((fi=fi->parent)))
          menu_file_context(ns,fi->tag,file); }
      break;
    case 2:
      if(event->type==GDK_BUTTON_RELEASE) {
        if(typebuffer_is_open(ns))
          clipboard_get(GDK_SELECTION_PRIMARY,ns);
        else
          tooltip_show_for(ns,file,(int)event->x_root,(int)event->y_root); }
      break;
    case 1:
      if(event->type==GDK_2BUTTON_PRESS) { /* double click */
        if(event->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK)) {
        noir_sess_create(SESSION_COPY,ns,file); }
        else {
          file_monitor_remove_all(ns);
          noir_sess_focus(nc->session,file); }}
      break; }
    free(file); }
  return ret; }



/**
 * callback for button-click events in a column.
 * double-clicks should unfold a level.
 * @return TRUE for handled (allow no further handlers), FALSE otherwise
 * @param view   the column (GTK-treeview)
 * @param event  the actual event
 * @param data   the column (administrative struct)
 */
static gboolean column_clicked(GtkWidget *view,GdkEventButton *event,gpointer data) {
  int          ret=FALSE;
  noir_column *nc=(noir_column *)data;

  if(view&&nc&&nc->session) {
    gint          dummy;
    GtkTreePath  *path =NULL;
    GtkListStore *store=nc->store;
    noir_sess    *ns=nc->session; /* we need to remeber the session as the column might go away when focus changes!! */

    if(view!=nc->view)
      E(0,"column_clicked: view is %p where we expected %p!?\n",view,nc->view);

    if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),event->x,event->y,&path,NULL,&dummy,&dummy)) {
      gchar *file;
      gint isdir;
      GtkTreeIter myiter;

      if(gtk_tree_model_get_iter(GTK_TREE_MODEL(store),&myiter,path)) {
        gtk_tree_model_get(GTK_TREE_MODEL(store),&myiter,FILE_FILE,&file,FILE_MODE,&isdir,-1);
        if(file) {
          ret=TRUE;
          switch(event->button) {
          case 3:
            if(event->type==GDK_BUTTON_RELEASE) {
              if(event->state&GDK_CONTROL_MASK)
                menu_root_context(ns,nc->path,3);
              else {
                GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(nc->view));
                if(sel)
                  browser_selection_changed(sel,nc);
                menu_file_context(ns,nc->path,file); }}
            break;
          case 2:
            if(event->type==GDK_BUTTON_RELEASE) {
              if(typebuffer_is_open(ns))
                clipboard_get(GDK_SELECTION_PRIMARY,ns);
              else
                tooltip_show_for(ns,file,(int)event->x_root,(int)event->y_root); }
            break;
          case 1:
            if(event->type==GDK_2BUTTON_PRESS) { /* double click */
              if(S_ISDIR(isdir)) {
                if(event->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK))
                  noir_sess_create(SESSION_COPY,ns,file);
                else {
                  file_monitor_remove_all(ns);
                  noir_sess_focus(nc->session,file); }}
              else {
                file_activate(ns,file,NULL,NULL);
                ret=FALSE; }}
            else {                               /* single click */
              ret=FALSE;
              /* single click on dir opens it *IF* no modifier keys are down,
                 otherwise we assume the user is trying to add to a selection */
              if((S_ISDIR(isdir)&&!(event->state&(GDK_SHIFT_MASK|GDK_CONTROL_MASK)))&&(event->type==GDK_BUTTON_PRESS)) {
                ret=TRUE;
                noir_sess_focus(nc->session,file); }}
            break;
          default:
            ret=FALSE; }
          g_free(file); }}

      if(path)
        gtk_tree_path_free(path); }

    else if(event->type==GDK_BUTTON_RELEASE) {
      if(event->button==3) {
        menu_root_context(ns,nc->path,3);
        ret=TRUE; }
      else if(event->button==2) {
        if(typebuffer_is_open(ns))
          clipboard_get(GDK_SELECTION_PRIMARY,ns);
        else
          menu_path(ns,2,event->time); }}}
  else
    E(0,"called with insufficient parameters: view %p\t nc: %p\t nc->session: %p\n",view,nc,nc?nc->session:NULL);
  return ret; }



/**
 * callback for key-events
 * @return TRUE for handled (allow no further handlers), FALSE otherwise
 * @param widget  the widget that received the event
 * @param ev      the actual event
 * @param data    the column the widget is in
 */
static gboolean column_keyed(GtkWidget *widget,GdkEventKey *ev,gpointer data) {
  noir_column       *nc=(noir_column *)data;
  noir_sess         *ns;
  GtkTreePath       *tp;
  GtkTreeViewColumn *fc;
  GtkTreeIter        myiter;
  gchar             *file=NULL;
  gint               isdir;
  int                ret=FALSE;
  noir_browser      *bv;

  if(!nc)
    return ret;

  ns=nc->session;
  bv=(noir_browser *)noir_sess_client_get_data(ns,CLIENT_BROWSERVIEW);

  gtk_tree_view_get_cursor(GTK_TREE_VIEW(widget),&tp,&fc);
  if(!tp)  /* no cursor yet? */
    tp=gtk_tree_path_new_first();

  if(gtk_tree_model_get_iter(GTK_TREE_MODEL(nc->store),&myiter,tp)) {
    gtk_tree_model_get(GTK_TREE_MODEL(nc->store),&myiter,FILE_FILE,&file,FILE_MODE,&isdir,-1);
    if(file) {
#if 0
      size_t  l=strlen(nc->path)+3+strlen(file);
      char   *x=malloc(l);
      if(x) {
        snprintf(x,l,"%s%s%s",nc->path,strcmp(nc->path,DIR_DELIM_STR)?DIR_DELIM_STR:"",file);
        g_free(file);
        file=x; }
#endif

      switch(ev->keyval) {
        case GDK_F2:
        case GDK_r:
          if((ev->keyval==GDK_F2)||(ev->state&GDK_CONTROL_MASK)) {
            if((ret=menu_action_by_mime(noir_sess_get_serial(ns),MIME_INTL_REN,file,noir_sess_get_selection(ns)))!=NOIR_SUCC)
              E(0,"could not open rename dialog (type \"" MIME_INTL_REN "\"): %d\n",ret);
            ret=TRUE; }
          break;
      case GDK_i:
        if(ev->state&GDK_CONTROL_MASK) {
          gint x,y;
          gdk_window_get_pointer(gtk_widget_get_window(widget),&x,&y,NULL);
          tooltip_show_for(ns,file,x,y);
          ret=TRUE; }
        break;
      case GDK_Menu:
      case GDK_m:
        if((ev->keyval==GDK_Menu)||(ev->state&GDK_CONTROL_MASK)) {
          menu_file_context(ns,nc->path,file);
          ret=TRUE; }
        break;

      case GDK_Tab:
        if(typebuffer_is_open(ns)) {
          ret=TRUE;
          typebuffer_tab(ns); }
        break;
      case GDK_p: {   /* ^P - properties dialog */
        if((ret=menu_action_by_mime(noir_sess_get_serial(ns),MIME_INTL_INF,file,noir_sess_get_selection(ns)))!=NOIR_SUCC)
          E(0,"could not open properties dialog (type \"" MIME_INTL_INF "\"): %d\n",ret); }
        ret=TRUE;
        break;
      case GDK_Left:
        if(ev->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK|GDK_SHIFT_MASK))
          gtk_widget_grab_focus(bv->columns->view);
        else if(nc!=bv->columns) {
          noir_column *mc=bv->columns;
          while(mc->next!=nc)
            mc=mc->next;
          gtk_widget_grab_focus(mc->view); }
        ret=TRUE;
        break;
      case GDK_Right:
        if(ev->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
          noir_column *mc=nc;
          while(mc->next)
            mc=mc->next;
          gtk_widget_grab_focus(mc->view); }
        else if(nc->next)
          gtk_widget_grab_focus(nc->next->view);
        ret=TRUE;
        break;
      case GDK_Return:
        if(!(ev->state&(GDK_MOD1_MASK|GDK_CONTROL_MASK|GDK_SHIFT_MASK))) {
          if(S_ISDIR(isdir))
            noir_sess_focus(ns,file);
          else
            file_activate(ns,file,NULL,NULL);
          ret=TRUE; }}}}
  if(file)
    g_free(file);
  else {
    switch(ev->keyval) {
      case GDK_Menu:
      case GDK_m:
        if((ev->keyval==GDK_Menu)||(ev->state&GDK_CONTROL_MASK)) {
          menu_root_context(ns,noir_sess_get_cwd(ns),3);
          ret=TRUE; }
        break; }}

  gtk_tree_path_free(tp);
  return ret; }



/****************************************************************************/



/**
 * create a single browser column
 * @return the new column, or NULL
 * @param col_no  colum index
 * @param title   title for this column
 * @param path    the object this column represents (normally path to a folder)
 * @param bv      the browser-view this column will be part of
 */
static noir_column *browser_view_dir(int col_no,char *title,char *path,noir_browser *bv) {
  noir_column       *nc;

  GtkWidget         *align,*hbox,*image,*arrow;

  GtkTreeViewColumn *column;
  GtkCellRenderer   *renderer;
  GtkListStore      *store;
  GtkWidget         *tree,*scroll;
  GtkAdjustment     *ha,*va;
  GtkTreeSelection  *selection;

  gsize              foo;
  gchar             *utf8;

  if(!(nc=malloc(sizeof(noir_column))))
    return NULL;

  utf8=g_locale_to_utf8(title,-1,&foo,&foo,NULL);

  nc->session=bv->session;

  memset(nc,0,sizeof(noir_column));

  align=gtk_alignment_new(0,0,1,0);
  hbox=gtk_hbox_new(TRUE,0);

  gtk_widget_set_name(hbox,"hbox");
  gtk_widget_set_name(align,"align");

  /* icon-path */
  if((image=icon_get_by_file(NULL,path,MIME_FOLDER))) {
    g_object_set_data_full(G_OBJECT(image),"user_data",strdup(path),free);

    /* right-arrow */
    if(col_no) {
      arrow=gtk_arrow_new(GTK_ARROW_RIGHT,GTK_SHADOW_IN);
      gtk_widget_set_name(arrow,"arrow");
      gtk_box_pack_start(GTK_BOX(hbox),arrow,FALSE,FALSE,0); }

    gtk_box_pack_start(GTK_BOX(hbox),image,FALSE,FALSE,0);
    gtk_widget_set_size_request(GTK_WIDGET(hbox),BROWSER_COLUMN_WIDTH,-1);

    g_signal_connect(G_OBJECT(image),"drag_data_get",G_CALLBACK(icon_path_drag_data_get),(gpointer)nc);
    g_signal_connect(G_OBJECT(image),"drag_data_received",G_CALLBACK(icon_path_drag_data_received),(gpointer)nc);

    g_signal_connect(G_OBJECT(image),"button_press_event",G_CALLBACK(icon_path_clicked),(gpointer)nc);
    g_signal_connect(G_OBJECT(image),"button_release_event",G_CALLBACK(icon_path_clicked),(gpointer)nc); }

   gtk_container_add(GTK_CONTAINER(nc->icon=align),hbox);
   gtk_box_pack_start(GTK_BOX(bv->icon_path),align,FALSE,FALSE,0);



   /* list-column */
#define TREE_MAX_FILES 1000
   ha=(GtkAdjustment*)gtk_adjustment_new(0,0,1,1,3,3);
   va=(GtkAdjustment*)gtk_adjustment_new(0,0,TREE_MAX_FILES,1,10,10);

   scroll=gtk_scrolled_window_new(ha,va);

   store=gtk_list_store_new(5,
                            G_TYPE_STRING,   /* name */
                            G_TYPE_STRING,   /* file */
                            G_TYPE_INT,      /* mode */
                            G_TYPE_POINTER,  /* stat */
                            G_TYPE_POINTER); /* info */

   gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store),SORT_BY_NAME,tree_sort,noir_sess_get_sortflags(bv->session),NULL);
   gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store),tree_sort,noir_sess_get_sortflags(bv->session),NULL);
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,GTK_SORT_ASCENDING);

   tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   gtk_widget_set_name(tree,title);
   gtk_widget_set_name(scroll,"tree_scroll");
   gtk_widget_set_size_request(tree,BROWSER_COLUMN_WIDTH,-1);

   g_object_unref(G_OBJECT(store));

   renderer=noir_cell_renderer_arrow_new();
   column=gtk_tree_view_column_new_with_attributes(utf8?utf8:title,renderer,
                                                   "text",FILE_NAME,
                                                   "mode",FILE_MODE,
                                                   NULL);

   gtk_tree_view_column_set_alignment(column,0);
   gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_GROW_ONLY); /* GTK_TREE_VIEW_COLUMN_AUTOSIZE */

   gtk_tree_view_column_set_sort_indicator(column,TRUE);
   gtk_tree_view_column_set_sort_order(column,GTK_SORT_ASCENDING);
   gtk_tree_view_column_set_sort_column_id(column,0);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree),column);

   gtk_tree_view_set_hadjustment(GTK_TREE_VIEW(tree),ha);
   gtk_tree_view_set_vadjustment(GTK_TREE_VIEW(tree),va);

   gtk_tree_view_set_search_column(GTK_TREE_VIEW(tree),0);
   gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree),TRUE);

   gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tree),FALSE);

   gtk_container_add(GTK_CONTAINER(scroll),tree);

   selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
   gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);

   gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(tree),1,target,targets,GDK_ACTION_LINK|GDK_ACTION_COPY|GDK_ACTION_MOVE);
   gtk_tree_view_enable_model_drag_dest(GTK_TREE_VIEW(tree),target,targets,GDK_ACTION_LINK|GDK_ACTION_COPY|GDK_ACTION_MOVE);

   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_ALWAYS);

/* gtk_box_pack_start(GTK_BOX(bv->browser_view),scroll,FALSE,FALSE,0); */
   gtk_box_pack_start(GTK_BOX(bv->browser_view),scroll,TRUE,TRUE,0);

   nc->session=bv->session;
   nc->no=col_no;
   nc->store=store;
   nc->view=tree;
   nc->title=strdup(title);
   nc->path=strdup(path);
   nc->next=NULL;

   g_signal_connect(G_OBJECT(tree),"button_press_event",G_CALLBACK(column_clicked),(gpointer)nc);
   g_signal_connect(G_OBJECT(tree),"button_release_event",G_CALLBACK(column_clicked),(gpointer)nc);

   g_signal_connect(G_OBJECT(tree),"key-press-event",G_CALLBACK(column_keyed),(gpointer)nc);
   g_signal_connect(G_OBJECT(tree),"drag_data_get",G_CALLBACK(browser_drag_data_get),(gpointer)nc);
   g_signal_connect(G_OBJECT(tree),"drag_data_received",G_CALLBACK(browser_drag_data_received),(gpointer)nc);

   { file_info *fi=file_info_fetch(NULL,path);
     if(fi)
       dcd_set(fi,bv->client,(void *)nc);
     else {
       E(0,"could not find \"%s\" in model!?\n",path); }}

   if(utf8)
     free(utf8);

   return nc; }



/**
 * destruct adminstrative info of a single column of a browser-view.
 * called from browser_view_release_columns() and browser_view_drop_column().
 * @return NULL
 * @param col  the column to destruct
 */
static noir_column *browser_view_release_column(noir_column *col) {
  if(col) {
    if(col->title)
      free(col->title);
    if(col->path) {
      file_info    *fi=file_info_fetch(NULL,col->path);
      noir_browser *bv=(noir_browser *)noir_sess_client_get_data(col->session,CLIENT_BROWSERVIEW);
      if(fi&&bv)
        dcd_set(fi,bv->client,NULL);
      free(col->path); }
    memset(col,0,sizeof(noir_column));
    free(col); }
  return NULL; }



/**
 * release administrative info of a column and all its successors
 * called from browser_view_dst().
 * @return NULL
 * @param ref  the first column to release (normally the first column in the view)
 */
static noir_column *browser_view_release_columns(noir_column **ref) {
  if(ref&&*ref) {
    noir_column *anchor=*ref;
    noir_column *n2;
    while(anchor) {
      n2=anchor;
      anchor=anchor->next;
      browser_view_release_column(n2); }
    *ref=NULL; }
  return NULL; }



/**
 * discard a single column from a browser-view.
 * @param bv  the view to drop the column from
 * @param nc  the column to drop
 */
static inline void browser_view_drop_column(noir_browser *bv,noir_column *nc) {
  if(bv->icon_path&&nc->icon) {  /* throw out folder-icon */
    gtk_container_remove(GTK_CONTAINER(bv->icon_path),nc->icon); }
  if(nc->view) {                 /* throw out list-view */
    gtk_container_remove(GTK_CONTAINER(nc->view->parent->parent),nc->view->parent); }

  /* release administrative info of the column */
  browser_view_release_column(nc); }



/**
 * release a column and all its successors (both GTK and administrative info).
 * called from browser_view_focus().
 * @param bv  the view to remove the columns from
 * @param nc  the first column to drop
 */
static void browser_view_drop_columns_starting_with(noir_browser *bv,noir_column *nc) {
  if(nc->next)
    browser_view_drop_columns_starting_with(bv,nc->next);
  browser_view_drop_column(bv,nc); }



/***************************************************************************************/



/**
 * set "selection-changed" handler for view.
 * @param bv  the browser-view
 */
static inline void browser_view_set_selection_handler(noir_browser *bv) {
  GtkTreeSelection *sel;
  noir_column      *nc=bv->columns;

  while(nc) {
    sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(nc->view));
    g_signal_connect(G_OBJECT(sel),"changed",G_CALLBACK(browser_selection_changed),(gpointer)nc);
    nc=nc->next; }}



/****************************************************************************/



/**
 * get column in view bv that contains file fi
 * @return the column the file is shown in, or NULL for not found.
 * called from browser_view_evt().
 * @return the column the file is in, or NULL
 * @param bv  the browser-view
 * @param fi  the file to find
 */
static inline noir_column *browser_view_fetch_column_for(noir_browser *bv,file_info *fi) {
  return fi->parent?dcd_get_data(fi->parent,bv->client):NULL; }



/**
 * find the entry for a given file in a certain column
 * @return the GtkTreeIter for the file called from browser_view_focus()
 * and browser_view_evt().
 * @return a GTK tree-iter, or NULL
 * @param fi      the file to find
 * @param nc      the column to search in
 * @param create  create if not found?
 */
static inline GtkTreeIter *browser_view_find_in_dir(file_info *fi,noir_column *nc,int create) {
  GtkTreeIter it;
  int         found=FALSE;

  if(nc&&gtk_tree_model_iter_children(GTK_TREE_MODEL(nc->store),&it,NULL)) {
    gchar *tag;
    do {
      gtk_tree_model_get(GTK_TREE_MODEL(nc->store),&it,FILE_FILE,&tag,-1);
      if(tag) {
        found=!strcmp(tag,fi->tag);
        g_free(tag); }
    } while(!found&&gtk_tree_model_iter_next(GTK_TREE_MODEL(nc->store),&it)); }

  if(!found) {
    if(create)
      gtk_list_store_append(nc->store,&it);
    else
      return NULL; }

  return gtk_tree_iter_copy(&it); }



/**
 * deselect an entry in a column.
 * @return an error-code
 * @param nc  the column to deselect in
 * @param it  the iter to deselect
 */
static inline int browser_view_deselect(noir_column *nc,GtkTreeIter *it) {
  GtkTreePath *tp;
  if((tp=gtk_tree_model_get_path(GTK_TREE_MODEL(nc->store),it))) {
    GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(nc->view));
    gtk_tree_selection_unselect_path(selection,tp);
    gtk_tree_path_free(tp);
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * if file-entry is element of cwd, select it.
 * @return an error-code
 * @param nc  the column to select in
 * @param fi  the file in question
 * @param it  the GtkTreeIter corresponding to fi
 */
static inline int browser_view_select_maybe(noir_column *nc,file_info *fi,GtkTreeIter *it) {
  GtkTreePath  *tp;
  char         *p;
  size_t        l=strlen(fi->tag);

  if(!nc||!nc->session)
    return NOIR_FAIL;

  p=noir_sess_get_cwd(nc->session);

  if(((!strncmp(p,fi->tag,l))&&((!p[l]||(p[l]==DIR_DELIMITER))))&&
     (tp=gtk_tree_model_get_path(GTK_TREE_MODEL(nc->store),it))) {
    GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(nc->view));
    gtk_tree_selection_select_path(selection,tp);
#if 0
    /* scroll column so selected item is actually visible */
    { GtkAdjustment *adj=gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(nc->view));
      gint           entries=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(nc->store),NULL);
      if(adj) {
        gdouble val=gtk_adjustment_get_value(adj);
        E(0,"%s  (%d)   %lf..%lf..%lf,  %lf/%lf\n",nc->path,entries,adj->lower,val,adj->upper,adj->step_increment,adj->page_increment);
        gtk_adjustment_set_value(adj,adj->upper/entries); }}
#endif

    gtk_tree_path_free(tp);
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * replay column from model rather than the fs.  if we're in the
 * middle of reading the directory in question from the file-system,
 * we'll click in what we already have in the store, then let the
 * events from the backend do the rest.  there is room for
 * race-conditions here since all backend-events from now on will add
 * files to the column, and this routine might catch up and try to add
 * them to the column again.  this has been accounted for.
 * called from browser_view_focus().
 * @return an error-code
 * @param bv  the browser-view
 * @param fp  the directory to process
 */
static inline int browser_view_replay_maybe(noir_browser *bv,file_info *fp) {
  if(fp->child) {
    if(!(fp->have&FF_LAST))
      E(1,"replaying \"%s\" while still loading it...\n",fp->tag);
    else
      E(1,"replaying \"%s\"...\n",fp->tag);

    fp=fp->child;
    while(fp) {
      browser_view_evt(fp,MODEL_UPD,fp->have,bv);
      fp=fp->next; }

    return NOIR_SUCC; }
  return NOIR_IGNORED; }



/**
 * change focus ("cd") for a browser-view.
 * @return an error-code
 * @param bv  the browser-view to change to focus for
 * @param fi  the folder to focus on
 */
static int browser_view_focus(noir_browser *bv,file_info *fi) {
  noir_column *nc,*nn;
  file_info   *fp;
  char        *pc=strdup(fi->tag),*b,*e;
  int          cnt=1,add=FALSE;
  gint         ih=0;
  file_info    fs;

  memset(&fs,0,sizeof(fs));

  if(!pc)
    return NOIR_OOM;

  if(bv->browser_paned)
    ih=gtk_paned_get_position(GTK_PANED(bv->browser_paned));

  gtk_widget_hide_all(bv->browser_scroll);

  if(bv->browser_box&&bv->browser_padding)
    gtk_container_remove(GTK_CONTAINER(bv->browser_box),bv->browser_padding);

  if(!(bv->columns))  /* first time around */
    bv->columns=browser_view_dir(0,get_hostname(),DIR_DELIM_STR,bv);

  nc=bv->columns;
  if((fp=file_info_fetch(NULL,DIR_DELIM_STR)))
    browser_view_replay_maybe(bv,fp);

  if(!strcmp(pc,DIR_DELIM_STR))
    goto drop_maybe;

  b=pc+1;
  do {
    if((e=strchr(b,DIR_DELIMITER)))
      *e='\0';

    if(!add) {
      if(!(nn=nc->next))   /* new path is just longer, start adding */
        add=TRUE;
      else {               /* check whether current elt is in new path too */
        char   *p=nn->path;
        size_t  l=strlen(p);

        /* new path is different from this element on */
        /* drop old remainder, then start adding elts */
        if(strncmp(p,pc,l)||(pc[l]&&pc[l]!=DIR_DELIMITER)) {
          GtkTreeIter  *it;
          fs.tag=p;
          if((it=browser_view_find_in_dir(&fs,nc,FALSE))) {
            browser_view_deselect(nc,it);
            gtk_tree_iter_free(it); }
          if((it=browser_view_find_in_dir(fi,nc,FALSE))) {
            browser_view_select_maybe(nc,fi,it);
            gtk_tree_iter_free(it); }
          browser_view_drop_columns_starting_with(bv,nn);
          add=TRUE; }}}

    if(add) {
      nn=browser_view_dir(cnt,b,pc,bv);
      if((fp=file_info_fetch(NULL,pc)))
        browser_view_replay_maybe(bv,fp);
      nc->next=nn;
      nc=nn; }
    else
      nc=nc->next;

    cnt++;
    if(e)
      *(e++)=DIR_DELIMITER;
    b=e;
  } while(b);

 drop_maybe:
  if(!add&&nc) {   /* new path is shorter than old one, drop remainder */
    GtkTreeIter  *it;
    if((nn=nc->next)) {
      fs.tag=nn->path;
      if((it=browser_view_find_in_dir(&fs,nc,FALSE))) {
        browser_view_deselect(nc,it);
        gtk_tree_iter_free(it); }
      browser_view_drop_columns_starting_with(bv,nn); }
    nc->next=NULL; }

  free(pc);

  /* pad browser view on the right if window is wider than sum of columns */
  bv->browser_padding=gtk_hbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(bv->browser_box),bv->browser_padding,TRUE,TRUE,0);

  /* show right-most column */
  gtk_adjustment_set_value(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(bv->browser_scroll)),100000);

  gtk_widget_show_all(bv->browser_scroll);

  { /* resize paned so icon-path is high enough for icons (unless it was
       previously hidden, in which case we'll keep it hidden) */
    GtkRequisition r;
    if(ih)
      gtk_widget_size_request(bv->icon_path,&r);
    else
      r.height=0;
    gtk_paned_set_position(GTK_PANED(bv->browser_paned),r.height); }

  gtk_widget_show_all(bv->browser_scroll);

  browser_view_set_selection_handler(bv);

  browser_view_set_selection(bv->session);

  return NOIR_SUCC; }



/**
 * callback for file-model events.
 * @return an error-code
 * @param fi       the file the event refers to
 * @param type     the type of the event
 * @param changed  what changed
 * @param data     the browser-view
 */
static int browser_view_evt(file_info *fi,model_event type,file_flags changed,void *data) {
  noir_browser *bv=(noir_browser *)data;
  noir_column  *nc;
  static int    sema=0;
  int           ret=NOIR_SUCC;

  static int foo=0;

  if((changed>0x700))
    foo=1;

  sema++;

  if(!bv) {
    E(0,"called with no browser-view!?\n");
    ret=NOIR_FAIL;
    goto done; }

  switch(type) {
    case MODEL_CWD:
      if(changed!=FF_NONE) {   /* post */
        for(nc=bv->columns;nc&&nc->next;nc=nc->next)
          ;
        if(nc&&!strcmp(fi->tag,nc->path))
          E(1,"already in \"%s\"!\n",nc->path);
        else
          browser_view_focus(bv,fi); }
      break;

    case MODEL_MSC:
      if(changed==FF_LAST)
        browser_view_set_selection(bv->session);
      break;

    case MODEL_UPD:
      if((nc=browser_view_fetch_column_for(bv,fi))) {
        GtkListStore *browse_store=nc->store;
        GtkTreeIter  *it=browser_view_find_in_dir(fi,nc,TRUE);

/* E(0,"\"%s\" goes in column #%d -- \"%s\" (\"%s\")\n",fi->tag,nc->no,nc->title,nc->path); */

        if(!it) {
          ret=NOIR_FAIL;
          goto done; }

        browser_view_select_maybe(nc,fi,it);

        if(changed&FF_NAME)
          gtk_list_store_set(browse_store,it,FILE_NAME,fi->name,FILE_FILE,fi->tag,-1);
        if(changed&FF_STAT) {
          int isdir=fi->intrinsic?fi->intrinsic->mode:0;
          gtk_list_store_set(browse_store,it,FILE_MODE,isdir,FILE_STAT,fi->intrinsic,FILE_INFO,fi,-1); }

        gtk_tree_iter_free(it); }

      else
        E(3,"event for \"%s\" ignored...\n",fi->tag);
      break;

    case MODEL_DEL:
      if((nc=browser_view_fetch_column_for(bv,fi))) {
        GtkTreeIter  *it=browser_view_find_in_dir(fi,nc,TRUE);
        if(it) {
          gtk_list_store_remove(nc->store,it);
          gtk_tree_iter_free(it); }}
      break;

    default:
      break; }

 done:
  sema--;

  return ret; }



/**
 * browser_view_dst - destruct a browser-view.
 * set on client-record for the respective view (as callback).
 * @return an error-code
 * @param data  the browser-view to destruct
 */
static int browser_view_dst(void **data) {
  noir_browser *bv=*((noir_browser **)data);

  browser_view_release_columns(&(bv->columns));

  memset(bv,0,sizeof(noir_browser));
  free(bv);

  *data=NULL;

  return NOIR_SUCC; }



/**
 * initialize a session's browser-view.
 * @return an error-code
 * @param ns  the session
 */
int browser_view_init(noir_sess *ns) {
  GtkAdjustment  *ha=(GtkAdjustment*)gtk_adjustment_new(100,0,100,1,10,10);
  GtkWidget      *scroll=gtk_scrolled_window_new(ha,NULL);

  GtkWidget      *icon_path=gtk_hbox_new(TRUE,0);
  GtkWidget      *browser_view=gtk_hbox_new(TRUE,0);

  GtkWidget      *paned=gtk_vpaned_new();

  noir_browser   *bv;

  if(!ns)
    return NOIR_FAIL;

  if(!(bv=(noir_browser *)malloc(sizeof(noir_browser))))
    return NOIR_OOM;
  memset(bv,0,sizeof(noir_browser));

  gtk_widget_set_name(scroll,"browser_scroll");
  gtk_widget_set_name(icon_path,"browser_icon_path");
  gtk_widget_set_name(browser_view,"browser_view");
  gtk_widget_set_name(paned,"browser_paned");

  gtk_paned_pack1(GTK_PANED(paned),icon_path,TRUE,TRUE);
  gtk_paned_pack2(GTK_PANED(paned),browser_view,TRUE,TRUE);

  bv->browser_box=gtk_hbox_new(FALSE,0);
/* gtk_box_pack_start(GTK_BOX(bv->browser_box),paned,FALSE,TRUE,0); */
  gtk_box_pack_start(GTK_BOX(bv->browser_box),paned,TRUE,TRUE,0);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_NEVER);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll),bv->browser_box);

  gtk_paned_set_position(GTK_PANED(paned),48);

  g_signal_connect(G_OBJECT(scroll),"button_press_event",G_CALLBACK(blind_clicked),(gpointer)ns);

  bv->browser_paned=paned;
  bv->browser_view=browser_view;
  bv->browser_scroll=scroll;
  bv->icon_path=icon_path;

  bv->session=ns;
  bv->client=noir_sess_client_register_full(ns,CLIENT_BROWSERVIEW,(void *)bv,browser_view_evt,browser_view_dst,browser_view_set_selection);

  return NOIR_SUCC; }



/**
 * deinitialize a session's browser
 * @param ns  the session
 */
int browser_view_exit(noir_sess *ns) {
  return NOIR_SUCC; }
#endif
