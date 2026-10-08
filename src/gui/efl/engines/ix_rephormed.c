/** @file  ix_rephormed.c
 *  @brief plugin that creates and handles a "text-path"
 *
 * written by Azundris, design idea by rephorm
 *
 * EXTENSIONS
 * (icon *i)il->theme->areas anchors a list of all extensions for icon-list il.
 * each of these "icon" entries describes a single extension.
 * user1 of each of these "icon" entries contains a pointer to a theme_area
 * struct that contains function pointers, a shared object-handle, and space
 * for extra user-data.
 * each of these "icon" entries is likely to have one or more sub-entries of
 * the "icon_el" type that describe the rectangle-list of the extension.
 * the user1 of each of these "icon_el" entries is not used by evidence and
 * may be used by the extensions to store pointers particular to the given
 * rectangle.  the nature of the data pointed to may vary from extension to
 * extension.
 *
 * icon
 *  +- user1{theme_area *}                   theme_area object
 *  |   +- user1,user2,user3,user4           extension-specific pointers
 *  |   +- lib                               library-handle
 *  |   +- ext_upd, ext_dst, ext_evt         functions in extension
 *  +- user2{void *}                         extension-specific pointer
 *  +- els{icon_el *}                        rectangle list of extension
 *  |   +- user1{file_info *}                extension-specific pointer
 *  |   +- xo,yo,w,h{int}                    position and area of rectangle
 *  |   +- ...
 *  |   +- next{icon_el *}                   next rectangle
 *  +- next{icon *}                          next extension
 *
 *
 *
 * TEXTPATH
 * each extension is described by an entry of the "icon" type.
 * its "user1" element points to an object of the "theme_area" type
 * that contains function pointers etc. for the particular extension.
 * a "textpath" extension will normally have a list of "icon_el" elements
 * attached to it, one for each element of the path to the current directory.
 * each icon_el will have its "user1" element pointing to the "file_info"
 * struct describing the directory referred to by the icon_el in evidence's
 * internal model of the file-system:
 *
 * icon
 *  +- user1{theme_area *}                   theme_area object
 *  |   +- user1,user2,user3{Evas_Object *}  light "corner" object, dark
 *  |   |                                    corner, drop shadow
 *  |   +- user4                             properties-struct
 *  |   +- lib                               library-handle
 *  |   +- ext_upd, ext_dst, ext_evt         functions in extension
 *  +- user2{file_info *}                    dir previously shown path for
 *  |                                        (in internal model of file-system)
 *  +- els{icon_el *}                        rectangle list of extension
 *  |   +- user1{file_info *}                file-info of referred-to dir
 *  |   |                                    in internal model of file-system
 *  |   +- xo,yo,w,h{int}                    position and area of rectangle
 *  |   +- ...
 *  |   +- next{icon_el *}                   next rectangle
 *  +- next{icon *}                          next extension
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "theme.h"
#include "debug.h"

#include "Evas.h"
#include "icon_list.h"
#include "errors.h"
#include "evas_macros.h"
#include "session.h"

#ifdef HAVE_IMLIB2
#  define X_DISPLAY_MISSING
#  include <Imlib2.h>
#  include "imlib.h"
#endif

#define BLUR    4

#define GRAD   32
#define DFL   (255-GRAD)
#define SEL   128
#define TXT     0
#define LINE    0



#define EXT_LABEL "textpath"
#define EXT_NAME  "rephormed"



/*****************************************************************************/
/* properties */
/**************/



#define LISTENER EXT_LABEL "{" EXT_NAME "}"

typedef enum {
  IX_FONTNAME,
  IX_FONTSIZE,
  IX_DROPSHADOW,
  IX_ALPHA,
  IX_SHRINKTOFIT
} ix_tag;

typedef struct _ix_param {
  icon_list *il;
  icon      *icon;
  char      *textpath_previous;
  char      *textpath_fontname;
  int        textpath_fontsize,textpath_fontsize_last;
  int        textpath_dropshadow;
  int        textpath_alpha;
  int        textpath_shrinktofit;
} ix_param;



int ext_upd(icon_list *il,icon *icon,void *data);



/**
 * a property (shadow, alpha, font, ...) of the textpath was changed, throw out its representation so it gets regenerated.
 * @param param - the parameter-block
 */
