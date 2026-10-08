#include <glib-object.h>
#include <gtk/gtkwidget.h>
#include <sys/stat.h>

#include "meta.h"
#include "model.h"

#define NOIR_TYPE_CELL_RENDERER_SIZE	         (noir_cell_renderer_size_get_type())
#define NOIR_CELL_RENDERER_SIZE(obj)	         (GTK_CHECK_CAST((obj),NOIR_TYPE_CELL_RENDERER_SIZE,NoirCellRendererSize))
#define NOIR_CELL_RENDERER_SIZE_CLASS(klass)    (GTK_CHECK_CLASS_CAST((klass),NOIR_TYPE_CELL_RENDERER_SIZE,NoirCellRendererSizeClass))
#define NOIR_IS_CELL_RENDERER_SIZE(obj)	 (GTK_CHECK_TYPE((obj),NOIR_TYPE_CELL_RENDERER_SIZE))
#define NOIR_IS_CELL_RENDERER_SIZE_CLASS(klass) (GTK_CHECK_CLASS_TYPE((obj),NOIR_TYPE_CELL_RENDERER_SIZE))
#define NOIR_CELL_RENDERER_SIZE_GET_CLASS(obj)  (GTK_CHECK_GET_CLASS((obj),NOIR_TYPE_CELL_RENDERER_SIZE,NoirCellRendererSizeClass))

typedef struct _NoirCellRendererSize      NoirCellRendererSize;
typedef struct _NoirCellRendererSizeClass NoirCellRendererSizeClass;

struct _NoirCellRendererSize
{
  GtkCellRendererText  parent;
  posixmd             *st;
  gchar               *text;
};

struct _NoirCellRendererSizeClass
{
  GtkCellRendererTextClass parent_class;
};

GType            noir_cell_renderer_size_get_type(void);
GtkCellRenderer *noir_cell_renderer_size_new(void);
