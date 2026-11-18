/** @file  live_preview.c
 *  @brief live video preview shown as a separate "tooltip" in the icon-view
 *
 * the info-tooltips (tt_* engines) show videos as a static poster only; the
 * playback itself lives in this second tooltip: a framed box holding the
 * live emotion object, placed in free space inside the window (never
 * overlapping the info-tooltip or the icon under the mouse) and connected
 * to that icon by a line, like tt_diagram's info-boxes.
 *
 * the preview shares the lifecycle of the info-tooltip: it is created in
 * tooltip_show_for() right after the info-tooltip and destroyed in
 * tooltip_cancel(), so it never outlives it.
 */

#include "config.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <Evas.h>

#include "debug.h"
#include "evas_macros.h"   /* ICON_EL_APP, get_evas_from_iconlist, evas_thumbnail_live */
#include "icon_list.h"
#include "theme.h"
#include "model.h"
#include "file.h"          /* file_get_mime() */

#include "live_preview.h"


#undef  MAX
#undef  MIN
#define MAX(a,b)  ((a)>(b))?(a):(b)
#define MIN(a,b)  ((a)<(b))?(a):(b)

/* margin between preview and everything else (tooltip, icon, window edge) */
#define LP_MARGIN  8
#define LP_MIN_W   160
#define LP_MAX_W   384

/* layers: must be above the fullscreen fade (505) and the diagram thumb
   (506) or the video would be hidden behind them; the preview never
   overlaps the info-tooltip, so it does not need to be above its text */
#define LP_LAYER_VIDEO  507
#define LP_LAYER_LINE   508
#define LP_LAYER_FRAME  509



/**
 * is this file a video we can play live?
 * @return 1 for MIME video/*, 0 otherwise
 */
int live_preview_is_video(file_info *fi) {
  const char *mime=NULL,*tag=NULL;

  if(!fi)
    return 0;
  if(!(mime=file_info_get_mime(fi))&&(tag=file_info_get_tag(fi)))
    mime=file_get_mime(tag);

  return (mime&&!strncasecmp(mime,"video/",6))?1:0; }



/**
 * bounding box of an open evas-tooltip.
 *
 * engines that know their box (tt_traditional, tt_centre, tt_wholewindow,
 * tt_diagram) store it in the head-icon's x/y/w/h; for the rest the union
 * of the visible element geometries is used (the fullscreen fade gradient
 * is skipped -- it is not part of the tooltip proper).
 *
 * @param tt  the tooltip handle theme->tt_new() returned
 * @param x,y,w,h  return buffers (empty box if nothing found)
 */
void live_preview_tt_rect(void *tt,int *rx,int *ry,int *rw,int *rh) {
  icon *i=(icon *)tt;
  int   x1=0,y1=0,x2=0,y2=0,first=1;

  *rx=*ry=*rw=*rh=0;
  if(!i)
    return;

  for(;i;i=i->next) {
    if((i->w>0)&&(i->h>0)) {
      if(first) { x1=i->x; y1=i->y; x2=i->x+i->w; y2=i->y+i->h; first=0; }
      else {
        if(i->x<x1)               x1=i->x;
        if(i->y<y1)               y1=i->y;
        if((i->x+i->w)>x2)        x2=i->x+i->w;
        if((i->y+i->h)>y2)        y2=i->y+i->h; } }
    else if(i->els) {
      icon_el *el;
      for(el=i->els;el;el=el->next) {
        Evas_Object *eo=(Evas_Object *)el->data;
        Evas_Coord   gx,gy,gw,gh;
        if(!eo)
          continue;
        if(evas_object_data_get(eo,"compat-gradient"))  /* fullscreen fade */
          continue;
        if(!evas_object_visible_get(eo))
          continue;
        evas_object_geometry_get(eo,&gx,&gy,&gw,&gh);
        if(first) { x1=gx; y1=gy; x2=gx+gw; y2=gy+gh; first=0; }
        else {
          if(gx<x1)           x1=gx;
          if(gy<y1)           y1=gy;
          if((gx+gw)>x2)      x2=gx+gw;
          if((gy+gh)>y2)      y2=gy+gh; } } } }

  if(!first) {
    *rx=x1;  *ry=y1;
    *rw=x2-x1;  *rh=y2-y1; } }



/**
 * does rect (x,y,w,h) hit box (bx1,by1,bx2,by2) with margin m?
 */
