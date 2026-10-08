#include <gtk/gtk.h>
#include "evidence.h"

#define CLIENT_BROWSERVIEW "browser"

#define BROWSER_COLUMN_WIDTH 128

typedef struct _noir_column {
  noir_sess    *session;
  int           no;
  GtkListStore *store;
  GtkWidget    *view,*icon;
  char         *title;
  char         *path;
  struct _noir_column *next; } noir_column;

typedef struct _noir_browser {
  client_funs *client;
  noir_sess   *session;
  GtkWidget   *browser_view,      /* the sum of the columns */ 
              *browser_box,       /* the sum of the columns and padding */ 
              *browser_padding,   /* padding on the right */
              *browser_scroll,    /* scrollable for browser_paned */
              *browser_paned,     /* container for browser_view and icon_path */
              *icon_path;         /* icons and padding */
  struct _noir_column *columns;   /* list of columns for browser-view */
} noir_browser;



int          browser_view_set_selection(void *);

int          browser_view_init(noir_sess *);
int          browser_view_exit(noir_sess *);
