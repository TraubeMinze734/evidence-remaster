/** @file  gevas_light.c
 *  @brief embed an evas-canvas into a GTK2-widget.
 *  used by gevas_bind.c to use evas-based icon-lists in evidence.
 *  originally based on work by Ben Martin.
 */



/***********************************************************************
 *
 * A Gtk2 wrapper for an Evas2.
 *
 * Developed by Ben Martin based on gtkev sample widget by Havoc Pennington
 * from GGAD; stripped down and adapted for evidence by Azundris.
 *
 * Copyright (C) 2000 Ben Martin.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>         /* strdup(), strcmp() */
#include <stdio.h>          /* fprintf(), stderr */
#include <stdlib.h>         /* exit() */

#include "debug.h"          /* E() */
#include "util.h"

#include "gevas_light.h"

#include "gui/efl/icon_view/evas_bind.h"

#include <gtk/gtk.h>
#include <cairo.h>

#include <Evas.h>
/* Modern EFL no longer installs the per-engine headers (Evas_Engine_Software_X11.h)
   and its X11 engines render into their own windows, which cannot be embedded
   into a GdkWindow.  Instead we use the buffer engine, which is still installed
   with Evas (Evas_Engine_Buffer.h) and renders into a memory buffer we hand to
   GDK in the expose handler. */
#include <Evas_Engine_Buffer.h>
#include <stdlib.h>         /* realloc(), free() */



/******************************************************************************/


/******************************************************************************/



static GtkWidgetClass *parent_class=NULL;



/******************************************************************************/



static void gevas_class_init(GtkgEvasClass *);



/* after every evas_render() the backing-store holds a new frame; blit it
   to the GdkWindow on the next GTK expose.  this also covers renders that
   plugins trigger on their own (e.g. the live-video thumbnailer calling
   evas_render() on each decoded frame) -- without the queue_draw those
   frames would stay in the buffer, invisible. */
static void gevas_render_flush(void *data,Evas *e,void *event_info) {
  GtkWidget *widget=GTK_WIDGET(data);
  (void)e; (void)event_info;
  E(2,"gevas_render_flush: widget=%p realized=%d\n",widget,GTK_WIDGET_REALIZED(widget)?1:0);
  if(GTK_WIDGET_REALIZED(widget))
    gtk_widget_queue_draw(widget); }


/* allocate the backing-store and attach it to the buffer engine */
static int gevas_buffer_configure(GtkgEvas *ev,int w,int h) {
  Evas                     *evas=ev->evas;
  Evas_Engine_Info_Buffer  *einfo;

  if(w<1) w=1;
  if(h<1) h=1;

  if((w!=ev->pixels_w)||(h!=ev->pixels_h)) {
    void *pixels=realloc(ev->pixels,(size_t)w*h*sizeof(int));
    if(!pixels)
      return -1;
    ev->pixels=pixels;
    ev->pixels_w=w;
    ev->pixels_h=h; }

  evas_output_size_set(evas,w,h);
  evas_output_viewport_set(evas,0,0,w,h);

  if(!(einfo=(Evas_Engine_Info_Buffer *)evas_engine_info_get(evas)))
    return -1;

  einfo->info.depth_type=EVAS_ENGINE_BUFFER_DEPTH_ARGB32;
  einfo->info.dest_buffer=ev->pixels;
  einfo->info.dest_buffer_row_bytes=w*sizeof(int);
  einfo->info.use_color_key=0;
  einfo->info.alpha_threshold=0;
  einfo->info.color_key_r=0;
  einfo->info.color_key_g=0;
  einfo->info.color_key_b=0;
  einfo->info.func.new_update_region=NULL;
  einfo->info.func.free_update_region=NULL;
  einfo->info.func.switch_buffer=NULL;
  einfo->info.switch_data=NULL;

  if(!evas_engine_info_set(evas,(Evas_Engine_Info *)einfo))
    return -1;

  return 0; }



/******************************************************************************/



static void gevas_init(GtkgEvas *ev) {
  GTK_WIDGET_SET_FLAGS(GTK_WIDGET(ev),GTK_CAN_FOCUS);

  ev->evas          =evas_new();
  ev->size_request_x=100;
  ev->size_request_y=100;
  ev->gevasobjlist  =NULL;
  ev->pixels        =NULL;
  ev->pixels_w=ev->pixels_h=0; }



GType gevas_get_type(void) {
  static GType ev_type=0;

  if(!ev_type) {
    static const GTypeInfo ev_info={
      sizeof(GtkgEvasClass),
      NULL,
      NULL,
      (GClassInitFunc)  gevas_class_init,
      NULL,
      NULL,
      sizeof(GtkgEvas),
      0,
      (GInstanceInitFunc) gevas_init };

    ev_type=g_type_register_static(GTK_TYPE_WIDGET,"GtkgEvas",&ev_info,0); }

  return ev_type; }



