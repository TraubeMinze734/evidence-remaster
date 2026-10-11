/** @file  session_gtk2.c
 *  @brief getters/setters for the parts of the session specific to the GTK2 UI
 */



#include "config.h"

#include <gtk/gtk.h>

#include <string.h>           /* memset() */
#include <stdlib.h>           /* free() */
#include <stdio.h>            /* stderr */

#include "model.h"
#include "debug.h"
#include "errors.h"
#include "session_gtk2.h"



typedef struct _noir_sess_gtk2 {

  GtkWidget      *window;          /**< top window */

  GtkWidget      *notebook;        /**< the thing with the tabs */
  GtkWidget      *about;           /**< about "view" */

  GdkPixbuf      *drag_icon;       /**< DND-icon */

  GdkDragContext *drag_context;    /**< drag-context */

} noir_sess_gtk2;




int noir_sess_uidata_new(noir_sess *ns) {
  if(ns) {
    noir_sess_gtk2 *sg=malloc(sizeof(noir_sess_gtk2));
    if(!sg)
      return NOIR_OOM;
    memset(sg,0,sizeof(noir_sess_gtk2));
    noir_sess_set_uidata(ns,sg);
    return NOIR_SUCC; }
  else
    return NOIR_FAIL; }



int noir_sess_uidata_free(noir_sess *ns) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NOIR_NODATA;

  if(sg->window)
    gtk_widget_destroy(sg->window);

  /* how do you free these, anyway?  there's no dedicated function for it? */
  if(sg->drag_context)
    g_object_unref(G_OBJECT(sg->drag_context));

  if(sg->drag_icon)
    g_object_unref(sg->drag_icon);

  memset(sg,0,sizeof(noir_sess_gtk2));

  free(sg);

  return NOIR_SUCC; }



/**
 * raise window belonging to this session
 * @param ns  the session the window of which to raise
 */
int noir_sess_present(noir_sess *ns) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NOIR_NODATA;

  if(sg->window) {
    gtk_window_present(GTK_WINDOW(sg->window));
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * set window-title for this session
 * @param ns    the session
 * @param utf8  the new title for the window (UTF-8 string)
 */
int noir_sess_set_window_title(noir_sess *ns,char *utf8) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NOIR_NODATA;

  if(sg->window) {
    gtk_window_set_title(GTK_WINDOW(sg->window),utf8);
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * redraw window belonging to this session
 * @param ns  the session the window of which to raise
 */
int noir_sess_redraw_window(noir_sess *ns) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NOIR_NODATA;

  if(sg->window) {
    GdkRectangle r;

    E(3,"redraw %ld (%s)\n",noir_sess_get_serial(ns),noir_sess_get_cwd(ns));

    r.x=r.y=0;
    gdk_drawable_get_size(gtk_widget_get_window(sg->window),&r.width,&r.height);
    gdk_window_invalidate_rect(gtk_widget_get_window(sg->window),&r,TRUE);
/*   gdk_window_process_updates(ns->window->window,TRUE); */
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * redraw window belonging to this session
 * @param ns  the session the window of which to raise
 */
int noir_sess_redraw_files(noir_sess *ns) {
  if(ns&&noir_sess_get_window(ns)) {
    file_info *cd=file_info_fetch(NULL,noir_sess_get_cwd(ns));
    if(cd) {
      noir_sess_tell_clients(ns,cd,MODEL_CWD,FF_NONE);   /* signal pre-cwd */
      noir_sess_tell_clients(ns,cd,MODEL_CWD,FF_NAME); } /* signal post-cwd */
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * destroy a session by destroying its window. use this to close a session.
 * @param ns  the session to destroy
 */
noir_sess *noir_sess_close(noir_sess *ns) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NULL;

  if(sg->window) {
    E(1,"destroying remaining session #%ld  (%s)\n",noir_sess_get_serial(ns),noir_sess_get_cwd(ns));
    gtk_widget_destroy(sg->window);
    sg->window=NULL; }

  return NULL; }



GdkDragContext *noir_sess_get_dragcontext(noir_sess *ns) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NULL;

  return sg->drag_context; }



int noir_sess_set_dragcontext(noir_sess *ns,GdkDragContext *d) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NOIR_NODATA;

  sg->drag_context=d;
  return NOIR_SUCC; }



int noir_sess_set_dragicon(noir_sess *ns,GdkPixbuf *di) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NOIR_NODATA;

  if(sg->drag_icon) {
    g_object_unref(sg->drag_icon);
    sg->drag_icon=NULL; }

  sg->drag_icon=di;
  return NOIR_SUCC; }



GtkWidget *noir_sess_get_window(noir_sess *ns) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NULL;

  return GTK_WIDGET(sg->window); }



int noir_sess_set_window(noir_sess *ns,GtkWidget *w) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NOIR_FAIL;
  sg->window=w;
  return NOIR_SUCC; }



GtkWidget *noir_sess_get_notebook(noir_sess *ns) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NULL;

  return GTK_WIDGET(sg->notebook); }



int noir_sess_set_notebook(noir_sess *ns,GtkWidget *w) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NOIR_FAIL;
  sg->notebook=w;
  return NOIR_SUCC; }



GtkWidget *noir_sess_get_about(noir_sess *ns) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NULL;

  return GTK_WIDGET(sg->about); }



int noir_sess_set_about(noir_sess *ns,GtkWidget *w) {
  noir_sess_gtk2 *sg;

  if(!ns||!(sg=noir_sess_get_uidata(ns)))
    return NOIR_FAIL;
  sg->about=w;
  return NOIR_SUCC; }
