/** @file  bg_tile.c
 *  @brief background-handler that uses tiled images
 *
 * loads whatever evas loads (.png and .jpg for now)
 */

#include <Evas.h>
#include <stdio.h>

#include "debug.h"
#include "config.h"
#include "evas_macros.h"
#include "icon_list.h"



/**
 * callback: view was scrolled, update background
 * @param data  the icon-list (contains coordinates and pointer to background)
 */
void bg_scroll(void *data) {
  icon_list *il=(icon_list *)data;
  evas_object_move((Evas_Object*)il->bg,il->sx,il->sy); }



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
  icon_list *il=((icon_list *)i);
  Evas_Object *bg=il->bg;
  evas_object_resize(bg,il->vis_w,il->vis_h); }



/**
 * set a background on a view
 * @return the newly set background object (il->bg)
 * @param t       the current theme in this session
 * @param il      the icon-list
 * @param bg_pic  the name of the image for the background (any format evas loads)
 */
void *bg_new(void *t,icon_list *il,char *bg_pic) {
  if(!il)
    return NULL;

  if(!bg_pic||!*bg_pic) {
    E(0,"no wallpaper given?\n");
    return NULL; }

  { int          err,w_,h_;
    Evas        *evas=get_evas_from_iconlist(il);
    Evas_Object *o=evas_object_image_add(evas);

    if(o) {
      evas_object_image_file_set(o,bg_pic,NULL);

      if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
        E(0,"error #%d loading %s\n",err,bg_pic);
        evas_object_del(o);
        o=NULL; }

      if(o) {
        evas_object_image_size_get(o,&w_,&h_);
        evas_object_image_fill_set(o,0,0,w_,h_);
        evas_object_move(o,0,0);
        evas_object_resize(o,il->vis_w,il->vis_h);
        evas_object_layer_set(o,-5000);
        evas_object_show(o); }}
    else
      E(0,"evas_object_image_add() failed!?\n");
    il->bg=o; }
  return il->bg; }



/**
 * destruct background
 * @param data  the background to dest
 */
void bg_dst(void **data) {
  if(data&&*data) {
    evas_object_del((Evas_Object *)*data);
    *data=NULL; }}
