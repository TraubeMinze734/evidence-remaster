/** @file  rb_gradient2.c
 *  @brief plugin that creates and handles a "rubberband" selection
 *
 * this simple selection installs no properties.  it consists of a central
 * rectangle and four gradients that "fade it out."  it mimicks the look
 * of the edb-selection that came with the first e17/essence prototype.
 */

#include <Evas.h>
#include <stddef.h>
#include <string.h>

#include "evas_macros.h"
#include "errors.h"
#include "icon_list.h"



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
  Evas          *evas=get_evas_from_iconlist(il);
  Evas_Object   *o,*c;
  icon          *icon=NULL;
  icon_el       *el=NULL;

  Evas_Coord    _x,_y,_w,_h;
  int           w_,h_,wt=0,ht=0,i;

  if(!(icon=malloc(sizeof(struct _icon))))
    return NULL;
  memset(icon,0,sizeof(struct _icon));

  c=o=evas_object_rectangle_add(evas);  /* clip */
  evas_object_color_set(c, 255,255,255, 255);
  ICON_EL_PRE(SL_NONE,0,0,0);

  o=evas_object_rectangle_add(evas);  /* middle */
  evas_object_color_set(o, 255,255,255, 100);
  evas_object_layer_set(o,300);
  ICON_EL_PRE(SL_MID,0,0,0);

  for(i=SL_TOP;i<=SL_LEFT;i++) {
    o=evas_object_rectangle_add(evas);
    evas_object_color_set(o, 0,0,0, 255);
    evas_object_layer_set(o,300);
    ICON_EL_PRE(i,0,0,0); }

  for(i=SL_TOP;i<=SL_LEFT;i++) {
    o=evas_object_gradient_add(evas);   /* grad */
    evas_object_gradient_angle_set(o,(i-SL_TOP)*90);
    evas_object_gradient_color_add(o, 255,255,255, 100, 8);
    evas_object_gradient_color_add(o, 255,255,255,   0, 8);
    evas_object_layer_set(o,300);
    evas_object_clip_set(o,c);
    ICON_EL_PRE((i|SL_GRAD),0,0,0); }

  icon->state=SL_ALL;
  icon->els=el;

  return (void *)icon; }



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
  icon        *ic;
  icon_el     *el;
  int          w=x2-x1,h=y2-y1,s,g1,g2;
  Evas_Object *o;
  Evas        *e=get_evas_from_iconlist(il);

  if(!e)
    return NOIR_FAIL;
  if(!(ic=(struct _icon *)il->rubber)||(!(el=ic->els)))
    return NOIR_NODATA;
  if((x2<x1)||(y2<y1))
    return NOIR_IGNORED;

  while(el) {
    o=el->data;
    g1=(el->state&SL_GRAD)?8:1;
    g2=g1?1:0;
    s=(el->state&~SL_GRAD);

    switch(s) {
    case SL_MID:
    case SL_NONE:
      evas_object_move(o,x1+1,y1+1);
      evas_object_resize(o,w-2,h-2);
      break;

    case SL_LEFT:
      evas_object_move(o,x1+g2,y1+1);
      evas_object_resize(o,g1,h-2);  /* h-1? */
      break;
    case SL_RIGHT:
      evas_object_move(o,x2-1-g1,y1+g2);
      evas_object_resize(o,g1,h-1-g2);
      break;
    case SL_TOP:
      evas_object_move(o,x1+g2,y1+g2);
      evas_object_resize(o,w-g2-g2,g1);
      break;
    case SL_BOTTOM:
      evas_object_move(o,x1+1,y2-1-g1);
      evas_object_resize(o,w-1-g2,g1);
      break; }
    evas_object_show(o);
    el=el->next; }

  evas_render((Evas *)e);
  return NOIR_SUCC; }



/**
 * destruct a rubber-band
 * @param data - the rubber-band handle
 */
void rubber_dst(void **data) {
  if(data&&*data) {
    icon_dst(*data);
    *data=NULL; }}
