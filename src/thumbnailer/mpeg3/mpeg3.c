/** @file  mpeg3.c
 *  @brief MPEG3 movie thumbnailer
 */

#include "config.h"
#include "debug.h"
#include "errors.h"

#include "../thumbnail_plugin.h"
#include "../thumbnail_private.h"

#include <stdlib.h>      /* malloc(), free() */
#include <libmpeg3.h>



/*****************************************************************************/

/**
 * thumbnail an MPEG (using libmpeg3).
 *
 * called from thumbnail_generate() only.
 * @return an error-code
 * @param e    - the thumbnail struct
 */
static inline Imlib_Image make(Evidence_Thumb *e) {
  int            ret=NOIR_IGNORED;
  mpeg3_t       *mpg=NULL;
  unsigned char *dst=NULL,**rows=NULL,*sp,*dp;
  DATA32        *ip;
  int            sx=0,sy=0,
                 sw,sh,
                 dw,dh,
                 x,y,i;
  int            stream=0;
  char          *p;
  Imlib_Image    img=NULL;

  if(!(p=evidence_thumbnail_path(e->size,e->hashedname,EXT_PNG))) {
    ret=NOIR_OOM;
    goto done; }

  if(e->thumbfile)
    free(e->thumbfile);
  e->thumbfile=p;

  if(!(mpeg3_check_sig(e->origfile))) {
    ret=NOIR_TYPEMISMATCH;
    goto done; }

  if(!(mpg=mpeg3_open(e->origfile))) {
    ret=NOIR_FAIL;
    goto done; }

  mpeg3_set_mmx(mpg,0);
  mpeg3_set_cpus(mpg,1);

  if(!mpeg3_has_video(mpg)) {
    ret=NOIR_NODATA;
    goto done; }

  dw=sw=mpeg3_video_width(mpg, stream);
  dh=sh=mpeg3_video_height(mpg,stream);
  if(!sw||!sh) {
    ret=NOIR_NODATA;
    goto done; }

  E(3,"detected mpeg3 %dx%d, ratio %2.2f, rate %3.2f in \"%s\"\n",sw,sh,
    mpeg3_aspect_ratio(mpg,stream),
    mpeg3_frame_rate(mpg,stream),
    e->origfile);

  if(!(dst=malloc(sw*sh*3+4))||!(rows=malloc(sh*sizeof(unsigned char *)))) {
    ret=NOIR_OOM;
    goto done; }

  for(y=0;y<sh;y++)
    rows[y]=&dst[y*sw*3];

#ifdef HAVE_MPEG3_SEEK_PERCENTAGE
  mpeg3_seek_percentage(mpg,0.21);
#else
  { int64_t len=mpeg3_get_bytes(mpg);
    mpeg3_seek_byte(mpg,(int64_t)(((float)len)*0.21)); }
#endif

  for(i=0;i<16;i++) {
    if(mpeg3_read_frame(mpg,rows,sx,sy,sw,sh,sw,sh,MPEG3_RGB888,stream)) {
      if(i)
        break;
      E(0,"error reading \"%s\"...\n",e->origfile);
      ret=NOIR_FAIL;
      goto done; }}

  if(!(img=imlib_create_image(dw,dh))) {
    ret=NOIR_OOM;
    goto done; }

  imlib_context_set_image(img);
  if(!(ip=imlib_image_get_data())) {
    E(0,"couldn't get imlib2 data!?\n");
    ret=NOIR_FAIL;
    goto done; }

  sp=(unsigned char *)dst;
  dp=(unsigned char *)ip;

  for(y=0;y<dh;y++) {
    for(x=0;x<dw;x++) {
#    ifdef FONTTHUMB_TRANSLUCENT
      dp[3]=239;   /* alpha */
#    else
      dp[3]=255;   /* alpha */
#    endif
      dp[2]=sp[0]; /* red */
      dp[1]=sp[1]; /* green */
      dp[0]=sp[2]; /* blue */
      sp+=3;
      dp+=4; }}

  imlib_image_put_back_data(ip);
  imlib_image_set_has_alpha(1);

  if((img=evidence_thumb_imlib_standardize(e)))
    ret=NOIR_SUCC;


 done:
  mpeg3_close(mpg);

  if(dst)
    free(dst);
  if(rows)
    free(rows);

  if(ret!=NOIR_SUCC)
    E(0,"failed to thumbnail \"%s\": #%d\n",e->origfile,ret);

  return img; }





/*****************************************************************************/



/**
 * initialize the thumbnailer-plugin and register it for the MIME-types and extensions it knows.
 * @return an error-code <0, or a set of features supported by the plugin
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_init(thumbnail_plugin *tp) {
  thumbnail_plugin_register_mime(tp,"video/mpeg",TP_PRIO_HIGH,TP_ICONS);
  return (TP_IMAGE|TP_IMLIB|TP_VIDEO); }



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
 * create a live thumbnail
 * @return an error-code
 * @param tp    an opaque thumbnailer-plugin struct
 * @param e     the struct describing the thumbnail
 * @param what  what is to be created (if the thumbnailer has more than one capability)
 */
int thumbnail_plugin_live(thumbnail_plugin *tp,Evidence_Thumb *e,thumbnail_plugin_capabilities what) {
  return NOIR_IGNORED; }



/*****************************************************************************/
