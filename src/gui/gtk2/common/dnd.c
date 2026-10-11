/** @file  dnd.c
 *  @brief Drag-N-Drop related functions.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <string.h>
#include <sys/stat.h>   /* S_ISDIR */
#include <stdlib.h>     /* malloc(), free() */

#ifdef HAVE_GNOMECANVAS
#  include "gui/gtk2/icon_view/gnome-icon-list.h"
#endif

#include "gui/common/session_private.h"
#include "gui/common/alert.h"      /* form_alert() */

#include "session_gtk2.h"

#include "dnd.h"
#include "errors.h"
#include "util.h"       /* esprintf() */
#include "file.h"       /* file_...() */
#include "file_lcl.h"   /* file_to_URL */
#include "action.h"



typedef struct _dt_menu {
  noir_sess      *ns;
  action_context *src;
  char           *tgt;
  int             typ; } dt_menu;



GdkAtom *target_atoms=NULL;

const GtkTargetEntry target[]={
  { "STRING",              0,DRAGDROP_TARGET_STRING },
  { "_NETSCAPE_URL",       0,DRAGDROP_TARGET_NETSCAPE_URL }, /* netscape/moz */
  { "text/uri-list",       0,DRAGDROP_TARGET_URL },          /* konq */
  { "text/plain",          0,DRAGDROP_TARGET_TEXT },
  { "application/x-gimp-imagefile-name", 0,DRAGDROP_TARGET_IMGFILE }, /* GIMP */
  { "image/png",           0,DRAGDROP_TARGET_PNG },          /* GIMP */
  { "application/x-color", 0,DRAGDROP_TARGET_COLOUR },       /* GIMP */
  { "XdndDirectSave0",     0,DRAGDROP_TARGET_XDS }};         /* XDS always last */
const guint targets=(guint)(sizeof(target)/sizeof(GtkTargetEntry))-1;
const guint targets_xds=
#ifdef WITH_XDS
                    (guint)(sizeof(target)/sizeof(GtkTargetEntry));
#else
                    (guint)(sizeof(target)/sizeof(GtkTargetEntry))-1;
#endif






/*****************************************************************************/



#ifdef WITH_XDS
/* XDS-functions adapted by Azundris from ROX-Filer, by the ROX-Filer team. */

#define MAXURILEN 4096		/* Longest URI to allow */

GdkAtom XdndDirectSave0,xa_text_plain;



/**
 * set XDS property
 * @param context - the drag-context. the XdndDirectSave0-property of context->source_window will be modified.
 * @param text - the string to set the property to.
 */
void xds_set_prop(GdkDragContext *context,const char *text) {
  if(text)
    gdk_property_change(context->source_window,XdndDirectSave0,xa_text_plain,8,
                        GDK_PROP_MODE_REPLACE,text,strlen(text));
  else
    gdk_property_delete(context->source_window,XdndDirectSave0); }



/**
 * read XDS property
 * @return the XdndDirectSave0-property of context->source_window will be returned.
 * @param context - the drag-context.
 */
char *xds_get_prop(GdkDragContext *context) {
  guchar *prop_text;
  int     length;

  if(gdk_property_get(context->source_window,XdndDirectSave0,xa_text_plain,0,
                      MAXURILEN,FALSE,NULL,NULL,&length,&prop_text)&&prop_text)
  {
    prop_text=g_realloc(prop_text,length+1);
    prop_text[length]='\0';
    return prop_text; }

  return NULL; }

#endif



/*****************************************************************************/
/* DND helpers */



/**
 * return next string in a \n separated list
 * @return start of a C-string
 * @param drop  pointer to remainder of list
 */
