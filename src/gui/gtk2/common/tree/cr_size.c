/** @file  cr_size.c
 *  @brief cell-renderer to show a file's size in tree-view.
 */

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <stdlib.h>
#include <stdio.h>

#include "cr_basic.h"
#include "cr_size.h"
#include "util.h"



static GtkCellRendererTextClass *parent_class;



#define PROP_STAT      1001

#define TREE_SIZE_MAX    64



/**
 * draw a text-cell containing a file-size, right-aligned.
 * @param cell - cell-renderer
 * @param window - the GDK-window we're drawing in
 * @param widget - the GTK-widget we're drawing in
 * @param background_area - rectangle describing whole cell ("gross=net+padding")
 * @param cell_area - rectangle describing cell-contents ("net")
 * @param expose_area - rectangle describing what needs a redraw
 * @param flags - flags
 */
#if GTK_CHECK_VERSION(3,0,0)
static void cr_size_render(GtkCellRenderer *cell,cairo_t *cr,
                            GtkWidget *widget,GdkRectangle *background_area,
                            GdkRectangle *cell_area,guint flags) {
#else
static void cr_size_render(GtkCellRenderer *cell,GdkWindow *window,
                            GtkWidget *widget,GdkRectangle *background_area,
                            GdkRectangle *cell_area,GdkRectangle *expose_area,
                            guint flags) {
#endif
  static char          buf[TREE_SIZE_MAX];
  posixmd             *st=NOIR_CELL_RENDERER_SIZE(cell)->st;
  GtkCellRendererText *celltext=(GtkCellRendererText *)cell;
  PangoLayout         *layout;
  gint                 x,y,w,h,ret;

  if(!st)
    return;

  ret=snprintf(buf,TREE_SIZE_MAX,"%ld",(long)posixmd_get_size(st));
  if((ret<0)||(ret>TREE_SIZE_MAX))
    return;

#if 0
  util_size(buf);
#endif

  celltext->text=buf;
  layout=cr_text_get_size(cell,widget,cell_area,&x,&y,&w,&h);

#if GTK_CHECK_VERSION(3,0,0)
  gtk_render_layout(gtk_widget_get_style_context(widget),cr,
                   cell_area->x+cell_area->width-w+cell->xpad,
                   cell_area->y+y+cell->ypad,
                   layout);
#else
  gtk_paint_layout(gtk_widget_get_style(widget),window,GTK_STATE_NORMAL,TRUE,cell_area,
                   widget,"size",
                   cell_area->x+cell_area->width-w+cell->xpad,
                   cell_area->y+y+cell->ypad,
                   layout);
#endif

  g_object_unref(layout); }



static void cr_size_init(NoirCellRendererSize *size) { }



static void cr_size_get_property(GObject *object,guint param_id,GValue *value,GParamSpec *pspec) {
  NoirCellRendererSize *size=NOIR_CELL_RENDERER_SIZE(object);

  if(param_id==PROP_STAT)
    g_value_set_pointer(value,size->st);
  else
   G_OBJECT_CLASS(parent_class)->get_property(object,param_id,value,pspec); }



static void cr_size_set_property(GObject *object,guint param_id,const GValue *value,GParamSpec *pspec) {
  NoirCellRendererSize *size=NOIR_CELL_RENDERER_SIZE(object);

  if(param_id==PROP_STAT)
    size->st=g_value_get_pointer(value);
  else
   G_OBJECT_CLASS(parent_class)->set_property(object,param_id,value,pspec); }



static void cr_size_finalize (GObject *object) {
  GtkCellRendererText *celltext=GTK_CELL_RENDERER_TEXT(object);
  pango_font_description_free(celltext->font); }



static void cr_size_class_init(NoirCellRendererSizeClass *class) {
  GtkCellRendererClass *cell_class  =GTK_CELL_RENDERER_CLASS(class);
  GObjectClass         *object_class=G_OBJECT_CLASS(class);

  cell_class->render=cr_size_render;

  object_class->get_property=cr_size_get_property;
  object_class->set_property=cr_size_set_property;
  object_class->finalize=cr_size_finalize;

  g_object_class_install_property(object_class,PROP_STAT,
                                  g_param_spec_pointer("stat",
                                                       "Stat",
                                                       "The file's stat-info",
                                                       G_PARAM_READABLE | G_PARAM_WRITABLE));

  parent_class=GTK_CELL_RENDERER_TEXT_CLASS(g_type_class_peek_parent(class)); }



GType noir_cell_renderer_size_get_type(void) {
  static GType cell_text_type=0;

  if(!cell_text_type) {
    static const GTypeInfo cell_text_info={
      sizeof(NoirCellRendererSizeClass),NULL,NULL,
      (GClassInitFunc)cr_size_class_init,NULL,NULL,
      sizeof(NoirCellRendererSize),
      0,(GInstanceInitFunc)cr_size_init};

    cell_text_type=g_type_register_static(GTK_TYPE_CELL_RENDERER_TEXT,"NoirCellRendererSize",&cell_text_info,0); }

  return cell_text_type; }



GtkCellRenderer *noir_cell_renderer_size_new(void) {
  GObject *cell=g_object_new(NOIR_TYPE_CELL_RENDERER_SIZE,NULL);
  return GTK_CELL_RENDERER(cell); }
