#include <glib-object.h>
#include <gtk/gtkwidget.h>
#include <sys/stat.h>

#include "meta.h"
#include "model.h"

#define NOIR_TYPE_CELL_RENDERER_DATE	         (noir_cell_renderer_date_get_type())
#define NOIR_CELL_RENDERER_DATE(obj)	         ((G_TYPE_CHECK_INSTANCE_CAST((obj),NOIR_TYPE_CELL_RENDERER_DATE,NoirCellRendererDate)))
#define NOIR_CELL_RENDERER_DATE_CLASS(klass)    ((G_TYPE_CHECK_CLASS_CAST((klass),NOIR_TYPE_CELL_RENDERER_DATE,NoirCellRendererDateClass)))
#define NOIR_IS_CELL_RENDERER_DATE(obj)	 ((G_TYPE_CHECK_INSTANCE_TYPE((obj),NOIR_TYPE_CELL_RENDERER_DATE)))
#define NOIR_IS_CELL_RENDERER_DATE_CLASS(klass) ((G_TYPE_CHECK_CLASS_TYPE((klass),NOIR_TYPE_CELL_RENDERER_DATE)))
#define NOIR_CELL_RENDERER_DATE_GET_CLASS(obj)  ((G_TYPE_INSTANCE_GET_CLASS((obj),NOIR_TYPE_CELL_RENDERER_DATE,NoirCellRendererDateClass)))

typedef struct _NoirCellRendererDate      NoirCellRendererDate;
typedef struct _NoirCellRendererDateClass NoirCellRendererDateClass;

struct _NoirCellRendererDate
{
  GtkCellRendererText  parent;
  posixmd             *st;
  gchar               *text;
};

struct _NoirCellRendererDateClass
{
  GtkCellRendererTextClass parent_class;
};

GType            noir_cell_renderer_date_get_type(void);
GtkCellRenderer *noir_cell_renderer_date_new(void);
