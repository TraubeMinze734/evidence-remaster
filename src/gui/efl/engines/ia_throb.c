/** @file  ia_throb.c
 *  @brief plugin that animates an icon (when starting a program, opening a
 *         folder, or somesuch).  which action is assigned which animation is
 *         decided in the theme.
 */

#include <Evas.h>
#include "icon_list.h"
#include "ia_common.h"
#include "evas_macros.h"

#define STEP 3



/**
 * @param il - the list that's affected
 * @param i - the icon we have to highlight
 *
 * we can get the evas in question from the icon_list; also, this allows
 * us to mess with icons other than the one selected (fade them out, or
 * somesuch).
 */
void animation(icon_list *il,icon *i) {
  int    x=i->x-il->sx,y=i->y-il->sy,j=0;
  Evas  *evas=get_evas_from_iconlist(il);

  for(j=0;j<3;j++) {
    float  c=0,m=10*STEP,n=0;

    /* grow icon. */
    while((n+=STEP)<m) {
      ani_icon_pulse(i->els,evas,c,(m-(n/STEP))/m,x,y,i->state);
      evas_render(evas);
      c+=n; }

    /* shrink icon. */
    while((n-=STEP)>0) {
      c-=n;
      ani_icon_pulse(i->els,evas,c,(m-(n/STEP))/m,x,y,i->state);
      evas_render(evas); }}

  /* reset to defaults. */
  ani_icon_pulse(i->els,evas,0,1,x,y,i->state);
  evas_render(evas); }
