/** @file  tt_traditional.c
 *  @brief plugin that creates a file-info "tooltip"
 *
 * this plugin creates tooltips that are centered around the item the tooltip
 * is for.
 *
 * some tunables exist as #defines and should be turned into properties
 * at some point. ###
 */

#define FS "tooltip-font-size"
#define TT "tooltip-font"

#include <ctype.h>
#include <string.h>
#include <Evas.h>
#include <stdlib.h>
#include <stdio.h>

#include "debug.h"
#include "evas_macros.h"
#include "icon_list.h"
#include "theme.h"
#include "model.h"



#define MIN(a,b)  ((a)<(b))?(a):(b)
#define MAX(a,b)  ((a)>(b))?(a):(b)



/**
 * create a frame around the tooltip.
 * @return a linked list of icon-elements.
 * @param evas - the evas
 * @param icon - the tooltip to append the elements to (needed by ICON_EL_*())
 * @param el - the current icon-element
 * @param x1 - left boundary
 * @param y1 - upper boundary
 * @param x2 - right boundary
 * @param y2 - lower boundary
 * @param state - state mask for the icon-elements to create
 */
static icon_el *frame_new(Evas *evas,icon *icon,icon_el *el,double x1,double y1,double x2,double y2,int state) {
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0;
  Evas_Object *o;

  o=evas_object_line_add(evas);  /* frame/left */
  ICON_EL_APP(state,510,0,0);
  evas_object_line_xy_set(o,x1,y1,x1,y2);
  evas_object_color_set(o,255,255,255,255);

  o=evas_object_line_add(evas);  /* frame/right */
  ICON_EL_APP(state,510,0,0);
  evas_object_line_xy_set(o,x2,y1,x2,y2);
  evas_object_color_set(o,255,255,255,255);

  o=evas_object_line_add(evas);  /* frame/upper */
  ICON_EL_APP(state,510,0,0);
  evas_object_line_xy_set(o,x1,y1,x2,y1);
  evas_object_color_set(o,255,255,255,255);

  o=evas_object_line_add(evas);  /* frame/lower */
  ICON_EL_APP(state,510,0,0);
  evas_object_line_xy_set(o,x1,y2,x2,y2);
  evas_object_color_set(o,255,255,255,255);

  return el; }






/*****************************************************************************/
/* public interface */
/********************/



/**
 * destruct a tooltip.
 * @param data - the tooltip-handle tt_new() gave us
 */
void tt_dst(void **data) {
  if(data&&*data) {
    icon_dst(*data);
    *data=NULL; }}



/**
 * create a tooltip.
 *
 * no assumptions about the nature of the tooltip are made; for all the main
 * application knows, the plugin might read the info to the user aloud.
 *
 * @return a handle to pass to tt_dst() when destroy the tooltip.
 * @param data - the icon-list the tooltip is related to
 * @param x - x-position of the icon the info is for
 * @param y - y-position of the icon the info is for
 * @param fi - the file we're displaying info for
 */
