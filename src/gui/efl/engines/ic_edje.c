/** @file  ic_edje.c
 *  @brief background-handler using the (new) edje format
 *
 * edje is the format used by the current prototype of enlightenment DR17.
 * it lets you compose a background out of individual items such as gradients,
 * images etc.  each item can be placed/resized to fit the current resolution.
 * to compile this plugin, only libedje is required, but to create your own
 * edjes (.eet), you'll need edje_cc.
 *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * With edjes that send a lot of (timer-/animation-) signals, you will notice
 * a certain (CPU-) load.  This is believed to decrease noticably once we drop
 * GTK2.  The edje plugin is provided "early" in the hopes that it may
 * facilitate theming; load created by themes can only get lighter in future
 * versions. 
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * by Azundris
 */

#include <Evas.h>
#include <Edje.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "config.h"
#include "evas_macros.h"
#include "icon_list.h"
#include "session.h"

#define  BG_NAME "Bg"
#define  FG_NAME "Fg"



static int edje_init_p=-1;






/**
 * edje-callback: update view
 */
static void debug_cb(void *data,Evas_Object *o,const char *sig,const char *src) {
  const char *name=evas_object_name_get(o);
  Evas_Coord  x,y,w,h;

  const char *hide[]={ "mouse,move",
                       "mouse,in", "mouse,out",
                       "mouse,up", "mouse,down",
                       "program,start", "program,stop",
                       "hide", "show" };
  int         hn=sizeof(hide)/sizeof(char*),hidep=0,i;

  if(!src||!strlen(src)||!strcmp(src,"seconds"))   /* example-specific, this will go away */
    hidep=1;

  else if(sig) {
    for(i=0;(i<hn)&&!hidep;i++)
      if(!strncmp(hide[i],sig,strlen(hide[i])))
        hidep=1; }
  else
    hidep=1;

  evas_object_geometry_get(o,&x,&y,&w,&h);
  if(!hidep)
    E(0,"%s -> %s: \"%s\"  (%d,%d,%dx%d)\n",src?src:"???",name?name:"???",sig?sig:"???",x,y,w,h);
  if(strncmp(src,"mouse,",6))
    icon_view_render_updates(data); }



/**
 * callback: view was scrolled, update background
 * @param il  the icon-list (contains coordinates and pointer to background)
 */
void bg_scroll(void *il) {
  icon_el     *el=((icon_list *)il)->bg;

  while(el) {
    evas_object_move((Evas_Object *)el->data,((icon_list *)il)->sx,((icon_list *)il)->sy);
    el=el->next; }}



/**
 * callback: update background (window resized or moved, backdrop changed, ...)
 * moving is relevant since the plugin uses pseudo-transparency!
 * @param i  the icon-list (contains pointer to background)
 * @param x  horizontal position
 * @param y  vertical position
 * @param w  width
 * @param h  height
 */
void bg_upd(void *i,int x,int y,int w,int h) {
  icon_list   *il=((icon_list *)i);
  icon_el     *el=il->bg;
  Evas_Object *o;
  Evas        *evas=get_evas_from_iconlist(il);

  while(el) {
    o=(Evas_Object *)el->data;
    evas_object_hide(o);
    /* elements may be non-image objects (edje groups); image_fill_set()
       is image-only and spams eina-safety errors */
    if(!strcmp(evas_object_type_get(o),"image"))
      evas_object_image_fill_set(o,0,0,il->vis_w,il->vis_h);
    evas_object_resize(o,il->vis_w,il->vis_h);
    evas_object_show(o);
    el=el->next; }

  evas_render(evas); }



/**
 * load a part from an edje
 * @return a smart-object containing the requested part
 * @param evas       the evas to create the part on
 * @param w          requested width
 * @param h          requested height
 * @param edje_name  the (file-) name of the edje
 * @param part_name  the name of the part
 */
static Evas_Object *bg_load_part_from_edje(Evas *evas,int w,int h,const char *edje_name,const char *part_name) {
  int          err;
  Evas_Object *o=edje_object_add(evas);

  if(o) {
    edje_object_file_set(o,edje_name,part_name);

    if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
      E(0,"error #%d loading \"%s\" from \"%s\"...\n",err,part_name,edje_name);
      evas_object_del(o);
      o=NULL; }

    if(o) {
      evas_object_move(o,0,0);
      evas_object_resize(o,w,h); }}
  else
    E(0,"evas_object_image_add() failed!?\n");
  return o; }



/**
 * set a background on a view
 * @return the newly set background object (il->bg)
 * @param t       the current theme in this session
 * @param il      the icon-list
 * @param bg_pic  the name of the image for the background (any format evas loads)
 */
void *bg_new(void *t,icon_list *il,char *bg_pic) {
  Evas        *evas;
  Evas_Object *o;
  icon_el     *el=NULL;

  if(edje_init_p<1) {
    char *edje_state=prop_get_string(prop_bag_prop_get(props_global,"feature-edje"));
    if(!edje_state||strcmp(edje_state,"engaged")) {
      edje_init();
      prop_bag_prop_set(props_global,"feature-edje","engaged"); }
    edje_init_p=1; }

  if(!il)
    return NULL;

  if(!bg_pic||!*bg_pic) {
    E(0,"no wallpaper given?\n");
    return NULL; }

  if(!(evas=get_evas_from_iconlist(il))) {
    E(0,"no evas set on icon-list!?\n");
    return NULL; }

  edje_frametime_set(1.0/30.0);

  if((o=bg_load_part_from_edje(evas,il->vis_w,il->vis_h,bg_pic,BG_NAME))) {
    evas_object_name_set(o,BG_NAME);
    el=icon_el_new(IE_ALL,0,0,il->vis_w,il->vis_h,o,el);
    evas_object_layer_set(o,-5000);
    evas_object_show(o);

    E(0,"%s::%s loaded.\n",bg_pic,BG_NAME);

    edje_object_signal_callback_add(o,"*","*",debug_cb,il);

    edje_object_part_text_set(o,"cwd",noir_sess_get_cwd(il->sess)); }



  if((o=bg_load_part_from_edje(evas,il->vis_w,il->vis_h,bg_pic,FG_NAME))) {
      evas_object_name_set(o,FG_NAME);
      el=icon_el_new(IE_ALL,0,0,il->vis_w,il->vis_h,o,el);
      evas_object_layer_set(o,15000);
      evas_object_show(o);

      edje_object_signal_callback_add(o,"*","*",debug_cb,il);

      E(0,"%s::%s loaded.\n",bg_pic,FG_NAME); }

  return il->bg=el; }



/**
 * destruct background
 * @param data  the background to dest
 */
void bg_dst(void **data) {
  if(data&&*data) {
    icon_el_dst_all(*data);
    *data=NULL; }}
