/** @file  thumbnailer/ttf/ttf.c
 *  @brief truetype font thumbnailer
 */

#include <stdlib.h>      /* malloc(), free() */
#include <string.h>      /* strlen() */
#include <stdio.h>       /* strlen() */
#include <math.h>        /* sqrt() */

#include "config.h"
#include "debug.h"
#include "errors.h"
#include "util.h"

#include "../thumbnail_private.h"
#include "../thumbnail_plugin.h"

#include "gui/efl/common/theme.h"        /* theme_get_iconsize() ### FIXME */
#ifdef HAVE_EVAS2
#  include "gui/efl/common/evas_macros.h"  /* evas_object_from_imlib_image() */
#endif






/*****************************************************************************/

/**
 * throw out old fonts
 */
static void imlib_font_gc(void) {
  int cache=imlib_get_font_cache_size();
  imlib_set_font_cache_size(0);
  imlib_set_font_cache_size(cache); }



/**
 * thumbnail a truetype font (using Imlib2)
 *
 * called from thumbnail_generate() only.
 * @return an error-code
 * @param e    - the thumbnail struct
 */
/* add as theme opts? */
#    define TB 2
#    define TP 2
#    define TTF_TL1 "Aa"
#    define TTF_TL2 "Cc"



/**
 * thumbnail a true-type font (using Imlib2).
 *
 * adapted from efm.
 * @param e - the thumbnail descriptor
 */
static Imlib_Image make(Evidence_Thumb *e) {
  Imlib_Image src=NULL,tmp=NULL;
  int         icon_height;
  char       *fontfile=NULL;

  if(e&&e->origfile) {
    Imlib_Font fn=NULL;

    if(!e->theme)
      return NULL;
    if(theme_get_iconsize(e->theme,NULL,&icon_height)!=NOIR_SUCC)
      icon_height=EVIDENCE_THUMB_BASE;

    if(icon_height)
      esprintf(&fontfile,"%s/%d",e->origfile,icon_height);
    else
      esprintf(&fontfile,"%s/16",e->origfile);

    imlib_font_gc();

    if((fn=imlib_load_font(fontfile))) {
      int w,h,ww,hh;

      imlib_context_set_font(fn);
      imlib_get_text_size(TTF_TL1,&w,&h);
      imlib_get_text_size(TTF_TL2,&ww,&hh);

      if(hh>h)
        h=hh;
      if(ww>w)
        w=ww;

      h+=TB+TP;
      hh=h*2;
      w+=(2*(TB+TP));

      if((tmp=imlib_create_image(w,hh))) {
#    ifdef FONTTHUMB_TRANSLUCENT
#      warning font thumbnails will be translucent. this looks nice, but is semantically questionable.
        Imlib_Color_Modifier cm;
        DATA8 atab[256];
#    endif

        imlib_context_set_image(tmp);
/*      imlib_context_set_color(255,255,255,255); */
        imlib_context_set_color(239,239,239, 255);
        imlib_context_set_cliprect(0,0,w,hh);
        imlib_image_set_has_alpha(1);

        imlib_image_fill_rectangle(0,0,w,hh);

        /* translucency for font thumb */
#    ifdef FONTTHUMB_TRANSLUCENT
        cm=imlib_create_color_modifier();
        imlib_context_set_color_modifier(cm);
        memset(atab,160,sizeof(atab));

        imlib_set_color_modifier_tables(NULL,NULL,NULL,atab);
        imlib_apply_color_modifier_to_rectangle(TB,TB,w-TB-TB,
                                                hh-TB-TB);
        imlib_free_color_modifier();
#    endif

        imlib_context_set_color(0,0,0,255);

        /* left */
        imlib_image_fill_rectangle(0,0,TB,hh);

        /* right */
        imlib_image_fill_rectangle(w - TB,0,TB,hh);

        /* upper */
        imlib_image_fill_rectangle(TB,0,w - TB - TB,TB);

        /* lower */
        imlib_image_fill_rectangle(TB,hh - TB,w - TB - TB,hh);

        imlib_context_set_blend(1);
        imlib_text_draw(TB + TP,TB + TP,TTF_TL1);
        imlib_text_draw(TB + TP,h,TTF_TL2);

        src=evidence_thumb_imlib_standardize(e);
      }                      /* imlib_create_image() failed */
      imlib_free_font();
    }
  }
  if(fontfile)
    free(fontfile);
  return src; }






/*****************************************************************************/
/**
 * preview a true-type font (using Imlib2).
 *
 * @return an evas-object containing the preview
 * @param e     the struct describing the thumbnail
 * @param what  what is to be created (if the thumbnailer has more than one capability)
 */
#ifdef HAVE_EVAS2

