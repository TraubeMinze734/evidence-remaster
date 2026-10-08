/* routines used by several of the ia_*.c (icon-animation plugins).
   this is used by several of the actual plugins, so no code that is
   used only by a single plug-in should end up in here. */

#include <Evas.h>
#include "icon_list.h"



/* resize and/or fade icon.
   alpha: 0.0 ... 1.0
   c:     0...oo        (grow by c pixels) */
void ani_icon_pulse(icon_el *el,Evas *evas,float c,float alpha,int x,int y,icon_state s);


/* tint image-parts of an icon to colour {Red/Green/Blue} */
void ani_icon_recolourize(Evas *evas,icon *i,int r,int g,int b);



/* raise elements 't' of icon 'i' by 'f' (negative values to lower) */
void ani_icon_raise(icon *i,int f,element_tag t);
