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



#define GTK_GEVAS(obj)          G_TYPE_CHECK_INSTANCE_CAST(obj,gevas_get_type(),GtkgEvas)
#define GTK_GEVAS_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST(klass,gevas_get_type(),GtkgEvasClass)
#define GTK_IS_GEVAS(obj)       G_TYPE_CHECK_INSTANCE_TYPE(obj,gevas_get_type())



#define GTK_GEVAS_SIZE_REQUEST_X 	"GtkgEvas::size_request_x"
#define GTK_GEVAS_SIZE_REQUEST_Y 	"GtkgEvas::size_request_y"
#define GTK_GEVAS_EVAS_ENGINE 		"GtkgEvas::evas_engine"



#define gevas_set_size_request_x(gevas,val) \
          G_STMT_START { ((GtkgEvas *)(gevas))->size_request_x=(gint)(val); } G_STMT_END
#define gevas_set_size_request_y(gevas,val) \
          G_STMT_START { ((GtkgEvas *)(gevas))->size_request_y=(gint)(val); } G_STMT_END



typedef struct _GtkgEvas      GtkgEvas;
typedef struct _GtkgEvasClass GtkgEvasClass;



struct _GtkgEvas {
  GtkWidget      widget;

  Evas          *evas;
  GdkRectangle   evas_r;            /* cache of the expose event's size.*/

  gint           size_request_x;
  gint           size_request_y;

  gchar         *evas_engine;       /* x11 or OpenGL? */

  void          *current_idle;      /* handles queue render callback (idle_enter_add: ecore!) */
  void          *flush_idle;        /* g_idle_add handle of the RENDER_FLUSH_POST blit; MUST NOT be
                                        mixed up with current_idle: idle_enter_remove() would feed a
                                        GTK guint to ecore_idle_enterer_del() (SIGSEGV) */

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



GType      gevas_get_type(void);
Evas      *gevas_get_evas(GtkgEvas *);
void       gevas_queue_redraw(GtkgEvas *);
gint       gevas_view_redraw(gpointer data);
GtkWidget *gevas_new(char *);

#endif   /* __GTK_GEVAS_H__ */
