/** @file  ic_alignbelow.c
 *  @brief plugin that creates an icon.
 *
 * text-labels appear below the icon's image.
 * long text-labels are cut off; an ellipsis ("...") will be appended to
 * the label to indicate this.
 *
 * items that evidence's thumbnailer was able to generate previews for will
 * be show a preview in lieu of a datafile-icon.
 *
 * the factory installs a variety of properties; see below.
 * these properties can be set in themes, or from the typebuffer, e.g.
 * 'icontextcolour #ff00ff
 */

#include <Evas.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "session.h"    /* noir_sess_redraw_files() */
#include "config.h"
#include "debug.h"
#include "errors.h"
#include "theme.h"
#include "evas_macros.h"
#include "icon_list.h"



#undef GHOST



#define LISTENER "iconfactory{alignbelow}"

typedef enum {
  IC_TEXTFONT,  IC_TEXTSIZE,
  IC_TEXTCOL,   IC_DSCOL,   IC_SELCOL,
  IC_TEXTALPHA, IC_DSALPHA, IC_SELALPHA,
  IC_DSXOFFS,   IC_DSYOFFS,
  IC_GRID } ic_tag;

typedef struct _ic_param {
  icon_list *il;
  char *text_font;
  int   text_size;
  long  text_colour,ds_colour,sel_colour;
  int   text_alpha, ds_alpha, sel_alpha;
  int   ds_x_offset,ds_y_offset;
  float grid;
  ic_mode icm_save;
} ic_param;



