/** @file  alert.c
 *  @brief modal dialogs inc.
 */

#include "errors.h"
#include "util.h"               /* e2utf8() */
#include "file.h"               /* file_partial() */

#include "src/gui/common/session.h"
#include "src/gui/common/session_private.h"
#include "src/gui/common/alert.h"

#include "session_gtk2.h"  /* noir_sess_get_window() */

#include <gtk/gtk.h>

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>             /* malloc(), free() */



static char *stock[]={
  "File is inaccessible; stale NFS-handle, maybe?",
  "File is not a binary executable, and no\nhandlers for its MIME-type were declared.",
  "Sorry, you cannot open that folder.\nMaybe you don't have the permissions required to access it?",
  "Sorry, there is nothing in that folder to be shown.",
  NULL };



/******************************************************************************/



/**
 * mutex-lock GUI (put main thread to sleep)
 */
void form_threads_enter(void) {
#ifdef HAVE_GTHREAD
  if(!pthread_equal(main_thread,pthread_self()))
    gdk_threads_enter();
#endif
}

/**
 * mutex-unlock GUI (wake up main thread)
 */
void form_threads_leave(void) {
#ifdef HAVE_GTHREAD
  if(!pthread_equal(main_thread,pthread_self())) {
    gdk_flush(); gdk_threads_leave(); }
#endif
}






/******************************************************************************/



/**
 * modeless info-box was destroyed, invalidate ref to it
 */
/* user1..user6 of a noir_job hold win,box,label,bar1,bar2,bar3; the
   window only knows &job->user1, the remaining five pointers follow
   contiguously right behind it (see struct _noir_job in file.h) */
static void form_modeless_destroyed(GtkWidget *w,void **win) {
  if(win) {
    void **u;
    int    n;
    *win=NULL;
    for(u=win+1,n=0;n<5;n++,u++)
      *u=NULL; } }

/**
 * destroy a modeless info-box
 * @return an error-code
 * @param win  the window to destroy
 */
int form_modeless_dst(void **win) {
  if(win&&*win) {
    form_threads_enter();
    gtk_widget_destroy(*win);
    form_threads_leave();
    return NOIR_SUCC; }
  else
    return NOIR_IGNORED; }



/**
 * update bar in a modeless dial
 * @return an error-code
 * @param bar  the progress bar widget
 * @param msg  text to set, or NULL for none
 * @param p    the percent (<0 for pulse)
 */
int form_modeless_bar_update(void *bar,const char *msg,double p) {
  int   ret=NOIR_IGNORED;
  char *m=NULL;

  if(!bar||!*((GtkWidget **)bar))
    return NOIR_IGNORED;

  form_threads_enter();

  if(msg) {
    if((m=e2utf8(msg))) {
      gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bar),m);
      free(m);
      file_partial(NOIR_SUCC,&ret); }
    else
      file_partial(NOIR_FAIL,&ret); }

  if(p<0)
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(bar));
  else if((p>=0)&&(p<=1.0))
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar),p);
  else {
    E(3,"wonky fraction %f for progress-bar \"%s\" set to 0.0...\n",p,msg);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar),0.0); }

  form_threads_leave();

  return ret; }



/**
 * add a progress bar to a modeless dial
 * @return an error-code
 * @param vbox  the container to add the bar to
 * @param bar   where the handle of the label is returned
 */
int form_modeless_bar_new(void *vbox,void **bar) {
  GtkWidget *pb=NULL;
  int        ret=NOIR_IGNORED;

  if(!vbox||!bar)
    return NOIR_FAIL;

  form_threads_enter();

  if((pb=gtk_progress_bar_new())) {
    gtk_box_pack_start(GTK_BOX(vbox),pb,FALSE,FALSE,0);
    gtk_widget_set_name(pb,"progress-bar");
    gtk_widget_show_all(vbox);
    ret=NOIR_SUCC; }
  else
    ret=NOIR_OOM;

  form_threads_leave();

  if(bar)
    *bar=pb;

  return ret; }



