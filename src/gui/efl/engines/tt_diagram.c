/** @file  tt_diagram.c
 *  @brief plugin that creates a file-info "tooltip"
 *
 * this plugin creates tooltips as shown in
 * http://evidence.sourceforge.net/screenshots/tooltip2.jpg
 *
 * (if you are looking for a simple tooltip plugin to modify, you might
 * prefer tt_traditional and friends; this one is slightly more involved
 * because of the code that tries to decide which boxes should go where.)
 *
 * a lot of tunables exist as #defines and should be turned into properties
 * at some point. ###
 */

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <Evas.h>
#include <stdio.h>

#include "debug.h"
#include "evas_macros.h"
#include "icon_list.h"
#include "theme.h"
#include "model.h"



/* the usual suspects */
#define  RND(lessthan)  (((float)lessthan)*rand()/RAND_MAX)
#define  MAX(a,b)       ((a)>(b))?(a):(b)
#define  MIN(a,b)       ((a)<(b))?(a):(b)



struct _ttd {
  icon_list *il;
  icon      *ic;
  char      *file;
};



/*****************************************************************************/



/** define for funky thumbnails */
#define WITH_THUMBNAILS



/** valid colours are 0 and 255.
    choose any other value, and you're on your own. */
#define COL 255

/** string-representation of a value can not get longer than this */
#define LEN 24

/** draw lines for each text-line (1), not just for each text-block (0)
 *  instead, a function may be given to randomly select text-lines that will
 *  get a line-object.  the first text-line of a text-block always will. */
#define LINES_FOR_ALL (RND(3)<1)

/** define for all values to be aligned (same X-cordinate) */
#define VAL_ALIGN

/** define for key and value to share a baseline. */
#define BOTTOM_ALIGN

/** define for info-block to the right of the icon to be right-aligned */
#undef RIGHT_ALIGN

/** padding between key and value */
#define KVS 24

/** draw frame around text-blocks? */
#define BOX

/** put group-name in frame? */
#define BOX_TITLES

/** pad frame this much (pad>0) */
#define PAD 8





/*****************************************************************************/


/* don't change this unless you know what you're doing. */
#define RAISE_BY 1000

#ifdef BOX
#  undef  LINES_FOR_ALL
#  define LINES_FOR_ALL 0
#else
#  undef  PAD
#  define PAD 0
#  undef  BOX_TITLES
#endif

#define FS "tooltip-font-size"
#define TT "tooltip-font"



/*****************************************************************************/



/**
 * create a frame around a group of info-items.
 * @return a linked list of icon-elements.
 * @param evas - the evas
 * @param icon - the tooltip to append the elements to (needed by ICON_EL_*())
 * @param el - the current icon-element
 * @param x1 - left boundary
 * @param y1 - upper boundary
 * @param x2 - right boundary
 * @param y2 - lower boundary
 * @param state - state mask for the icon-elements to create
 */
static icon_el *frame_new(Evas *evas,icon *icon,icon_el *el,double x1,double y1,double x2,double y2,int state) {
  Evas_Coord   _x,_y,_w,_h,t;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0;
  Evas_Object *o;

  if(x2<x1) {
    t=x2;
    x2=x1;
    x1=t; }
  if(y2<y1) {
    t=y2;
    y2=y1;
    y1=t; }

  o=evas_object_line_add(evas);  /* frame/left */
  ICON_EL_APP(state,510,x1,y1);
  evas_object_line_xy_set(o,x1,y1,x1,y2);
  evas_object_color_set(o,COL,COL,COL,255);

  o=evas_object_line_add(evas);  /* frame/right */
  ICON_EL_APP(state,510,x2,y1);
  evas_object_line_xy_set(o,x2,y1,x2,y2);
  evas_object_color_set(o,COL,COL,COL,255);

  o=evas_object_line_add(evas);  /* frame/upper */
  ICON_EL_APP(state,510,x1,y1);
  evas_object_line_xy_set(o,x1,y1,x2,y1);
  evas_object_color_set(o,COL,COL,COL,255);

  o=evas_object_line_add(evas);  /* frame/lower */
  ICON_EL_APP(state,510,x1,y2);
  evas_object_line_xy_set(o,x1,y2,x2,y2);
  evas_object_color_set(o,COL,COL,COL,255);

  return el; }