/******************************************************************************/



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 */
int icon_die(noir_theme *theme) {
  ic_param    *param;
  int          c=0;

  if(!theme)
    return NOIR_FAIL;

  if(prop_bag_prop_deaf(theme->data,LISTENER,NULL,NULL)==NOIR_SUCC)
    c++;
  if((param=theme->icon_data)) {
    if(param->text_font)
      free(param->text_font);
    memset(param,0,sizeof(param));
    free(param);
    theme->icon_data=NULL;
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
static int ic_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  long           val=prop_get_int(e);
  const char    *key=prop_get_key(e);
  float          fv;
  ic_param      *param=(ic_param *)data;
  noir_theme    *theme=(noir_theme *)param->il->theme;

  switch(tag) {
    case IC_GRID:
      fv=prop_get_float(e);
      if(param->grid!=fv) {
        if((param->grid=fv)) {
          param->icm_save=theme->icon_mode;
          theme->icon_mode=IC_AUTO; }
        else
          theme->icon_mode=param->icm_save;
        icon_list_layout_all(param->il); }
      break;

    case IC_TEXTSIZE:
      param->text_size=val; break;

    case IC_TEXTCOL:
      param->text_colour=val; break;
    case IC_TEXTALPHA:
      param->text_alpha=val; break;

    case IC_DSCOL:
      param->ds_colour=val; break;
    case IC_DSALPHA:
      param->ds_alpha=val; break;

    case IC_SELCOL:
      param->sel_colour=val; break;
    case IC_SELALPHA:
      param->sel_alpha=val; break;

    case IC_DSXOFFS:
      param->ds_x_offset=val; break;
    case IC_DSYOFFS:
      param->ds_y_offset=val; break;

    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld/#%06lx, %p)\n",key,val,val,data);

  return NOIR_SUCC; }



/**
 * listen for changes in our string-properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int ic_str_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  char          *val=prop_get_string(e);
  const char    *key=prop_get_key(e);
  ic_param      *param=(ic_param *)data;

  if(val) {
    switch(tag) {
      case IC_TEXTFONT:
        if(param->text_font)
          free(param->text_font);
        param->text_font=strdup(val);
        noir_sess_redraw_files(param->il->sess);
        break;
      default:
        E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
        return NOIR_FAIL; }

    E(1,"(%s::=%s, %p)\n",key,val,data);
    return NOIR_SUCC; }

  return NOIR_IGNORED; }






/******************************************************************************/
/* icon-factory */
/****************/



/**
 * create elements of an icon.
 * @return the first element of a linked list of icon-elements.
 * @param il - the icon-list to create the icon in
 * @param icon - the icon the elements are intended for
 * @param thumb - the path to a thumb-nailed image of the portrayed file's contents, or NULL
 */
icon_el *icon_els_create(icon_list *il,icon *icon,char *thumb) {
  noir_theme  *theme=(noir_theme *)il->theme;
  ic_param    *param=NULL;
  icon_el     *el=NULL,*ic;
  Evas        *evas=get_evas_from_iconlist(il);
  Evas_Object *o=NULL;
  Evas_Coord   _w,_h,_x,_y;
  int          w_,h_,wt=0,ht=0,tx=0,ty=0,iw,ih,err,fs;
  int          r,g,b;
  float        gw;
  char        *fm;
  file_info   *fi=(file_info *)(icon?icon->user2:NULL);

  icon->state&=~IE_THMB;

  if((o=evas_thumbnail_dead(theme,evas,fi))) {
    icon->state|=IE_THMB;
    evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
    w_=_w; h_=_h; }

  if(!o) {
    if(!icon->name||!*icon->name)
      E(0,"ic_alignbelow: cannot thumb-nail, and no fallback icon given...\n");
    else {
      o=evas_object_image_add(evas);
      evas_object_image_file_set(o,icon->name,NULL);
      if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
        E(0,"ic_alignright: error #%d loading \"%s\"...\n",err,icon->name);
        evas_object_del(o);
        o=NULL; }
      if(o) {
        evas_object_image_size_get(o,&w_,&h_);
        evas_object_resize(o,w_,h_);
        evas_object_image_fill_set(o,0,0,w_,h_); }}}

  if(!(param=theme->icon_data)) {
    if(!(theme->icon_data=param=malloc(sizeof(ic_param)))) {
      E(0,"out of memory!?\n");
      return NULL; }

    memset(param,0,sizeof(ic_param));

    param->il=il;

    prop_bag_prop_default(theme->data,"icon-font",             "verase.ttf",0,0,0);
    prop_bag_prop_default(theme->data,"icon-font-size",        "20",3,128,1);
    prop_bag_prop_default(theme->data,"icon-text-colour",      "#ffffff",0,0,0);
    prop_bag_prop_default(theme->data,"icon-text-alpha",       "179",0,255,1);
    prop_bag_prop_default(theme->data,"icon-selection-colour", "#00677f",0,0,0);
    prop_bag_prop_default(theme->data,"icon-selection-alpha",  "127",0,255,1);
    prop_bag_prop_default(theme->data,"icon-shadow-colour",    "#000000",0,0,0);
    prop_bag_prop_default(theme->data,"icon-shadow-alpha",     "255",0,255,1);
    prop_bag_prop_default(theme->data,"icon-shadow-x-offset",  "2",-64,64,1);
    prop_bag_prop_default(theme->data,"icon-shadow-y-offset",  "2",-64,64,1);
    prop_bag_prop_default(theme->data,"icon-list-grid",        "-1.9",-4,128,0.125);

    prop_bag_prop_listen(theme->data,LISTENER,"icon-font",            ic_str_listener,IC_TEXTFONT,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-font-size",       ic_num_listener,IC_TEXTSIZE,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-text-colour",     ic_num_listener,IC_TEXTCOL,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-text-alpha",      ic_num_listener,IC_TEXTALPHA,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-selection-colour",ic_num_listener,IC_SELCOL,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-selection-alpha", ic_num_listener,IC_SELALPHA,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-colour",   ic_num_listener,IC_DSCOL,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-alpha",    ic_num_listener,IC_DSALPHA,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-x-offset", ic_num_listener,IC_DSXOFFS,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-y-offset", ic_num_listener,IC_DSYOFFS,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-list-grid",       ic_num_listener,IC_GRID,param); }

  iw=w_;
  ih=h_;

  ICON_EL_PRE((IE_DFL|IE_KBD|IE_SEL),0,0,(theme->icon_height-h_)/2.0);
  ic=el;

  if(!theme->icon_height) {
#ifdef NOIR_DEBUG
    E(1,"got height (%d) from %s\n",ih,thumb?thumb:icon->name);
#endif
    theme->icon_height=ih; }
  if(!theme->icon_width) {
#ifdef NOIR_DEBUG
    E(1,"got width  (%d) from %s\n",iw,thumb?thumb:icon->name);
#endif
    theme->icon_width=iw; }

  if((gw=param->grid)<0)
    gw=-gw*theme->icon_width;

  ty=theme->icon_height;
  fs=param->text_size;

  if((fm=strchr(icon->label,'\n'))&&(strlen(fm)>=3))
    strcpy(fm,"...");

  long_to_rgb(param->text_colour,&r,&g,&b);
  o=evas_object_text_add(evas);
  evas_object_text_font_set(o,param->text_font,fs);
 try_text:
  evas_object_text_text_set(o,icon->label);
  evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
  if(param->grid) {
    w_=gw;
    if(gw<_w) {
      size_t l=strlen(icon->label);
      if(l>3) {
        strcpy(icon->label+l-4,"...");
        goto try_text; }}}

  if(w_>_w)
    tx=((w_-_w)/2.0);
  evas_object_color_set(o,r,g,b,255);
  ICON_EL_PRE((IE_SEL|IE_KBD),20,tx,ty);

  o=evas_object_text_add(evas);
  evas_object_text_font_set(o,param->text_font,fs);
  evas_object_text_text_set(o,icon->label);
  evas_object_color_set(o,r,g,b,param->text_alpha);
  ICON_EL_PRE(IE_DFL,20,tx,ty);

  long_to_rgb(param->ds_colour,&r,&g,&b);
  o=evas_object_text_add(evas);
  evas_object_text_font_set(o,param->text_font,fs);
  evas_object_text_text_set(o,icon->label);
  evas_object_color_set(o,r,g,b,param->ds_alpha);
  ICON_EL_PRE((IE_SEL|IE_KBD),19,tx+param->ds_x_offset,ty+param->ds_y_offset);



/* For the image portion to be of the same size, the icon-mode must be
   "auto" (see config-files).  The icon-factory must then simply make
   sure labels are all the same size.  An invisible evas box is put
   underneath the icon's image.  If the text for the label is wider
   than this, it is cut off.  This is marked with the ellipsis.  If
   the text is shorter, the wider (invisible) box will keep the
   overall bounding box of the icon wide, the same width as all the
   other icons. */

  if(param->grid) {
    o=evas_object_rectangle_add(evas);
    evas_object_resize(o,gw,theme->icon_height);
    evas_object_color_set(o,55,55,55, 0);
    ICON_EL_PRE(IE_DFL,10,0,0);
    el->tag=ET_MISC; }



  icon->w=wt;
  icon->h=ht;

  ic->xo=(int)((wt-ic->w)/2);

#ifndef GHOST
  long_to_rgb(param->sel_colour,&r,&g,&b);
  o=evas_object_gradient_add(evas);
  evas_object_resize(o,ic->w+4,ic->h+4);
  evas_object_gradient_color_add(o, r,g,b,       0,                 8);
  evas_object_gradient_color_add(o, 2*r,2*g,2*b, param->sel_alpha, 16);
  evas_object_gradient_angle_set(o,150);
  ICON_EL_PRE(IE_SEL,-1,ic->xo-2,ic->yo-2);
#else
  long_to_rgb(param->sel_colour,&r,&g,&b);
  o=evas_object_image_add(evas);
  evas_object_image_file_set(o,icon->name,NULL);
  evas_object_resize(o,iw,ih);
  evas_object_image_fill_set(o,0,0,iw,ih);
  evas_object_color_set(o, r,g,b, param->sel_alpha);
  ICON_EL_PRE(IE_SEL,-10,ic->xo,ic->yo);
#endif

  o=evas_object_gradient_add(evas);
  ICON_EL_PRE(IE_KBD,-10,0,0);
  evas_object_resize(o,wt,ht);
  evas_object_gradient_color_add(o,  2*r,2*g,2*b, param->sel_alpha, 16);
  evas_object_gradient_color_add(o,  r,g,b,       0,                 8);
  evas_object_gradient_angle_set(o,150);

  return el; }
