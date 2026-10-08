/** @file  ic_nautilus.c
 *  @brief plugin that creates an icon.
 *
 * this is an "advanced" icon-factory that uses the icon_set_state() method
 * to modify icons dynamically rather than to precompute the states.
 * this is the suggested procedure.
 *
 * visually, this will create icons that look a bit like those used by the
 * nautilus file-manager: text-labels appear below the icon-image; when
 * selected, the text-label is underlaid with a grey box with filed off
 * corners.
 *
 * long text-labels are wrapped; for each wrap, the text-size is decreased
 * by one.
 *
 * icons may be overlaid with automatic "emblems" indicating that an item
 * might not be accessible or writable by the current user.  in the default
 * theme, this is indicated by the stop-sign and eye of horus, respectively.
 *
 * icons may be overlaid with additional textual information; at this time,
 * the number of items contained in a folder is superimposed over the folder's
 * icon.  if the exact number of items is not known, an estimate is made
 * ("123+" rather than "4711").  this estimate coincides with the number
 * of folders contained in the folder (normally the number of hardlinks-2).
 *
 * items that are symlinks are "ghosted"; they appear translucent.
 *
 * items that evidence's thumbnailer was able to generate previews for will
 * be show a preview in lieu of a datafile-icon.
 *
 * the factory installs a variety of properties; see below.
 * these properties can be set in themes, or from the typebuffer, e.g.
 * 'icontextcolour #ff00ff
 */

#include <string.h>
#include <Evas.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

#include "session.h"    /* noir_sess_redraw_files() */
#include "debug.h"
#include "errors.h"
#include "config.h"
#include "theme.h"
#include "evas_macros.h"
#include "icon_list.h"
#include "icon_path.h"



#define LISTENER "iconfactory{nautilus}"

typedef enum {
  IC_TEXTFONT,  IC_TEXTSIZE,  IC_TEXTSIZEMIN,
  IC_TEXTCOL,   IC_DSCOL,     IC_SELCOL,
  IC_TEXTALPHA, IC_DSALPHA,   IC_SELALPHA,
  IC_DSXOFFS,   IC_DSYOFFS,
  IC_MLBLXOFFS, IC_MLBLYOFFS, IC_MLBLWIDTH,
  IC_XRESIZE,   IC_YRESIZE,
  IC_GRID } ic_tag;

typedef struct _ic_param {
  icon_list *il;
  char      *text_font;
  int        text_size,text_size_min;
  long       text_colour,ds_colour,sel_colour;
  int        text_alpha, ds_alpha, sel_alpha;
  int        ds_x_offset,ds_y_offset;
  int        mlbl_x_offset,mlbl_y_offset,mlbl_width;
  float      xresize,yresize,grid;
  ic_mode    icm_save;
} ic_param;



/******************************************************************************/



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 */
int icon_die(noir_theme *theme) {
  ic_param    *param;
  int          c=0;

  if(!theme)
    return NOIR_FAIL;

  if(prop_bag_prop_deaf(theme->data,LISTENER,NULL,NULL)==NOIR_SUCC)
    c++;
  if((param=theme->icon_data)) {
    if(param->text_font)
      free(param->text_font);
    memset(param,0,sizeof(param));
    free(param);
    theme->icon_data=NULL;
    c++; }

  return c?NOIR_SUCC:NOIR_IGNORED; }



