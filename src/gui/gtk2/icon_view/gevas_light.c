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
static void gevas_paint(GtkgEvas *ev,GdkRectangle *area);   /* fwd: used by gevas_flush_blit() */



/******************************************************************************/



static void gevas_class_init(GtkgEvasClass *);



/* after every evas_render() the backing-store holds a new frame; blit it
   to the GdkWindow on the next GTK expose.  this also covers renders that
   plugins trigger on their own (e.g. the live-video thumbnailer calling
   evas_render() on each decoded frame) -- without the queue_draw those
   frames would stay in the buffer, invisible. */
/* the backing-store holds a new frame; make sure it reaches the screen.
   queue_draw() alone proved unreliable after a window resize -- GTK
   stopped delivering expose events to the widget even though it was
   realized and queue_draw() kept being called (live video kept playing
   audio-only).  the blit path gevas_queue_redraw() uses (idle handler
   calling gevas_paint() directly) does not depend on GTK's exposure
   bookkeeping, so we take that one; queue_draw() is kept as a second
   chance for the ordinary icon-view repaints. */
/* rate-limit for the flush blit, in ms.  the live-video thumbnailer
   renders per decoded frame (25-30fps), and an unrestricted full-area
   blit per frame made window resizing jerky -- the blits competed with
   GTK's own resize drawing.  33ms ~= 30fps: no visible loss for video,
   and resize stays smooth. */
#define GEVAS_FLUSH_INTERVAL 33

/* blit the backing-store to the GdkWindow WITHOUT rendering.  the
   flush callback only fires after evas_render() has already produced a
   new frame, so blitting is all that's left.  calling gevas_paint()
   here instead created a feedback loop: paint -> render -> flush ->
   idle -> paint ... repainting the whole canvas at idle forever, which
   made plain window resizing jerky (constant background re-adapting). */
static void gevas_blit(GtkgEvas *ev,GdkRectangle *area) {
  GtkWidget *widget=GTK_WIDGET(ev);
  cairo_t         *cr;
  cairo_surface_t *surface;
  if(!gtk_widget_is_drawable(widget))
    return;
  surface=cairo_image_surface_create_for_data((unsigned char *)ev->pixels,
                                              CAIRO_FORMAT_ARGB32,
                                              ev->pixels_w,ev->pixels_h,
                                              ev->pixels_w*sizeof(int));
  if((cr=gdk_cairo_create(gtk_widget_get_window(widget)))) {
    cairo_set_source_surface(cr,surface,0,0);
    cairo_rectangle(cr,area->x,area->y,area->width,area->height);
    cairo_clip(cr);
    cairo_paint(cr);
    cairo_destroy(cr); }
  cairo_surface_destroy(surface); }

static gboolean gevas_flush_blit(gpointer data) {
  GtkgEvas *ev=(GtkgEvas *)data;
  ev->flush_idle=NULL;
  if(gtk_widget_get_mapped(GTK_WIDGET(ev))) {
    GdkRectangle area;
    ulong t=now(0);
    /* coalesce: if the last blit is younger than the interval, wait out
       the remainder instead of blitting immediately; the next frame
       re-schedules us anyway. */
    if(ev->flush_last&&(t-ev->flush_last)<GEVAS_FLUSH_INTERVAL) {
      ev->flush_idle=(void *)(uintptr_t)g_timeout_add(GEVAS_FLUSH_INTERVAL-(t-ev->flush_last),gevas_flush_blit,ev);
      return FALSE; }
    ev->flush_last=t;
    area.x=area.y=0;
    area.width=ev->pixels_w;
    area.height=ev->pixels_h;
    gevas_blit(ev,&area); }
  return FALSE; }

/* the backing-store holds a new frame; make sure it reaches the screen.
   queue_draw() alone proved unreliable after a window resize -- GTK
   stopped delivering expose events to the widget even though it was
   realized and queue_draw() kept being called (live video kept playing
   audio-only).  blit via a GTK idle directly, bypassing GTK's exposure
   bookkeeping; queue_draw() is kept as a second chance for ordinary
   icon-view repaints. */
