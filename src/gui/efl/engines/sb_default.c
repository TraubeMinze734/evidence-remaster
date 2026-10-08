/** @file  sb_default.c
 *  @brief creates a shiny metal scrollbar
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Evas.h>
#include "evas_macros.h"
#include "icon_list.h"
#include "theme.h"
#include "debug.h"
#include "errors.h"



#define LISTENER "scrollbar{default}"

typedef enum {
  SB_TROUGHWIDTH,
  SB_TROUGHALPHA, SB_BARALPHA, SB_TROUGHLINEALPHA, SB_BARLINEALPHA } ic_tag;

typedef struct _sb_param {
  noir_theme *theme;
  int         trough_width;
  int         trough_alpha,bar_alpha,trough_line_alpha,bar_line_alpha;
} sb_param;



/******************************************************************************/



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 */
int sb_die(noir_theme *theme) {
  if(!theme)
    return NOIR_FAIL;
  if(!theme->sb_data)
    return NOIR_IGNORED;
  free(theme->sb_data);
  theme->sb_data=NULL;
  prop_bag_prop_deaf(theme->data,LISTENER,NULL,NULL);
  return NOIR_SUCC; }



/**
 * listen for changes in our numeric properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int sb_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  long           val=prop_get_int(e);
  const char    *key=prop_get_key(e);
  sb_param      *param=(sb_param *)data;
#if 0
  float          fv;
  noir_theme    *theme=(noir_theme *)param->theme;
#endif

  switch(tag) {
    case SB_TROUGHALPHA:
      param->trough_alpha=val; break;

    case SB_BARALPHA:
      param->bar_alpha=val; break;

    case SB_TROUGHLINEALPHA:
      param->trough_line_alpha=val; break;

    case SB_BARLINEALPHA:
      param->bar_line_alpha=val; break;

    case SB_TROUGHWIDTH:
      param->trough_width=val; break;

    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld/#%06lx, %p)\n",key,val,val,data);

  return NOIR_SUCC; }






/******************************************************************************/
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
  sb_param    *param=NULL;

  if(!(icon=malloc(sizeof(struct _icon))))
    return NULL;

  memset(icon,0,sizeof(struct _icon));
  icon->label=strdup("scrollbar");
  icon->file=NULL;
  icon->name=NULL; /* if sb-data is loaded from a file, note file here */
  icon->index=-1;
  icon->state=IE_DFL;



  if(!(param=t->sb_data)) {
    if(!(t->sb_data=param=malloc(sizeof(sb_param)))) {
      E(0,"out of memory!?\n");
      return NULL; }

    memset(param,0,sizeof(sb_param));

    param->theme=t;

    prop_bag_prop_default(t->data,"bar-alpha","255",0,255,1);
    prop_bag_prop_listen(t->data,LISTENER,"bar-alpha",sb_num_listener,SB_BARALPHA,param);

    prop_bag_prop_default(t->data,"trough-alpha","160",0,255,1);
    prop_bag_prop_listen(t->data,LISTENER,"trough-alpha",sb_num_listener,SB_TROUGHALPHA,param);

    prop_bag_prop_default(t->data,"trough-line-alpha","95",0,255,1);
    prop_bag_prop_listen(t->data,LISTENER,"trough-line-alpha",sb_num_listener,SB_TROUGHLINEALPHA,param);

    prop_bag_prop_default(t->data,"bar-line-alpha","95",0,255,1);
    prop_bag_prop_listen(t->data,LISTENER,"bar-line-alpha",sb_num_listener,SB_BARLINEALPHA,param);

    prop_bag_prop_default(t->data,"trough-width","11",3,64,1);
    prop_bag_prop_listen(t->data,LISTENER,"trough-width",sb_num_listener,SB_TROUGHWIDTH,param); }




  o=evas_object_gradient_add(evas);  /* trough */
  if(!o) {
    icon_dst(icon);
    return NULL; }
  evas_object_resize(o,32,1);
  evas_object_gradient_color_add(o,200,200,200,param->trough_alpha,  8);
  evas_object_gradient_color_add(o,120,120,120,param->trough_alpha, 16);
  evas_object_gradient_angle_set(o,45);
  ICON_EL_PRE(IE_TRO,100,0,0);

  o=evas_object_line_add(evas);      /* trough/line */
  evas_object_line_xy_set(o,0,0,0,100);
  evas_object_color_set(o, 0,0,0, param->trough_line_alpha);
  ICON_EL_PRE(IE_TRO,101,0,0);



  o=evas_object_gradient_add(evas);  /* bar */
  evas_object_resize(o,1,1);
  evas_object_gradient_color_add(o,200,200,200,param->bar_alpha,  8);
  evas_object_gradient_color_add(o,120,120,120,param->bar_alpha, 16);
  evas_object_gradient_angle_set(o,45);
  ICON_EL_PRE(IE_BAR,200,1,0);

  o=evas_object_line_add(evas);      /* bar/line */
  evas_object_line_xy_set(o,0,0,  param->trough_width-2,0);
  evas_object_color_set(o, 0,0,0, param->bar_line_alpha);
  ICON_EL_PRE((IE_BAR|IE_ABS),201,1,0);

  o=evas_object_line_add(evas);      /* bar/line */
  evas_object_line_xy_set(o,0,0,param->trough_width-2,0);
  evas_object_color_set(o, 0,0,0, param->bar_line_alpha);
  ICON_EL_PRE((IE_BAR|IE_ABS),201,1,-1);

  icon->els=el;
  icon->x  =
  icon->y  =0;
  icon->w  =param->trough_width;
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
    w=i->w;

    if(e->state&IE_BAR) {
      if(!(e->state&IE_ABS)) {
        w=e->w=i->w-e->xo-e->xo;
        e->h=h=(int)((vis*vis)/ch);
        e->yo=y=(vis*pos)/ch+e->xo; }
      else {
        h=e->h;
        if(e->yo<0)
          y=((int)(vis*pos)/ch)+((int)((vis*vis)/ch))+e->yo+1;
        else
          y=(vis*pos)/ch+e->yo; }}
    else {
      y=0;
      e->h=h=vis; }

    if(!(e->state&IE_ABS)) {
      if(!strcmp(evas_object_type_get((Evas_Object *)e->data),"line"))
        evas_object_line_xy_set((Evas_Object *)e->data,0,0,0,h);
      else {
        evas_object_resize((Evas_Object *)e->data,w,h); }}

    evas_object_move((Evas_Object *)e->data,x,y);
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
