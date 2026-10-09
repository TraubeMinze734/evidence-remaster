/** @file  bg_trans.c
 *  @brief background-handler providing pseudo-transparency
 *
 * this plugin provides pseudo-transparency, so you will see the
 * "root-window" -- the desktop -- shining through, but not other windows.

 * properties:
 * - backgroundtintalpha
 * - backgroundtintcolour
 */

#include "config.h"

#include <Evas.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "errors.h"
#include "evas_macros.h"
#include "theme.h"
#include "icon_list.h"
#include "setup.h"
#include "debug.h"

#include "gui/common/x11.h"

#define LISTENER "background{pseudotrans}"

typedef enum { BG_TINTCOL, BG_TINTALPHA } bg_tag;

typedef struct _bg_param {
  icon_list   *il;
  Evas_Object *trans;
  Evas_Object *pic;
  Evas_Object *tint; } bg_param;



#if defined(HAVE_X) && defined(HAVE_IMLIB2)
#  include <X11/Xlib.h>
#  include <X11/Xatom.h>
#  include <X11/Intrinsic.h>
#  include <Imlib2.h>
#  include "imlib.h"

/* obtain a partial copy of the root-pixmap for pseudo-transparency effects. */

Atom rootpmap=None,xpixmap=None,xcolor=None;



/**
 * find root-window our application window lives in
 * @return the "desktop"
 * @param app  our application-window
 */
Window get_desk(Window app) {
  /* this routine inspired by some code from Eterm */
  Atom           type;
  int            format;
  unsigned long  length,after;
  unsigned char *data;
  unsigned int   nchildren;
  Window         w,root,parent,*children;

  for(w=app;w;w=parent) {
    if((XQueryTree(get_display(),w,&root,&parent,&children,&nchildren))==False)
      return None;
    if(nchildren)
      XFree(children);
    if((XGetWindowProperty(get_display(),w,xpixmap,0L,1L,False,AnyPropertyType,&type,&format,&length,&after,&data))!=Success) {
      if((XGetWindowProperty(get_display(),w,xcolor,0L,1L,False,AnyPropertyType,&type,&format,&length,&after,&data))!=Success)
        continue; }

    XFree(data);

    if((w!=app)&&(type!=None)) {
      if(w!=get_root_window())
        XSelectInput(get_display(),w,PropertyChangeMask);
      return w; }}

  return None; }



/**
 * get pixmap for pseudo-transparency
 * @return an evas image-object
 * @param il   the icon-list
 * @param old  the previous PT pixmap
 * @param x    position of the application-window
 * @param y    position of the application-window
 * @param w    width of the application-window
 * @param h    height of the application-window
 */
