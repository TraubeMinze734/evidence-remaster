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

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <Evas.h>
#include <Evas_Engine_Software_X11.h>
#ifdef HAVE_EVAS_GL
#  include <Evas_Engine_GL_X11.h>
#endif



/******************************************************************************/


enum {
  ARG_0,                    /**< Skip 0, an invalid argument ID */
  ARG_EVAS_ENGINE,          /**< Evas engine type (GL, software, ...) */
  ARG_SIZE_REQUEST_X,
  ARG_SIZE_REQUEST_Y,
};



/******************************************************************************/



static GtkWidgetClass *parent_class=NULL;



/******************************************************************************/



static void gevas_class_init(GtkgEvasClass *);



/******************************************************************************/



static void gevas_init(GtkgEvas *ev) {
  GTK_WIDGET_SET_FLAGS(GTK_WIDGET(ev),GTK_CAN_FOCUS);

  ev->evas          =evas_new();
  ev->size_request_x=100;
  ev->size_request_y=100;
  ev->gevasobjlist  =NULL; }



guint gevas_get_type(void) {
  static guint ev_type=0;

  if(!ev_type) {
    static const GtkTypeInfo ev_info={
      "GtkgEvas",
      sizeof(GtkgEvas),
      sizeof(GtkgEvasClass),
      (GtkClassInitFunc)  gevas_class_init,
      (GtkObjectInitFunc) gevas_init,
      NULL,
      NULL,
      (GtkClassInitFunc) NULL };

    ev_type=gtk_type_unique(gtk_widget_get_type(),&ev_info); }

  return ev_type; }



GtkWidget *gevas_new(char *engine) {
  GtkgEvas *ev;
  char     *dflt_engine="software_x11";

  if(!(ev=gtk_type_new(gevas_get_type())))
    return NULL;

  gevas_set_size_request_x(ev,200);
  gevas_set_size_request_y(ev,200);

  if(!engine)
    engine=dflt_engine;
  else if(!evas_render_method_lookup(engine)) {
    E(0,"engine \"%s\" not available, defaulting to \"%s\"...\n",
      engine,dflt_engine);
    engine=dflt_engine; }
  ev->evas_engine=strdup(engine);
  evas_output_method_set(ev->evas,evas_render_method_lookup(ev->evas_engine));
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

  evas_output_size_set(ev->evas,widget->allocation.width,
                       widget->allocation.height);
  evas_output_viewport_set(ev->evas,0,0,widget->allocation.width,
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

  /*  evas_output_method_set(evas,evas_render_method_lookup("software_x11")); */
  evas_output_size_set(evas,widget->allocation.width,
                       widget->allocation.height);
  evas_output_viewport_set(evas,0,0,widget->allocation.width,
                           widget->allocation.height);

  if(!strcmp(ev->evas_engine,"software_x11")) {
    Evas_Engine_Info_Software_X11 *einfo=(Evas_Engine_Info_Software_X11*)evas_engine_info_get(evas);
    GdkVisual                     *rootvis=gdk_visual_get_system();

    if(!einfo) {
      E(0,"could not initialize evas with engine \"%s\"\n",ev->evas_engine);
      exit(EXIT_FAILURE); }

    /* the following is specific to the engine */
    einfo->info.display=GDK_WINDOW_XDISPLAY(widget->window);
    einfo->info.visual=GDK_VISUAL_XVISUAL(gtk_widget_get_visual(widget));
    einfo->info.colormap=GDK_COLORMAP_XCOLORMAP(gtk_widget_get_colormap(widget));
    einfo->info.drawable=GDK_WINDOW_XWINDOW(widget->window);
    einfo->info.depth=rootvis->depth;
    einfo->info.rotation=0;
    einfo->info.debug=0;

    evas_engine_info_set(evas,(Evas_Engine_Info *)einfo); }

#ifdef HAVE_EVAS_GL
  else if(!strcmp(ev->evas_engine,"gl_x11")) {
    Display                 *disp=GDK_WINDOW_XDISPLAY(widget->window);
    Evas_Engine_Info_GL_X11 *einfo=(Evas_Engine_Info_GL_X11 *)evas_engine_info_get(evas);

    if(!einfo) {
      E(0,"could not initialize evas with engine \"%s\"\n",ev->evas_engine);
      exit(-1); }

    einfo->info.display =disp;
    einfo->info.visual  =einfo->func.best_visual_get(disp,DefaultScreen(disp));
    einfo->info.drawable=GDK_WINDOW_XWINDOW(widget->window);
E(0,"Drawable: %p\n",widget->window);
    einfo->info.colormap=einfo->func.best_colormap_get(disp,DefaultScreen(disp));
    einfo->info.depth   =einfo->func.best_depth_get(disp,DefaultScreen(disp));

    evas_engine_info_set(evas,(Evas_Engine_Info *)einfo); }
#endif

  else {
    E(0,"could not initialize evas, built without engine \"%s\"\n",ev->evas_engine);
    exit(EXIT_FAILURE); }

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
    evas_output_size_set(ev->evas,widget->allocation.width,
                                  widget->allocation.height);
    evas_output_viewport_set(ev->evas,0,0,widget->allocation.width,
                                          widget->allocation.height);

    gdk_window_move_resize(widget->window,allocation->x,allocation->y,
                           allocation->width,allocation->height); }}




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
  GtkWidget *widget;

  g_return_if_fail(ev!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(ev));

  widget=GTK_WIDGET(ev);

  if(!GTK_WIDGET_DRAWABLE(widget))
    return;

  evas_damage_rectangle_add(ev->evas,area->x,area->y,area->width,area->height);

  ev->evas_r=*area;
  evas_obscured_clear(ev->evas); }



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

  if(!GTK_WIDGET_MAPPED(ev))
    return FALSE;

  evas_obscured_clear(ev->evas);
  evas_render(ev->evas);
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
      ev->evas=NULL; }}

  GTK_OBJECT_CLASS(parent_class)->destroy(object); }