static int lp_hit(int x,int y,int w,int h,int bx1,int by1,int bx2,int by2,int m) {
  if((bx2<=bx1)||(by2<=by1))
    return 0;
  return !( (x+w+m<=bx1)||(bx2+m<=x)||(y+h+m<=by1)||(by2+m<=y) ); }



/**
 * pick a position for the preview box inside the visible area.
 *
 * preference: the side opposite the info-tooltip (tooltip on the right ->
 * preview on the left and vice versa), then beside the icon, then the
 * window edges; vertically near the icon.  the preview must not overlap
 * the info-tooltip or the icon.
 */
static void lp_place(int vis_w,int vis_h,int pw,int ph,
                     int tx,int ty,int tw,int th,
                     int ix,int iy,int iw,int ih,
                     int *px,int *py) {
  int m=LP_MARGIN;
  int acx=(iw>0)?(ix+iw/2):((tw>0)?(tx+tw/2):(vis_w/2));
  int acy=(ih>0)?(iy+ih/2):((th>0)?(ty+th/2):(vis_h/2));
  int xl[8],yl[6],nx=0,ny=0,i,j,x,y;

  /* horizontal candidates: opposite side of the tooltip first */
  if(tw>0) {
    if((tx+tw/2)>(vis_w/2)) {  /* tooltip right -> left first */
      xl[nx++]=tx-pw-m;
      xl[nx++]=tx+tw+m; }
    else {                      /* tooltip left -> right first */
      xl[nx++]=tx+tw+m;
      xl[nx++]=tx-pw-m; } }
  if(iw>0) {
    xl[nx++]=ix-pw-m;
    xl[nx++]=ix+iw+m; }
  xl[nx++]=m;
  xl[nx++]=vis_w-pw-m;

  /* vertical candidates: near the icon first, then tooltip-relative */
  yl[ny++]=acy-ph/2;
  if(th>0) {
    yl[ny++]=ty-ph-m;
    yl[ny++]=ty+th+m; }
  yl[ny++]=m;
  yl[ny++]=vis_h-ph-m;

  for(i=0;i<nx;i++) {
    x=MIN(MAX(xl[i],m),vis_w-pw-m);
    if(x< m) x=m;
    for(j=0;j<ny;j++) {
      y=MIN(MAX(yl[j],m),vis_h-ph-m);
      if(y<m) y=m;
      if(!lp_hit(x,y,pw,ph, tx,ty,tx+tw,ty+th, m)&&
         !lp_hit(x,y,pw,ph, ix,iy,ix+iw,iy+ih, m)) {
        *px=x;  *py=y;
        return; } } }

  /* nothing was completely free: least-bad fallback, clamped into view */
  x=MIN(MAX(xl[0],m),vis_w-pw-m);  if(x<m) x=m;
  y=MIN(MAX(acy-ph/2,m),vis_h-ph-m);  if(y<m) y=m;
  *px=x;  *py=y; }



/**
 * create the live-preview tooltip.
 * @return a handle for live_preview_hide(), or NULL
 */
