/** @file  cr_basic.c
 *  @brief common routines for cell-renderers in cr_*.c
 */

#include <gtk/gtk.h>



/**
 * get extents of a string.
 * used in cr_date_render() and cr_size_render().
 */
PangoLayout *cr_text_get_size(GtkCellRenderer *cell,
                              GtkWidget       *widget,
                              GdkRectangle    *cell_area,
                              gint            *x_offset,
                              gint            *y_offset,
                              gint            *width,
                              gint            *height) {

  GtkCellRendererText *celltext=(GtkCellRendererText *)cell;
  PangoRectangle       rect;
  PangoLayout         *layout;

  if(celltext->calc_fixed_height) {
      PangoContext         *context;
      PangoFontMetrics     *metrics;
      PangoFontDescription *font_desc;
      gint                  row_height;

      font_desc=pango_font_description_copy(gtk_widget_get_style(widget)->font_desc);
      pango_font_description_merge(font_desc,celltext->font,TRUE);

      if(celltext->scale_set)
	pango_font_description_set_size(font_desc,celltext->font_scale*pango_font_description_get_size(font_desc));

      context=gtk_widget_get_pango_context(widget);

      metrics=pango_context_get_metrics(context,font_desc,pango_context_get_language(context));
      row_height=(pango_font_metrics_get_ascent(metrics)+
                  pango_font_metrics_get_descent(metrics));
      pango_font_metrics_unref(metrics);
      
      { gint xpad,ypad;
        gtk_cell_renderer_get_padding(cell,&xpad,&ypad);
      { gint fw,fh;
        gtk_cell_renderer_get_fixed_size(cell,&fw,&fh);
        gtk_cell_renderer_set_fixed_size(cell,fw,2*ypad+
                                       celltext->fixed_height_rows*
                                       PANGO_PIXELS(row_height)); } }

      if(height) {
        gint fw,fh;
        gtk_cell_renderer_get_fixed_size(cell,&fw,&fh);
        *height=fh;
        height=NULL; }
      celltext->calc_fixed_height=FALSE; }

  layout=gtk_widget_create_pango_layout(widget,celltext->text);
  pango_layout_get_pixel_extents(layout,NULL,&rect);

  { gint xpad,ypad;
    gtk_cell_renderer_get_padding(GTK_CELL_RENDERER(celltext),&xpad,&ypad);
    if(width)
      *width=xpad*2+rect.width;
    if(height)
      *height=ypad*2+rect.height; }

  if(cell_area) {
    gfloat xalign,yalign;
    gtk_cell_renderer_get_alignment(cell,&xalign,&yalign);
    if(x_offset) {
      *x_offset=xalign*(cell_area->width-rect.width-(2*xpad));
      *x_offset=MAX(*x_offset,0); }
    if(y_offset) {
      *y_offset=yalign*(cell_area->height-rect.height-(2*ypad));
      *y_offset=MAX(*y_offset,0); }}

  return layout; }