void *tt_new(void *data,int x,int y,file_info *fi) {
  icon_list   *il=(icon_list *)data;
  noir_theme  *theme=(noir_theme *)il->theme;
  Evas        *evas=get_evas_from_iconlist(il);

  char        *info=meta_to_string(file_info_get_meta(fi)),*k=info,*v,*n,*p;

  char       **pfont=NULL;
  long        *pfs=NULL;

  icon        *icon;
  icon_el     *el=NULL;
  Evas_Object *o,*fade,*thumb=NULL;
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0,wtk=0,htk=0,wtv=0,htv=0,c=0,sep,fs2,
               thumb_x=0,thumb_y=0,thumb_w=0,thumb_h=0;
  noir_theme   t;

  t.icon_mode=IC_AUTO;

  if(!info)
    return NULL;

  if(!pfont) {
    if(!(pfont=(char **)prop_get_data(prop_bag_prop_get(theme->data,TT)))) {
      E(0,TT " not set, failing.\n");
      return NULL; }}

  if(!pfs) {
    if(!(pfs=(long *)prop_get_data(prop_bag_prop_get(theme->data,FS)))) {
      prop_bag_prop_set(theme->data,FS,"20");
      pfs=(long *)prop_get_data(prop_bag_prop_get(theme->data,FS));
      E(0,FS " not set, falling back to default (%ld).\n",*pfs); }}

  sep=*pfs;
  if((fs2=(*pfs*3)/4)<9)
    fs2=9;

  if(!evas)
    return NULL;

  if(!(icon=malloc(sizeof(struct _icon))))
    return NULL;

  memset(icon,0,sizeof(struct _icon));
  icon->label=strdup("tooltip");
  icon->file=NULL;
  icon->name=NULL; /* if sb-data is loaded from a file, note file here */
  icon->index=-1;
  icon->state=IE_DFL|IE_KEY|IE_VAL;



  fade=o=evas_object_gradient_add(evas);  /* fadeout */
  evas_object_gradient_angle_set(o,100);
  ICON_EL_PRE(IE_DFL,505,0,0);
/* evas_object_move(o,0,0); */

  icon->els=el;

  while((v=strchr(k,':'))) {              /* create & count info lines */
    p=v-1;                                /* p: last char before : */
    while(*(++v)==' ')                    /* v: skip spaces after :, if any */
      ;
    while((p>=k)&&(*p==' '))              /* p: \0 whitespace after key */
      p--;
    *(++p)='\0';
    if((n=strchr(v,'\n')))                /* n: next line */
      *(n++)='\0';
    else
      n=v+strlen(v);

    wt=wtk; ht=htk;
    o=evas_object_text_add(evas);
    evas_object_text_font_set(o,*pfont,fs2);
    evas_object_text_text_set(o,k);
    evas_object_color_set(o,87,87,87, 255);
    ICON_EL_APP(IE_KEY,510,0,0);
    wtk=wt; htk=ht;

    wt=wtv; ht=htv;
    o=evas_object_text_add(evas);
    evas_object_text_font_set(o,*pfont,*pfs);
    evas_object_text_text_set(o,v);
    evas_object_color_set(o,0,0,0, 255);
    ICON_EL_APP(IE_VAL,510,0,0);
    wtv=wt; htv=ht;

    k=n;
    c++; }

  _y=0;
  _x=0;
  _h=(htv>htk)?htv:htk;
  _w=wtk+sep;

  wt=wtk+wtv+sep;
  ht=_h;

  { int w=wt+*pfs,h=ht*c,scr_w,scr_h;

    scr_w=il->vis_w-w;   scr_h=il->vis_h-h;    /* screen-size */
    x-=w/2;              y-=h/2;               /* centre around mouse */
    x=MIN(MAX(x,0),scr_w);
    y=MIN(MAX(y,0),scr_h);
    _x=x;  _y=y; }

  evas_object_move(fade,_x,_y);
  evas_object_resize(fade,wt+*pfs,ht*c);

  if(file_info_get_thumbfile(fi)) {
    t.icon_width=wt+*pfs;
    t.icon_height=ht*c;

    if(
#if 1
      (o=evas_thumbnail_dead_or_alive(&t,evas,fi,t.icon_width,t.icon_height))
#else
      (o=evas_thumbnail_dead(&t,evas,fi))
#endif
      ) {
      /* a live thumbnail is an emotion smart object -- image_fill_set()
         on it is illegal and crashes (eina safety / SIGSEGV).  only an
         actual image object gets the fill-rect + fade-in treatment. */
      int is_image=!strcmp(evas_object_type_get(o),"image");
      int    hts=ht,wts=wt;
      double _xs=_x,_ys=_y,
             _ws=_w,_hs=_h;
      double tw=(wt+*pfs),th=ht*c;;

      evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
      w_=_w; h_=_h;
      thumb_w=w_;
      thumb_h=h_;
      ICON_EL_APP(IE_DFL,500,0,0);
      evas_object_move(o,thumb_x=((_x=_xs)+tw-w_),thumb_y=((_y=_ys)+th-h_));
      evas_object_show(thumb=o);
      _w=_ws; _h=_hs; wt=wts; ht=hts; }}

  frame_new(evas,icon,el,_x,_y,_x+wt+*pfs,_y+ht*c,IE_DFL);

  /* do the fade-in */
  if(!thumb) {
    for(c=0;c<8;c+=2) {
      evas_object_gradient_colors_clear(fade);
      evas_object_gradient_color_add(fade,255,255,255, c*4,     63); /* c*4 */
      evas_object_gradient_color_add(fade,255,255,255, 63+c*32,  7); /* 31+c*32 */
      evas_render(evas); }}
  else {
    double d=1;
    double s=(thumb_w>thumb_h)?thumb_w:thumb_h;
    double xs=thumb_w/s,ys=thumb_h/s,xc=0,yc=0;
    evas_object_gradient_colors_clear(fade);
    evas_object_gradient_color_add(fade,255,255,255,  24,  63);
    evas_object_gradient_color_add(fade,255,255,255, 255,  7);
    evas_object_hide(fade);
    for(c=0;d>0;c+=d) {
      d*=1.2;
      xc+=(xs*d);
      yc+=(ys*d);
      if((xc>thumb_w)||(yc>thumb_h))
	d=0;
      else {
        evas_object_resize(thumb,xc,yc);
        if(is_image)
          evas_object_image_fill_set(o,0,0,xc,yc);
        evas_object_move(thumb,thumb_x+(thumb_w-xc)/2,thumb_y+(thumb_h-yc)/2);
        evas_render(evas); }}

    evas_object_resize(thumb,thumb_w,thumb_h);
    if(is_image)
    evas_object_image_fill_set(o,0,0,thumb_w,thumb_h);
    evas_object_move(thumb,thumb_x,thumb_y);

    evas_object_show(fade);
    evas_render(evas); }

  if((_x+=(*pfs/2))<8)
    _x=8;
  _y-=*pfs+(*pfs/2.0);

  c=1;
  el=icon->els;
  while(el) {
    if(el->state&(IE_KEY|IE_VAL)) {
      if(c++%2) {
        _y+=_h;
        evas_render(evas); }

      if(el->state&IE_KEY) {
        evas_object_move((Evas_Object *)el->data,el->xo=_x,el->yo=_y+(*pfs-fs2)); }
      else {
        evas_object_move((Evas_Object *)el->data,el->xo=_x+_w,el->yo=_y); }}
    el=el->next; }

  free(info);

  return (void *)icon; }