void *live_preview_show(icon_list *il,file_info *fi,icon *ic,int ax,int ay,void *tt) {
  Evas        *evas;
  icon        *icon=NULL;
  icon_el     *el=NULL;
  Evas_Object *o;
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0;
  noir_theme   t;
  int          tx,ty,tw,th, ix,iy,iw,ih, px,py,pw,ph, cx,cy,lx1,ly1,lx2,ly2;
  const char  *tag;

  if(!il||!fi)
    return NULL;
  if(!live_preview_is_video(fi))
    return NULL;
  if(!(evas=get_evas_from_iconlist(il)))
    return NULL;

  /* rects in canvas coordinates (visible area starts at 0,0) */
  live_preview_tt_rect(tt,&tx,&ty,&tw,&th);
  if(ic) {
    ix=ic->x-il->sx;  iy=ic->y-il->sy;  iw=ic->w;  ih=ic->h; }
  else {
    ix=ax;  iy=ay;  iw=0;  ih=0; }

  /* preview size: about a third of the window width, 4:3, clamped */
  pw=il->vis_w/3;
  if(pw>LP_MAX_W)     pw=LP_MAX_W;
  if(pw<LP_MIN_W)     pw=LP_MIN_W;
  ph=(pw*3)/4;
  if(ph>il->vis_h/2) {
    ph=il->vis_h/2;
    pw=(ph*4)/3; }
  if(pw>il->vis_w-2*LP_MARGIN)  pw=il->vis_w-2*LP_MARGIN;
  if(ph>il->vis_h-2*LP_MARGIN)  ph=il->vis_h-2*LP_MARGIN;
  if((pw<32)||(ph<32))
    return NULL;

  lp_place(il->vis_w,il->vis_h,pw,ph, tx,ty,tw,th, ix,iy,iw,ih, &px,&py);

  /* the live object itself (NULL if live thumbnails are off or no
     thumbnailer claims the file -- then there is simply no preview) */
  memset(&t,0,sizeof(t));
  t.icon_mode=IC_FILL;
  t.icon_width=pw;
  t.icon_height=ph;

  tag=file_info_get_tag(fi);
  if(!(o=evas_thumbnail_live(&t,evas,fi,pw,ph))) {
    E(1,"live preview: no live object for \"%s\"\n",tag?tag:"?");
    return NULL; }

  if(!(icon=malloc(sizeof(struct _icon)))) {
    evas_object_del(o);
    return NULL; }
  memset(icon,0,sizeof(struct _icon));
  icon->label=strdup("live-preview");
  icon->file=NULL;
  icon->name=NULL;
  icon->index=-1;
  icon->state=IE_DFL;

  ICON_EL_APP(IE_DFL,LP_LAYER_VIDEO,0,0);
  evas_object_move(o,px,py);
  evas_object_resize(o,pw,ph);
  evas_object_show(o);

  /* frame around the preview, same style as the tooltip frames */
  o=evas_object_line_add(evas);
  ICON_EL_APP(IE_DFL,LP_LAYER_FRAME,0,0);
  evas_object_line_xy_set(o,px,py,px,py+ph);
  evas_object_color_set(o,255,255,255,255);

  o=evas_object_line_add(evas);
  ICON_EL_APP(IE_DFL,LP_LAYER_FRAME,0,0);
  evas_object_line_xy_set(o,px+pw,py,px+pw,py+ph);
  evas_object_color_set(o,255,255,255,255);

  o=evas_object_line_add(evas);
  ICON_EL_APP(IE_DFL,LP_LAYER_FRAME,0,0);
  evas_object_line_xy_set(o,px,py,px+pw,py);
  evas_object_color_set(o,255,255,255,255);

  o=evas_object_line_add(evas);
  ICON_EL_APP(IE_DFL,LP_LAYER_FRAME,0,0);
  evas_object_line_xy_set(o,px,py+ph,px+pw,py+ph);
  evas_object_color_set(o,255,255,255,255);

  /* connecting line from the icon to the nearest point of the preview
     (two lines like tt_diagram's line_new) */
  cx=(iw>0)?(ix+iw/2):ix;
  cy=(ih>0)?(iy+ih/2):iy;
  lx1=cx;  ly1=cy;
  lx2=MIN(MAX(cx,px),px+pw);
  ly2=MIN(MAX(cy,py),py+ph);

  o=evas_object_line_add(evas);
  ICON_EL_APP(IE_DFL,LP_LAYER_LINE,0,0);
  evas_object_line_xy_set(o,lx1,ly1,lx2,ly2);
  evas_object_color_set(o,255,255,255,127);

  o=evas_object_line_add(evas);
  ICON_EL_APP(IE_DFL,LP_LAYER_LINE,0,0);
  evas_object_line_xy_set(o,lx1,ly1,lx2+1,ly2+1);
  evas_object_color_set(o,255,255,255,32);

  icon->x=px;  icon->y=py;  icon->w=pw;  icon->h=ph;

  evas_render(evas);

  E(1,"live preview for \"%s\" at %d,%d (%dx%d)\n",tag?tag:"?",px,py,pw,ph);

  return (void *)icon; }



/**
 * destroy a live-preview tooltip.
 * @param pv  pointer to the handle live_preview_show() returned
 */
void live_preview_hide(void **pv) {
  if(pv&&*pv) {
    /* icon_dst() -> icon_el_dst_all() -> icon_el_data_dst() ->
       evas_object_del(): the emotion object's DEL callback stops playback
       and unloads the pipeline before the object is freed */
    icon_dst((icon *)*pv);
    *pv=NULL; } }