char *dnd_get_file_from_drop(char **drop) {
  char *p,*r;

  if(!drop||!*drop)
    return NULL;

  r=*drop;                       /* remember head */

  if((p=strchr(r,DELIMITER_)))   /* find \n */
    *(p++)='\0';
  *drop=p;                       /* set drop to tail or NULL */

  if(strstr(r,PROTOCOL_DELIMITER)) {
    if((p=strchr(r,'#')))
      *p='\0'; }

  /* remove file:// if present */
#if 1
  if(!strncmp(r,PROTOCOL_FILE,sizeof(PROTOCOL_FILE)-2)) { /* -2 i/o -1 so we catch broken konquie URLs, as well */
    if((p=strchr(r,'#')))
      *p='\0';
    if((p=strchr(r,'\r')))
      *p='\0';
    p=(char *)file_if_local(r);
    E(0,"pre   |%s|%s|\n",p,r);
    r=url_decode(p,FALSE); }
  E(0,"post |%s|\n",r);
#else
  r=(!strncmp(r,PROTOCOL_FILE,sizeof(PROTOCOL_FILE)-1))
    ?(r+sizeof(PROTOCOL_FILE)-1)
    :r;
#endif

  /* if rightmost element is /. complain and return next element */
  if((p=strrchr(r,'/'))&&!strcmp(p,"/.")) {
    E(0,"Cannot use the magic .folder in this way yet...\n");
    /* form_alert(NULL,"Cannot use the magic " DIR_DOT "\nfolder in this way yet..."); */  /* ### FIXME */
    free(r);
    r=dnd_get_file_from_drop(drop); }

  return r; }



/**
 * DND: Is the sender willing/able to supply this target type?
 * @return TRUE/FALSE
 * @param dc   the drag-context
 * @param tgt  the target-type we want to check for
 */
gboolean dnd_src_speaks(GdkDragContext *dc,GdkAtom tgt) {
#if GTK_CHECK_VERSION(3,0,0)
  GList *tl=(GList *)gdk_drag_context_list_targets(dc);
#else
  GList *tl=dc->targets;
#endif
  while(tl&&((GdkAtom)tl->data!=tgt))
    tl=tl->next;
  return (tl!=NULL); }



/**
 * DND: drag completed, negotiate content-type.
 * User has tried to drop some data on us. Decide what format we would like
 * the data in.
 * @param widget     the widget
 * @param dc         the drag-context
 * @param timestamp  when the event happened
 * @param where      file-system path of the target object (item or "background")
 */
gboolean dnd_drag_drop(GtkWidget *widget,GdkDragContext *dc,guint timestamp,char *where) {
  char       *msg=NULL;
  GdkAtom     tgt=GDK_NONE;

  if(!where)
    return TRUE;

#ifdef WITH_XDS
  if(dnd_src_speaks(dc,XdndDirectSave0)) {
    char *what=NULL;
    if((what=xds_get_prop(dc))) {
      if(strchr(what,'/')) {  /* '/' == part of XDS, not backend, use verbatim */
        msg="XDS protocol msg: item may not contain '/'\n";
        g_free(what); }
      else {
        char *x=NULL;
        esprintf(&x,"file://%s%s/%s",get_hostname(),where,what);
        if(x) {
          E(0,"XDS: target \"%s\"\n",x);
          xds_set_prop(dc,x);
          tgt=XdndDirectSave0;
          *x='\0'; /* free() here? */
          free(x); }
        else
          msg="out of memory"; }}
    else
      msg="XdndDirectSave0 target provided, but the atom "
          "XdndDirectSave0 (type text/plain) did not "
          "contain the desired name.\n"; }

  /* no XDS */
  else
#endif
  { GList *tl;
    int    n=0;

    while(n<targets) {
#if GTK_CHECK_VERSION(3,0,0)
      tl=(GList *)gdk_drag_context_list_targets(dc);
#else
      tl=dc->targets;
#endif
      while(tl&&((GdkAtom)tl->data!=target_atoms[n]))
        tl=tl->next;
      if(!tl)
        n++;
      else {
        /* netscape gives _NETSCAPE_URL, konquie gives text/uri-list */
        E(1,"target-type %s\n",target[n].target);
        n=targets; }}

    if(tl)
      tgt=(GdkAtom)tl;
    else
      msg="couldn't agree on a data-format..."; }

  if(msg) {
    E(0,"%s",msg);
    gtk_drag_finish(dc,FALSE,FALSE,timestamp); }
/*
  else
    gtk_drag_get_data(widget,dc,tgt,timestamp);
*/
  return TRUE; }



