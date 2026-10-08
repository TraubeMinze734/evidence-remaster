/** @file  tt_wholewindow.c
 *  @brief plugin that creates a file-info "tooltip"
 *
 * this plugin creates tooltips that fill the whole window.
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
#include "evas_macros.h"
#include "icon_list.h"
#include "theme.h"
#include "model.h"



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
  Evas_Object *o;
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0,wtk=0,htk=0,wtv=0,htv=0,c,sep;
  noir_theme   t;

  t.icon_mode=IC_NONE;

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



  o=evas_object_gradient_add(evas);  /* fadeout */
  evas_object_resize(o,il->vis_w,il->vis_h);
  evas_object_gradient_angle_set(o,100);
  ICON_EL_APP(IE_DFL,505,0,0);
  evas_object_move(o,0,0);

  icon->els=el;

  for(c=0;c<8;c+=2) {
    evas_object_gradient_colors_clear(o);
    evas_object_gradient_color_add(o,255,255,255, c*4,     63);
    evas_object_gradient_color_add(o,255,255,255, 31+c*32,  7);
    evas_render(evas); }

  if(file_info_get_thumbfile(fi)) {
    if((o=evas_thumbnail_dead_or_alive(&t,evas,fi,il->vis_w,il->vis_h))) {
      evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
      w_=_w; h_=_h;
      ICON_EL_APP(IE_DFL,500,0,0);
      evas_object_move(o,il->vis_w-w_,0);
      evas_object_show(o); }}


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
    evas_object_text_font_set(o,*pfont,(*pfs*3)/4);
    evas_object_text_text_set(o,k);
    evas_object_color_set(o,127,127,127,192);
    ICON_EL_APP(IE_KEY,510,0,0);
    wtk=wt; htk=ht;

    wt=wtv; ht=htv;
    o=evas_object_text_add(evas);
    evas_object_text_font_set(o,*pfont,*pfs);
    evas_object_text_text_set(o,v);
    evas_object_color_set(o,0,0,0, 192);
    ICON_EL_APP(IE_VAL,510,0,0);
    wtv=wt; htv=ht;

    k=n; }

  _y=0;
  _x=0;

  _h=(htv>htk)?htv:htk;
  _w=wtk+sep;

  c=1;
  el=icon->els;
  while(el) {
    if(el->state&(IE_KEY|IE_VAL)) {
      if(c++%2) {
        _y+=_h;
        evas_render(evas); }

      if(el->state&IE_KEY) {
        evas_object_move((Evas_Object *)el->data,_x,_y); }
      else {
        evas_object_move((Evas_Object *)el->data,_w,_y); }}
    el=el->next; }

  free(info);

  return (void *)icon; }
