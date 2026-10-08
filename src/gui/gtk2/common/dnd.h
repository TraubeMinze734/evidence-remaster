#include <gtk/gtk.h>
#include "src/gui/common/session.h"
#include "action.h"



#define DELIMITER          "\n"
#define DELIMITER_         '\n'



typedef enum {
  DRAGDROP_TARGET_STRING,
  DRAGDROP_TARGET_NETSCAPE_URL,
  DRAGDROP_TARGET_URL,
  DRAGDROP_TARGET_TEXT,
  DRAGDROP_TARGET_IMGFILE,
  DRAGDROP_TARGET_PNG,
  DRAGDROP_TARGET_COLOUR,
  DRAGDROP_TARGET_XDS   /* XDS always last */
} DragDropTargetTypes;



extern const GtkTargetEntry target[];
extern const guint targets,targets_xds;



/* DND helpers */
void          file_drop_remember(noir_sess *,GdkDragContext *,action_context *);
void          file_drop_free(GtkWidget *,GdkDragContext *,gpointer);
void          file_drop_delete_from_iconlist(GtkWidget *,GdkDragContext *,gpointer);
void          file_drop_selection_received(GtkWidget *,GtkSelectionData *,guint,gpointer);
int           file_drop(noir_sess *,int,guint info,const char *,const char *);
char         *dnd_get_file_from_drop(char **drop);

int           dnd_init(noir_sess *);
int           dnd_exit(noir_sess *);

gchar *dnd_send_URL(gchar *list,gchar *file);
gchar *dnd_send_string(gchar *list,gchar *file);

gboolean      dnd_drag_drop(GtkWidget *widget,GdkDragContext *dc,guint time,char *where);

#ifdef WITH_XDS
char         *xds_get_prop(GdkDragContext *);
void          xds_set_prop(GdkDragContext *,const char *);
#endif
