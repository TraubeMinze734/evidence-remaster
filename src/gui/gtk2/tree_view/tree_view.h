#ifndef TREE_VIEW_H
#  define TREE_VIEW_H

#  include <gtk/gtk.h>

#  define CLIENT_TREEVIEW "treeview"


/* how many entries can be in a dir for it to still count as empty? */
/* at this time 2, the token entry, and the dot-entry */
#  define DIR_EMPTY 1
#  define DIR_EMPTY_MARKER  "--loading--"


typedef struct _noir_treeview {
  client_funs  *client;
  noir_sess    *session;      /* back pointer */
  GtkWidget    *tree_view,    /* tree-view */
               *tree_scroll;  /* scrollable for tree-view */

  GtkTreeIter  *pi,*ci,*di;   /* parent-iter, child-iter, dot-iter */
  GtkTreePath  *tree_path;    /* tree-path */
  GtkTreeStore *store;        /* store. normally shared between all windows. */
  int           sema_unfold;  /* do not re-focus() in unfold */
  int           realized;     /* GTK_WIDGET_REALIZED() alone not good enough */
} noir_treeview;

GtkTreeStore *tree_model_setup(noir_treeview *);

int  tree_view_init(noir_sess *);
int  tree_view_exit(noir_sess *);

#endif
