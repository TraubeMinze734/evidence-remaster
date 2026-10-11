/** @file  cr_arrow.c
 *  @brief cell-renderer that displays a right-arrow after folders in browser-view.
 */

#include <gtk/gtk.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "cr_arrow.h"



static GtkCellRendererTextClass *parent_class;



#define PROP_STAT 1000



/**
 * draw a text-cell containing a file-name.
 * add a cutesy little arrow to the right if it's a directory.
 * used in browser-view.
 * @param cell - cell-renderer
 * @param window - the GDK-window we're drawing in
 * @param widget - the GTK-widget we're drawing in
 * @param background_area - rectangle describing whole cell ("gross=net+padding")
 * @param cell_area - rectangle describing cell-contents ("net")
 * @param expose_area - rectangle describing what needs a redraw
 * @param flags - flags
 */
static void cr_arrow_render(GtkCellRenderer *cell,GdkWindow *window,
                            GtkWidget *widget,GdkRectangle *background_area,
                            GdkRectangle *cell_area,GdkRectangle *expose_area,
                            guint flags) {

  gint wr,wn,as=(cell_area->height-cell->ypad-cell->ypad);

  /* widget->window is *not* window! */
  gdk_drawable_get_size(gtk_widget_get_window(widget),&wr,NULL);

  wn=wr-as;
  if(S_ISDIR(NOIR_CELL_RENDERER_ARROW(cell)->dirstat)) {
    gtk_paint_arrow(gtk_widget_get_style(widget),window,GTK_STATE_NORMAL,GTK_SHADOW_NONE,
                    cell_area,widget,"cellrendererarrow",GTK_ARROW_RIGHT,FALSE,
                    cell_area->x+wn,
                    cell_area->y+cell->ypad,
                    as,  /* yes, height : ) */
                    as);
    cell_area->width=wn+4; }
  GTK_CELL_RENDERER_CLASS(parent_class)->render(cell,window,widget,background_area,cell_area,expose_area,flags); }



static void cr_arrow_init(NoirCellRendererArrow *arrow) { }



static void cr_arrow_get_property(GObject *object,guint param_id,GValue *value,GParamSpec *pspec) {
  NoirCellRendererArrow *arrow=NOIR_CELL_RENDERER_ARROW(object);

  if(param_id==PROP_STAT)
    g_value_set_int(value,arrow->dirstat);
  else
   G_OBJECT_CLASS(parent_class)->get_property(object,param_id,value,pspec); }



static void cr_arrow_set_property(GObject *object,guint param_id,const GValue *value,GParamSpec *pspec) {
  NoirCellRendererArrow *arrow=NOIR_CELL_RENDERER_ARROW(object);

  if(param_id==PROP_STAT)
    arrow->dirstat=g_value_get_int(value);
  else
   G_OBJECT_CLASS(parent_class)->set_property(object,param_id,value,pspec); }



static void cr_arrow_class_init(NoirCellRendererArrowClass *class) {
  GtkCellRendererClass *cell_class  =GTK_CELL_RENDERER_CLASS(class);
  GObjectClass         *object_class=G_OBJECT_CLASS(class);

  cell_class->render=cr_arrow_render;

  object_class->get_property=cr_arrow_get_property;
  object_class->set_property=cr_arrow_set_property;

  g_object_class_install_property(object_class,PROP_STAT,
                                  g_param_spec_int("mode",
                                                   "Mode",
                                                   "The file's mode information",
                                                   0,
                                                   G_MAXINT,
                                                   0,
                                                   G_PARAM_READABLE | G_PARAM_WRITABLE));

  parent_class=GTK_CELL_RENDERER_TEXT_CLASS(g_type_class_peek_parent(class)); }



GType noir_cell_renderer_arrow_get_type(void) {
  static GType cell_text_type=0;

  if(!cell_text_type) {
    static const GTypeInfo cell_text_info={
      sizeof(NoirCellRendererArrowClass),NULL,NULL,
      (GClassInitFunc) cr_arrow_class_init,NULL,NULL,
      sizeof(NoirCellRendererArrow),
      0,(GInstanceInitFunc) cr_arrow_init };

    cell_text_type=g_type_register_static(GTK_TYPE_CELL_RENDERER_TEXT,"NoirCellRendererArrow",&cell_text_info,0); }

  return cell_text_type; }



GtkCellRenderer *noir_cell_renderer_arrow_new(void) {
  GObject *cell=g_object_new(NOIR_TYPE_CELL_RENDERER_ARROW,NULL);
  return GTK_CELL_RENDERER(cell); }
