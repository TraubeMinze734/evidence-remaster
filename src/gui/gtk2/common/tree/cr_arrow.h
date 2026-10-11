#include <glib-object.h>
#include <gtk/gtk.h>

#define NOIR_TYPE_CELL_RENDERER_ARROW	         (noir_cell_renderer_arrow_get_type())
#define NOIR_CELL_RENDERER_ARROW(obj)	         ((G_TYPE_CHECK_INSTANCE_CAST((obj),NOIR_TYPE_CELL_RENDERER_ARROW,NoirCellRendererArrow)))
#define NOIR_CELL_RENDERER_ARROW_CLASS(klass)    ((G_TYPE_CHECK_CLASS_CAST((klass),NOIR_TYPE_CELL_RENDERER_ARROW,NoirCellRendererArrowClass)))
#define NOIR_IS_CELL_RENDERER_ARROW(obj)	 ((G_TYPE_CHECK_INSTANCE_TYPE((obj),NOIR_TYPE_CELL_RENDERER_ARROW)))
#define NOIR_IS_CELL_RENDERER_ARROW_CLASS(klass) ((G_TYPE_CHECK_CLASS_TYPE((klass),NOIR_TYPE_CELL_RENDERER_ARROW)))
#define NOIR_CELL_RENDERER_ARROW_GET_CLASS(obj)  ((G_TYPE_INSTANCE_GET_CLASS((obj),NOIR_TYPE_CELL_RENDERER_ARROW,NoirCellRendererArrowClass)))

typedef struct _NoirCellRendererArrow      NoirCellRendererArrow;
typedef struct _NoirCellRendererArrowClass NoirCellRendererArrowClass;

struct _NoirCellRendererArrow
{
  GtkCellRendererText parent;
  int dirstat;
};

struct _NoirCellRendererArrowClass
{
  GtkCellRendererTextClass parent_class;
};

GType            noir_cell_renderer_arrow_get_type(void);
GtkCellRenderer *noir_cell_renderer_arrow_new(void);
