/** @file  notebook.c
 *  @brief implements notebook.
 */

#include <gtk/gtk.h>

#include "gui/common/session.h"
#include "gui/common/session_private.h"

#ifdef WITH_TREEVIEW
#  include "gui/gtk2/tree_view/tree_view.h"
#endif
#ifdef WITH_BROWSERVIEW
#  include "gui/gtk2/browser_view/browser_view.h"
#endif
#ifdef WITH_ICONVIEW
#  include "gui/gtk2/icon_view/icon_view.h"
#endif



/**
 * notebook_setup - create a GTK notebook with pages for the known views.
 * @return a GTK-notebook, or NULL for failure.
 * @param ns - the session/window to create the note-book for.
 * @param about - the "about" page
 */
GtkWidget *notebook_setup(noir_sess *ns,GtkWidget *about) {
  GtkWidget *label;
  GtkWidget *notebook;

/* FIXME ### make generic */
#ifdef WITH_TREEVIEW
  noir_treeview *tv=(noir_treeview *)noir_sess_client_get_data(ns,CLIENT_TREEVIEW);
#endif
#ifdef WITH_ICONVIEW
  noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
#endif
#ifdef WITH_BROWSERVIEW
  noir_browser  *bv=(noir_browser *)noir_sess_client_get_data(ns,CLIENT_BROWSERVIEW);
#endif

  if(!ns)
    return NULL;

  notebook=gtk_notebook_new();

  gtk_widget_set_name(notebook,"notebook");

#ifdef WITH_TREEVIEW
  if(tv&&tv->tree_scroll) {
    label=gtk_label_new_with_mnemonic("_Tree");
    gtk_widget_set_name(label,"tree_view_label");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),tv->tree_scroll,label); }
#endif

#ifdef WITH_BROWSERVIEW
  if(bv&&bv->browser_scroll) {
    label=gtk_label_new_with_mnemonic("_Browser");
    gtk_widget_set_name(label,"browser_view_label");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),bv->browser_scroll,label); }
#endif

#ifdef WITH_ICONVIEW
  if(iv&&iv->icon_scroll) {
    label=gtk_label_new_with_mnemonic("_Icon");
    gtk_widget_set_name(label,"icon_view_label");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),iv->icon_scroll,label); }
#endif

  if(about) {
    label=gtk_label_new_with_mnemonic("_About");
    gtk_widget_set_name(label,"about_label");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),about,label); }

  return notebook; }
