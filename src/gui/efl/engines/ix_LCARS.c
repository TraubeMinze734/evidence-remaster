/** @file  ix_LCARS.c
 *  @brief plugin that creates and handles a "text-path"
 *
 * written by Azundris
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
 *  |   +- user1,user2,user3                 reserved
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
#include "util.h"    /* esprintf() */

#include "Evas.h"
#include "icon_list.h"
#include "errors.h"
#include "evas_macros.h"
#include "session.h"

#define GRAD   32
#define DFL   (255-GRAD)
#define SEL   128
#define TXT     0
#define LINE    0



#define EXT_LABEL "textpath"
#define EXT_NAME  "LCARS"



/*****************************************************************************/
/* properties */
/**************/



#define LISTENER EXT_LABEL "{" EXT_NAME "}"

typedef enum {
  IX_FONTNAME,
  IX_FONTSIZE,
  IX_ALPHA,
  IX_INACTIVE,
  IX_ACTIVE,
  IX_PADDING,
  IX_HIDE,
  IX_CORNERFACTOR,IX_CORNERW,IX_CORNERH,
  IX_X,IX_Y,IX_W,IX_H
} ix_tag;

typedef struct _ix_param {
  icon_list *il;
  icon      *icon;
  char      *textpath_previous;
  char      *textpath_fontname;
  int        textpath_fontsize;
  int        textpath_alpha;
  int        hide;
  int        padding,x,y,w,h;
  int        cornerw,cornerh;
  float      cornerfactor;
  long       active_colour,inactive_colour;
} ix_param;



int ext_upd(icon_list *il,icon *icon,void *data);



static Evas_Object *box(ix_param *param,Evas *evas,int sel,int w,int h) {
  int r,g,b,col;
  Evas_Object *o=NULL;

  col=sel?param->active_colour:param->inactive_colour;

  r=col>>16;
  g=(col>>8)&0xff;
  b=col&0xff;

  /* box */
  if((o=evas_object_rectangle_add(evas))) {
    evas_object_color_set(o,r,g,b,param->textpath_alpha);
    evas_object_resize(o,w,h); }
  return o; }



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
    case IX_ALPHA:
      param->textpath_alpha=val; break;

    case IX_INACTIVE:
      param->inactive_colour=val; break;
    case IX_ACTIVE:
      param->active_colour=val; break;

    case IX_X:
      param->x=val; break;
    case IX_Y:
      param->y=val; break;
    case IX_W:
      param->w=val; break;
    case IX_H:
      param->h=val; break;

    case IX_PADDING:
      param->padding=val; break;

    case IX_HIDE:
      param->hide=val; break;

    case IX_CORNERFACTOR:
      if(!(param->cornerfactor=prop_get_float(e)))
        param->cornerfactor=1.0;
      break;
    case IX_CORNERW:
      param->cornerw=val; break;
    case IX_CORNERH:
      param->cornerh=val; break;

    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld/#%06lx, %p)\n",key,val,val,data);

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

    return NOIR_SUCC; }

  return NOIR_IGNORED; }






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
  Evas_Object    *o,*clip=NULL;
  int             elt=0,sel=2,     /* current element index, selected elt */
                  x1=0,x2,         /* left/right of current element */
                  hpad=8,vpad=2,   /* horizontal/vertical padding */
                  w_,h_,           /* current width/height (buffer for macro) */
                  w,h,             /* current width/height */
                  wt=0,ht=0;       /* total width/height */
  Evas_Coord      _x,_y,_w,_h;     /* buffers for evas_object_geometry_get() */
  icon_el        *el=NULL,*sb=NULL;
  theme_area     *ta;
  int             sx=0,sy=0,sw=0;
  char           *file=NULL;

  noir_theme     *theme=(noir_theme *)il->theme;
