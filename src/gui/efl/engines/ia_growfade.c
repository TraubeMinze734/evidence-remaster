/** @file  ia_growfade.c
 *  @brief plugin that animates an icon (when starting a program, opening a
 *         folder, or somesuch).  which action is assigned which animation is
 *         decided in the theme.
 */

#include <Evas.h>
#include "icon_list.h"
#include "ia_common.h"
#include "evas_macros.h"



/**
 * fade out a single icon.
 * @param el - the icon-element to fade
 * @param alpha - the opaqueness (0.0..1.0)
 * @param s - affect only elements that have one of the states in s
 */
static inline void ani_icon_fade(icon_el *el,float alpha,icon_state s) {
  /* an icon usually consists of several parts -- a text-label, an image,
     maybe a read-only emblem ("stop sign")...  hence, we get a list of
     parts in el that we now iterate over. */
  while(el) {
    if(el->state&s)
      evas_object_color_set((Evas_Object *)el->data,255,255,255,alpha*255);
    el=el->next; }}



/**
 * @param il - the list that's affected
 * @param i - the icon we have to highlight
 *
 * we can get the evas in question from the icon_list; also, this allows
 * us to mess with icons other than the one selected (fade them out, or
 * somesuch).
 */
void animation(icon_list *il,icon *i) {
  icon  *j;
  int    x=i->x-il->sx,y=i->y-il->sy;
  float  c=0,m=32,n=0;
  Evas  *evas=get_evas_from_iconlist(il);

  while((n+=4)<m) {

    /* fade out all icons except the selected one. */
    for(j=il->icons;j;j=j->next) {                     /* iterate over icons */
      if((j!=i)&&icon_list_icon_visible(il,j))         /* if visible... */
        ani_icon_fade(j->els,(m-n)/m,j->state); }      /* ...fade. */

    /* grow+fade the Chosen One. */
    ani_icon_pulse(i->els,evas,c,(m-n)/m,x,y,i->state);

    /* redraw the canvas */
    evas_render(evas);

    /* next round -- increase size for growing icon. */
    c+=n; }

#if 0
  for(j=il->icons;j;j=j->next) {                     /* iterate over icons */
    if((j!=i)&&icon_list_icon_visible(il,j))         /* if visible... */
      ani_icon_fade(j->els,1.0,j->state); }          /* ...set opaque. */
  ani_icon_pulse(i->els,evas,0,1,x,y,i->state);
  evas_render(evas);
#endif
}
