/** @file  ia_basic.c
 *  @brief plugin that animates an icon (when starting a program, opening a
 *         folder, or somesuch).  which action is assigned which animation is
 *         decided in the theme.
 */

#include <Evas.h>
#include "icon_list.h"
#include "ia_common.h"
#include "evas_macros.h"



/**
 * @param il - the list that's affected
 * @param i - the icon we have to highlight
 *
 * we can get the evas in question from the icon_list; also, this allows
 * us to mess with icons other than the one selected (fade them out, or
 * somesuch).
 */
void animation(icon_list *il,icon *i) {
  int    x=i->x-il->sx,y=i->y-il->sy;
  float  c=0,m=32,n=0;
  Evas  *evas=get_evas_from_iconlist(il);

  /* grow icon. */
  while((n+=1)<m) {                                      /* do this ~m times */
    ani_icon_pulse(i->els,evas,c,(m-n)/m,x,y,i->state);  /* paint icon. */
    evas_render(evas);
    c+=n; }                               /* make icon bigger in next around */

  /* reset to default. */
  ani_icon_pulse(i->els,evas,0,1,x,y,i->state);
  evas_render(evas); }