static int ix_expire_visuals(ix_param *param) {
  icon       *i;
  theme_area *ta;
  icon_el    *e;

  if(!(i=param->icon)||!(ta=i->user1))
    return NOIR_FAIL;

  e=i->els;
  while(e) {
    if(e->data==ta->user1)
      ta->user1=NULL;
    else if(e->data==ta->user2)
      ta->user2=NULL;
    else if(e->data==ta->user3)
      ta->user3=NULL;
    e=e->next; }

  if(ta->user1)
    evas_object_del(ta->user1);
  if(ta->user2)
    evas_object_del(ta->user2);
  if(ta->user3)
    evas_object_del(ta->user3);

  ta->user1=ta->user2=ta->user3=NULL;

  return NOIR_SUCC; }



/**
 * listen for changes in our numeric properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int ix_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  long           val=prop_get_int(e);
  const char    *key=prop_get_key(e);
  ix_param      *param=(ix_param *)data;
  int            ret;

  switch(tag) {
    case IX_FONTSIZE:
      param->textpath_fontsize=val; break;
    case IX_DROPSHADOW:
      param->textpath_dropshadow=val; break;
    case IX_ALPHA:
      param->textpath_alpha=val; break;
    case IX_SHRINKTOFIT:
      param->textpath_shrinktofit=val; break;

    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld/#%06lx, %p)\n",key,val,val,data);

  ix_expire_visuals(param);

  if((ret=ext_upd(param->il,param->icon,NULL))!=NOIR_SUCC)
    E(3,"update failed after property was changed: %d\n",ret);

  return NOIR_SUCC; }



/**
 * listen for changes in our string-properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int ix_str_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  char          *val=prop_get_string(e);
  const char    *key=prop_get_key(e);
  ix_param      *param=(ix_param *)data;

  if(val) {
    switch(tag) {
      case IX_FONTNAME:
        if(param->textpath_fontname)
          free(param->textpath_fontname);
        param->textpath_fontname=strdup(val);
        break;
      default:
        E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
        return NOIR_FAIL; }

    E(1,"(%s::=%s, %p)\n",key,val,data);

    ix_expire_visuals(param);

    return NOIR_SUCC; }

  return NOIR_IGNORED; }






/*****************************************************************************/
/* create path-elements using Imlib2 */
/*************************************/



#ifdef HAVE_IMLIB2

/**
 * draw the right-most "rounded off" element of the text-path
 * @return an Evas-object containing the right-most "rounded off" element of
 *         the text-path, or NULL for failure
 * @param evas - the canvas to create the image-object on
 * @param w - total width in pixels
 * @param h - total height in pixels
 * @param cw - width of rounded zone in pixels
 * @param ch - height of rounded zone in pixels
 * @param sel - selected (invert)?
 * @param drop_shadow (0, none; 2, drop-shadow)
 * @param alpha (0, translucent; 255, solid)
 */
