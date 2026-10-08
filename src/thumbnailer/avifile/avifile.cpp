/** @file  avifile.cpp
 *  @brief threaded thumbnailer.
 */



extern "C" {
#include "config.h"
#include "userconfig.h"

#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "errors.h"
#include "../thumbnail_private.h"
#include "../thumbnail_plugin.h"
#include "util.h"
}

#include <avifile.h>
#include <aviplay.h>
/* #  include <avm_fourcc.h> #  include <avm_except.h> */

#define INDEX_STEP 8000
#define THRESH      127


/*****************************************************************************/



/**
 * thumbnail an AVI (using libavifile).
 *
 * called from thumbnail_generate() only.
 * @return an error-code
 * @param e    - the thumbnail struct
 */
static Imlib_Image make(Evidence_Thumb *e) {
  Imlib_Image     img=NULL;
  int             ret=NOIR_IGNORED,w,h,x,y;
  uint8_t        *sp,*dp;
  DATA32         *ip;
  int             keyframe=-1,index=INDEX_STEP,index2=1;
  long            count;

  IAviReadFile   *avi=NULL;
  IAviReadStream *video=NULL;
  CImage         *src=NULL,*tgt=NULL;


  E(0,"trying to open \"%s\"...\n",e->origfile);

  if(!(avi=CreateIAviReadFile(e->origfile))) {
     E(0,"could not open \"%s\"...\n",e->origfile);
     ret=NOIR_FAIL;
     goto done; }

  if(!(video=avi->GetStream(0,AviStream::Video))) {
     ret=NOIR_NODATA;
     goto done; }

  if(video->StartStreaming()!=0) {
    E(0,"could not read \"%s\", no suitable codec maybe?\n",e->origfile);
    ret=NOIR_NOTSUPP;
    goto done; }

 acquire:

  /* position a few minutes into the movie.
     if that's behind the end of the movie,
     try to find a suitable position. */
  while(1) {
     if((keyframe=video->GetPrevKeyFrame(index))>=0)
        break;
     if((index-=1000)<0) {
        index2=0;
        break; }}

  if(keyframe<0)
     keyframe=0;

  video->Seek(keyframe);
  video->ReadFrame();

  src=video->GetFrame(true);

  tgt=new CImage(src,24);

  tgt->GetFmt()->Print();

  w=tgt->Width();
  h=tgt->Height();

  if(!(img=imlib_create_image(w,h))) {
    ret=NOIR_OOM;
    goto done; }

  imlib_context_set_image(img);
  if(!(ip=imlib_image_get_data())) {
    E(0,"couldn't get imlib2 data!?\n");
    ret=NOIR_FAIL;
    goto done; }

  sp=tgt->Data();
  dp=(uint8_t *)ip;

  E(2,"%d x %d (%p, %p)\n",w,h,sp,dp);

  count=0;

  for(y=0;y<h;y++) {
    for(x=0;x<w;x++) {
#    ifdef FONTTHUMB_TRANSLUCENT
      dp[3]=239;   /* alpha */
#    else
      dp[3]=255;   /* alpha */
#    endif
      dp[2]=sp[2]; /* red */
      dp[1]=sp[1]; /* green */
      dp[0]=sp[0]; /* blue */

      if((sp[0]>THRESH)||(sp[1]>THRESH)||(sp[2]>THRESH))
         count++;

      sp+=3;
      dp+=4; }}

  imlib_image_put_back_data(ip);
  imlib_image_set_has_alpha(1);

  if((count<((w*h)/16.0))&&((index2>0)&&(index2<10))) {
     imlib_free_image_and_decache();
     src->Release();
     tgt->Release();
     index=(++index2)*INDEX_STEP;
     E(0,"snapshot too dark, skipping and reacquiring at %d...\n",index);
     goto acquire; }

  if((img=evidence_thumb_imlib_standardize(e)))
    ret=NOIR_SUCC;


 done:
  if(src)
    src->Release();
  if(tgt)
    tgt->Release();
  delete avi;

  puts("");

  return img; }






/*****************************************************************************/


extern "C" {

/**
 * initialize the thumbnailer-plugin and register it for the MIME-types and extensions it knows.
 * @return an error-code <0, or a set of features supported by the plugin
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_init(thumbnail_plugin *tp) {
  thumbnail_plugin_register_mime(tp,"video/quicktime",TP_PRIO_LOW,TP_ICONS);
  thumbnail_plugin_register_mime(tp,"video/x-msvideo",TP_PRIO_LOW,TP_ICONS);
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
  Imlib_Image  im;
#if 0
  char        *fail;

  /* pessimistic approach: write failinfo in advance and delete it if we
     succeed, so if a rotten avifile plugin crashes us, we won't try again. */
  thumbnail_failinfo_save(e->origfile,file_name(tp->file),&fail);

  im=make(e);
  if((*((Imlib_Image *)data)=im))
    thumbnail_failinfo_remove(fail);
  else
    free(fail);
#else
  im=make(e);
  *((Imlib_Image *)data)=im;
#endif

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



}
