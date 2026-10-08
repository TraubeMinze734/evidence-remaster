#ifndef TREE_H
#  define TREE_H
#  include <gtk/gtk.h>

enum {
  FILE_NAME,
  FILE_FILE,
  FILE_MODE,
  FILE_STAT,
  FILE_INFO,
  FILE_MIME,
  N_COLUMNS };



enum {
  SORT_DEFAULT,
  SORT_BY_NAME,
  SORT_BY_TYPE,

  N_SORT_TYPES };



void tree_send_URL(GtkTreeModel *,GtkTreePath *,GtkTreeIter *,gpointer);
void tree_send_string(GtkTreeModel *,GtkTreePath *,GtkTreeIter *,gpointer);
gint tree_sort(GtkTreeModel *,GtkTreeIter *,GtkTreeIter *,gpointer);
#endif

