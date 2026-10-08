/** @file  ia_creepology.c
 *  @brief plugin that animates an icon (when starting a program, opening a
 *         folder, or somesuch).  which action is assigned which animation is
 *         decided in the theme.
 *
 * idea and original code by creepy,
 * streamlined (using secret evidence hacks : ) by Azundris
 */

#include <Evas.h>
#include "icon_list.h"
#include "ia_common.h"
#include "evas_macros.h"

#define SPEED     2
#define SPEED2   40
#define SELECTED 20
#define OTHER    32

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
  int    x=i->x-il->sx,
         y=i->y-il->sy,
         c=0,n=0,s=SELECTED;
  Evas  *evas=get_evas_from_iconlist(il);

  while(c<il->width) {
    if(n<(16*SPEED)) {
      n+=SPEED;
      /* move away all icons except the selected one */
      for(j=il->icons;j;j=j->next) {                     /* iterate over icons */
        if((j!=i)&&icon_list_icon_visible(il,j)) {       /* if visible... */
          if(j->x+(j->w/2)<il->vis_w/2) {
            if(j->y+(j->h/2)<il->vis_h/2)
              icon_move(j,j->x-=OTHER,j->y-=OTHER);
            else
              icon_move(j,j->x-=OTHER,j->y+=OTHER); }
          else {
            if(j->y+(j->h/2)<il->vis_h/2){
              icon_move(j,j->x+=OTHER,j->y-=OTHER);}
            else
              icon_move(j,j->x+=OTHER,j->y+=OTHER); }}}}
    else
      n+=SPEED2;

    /* center and grow the Chosen One. */
    if(((il->vis_w/2)-(x+(i->w/2)))>s)
      x+=s;
    else if(((x+i->w/2)-(il->vis_w/2))>s)
      x-=s;
    if(((il->vis_h/2)-(y+i->h/2))>s)
      y+=s;
    else if(((y+i->h/2)-(il->vis_h/2))>s)
      y-=s;
    ani_icon_pulse(i->els,evas,c,1,x,y,i->state);

    /* redraw the canvas */
    evas_render(evas);

    /* next round -- increase size for growing icon */
    c+=n; }}
