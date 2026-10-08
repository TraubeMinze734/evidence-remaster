#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

GdkPixbuf *pixbuf_get_by_path_no_resize(const char *);
GdkPixbuf *pixbuf_get_by_path_and_resize(const char *,int,int);

GtkWidget *icon_get_by_path_no_resize(const char *);
GtkWidget *icon_get_by_path(const char *,const char *);
GtkWidget *icon_get_by_file(const char *,const char *,const char *);

int        icon_exit();
int        icon_init();
