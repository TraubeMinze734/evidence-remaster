/** @file  bg_ebg.c
 *  @brief background-handler using the (deprecated) e-bg format
 *
 * ebg was the format used by the first prototype of enlightenment DR17.
 * it lets you compose a background out of individual items such as gradients,
 * images etc.  each item can be placed/resized to fit the current resolution.
 * to compile this plugin, only edb is required, but to create your own
 * ebgs (.bg.db), you'll need ebony, the graphical ebg-editor.
 */

#include "config.h"

#include <Evas.h>
#include <stdio.h>

#include "debug.h"
#include "evas_macros.h"
#include "theme.h"
#include "icon_list.h"

#ifdef HAVE_EDB

#  include "Ebg.h"



/**
 * callback: view was scrolled, update background
 * @param il  the icon-list (contains coordinates and pointer to background)
 */
void bg_scroll(void *il) {
  e_bg_move(((E_Background)(((icon_list *)il)->bg)),((icon_list *)il)->sx,((icon_list *)il)->sy); }



/**
 * callback: update background (window resized or moved, backdrop changed, ...)
 * moving is only relevant if the plugin uses pseudo-transparency
 * @param il  the icon-list (contains pointer to background)
 * @param x   horizontal position
 * @param y   vertical position
 * @param w   width
 * @param h   height
 */
void bg_upd(void *il,int x,int y,int w,int h) {
  E_Background bg=((icon_list *)il)->bg;
  e_bg_resize(bg,w,h);
  e_bg_show(bg); }



/**
 * set an ebg on an evas-canvas
 * @return the newly set E_Background
 * @param evas     the canvas to set the background on
 * @param edbname  the name of the file containing the ebg db
 */
static E_Background set_ebg(Evas *evas,char *edbname) {
  E_Background new_bg;

  if(!(new_bg=e_bg_load(edbname))) {
    E(0,"failed to load \"%s\"...\n",edbname);
    return NULL; }

  e_bg_add_to_evas(new_bg,evas);
  e_bg_set_scroll(new_bg,0,0);
  e_bg_set_layer(new_bg,-5000);

  return new_bg; }
#endif



/**
 * set a background on a view
 * @return the newly set background object (il->bg)
 * @param t   the current theme in this session
 * @param il  the icon-list
 * @param fp  the name of the ebg-file
 */
void *bg_new(struct _noir_theme *t,icon_list *il,char *fp) {
  int            ok=0;

  if(!il)
    return NULL;

#ifdef HAVE_EDB
  { size_t l=strlen(fp);
    if((l>6)&&(!strcmp(".bg.db",&fp[l-6]))) {
      E_Background en;
      if((en=set_ebg(get_evas_from_iconlist(il),fp))) {
        if(il->bg)
          e_bg_free(il->bg);
        ok=1;
        il->bg=en; }
      else
        E(0,"cannot set \"%s\"!?\n",fp); }
    else
      E(0,"\"%s\" does not look like a background database?\n",fp); }
  if(!ok) {
    if(il->bg)
      e_bg_free(il->bg);
    il->bg=NULL; }
#else
  E(0,"built without libebg, how utterly pointless! :-S\n");
  il->bg=NULL;
#endif
  return il->bg; }



/**
 * destruct background
 * @param data  the background to dest
 */
void bg_dst(void **data) {
  if(data&&*data) {
#ifdef HAVE_EDB
    e_bg_free((E_Background) *data);
#endif
    *data=NULL; }}

#ifndef HAVE_EDB
#  warning building the ebg-type background-plugin in the absence of libedb is kinda pointless...
#endif
