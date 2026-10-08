#ifndef SESSION_GTK2
#  define SESSION_GTK2
#  include <gtk/gtk.h>

#  include "gui/common/session.h"

GdkDragContext *noir_sess_get_dragcontext(noir_sess *ns);
int             noir_sess_set_dragcontext(noir_sess *ns,GdkDragContext *d);
int             noir_sess_set_dragicon(noir_sess *ns,GdkPixbuf *di);

GtkWidget      *noir_sess_get_window(noir_sess *ns);
int             noir_sess_set_window(noir_sess *ns,GtkWidget *w);

GtkWidget      *noir_sess_get_notebook(noir_sess *ns);
int             noir_sess_set_notebook(noir_sess *ns,GtkWidget *w);

GtkWidget      *noir_sess_get_about(noir_sess *ns);
int             noir_sess_set_about(noir_sess *ns,GtkWidget *w);

#endif
