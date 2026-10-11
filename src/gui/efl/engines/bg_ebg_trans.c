/** @file  bg_ebg_trans.c
 *  @brief background-handler using the (deprecated) e-bg format
 *
 * ebg was the format used by the first prototype of enlightenment DR17.
 * it lets you compose a background out of individual items such as gradients,
 * images etc.  each item can be placed/resized to fit the current resolution.
 * this version of the plugin provides pseudo-transparency, so the items of
 * the ebg will be laid out on a seemingly transparent window.  (PT means that
 * you will see the "root-window" -- the desktop -- shining through, but not
 * other windows.)
 * to compile this plugin, only edb is required, but to create your own
 * ebgs (.bg.db), you'll need ebony, the graphical ebg-editor.
 */

#include "config.h"
#include "evidence.h"   /* get_display() */
#include "evas_macros.h"
#include "theme.h"
#include "icon_list.h"
#include "setup.h"

#include <Evas.h>
#include <stdio.h>

#if defined(HAVE_EDB)
#  include "Ebg.h"

#  ifdef HAVE_X
#    include "gui/common/x11.h"
#    include <X11/Xlib.h>
#    include <X11/Xatom.h>
#    include <X11/Intrinsic.h>
#    include <Imlib2.h>
#    include "imlib.h"
#  endif

/* obtain a partial copy of the root-pixmap for pseudo-transparency effects. */
#  ifdef HAVE_X

Atom rootpmap=None,xpixmap=None,xcolor=None;



/**
 * find root-window our application window lives in
 * @return the "desktop"
 * @param app  our application-window
 */
Window get_desk(Window app) {
  if(!get_display())   /* not on X11 (e.g. Wayland session) */
    return None;
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
        evas_object_layer_set(new,-9999);
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



#  else
#    warning building the ebg-type transparency background-plugin without calls to X being allowed will result in a transparency-less plugin for now...
#  endif



/**
 * callback: view was scrolled, update background
 * @param data  the icon-list (contains coordinates and pointer to background)
 */
void bg_scroll(void *data) {
  icon_list *il=(icon_list *)data;
  E_Background ebg=(E_Background)il->bg;
  e_bg_move(ebg,il->sx,il->sy);
  evas_object_move(ebg->base_obj,il->sx,il->sy); }



/**
 * callback: update background (window resized or moved, backdrop changed, ...)
 * moving is relevant since the plugin uses pseudo-transparency!
 * @param il  the icon-list (contains pointer to background)
 * @param x   horizontal position
 * @param y   vertical position
 * @param w   width
 * @param h   height
 */
void bg_upd(void *il,int x,int y,int w,int h) {
  E_Background bg=((icon_list *)il)->bg;

#  ifdef HAVE_X
  bg->base_obj=transparency_get_pixmap(il,bg->base_obj,x,y,w,h);
#  endif
  e_bg_resize(bg,w,h);
  e_bg_show(bg); }



/**
 * set an ebg on an evas-canvas
 * @return the newly set E_Background
 * @param evas     the canvas to set the background on
 * @param edbname  the name of the file containing the ebg db
 */
static E_Background set_ebg(Evas *evas,char *edbname) {
  E_Background new_bg;

  if(!(new_bg=e_bg_load(edbname))) {
    E(0,"failed to load \"%s\"...\n",edbname);
    return NULL; }

  e_bg_add_to_evas(new_bg,evas);
  e_bg_set_scroll(new_bg,0,0);
  e_bg_set_layer(new_bg,-5000);

  return new_bg; }
#endif



/**
 * set a background on a view
 * @return the newly set background object (il->bg)
 * @param t   the current theme in this session
 * @param il  the icon-list
 * @param fp  the name of the ebg-file
 */
void *bg_new(struct _noir_theme *t,icon_list *il,char *fp) {
#ifdef HAVE_EDB
  int            ok=0;

  if(!il) {
    return NULL; }

  { size_t l=strlen(fp);
    if((l>6)&&(!strcmp(".bg.db",&fp[l-6]))) {
      E_Background en;
      if((en=set_ebg(get_evas_from_iconlist(il),fp))) {
        if(il->bg)
          e_bg_free(il->bg);
        ok=1;
        il->bg=en; }
      else
        E(0,"cannot set \"%s\"!?\n",fp); }
    else
      E(0,"\"%s\" does not look like a background database?\n",fp); }
  if(!ok) {
    if(il->bg)
      e_bg_free(il->bg);
    il->bg=NULL; }
#else
  E(0,"built without libebg, how utterly pointless! :-S\n");
  if(il)
    il->bg=NULL;
  else
    return NULL;
#endif
  return il->bg; }



/**
 * destruct background
 * @param data  the background to dest
 */
void bg_dst(void **data) {
  if(data&&*data) {
#ifdef HAVE_EDB
    e_bg_free((E_Background) *data);
#endif
    *data=NULL; }}

#ifndef HAVE_EDB
#  warning building the ebg-type (transparent) background-plugin in the absence of libedb is a tad pointless...
#endif
