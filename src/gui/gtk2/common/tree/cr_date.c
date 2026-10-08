/** @file  cr_date.c
 *  @brief cell-renderer that shows a date in tree-view.
 */

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <stdlib.h>
#include <time.h>

#include "cr_basic.h"
#include "cr_date.h"



static GtkCellRendererTextClass *parent_class;



#define PROP_STAT      1001

#define TREE_TIME_MAX    64



/**
 * draw a text-cell containing a date.
 * @param cell - cell-renderer
 * @param window - the GDK-window we're drawing in
 * @param widget - the GTK-widget we're drawing in
 * @param background_area - rectangle describing whole cell ("gross=net+padding")
 * @param cell_area - rectangle describing cell-contents ("net")
 * @param expose_area - rectangle describing what needs a redraw
 * @param flags - flags
 */
static void cr_date_render(GtkCellRenderer *cell,GdkWindow *window,
                            GtkWidget *widget,GdkRectangle *background_area,
                            GdkRectangle *cell_area,GdkRectangle *expose_area,
                            guint flags) {
  struct tm            mktm;
  static char          time[TREE_TIME_MAX];
  posixmd             *st=NOIR_CELL_RENDERER_DATE(cell)->st;
  GtkCellRendererText *celltext=(GtkCellRendererText *)cell;
  PangoLayout         *layout;
  gint                 x,y;
  time_t               mtime=posixmd_get_mtime(st);

  if(!st)
    return;

  localtime_r(&mtime,&mktm);
  strftime(time,TREE_TIME_MAX,"%Y-%m-%d %H:%M",&mktm);

  celltext->text=time;
  layout=cr_text_get_size(cell,widget,cell_area,&x,&y,NULL,NULL);

  gtk_paint_layout(widget->style,window,GTK_STATE_NORMAL,TRUE,cell_area,
                   widget,"date",
                   cell_area->x+x+cell->xpad,
                   cell_area->y+y+cell->ypad,
                   layout);

  g_object_unref(layout); }



static void cr_date_init(NoirCellRendererDate *date) { }



static void cr_date_get_property(GObject *object,guint param_id,GValue *value,GParamSpec *pspec) {
  NoirCellRendererDate *date=NOIR_CELL_RENDERER_DATE(object);

  if(param_id==PROP_STAT)
    g_value_set_pointer(value,date->st);
  else
   G_OBJECT_CLASS(parent_class)->get_property(object,param_id,value,pspec); }



static void cr_date_set_property(GObject *object,guint param_id,const GValue *value,GParamSpec *pspec) {
  NoirCellRendererDate *date=NOIR_CELL_RENDERER_DATE(object);

  if(param_id==PROP_STAT)
    date->st=g_value_get_pointer(value);
  else
   G_OBJECT_CLASS(parent_class)->set_property(object,param_id,value,pspec); }



static void cr_date_finalize (GObject *object) {
  GtkCellRendererText *celltext=GTK_CELL_RENDERER_TEXT(object);
  pango_font_description_free(celltext->font); }



static void cr_date_class_init(NoirCellRendererDateClass *class) {
  GtkCellRendererClass *cell_class  =GTK_CELL_RENDERER_CLASS(class);
  GObjectClass         *object_class=G_OBJECT_CLASS(class);

  cell_class->render=cr_date_render;

  object_class->get_property=cr_date_get_property;
  object_class->set_property=cr_date_set_property;
  object_class->finalize=cr_date_finalize;

  g_object_class_install_property(object_class,PROP_STAT,
                                  g_param_spec_pointer("stat",
                                                       "Stat",
                                                       "The file's stat-info",
                                                       G_PARAM_READABLE | G_PARAM_WRITABLE));

  parent_class=GTK_CELL_RENDERER_TEXT_CLASS(g_type_class_peek_parent(class)); }



GType noir_cell_renderer_date_get_type(void) {
  static GType cell_text_type=0;

  if(!cell_text_type) {
    static const GTypeInfo cell_text_info={
      sizeof(NoirCellRendererDateClass),NULL,NULL,
      (GClassInitFunc) cr_date_class_init,NULL,NULL,
      sizeof(NoirCellRendererDate),
      0,(GInstanceInitFunc) cr_date_init };

    cell_text_type=g_type_register_static(GTK_TYPE_CELL_RENDERER_TEXT,"NoirCellRendererDate",&cell_text_info,0); }

  return cell_text_type; }



GtkCellRenderer *noir_cell_renderer_date_new(void) {
  GObject *cell=g_object_new(NOIR_TYPE_CELL_RENDERER_DATE,NULL);
  return GTK_CELL_RENDERER(cell); }