/**
 * create a line connecting the file-icon with an info-item.
 * @return an icon-element wrapping a line
 * @param evas - the evas to create the line-object on
 * @param icon - ICON_EL_*() wants this
 * @param el - the current icon-element
 * @param x1 - left boundary
 * @param y1 - upper boundary
 * @param x2 - right boundary
 * @param y2 - lower boundary
 * @param state - state mask for the icon-elements to create
 */
static icon_el *line_new(Evas *evas,icon *icon,icon_el *el,double x1,double y1,double x2,double y2,int state) {
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0;
  Evas_Object *o;

  o=evas_object_line_add(evas);  /* frame/left */
  ICON_EL_APP(state,RAISE_BY-10,(x1<x2)?x1:x2,(y1<y2)?y1:y2);
  evas_object_line_xy_set(o,x1,y1,x2,y2);
  evas_object_color_set(o,COL,COL,COL, 127);
  el->tag=ET_MISC;

  o=evas_object_line_add(evas);  /* frame/left */
  ICON_EL_APP(state,RAISE_BY-10,((x1<x2)?x1:x2)+1,((y1<y2)?y1:y2)+1);
  evas_object_line_xy_set(o,x1,y1,x2,y2);
  evas_object_color_set(o,COL,COL,COL, 32);
  el->tag=ET_MISC;

  return el; }



/**
 * create an "icon" (a group of info-lines)
 * @return an icon-struct, or NULL for out of memory
 * @param label - the header for the group
 */
static icon *tt_icon_new(const char *label) {
  icon *icon;
  if(!(icon=malloc(sizeof(struct _icon))))
    return NULL;

  memset(icon,0,sizeof(struct _icon));
  icon->label=strdup(label);
  icon->file=NULL;
  icon->name=NULL; /* if sb-data is loaded from a file, note file here */
  icon->index=-1;
  icon->state=IE_DFL|IE_KEY|IE_VAL|IE_ID;

  return icon; }



/*****************************************************************************/



/**
 * put group of info-items to the left of file-icon.
 * @return y-coordinate for next box on this side
 * @param il - the icon-list
 * @param evas - the canvas
 * @param icon - the group of info-items
 * @param i - the file-icon
 * @param el2 - the current icon-element
 * @param x - x-offset for all boxes on this side
 * @param y - y-offset for current box on this side
 */
static inline int align_left(icon_list *il,Evas *evas,
                             struct _icon *icon,struct _icon *i,
                             icon_el *el2,int x,int y) {
  icon_el *el;
  double   ix,iy;
  int      f=0,sx=0,sy=0;

  sx=il->sx;
  sy=il->sy;

  icon->x=x;
  icon->y=y;

  ix=i->x+(i->w/2)-icon->x-sx;
  iy=i->y+(i->h/2)-icon->y-sy;

  /* add a line-object for each text-line */
  for(el=icon->els;el;el=el->next) {
    int h;

    if(el->state&IE_VAL) {
      h=el->h-evas_object_text_descent_get((Evas_Object *)el->data)-1;

#ifdef BOX
      if(!f) {
        f++;
        el2=frame_new(evas,icon,el2,0,0,icon->w+PAD,icon->h+PAD-3,IE_DFL);
        el2=line_new(evas,icon,el2,icon->w+PAD,el->yo+h,
                                   icon->w+KVS,el->yo+h,IE_DFL);
        el2=line_new(evas,icon,el2,icon->w+KVS,el->yo+h,ix,iy,IE_DFL); }
#else
      if(!f||LINES_FOR_ALL) {
        f++;
        el2=line_new(evas,icon,el2,el->xo+el->w,el->yo+h,
                                   icon->w+KVS,el->yo+h,IE_DFL);
        el2=line_new(evas,icon,el2,icon->w+KVS,el->yo+h,ix,iy,IE_DFL); }
#endif
    }
#ifdef BOTTOM_ALIGN
    else if(el->state&IE_KEY)
      el->yo+=evas_object_text_ascent_get((Evas_Object *)el->next->data)-
              evas_object_text_ascent_get((Evas_Object *)el->data);
#endif
    else if(el->state&IE_ID)
      el->xo=icon->w-el->w;
  }
  return y+=icon->h+icon->els->h+KVS; }



