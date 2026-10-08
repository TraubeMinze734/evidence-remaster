/** @file  ia_creepy.c
 *  @brief plugin that animates an icon (when starting a program, opening a
 *         folder, or somesuch).  which action is assigned which animation is
 *         decided in the theme.
 *
 * idea and original code by creepy,
 * streamlined (using secret evidence hacks : ) by Azundris
 */

#include <string.h>
#include <Evas.h>
#include <stdio.h>
#include <sys/stat.h>

#include "debug.h"
#include "icon_list.h"
#include "ia_common.h"
#include "evas_macros.h"
#include "model.h"



#define ABS(x)     (((x)<0)?-(x):x)
#define STEPS      100
#define RAISE_BY   2000
#define RAISE_ELTS (ET_EMBLEM_IMAGE|ET_EMBLEM_SHADOW|ET_EMBLEM_OTHER|ET_ICON_IMAGE|ET_ICON_SHADOW|ET_ICON_OTHER)



/**
 * @param il - the list that's affected
 * @param i - the icon we have to highlight
 *
 * we can get the evas in question from the icon_list; also, this allows
 * us to mess with icons other than the one selected (fade them out, or
 * somesuch).
 */
void animation(icon_list *il,icon *i) {
  Evas       *evas=get_evas_from_iconlist(il);
  int         s=STEPS,d=0;                                  /* we done yet? */
  double      mx=(il->vis_w-i->w)/2,my=(il->vis_h-i->h)/2,  /* where to? */
              xx=i->x,yy=i->y,                              /* where from? */
              dx,dy,f;                                      /* how fast? */

  /* raise icon so it covers others when growing */
  ani_icon_raise(i,RAISE_BY,RAISE_ELTS);

  /* work out how to move (to the centre of the window).  we always want the
     same number of steps, independent from angle and distance. */
  dx=mx-xx;
  dy=my-yy;
  if(ABS(dx)>ABS(dy)) {
    f=dy?(dx/ABS(dy)):1;
    dx=dx/s;
    dy=dy/ABS(f*s); }
  else {
    f=dx?(dy/ABS(dx)):1;
    dy=dy/s;
    dx=dx/ABS(f*s); }

/*  E(0,"%3.2f/%3.2f => %3.2f/%3.2f, step %3.2f/%3.2f\n",xx,yy,mx,my,dx,dy); */

  /* move it! */
  s=d=0;
  do {
    xx+=dx;
    yy+=dy;
    icon_move(i,xx,yy);
    if(d++>3) {
      d=0;
      evas_render(evas); }
  } while(++s<STEPS);

  /* once in the centre of the window, grow icon. if it's not a directory,
     fade icon while doing so. */
  { float c=0,m=100,n=0,z=1,o,e;
    int   is_dir;

    /* work out whether icon represents a directory. */
    if((is_dir=(i->user1)?S_ISDIR(posixmd_get_mode((posixmd *)i->user1)):1)) {
      o=m/2.75;
      e=(m/3)*2; }
    else {
      o=m/2;
      e=m; }

    /* grow (and maybe fade) the icon. */
    d=0;
    while(n++<e) {
      ani_icon_pulse(i->els,evas,c,is_dir?1.0:((m-n)/m),xx,yy,i->state);
      if(d++&1)
        evas_render(evas);  /* update window */
      if(n>o)
        z=2*(n-o)+2;      /* "explosive decompression" */
      c+=z; }             /* make bigger next time around */

    /* reset to defaults. (needed if it wasn't a directory.) */
    ani_icon_pulse(i->els,evas,0,1,xx,yy,i->state); }
  ani_icon_raise(i,-RAISE_BY,RAISE_ELTS); }