static void gevas_render_flush(void *data,Evas *e,void *event_info) {
  GtkgEvas *ev=(GtkgEvas *)data;
  (void)e; (void)event_info;
  E(4,"gevas_render_flush: ev=%p mapped=%d\n",ev,gtk_widget_get_mapped(GTK_WIDGET(ev))?1:0);
  if(gtk_widget_get_realized(GTK_WIDGET(ev))) {
    gtk_widget_queue_draw(GTK_WIDGET(ev));
    if(!ev->flush_idle)
      ev->flush_idle=(void *)(uintptr_t)g_idle_add(gevas_flush_blit,ev); } }


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
  gtk_widget_set_can_focus(GTK_WIDGET(ev),TRUE);

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
      gdk_window_get_pointer(gtk_widget_get_window(widget),&x,&y,NULL);
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


    
  if(event->any.window==gtk_widget_get_window(widget)) {
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

  { GtkAllocation allocation; gtk_widget_get_allocation(widget,&allocation);
    gevas_buffer_configure(ev,allocation.width,allocation.height); } }



static void gevas_realize(GtkWidget *widget) {
  GdkWindowAttr  attributes;
  gint           attributes_mask;
  GtkgEvas      *ev;
  Evas          *evas;

  g_return_if_fail(widget!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(widget));

  ev=GTK_GEVAS(widget);

  /* Set realized flag */
  gtk_widget_set_realized(widget,TRUE);

  /* Evas window */
  attributes.window_type=GDK_WINDOW_CHILD;
  { GtkAllocation allocation; gtk_widget_get_allocation(widget,&allocation);
    attributes.x=allocation.x;
    attributes.y=allocation.y;
    attributes.width=allocation.width;
    attributes.height=allocation.height; }
  attributes.wclass=GDK_INPUT_OUTPUT;
  attributes.visual=gtk_widget_get_visual(widget);
#if !GTK_CHECK_VERSION(3,0,0)
  attributes.colormap=gtk_widget_get_colormap(widget);
#endif
  attributes.event_mask=gtk_widget_get_events(widget)|GDK_EXPOSURE_MASK;
  attributes.event_mask|=GDK_EXPOSURE_MASK
    |GDK_BUTTON_PRESS_MASK
    |GDK_BUTTON_RELEASE_MASK
    |GDK_ENTER_NOTIFY_MASK
    |GDK_LEAVE_NOTIFY_MASK
    |GDK_SCROLL_MASK
    |GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK;

  /*  attributes.event_mask = GDK_ALL_EVENTS_MASK;  */
#if GTK_CHECK_VERSION(3,0,0)
  attributes_mask=GDK_WA_X|GDK_WA_Y|GDK_WA_VISUAL;
#else
  attributes_mask=GDK_WA_X|GDK_WA_Y|GDK_WA_VISUAL|GDK_WA_COLORMAP;
#endif

  gtk_widget_set_window(widget,gdk_window_new(gtk_widget_get_parent_window(widget),
                                &attributes,attributes_mask));
  gdk_window_set_user_data(gtk_widget_get_window(widget),widget);
  evas=ev->evas;

#if !GTK_CHECK_VERSION(3,0,0)
  /* Style */
  gtk_widget_style_attach(widget);
  gtk_style_set_background(gtk_widget_get_style(widget),gtk_widget_get_window(widget),GTK_STATE_NORMAL);

  gdk_window_set_back_pixmap(gtk_widget_get_window(widget),NULL,FALSE);
#endif

  { GtkAllocation allocation; gtk_widget_get_allocation(widget,&allocation);
    evas_output_size_set(evas,allocation.width,allocation.height);
    evas_output_viewport_set(evas,0,0,allocation.width,allocation.height);

    if(gevas_buffer_configure(ev,allocation.width,allocation.height)) {
      E(0,"could not initialize evas with engine \"%s\"\n",ev->evas_engine);
      exit(EXIT_FAILURE); } }

  evas_event_callback_add(evas,EVAS_CALLBACK_RENDER_FLUSH_POST,gevas_render_flush,ev);

#if !GTK_CHECK_VERSION(3,0,0)
  /* FIXME: Maybe we can do something better than single buffering? */
  gtk_widget_set_double_buffered(widget,0);
#endif
}



