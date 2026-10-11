/** @file  cr_basic.c
 *  @brief common routines for cell-renderers in cr_*.c */

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
  PangoRectangle        rect;
  PangoLayout          *layout;
  gint                  xpad,ypad;
  gchar                *text=NULL;
  PangoFontDescription *font_desc=NULL;
  gfloat                font_scale=1.0;
  gboolean              scale_set=FALSE;
  gint                  fixed_height_rows=-1;
  gboolean              calc_fixed_height=FALSE;
  gint                  fw,fh;

  /* GtkCellRendererText is opaque in GTK3; read the properties instead
     of the struct members the 2005 code used. g_object_get covers both
     build paths. */
  g_object_get(celltext,
               "text",&text,
               "font-desc",&font_desc,
               "font-scale",&font_scale,
               "scale-set",&scale_set,
               "fixed-height-rows",&fixed_height_rows,
#if !GTK_CHECK_VERSION(3,0,0)
               "calc-fixed-height",&calc_fixed_height,
#endif
               NULL);

  gtk_cell_renderer_get_padding(cell,&xpad,&ypad);
  gtk_cell_renderer_get_fixed_size(cell,&fw,&fh);

  if(calc_fixed_height) {
    PangoContext     *context;
    PangoFontMetrics *metrics;
    gint             row_height;

    if(font_desc) {
      PangoFontDescription *fd=pango_font_description_copy(gtk_widget_get_style(widget)->font_desc);
      pango_font_description_merge(fd,font_desc,TRUE);
      if(scale_set)
        pango_font_description_set_size(fd,font_scale*pango_font_description_get_size(fd));
      context=gtk_widget_get_pango_context(widget);
      metrics=pango_context_get_metrics(context,fd,pango_context_get_language(context));
      pango_font_description_free(fd); }
    else {
      context=gtk_widget_get_pango_context(widget);
      metrics=pango_context_get_metrics(context,gtk_widget_get_style(widget)->font_desc,pango_context_get_language(context)); }

    row_height=(pango_font_metrics_get_ascent(metrics)+
                pango_font_metrics_get_descent(metrics));
    pango_font_metrics_unref(metrics);

    gtk_cell_renderer_set_fixed_size(cell,fw,2*ypad+
                                     fixed_height_rows*
                                     PANGO_PIXELS(row_height));

    if(height) {
      gtk_cell_renderer_get_fixed_size(cell,&fw,&fh);
      *height=fh;
      height=NULL; }

#if !GTK_CHECK_VERSION(3,0,0)
    g_object_set(celltext,"calc-fixed-height",FALSE,NULL);
#endif
  }

  layout=gtk_widget_create_pango_layout(widget,text?text:"");
  pango_layout_get_pixel_extents(layout,NULL,&rect);

  if(width)
    *width=xpad*2+rect.width;
  if(height)
    *height=ypad*2+rect.height;

  if(cell_area) {
    gfloat xalign,yalign;
    gtk_cell_renderer_get_alignment(cell,&xalign,&yalign);
    if(x_offset) {
      *x_offset=xalign*(cell_area->width-rect.width-(2*xpad));
      *x_offset=MAX(*x_offset,0); }
    if(y_offset) {
      *y_offset=yalign*(cell_area->height-rect.height-(2*ypad));
      *y_offset=MAX(*y_offset,0); } }

  if(text)
    g_free(text);
  if(font_desc)
    pango_font_description_free(font_desc);

  return layout; }