/**
 * update label in a modeless dial
 * @return an error-code
 * @param label  the label widget
 * @param msg    the new text for the label
 */
int form_modeless_label_update(void *label,const char *msg) {
  char *m;

  if(!msg)
    msg="";
  if(!label||!*((GtkWidget **)label))
    return NOIR_IGNORED;

  if((m=e2utf8(msg))) {
    form_threads_enter();
    gtk_label_set_text(GTK_LABEL(label),m);
    form_threads_leave();
    free(m);
    return NOIR_SUCC; }

  return NOIR_FAIL; }



/**
 * add a label to a modeless dial
 * @return an error-code
 * @param vbox  the container to add the bar to
 * @param lbl   where the handle of the label is returned
 * @param msg   the text to set on the label, or NULL for none
 */
int form_modeless_label_new(void *vbox,void **lbl,const char *msg) {
  GtkWidget *label=NULL;
  int        ret=NOIR_IGNORED;

  if(!vbox||!lbl)
    return NOIR_FAIL;

  form_threads_enter();

  if(msg) {
    char      *m=e2utf8(msg);
    if(m) {
      if((label=gtk_label_new(m))) {
        gtk_box_pack_start(GTK_BOX(vbox),label,FALSE,FALSE,0);
	gtk_widget_set_name(label,"label");
	gtk_widget_show_all(vbox);
	ret=NOIR_SUCC; }
      free(m); }
    else
      ret=NOIR_FAIL; }

  form_threads_leave();

  if(lbl)
    *lbl=label;

  return ret; }



/**
 * show a modeless dial
 * @return an error-code
 * @param win  the window to show
 */