static Evas_Object *transparency_get_pixmap(icon_list *il,Evas_Object *old,
                                            int x,int y,int w,int h) {
  Atom            type; 
  int             format,ret;
  unsigned long   length,after;
  unsigned char  *data;
  Evas           *evas=get_evas_from_iconlist(il);
  Evas_Object    *new=NULL;
  Pixmap          p;
  Window          win=get_widget_window(il->canvas);
  Window          desk=get_desk(win);

  if(xpixmap==None)
    xpixmap=XInternAtom(get_display(),"_XROOTPMAP_ID",False);
  if(xcolor==None)
    xcolor=XInternAtom(get_display(),"_XROOTCOLOR_PIXEL",False);
  if(rootpmap==None)
    rootpmap=XInternAtom(get_display(),"_XROOTPMAP_ID",False);
  if(desk==None)
    desk=get_root_window();

  if(old)
    evas_object_del(old);

  if(rootpmap!=None) {
    ret=XGetWindowProperty(get_display(),desk,rootpmap,0L,1L,
                           False,AnyPropertyType,&type,&format,&length,
                           &after,&data);

    if((ret==Success)&&(type==XA_PIXMAP)&&((p=*((Pixmap *)data))!=None)) {
      Imlib_Image  im;
      unsigned int pw,ph, pb,pd;
      int          px,py;
      Window       win_dummy;
      Status       st;

      st=XGetGeometry(get_display(),p,&win_dummy, &px,&py,&pw,&ph, &pb, &pd);
      if(st&&(pw>0)&&(ph>0)) {
        E(3,"transparency update %3d,%3d %3dx%3d\n",x,y,w,h);

        IMLIB_ACQUIRE;

        imlib_context_set_display(get_display());
        imlib_context_set_visual(DefaultVisual(get_display(),DefaultScreen(get_display())));
        imlib_context_set_colormap(DefaultColormap(get_display(),DefaultScreen(get_display())));
        imlib_context_set_drawable(*((Pixmap *)data));

        if((x>=px)&&(y>=py)&&((x+w)<=(py+((signed int)pw)))&&((y+h)<=(py+((signed int)ph)))) {
          im=imlib_create_image_from_drawable(0,x,y,w,h,1);
          imlib_context_set_image(im); }

        else {  /* tiled root pixmap */
          Imlib_Image  dst;
          int          sx,sy,dx,dy;

          im=imlib_create_image_from_drawable(0,px,py,pw,ph,1);
          dst=imlib_create_image(w,h);
          imlib_context_set_image(dst);
          imlib_image_clear();
          imlib_context_set_cliprect(0,0,w,h);

          dx=pw;
          dy=ph;
          dx=(x%dx);
          dy=(y%dy);

          for(sy=0;sy<(h+dy);sy+=ph) {
            for(sx=0;sx<(w+dx);sx+=pw) {
              imlib_blend_image_onto_image(im,1,0,0,pw,ph,sx-dx,sy-dy,pw,ph); }}
          imlib_context_set_image(im);
          imlib_free_image();
          imlib_context_set_image(dst); }

        imlib_image_set_format("argb");
        new=evas_object_image_add(evas);
        evas_object_image_alpha_set(new,0);
        evas_object_image_size_set(new,w,h);   /* thanks rephorm */
        evas_object_image_data_copy_set(new,imlib_image_get_data_for_reading_only());
        imlib_free_image();

        IMLIB_RELEASE;

        evas_object_image_fill_set(new,0,0,w,h);
        evas_object_resize(new,w,h);
        evas_object_move(new,0,0);
        evas_object_layer_set(new,-5002);
        evas_object_image_data_update_add(new,0,0,w,h);
        evas_object_show(new); }
      else /* this can happen with e16 */
        E(0,"got invalid pixmap from root-window, ignoring...\n"); }
    else {
      XFree(data);
      E(2,"could not read root-window property _XROOTPMAP_ID...\n"); }}
  else
    E(2,"could not get XAtom _XROOTPMAP_ID...\n");

  if(!new) {  /* fallback if no root pixmap is found */
    int r=127,g=127,b=127,gotcolour=0;

    new=evas_object_rectangle_add(evas);
    evas_object_resize(new,w,h);
    evas_object_move(new,0,0);
    evas_object_layer_set(new,-9999);

    if(XGetWindowProperty(get_display(),desk,xcolor,0L,1L,False,AnyPropertyType,&type,&format,&length,&after,&data)==Success) {
      if(type==XA_CARDINAL) {
        Pixel pix=*((Pixel *)data);
        r=(pix>>16)&0xff;
        g=(pix>>8)&0xff;
        b=pix&0xff;
        gotcolour=1;
        XFree(data); }
      else
        XFree(data); }

    if(!gotcolour)
      E(0,"cannot create transparency pixmap, no valid wallpaper and no background colour set.\n");

    evas_object_color_set(new, r,g,b, 255);
    evas_object_show(new); }

  return new; }

#else
#  warning building the ebg-type transparency background-plugin without calls to X being allowed will result in a transparency-less plugin for now...
#endif



/**
 * listen for changes in our numeric properties.
 * @return NOIR_SUCC, or an error-code.
 * @param bag - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int bg_num_listener(noir_prop_bag *bag,noir_prop *e,int tag,void *data) {
  long           val=prop_get_int(e);
  const char    *key=prop_get_key(e);
  bg_param      *bg=(bg_param *)data;
  int            r=127,g=127,b=127,a=0;

  if(bg->tint)
    evas_object_color_get(bg->tint,&r,&g,&b,&a);

  switch(tag) {
    case BG_TINTCOL:
      r=val>>16;
      g=(val>>8)%256;
      b=val%256;
      if(bg->tint)
        evas_object_color_set(bg->tint,r,g,b,a);
      icon_list_layout_all(bg->il);
      break;
    case BG_TINTALPHA:
      if(bg->tint)
        evas_object_color_set(bg->tint,r,g,b,val);
      icon_list_layout_all(bg->il);
      break;
    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld, %p)\n",key,val,data);

  return NOIR_SUCC; }



/**
 * callback: view was scrolled, update background
 * @param i  the icon-list (contains coordinates and pointer to background)
 */
void bg_scroll(void *i) {
  icon_list *il=(icon_list *)i;
  bg_param  *b=(bg_param *)il->bg;

  if(b->trans)
    evas_object_move(b->trans,il->sx,il->sy);
  if(b->pic)
    evas_object_move(b->pic  ,il->sx,il->sy);
  if(b->tint)
    evas_object_move(b->tint ,il->sx,il->sy); }



