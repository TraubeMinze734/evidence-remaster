/* FORTHCOMING. MAYBE. */

#include "config.h"

#ifndef HAVE_EDB
#  warning building the edb-type rubberband-plugin in the absence of libedb is rather pointless...
#else
#  include "evidence.h"
#  include <Evas.h>
#  include "evas_macros.h"
#  include "icon_list.h"
#  include "errors.h"



/* rubberband factory */



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 */
int rubber_die(noir_theme *theme) {
  return NOIR_IGNORED; }



void *rubber_new(icon_list *il) {
  Evas        *canvas=get_evas_from_iconlist(il);
  Evas_Object *o;
  if((o=evas_object_gradient_add(canvas))) {
    evas_object_move(o,-1000,-1000);
    evas_object_resize(o,1,1);
    evas_object_show(o);

    evas_object_gradient_color_add(o,100,100,100, 45,  8);
    evas_object_gradient_color_add(o,240,240,240,150, 16);
    evas_object_gradient_angle_set(o,150);

    evas_object_layer_set(o,-10); }
  return (void *)o; }



int rubber_set(icon_list *il,int x1,int y1,int x2,int y2) {
  Evas        *e=get_evas_from_iconlist(il);
  Evas_Object *r=il->rubber;

  if(r&&(x2>x1)&&(y2>y1)) {
    evas_object_move(r,x1,y1);
    evas_object_resize(r,x2-x1,y2-y1);
    evas_render(e);
    return TRUE; }
  return FALSE; }



void rubber_dst(void **data) {
  evas_object_del((Evas_Object *)(*data));
  *data=NULL; }

#endif