int form_modeless_show(void *win) {
  if(win) {
    form_threads_enter();
    gtk_widget_show_all(GTK_WIDGET((GtkWidget *)win));
    form_threads_leave();
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * create a modeless dial
 * @return an error-code
 * @param win  where the handle of the window is returned
 * @param box  where the handle of the container is returned
 */
int form_modeless_new(void **win,void **box) {
  GtkWidget *window,*vbox=NULL;
  int        ret=NOIR_IGNORED;

  if(!win||!box)
    return NOIR_FAIL;

  form_threads_enter();

  if((window=gtk_window_new(GTK_WINDOW_TOPLEVEL))) {
    g_signal_connect(G_OBJECT(window),"destroy",G_CALLBACK(form_modeless_destroyed),(gpointer)win);
    gtk_window_set_type_hint(GTK_WINDOW(window),GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_decorated(GTK_WINDOW(window),TRUE);
    gtk_window_set_position(GTK_WINDOW(window),GTK_WIN_POS_MOUSE);  /* GTK_WIN_POS_CENTER_ON_PARENT */
    gtk_window_set_modal(GTK_WINDOW(window),FALSE);
    gtk_window_set_wmclass(GTK_WINDOW(window),"progress_indicator","evidence");
    gtk_window_set_policy(GTK_WINDOW(window),FALSE,FALSE,TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(window),8);
/*  gdk_window_set_decorations(GTK_WIDGET(window)->window,GDK_DECOR_BORDER); */
    gtk_widget_set_name(window,"modeless_window");
    if(!(vbox=gtk_vbox_new(TRUE,0))) {
      gtk_widget_destroy(GTK_WIDGET(window));
      ret=NOIR_FAIL; }
    else {
      gtk_widget_set_name(vbox,"container-box");
      gtk_container_add(GTK_CONTAINER(window),vbox);
      ret=NOIR_SUCC; }}
  else
    ret=NOIR_OOM;

  form_threads_leave();

  *win=window;
  *box=vbox;

  return ret; }






/******************************************************************************/
/* dialog-boxes */



/**
 * handle a dialog.
 * @return index of selected button if ab==ALERT_BUTTONS_CUSTOM, or one of
 * { NOIR_SUCC, NOIR_FAIL } otherwise.
 * @param ns - the session that caused this dialog, or NULL
 * @param at - what symbol to show (ALERT_ERROR/ALERT_QUESTION)
 * @param ab - what buttons to show (ALERT_BUTTONS_OK,ALERT_BUTTONS_YES_NO,ALERT_BUTTONS_CUSTOM)
 * @param msg - the message to display
 * @param btxt - the labels for the buttons if ab==ALERT_BUTTONS_CUSTOM, separated by pipe-characters "|"
 */
int form_dial(void *ns,alert_type at,alert_buttons ab,const char *msg,const char *btxt) {
  GtkWidget      *dialog;
  gint            res;
  GtkMessageType  type=GTK_MESSAGE_ERROR;
  GtkButtonsType  buttons=GTK_BUTTONS_NONE;
  char           *butt=NULL;
  char           *message=e2utf8(msg),*escaped=NULL,*s,*t;
  gint            pc=0;

  if(!message)
    return NOIR_FAIL;

  switch(ab) {
    case ALERT_BUTTONS_OK:     buttons=GTK_BUTTONS_OK; break;
    case ALERT_BUTTONS_YES_NO: buttons=GTK_BUTTONS_YES_NO; break;
    case ALERT_BUTTONS_CUSTOM: buttons=GTK_BUTTONS_NONE; break; }

  switch(at) {
    case ALERT_ERROR:    type=GTK_MESSAGE_ERROR;    break;
    case ALERT_QUESTION: type=GTK_MESSAGE_QUESTION; break; }

  /* gtk_message_dialog_new() accepts %-formatting, so make all %s we might
     have literal. */
  for(pc=0,s=message;*s;s++)
    if(*s=='%')
      pc++;

  if(pc) {
    if(!(escaped=malloc(pc+strlen(message)+1))) {
      free(message);
      return NOIR_OOM; }

    for(s=message,t=escaped;*s;s++,t++) {
      if(*s=='%')
        *(t++)='%';
      *t=*s; }
    *t='\0';

    E(0,"escaped dial-text: \"%s\" -> \"%s\"\n",message,escaped); }

  else if(!(escaped=strdup(message))) {
    free(message);
    return NOIR_OOM; }

  form_threads_enter();

  dialog=gtk_message_dialog_new(ns?GTK_WINDOW(noir_sess_get_window(ns)):NULL,
                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                type,buttons,escaped);

  if(btxt) {
    if((butt=strdup(btxt))) {
      char *p;
      char *e=butt;
      int   c=0;

      buttons=GTK_BUTTONS_NONE;
      do {
        p=e;
        if((e=strchr(p,'|')))
          *(e++)='\0';
        gtk_dialog_add_button(GTK_DIALOG(dialog),p,++c);
      } while(e); }}

  res=gtk_dialog_run(GTK_DIALOG(dialog));

  gtk_widget_destroy(dialog);

  form_threads_leave();

  free(escaped);
  free(message);

  if(butt) {
    free(butt);
    return res; }

  if(ab==ALERT_BUTTONS_CUSTOM)
    return res;

  switch(res) {
    case GTK_RESPONSE_ACCEPT:
    case GTK_RESPONSE_OK:
    case GTK_RESPONSE_YES:
    case GTK_RESPONSE_APPLY:
      return NOIR_SUCC;
      break;
    default:
      return NOIR_FAIL; }}



/**
 * a simple yes/no dialog.
 * @return NOIR_SUCC or NOIR_FAIL.
 * @param ns - the session that caused this dialog, or NULL
 * @param message - the message to display
 */
int form_yes_no(void *ns,const char *message) {
  return form_dial(ns,ALERT_QUESTION,ALERT_BUTTONS_YES_NO,message,NULL); }



/**
 * a yes/no/yes-to-all dialog.
 * @return NOIR_SUCC or NOIR_FAIL.
 * @param ns - the session that caused this dialog, or NULL
 * @param message - the message to display
 * @param ac - action-context (to remember "yes-to-all" was clicked etc.), or NULL
 */
int form_yes_no_all(void *ns,const char *message,action_context *ac) {
  if(ac&&action_context_get_selection(ac)&&action_context_tst_flags(ac,AF_SELECTION)) {  /* have selection */
    int ret=NOIR_NOTSUPP;

    if(action_context_get_flags(ac)&AF_YES_TO_ALL)
      return NOIR_SUCC;
    else if(action_context_get_flags(ac)&AF_CANCEL_ALL)
      return NOIR_FAIL;

    switch(form_dial(ns,ALERT_QUESTION,ALERT_BUTTONS_CUSTOM,message,
                     "_Yes|Yes to _all|_Skip|_Cancel")) {
    case 2:
      action_context_add_flags(ac,AF_YES_TO_ALL);
      /* FALLTHROUGH */
    case 1:
      ret=NOIR_SUCC;
      break;
    case 4:
      action_context_add_flags(ac,AF_CANCEL_ALL|AF_LAST);
      /* FALLTHROUGH */
     case 3:
      ret=NOIR_FAIL;
      break; }
    return ret; }

  /* selection is (now) empty */
  else if(ac&&!action_context_get_selection(ac)&&action_context_tst_flags(ac,AF_SELECTION)) {
    action_context_add_flags(ac,AF_LAST);

    if(action_context_tst_flags(ac,AF_YES_TO_ALL))
      return NOIR_SUCC;
    else if(action_context_tst_flags(ac,AF_CANCEL_ALL))
      return NOIR_FAIL; }

  /* only file (left) */
  return form_dial(ns,ALERT_QUESTION,ALERT_BUTTONS_YES_NO,message,NULL); }



/**
 * show a dialog box
 * @param ns - the session that caused this dialog, or NULL
 * @param fmt - the message to display. may contain %-substitutions as per printf()
 * @param ... - variables of appropriate type and number for %-substitutions
 */
int form_alert(void *ns,const char *fmt, ...) {
  int      need,have;
  va_list  ap;
  char    *n;

  if(!(n=(char*)malloc(have=512)))
    return NOIR_OOM;

retry:
  va_start(ap,fmt);
  need=vsnprintf(n,have,fmt,ap);
  va_end(ap);

  if((need>=have)||(need<0)) {
    char *p;
    if(need<0)
      need=2*have;
    else
      need++;
    if(!(p=(char*)realloc(n,need))) {
      free(n);
      return NOIR_OOM; }
    have=need;
    n=p;
    goto retry; }

  (void)form_dial(ns,ALERT_ERROR,ALERT_BUTTONS_OK,n,NULL);
  free(n);

  return NOIR_SUCC; }



/**
 * display a stock dialog
 * @param ns - the session that caused this dialog, or NULL
 * @param t - the type of dialog to show
 */
void form_stock(void *ns,alert_stock t) {
  (void)form_dial(ns,ALERT_ERROR,ALERT_BUTTONS_OK,stock[t],NULL); }



/*****************************************************************************/
/* if src and dst for a file-op are the same, interview user */



/**
 * break inner event-loop for "copy over?"-dialog.
 * @param win - the window the dialog lives in
 * @param data - a pointer to a conditional we'll set to 0 to break the loop
 */
static void form_file_edit_cxl(GtkWindow *win,gpointer data) {
#ifdef HAVE_GTHREAD
  pthread_mutex_unlock((pthread_mutex_t *)data);
#else
  *((int *)data)=0;
#endif
}



/**
 * "copy over?"-dialog was confirmed; get final dst name,
 * then close dialog and break its event-loop.
 */
static void form_file_edit_do(GtkWidget *ent,gpointer data) {
  GtkWidget  **wi=(GtkWidget **)data;
  GtkWidget   *to=wi[1];
  GtkWindow   *win=GTK_WINDOW(wi[2]);
  gsize        foo;
  char        *new_to,
             **old_to=g_object_get_data(G_OBJECT(to),"data");
  const char  *utf8;

  utf8=gtk_entry_get_text(GTK_ENTRY(to));
  new_to=g_locale_from_utf8(utf8,-1,&foo,&foo,NULL);
  if(strcmp(new_to,*old_to)) {
    free(*old_to);
    *old_to=new_to; }
  else
    g_free(new_to);

  gtk_widget_destroy(GTK_WIDGET(win)); }



/**
 * user tries to copy or move a file on top of itself. ask them for a
 * better target name. this function will block until the user
 * decides; if after the call the src and tgt names are still the
 * same, the calling function should abort, otherwise, it should
 * copy/move/rename.
 * @param op - the operation in question, single word ("copy" or "move"), or full description
 * @param t - where the pointer to the target name is stored (may change)
 */
void form_file_edit(const char *op,char **t) {
  GtkWidget *win=gtk_window_new(GTK_WINDOW_TOPLEVEL),
            *label=NULL,
            *label_fm=NULL,
            *label_to=NULL,
            *box=gtk_vbox_new(FALSE,0),
            *fm=gtk_entry_new(),
            *to=gtk_entry_new();
  GtkWidget *wi[3];
  gsize      foo;
  char      *utf8;

#ifdef HAVE_GTHREAD
  pthread_mutex_t ffl=PTHREAD_MUTEX_INITIALIZER;
#else
  int        ffl=1;
#endif

  if(strchr(op,' ')) {
    label=gtk_label_new(op);
    label_fm=gtk_label_new("\nName of the existing item...");
    label_to=gtk_label_new("\nName for the new item..."); }
  else {
    label=gtk_label_new("You are trying to copy or move a file\n"
                        "on top of itself. Unless you choose a\n"
                        "different name for the destination file,\n"
                        "this item will be skipped...");
    label_fm=gtk_label_new(!strcmp(op,"copy")?"\nName of the original...":"\nOld name...");
    label_to=gtk_label_new(!strcmp(op,"copy")?"\nName for the copy...":"\nNew name..."); }

  form_threads_enter();

  gtk_container_add(GTK_CONTAINER(win),box);

  gtk_container_add(GTK_CONTAINER(box),label);

  utf8=g_locale_to_utf8(*t,-1,&foo,&foo,NULL);

  gtk_container_add(GTK_CONTAINER(box),label_fm);
  gtk_widget_set_name(fm,"rename_src");
  gtk_entry_set_text(GTK_ENTRY(fm),utf8);
  gtk_container_add(GTK_CONTAINER(box),fm);
  gtk_widget_set_sensitive(fm,FALSE);

  gtk_container_add(GTK_CONTAINER(box),label_to);
  gtk_widget_set_name(to,"rename_dst");
  gtk_entry_set_text(GTK_ENTRY(to),utf8);
  gtk_container_add(GTK_CONTAINER(box),to);
  GTK_WIDGET_SET_FLAGS(to,GTK_CAN_DEFAULT);

  g_free(utf8);

  gtk_widget_set_name(win,"rename_window");
  gtk_window_set_title(GTK_WINDOW(win),op);
  gtk_entry_set_activates_default(GTK_ENTRY(to),TRUE);
  gtk_window_set_default(GTK_WINDOW(win),to);

  g_object_set_data(G_OBJECT(to), "data",t);
  g_object_set_data(G_OBJECT(win),"data",(char *)op);

  wi[0]=fm; wi[1]=to; wi[2]=win;
  g_signal_connect(GTK_OBJECT(to), "activate",G_CALLBACK(form_file_edit_do),(gpointer)wi);
  g_signal_connect(GTK_OBJECT(win),"destroy", G_CALLBACK(form_file_edit_cxl),(gpointer)&ffl);

  gtk_widget_show_all(GTK_WIDGET(win));

#ifdef HAVE_GTHREAD
  gdk_flush();

  pthread_mutex_lock(&ffl);   /* lock it */

  form_threads_leave();

  /* returning to main-loop, which will unlock our mutex at some point.
     only then will we be able to relock it. */

  pthread_mutex_lock(&ffl);
#else
  while(ffl)
    mainloop_once();

#  if 0
  while(gdk_events_pending())
    g_main_context_iteration(NULL,TRUE);
#  endif
#endif
}



/******************************************************************************/