#if 1
/**
 * make URL from 'file' and append to 'list'.
 * icon_drag_data_get() will send the list to the drop-target.
 * @param list - the list
 * @param file - the file
 */
gchar *dnd_send_URL(gchar *list,gchar *file) {
  gchar *new=NULL;
  if(file) {
    char *myfile=file_to_URL(file);
    if(myfile) {
      if(list) {
        if((new=malloc(strlen(file)+strlen(list)+2))) {
          strcat(strcat(strcpy(new,list),DELIMITER),file);
          g_free(list); }}
      else
        new=strdup(file);
      free(myfile); }}
  return new; }



/**
 * add 'file' to 'list'
 * icon_drag_data_get() will send the list to the drop-target.
 * @param list - the list
 * @param file - the file
 */
gchar *dnd_send_string(gchar *list,gchar *file) {
  gchar *new=NULL;
  if(file) {
    if(list) {
      if((new=malloc(strlen(file)+strlen(list)+2))) {
        strcat(strcat(strcpy(new,list),DELIMITER),file);
        g_free(list); }}
    else
      new=strdup(file); }
  return new; }
#endif



/**
 * a file or selection was dropped, all questions (if any) were asked, now
 * take action (and move, copy, link, whatever).
 * @param ns - the session
 * @param dtm - the dt_menu struct; will be released
 */
static int file_drop_do(noir_sess *ns,dt_menu *dtm) {
  int   ret=NOIR_SUCC;

  if(!dtm)
    return NOIR_FAIL;

  if(dtm->src&&dtm->tgt) {    /* have source and target */
    action_file *af=action_context_get_1st(dtm->src);
    if(!af)                   /* no src-items!?  fail!  */
      ret=NOIR_NODATA;

                              /* dropped on top of itself?  ignore! */
    else if(!action_file_get_next(af)&&
            !strcmp(action_file_get_tag(af),dtm->tgt)) {
      E(0,"item dropped on top of itself, ignored...\n");
      ret=NOIR_IGNORED; }

                              /* is target a folder? */
    else if(S_ISDIR(file_exists(dtm->tgt))) {

      switch(dtm->typ) {
      case GDK_ACTION_MOVE: { /* move items to folder */
#if 0
        char        *dn=NULL;
        action_file *a2;
        int          lcl=1;
        for(a2=action_context_get_1st(dtm->src);a2;a2=action_file_get_next(a2)) {
          if((dn=file_dirname(action_file_get_tag(a2)))) {
            if(strcmp(dn,dtm->tgt))
              lcl=0;
            free(dn); }}
        if(lcl) {
          E(0,"lcl move\n");
          ret=NOIR_IGNORED; }
        else
#endif
        if((af=action_file_new(NULL,dtm->tgt))) {
          action_context_set_single(dtm->src,af);
          ret=file_job_create(ns,NOIR_JOB_MV,dtm->src,NULL);
          dtm->src=NULL; }
        else
          ret=NOIR_OOM; }
        break;

      case GDK_ACTION_COPY:   /* copy items to folder */
        if((af=action_file_new(NULL,dtm->tgt))) {
          action_context_set_single(dtm->src,af);
          ret=file_job_create(ns,NOIR_JOB_CP,dtm->src,NULL);
          dtm->src=NULL; }
        else
          ret=NOIR_OOM;
        break;

      case GDK_ACTION_LINK:   /* link to these items from within folder */
        while(af) {
          ret=file_link(TRUE,action_file_get_tag(af),dtm->tgt);
          af=action_file_get_next(af); }
        break; }}

    else {                    /* target not folder. */
      if(dtm->typ!=GDK_ACTION_COPY) {
        form_alert(NULL,"'Link' and 'move' actions are only available for use with folders!");
        ret=NOIR_FAIL; }
      else {                  /* target is application. */
        int r=NOIR_IGNORED;
        while(af) {
          file_partial(file_activate(NULL,action_file_get_tag(af),NULL,dtm->tgt),&r);
          af=action_file_get_next(af); }
        ret=(r==NOIR_IGNORED)?NOIR_FAIL:r; }}}
  else
    ret=NOIR_NODATA;

  if(dtm->src)
    action_context_dst(dtm->src);
  if(dtm->tgt)
    free(dtm->tgt);
  free(dtm);

  return ret; }