/**
 * update a single element of the (composed) background
 * @param il  the icon-list
 * @param bg  the background-element (evas-object)
 */
static inline void bg_upd_el(icon_list *il,Evas_Object *bg) {
  if(bg) {
    evas_object_hide(bg);
    /* the tint is a rectangle object -- image_fill_set() is image-only
       and spams eina-safety errors.  rectangles need no fill-rect. */
    if(!strcmp(evas_object_type_get(bg),"image"))
      evas_object_image_fill_set(bg,0,0,il->vis_w,il->vis_h);
    evas_object_resize(bg,il->vis_w,il->vis_h);
    evas_object_show(bg); }}



/**
 * callback: update background (window resized or moved, backdrop changed, ...)
 * moving is relevant since the plugin uses pseudo-transparency!
 * @param i  the icon-list (contains pointer to background)
 * @param x  horizontal position
 * @param y  vertical position
 * @param w  width
 * @param h  height
 */
void bg_upd(void *i,int x,int y,int w,int h) {
  icon_list   *il=((icon_list *)i);
  Evas        *evas=get_evas_from_iconlist(il);
  bg_param    *bg=il->bg;

#ifdef HAVE_X
  bg->trans=transparency_get_pixmap(il,bg->trans,x,y,w,h);
#endif

  bg_upd_el(il,bg->trans);
  bg_upd_el(il,bg->pic);
  bg_upd_el(il,bg->tint);
  evas_render(evas); }



/**
 * set up a single element of the (composed) background
 * @param il     the icon-list
 * @param o      the background-element (evas-object)
 * @param layer  the layer the object should live in
 */
static inline void bg_new_el(icon_list *il,Evas_Object *o,int layer) {
  if(o) {
    evas_object_move(o,0,0);
    evas_object_layer_set(o,layer);
    evas_object_show(o); }}



/**
 * set a background on a view
 * @return the newly set background object (il->bg)
 * @param t       the current theme in this session
 * @param il      the icon-list
 * @param bg_pic  the name of the image for the background (any format evas loads)
 */
void *bg_new(noir_theme *t,icon_list *il,char *bg_pic) {
  bg_param       *b;
  char            buf[128],*prop;
  Evas           *evas=get_evas_from_iconlist(il);
  noir_prop_bag  *te=t->data;
    
  if(!il)
    return NULL;

  if(!(b=malloc(sizeof(bg_param))))
    return NULL;
  memset(b,0,sizeof(bg_param));

  strcpy(buf,il->name);
  prop=buf+strlen(buf);

  if(!bg_pic||!*bg_pic) {
    E(1,"no wallpaper given.\n"); }
  else {
    int          err;
    Evas_Object *o=evas_object_image_add(evas);

    if((b->pic=o)) {
      evas_object_image_file_set(o,bg_pic,NULL);

      if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
        E(0,"error #%d loading \"%s\"...\n",err,bg_pic);
        evas_object_del(o);
        b->pic=o=NULL; }
      else {
        evas_object_image_fill_set(o,0,0,il->vis_w,il->vis_h);
        evas_object_resize(o,il->vis_w,il->vis_h);
        bg_new_el(il,o,-5001); }}}

  if((b->tint=evas_object_rectangle_add(evas))) {
    evas_object_color_set(b->tint, 255, 0, 0, 63);
    bg_new_el(il,b->tint,-5000); }

  b->il=il;
  il->bg=b;

  prop_bag_prop_default(te,strcpy(prop,"background-tint-alpha")?buf:buf,"63",0,255,1);

  prop_bag_prop_listen(te,LISTENER,strcpy(prop,"background-tint-colour")?buf:buf,bg_num_listener,BG_TINTCOL,b);
  prop_bag_prop_listen(te,LISTENER,strcpy(prop,"background-tint-alpha")?buf:buf, bg_num_listener,BG_TINTALPHA,b);

  return il->bg; }



/**
 * destruct background
 * @param data  the background to dest
 */
void bg_dst(void **data) {
  if(data&&*data) {
    bg_param *b=*((bg_param **)data);
    if(b) {
      if(b->trans) {
        evas_object_hide(b->trans);
        evas_object_del(b->trans); }
      if(b->pic) {
        evas_object_hide(b->pic);
        evas_object_del(b->pic); }
      if(b->tint) {
        evas_object_hide(b->tint);
      evas_object_del(b->tint); }
      free(b); }
    *data=NULL; }}

#ifndef HAVE_X
#  warning building the (transparent) background-plugin in the absence of X is a tad pointless...
#endif
