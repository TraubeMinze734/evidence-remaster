#include <gtk/gtk.h>
#include "evidence.h"

typedef enum {
  START_IN_TREEVIEW=0,
  START_IN_BROWSER,
  START_IN_ICONVIEW,
  START_IN_CREDITS } noir_viewtype;

int        notebook_go2view(noir_sess *,int);

GtkWidget *notebook_setup(noir_sess *,GtkWidget *);
