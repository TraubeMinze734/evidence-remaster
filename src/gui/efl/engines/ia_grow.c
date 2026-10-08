/** @file  ia_grow.c
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
  float  c=0,m=100,n=0,o=m/2,z=1;
  Evas  *evas=get_evas_from_iconlist(il);

  /* grow+fade the Chosen One. */
  while(n++<m) {
    ani_icon_pulse(i->els,evas,c,(m-n)/m,x,y,i->state);
    evas_render(evas);
    if(n>o)
      z=2*(n-o)+2;  /* "explosive decompression" */
    c+=z; }         /* make bigger next time around */

  /* reset to defaults. */
  ani_icon_pulse(i->els,evas,0,1,x,y,i->state);
  evas_render(evas); }