/**
 * put group of info-items to the right of file-icon.
 * @return y-coordinate for next box on this side
 * @param il - the icon-list
 * @param evas - the canvas
 * @param icon - the group of info-items
 * @param i - the file-icon
 * @param el2 - the current icon-element
 * @param x - x-offset for all boxes on this side
 * @param y - y-offset for current box on this side
 */
static inline int align_right(icon_list *il,Evas *evas,
                              struct _icon *icon,struct _icon *i,
                              icon_el *el2,int x,int y) {
  icon_el *el;
  double   ix,iy;
  int      f=0,sx=0,sy=0;

  sx=il->sx;
  sy=il->sy;

  icon->x=x-icon->w;
  icon->y=y;

  ix=i->x+(i->w/2)-icon->x-sx;
  iy=i->y+(i->h/2)-icon->y-sy;

  /* add a line-object for each text-line */
  for(el=icon->els;el;el=el->next) {
    int h;

    if(el->state&IE_KEY) {
      h=el->h-evas_object_text_descent_get((Evas_Object *)el->data)-1;
#ifdef BOTTOM_ALIGN
      el->yo+=evas_object_text_ascent_get((Evas_Object *)el->next->data)-
              evas_object_text_ascent_get((Evas_Object *)el->data);
#endif
#ifdef RIGHT_ALIGN
      el->xo=icon->w-el->w-KVS-el->next->w;
#endif

#ifdef BOX
      if(!f) {
        f++;
        el2=frame_new(evas,icon,el2,0,0,icon->w+PAD,icon->h+PAD-3,IE_DFL);
        el2=line_new(evas,icon,el2,0,el->yo+h,
                                   el->xo-KVS,el->yo+h,IE_DFL);
        el2=line_new(evas,icon,el2,el->xo-KVS,el->yo+h,ix,iy,IE_DFL); }
#else
      if(!f||LINES_FOR_ALL) {
        f++;
        el2=line_new(evas,icon,el2,el->xo,el->yo+h,
                                   el->xo-KVS,el->yo+h,IE_DFL);
        el2=line_new(evas,icon,el2,el->xo-KVS,el->yo+h,ix,iy,IE_DFL); }
#endif
    }
#ifdef RIGHT_ALIGN
    else if(el->state&IE_VAL)
      el->xo=icon->w-el->w;
#endif
    else if(el->state&IE_ID)
      el->xo=icon->w-el->w;
  }
  return y+=icon->h+icon->els->h+KVS; }



/*****************************************************************************/



/**
 * align all values of a group (same x-ordinate)
 * @return nw - new width of the group
 * @param icon - the group of info-items
 * @param wk - widest key in group
 */
int align_values(struct _icon *icon,int wk) {
  icon_el *el=icon->els,*val;
  int      nw=icon->w,d;

  while(el) {
    if(el->state&IE_KEY) {
      val=el->next;
      if(el->w<wk) {
        d=(wk-el->w);
        val->xo+=d; }
      if((d=(val->xo+val->w))>nw) {
        nw=d; }
      el=val->next; }
    else
      el=el->next; }
  return nw; }



/*****************************************************************************/
/* public interface */
/********************/



/**
 * destruct a tooltip.
 * @param data - the tooltip-handle tt_new() gave us
 */
void tt_dst(void **data) {
  if(data&&*data) {
    icon        *i2=*((icon **)data),*i3,*i;
    struct _ttd *ttd=i2->user1;
    icon_el     *el;

    if(!ttd) {
      E(0,"missing parmblk!?\n");
      goto done; }

    if((i=((icon *)ttd->ic))) {
      i3=icon_list_get_icon_by_file(ttd->il,ttd->file);
      if(!i3)
	E(0,"icon has gone away -- seems like the item (\"%s\") deleted while we were still looking at it, how rude!\n",ttd->file);
      else if(i!=i3)
	E(0,"icon has been recreated -- seems like the item (\"%s\") was destroyed and remade while we were still looking at it, how rude!\n",ttd->file);
      else {
        for(el=i->els;el;el=el->next) {
          evas_object_layer_set((Evas_Object *)el->data,
                                evas_object_layer_get((Evas_Object *)el->data)-RAISE_BY); }}
      i2->user1=NULL; }
    else
      E(2,"no highlighted icon noted on tooltip for \"%s\".\n",ttd->file);

    free(ttd->file);
    free(ttd);

  done:
    icon_dst_all(i2);
    *data=NULL; }}



