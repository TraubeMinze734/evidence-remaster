/** @file  ic_alignright.c
 *  @brief plugin that creates an icon.
 *
 * text-labels appear to the right of the icon's image.
 * long text-labels are shown 'as is'.
 *
 * items that evidence's thumbnailer was able to generate previews for will
 * be show a preview in lieu of a datafile-icon.
 *
 * the factory installs a variety of properties; see below.
 * these properties can be set in themes, or from the typebuffer, e.g.
 * 'icontextcolour #ff00ff
 */

#include <stdio.h>
#include <Evas.h>
#include <string.h>
#include <stdlib.h>

#include "session.h"    /* noir_sess_redraw_files() */
#include "config.h"
#include "debug.h"
#include "errors.h"
#include "theme.h"
#include "evas_macros.h"
#include "icon_list.h"



#define LISTENER "iconfactory{alignright}"

typedef enum {
  IC_TEXTFONT,  IC_TEXTSIZE,
  IC_TEXTCOL,   IC_DSCOL,   IC_SELCOL,
  IC_TEXTALPHA, IC_DSALPHA, IC_SELALPHA,
  IC_DSXOFFS,   IC_DSYOFFS } ic_tag;

typedef struct _ic_param {
  icon_list *il;
  char *text_font;
  int   text_size;
  long  text_colour,ds_colour,sel_colour;
  int   text_alpha, ds_alpha, sel_alpha;
  int   ds_x_offset,ds_y_offset;
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
  ic_param      *param=(ic_param *)data;

  switch(tag) {
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
  icon_el     *el=NULL;
  Evas        *evas=get_evas_from_iconlist(il);
  Evas_Object *o=NULL;
  Evas_Coord   _w,_h,_x,_y;
  int          w_,h_,wt=0,ht=0,tx=0,err;
  int          fs,r,g,b;
  file_info   *fi=(file_info *)(icon?icon->user2:NULL);

  icon->state&=~IE_THMB;

  if((o=evas_thumbnail_dead(theme,evas,fi))) {
    icon->state|=IE_THMB;
    evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
    w_=_w; h_=_h; }

  if(!o) {
    if(!icon->name||!*icon->name)
      E(0,"cannot thumb-nail, and no fallback icon given...\n");
    else {
      o=evas_object_image_add(evas);
      evas_object_image_file_set(o,icon->name,NULL);
      if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
        E(0,"error #%d loading \"%s\"...\n",err,icon->name);
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

    prop_bag_prop_default(theme->data,"icon-font",            "andover.ttf",0,0,0);
    prop_bag_prop_default(theme->data,"icon-text-colour",     "#7f7f7f",0,0,0);
    prop_bag_prop_default(theme->data,"icon-text-alpha",      "79",0,255,1);
    prop_bag_prop_default(theme->data,"icon-selection-colour","#c8c8c8",0,0,0);
    prop_bag_prop_default(theme->data,"icon-selection-alpha", "100",0,255,1);
    prop_bag_prop_default(theme->data,"icon-shadow-colour",   "#ffffff",0,0,0);
    prop_bag_prop_default(theme->data,"icon-shadow-alpha",    "255",0,255,1);
    prop_bag_prop_default(theme->data,"icon-shadow-x-offset", "-3",-64,64,1);
    prop_bag_prop_default(theme->data,"icon-shadow-y-offset", "-2",-64,64,1);

    prop_bag_prop_listen(theme->data,LISTENER,"icon-font",             ic_str_listener,IC_TEXTFONT,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-text-colour",      ic_num_listener,IC_TEXTCOL,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-text-alpha",       ic_num_listener,IC_TEXTALPHA,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-selection-colour", ic_num_listener,IC_SELCOL,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-selection-alpha",  ic_num_listener,IC_SELALPHA,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-colour",    ic_num_listener,IC_DSCOL,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-alpha",     ic_num_listener,IC_DSALPHA,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-x-offset",  ic_num_listener,IC_DSXOFFS,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-y-offset",  ic_num_listener,IC_DSYOFFS,param); }

  ICON_EL_PRE(IE_ALL,0,0,0);

  fs=h_;
  tx=wt;

  { char *fm;        /* undo text-wrap */
    while((fm=strchr(icon->label,'\n')))
      memmove(fm,fm+1,strlen(fm+1)+1); }

  if(!theme->icon_height) {
    E(1,"got height (%d) from %s\n",h_,thumb?thumb:icon->name);
    theme->icon_height=h_; }

  long_to_rgb(param->text_colour,&r,&g,&b);
  o=evas_object_text_add(evas);
  evas_object_text_font_set(o,param->text_font,fs);
  evas_object_text_text_set(o,icon->label);
  evas_object_color_set(o,r,g,b,param->text_alpha);
  ICON_EL_PRE(IE_DFL,20,tx,0);

  long_to_rgb(param->ds_colour,&r,&g,&b);
  o=evas_object_text_add(evas);
  evas_object_text_font_set(o,param->text_font,fs);
  evas_object_text_text_set(o,icon->label);
  evas_object_color_set(o,r,g,b,param->ds_alpha);
  ICON_EL_PRE(IE_SEL,20,tx,0);

  o=evas_object_text_add(evas);
  evas_object_text_font_set(o,param->text_font,fs);
  evas_object_text_text_set(o,icon->label);
  evas_object_color_set(o,r,g,b,200);
  ICON_EL_PRE(IE_KBD,20,tx+param->ds_x_offset,param->ds_y_offset);

  o=evas_object_text_add(evas);
  evas_object_text_font_set(o,param->text_font,fs);
  evas_object_text_text_set(o,icon->label);
  evas_object_color_set(o,0,0,0,199);
  ICON_EL_PRE(IE_KBD,30,tx,0);

  long_to_rgb(param->sel_colour,&r,&g,&b);
  o=evas_object_rectangle_add(evas);
  evas_object_resize(o,wt,ht);
  evas_object_color_set(o, r,g,b, param->sel_alpha);
  ICON_EL_PRE(IE_SEL,10,0,0);

  icon->w=wt;
  icon->h=ht;

  return el; }
