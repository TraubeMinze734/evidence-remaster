/** @file  tt_centre.c
 *  @brief plugin that creates a file-info "tooltip"
 *
 * this plugin creates tooltips that are centered in the middle of the window.
 *
 * some tunables exist as #defines and should be turned into properties
 * at some point. ###
 */

#define FS "tooltip-font-size"
#define TT "tooltip-font"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <Evas.h>
#include <stdio.h>

#include "debug.h"
#include "icon_list.h"
#include "evas_macros.h"
#include "theme.h"
#include "model.h"



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
 */
static icon_el *frame_new(Evas *evas,icon *icon,icon_el *el,double x1,double y1,double x2,double y2) {
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0;
  Evas_Object *o;

  o=evas_object_line_add(evas);  /* frame/left */
  ICON_EL_APP(IE_DFL,510,0,0);
  evas_object_line_xy_set(o,x1,y1,x1,y2);
  evas_object_color_set(o,255,255,255,255);

  o=evas_object_line_add(evas);  /* frame/right */
  ICON_EL_APP(IE_DFL,510,0,0);
  evas_object_line_xy_set(o,x2,y1,x2,y2);
  evas_object_color_set(o,255,255,255,255);

  o=evas_object_line_add(evas);  /* frame/upper */
  ICON_EL_APP(IE_DFL,510,0,0);
  evas_object_line_xy_set(o,x1,y1,x2,y1);
  evas_object_color_set(o,255,255,255,255);

  o=evas_object_line_add(evas);  /* frame/lower */
  ICON_EL_APP(IE_DFL,510,0,0);
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

  char       **pfont=NULL,*font;
  long        *pfs=NULL,fs;

  icon        *icon;
  icon_el     *el=NULL;
  Evas_Object *o,*fade;
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0,wtk=0,htk=0,wtv=0,htv=0,c=0,sep,fs2;
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

  font=*pfont;
  fs=*pfs;

  sep=fs;
  fs2=(fs*3)/4;

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
  ICON_EL_APP(IE_DFL,505,0,0);
  evas_object_move(o,0,0);

  icon->els=el;

  while((v=strchr(k,':'))) {
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
    evas_object_text_font_set(o,font,fs2);
    evas_object_text_text_set(o,k);
    evas_object_color_set(o,87,87,87, 255);
    ICON_EL_APP(IE_KEY,510,0,0);
    wtk=wt; htk=ht;

    wt=wtv; ht=htv;
    o=evas_object_text_add(evas);
    evas_object_text_font_set(o,font,fs);
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

  _x=(il->vis_w-wt)/2;
  _y=(il->vis_h-(ht*c))/2;

  evas_object_move(fade,_x,_y);
  evas_object_resize(fade,wt+fs,ht*c);

  if(file_info_get_thumbfile(fi)) {
    t.icon_width=wt+fs;
    t.icon_height=ht*c;

    if(
#if 1
      (o=evas_thumbnail_dead_or_alive(&t,evas,fi,t.icon_width,t.icon_height))
#else
      (o=evas_thumbnail_dead(&t,evas,fi))
#endif
      ) {
      int hts=ht,wts=wt;
      Evas_Coord _xs=_x,_ys=_y,_ws=_w,_hs=_h,tw=(wt+fs),th=ht*c;;
      evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
      w_=_w; h_=_h;
      ICON_EL_APP(IE_DFL,500,0,0);
      evas_object_move(o,(_x=_xs)+tw-w_,(_y=_ys)+th-h_);
      evas_object_show(o);
      _w=_ws; _h=_hs; wt=wts; ht=hts; }}

  frame_new(evas,icon,el,_x,_y,_x+wt+fs,_y+ht*c);

  for(c=0;c<8;c+=2) {   /* do the fade-in */
    evas_object_gradient_colors_clear(fade);
    evas_object_gradient_color_add(fade,255,255,255, c*4,     63);
    evas_object_gradient_color_add(fade,255,255,255, 63+c*32,  7);
    evas_render(evas); }

  if((_x+=(fs/2))<8)
    _x=8;
  _y-=fs+(fs/2.0);

  c=1;
  el=icon->els;
  while(el) {
    if(el->state&(IE_KEY|IE_VAL)) {
      if(c++%2) {
        _y+=_h;
        evas_render(evas); }

      if(el->state&IE_KEY) {
        evas_object_move((Evas_Object *)el->data,_x,_y+(fs-fs2)); }
      else {
        evas_object_move((Evas_Object *)el->data,_x+_w,_y); }}
    el=el->next; }

  free(info);

  return (void *)icon; }