static void gevas_set_arg(GtkObject *object,GtkArg *arg,guint arg_id) {
  GtkgEvas *ev;

  g_return_if_fail(object!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(object));

  ev=GTK_GEVAS(object);

  switch (arg_id) {
    case ARG_EVAS_ENGINE:
      E(0,"evas engine-type may only be set at creation time!\n");
      break;

    case ARG_SIZE_REQUEST_X:
      ev->size_request_x=GTK_VALUE_INT(*arg);
      break;

    case ARG_SIZE_REQUEST_Y:
      ev->size_request_y=GTK_VALUE_INT(*arg);
      break; }}



static void gevas_get_arg(GtkObject *object,GtkArg *arg,guint arg_id) {
  GtkgEvas *ev;

  g_return_if_fail(object!=NULL);
  g_return_if_fail(GTK_IS_GEVAS(object));

  ev=GTK_GEVAS(object);

  switch (arg_id) {
    case ARG_EVAS_ENGINE:
      GTK_VALUE_STRING(*arg)=ev->evas_engine;
      break;
    case ARG_SIZE_REQUEST_X:
      GTK_VALUE_INT(*arg)=ev->size_request_x;
      break;
    case ARG_SIZE_REQUEST_Y:
      GTK_VALUE_INT(*arg)=ev->size_request_y;
      break;
    default:
      arg->type=GTK_TYPE_INVALID;
      break; }}



static void gevas_class_init(GtkgEvasClass *klass) {
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GObjectClass   *go;

  go=(GObjectClass*)klass;
  object_class=(GtkObjectClass*)klass;
  widget_class=(GtkWidgetClass*)klass;
  parent_class=gtk_type_class(gtk_widget_get_type());

  widget_class->realize=gevas_realize;
  widget_class->unrealize=gevas_unrealize;
  widget_class->size_request=gevas_size_request;
  widget_class->size_allocate=gevas_size_allocate;
  widget_class->map=gevas_map;
  widget_class->event=gevas_event; /* NULL; */
  widget_class->expose_event=gevas_expose;
  widget_class->focus_in_event=gevas_focus_in;
  widget_class->focus_out_event=gevas_focus_out;

  object_class->get_arg=gevas_get_arg;
  object_class->set_arg=gevas_set_arg;
  object_class->destroy=gevas_destroy;


  gtk_object_add_arg_type(GTK_GEVAS_SIZE_REQUEST_X,GTK_TYPE_INT,GTK_ARG_READWRITE,ARG_SIZE_REQUEST_X);
  gtk_object_add_arg_type(GTK_GEVAS_SIZE_REQUEST_Y,GTK_TYPE_INT,GTK_ARG_READWRITE,ARG_SIZE_REQUEST_Y); }



/******************************************************************************/