/* modern EFL: the widget renders into a backing-store blitted to the
   GdkWindow via cairo, so the buffer engine is the only usable method.
   other engines (software_x11, gl_x11, ...) render into their own X
   windows, which cannot be embedded into a GdkWindow. */
static int gevas_render_method(Evas *e,const char *engine) {
  int m=evas_render_method_lookup("buffer");
  if(engine&&strcmp(engine,"buffer")&&evas_render_method_lookup(engine))
    E(0,"engine \"%s\" requested, but only \"buffer\" can be embedded into a GdkWindow; using buffer...\n",engine);
  return m; }



GtkWidget *gevas_new(char *engine) {
  GtkgEvas *ev;
  char     *dflt_engine="buffer";

  if(!(ev=g_object_new(gevas_get_type(),NULL)))
    return NULL;

  gevas_set_size_request_x(ev,200);
  gevas_set_size_request_y(ev,200);

  if(!engine)
    engine=dflt_engine;
  ev->evas_engine=strdup(dflt_engine);
  evas_output_method_set(ev->evas,gevas_render_method(ev->evas,engine));
  E(3,"render-method %d\n",evas_output_method_get(ev->evas));

  return GTK_WIDGET(ev); }



static gint gevas_event(GtkWidget *widget,GdkEvent *event) {
  GtkgEvas *gevas;
  int       x,y,b,c=EVAS_BUTTON_NONE;
  unsigned int ts=0;

  g_return_val_if_fail(widget!= NULL,FALSE);
  g_return_val_if_fail(GTK_IS_GEVAS(widget),FALSE);

  gevas=GTK_GEVAS(widget);

  gevas->current_event=event;

  switch(event->type) {
    case GDK_LEAVE_NOTIFY:
      ts=((GdkEventCrossing *)event)->time;
      evas_event_feed_mouse_out(gevas->evas,ts,NULL);
      break;

    case GDK_ENTER_NOTIFY:
      ts=((GdkEventCrossing *)event)->time;
      evas_event_feed_mouse_in(gevas->evas,ts,NULL);
      break;

    case GDK_MOTION_NOTIFY:
      ts=((GdkEventMotion *)event)->time;
      gdk_window_get_pointer(widget->window,&x,&y,NULL);
      evas_event_feed_mouse_move(gevas->evas,x,y,ts,NULL);
      break;

    case GDK_3BUTTON_PRESS:
      c=EVAS_BUTTON_TRIPLE_CLICK;
    case GDK_2BUTTON_PRESS:
      if(c==EVAS_BUTTON_NONE)
        c=EVAS_BUTTON_DOUBLE_CLICK;
    case GDK_BUTTON_PRESS:
      x=(int)event->button.x;
      y=(int)event->button.y;
      b=(int)event->button.button;
      ts=((GdkEventButton *)event)->time;

      evas_event_feed_mouse_move(gevas->evas,x,y,ts,NULL);
      evas_event_feed_mouse_down(gevas->evas,b,c,ts,NULL);
      break;
            
    case GDK_BUTTON_RELEASE:
      x=(int)event->button.x;
      y=(int)event->button.y;
      b=(int)event->button.button;
      ts=((GdkEventButton *)event)->time;

      evas_event_feed_mouse_move(gevas->evas,x,y,ts,NULL);
      evas_event_feed_mouse_up(gevas->evas,b,c,ts,NULL);
      break;

    default:
      break; }

  gevas->current_event=0;


    
  if(event->any.window==widget->window) {
    if(GTK_WIDGET_CLASS(parent_class)->event)
      return (*GTK_WIDGET_CLASS(parent_class)->event)(widget,event);
    else
      return FALSE; }

  return FALSE; }



static void gevas_map(GtkWidget *widget) {
  GtkgEvas *ev;

  g_return_if_fail(widget!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(widget));
  ev=GTK_GEVAS(widget);

  if(GTK_WIDGET_CLASS(parent_class)->map)
    (*GTK_WIDGET_CLASS(parent_class)->map)(widget);

  gevas_buffer_configure(ev,widget->allocation.width,
                          widget->allocation.height); }



