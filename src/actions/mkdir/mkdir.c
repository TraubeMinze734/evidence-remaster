/** @file  mkdir.c
 *  @brief context-menu action "make directory"
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "evidence.h"
#include "gui/common/alert.h"
#include "file.h"
#include "meta.h"
#include "util.h"
#include "file_lcl.h"



/******************************************************************************/



/**
 * release data
 * @param win   the window that was destroyed
 * @param data  the buffer to release
 */
static void mkdir_cxl(GtkWindow *win,gpointer data) {
  free(data); }

/**
 * callback: dial was activated, create dir
 * @param ent   the entry-field
 * @param data  the window 'ent' lives in
 */
static void mkdir_do(GtkWidget *ent,gpointer data) {
  char           *where=(char *)g_object_get_data(G_OBJECT(ent),"data");
  GtkWindow      *win=(GtkWindow *)data;
  gsize           foo;
  char           *folder;
  const char     *utf8=gtk_entry_get_text(GTK_ENTRY(ent));
  int             ret;

  folder=g_locale_from_utf8(utf8,-1,&foo,&foo,NULL);
  if(!file_if_absolute(folder)) {
    char *t=file_compose_path(where,folder,NULL);
    E(3,"entered: |%s|   cwd: |%s|   make: |%s|\n",folder,where,t?t:"???");
    free(folder);
    if(!(folder=t))
      return; }
  E(1,"new folder: %s\n",folder);
  if(folder&&*folder&&((ret=file_mkdir(folder,0755,TRUE))!=NOIR_SUCC))
    form_alert(NULL,"Could not create folder...\n%s",folder);
  else
    gtk_widget_destroy(GTK_WIDGET(win));
  g_free(folder); }



/******************************************************************************/



/**
 * context-menu action: make a new directory
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  GtkWidget *win=gtk_window_new(GTK_WINDOW_TOPLEVEL),
            *ent=gtk_entry_new();
  char      *fn=strdup(filename);

  if(!fn||!win||!ent) {
    E(0,"out of memory\n");
    return NOIR_OOM; }

  gtk_widget_set_name(win,"mkdir_window");
  gtk_widget_set_name(ent,"mkdir_entry");
  gtk_window_set_title(GTK_WINDOW(win),"New folder...");
  GTK_WIDGET_SET_FLAGS(ent,GTK_CAN_DEFAULT);
  gtk_entry_set_activates_default(GTK_ENTRY(ent),TRUE);
  gtk_container_add(GTK_CONTAINER(win),ent);
  gtk_window_set_default(GTK_WINDOW(win),ent);
  g_object_set_data(G_OBJECT(ent),"data",(gpointer)fn);
  g_signal_connect(GTK_OBJECT(ent),"activate",G_CALLBACK(mkdir_do),(gpointer)win);
  g_signal_connect(GTK_OBJECT(win),"destroy",G_CALLBACK(mkdir_cxl),(gpointer)fn);
  gtk_widget_show_all(GTK_WIDGET(win));

  return NOIR_SUCC; }



/******************************************************************************/