static void gevas_unrealize(GtkWidget *widget) {
  GtkgEvas *ev;

  g_return_if_fail(widget!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(widget));

  ev=GTK_GEVAS(widget);

  idle_enter_remove(&ev->current_idle);
  if(ev->flush_idle) {   /* GTK idle, not ecore: remove via g_source_remove */
    g_source_remove((guint)(uintptr_t)ev->flush_idle);
    ev->flush_idle=NULL; }

  if(ev->gevasobjlist) {
    g_slist_free(ev->gevasobjlist);
    ev->gevasobjlist=NULL; }

  if(ev->evas) {
#if 0
    evas_free(ev->evas);
#endif
    ev->evas=NULL; }

  /* Hide all windows */
  if(gtk_widget_get_mapped(widget))
    gtk_widget_unmap(widget);

  gtk_widget_set_mapped(widget,FALSE);

  /* This destroys widget->window and unsets the realized flag
   */
  if(GTK_WIDGET_CLASS(parent_class)->unrealize)
    (*GTK_WIDGET_CLASS(parent_class)->unrealize) (widget); }



#if GTK_CHECK_VERSION(3,0,0)
static void gevas_get_preferred_width(GtkWidget *widget,gint *minimal,gint *natural) {
  GtkgEvas *ev=GTK_GEVAS(widget);
  *minimal=*natural=ev->size_request_x; }
static void gevas_get_preferred_height(GtkWidget *widget,gint *minimal,gint *natural) {
  GtkgEvas *ev=GTK_GEVAS(widget);
  *minimal=*natural=ev->size_request_y; }
#else
static void gevas_size_request(GtkWidget *widget,GtkRequisition *requisition) {
  GtkgEvas *ev;

  g_return_if_fail(widget!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(widget));
  ev=GTK_GEVAS(widget);

  requisition->width=ev->size_request_x;
  requisition->height=ev->size_request_y; }
#endif



static void gevas_size_allocate(GtkWidget *widget,GtkAllocation *allocation) {
  GtkgEvas *ev;

  g_return_if_fail(widget!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(widget));

  ev=GTK_GEVAS(widget);

  gtk_widget_set_allocation(widget,allocation);

  if(gtk_widget_get_realized(widget)) {
    gevas_buffer_configure(ev,allocation->width,allocation->height);
    gtk_widget_queue_draw(widget);
    gdk_window_move_resize(gtk_widget_get_window(widget),allocation->x,allocation->y,
                           allocation->width,allocation->height); } }




static gint gevas_focus_in(GtkWidget *widget,GdkEventFocus *event) {
  g_return_val_if_fail(widget!=NULL,FALSE);
  g_return_val_if_fail(GTK_IS_GEVAS(widget),FALSE);

  return FALSE; }



static gint gevas_focus_out(GtkWidget *widget,GdkEventFocus *event) {
  g_return_val_if_fail(widget!=NULL,FALSE);
  g_return_val_if_fail(GTK_IS_GEVAS(widget),FALSE);



  return FALSE; }



static void gevas_paint(GtkgEvas *ev,GdkRectangle *area) {
  E(4,"gevas_paint: area %d,%d %dx%d, store %dx%d\n",
    area->x,area->y,area->width,area->height,ev->pixels_w,ev->pixels_h);
  GtkWidget *widget;

  g_return_if_fail(ev!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(ev));

  widget=GTK_WIDGET(ev);

  if(!gtk_widget_is_drawable(widget))
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
    if((cr=gdk_cairo_create(gtk_widget_get_window(widget)))) {
      cairo_set_source_surface(cr,surface,0,0);
      cairo_rectangle(cr,area->x,area->y,area->width,area->height);
      cairo_clip(cr);
      cairo_paint(cr);
      cairo_destroy(cr); }
    cairo_surface_destroy(surface); }

  ev->evas_r=*area; }



