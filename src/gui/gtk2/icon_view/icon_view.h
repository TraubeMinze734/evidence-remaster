#ifndef ICON_VIEW_H
#  define ICON_VIEW_H

#  include <gtk/gtk.h>
#  include "evidence.h"

#  define CLIENT_ICONVIEW "iconview"

typedef struct _noir_iconview {
  client_funs     *client;
  long             serial;
  noir_sess       *session;
  GtkWidget       *icon_view,   /* icon-view */
                  *icon_scroll; /* GTK: scrollable for icon-view , Evas: eq icon_view */
  GtkAdjustment   *icon_adj;    /* scroll info for icon-view if gnome-canvas is used */
  unsigned long    next_update;
  pthread_mutex_t  viewlock;    /* view mutex */
} noir_iconview;

#define    ICON_LIST_GET_FROM_WIDGET(w)  ((icon_list *)(w?g_object_get_data(G_OBJECT(w),"user_data"):NULL))

#if 0
#  ifdef HAVE_GNOMECANVAS
#    include "gnome-icon-list.h"
void      icon_view_selected(GnomeIconList *,gint,GdkEvent *,gpointer);
gboolean  icon_view_make_tooltip(GtkWidget *,GdkEventMotion *,gpointer);
#  endif
#endif

#if GTK_CHECK_VERSION(3,0,0)
gboolean  icon_view_exposed(GtkWidget *,cairo_t *,gpointer);
#else
gboolean  icon_view_exposed(GtkWidget *,GdkEventExpose *,gpointer);
#endif

void      icon_view_connect(noir_sess *);

int       icon_view_set_selection(void *);

int       icon_view_init(noir_sess *);
int       icon_view_exit(noir_sess *);

#endif
