/*
 * A GTK2 wrapper for an Evas.
 *
 * Copyright (C) 2000 Ben Martin.
 *
 * Original author: Ben Martin
 * stripped down and adapted for use with evidence by Azundris
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


#ifndef INC_GTK_GEVAS_H
#define INC_GTK_GEVAS_H

#include <gtk/gtkwidget.h>
#include <Evas.h>



#define GTK_GEVAS(obj)          GTK_CHECK_CAST(obj,gevas_get_type(),GtkgEvas)
#define GTK_GEVAS_CLASS(klass)  GTK_CHECK_CLASS_CAST(klass,gevas_get_type(),GtkgEvasClass)
#define GTK_IS_GEVAS(obj)       GTK_CHECK_TYPE(obj,gevas_get_type())



#define GTK_GEVAS_SIZE_REQUEST_X 	"GtkgEvas::size_request_x"
#define GTK_GEVAS_SIZE_REQUEST_Y 	"GtkgEvas::size_request_y"
#define GTK_GEVAS_EVAS_ENGINE 		"GtkgEvas::evas_engine"



#define gevas_set_size_request_x(gevas,val) \
          gtk_object_set(GTK_OBJECT(gevas), \
          GTK_GEVAS_SIZE_REQUEST_X,(gint)val,NULL);

#define gevas_set_size_request_y(gevas,val) \
          gtk_object_set(GTK_OBJECT(gevas), \
          GTK_GEVAS_SIZE_REQUEST_Y,(gint)val,NULL);



typedef struct _GtkgEvas      GtkgEvas;
typedef struct _GtkgEvasClass GtkgEvasClass;



struct _GtkgEvas {
  GtkWidget      widget;

  Evas          *evas;
  GdkRectangle   evas_r;            /* cache of the expose event's size.*/

  gint           size_request_x;
  gint           size_request_y;

  gchar         *evas_engine;       /* x11 or OpenGL? */

  void          *current_idle;      /* handles queue render callback*/

  GdkEvent      *current_event;     /* so that evas callbacks can get the gdk event.*/

  gboolean       updating;
	
  GSList        *gevasobjlist;      /* collection of each GtkgEvas in a list.*/
  void          *pixels;            /* backing-store handed to the buffer engine */
  int            pixels_w,pixels_h; /* current size of the backing-store */
};



struct _GtkgEvasClass {
  GtkWidgetClass parent_class;

  /** protected **/
  void (*_register_gevasobj)( GtkgEvas* thisp, GtkObject* gobj );

  /** public **/
  GtkObject* (*get_object_under_mouse)( GtkgEvas* ev );
  GtkObject* (*object_in_rect)(GtkgEvas* ev, double x, double y, double w, double h);
  GtkObject* (*object_at_position)(GtkgEvas* ev, double x, double y);
  GtkObject* (*object_get_named)(GtkgEvas* ev, char *name);
};



Evas      *gevas_get_evas(GtkgEvas *);
void       gevas_queue_redraw(GtkgEvas *);
gint       gevas_view_redraw(gpointer data);
GtkWidget *gevas_new(char *);

#endif   /* __GTK_GEVAS_H__ */