#if GTK_CHECK_VERSION(3,0,0)
static gboolean gevas_draw(GtkWidget *widget,cairo_t *cr) {
  g_return_val_if_fail(widget!=NULL,FALSE);
  g_return_val_if_fail(GTK_IS_GEVAS(widget),FALSE);
  if(cairo_status(cr))
    return FALSE;
  { double x1,y1,x2,y2;
    GdkRectangle area;
    cairo_clip_extents(cr,&x1,&y1,&x2,&y2);
    area.x=(int)x1; area.y=(int)y1;
    area.width=(int)(x2-x1); area.height=(int)(y2-y1);
    gevas_paint(GTK_GEVAS(widget),&area); }
  return TRUE; }
#else
static gint gevas_expose(GtkWidget *widget,GdkEventExpose *event) {
  GtkgEvas *ev;

  g_return_val_if_fail(widget!=NULL,0);
  g_return_val_if_fail(GTK_IS_GEVAS(widget),0);

  ev=GTK_GEVAS(widget);
  if(event->window==gtk_widget_get_window(widget)) {
    gevas_paint(GTK_GEVAS(widget),&event->area); }
  else
    g_assert_not_reached();

  return TRUE; }
#endif

gint gevas_view_redraw(gpointer data) {
  GtkgEvas *ev=(GtkgEvas *)data;
  GdkRectangle area;

  if(!gtk_widget_get_mapped(GTK_WIDGET(ev)))
    return FALSE;

  area.x=area.y=0;
  area.width=ev->pixels_w;
  area.height=ev->pixels_h;
  gevas_paint(ev,&area);
  ev->current_idle=0;
  return FALSE; }



void gevas_queue_redraw(GtkgEvas *gevas) {
  if(!gevas||!gtk_widget_get_realized(GTK_WIDGET(gevas))||!gtk_widget_get_mapped(GTK_WIDGET(gevas)))
    return;

  idle_enter_remove(&gevas->current_idle);
  gevas->current_idle=idle_enter_add(G_PRIORITY_LOW,gevas_view_redraw,gevas); }



Evas *gevas_get_evas(GtkgEvas *gevas) {
  return gevas->evas; }



#if GTK_CHECK_VERSION(3,0,0)
static void gevas_destroy(GtkWidget *object) {
#else
static void gevas_destroy(GtkObject *object) {
#endif
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

#if GTK_CHECK_VERSION(3,0,0)
  GTK_WIDGET_CLASS(parent_class)->destroy(object); }
#else
  GTK_OBJECT_CLASS(parent_class)->destroy(object); }
#endif




#if GTK_CHECK_VERSION(3,0,0)
static void gevas_class_init(GtkgEvasClass *klass) {
  GtkWidgetClass *widget_class;
  GObjectClass   *go;

  go=(GObjectClass*)klass;
  widget_class=(GtkWidgetClass*)klass;
  parent_class=g_type_class_peek(GTK_TYPE_WIDGET);

  widget_class->realize=gevas_realize;
  widget_class->unrealize=gevas_unrealize;
  widget_class->get_preferred_width=gevas_get_preferred_width;
  widget_class->get_preferred_height=gevas_get_preferred_height;
  widget_class->size_allocate=gevas_size_allocate;
  widget_class->map=gevas_map;
  widget_class->event=gevas_event; /* NULL; */
  widget_class->draw=gevas_draw;
  widget_class->focus_in_event=gevas_focus_in;
  widget_class->focus_out_event=gevas_focus_out;

  widget_class->destroy=gevas_destroy; }
#else
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
#endif



/******************************************************************************/
