/** @file  rb_nautilus.c
 *  @brief plugin that creates and handles a "rubberband" selection
 *
 * this selection by default looks like that in the nautilus file-manager.
 *
 * the factory installs a variety of properties; see below.
 * these properties can be set in themes, or from the typebuffer, e.g.
 * 'rubberboxbordercolour #ff00ff
 */

#include <string.h>
#include <stdlib.h>
#include <Evas.h>
#include <stdio.h>

#include "evas_macros.h"
#include "icon_list.h"
#include "errors.h"
#include "debug.h"
#include "theme.h"

#define LISTENER "rubberbox{nautilus}"

typedef enum {
  RB_BORDERCOL,
  RB_INTLCOL,
  RB_BORDERALPHA,
  RB_INTLALPHA,
  RB_BORDERWIDTH } rb_tag;

typedef struct _rb_param {
  long interior_colour,border_colour;
  int interior_alpha,border_alpha;
  int border_width;
} rb_param;



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 */
int rubber_die(noir_theme *theme) {
  rb_param    *param;
  int          c=0;

  if(!theme)
    return NOIR_FAIL;

  if(prop_bag_prop_deaf(theme->data,LISTENER,NULL,NULL)==NOIR_SUCC)
    c++;
  if((param=theme->rubber_data)) {
    memset(param,0,sizeof(param));
    free(param);
    theme->rubber_data=NULL;
    c++; }

  return c?NOIR_SUCC:NOIR_IGNORED; }



/**
 * listen for changes in our numeric properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int rb_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  long           val=prop_get_int(e);
  const char    *key=prop_get_key(e);
  rb_param      *param=(rb_param *)data;

  switch(tag) {
    case RB_BORDERCOL:
      param->border_colour=val; break;
    case RB_BORDERALPHA:
      param->border_alpha=val; break;
    case RB_INTLCOL:
      param->interior_colour=val; break;
    case RB_INTLALPHA:
      param->interior_alpha=val; break;
    case RB_BORDERWIDTH:
      param->border_width=val; break;
    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld, %p)\n",key,val,data);

  return NOIR_SUCC; }



/**
 * create a new rubber-band selection
 * @return the handle of the newly-created rubber-band selection
 * @param il - the icon-list we wish to select in
 */
void *rubber_new(icon_list *il) {
  noir_theme     *t=(noir_theme  *)il->theme;
  noir_prop_bag  *te=t->data;
  Evas           *evas=get_evas_from_iconlist(il);
  Evas_Object    *o;
  icon           *icon=NULL;
  icon_el        *el=NULL;
  rb_param       *param=NULL;

  Evas_Coord     _x,_y,_w,_h;
  int            w_,h_,wt=0,ht=0,i;
  long           col;

  if(!(param=t->rubber_data)) {
    if(!(t->rubber_data=param=malloc(sizeof(rb_param)))) {
      E(0,"out of memory!?\n");
      return NULL; }

    memset(param,0,sizeof(rb_param));

    param->border_colour=0x400020;
    param->border_alpha=255;
    param->interior_colour=0x400020;
    param->interior_alpha=40;
    param->border_width=1;

    prop_bag_prop_listen(te,LISTENER,"rubberbox-border-colour",   rb_num_listener,RB_BORDERCOL,param);
    prop_bag_prop_listen(te,LISTENER,"rubberbox-border-alpha",    rb_num_listener,RB_BORDERALPHA,param);
    prop_bag_prop_listen(te,LISTENER,"rubberbox-interior-colour", rb_num_listener,RB_INTLCOL,param);
    prop_bag_prop_listen(te,LISTENER,"rubberbox-interior-alpha",  rb_num_listener,RB_INTLALPHA,param);
    prop_bag_prop_listen(te,LISTENER,"rubberbox-border-width",    rb_num_listener,RB_BORDERWIDTH,param); }

  if(!(icon=malloc(sizeof(struct _icon))))
    return NULL;
  memset(icon,0,sizeof(struct _icon));

  o=evas_object_rectangle_add(evas);  /* middle */
  col=param->interior_colour;
  evas_object_color_set(o, (col>>16)%256,(col>>8)%256,col%256, param->interior_alpha);
  evas_object_layer_set(o,300);
  ICON_EL_PRE(SL_MID,0,0,0);

  col=param->border_colour;
  for(i=SL_TOP;i<=SL_LEFT;i++) {
    o=evas_object_rectangle_add(evas);
    evas_object_color_set(o, (col>>16)%256,(col>>8)%256,col%256, param->border_alpha);
    evas_object_layer_set(o,300);
    ICON_EL_PRE(i,0,0,0); }

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
  noir_theme  *t=(noir_theme  *)il->theme;
  rb_param    *param=(rb_param *)t->rubber_data;
  Evas        *e=get_evas_from_iconlist(il);
  icon        *icon=il->rubber;
  icon_el     *el;
  int          w=x2-x1,h=y2-y1,s,b=param?param->border_width:1;
  Evas_Object *o;

  if(!e||!icon||!(el=icon->els))
    return NOIR_FAIL;

  while(el) {
    o=el->data;
    s=(el->state&~SL_GRAD);

    /*
     *  |----|
     *  |    |
     *  |____|
     */

    switch(s) {
    case SL_MID:
    case SL_NONE:
      evas_object_move(o,x1+b,y1+b);
      evas_object_resize(o,w-b-b-1,h-b-b-1);
      break;
    case SL_LEFT:
      evas_object_move(o,x1,y1);
      evas_object_resize(o,b,h-1);
      break;
    case SL_RIGHT:
      evas_object_move(o,x2-b-1,y1);
      evas_object_resize(o,b,h-1);
      break;
    case SL_TOP:
      evas_object_move(o,x1+b,y1);
      evas_object_resize(o,w-b-b,b);
      break;
    case SL_BOTTOM:
      evas_object_move(o,x1+b,y2-1-b);
      evas_object_resize(o,w-b-b,b);
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
