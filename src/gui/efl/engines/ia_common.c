/** @file  ia_common.c
 *  @brief routines used by several of the ia_*.c (icon-animation plugins).
 *         this is used by several of the actual plugins, so no code that is
 *         used only by a single plug-in should end up in here.
 */

#include <Evas.h>
#include <string.h>
#include "icon_list.h"
#include "evas_compat.h"   /* clamps colours to premultiplied */



/**
 * resize and/or fade icon.
 * @param el - icon-element to apply the action to
 * @param evas - canvas the object lives on
 * @param c - grow by c pixels (0...oo)
 * @param alpha - transparency (0.0 ... 1.0)
 * @param x - x-position
 * @param y - y-position
 * @param s - affect only elements that have at least on of the states in s,
 *            and are of the "image" type.
 */
void ani_icon_pulse(icon_el *el,Evas *evas,float c,float alpha,int x,int y,icon_state s) {
  int          c2,w,h,r,g,b,a,img;
  Evas_Object *o;

  c2=c*2;
  while(el) {
    if((o=(Evas_Object *)el->data)) {
      img=!strcmp(evas_object_type_get(o),"image");
      if(img&&el->state&s) {
        w=el->w+c2;
        h=el->h+c2;
        evas_object_resize(o,w,h);
        if(img) {
          evas_object_image_fill_set(o,0,0,w,h);
          r=g=b=255; }
        else
          evas_object_color_get(o,&r,&g,&b,&a);
        evas_object_color_set(o,r,g,b,alpha*255);
        evas_object_move(o,x+el->xo-c,y+el->yo-c); }}
    el=el->next; }}



/**
 * tint image-parts of an icon to colour
 * @param evas - the canvas the icon is displayed on
 * @param i - the icon to modify
 * @param r - red (0..255)
 * @param g - green (0..255)
 * @param b - blue (0..255)
 */
void ani_icon_recolourize(Evas *evas,icon *i,int r,int g,int b) {
  icon_el     *el=i->els;
  Evas_Object *o;

  /* an icon usually consists of several parts -- a text-label, an image,
     maybe a read-only emblem ("stop sign")...  hence, we get a list of
     parts in el that we now iterate over. */
  while(el) {
    if((o=(Evas_Object *)el->data)) {
      if((el->state&i->state)&&!strcmp(evas_object_type_get(o),"image")) {
        evas_object_color_set(o,r,g,b,255); }}
    el=el->next; }}



/**
 * raise/lower an icon
 * @param i - the icon to raise
 * @param f - raise/lower by
 * @param t - affect only icon-elements that have a least once of the tags in t
 */
void ani_icon_raise(icon *i,int f,element_tag t) {
  icon_el     *el=i->els;
  Evas_Object *o;

  /* an icon usually consists of several parts -- a text-label, an image,
     maybe a read-only emblem ("stop sign")...  hence, we get a list of
     parts in el that we now iterate over. */
  while(el) {
    if(el->tag&t) {
      if((o=(Evas_Object *)el->data))
        evas_object_layer_set(o,evas_object_layer_get(o)+f); }
    el=el->next; }}
