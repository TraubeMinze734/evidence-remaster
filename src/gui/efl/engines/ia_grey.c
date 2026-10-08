/** @file  ia_grey.c
 *  @brief plugin that animates an icon (when starting a program, opening a
 *         folder, or somesuch).  which action is assigned which animation is
 *         decided in the theme.
 */

#include <string.h>
#include <Evas.h>
#include <stdlib.h>

#include "icon_list.h"
#include "ia_common.h"
#include "evas_macros.h"

#define RND(lessthan)  (((float)lessthan)*rand()/RAND_MAX)

#define CS 4
#define L  96
#define H  255



/**
 * @param il - the list that's affected
 * @param i - the icon we have to highlight
 *
 * we can get the evas in question from the icon_list; also, this allows
 * us to mess with icons other than the one selected (fade them out, or
 * somesuch).
 */
void animation(icon_list *il,icon *i) {
  Evas  *evas=get_evas_from_iconlist(il);
  int m=3,n,c,cs;

  /* random shading foo, on selected icon only */
  while(m--) {
    n=400;
    c=255;
    cs=CS;
    while(n--) {
      if((c-=cs)<L) {
        c=L;
        cs*=-1; }
      else if(c>H)
        c=H;
      ani_icon_recolourize(evas,i,c,c,c);
      evas_render(evas); }}

  /* change back to original colours when done. */
  ani_icon_recolourize(evas,i,255,255,255);
  evas_render(evas); }