/* noir_theme_el **te=&theme->data; */

  ix_param       *param=NULL;



  if(!il->vis_h) {
    E(4,"il->vis_h not set, ignored...\n");
    return NOIR_IGNORED; }



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

    prop_bag_prop_default(theme->data,EXT_LABEL "-font",      "vera.ttf",0, 0,0);
    prop_bag_prop_default(theme->data,EXT_LABEL "-font-size", "18",      9,64,1);
    prop_bag_prop_default(theme->data,EXT_LABEL "-alpha",     "255",     0,255,1);

    prop_bag_prop_default(theme->data,EXT_LABEL "-x",         "32",      0,255,1);
    prop_bag_prop_default(theme->data,EXT_LABEL "-y",         "5",       0,255,1);
    prop_bag_prop_default(theme->data,EXT_LABEL "-w",         "255",     0,255,1);
    prop_bag_prop_default(theme->data,EXT_LABEL "-h",         "0",       0,255,1);

    prop_bag_prop_default(theme->data,EXT_LABEL "-hide",      "255",     0,255,1);

    prop_bag_prop_default(theme->data,EXT_LABEL "-padding",   "4",       0,16,1);

    prop_bag_prop_default(theme->data,EXT_LABEL "-corner-factor","1.0",   0,8,0.01);
    prop_bag_prop_default(theme->data,EXT_LABEL "-corner-w",     "0",     0,255,1);
    prop_bag_prop_default(theme->data,EXT_LABEL "-corner-h",     "0",     0,255,1);

    prop_bag_prop_default(theme->data,EXT_LABEL "-active-colour",  "#ce9ccf", 0,0,0);
    prop_bag_prop_default(theme->data,EXT_LABEL "-inactive-colour","#ff9c00", 0,0,0);




    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-font",          ix_str_listener,IX_FONTNAME,  param);

    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-x",  ix_num_listener,IX_X,    param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-y",  ix_num_listener,IX_Y,    param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-w",  ix_num_listener,IX_W,    param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-h",  ix_num_listener,IX_H,    param);

    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-padding",       ix_num_listener,IX_PADDING,   param);

    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-hide",          ix_num_listener,IX_HIDE,      param);

    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-corner-factor",  ix_num_listener,IX_CORNERFACTOR, param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-corner-w",       ix_num_listener,IX_CORNERW,    param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-corner-h",       ix_num_listener,IX_CORNERH,    param);

    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-active-colour",  ix_num_listener,IX_ACTIVE,    param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-inactive-colour",ix_num_listener,IX_INACTIVE,  param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-font-size",      ix_num_listener,IX_FONTSIZE,  param);
    prop_bag_prop_listen(theme->data,LISTENER,EXT_LABEL "-alpha",         ix_num_listener,IX_ALPHA,     param); }


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

  sx=param->x;
  sy=param->y;



  /***************************************************************************/
  /* side-bar */

  esprintf(&file,"%s/%s/%s",theme->path,theme->name,"LCARS_ul.png");

  o=NULL;
  if(file) {
    if((o=evas_object_image_add(evas))) {
      int err;
      evas_object_image_file_set(o,file,NULL);
      if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
        E(0,"error #%d loading image \"%s\"...\n",err,file);
        evas_object_del(o);
        o=NULL; }}
    free(file); }

  if(o) {
    long col;
    int  r,g,b;

    /* corner */

    evas_object_image_size_get(o,&w,&h);

    if(param->cornerfactor!=1.0) {
      w=(float)w*param->cornerfactor;
      h=(float)h*param->cornerfactor; }
    if(param->cornerw)
      w=param->cornerw;
    if(param->cornerh)
      h=param->cornerh;

    evas_object_resize(o,w,h);
    evas_object_image_fill_set(o,0,0,w,h);
    ICON_EL_PRE(IE_DFL,110,0,0);
    el->tag=ET_LABEL_IMAGE;

    col=param->inactive_colour;
    r=col>>16;
    g=(col>>8)&0xff;
    b=col&0xff;
    evas_object_color_set(o,r,g,b,param->textpath_alpha);
    evas_object_move(o,sx,sy);
    x1=w+param->padding;

    /* bar */

    if(param->h) {
      int ch=h,rh=2*ch,i;
      int lt=il->vis_h-sy,ln=lt-ch-rh;
      int els=3,elh=ln/els;
      int bx,by,bw,bh;

      /* lower box */
      if((o=box(param,evas,0,param->h,rh))) {
        ICON_EL_PRE(IE_DFL,90,sx,il->vis_h-rh-1+param->padding);
        el->tag=ET_LABEL_OTHER;
        evas_object_move(o,el->xo,el->yo); }

      /* other boxes */
      for(i=0;i<els;i++) {
        bw=param->h;
        bh=elh-param->padding;
        bx=sx;
        by=sy+ch+param->padding+elh*i;

        if((o=box(param,evas,1,bw,bh))) {
          E(5,"%d,%d %dx%d\n",bx,by,bw,bh);
          ICON_EL_PRE(IE_DFL,90,bx,by);
          el->tag=ET_LABEL_OTHER;
          evas_object_move(o,el->xo,el->yo); }}}}

  sw=x1+hpad;

  /* clip */
  if((o=evas_object_rectangle_add(evas))) {
    evas_object_color_set(o,255,255,255, 255);
    evas_object_resize(o,il->vis_w-x1,h+sy);
    ICON_EL_PRE(IE_DFL,-1111,sw,0);
    evas_object_move(o,el->xo,el->yo);
    clip=o; }

  /* lowlight/hide underneath path */
  if(param->hide&&(o=evas_object_rectangle_add(evas))) {
    evas_object_color_set(o,0,0,0, param->hide);
    evas_object_resize(o,il->vis_w-x1,h+sy);
    ICON_EL_PRE(IE_DFL,80,sw,0);
    evas_object_move(o,el->xo,el->yo); }

  sb=el;
  el=NULL;



  /***************************************************************************/
  /* path */

  do {
    fr=fi;
    while(file_info_get_parent(fr)&&(file_info_get_parent(fr)!=fp))
      fr=file_info_get_parent(fr);
    fp=fr;

    /* text */
    o=evas_object_text_add(evas);
    evas_object_text_font_set(o,param->textpath_fontname,param->textpath_fontsize);
    evas_object_text_text_set(o,file_info_get_name(fr));
    evas_object_color_set(o,TXT,TXT,TXT,255);
    ICON_EL_PRE(IE_DFL,110,x1+hpad,vpad);
    if(param->h)
      el->yo=(param->h-h_)/2;
    evas_object_move(o,el->xo+sx,el->yo+sy);

    if(!h_)
      return NOIR_FAIL;

    if(!(h=param->h))
      h=h_+2*vpad;

    x2=x1+_w+2*hpad;
    w=x2-x1;

    sel=(fr==fn);

    if((o=box(param,evas,sel,w,h))) {
      ICON_EL_PRE(IE_DFL,90,x1,0);
      evas_object_move(o,el->xo+sx,el->yo+sy);
      el->user1=fr; }

    x1=x2+param->padding;
    elt++;
  } while(fr!=fi);

  icon->x  =sx;
  icon->y  =sy;
  icon->w  =wt;
  icon->h  =ht;
  icon->els=el;



  /* scroll text-path if needed */
  if((icon->x+icon->w)>il->vis_w) {
    int xo=icon->x+sw+param->padding;
    int vis_w=il->vis_w-xo;

    while(el) {
      if(el->user1==fn) {
        if(((xo+el->xo)<0)||((xo+el->xo+el->w)>vis_w)) {
          int mx,nx,dx;
          nx=vis_w-el->w;       /* center current element... */
          mx=icon->w-vis_w;     /* last element is at right border */
          dx=xo+el->xo;
          if(dx>mx)
            dx=mx;
          else
            dx-=nx/2;
          icon->x-=(dx-xo);
          icon_move(icon,icon->x,icon->y); }
        el=NULL; }
      else
        el=el->next; }}

  for(el=icon->els;el;el=el->next) {
/*  E(0,"%s: %d+%d=%d <> %d\n",el->user1?file_info_get_name((file_info *)el->user1):"",icon->x,el->xo,icon->x+el->xo,sx+sw); */
    if((icon->x+el->xo)<=(sx+sw)) {
      if((icon->x+el->xo+el->w)>=sw)
        evas_object_clip_set(el->data,clip);
      else
	evas_object_hide(el->data); }
    if(!el->next) {
      el->next=sb;
      break; }}



  if(param->textpath_previous)
    free(param->textpath_previous);
  param->textpath_previous=strdup(file_info_get_tag(fn));

  return NOIR_SUCC; }