static void gevas_realize(GtkWidget *widget) {
  GdkWindowAttr  attributes;
  gint           attributes_mask;
  GtkgEvas      *ev;
  Evas          *evas;

  g_return_if_fail(widget!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(widget));

  ev=GTK_GEVAS(widget);

  /* Set realized flag */
  GTK_WIDGET_SET_FLAGS(widget,GTK_REALIZED);

  /* Evas window */
  attributes.window_type=GDK_WINDOW_CHILD;
  attributes.x=widget->allocation.x;
  attributes.y=widget->allocation.y;
  attributes.width=widget->allocation.width;
  attributes.height=widget->allocation.height;
  attributes.wclass=GDK_INPUT_OUTPUT;
  attributes.visual=gtk_widget_get_visual(widget);
  attributes.colormap=gtk_widget_get_colormap(widget);
  attributes.event_mask=gtk_widget_get_events(widget)|GDK_EXPOSURE_MASK;
  attributes.event_mask|=GDK_EXPOSURE_MASK
    |GDK_BUTTON_PRESS_MASK
    |GDK_BUTTON_RELEASE_MASK
    |GDK_ENTER_NOTIFY_MASK
    |GDK_LEAVE_NOTIFY_MASK
    |GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK;

  /*  attributes.event_mask = GDK_ALL_EVENTS_MASK;  */
  attributes_mask=GDK_WA_X|GDK_WA_Y|GDK_WA_VISUAL|GDK_WA_COLORMAP;

  widget->window=gdk_window_new(gtk_widget_get_parent_window(widget),
                                &attributes,attributes_mask);
  gdk_window_set_user_data(widget->window,widget);
  evas=ev->evas;

  /* Style */
  widget->style=gtk_style_attach(widget->style,widget->window);
  gtk_style_set_background(widget->style,widget->window,GTK_STATE_NORMAL);

  gdk_window_set_back_pixmap(widget->window,NULL,FALSE);

  evas_output_size_set(evas,widget->allocation.width,
                       widget->allocation.height);
  evas_output_viewport_set(evas,0,0,widget->allocation.width,
                           widget->allocation.height);

  if(gevas_buffer_configure(ev,widget->allocation.width,
                            widget->allocation.height)) {
    E(0,"could not initialize evas with engine \"%s\"\n",ev->evas_engine);
    exit(EXIT_FAILURE); }

  evas_event_callback_add(evas,EVAS_CALLBACK_RENDER_FLUSH_POST,gevas_render_flush,widget);

  /* FIXME: Maybe we can do something better than single buffering? */
  gtk_widget_set_double_buffered(widget,0); }



static void gevas_unrealize(GtkWidget *widget) {
  GtkgEvas *ev;

  g_return_if_fail(widget!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(widget));

  ev=GTK_GEVAS(widget);

  idle_enter_remove(&ev->current_idle);

  if(ev->gevasobjlist) {
    g_slist_free(ev->gevasobjlist);
    ev->gevasobjlist=NULL; }

  if(ev->evas) {
#if 0
    evas_free(ev->evas);
#endif
    ev->evas=NULL; }

  /* Hide all windows */
  if(GTK_WIDGET_MAPPED(widget))
    gtk_widget_unmap(widget);

  GTK_WIDGET_UNSET_FLAGS(widget,GTK_MAPPED);

  /* This destroys widget->window and unsets the realized flag
   */
  if(GTK_WIDGET_CLASS(parent_class)->unrealize)
    (*GTK_WIDGET_CLASS(parent_class)->unrealize) (widget); }



static void gevas_size_request(GtkWidget *widget,GtkRequisition *requisition) {
  GtkgEvas *ev;

  g_return_if_fail(widget!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(widget));
  ev=GTK_GEVAS(widget);

  requisition->width=ev->size_request_x;
  requisition->height=ev->size_request_y; }



static void gevas_size_allocate(GtkWidget *widget,GtkAllocation *allocation) {
  GtkgEvas *ev;

  g_return_if_fail(widget!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(widget));

  ev=GTK_GEVAS(widget);

  widget->allocation=*allocation;

  if(GTK_WIDGET_REALIZED(widget)) {
    gevas_buffer_configure(ev,allocation->width,allocation->height);
    gtk_widget_queue_draw(widget);
    gdk_window_move_resize(widget->window,allocation->x,allocation->y,
                           allocation->width,allocation->height); } }




static gint gevas_focus_in(GtkWidget *widget,GdkEventFocus *event) {
  g_return_val_if_fail(widget!=NULL,FALSE);
  g_return_val_if_fail(GTK_IS_GEVAS(widget),FALSE);

  GTK_WIDGET_SET_FLAGS(widget,GTK_HAS_FOCUS);

  return FALSE; }



static gint gevas_focus_out(GtkWidget *widget,GdkEventFocus *event) {
  g_return_val_if_fail(widget!=NULL,FALSE);
  g_return_val_if_fail(GTK_IS_GEVAS(widget),FALSE);

  GTK_WIDGET_UNSET_FLAGS(widget,GTK_HAS_FOCUS);

  return FALSE; }



