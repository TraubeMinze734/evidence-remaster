/** @file  rename.c
 *  @brief context-menu action "rename item"
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "evidence.h"
#include "gui/common/alert.h"
#include "gui/common/session.h"
#include "file.h"       /* file_mv() */
#include "file_lcl.h"   /* file_compose_path() */
#include "meta.h"
#include "util.h"



typedef struct _ren_ctx {
  char      *oldname,*cwd;
  void      *user1;
} ren_ctx;



/*****************************************************************************/



/**
 * destruct a rename context
 * @return NULL
 * @param ctx  the context to destroy
 */
ren_ctx *ren_ctx_dst(ren_ctx **ctx) {
  ren_ctx *c;

  if(ctx) {
    if((c=*ctx)) {
      if(c->oldname)
	free(c->oldname);
      if(c->cwd)
	free(c->cwd);
      free(c);
      *ctx=NULL; }}
  return NULL; }



/**
 * create a rename context
 * @return the new context, or NULL
 * @param old  the old name
 * @param cwd  the current work-directory
 */
ren_ctx *ren_ctx_new(const char *old,const char *cwd) {
  ren_ctx *c=NULL;

  if(old&&cwd&&(c=malloc(sizeof(ren_ctx)))) {
    memset(c,0,sizeof(ren_ctx));
    c->oldname=strdup(old);
    c->cwd=strdup(cwd);
    if(c->oldname&&c->cwd)
      return c; }
  ren_ctx_dst(&c);
  return NULL; }



/*****************************************************************************/



/**
 * release data
 * @param win   the window that was destroyed
 * @param data  the context to release
 */
static void rename_cxl(GtkWindow *win,gpointer data) {
  ren_ctx *rc=(ren_ctx *)data;
  ren_ctx_dst(&rc); }



/**
 * callback: dial was activated, rename file
 * @param ent   the entry-field
 * @param data  the window 'ent' lives in
 */
static void rename_do(GtkWidget *ent,gpointer data) {
  ren_ctx        *rc=(ren_ctx *)data;
  gsize           foo;
  char           *newname;
  const char     *utf8=gtk_entry_get_text(GTK_ENTRY(ent));
  int             ret;

  newname=g_locale_from_utf8(utf8,-1,&foo,&foo,NULL);
  if(newname[0]!='/') {
    char *t=file_compose_path(rc->cwd,newname,NULL);
    free(newname);
    if(!(newname=t))
      return; }
  E(1,"\"%s\" => \"%s\"\n",rc->oldname,newname);
  if(file_is_same(rc->oldname,newname)||
     ((ret=file_mv(rc->oldname,newname))==NOIR_SUCC))
    gtk_widget_destroy(GTK_WIDGET(rc->user1));
  g_free(newname); }



/*****************************************************************************/



/**
 * context-menu action: rename item
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  GtkWidget *win=NULL,
            *ent=NULL;
  char      *utf8,*p;
  gsize      foo;
  ren_ctx   *rc;
  int        ret=NOIR_SUCC;

  if(!ac||!action_context_get_cwd(ac)) {
    form_alert(noir_sess_get_by_serial(serial),"mkdir: No action-context!?");
    return NOIR_FAIL; }

  if(!(rc=ren_ctx_new(filename,action_context_get_cwd(ac)))||
     !(win=gtk_window_new(GTK_WINDOW_TOPLEVEL))||
     !(ent=gtk_entry_new())) {
    ret=NOIR_OOM;
    goto done; }

  rc->user1=win;

  if((p=strrchr(filename,'/')))
    p++;
  else
    p=filename;

  if(!*p) {
    ret=NOIR_IGNORED;
    goto done; }

  utf8=g_locale_to_utf8(p,-1,&foo,&foo,NULL);

  gtk_widget_set_name(win,"rename_window");
  gtk_widget_set_name(ent,"rename_entry");

  gtk_window_set_title(GTK_WINDOW(win),"Rename to...");

  gtk_window_set_default_size(GTK_WINDOW(win),400,10);

  gtk_widget_set_can_default(ent,TRUE);
  gtk_entry_set_activates_default(GTK_ENTRY(ent),TRUE);
  gtk_entry_set_text(GTK_ENTRY(ent),utf8);
  g_free(utf8);

  gtk_container_add(GTK_CONTAINER(win),ent);

  gtk_window_set_default(GTK_WINDOW(win),ent);

  g_signal_connect(G_OBJECT(ent),"activate",G_CALLBACK(rename_do),(gpointer)rc);
  g_signal_connect(G_OBJECT(win),"destroy",G_CALLBACK(rename_cxl),(gpointer)rc);

  gtk_widget_show_all(GTK_WIDGET(win));

  win=ent=NULL;

  return NOIR_SUCC;

done:
  ren_ctx_dst(&rc);
  if(win)
    gtk_widget_destroy(win);
  return ret; }



/*****************************************************************************/