/**
 * create a tooltip.
 *
 * no assumptions about the nature of the tooltip are made; for all the main
 * application knows, the plugin might read the info to the user aloud.
 *
 * @return a handle to pass to tt_dst() when destroy the tooltip.
 * @param data - the icon-list the tooltip is related to
 * @param x - x-position of the icon the info is for
 * @param y - y-position of the icon the info is for
 * @param fi - the file we're displaying info for
 */
void *tt_new(void *data,int x,int y,file_info *fi) {
  icon_list   *il=(icon_list *)data;
  noir_theme  *theme=(noir_theme *)il->theme;
  Evas        *evas=get_evas_from_iconlist(il);

  char       **pfont=NULL;
  long        *pfs=NULL;

  noir_md     *md=NULL;
  noir_meta   *m=NULL,*p=NULL;
  int          groups=0,items=0;

  icon        *icon,*ihead,*itmp,*ishelf=NULL,*i=NULL;
  icon_el     *el=NULL;
  Evas_Object *o,*o2;
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,
               wt=0,ht=0,wk=0,
               lx,ly,rx,ry,ls,rs,pr,hilight=1,
               sep;
#if 0
  const char  *tn=NULL;
#endif
  noir_theme   t;

  t.icon_mode=IC_FILL;
  t.icon_width=il->vis_w;
  t.icon_height=il->vis_h;



  /***************************************************************************/
  /* init */

  if(!pfont) {
    if(!(pfont=(char **)prop_get_data(prop_bag_prop_get(theme->data,TT)))) {
      E(0,TT " not set, failing.\n");
      return NULL; }}

  if(!pfs) {
    if(!(pfs=(long *)prop_get_data(prop_bag_prop_get(theme->data,FS)))) {
      prop_bag_prop_set(theme->data,FS,"20");
      pfs=(long *)prop_get_data(prop_bag_prop_get(theme->data,FS));
      E(0,FS " not set, falling back to default (%ld).\n",*pfs); }}

  sep=*pfs;

  if(!evas)
    return NULL;

  if(!(i=icon_list_get_icon_by_file(il,file_info_get_tag(fi)))) {
    E(2,"could not find \"%s\" in icon-list @ %p!?\n",file_info_get_tag(fi),il);
    if(!(i=ishelf=malloc(sizeof(struct _icon))))
      return NULL;
    memset(ishelf,0,sizeof(struct _icon)); }

  if(!(ihead=icon=tt_icon_new("tooltip")))
    return NULL;



  /***************************************************************************/
  /* fade out */

  for(el=i->els;el;el=el->next) {
    evas_object_layer_set((Evas_Object *)el->data,
                          evas_object_layer_get((Evas_Object *)el->data)+RAISE_BY); }

  o=evas_object_gradient_add(evas);  /* fadeout */
  evas_object_resize(o,il->vis_w,il->vis_h);
  evas_object_gradient_angle_set(o,100);
  ICON_EL_APP(IE_DFL,505,0,0);
  evas_object_move(o,0,0);

  icon->els=el;

  evas_object_gradient_colors_clear(o);



  /***************************************************************************/
  /* thumbnail */

#ifdef WITH_THUMBNAILS
/* tn=file_info_get_thumbfile(fi); */
  if(
#if 1
    (o2=evas_thumbnail_dead_or_alive(&t,evas,fi,il->vis_w,il->vis_h))
#else
    (o2=evas_thumbnail_dead(&t,evas,fi))
#endif
    ) {
    evas_object_gradient_color_add(o,255-COL,255-COL,255-COL, 79,  63);
    evas_object_gradient_color_add(o,255-COL,255-COL,255-COL, 139,  7);
    evas_render(evas);

    o=o2;
    evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
    w_=_w; h_=_h;
    /* layer 506: above the darkening fade (505), below the text (1010) --
       with the fade at 504 the live video was hidden underneath it */
    ICON_EL_APP(IE_DFL,506,0,0);
    evas_object_move(o,0,0);
    evas_object_show(o); }
  else
#endif
  { evas_object_gradient_color_add(o,255-COL,255-COL,255-COL, 159, 63);
    evas_object_gradient_color_add(o,255-COL,255-COL,255-COL, 223,  7);
    evas_render(evas); }



  /***************************************************************************/
  /* create text */

  md=file_info_get_meta(fi);

  for(p=NULL,m=meta_get_1st(md);m;m=meta_get_next(m)) {
    char   *val,*buf=NULL;
    int     l,my_w,my_h,my_c=COL>128?(COL-32):32;

    if(!p||strcmp(meta_get_group(p),meta_get_group(m))) {
      if(!(itmp=tt_icon_new(meta_get_group(m)))) {
        void *v=(void *)ihead;
        tt_dst(&v);
        return NULL; }

      if(!p)
        icon->user1=m;
      else {
#ifdef VAL_ALIGN
        icon->w=align_values(icon,wk);
#else
        icon->w=wt;
#endif
        icon->h=ht; }

      icon->next=itmp;
      icon=itmp;
      ht=wt=wk=0;

      el=NULL;

#ifdef BOX_TITLES
      o=evas_object_text_add(evas);
      evas_object_text_font_set(o,*pfont,((float)*pfs)/2.0);
      evas_object_text_text_set(o,meta_get_group(m));
      evas_object_color_set(o,COL,COL,COL, 192);
      ICON_EL_APP(IE_ID,1010,0,0);
      el->yo=-evas_object_text_max_ascent_get((Evas_Object *)el->data); /* thanks raster */
      icon->els=el;
      wt=0;
#endif

      E(2,"GRP: %s,  state: $%x  (%dx%d)\n",meta_get_group(m)?meta_get_group(m):"??",icon->state,el->w,el->h);
      groups++; }

    p=m;

    if(meta_get_flags(m)==META_EF)
      continue;

    items++;

    my_h=ht;
    o=evas_object_text_add(evas);
    evas_object_text_font_set(o,*pfont,(*pfs*3)/4);
    evas_object_text_text_set(o,meta_get_key(m));
    evas_object_color_set(o,COL,COL,COL, 192);
    ICON_EL_APP(IE_KEY,1010,PAD,ht);
    my_w=w_;
    if(w_>wk)
      wk=w_;

    /* only needed if box-titles are disabled */
    if(!icon->els)
      icon->els=el;

    val=(char *)meta_get_val(m);
    if((l=strlen(val))>LEN) {
      if((buf=malloc(LEN+1))) {
        if(!strcmp(meta_get_key(m),"Icon")||
           !strcmp(meta_get_key(m),"Thumbnail")||
           !strcmp(meta_get_key(m),"Tag")) {
          strcpy(buf,"...");
          strcat(buf+3,val+l-LEN+3);
          buf[LEN]='\0'; }
        else {
          strncpy(buf,val,LEN-3);
          strcpy(&buf[LEN-3],"..."); }}
      val=buf; }
    else if(val[0]==':')
      val++;

    if(val) {
      ht=my_h;

      o=evas_object_text_add(evas);
      evas_object_text_font_set(o,*pfont,*pfs);
      evas_object_text_text_set(o,val);
      evas_object_color_set(o,my_c,my_c,my_c, 192);
      ICON_EL_APP(IE_VAL,1010,my_w+PAD+KVS,ht);

      if(buf)
        free(buf); }}

#ifdef VAL_ALIGN
  icon->w=align_values(icon,wk);
#else
  icon->w=wt;
#endif
  icon->h=ht;



  /***************************************************************************/
  /* align text */


  ls=i->x-il->sx;     /* how much space to the left and to the right? */
  rs=il->vis_w-i->x-il->sx-i->w;

  lx=24;              /* coordinate for first left block */
  ly=32;
  rx=il->vis_w-lx;    /* coordinate for first right block */
  ry=ly;

  pr=(ls<rs)?0:1;

  for(icon=ihead->next;icon;icon=icon->next) {
    icon_el *el2;

    /* find last icon-el in icon */
    for(el2=NULL,el=icon->els;el;el=el->next)
      el2=el;

    if(!pr) {    /* prefer left-alignment */
      if((icon->w<ls)&&((icon->h+ly)<il->vis_h))
        ly=align_left(il,evas,icon,i,el2,lx,ly);
      else if((icon->w<rs)&&((icon->h+ry)<il->vis_h))
        ry=align_right(il,evas,icon,i,el2,rx,ry);
      else {
        if((icon->h+ly)<il->vis_h)
          ly=align_left(il,evas,icon,i,el2,lx,ly);
        else if((icon->h+ry)<il->vis_h)
          ry=align_right(il,evas,icon,i,el2,rx,ry);
        else
          icon->x=-icon->w; }}

    else {       /* prefer right alignment */
      if((icon->w<rs)&&((icon->h+ry)<il->vis_h))
        ry=align_right(il,evas,icon,i,el2,rx,ry);
      else if((icon->w<ls)&&((icon->h+ly)<il->vis_h))
        ly=align_left(il,evas,icon,i,el2,lx,ly);
      else {
        if((icon->h+ry)<il->vis_h)
          ry=align_right(il,evas,icon,i,el2,rx,ry);
        else if((icon->h+ly)<il->vis_h)
          ly=align_left(il,evas,icon,i,el2,lx,ly);
        else
          icon->x=-icon->w; }}

#define WITHIN(x,y,x1,y1,x2,y2)  (((x>=x1)&&(x<x2))&&((y>=y1)&&(y<y2)))

    { int bx1=i->x-il->sx,bx2=i->x+i->w-il->sx,by1=i->y-il->sy,by2=i->y+i->h-il->sy;
      int ix1=icon->x,ix2=icon->x+icon->w,iy1=icon->y,iy2=icon->y+icon->h;

      if(WITHIN(bx1,by1,ix1,iy1,ix2,iy2)||
         WITHIN(bx2,by1,ix1,iy1,ix2,iy2)||
         WITHIN(bx1,by2,ix1,iy1,ix2,iy2)||
         WITHIN(bx2,by2,ix1,iy1,ix2,iy2)) {
        E(2,"WITHOUT %d,%d-%d,%d   %d,%d-%d,%d\n",bx1,by1,bx2,by2,ix1,iy1,ix2,iy2);
        hilight=0; }
      else
        E(2,"without %d,%d-%d,%d   %d,%d-%d,%d\n",bx1,by1,bx2,by2,ix1,iy1,ix2,iy2); }

    /* make icon visible */
    icon_move(icon,icon->x-1,icon->y);

    evas_render(evas); }



  if(!hilight) {
    E(2,"HIDE!\n");
    for(el=i->els;el;el=el->next)
      evas_object_layer_set((Evas_Object *)el->data,
                            evas_object_layer_get((Evas_Object *)el->data)-RAISE_BY);

    for(icon=ihead->next;icon;icon=icon->next) {
      for(el=icon->els;el;el=el->next) {
        if(el->tag==ET_MISC) {
          evas_object_hide((Evas_Object *)el->data); }}}
    evas_render(evas); }

  { struct _ttd *ttd=calloc(1,sizeof(struct _ttd));

    if(ishelf) {
      icon_dst(ishelf);
      i=NULL; }

    if(ttd) {
      ttd->il=il;
      ttd->ic=i;
      if(!ishelf&&
         (!i||!ICON_GET_FILENAME(i)||!(ttd->file=strdup(ICON_GET_FILENAME(i))))) {
	free(ttd);
	ttd=NULL; }}

    if(ttd)
      ihead->user1=ttd;
    else {
      E(0,"out of memory!?\n");
      icon_dst_all(ihead);
      evas_render(evas);
      return NULL; }}

  return (void *)ihead; }