/**
 * listen for changes in our numeric properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int ic_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  long           val=prop_get_int(e);
  const char    *key=prop_get_key(e);
  float          fv;
  ic_param      *param=(ic_param *)data;
  noir_theme    *theme=(noir_theme *)param->il->theme;

  switch(tag) {
    case IC_GRID:
      fv=prop_get_float(e);
      if(param->grid!=fv) {
        if((param->grid=fv)) {
          param->icm_save=theme->icon_mode;
          theme->icon_mode=IC_AUTO; }
        else
          theme->icon_mode=param->icm_save;
        icon_list_layout_all(param->il); }
      break;

    case IC_XRESIZE:
      fv=prop_get_float(e);
      if(param->xresize!=fv) {
        param->xresize=fv;
        icon_list_layout_all(param->il); }
      break;

    case IC_YRESIZE:
      fv=prop_get_float(e);
      if(param->yresize!=fv) {
        param->yresize=fv;
        icon_list_layout_all(param->il); }
      break;



    case IC_TEXTSIZE:
      param->text_size=val; break;

    case IC_TEXTSIZEMIN:
      param->text_size_min=val; break;

    case IC_TEXTCOL:
      param->text_colour=val; break;
    case IC_TEXTALPHA:
      param->text_alpha=val; break;

    case IC_DSCOL:
      param->ds_colour=val; break;
    case IC_DSALPHA:
      param->ds_alpha=val; break;

    case IC_SELCOL:
      param->sel_colour=val; break;
    case IC_SELALPHA:
      param->sel_alpha=val; break;

    case IC_DSXOFFS:
      param->ds_x_offset=val; break;
    case IC_DSYOFFS:
      param->ds_y_offset=val; break;

    case IC_MLBLXOFFS:
      param->mlbl_x_offset=val; break;
    case IC_MLBLYOFFS:
      param->mlbl_y_offset=val; break;
    case IC_MLBLWIDTH:
      param->mlbl_width=val; break;

    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld/#%06lx, %p)\n",key,val,val,data);

  return NOIR_SUCC; }



/**
 * listen for changes in our string-properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int ic_str_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  char          *val=prop_get_string(e);
  const char    *key=prop_get_key(e);
  ic_param      *param=(ic_param *)data;

  if(val) {
    switch(tag) {
      case IC_TEXTFONT:
        if(param->text_font)
          free(param->text_font);
        param->text_font=strdup(val);
        noir_sess_redraw_files(param->il->sess);
        break;
      default:
        E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
        return NOIR_FAIL; }

    E(1,"(%s::=%s, %p)\n",key,val,data);
    return NOIR_SUCC; }

  return NOIR_IGNORED; }






/******************************************************************************/
/* icon-factory */
/****************/



/**
 * change an icon's state.
 *
 * an icon-factory may create elements for all states when the icon is first
 * created; in this case, icon_set_state() needs not be provided.
 * using icon_set_state() does however allow for more flexibility (and reduced
 * use of memory) by creating the desired elements "on the fly."
 * @return TRUE if evas_icon_set_state() wrapper show show/hide icon-elements
 * based on their states after calling this, FALSE otherwise.  (usually FALSE)
 * @param il - the icon-list
 * @param icon - the icon in question
 * @param s - the desired state
 */