static inline Evas_Object *live(Evidence_Thumb *e,thumbnail_plugin_capabilities what) {
  Imlib_Image  tmp=NULL;
  char        *fontfile=NULL;
  Evas_Object *o=NULL;

  const char  *calib_template="The quick brown fox jumps over the lazy dog. THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG. äÄæÆåÅçÇïÏñÑöÖüÜß {}[]«»() ~/\\ ?!.,;: @%&='´`^ +*~# _- | µ¹²³";
  char        *calib=NULL;
  char         cb[2]="_";

  int          tw,th,
               cw,ch,
               mw,mh,mi,
               x,y,
               fs=48;

  int          i,j;

  double       aspect=0,rescale;

  size_t       l;

  Imlib_Font   fn=NULL;

  esprintf(&fontfile,"%s/%d",e->origfile,fs);
  if(!fontfile)
    goto fail;

  if(!(calib=strdup(calib_template)))
    goto fail;

  imlib_font_gc();

  if(!(fn=imlib_load_font(fontfile)))
    goto fail;

  imlib_context_set_font(fn);

  for(l=strlen(calib),mi=mw=mh=0,tw=th=0,j=i=0;i<l;i++) {
    cb[0]=calib[i];
    imlib_get_text_size(cb,&cw,&ch);
    if((cw>0)&&(ch>0)) {
      j++;
      if(cw>mw) { mw=cw; mi=i; }
      if(ch>mh) mh=ch;
      tw+=cw;
      th+=ch;
/*    E(0,"%c: %dx%d  (%dx%d)\n",cb[0],cw,ch,tw/j,th/j); */
    }
    else
      calib[i]='\0'; }
  if(!j) {
    E(0,"couldn't get characters from font \"%s\"!?\n",fontfile);
    goto done; }

  cw=tw/j;
  ch=th/j;

  aspect=ch/((float)cw);
  E(0,"aspect: %lf (%dx%d)\n",aspect,cw,ch);

  rescale=sqrt(e->w*e->h)/sqrt(mw*mh*j);

#if 0
  E(0,"rescale: (%d => %d*%d=%d) factor %3.2lf, new font-size %4.0lf\n",
    (cw*ch*j),
    e->w,e->h,(e->w*e->h),
    rescale,fs*rescale);

  if((mw*rescale)>e->w) {
    E(0,"overflow: %4.0lf > %d\n",mw*rescale,e->w);
    rescale=e->w/((float)mw);
    E(0,"fixed: %4.0lf <= %d (rescale to %4.0lf)\n",mw*rescale,e->w,rescale);
    E(0,"rescale: (%d => %d*%d=%d) factor %3.2lf, new font-size %4.0lf\n",
      (cw*ch*j),
      e->w,e->h,(e->w*e->h),
      rescale,fs*rescale); }
#endif 

  fs=((float)fs)*rescale;
  mh=((float)mh)*rescale;

  imlib_free_font();

  esprintf(&fontfile,"%s/%d",e->origfile,fs);
  if(!fontfile)
    return NULL;
  if(!(fn=imlib_load_font(fontfile)))
    goto done;

  imlib_context_set_font(fn);

  if((tmp=imlib_create_image(e->w,e->h))) {
    imlib_context_set_image(tmp);
    imlib_context_set_color(159,159,159, 255);
    imlib_context_set_cliprect(0,0,e->w,e->h);
    imlib_image_set_has_alpha(1);
    imlib_image_fill_rectangle(0,0,e->w,e->h);
    imlib_context_set_color(0,0,0, 255);
    imlib_context_set_blend(1);

    x=y=0;
    for(i=0;i<l;i++) {
      if((cb[0]=calib[i])!='\0') {
        imlib_get_text_size(cb,&tw,&th);
        if((x+tw)>e->w) {
          x=0;
          y+=mh; }
        imlib_text_draw_with_return_metrics(x,y,cb,&tw,&th,&cw,&ch);
        x+=cw; }}

    o=evas_object_from_imlib_image((Evas *)e->data,NULL); }

done:
  imlib_free_font();
/* imlib_gc(); */
fail:
  if(calib)
    free(calib);
  if(fontfile)
    free(fontfile);

  return e->data=o; }

#endif






/*****************************************************************************/



/**
 * initialize the thumbnailer-plugin and register it for the MIME-types and extensions it knows.
 * @return an error-code <0, or a set of features supported by the plugin
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_init(thumbnail_plugin *tp) {
  thumbnail_plugin_register_mime(tp,"application/x-font-ttf",TP_PRIO_HIGH,TP_ICONS);
  thumbnail_plugin_register_mime(tp,"font/*",                TP_PRIO_LOW, TP_ICONS);

  thumbnail_plugin_register_mime(tp,"application/x-font-ttf",TP_PRIO_HIGH,TP_MOUSEOVER);
  thumbnail_plugin_register_mime(tp,"font/*",                TP_PRIO_LOW, TP_MOUSEOVER);

  return (TP_IMAGE|TP_IMLIB); }



/**
 * deinitialize the thumbnailer-plugin and free any resources it may hold
 * @return an error-code
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_exit(thumbnail_plugin *tp) {
  return NOIR_IGNORED; }



/**
 * create a thumbnail
 * @return an error-code
 * @param tp    an opaque thumbnailer-plugin struct
 * @param e     the struct describing the thumbnail
 * @param data  the imlib-image (if TP_IMLIB)
 */
int thumbnail_plugin_make(thumbnail_plugin *tp,Evidence_Thumb *e,void **data) {
  Imlib_Image im=make(e);
  *((Imlib_Image *)data)=im;
  return im?NOIR_SUCC:NOIR_FAIL; }



/**
 * create a big preview
 * @return an error-code
 * @param tp    an opaque thumbnailer-plugin struct
 * @param e     the struct describing the thumbnail
 * @param what  what is to be created (if the thumbnailer has more than one capability)
 */
int thumbnail_plugin_live(thumbnail_plugin *tp,Evidence_Thumb *e,thumbnail_plugin_capabilities what) {
#ifdef HAVE_EVAS2
  e->data=live(e,what);
#else
  e->data=NULL;
#endif
  return e->data?NOIR_SUCC:NOIR_FAIL; }



/*****************************************************************************/