/**
 * views can connect "drag-end" to this.
 * releases DND-info.
 */
void file_drop_free(GtkWidget *widget,GdkDragContext *dc,gpointer data) {
  noir_sess *ns=data;

  if(!ns||(noir_sess_get_dragcontext(ns)!=dc)||!ns->drag_files)
    return;

  action_context_dst(ns->drag_files);
  noir_sess_set_dragcontext(ns,NULL);
  ns->drag_files=NULL; }



/**
 * views may connect "drag_data_delete" to this.
 */
void file_drop_delete_from_iconlist(GtkWidget *widget,GdkDragContext *dc,gpointer data) {
  noir_sess   *ns=data;
  action_file *af;

  if(dc==noir_sess_get_dragcontext(ns)) {
    for(af=action_context_get_1st(ns->drag_files);af;af=action_file_get_next(af)) {
#if defined(HAVE_GNOMECANVAS)&&!defined(STANDALONE)
        int no;
        char *i=NULL;
        for(no=0;i;no++) {
          if((i=gnome_icon_list_get_icon_filename(GNOME_ICON_LIST(widget),no))&&!strcmp(i,action_file_get_tag(af))) {
            gnome_icon_list_remove(GNOME_ICON_LIST(widget),no);
            i=NULL; }}
#endif
    }}}



/**
 * remember what data we sent the drop target.
 * @param ns - session
 * @param dc - drag-context
 * @param reply - the info we sent
 */
void file_drop_remember(noir_sess *ns,GdkDragContext *dc,action_context *reply) {
  static int sema=0;

  if(sema>0) { /* we only allow one drag at a time... */
    E(0,"already running, denied.\n");
    return; }

  sema++;

  if(ns->drag_files) {
    char *o=action_context_files_to_quoted_string(ns->drag_files),
         *n=action_context_files_to_quoted_string(reply);
    E(0,"cannot set drag_context to \"%s\", already set to %s...\n",n?n:"???",o?o:"???");
    if(n) free(n);
    if(o) free(o); }

  while(ns->drag_files)
    mainloop_once();

  noir_sess_set_dragcontext(ns,dc);
  ns->drag_files=reply;

  sema--; }



/**
 * callback for drop-actions menu.
 * @param item - the GTK menu-item that was selected. "data" property should contain action code.
 * @param data - the drop-context
 */
static gint dropactions_menu_selected(GtkWidget *item,gpointer data) {
  dt_menu *dtm=(dt_menu *)data;
  if(dtm) {
    dtm->typ=(int)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"data"));
    return TRUE; }
  else
    return FALSE; }



/**
 * add an entry to drop-actions menu.
 * @param menu - the GTK menu to add to
 * @param dtm - the drop-context
 * @param label - the label for the new item
 * @param code - the action-code this item will emit when activated (cp, ln, mv, ...)
 * this code will be set on the item's "data" property.
 */
static void dropactions_add(GtkWidget *menu,dt_menu *dtm,char *label,int code) {
  GtkWidget *item;
  item=gtk_menu_item_new_with_label(label);
  g_object_set_data(G_OBJECT(item),"data",GINT_TO_POINTER(code));
  g_signal_connect(item,"activate",G_CALLBACK(dropactions_menu_selected),dtm);
  gtk_menu_shell_append((GtkMenuShell *)menu,item); }



/**
 * create and run a drop-actions ("mv, ln, cp?") menu.
 * @param dtm - the drop-context (what sources, what target, ...)
 */
