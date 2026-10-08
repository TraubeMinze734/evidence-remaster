#include "config.h"
#include "gui/common/icon_list.h"
#include "theme.h"
#ifdef HAVE_EVAS2
#  include "evas_compat.h"   /* gradient API removed from modern EFL */
#endif

#ifndef ICON_EL_PRE
#  define ICON_EL_ADD(t,l,ex,ey) \
  evas_object_move(o,icon->x+ex,icon->y+ey); \
  evas_object_geometry_get(o,&_x,&_y,&_w,&_h); \
  evas_object_resize(o,_w,_h); \
  evas_object_layer_set(o,(l)); \
  evas_object_show(o); \
  el=icon_el_new(t,ex,ey,_w,_h,o,el);
#endif

#ifndef ICON_EL_PRE
#  define ICON_EL_PRE(t,l,ex,ey) \
  evas_object_move(o,-1000,-1000); \
  evas_object_geometry_get(o,&_x,&_y,&_w,&_h); \
  evas_object_resize(o,_w,_h); \
  if(!icon||(icon->state&t)) \
    evas_object_show(o); \
  w_=(int)_w; \
  h_=(int)_h; \
  evas_object_layer_set(o,(l)); \
  el=icon_el_new(t,ex,ey,w_,h_,o,el); \
  if((ex+w_)>wt) wt=(ex+w_); \
  if((ey+h_)>ht) ht=(ey+h_);
#endif

#ifndef ICON_EL_APP
#  define ICON_EL_APP(t,l,ex,ey) \
  evas_object_move(o,-1000,-1000); \
  evas_object_geometry_get(o,&_x,&_y,&_w,&_h); \
  evas_object_resize(o,_w,_h); \
  if(!icon||(icon->state&t)) \
    evas_object_show(o); \
  w_=(int)_w; \
  h_=(int)_h; \
  evas_object_layer_set(o,(l)); \
  if(el) { \
    el->next=icon_el_new(t,ex,ey,w_,h_,o,NULL); \
    el=el->next; } \
  else \
    el=icon_el_new(t,ex,ey,w_,h_,o,NULL); \
  if((ex+w_)>wt) wt=(ex+w_); \
  if((ey+h_)>ht) ht=(ey+h_);
#endif

/* load image using evas. no frills, just loading/error-checking. */
Evas_Object *evas_thumbnail_dead(void *t,Evas *evas,file_info *fi);
Evas_Object *evas_thumbnail_live(void *t,Evas *evas,file_info *fi,int w,int h);
Evas_Object *evas_thumbnail_dead_or_alive(void *t,Evas *evas,file_info *fi,int w,int h);

Evas_Object *evas_object_from_imlib_image(Evas *evas,void *);

Evas        *get_evas_from_iconlist(icon_list *);

int          icon_view_render_updates(icon_list *);

int          evas_ext_evt(icon_list *il,int x,int y,int b,theme_area_event_type t,theme_area_event_modifiers m,void *data,icon **ext,void **rp);
icon        *evas_ext_find(icon **areas,const char *name);
icon        *evas_ext_new(icon **areas,char *label,char *name);
int          evas_ext_upd(icon_list *il,file_info *fi);
