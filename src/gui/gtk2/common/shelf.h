#include <gtk/gtk.h>

#include "evidence.h"

#define CLIENT_SHELF "shelf"

typedef struct _noir_shelf {
  GtkWidget *shelf_view,      /* the shelf */
            *shelf_scroll;    /* GTK-mode: scrollable for shelf. Evas: eq shelf_view */
} noir_shelf;

int shelf_init(noir_sess *);
int shelf_exit(noir_sess *);