int icon_set_state(icon_list *il,icon *icon,icon_state s) {
  icon_state   c=s^icon->state;
  icon_el     *el;
  void        *o=NULL;
  Evas        *evas=get_evas_from_iconlist(il);
  Evas_Coord   _x,_y,_w,_h;
  static char *noxs=NULL,*readonly=NULL;
  int          tr,tg,tb,ta,
               sr,sg,sb,sa;
  int          alpha=255;
  ic_param    *param=((noir_theme *)il->theme)->icon_data;



  if(!noxs) {
    if(!(noxs=icon_path_get(NULL,"emblem/no-access"))) {
      E(0,"icon-set set no no-access emblem, falling back to default...\n");
      noxs=PACKAGE_DATA_DIR "/icons/emblems/noxs.png"; }}
  if(!readonly) {
    if(!(readonly=icon_path_get(NULL,"emblem/read-only"))) {
      E(0,"icon-set set no read-only emblem, falling back to default...\n");
      readonly=PACKAGE_DATA_DIR "/icons/emblems/readonly.png"; }}



  E(5,"%s: $%x -> $%x  ($%x)\n",icon->label,icon->state,s,c);



  /* make symlinks semi-transparent */
  if(icon->user1) {
    alpha=posixmd_get_lmode((posixmd *)icon->user1)?159:255;
#if 0
    /* defer alpha-mod, we'll walk the images later on anyway when creating
       the hilight for mouse-overs */
    for(el=icon->els;(el=icon_el_get_by_tag(el,ET_ICON_IMAGE,&o));el=el->next)
      evas_object_color_set(o,255,255,255, alpha);
#endif
  }



  /* noXS */
  if(icon->user2) {
    file_info *fi=(file_info *)icon->user2;
    int        nn=(file_info_get_flags(fi)&(IF_NOUSE|IF_NOMODIFY))?1:0,no;

    no=(el=icon_el_get_by_tag(icon->els,ET_EMBLEM_IMAGE,&o))?1:0;

    if(no!=nn) {
      if(nn) {
        int   err;
        char *file=(file_info_get_flags(fi)&IF_NOUSE)?noxs:readonly;

        o=evas_object_image_add(evas);
        evas_object_image_file_set(o,file,NULL);
        if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
          E(0,"error #%d loading emblem \"%s\"...\n",err,file);
          evas_object_del(o);
          o=NULL; }
        if(o) {
          int w,h;
          evas_object_image_size_get(o,&w,&h);
          evas_object_resize(o,w,h);
          evas_object_image_fill_set(o,0,0,w,h);

          el=icon->els;

          ICON_EL_ADD(IE_ALL,50,0,0);
          el->tag=ET_EMBLEM_IMAGE;

          icon->els=el;

          if((el=icon_el_get_by_tag(icon->els,ET_EMBLEM_IMAGE,&o))) {
            icon->els->xo=el->xo+el->w/8;
            icon->els->yo=el->yo+el->h/8; }}}
      else
        icon_el_dst_by_tag(icon,ET_EMBLEM_IMAGE); }}



  /* keyboard selection gets gradient as drop-shadow */
  if(c&IE_KBD) {
    if(s&IE_KBD) {
      int         ht=icon->h;

      el=icon->els;

      o=evas_object_gradient_add(evas);
      ICON_EL_ADD(IE_KBD,-10,0,0);
      evas_object_resize(o,icon->w,ht);
      evas_object_gradient_color_add(o,  0,103,127,  0,  8);
      evas_object_gradient_color_add(o,  0,206,255, 63, 16);
      evas_object_gradient_angle_set(o,150);
      el->tag=ET_KEYED_IMAGE;

      icon->els=el; }
    else
      icon_el_dst_by_tag(icon,ET_KEYED_IMAGE); }



  /* selected items get inverted label */
  if(c&IE_SEL) {
    if(s&IE_SEL) {
      Evas_Object *z;
      Evas_Coord   _x,_y,_w,_h;
      noir_theme  *t=il->theme;
      Evas        *evas=get_evas_from_iconlist(il);
      int          tb=t->icon_height,
                   wt=icon->w,
                   ht=icon->h;

      el=icon->els;

      o=evas_object_rectangle_add(evas);
      evas_object_resize(o,wt,ht-tb);
      evas_object_color_set(o,55,55,55, 100);
      ICON_EL_ADD(IE_SEL,10,1,tb);
      el->tag=ET_LABEL_IMAGE;
      z=o;

      o=evas_object_rectangle_add(evas);
      evas_object_resize(o,wt+2,ht-tb-2);
      evas_object_color_set(o,96,96,96, 100);
      ICON_EL_ADD(IE_SEL,10,0,tb+1);
      el->tag=ET_LABEL_IMAGE;

      icon->els=el; }
    else
      icon_el_dst_by_tag(icon,ET_LABEL_IMAGE); }



  /* text / drop-shadow colours */
  if(s&IE_SEL) {
    sr=(param->text_colour>>16);
    sg=(param->text_colour>>8)&0xff;
    sb=(param->text_colour)&0xff;
    sa=param->ds_alpha;  /* alpha remains unswapped! */

    tr=(param->ds_colour>>16);
    tg=(param->ds_colour>>8)&0xff;
    tb=(param->ds_colour)&0xff;
    ta=param->text_alpha; }
  else {
    tr=(param->text_colour>>16);
    tg=(param->text_colour>>8)&0xff;
    tb=(param->text_colour)&0xff;
    ta=param->text_alpha;

    sr=(param->ds_colour>>16);
    sg=(param->ds_colour>>8)&0xff;
    sb=(param->ds_colour)&0xff;
    sa=param->ds_alpha; }

  for(el=icon->els;(el=icon_el_get_by_tag(el,ET_LABEL_TEXT_HI,&o));el=el->next)
    evas_object_color_set(o,tr,tg,tb, ta);

  for(el=icon->els;(el=icon_el_get_by_tag(el,ET_LABEL_TEXT_LO,&o));el=el->next)
    evas_object_color_set(o,sr,sg,sb, sa);



  /* hilight on mouse-over */
  if(s&IE_HI) {
    for(el=icon->els;(el=icon_el_get_by_tag(el,ET_ICON_IMAGE,&o));el=el->next)
      evas_object_color_set(o,255,255,255, alpha); }
  else {
    for(el=icon->els;(el=icon_el_get_by_tag(el,ET_ICON_IMAGE,&o));el=el->next)
      evas_object_color_set(o,240,240,240, alpha); }



  /* redraw item */
  icon_move(icon,icon->x-il->sx,icon->y-il->sy);

  return FALSE; }