static Evas_Object *evas_path_corner(Evas *evas,
                                     int w,int h,int cw,int ch,int sel,int drop_shadow,int alpha) {
  Evas_Object       *new=NULL;
  Imlib_Image        src=NULL,dst;
  Imlib_Color_Range  range;
  int                col=sel?SEL:DFL;
  int                tw,th;

  h++;

  tw=w+drop_shadow+BLUR;
  th=h+drop_shadow+BLUR;

  /* MASK *************************************/

  IMLIB_ACQUIRE;

  if(!evas||!(src=imlib_create_image(tw,th)))
    goto fail;

  imlib_context_set_cliprect(0,0,tw+1,th+1);
  imlib_context_set_image(src);
  imlib_image_set_format("argb");
  imlib_context_set_anti_alias(1);
  imlib_image_set_has_alpha(1);
  imlib_context_set_color(255,255,255, 255);

  imlib_image_clear();

  imlib_image_fill_rectangle(0,0,w-cw,h);    /* left  box */
  imlib_image_fill_rectangle(0,0,w,h-ch);    /* upper box */
  imlib_image_fill_ellipse(w-cw-1,h-ch-2,cw,ch);

  imlib_image_blur(1);



  /* IMAGE *************************************/

  if(!evas||!(dst=imlib_create_image(tw,th))) {
    imlib_free_image();
    goto fail; }

  imlib_context_set_cliprect(0,0,tw+1,th+1);
  imlib_context_set_image(dst);
  imlib_image_set_format("argb");
  imlib_context_set_anti_alias(1);
  imlib_image_set_has_alpha(1);

  imlib_image_clear();

  range=imlib_create_color_range();
  imlib_context_set_color_range(range);
  imlib_context_set_color(col-GRAD,col-GRAD,col-GRAD, 255);
  imlib_add_color_to_color_range(0);
  imlib_context_set_color(col+GRAD,col+GRAD,col+GRAD, 255);
  imlib_add_color_to_color_range(h);
  imlib_image_fill_color_range_rectangle(0,0,w-1,h-1,0);
  imlib_free_color_range();

  imlib_context_set_color(LINE,LINE,LINE, 255);
  imlib_context_set_cliprect(0,0,w+1,h+1);
  imlib_image_draw_line(w-1,0,w-1,h-ch,FALSE);            /* right */
  imlib_image_draw_line(0,h-1,w-cw+1,h-1,FALSE);          /* bottom */
  imlib_context_set_cliprect(w-cw-1,h-ch-1,cw+1,ch+1);
  imlib_image_draw_ellipse(w-cw-1,h-ch-1,cw,ch);          /* corner */

  imlib_image_copy_alpha_to_image(src,0,0);

  if(drop_shadow>0) {
    imlib_context_set_cliprect(0,0,tw+1,th+1);

    imlib_context_set_image(src);

    imlib_context_set_color(0,0,0, 255);
    imlib_image_fill_rectangle(0,0,w-cw,h); /* lower */
    imlib_image_fill_rectangle(0,0,w,h-ch); /* right */
    imlib_image_fill_ellipse(w-cw-1,h-ch-2,cw,ch);

    imlib_image_blur(BLUR);

    imlib_blend_image_onto_image(dst,1, 0,0,tw,th, 0,0,tw,th); }

  if((new=evas_object_image_add(evas))) {
    evas_object_image_alpha_set(new,1);
    evas_object_image_size_set(new,tw,th);
    evas_object_image_data_copy_set(new,imlib_image_get_data_for_reading_only());
    evas_object_resize(new,tw,th);
    evas_object_image_fill_set(new,0,0,tw,th);
    evas_object_image_data_update_add(new,0,0,tw,th);
    evas_object_color_set(new,255,255,255,alpha); }

  imlib_context_set_image(src);
  imlib_free_image();

  imlib_context_set_image(dst);
  imlib_free_image();

fail:
  IMLIB_RELEASE;
  return new; }



/**
 * create an evas-object containing a drop-shadow.
 * @return an Evas-object, or NULL for failure
 * @param evas - which canvas to create the new object on
 * @param drop_shadow (0, none; 2, drop-shadow)
 * @param alpha (0, translucent; 255, solid)
 */
static Evas_Object *evas_path_shadow(Evas *evas,int drop_shadow,int alpha) {
  Evas_Object       *new=NULL;
  Imlib_Image        src;
  int                tw,th;

  tw=16+drop_shadow+BLUR;
  th=drop_shadow+BLUR;

  IMLIB_ACQUIRE;

  if(!evas||!(src=imlib_create_image(tw,th+BLUR)))
    goto fail;

  imlib_context_set_cliprect(0,0,tw,th+BLUR);
  imlib_context_set_image(src);
  imlib_image_set_format("argb");
  imlib_context_set_anti_alias(1);
  imlib_image_set_has_alpha(1);

  imlib_image_clear();

  if(drop_shadow>0) {
    imlib_context_set_color(0,0,0, 255);
    imlib_image_fill_rectangle(0,0,tw,BLUR); /* lower */
    imlib_image_blur(BLUR);
    imlib_image_copy_rect(0,BLUR,tw,th,0,0); }

  if((new=evas_object_image_add(evas))) {
    evas_object_image_alpha_set(new,1);
    evas_object_image_size_set(new,tw,th);
    evas_object_image_data_copy_set(new,imlib_image_get_data_for_reading_only());
    evas_object_resize(new,tw,th);
    evas_object_image_fill_set(new,0,0,tw,th);
    evas_object_image_data_update_add(new,0,0,tw,th);
    evas_object_color_set(new,255,255,255,alpha); }

  imlib_free_image();

fail:
  IMLIB_RELEASE;

  return new; }