static void gevas_paint(GtkgEvas *ev,GdkRectangle *area) {
  E(2,"gevas_paint: area %d,%d %dx%d, store %dx%d\n",
    area->x,area->y,area->width,area->height,ev->pixels_w,ev->pixels_h);
  GtkWidget *widget;

  g_return_if_fail(ev!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(ev));

  widget=GTK_WIDGET(ev);

  if(!GTK_WIDGET_DRAWABLE(widget))
    return;

  /* clamp the area to the backing-store rather than dropping it:
     after a window resize GTK sends partial exposes whose area does
     not exactly match the (already reconfigured) backing-store; the
     old 'return' rejected those paints forever, so anything rendered
     by plugins between size-allocates (e.g. the live-video thumbnail
     calling evas_render() per decoded frame) never reached the
     screen -- video kept playing audio-only after a resize. */
  if(area->x<0) { area->width+=area->x; area->x=0; }
  if(area->y<0) { area->height+=area->y; area->y=0; }
  if((area->x+area->width)>ev->pixels_w)
    area->width=ev->pixels_w-area->x;
  if((area->y+area->height)>ev->pixels_h)
    area->height=ev->pixels_h-area->y;
  if((area->width<1)||(area->height<1))
    return;   /* nothing left to paint */

  evas_damage_rectangle_add(ev->evas,area->x,area->y,area->width,area->height);
  evas_obscured_clear(ev->evas);
  evas_render(ev->evas);

  /* blit the rendered buffer into the GdkWindow.  the evas buffer engine
     writes premultiplied native-endian ARGB, which is exactly what cairo's
     CAIRO_FORMAT_ARGB32 expects, so we can wrap it zero-copy. */
  {
    cairo_t         *cr;
    cairo_surface_t *surface;

    surface=cairo_image_surface_create_for_data((unsigned char *)ev->pixels,
                                                CAIRO_FORMAT_ARGB32,
                                                ev->pixels_w,ev->pixels_h,
                                                ev->pixels_w*sizeof(int));
    if((cr=gdk_cairo_create(widget->window))) {
      cairo_set_source_surface(cr,surface,0,0);
      cairo_rectangle(cr,area->x,area->y,area->width,area->height);
      cairo_clip(cr);
      cairo_paint(cr);
      cairo_destroy(cr); }
    cairo_surface_destroy(surface); }

  ev->evas_r=*area; }



static gint gevas_expose(GtkWidget *widget,GdkEventExpose *event) {
  GtkgEvas *ev;

  g_return_val_if_fail(widget!=NULL,0);
  g_return_val_if_fail(GTK_IS_GEVAS(widget),0);

  ev=GTK_GEVAS(widget);

  if(event->window==widget->window) {
    gevas_paint(GTK_GEVAS(widget),&event->area); }
  else
    g_assert_not_reached();

  return TRUE; }



gint gevas_view_redraw(gpointer data) {
  GtkgEvas *ev=(GtkgEvas *)data;
  GdkRectangle area;

  if(!GTK_WIDGET_MAPPED(ev))
    return FALSE;

  area.x=area.y=0;
  area.width=ev->pixels_w;
  area.height=ev->pixels_h;
  gevas_paint(ev,&area);
  ev->current_idle=0;
  return FALSE; }



void gevas_queue_redraw(GtkgEvas *gevas) {
  if(!gevas||!GTK_WIDGET_REALIZED(gevas)||!GTK_WIDGET_MAPPED(gevas))
    return;

  idle_enter_remove(&gevas->current_idle);
  gevas->current_idle=idle_enter_add(G_PRIORITY_LOW,gevas_view_redraw,gevas); }



Evas *gevas_get_evas(GtkgEvas *gevas) {
  return gevas->evas; }



static void gevas_destroy(GtkObject *object) {
  GtkgEvas *ev;

  if((object!=NULL)&&GTK_IS_GEVAS(object)) {
    ev=GTK_GEVAS(object);
    if(ev->evas_engine) {
      free(ev->evas_engine);
      ev->evas_engine=NULL; }
    if(ev->evas) {
      evas_free(ev->evas);
      ev->evas=NULL; }
    if(ev->pixels) {
      free(ev->pixels);
      ev->pixels=NULL;
      ev->pixels_w=ev->pixels_h=0; }}

  GTK_OBJECT_CLASS(parent_class)->destroy(object); }




static void gevas_class_init(GtkgEvasClass *klass) {
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GObjectClass   *go;

  go=(GObjectClass*)klass;
  object_class=(GtkObjectClass*)klass;
  widget_class=(GtkWidgetClass*)klass;
  parent_class=g_type_class_peek(GTK_TYPE_WIDGET);

  widget_class->realize=gevas_realize;
  widget_class->unrealize=gevas_unrealize;
  widget_class->size_request=gevas_size_request;
  widget_class->size_allocate=gevas_size_allocate;
  widget_class->map=gevas_map;
  widget_class->event=gevas_event; /* NULL; */
  widget_class->expose_event=gevas_expose;
  widget_class->focus_in_event=gevas_focus_in;
  widget_class->focus_out_event=gevas_focus_out;

  object_class->destroy=gevas_destroy; }



/******************************************************************************/
