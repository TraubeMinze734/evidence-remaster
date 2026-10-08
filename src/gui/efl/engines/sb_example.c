/** @file  sb_example.c
 *  @brief creates a scrollbar consisting of gradients
 */

#include <string.h>
#include <stdlib.h>
#include <Evas.h>
#include "evas_macros.h"
#include "icon_list.h"
#include "theme.h"
#include "errors.h"



/* scrollbar factory */



/**
 * create a scroll-bar
 * @return a handle (icon) for the newly-created scroll-bar, or NULL
 * @param t       the current theme of this window
 * @param canvas  the canvas to create the scroll-bar on
 */
void *sb_new(noir_theme *t,void *canvas) {
  icon        *icon;
  icon_el     *el=NULL;
  Evas        *evas=(Evas *)canvas;
  Evas_Object *o;
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0;

  if(!(icon=malloc(sizeof(struct _icon))))
    return NULL;

  memset(icon,0,sizeof(struct _icon));
  icon->label=strdup("scrollbar");
  icon->file=NULL;
  icon->name=NULL; /* if sb-data is loaded from a file, note file here */
  icon->index=-1;
  icon->state=IE_DFL;



  o=evas_object_gradient_add(evas);  /* trough */
  evas_object_resize(o,0,0);
  evas_object_gradient_color_add(o,240,240,240, 45,  8);
  evas_object_gradient_color_add(o,200,200,200, 45, 16);
  evas_object_gradient_angle_set(o,150);
  ICON_EL_PRE(IE_TRO,-10,0,0);



  o=evas_object_gradient_add(evas);  /* bar */
  evas_object_resize(o,5,5);
  evas_object_gradient_color_add(o,200,200,200,128,  8);
  evas_object_gradient_color_add(o,120,120,120,128, 16);
  evas_object_gradient_angle_set(o,45);
  ICON_EL_PRE(IE_BAR,10,5,5);

  o=evas_object_gradient_add(evas);  /* bar */
  evas_object_resize(o,16,16);
  evas_object_gradient_color_add(o,200,200,200,128,  8);
  evas_object_gradient_color_add(o,120,120,120,128, 24);
  evas_object_gradient_angle_set(o,315);
  ICON_EL_PRE((IE_BAR|IE_ABS),10,3,3);

  o=evas_object_gradient_add(evas);  /* bar */
  evas_object_resize(o,8,8);
  evas_object_gradient_color_add(o,200,200,200,128,  8);
  evas_object_gradient_color_add(o,120,120,120,128, 24);
  evas_object_gradient_angle_set(o,45);
  ICON_EL_PRE((IE_BAR|IE_ABS),10,0,0);



  /* arrows... */



  icon->els=el;
  icon->x  =
  icon->y  =0;
  icon->w  =32;
  icon->h  =ht;
  return (void *)icon; }



/**
 * update scroll-bar position and/or size
 * @param evas  the canvas the scrollbar is on
 * @param i     the scroll-bar object
 * @param cw    width (total)
 * @param ch    height (total)
 * @param vis   height (visible)
 * @param pos   y-offset (in pixels)
 */
void sb_set(void *evas,icon *i,int cw,int ch,int vis,int pos) {
  icon_el *e;
  int      x,y,w,h;

  if(!i||!ch)
    return;

  if(vis>ch)
    ch=vis;

  i->h=vis;
  i->y=0;
  i->x=cw-i->w;

  e=i->els;
  while(e) {
    x=i->x+e->xo;   /* align at right border */
    w=i->w-e->w-e->xo;

    if(e->state&IE_BAR) {
      if(!(e->state&IE_ABS)) {
	w=e->w=i->w-e->xo-e->xo;
	e->h=h=(int)((vis*vis)/ch)-e->xo-e->xo;
	e->yo=y=(vis*pos)/ch+e->xo; }
      else {
	h=e->h;
	y=(vis*pos)/ch+e->yo; }}
    else {
      y=0;
      e->h=h=vis; }

#if 0
    y+=e->yo;
    h=h-e->yo-e->h;
#endif

    if(!(e->state&IE_ABS))
      evas_object_resize((Evas_Object *)e->data,(double)w,(double)h);

    evas_object_move((Evas_Object *)e->data,(double)x,(double)y);
    evas_object_show((Evas_Object *)e->data);
    e=e->next; }}



/**
 * destruct a scroll-bar
 * @param data  the handle of the scroll-bar to destroy
 */
void sb_dst(void **data) {
  if(data&&*data) {
    icon_dst(*data);
    *data=NULL; }}



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 */
int sb_die(noir_theme *theme) {
  return NOIR_IGNORED; }