#endif






/*****************************************************************************/
/* public interface of an evidence-extension */
/*********************************************/



/**
 * destruct user-data of this extension
 * @param i - the instance of this extension
 */
int ext_dst(icon *i) {
  theme_area     *ta;
  if(!i)
    return NOIR_IGNORED;
  if((ta=((theme_area *)i->user1))) {
    ix_param *param;
    if((param=(ix_param *)ta->user4)) {  /* parm-blk */
      if(param->textpath_fontname)
        free(param->textpath_fontname);
      if(param->textpath_previous)
        free(param->textpath_previous);
      free(ta->user4);
      ta->user4=NULL; }}
  return NOIR_SUCC; }



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 * @param i      the theme-area corresponding to this instance of the extension
 */
int ext_die(noir_theme *theme,icon *i) {
  ext_dst(i);
  return prop_bag_prop_deaf(theme->data,LISTENER,NULL,NULL); }



/**
 * create/update the area "owned" by the extension
 * @return an error-code
 * @param il - the icon-list that is extended by this plugin
 * @param icon - the instance of this extension
 * @param data - the file-info struct of the current working directory
 */
int ext_upd(icon_list *il,icon *icon,void *data) {
  Evas           *evas=get_evas_from_iconlist(il);
  file_info      *fn=(file_info *)data,*fi=NULL,*fp=NULL,*fr=NULL;
  Evas_Object    *o;
  int             elt=0,sel=2,     /* current element index, selected elt */
                  x1=0,x2,         /* left/right of current element */
                  hpad=8,vpad=2,   /* horizontal/vertical padding */
                  w_,h_,           /* current width/height (buffer for macro) */
                  w,h,             /* current width/height */
                  wt=0,ht=0,       /* total width/height */
                  col,corner=8,    /* current colour, "corner" size */
                  fs=18;           /* actual font-size (as opposed to req) */
  Evas_Coord      _x,_y,_w,_h;     /* buffers for evas_object_geometry_get() */
  icon_el        *el=NULL;
  theme_area     *ta;
  int             sx=0,sy=0;

  noir_theme     *theme=(noir_theme *)il->theme;
/* noir_theme_el **te=&theme->data; */

  ix_param       *param=NULL;

  if(!evas) /* ||!data) */
    return NOIR_IGNORED;

  if(!icon) {
    E(0,"identity crisis: don't know who I am?\n");
    return NOIR_FAIL; }

  if(!(ta=((theme_area *)icon->user1))) {
    E(0,"I have no theme-area, and I must scream!\n");
    return NOIR_NODATA; }

  if(!(param=ta->user4)) {
    if(!(ta->user4=param=malloc(sizeof(ix_param)))) {
      E(0,"out of memory!?\n");
      return NOIR_OOM; }

    memset(param,0,sizeof(ix_param));

    param->il=il;
    param->icon=icon;

    prop_bag_prop_default(theme->data,EXT_LABEL "-font",         "vera.ttf",0, 0,0);
    prop_bag_prop_default(theme->data,EXT_LABEL "-font-size",    "18",      9,64,1);
    prop_bag_prop_default(theme->data,EXT_LABEL "-alpha",        "255",     0,255,1);
    prop_bag_prop_default(theme->data,EXT_LABEL "-dropshadow",   "2",       0,16,1);
    prop_bag_prop_default(theme->data,EXT_LABEL "-shrink-to-fit","0",       0,1,1);

    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-font",         ix_str_listener,IX_FONTNAME,   param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-font-size",    ix_num_listener,IX_FONTSIZE,   param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-alpha",        ix_num_listener,IX_ALPHA,      param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-shrink-to-fit",ix_num_listener,IX_SHRINKTOFIT,param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-dropshadow",   ix_num_listener,IX_DROPSHADOW, param); }

  /* remove our funky corners from list, we don't want those object_del()'d */
  el=icon->els;
  while(el) {
    if((el->data==ta->user1)||(el->data==ta->user2)||(el->data==ta->user3))
      el->data=NULL;
    el=el->next; }
  el=icon->els=icon_el_dst_all(icon->els);

  /* update path in icon when no path or previous path was left */
  if(!fn) {
    if(!param->textpath_previous||
       !(fn=file_info_fetch(NULL,param->textpath_previous))) {
      E(3,"discarded, no idea where I am...\n");
      return NOIR_NODATA; }}

  if(!param->textpath_previous||
     !(fi=file_info_fetch(NULL,param->textpath_previous)))
    fi=fn;
  else {
    fr=fi;
    while(fr&&(fr!=fn))
      fr=file_info_get_parent(fr);
    if(!fr)
      fi=fn; }

  fs=param->textpath_fontsize;

  /* if the textpath is too big to fit, try smaller letters. (Fletch) */
  if(param->textpath_shrinktofit) {
    int         els=0;

    /* this is the size we'd like. let's see whether if fits. */
    fr=fi;
    while((fr=file_info_get_parent(fr)))
      els++;
    els=els*2*hpad;
    if((o=evas_object_text_add(evas))) {
      evas_object_move(o,0,0);
      evas_object_text_text_set(o,file_info_get_tag(fi));
    try_get_chance:
      evas_object_text_font_set(o,param->textpath_fontname,fs);
      evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
      if((fs>9)&&((_w+els)>il->vis_w)) {
        fs--;
        goto try_get_chance; }
      evas_object_del(o); }

    /* has text-size changed from last time */
    if(param->textpath_fontsize_last!=fs) {
      /* force regeneration of corners */
      evas_object_del(ta->user1);
      evas_object_del(ta->user2);
      ta->user1=ta->user2=NULL;
      param->textpath_fontsize_last=fs; }}



  do {
    fr=fi;
    while(file_info_get_parent(fr)&&(file_info_get_parent(fr)!=fp))
      fr=file_info_get_parent(fr);
    fp=fr;

    o=evas_object_text_add(evas);
    evas_object_text_font_set(o,param->textpath_fontname,fs);
    evas_object_text_text_set(o,file_info_get_name(fr));
    evas_object_color_set(o,TXT,TXT,TXT,255);
    ICON_EL_PRE(IE_DFL,110,x1+hpad,vpad);
    evas_object_move(o,el->xo+sx,el->yo+sy);

    if(!h_)
      return NOIR_FAIL;

    x2=x1+_w+2*hpad;
    h=h_+2*vpad;
    w=x2-x1;

    sel=(fr==fn);
    col=sel?SEL:DFL;

    if(fr!=fi) {  /* draw box */
      o=evas_object_gradient_add(evas);
      evas_object_gradient_color_add(o, col-GRAD,col-GRAD,col-GRAD, param->textpath_alpha, h);
      evas_object_gradient_color_add(o, col+GRAD,col+GRAD,col+GRAD, param->textpath_alpha, h);
      evas_object_gradient_angle_set(o,0);
      evas_object_resize(o,w,h);
      ICON_EL_PRE(IE_DFL,90,x1,0);
      evas_object_move(o,el->xo+sx,el->yo+sy);
      el->user1=fr;

      o=evas_object_line_add(evas);
      evas_object_line_xy_set(o,0,0,0,h);
      evas_object_color_set(o,LINE,LINE,LINE, param->textpath_alpha);
      ICON_EL_PRE(IE_DFL,100,x2,0);
      evas_object_move(o,el->xo+sx,el->yo+sy); }
    else {   /* draw rounded box */

      /* gradient for square part */
      o=evas_object_gradient_add(evas);
      evas_object_gradient_color_add(o, col-GRAD,col-GRAD,col-GRAD, param->textpath_alpha, h);
      evas_object_gradient_color_add(o, col+GRAD,col+GRAD,col+GRAD, param->textpath_alpha, h);
      evas_object_gradient_angle_set(o,0);
      evas_object_resize(o,w-corner,h);
      ICON_EL_PRE(IE_DFL,95,x1,0);
      evas_object_move(o,el->xo+sx,el->yo+sy);
      el->user1=fr;

#ifdef HAVE_IMLIB2
      /* create rounded corner as needed */
      if(!ta->user1)
        ta->user1=evas_path_corner(evas,2*corner,h,2*corner,2*corner,FALSE,param->textpath_dropshadow,param->textpath_alpha);
      if(!ta->user2)
        ta->user2=evas_path_corner(evas,2*corner,h,2*corner,2*corner,TRUE, param->textpath_dropshadow,param->textpath_alpha);
      if(!ta->user3)
        ta->user3=evas_path_shadow(evas,param->textpath_dropshadow,param->textpath_alpha);

      /* get correct corner (normal or inverted), hide the other */
      if(sel) {
        if(ta->user1)
          evas_object_hide(ta->user1);
        o=ta->user2; }
      else {
        if(ta->user2)
          evas_object_hide(ta->user2);
        o=ta->user1; }
      if(!o) {
        o=evas_object_line_add(evas);
        evas_object_line_xy_set(o,0,0,1,1);
        E(0,"failed to create %s rounded corner!?\n",sel?"selected":"unselected"); }
      ICON_EL_PRE(IE_DFL,100,x2-corner,0);
      evas_object_move(o,el->xo+sx,el->yo+sy);
      el->user1=fr;
#else
      /* don't have imlib, can't have rounded corner, draw cut corner */
      { int h2=h-2*corner;

        /* background (goes underneath gradient, if any) */
        /* yes, this looks wrong.  apparently, we're working around a
           bug concerning polygons in evas_object_move(). */
        o=evas_object_polygon_add(evas);
        evas_object_polygon_point_add(o,x1,sy);
        evas_object_polygon_point_add(o,x1,h+sy);
        evas_object_polygon_point_add(o,x2-corner,h+sy);
        evas_object_polygon_point_add(o,x2+corner,h2+sy);
        evas_object_polygon_point_add(o,x2+corner,sy);
        evas_object_polygon_point_add(o,x1,sy);
        evas_object_color_set(o,col,col,col,param->textpath_alpha);
        ICON_EL_PRE(IE_DFL,90,x1,sy);
        evas_object_move(o,x1,sy);
        el->user1=fr;

        /* skewed line */
        o=evas_object_line_add(evas);
        evas_object_line_xy_set(o,0,2*corner,2*corner,0);
        evas_object_color_set(o,LINE,LINE,LINE, param->textpath_alpha);
        ICON_EL_PRE(IE_DFL,100,x2-corner,h2);
        evas_object_move(o,el->xo+sx,el->yo+sy);

        /* right line */
        o=evas_object_line_add(evas);
        evas_object_line_xy_set(o,0,0,0,h2);
        evas_object_color_set(o,LINE,LINE,LINE, param->textpath_alpha);
        ICON_EL_PRE(IE_DFL,100,x2+corner,0);
        evas_object_move(o,el->xo+sx,el->yo+sy); }
#endif

      /* bottom line */
      o=evas_object_line_add(evas);
      evas_object_line_xy_set(o,0,0,x2-corner,0);
      evas_object_color_set(o,LINE,LINE,LINE, param->textpath_alpha);
      ICON_EL_PRE(IE_DFL,100,0,h);
      evas_object_move(o,el->xo+sx,el->yo+sy); }

    x1=x2;
    elt++;
  } while(fr!=fi);

  /* resize drop shadow for normal elements */
  w=el->w-(((float)BLUR)/2.0);
  o=ta->user3;
  ICON_EL_PRE(IE_DFL,100,0,h);
  evas_object_resize(o,w,h_);
  evas_object_move(o,el->xo+sx,el->yo+sy);

  icon->x  =
  icon->y  =0;
  icon->w  =wt;
  icon->h  =ht;
  icon->els=el;



  /* scroll text-path if needed */
  if(icon->w>il->vis_w) {
    while(el) {
      if(el->user1==fn) {
        if(((icon->x+el->xo)<0)||((icon->x+el->xo+el->w)>il->vis_w)) {
          int mx,nx,dx;
          nx=il->vis_w-el->w;       /* center current element... */
          mx=icon->w-il->vis_w;     /* last element is at right border */
          dx=icon->x+el->xo;
          if(dx>mx)
            dx=mx;
          else
            dx-=nx/2;
          icon->x-=dx;
          icon_move(icon,icon->x,icon->y); }
        el=NULL; }
      else
        el=el->next; }}



  if(param->textpath_previous)
    free(param->textpath_previous);
  param->textpath_previous=strdup(file_info_get_tag(fn));

  return NOIR_SUCC; }
