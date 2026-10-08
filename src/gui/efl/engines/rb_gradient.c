/** @file  rb_gradient.c
 *  @brief plugin that creates and handles a "rubberband" selection
 *
 * this extremely simple selection consists of a single gradient.
 * it installs no properties.
 */


#include <Evas.h>
#include <stddef.h>
#include "icon_list.h"
#include "evas_macros.h"
#include "errors.h"



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 */
int rubber_die(noir_theme *theme) {
  return NOIR_IGNORED; }



/**
 * create a new rubber-band selection
 * @return the handle of the newly-created rubber-band selection
 * @param il - the icon-list we wish to select in
 */
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



/**
 * update rubber-band to encompass given coordinates.
 * the coordinates are guaranteed to be in order, ie, x1<=x2 ^ y1<=y2.
 * @return an error-code
 * @param il - the icon-list we're selecting in
 * @param x1 - left boundary
 * @param y1 - upper boundary
 * @param x2 - right boundary
 * @param y2 - lower boundary
 */
int rubber_set(icon_list *il,int x1,int y1,int x2,int y2) {
  Evas *e=get_evas_from_iconlist(il);
  if(!il->rubber)
    return NOIR_NODATA;
  if((x2<x1)||(y2<y1))
    return NOIR_IGNORED;

  evas_object_move((Evas_Object *)il->rubber,x1,y1);
  evas_object_resize((Evas_Object *)il->rubber,x2-x1,y2-y1);
  evas_render(e);
  return NOIR_SUCC; }



/**
 * destruct a rubber-band
 * @param data - the rubber-band handle
 */
void rubber_dst(void **data) {
  evas_object_del((Evas_Object *)(*data));
  *data=NULL; }