/**
 * create elements of an icon.
 * @return the first element of a linked list of icon-elements.
 * @param il - the icon-list to create the icon in
 * @param icon - the icon the elements are intended for
 * @param thumb - the path to a thumb-nailed image of the portrayed file's contents, or NULL
 */
icon_el *icon_els_create(icon_list *il,icon *icon,char *thumb) {
  noir_theme  *theme=(noir_theme *)il->theme;
  icon_el     *el=NULL,*ic;
  Evas        *evas=get_evas_from_iconlist(il);
  Evas_Object *o=NULL;
  Evas_Coord   _x,_y,_w,_h;
  int          w_,h_,wt=0,ht=0,tx=2,ty=0,iw,ih,err,links,fs_;
  float        gw;
  char        *fm,*to;
  posixmd     *intrinsic=(posixmd *)(icon?icon->user1:NULL);
  file_info   *fi=(file_info *)(icon?icon->user2:NULL);

  ic_param    *param=NULL;
  int          tr,tg,tb,ta,
               sr,sg,sb,sa;
  int          fs;
  int          mc=0; /* sanity check */

  icon->state&=~IE_THMB;

  if((o=evas_thumbnail_dead(theme,evas,fi))) {
    icon->state|=IE_THMB;
    evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
    w_=_w; h_=_h; }

  if(!(param=theme->icon_data)) {
    if(!(theme->icon_data=param=malloc(sizeof(ic_param)))) {
      E(0,"out of memory!?\n");
      return NULL; }

    memset(param,0,sizeof(ic_param));

    param->il=il;

    /* when decreasing the text-size with each wrap,
       don't make the font smaller than this */

    prop_bag_prop_default(theme->data,"icon-font","vera.ttf",0,0,0);
    prop_bag_prop_default(theme->data,"icon-font-size","20",3,128,1);
    prop_bag_prop_default(theme->data,"icon-font-size-minimum","9",4,48,1);
    prop_bag_prop_default(theme->data,"icon-text-colour","#0",0,0,0);
    prop_bag_prop_default(theme->data,"icon-text-alpha","255",0,255,1);
    prop_bag_prop_default(theme->data,"icon-shadow-colour","#ffffff",0,0,0);
    prop_bag_prop_default(theme->data,"icon-shadow-alpha","129",0,255,1);
    prop_bag_prop_default(theme->data,"icon-shadow-x-offset","1.0",-64,64,1);
    prop_bag_prop_default(theme->data,"icon-shadow-y-offset","1.0",-64,64,1);
    prop_bag_prop_default(theme->data,"icon-meta-label-x-offset","17",0,512,1);
    prop_bag_prop_default(theme->data,"icon-meta-label-y-offset","1",0,512,1);
    prop_bag_prop_default(theme->data,"icon-meta-label-width","92",0,512,1);
    prop_bag_prop_default(theme->data,"icon-list-grid","-1.0",-4,128,0.01);
    prop_bag_prop_default(theme->data,"icon-x-resize","0.0",-4,256,0.1);
    prop_bag_prop_default(theme->data,"icon-y-resize","0.0",-4,256,0.1);

    prop_bag_prop_listen(theme->data,LISTENER,"icon-font",               ic_str_listener,IC_TEXTFONT,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-font-size",          ic_num_listener,IC_TEXTSIZE,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-font-size-minimum",  ic_num_listener,IC_TEXTSIZEMIN,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-text-colour",        ic_num_listener,IC_TEXTCOL,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-text-alpha",         ic_num_listener,IC_TEXTALPHA,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-colour",      ic_num_listener,IC_DSCOL,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-alpha",       ic_num_listener,IC_DSALPHA,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-x-offset",    ic_num_listener,IC_DSXOFFS,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-shadow-y-offset",    ic_num_listener,IC_DSYOFFS,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-meta-label-x-offset",ic_num_listener,IC_MLBLXOFFS,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-meta-label-y-offset",ic_num_listener,IC_MLBLYOFFS,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-meta-label-width",   ic_num_listener,IC_MLBLWIDTH,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-list-grid",          ic_num_listener,IC_GRID,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-x-resize",           ic_num_listener,IC_XRESIZE,param);
    prop_bag_prop_listen(theme->data,LISTENER,"icon-y-resize",           ic_num_listener,IC_YRESIZE,param); }

  if(!o) {
    if(!icon->name||!*icon->name)
      E(0,"cannot thumb-nail, and no fallback icon given...\n");
    else {
      o=evas_object_image_add(evas);
      evas_object_image_file_set(o,icon->name,NULL);
      if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
        E(0,"error #%d loading \"%s\"...\n",err,icon->name);
        evas_object_del(o);
        o=NULL; }
      if(o) {
        evas_object_image_size_get(o,&w_,&h_);

        /* stretch icon if requested */
        if(param->xresize) {
          if(param->xresize<0)
            w_*=-param->xresize;
          else
            w_=w_*param->xresize/theme->icon_width; }
        if(param->yresize) {
          if(param->yresize<0)
            h_*=-param->yresize;
          else
            h_=h_*param->yresize/theme->icon_height; }

        evas_object_resize(o,w_,h_);
        evas_object_image_fill_set(o,0,0,w_,h_); }}}

  iw=w_;
  ih=h_;

  fs=param->text_size;

  ICON_EL_PRE((IE_DFL|IE_SEL),0,0,(theme->icon_height-h_)/2.0);
  mc++;
  el->tag=ET_ICON_IMAGE;
  ic=el;

  if(!theme->icon_height) {
    theme->icon_height=ih; }
  if(!theme->icon_width) {
    theme->icon_width=iw; }

  if((gw=param->grid)<0)
    gw=-gw*((float)theme->icon_width);

  ty=theme->icon_height;




/*****************************************************************************/

  long_to_rgb(param->text_colour,&tr,&tg,&tb);
  ta=param->text_alpha;

  long_to_rgb(param->ds_colour,&sr,&sg,&sb);
  sa=param->ds_alpha;

  /* re-scale font -- shrink for each line-wrap */
  { fs_=fs/6;
    fs_=fs_?fs_:1;
    fm=icon->label;
    while((fm=strchr(fm,'\n'))) {
      if((fs-fs_)>=param->text_size_min)
        fs-=fs_;
      else
        fs=param->text_size_min;
      fm++; }}

  fm=icon->label;        /* support text-wrap */



/*****************************************************************************/

  while(fm) {
    if((to=strchr(fm,'\n')))
      *to='\0';

    o=evas_object_text_add(evas);
    fs_=fs;
    evas_object_text_text_set(o,fm);
  try_text:
    evas_object_text_font_set(o,param->text_font,fs);
    evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
    /* if icon-grid is enabled and our new label is too big, try a smaller
       font... */
    if(param->grid!=0) {
      if((w_=gw)<=_w) {
        int fd=(fs/6);
        fd=fs-((fd<1)?1:fd);
        if(fd>=param->text_size_min) {
          fs=fd;
          goto try_text; }}}

    if(w_>_w)
      tx=((w_-_w)/2.0);
      
    evas_object_color_set(o,tr,tg,tb, ta);        /* black */
    ICON_EL_PRE((IE_DFL|IE_KBD),20,tx,ty);
    mc++;
    el->tag=ET_LABEL_TEXT_HI;

    o=evas_object_text_add(evas);
    evas_object_text_font_set(o,param->text_font,fs);
    evas_object_text_text_set(o,fm);
    evas_object_color_set(o,sr,sg,sb, sa);  /* white/trans */
    ICON_EL_PRE((IE_DFL|IE_KBD),18,tx+param->ds_x_offset,ty+param->ds_y_offset);
    mc++;
    el->tag=ET_LABEL_TEXT_LO;

#if 0
    /* normally, if we need extra-shrinkage for the label to satisfy the
       constraints of the grid, we'd reset the text-size to the original
       size here.  more specifically, we'd reset to the user-defined text-
       size adjusted for number of lines.  however, as the last line is
       often the shortest (remaining letters), this leads to shrinkage for
       all lines but the last, which looks really weird.  hence, once we're
       forced to extra-shrink for the label, we'll stick with the smaller
       size.  sounds wonky, but does the right thing(TM). */

    fs=fs_;    /* if we need extra-shrinkage to satisfy the grid, reset */
#endif

    /* more lines in label? advance. */
    if(to) {
      *(to++)='\n';
      fm=to;
      ty+=fs_; }
    else
      fm=NULL; }






/*****************************************************************************/

/* For the image portion to be of the same size, the icon-mode must be
   "auto" (see config-files).  The icon-factory must then simply make
   sure labels are all the same size; here, this is done by scaling
   down the text until it fits.  Then, an invisible evas box is put
   underneath the icon's image that's the size of one grid box.  If
   the text is shorter, the wider (invisible) box will keep the
   overall bounding box of the icon wide, the same width as all the
   other icons. */

  if(param->grid) {
    o=evas_object_rectangle_add(evas);
    evas_object_resize(o,gw,theme->icon_height);
    evas_object_color_set(o,55,55,55, 0);
    ICON_EL_PRE(IE_SEL,10,0,0);
    el->tag=ET_MISC; }



  /* additional labels (folders contained etc.) ******************************/

  icon->w=wt+4;
  icon->h=ht;



  /* link-count */
  { char b[9];

    if((links=file_info_get_num_children(fi))>0)
      snprintf(b,7,"%d",links<999999?links:999999);
    else if(intrinsic&&S_ISDIR(posixmd_get_mode(intrinsic))) {
      links=posixmd_get_hardlinks(intrinsic)-
      (((theme->tf&TF_DOTDIR)&&!(file_info_get_flags(fi)&IF_DOTDIR))?1:2);
      snprintf(b,7,"%d+",links<999999?links:999999); }
    else
      links=-1;

    if(links>=0) {
      o=evas_object_text_add(evas);
      evas_object_text_font_set(o,param->text_font,fs);
      evas_object_text_text_set(o,b);
      evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
      tx=ic->xo+((ic->w-_w)/2);

      ty=(ic->h-_h)/2;
      evas_object_color_set(o,255,255,255, 129);   /* white/trans */

      ICON_EL_PRE((IE_DFL|IE_KBD),18,tx+1,ty+1);
      mc++;
      el->tag=ET_ICON_TEXT_LO; }}






  /* SortKey */
  { const char *p=file_info_get_sortkey(fi);    /* meta-data sortkey? */

    if(p&&*p) {
      float zz=((float)fs)*0.6;

      o=evas_object_text_add(evas);
      evas_object_text_font_set(o,param->text_font,zz);
      evas_object_text_text_set(o,p);
      evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
      tx=ic->xo+((ic->w-_w)/2);

      if(_w>wt) {
        evas_object_resize(o,_w=wt,_h);
     /* tx=ic->xo+((ic->w-_w)/2); */
        tx=0; }
      if(tx<param->mlbl_x_offset)
        tx=param->mlbl_x_offset;
      ty=ht-2;
      evas_object_color_set(o,0,0,0, 129);         /* black/trans */

      ICON_EL_PRE((IE_DFL|IE_KBD),18,tx+1,ty+1);
      mc++;
      el->xo=(int)((wt-el->w)/2);                  /* center label */
      el->tag=ET_ICON_TEXT_LO;

      icon->h=ht; }}



  ic->xo=(int)((wt-ic->w)/2);  /* center image */

  icon->els=el;

/* icon_set_state(il,icon,icon->state); */

  return el; }