static int dropactions_menu(dt_menu *dtm) {
  GtkWidget *menu=gtk_menu_new();

  dropactions_add(menu,dtm,"Move",GDK_ACTION_MOVE);
  dropactions_add(menu,dtm,"Link",GDK_ACTION_LINK);
  dropactions_add(menu,dtm,"Copy",GDK_ACTION_COPY);

  gtk_widget_show_all(menu);
#if GTK_CHECK_VERSION(3,0,0)
  gtk_menu_popup_at_pointer(GTK_MENU(menu),NULL);
#else
  gtk_menu_popup(GTK_MENU(menu),NULL,NULL,NULL,NULL,1,0);
#endif

  return NOIR_SUCC; }



/**
 * something was dropped on evidence, take action.
 * @param ns - session
 * @param type - what kind of action? (cp, mv, ln, ...)
 * @param info - what type of source-data? (as per target[])
 * @param files - what was dropped?
 * @param tgt - where was it dropped?
 */
int file_drop(noir_sess *ns,int type,guint info,const char *files,const char *tgt) {
  dt_menu        *dtm=malloc(sizeof(dt_menu));
  char           *mf=NULL,*file;
  int             cnt=0;
  action_context *ac=NULL;
  size_t          l;

  if(type==GDK_ACTION_PRIVATE)
    return NOIR_NOTSUPP;
  if(!dtm)
    goto fd_oom;

  memset(dtm,0,sizeof(dt_menu));

  E(1,"#%d (%s): \"%s\" on \"%s\" in \"%s\"\n",type,target[type].target,files,tgt,ns->cwd);

  if(!(mf=strdup(files))||!(dtm->tgt=strdup(tgt)))
    goto fd_oom;

  if(!(ac=action_context_new(0,NULL)))
    goto fd_oom;

  while((file=dnd_get_file_from_drop(&mf))) {
    l=strlen(file);

    cnt++;

    switch(info) {
#if 0
      case DRAGDROP_TARGET_URL: /* konquie's text/uri-list has \r\n */
        if(l&&(file[l-1]=='\r'))
          file[--l]='\0';
        break;
#endif
      case DRAGDROP_TARGET_NETSCAPE_URL:  /* netscape also gives us the URL-title, ignore */
        if(!(cnt%2))
          continue;
        break;
      default:
        break; }

    E(2,"got |%s| %d\n",file,strlen(file));

    if(!l)
      ;
    else
      action_file_new(ac,file); }

  dtm->src=ac;

  free(mf);
  mf=NULL;

  switch(type) {
  case GDK_ACTION_DEFAULT:
    dtm->typ=GDK_ACTION_COPY;
    break;
  case GDK_ACTION_MOVE:
  case GDK_ACTION_COPY:
  case GDK_ACTION_LINK:
    dtm->typ=type;
    break;
  case GDK_ACTION_ASK:
  default:
#if 0
    if(S_ISDIR(file_exists(tgt)))
      dtm->typ=GDK_ACTION_COPY;
    else
#endif
    { dropactions_menu(dtm);
      while(dtm->typ==GDK_ACTION_ASK)
        mainloop_once(); }
    break; }

  return file_drop_do(ns,dtm);

 fd_oom:
  if(ac)
    action_context_dst(ac);
  if(mf)
    free(mf);
  if(dtm) {
    if(dtm->tgt)
      free(dtm->tgt);
    free(dtm); }
  return NOIR_OOM; }



/**
 * init drag-n-drop subsystem
 * @return an error-code
 * @param ns  the session
 */
int dnd_init(noir_sess *ns) {
  int n;

#ifdef WITH_XDS
  XdndDirectSave0=gdk_atom_intern("XdndDirectSave0",FALSE);
  xa_text_plain  =gdk_atom_intern("text/plain",FALSE);
#endif

  if(!(target_atoms=malloc(sizeof(GdkAtom)*targets)))
    return NOIR_OOM;

  for(n=0;n<targets;n++)
    target_atoms[n]=gdk_atom_intern(target[n].target,FALSE);

  return NOIR_SUCC; }



/**
 * de-init drag-n-drop subsystem
 * @return an error-code
 * @param ns  the session
 */
int dnd_exit(noir_sess *ns) {
  free(target_atoms);
  return NOIR_IGNORED; }
