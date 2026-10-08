/** @file  epeg.c
 *  @brief epeg JPEG thumbnailer
 */

#include "config.h"

#include <stdio.h>                    /* stderr */
#include <stdlib.h>                   /* free() */
#include <sys/stat.h>                 /* S_IWUSR, S_IRUSR */
#include <unistd.h>                   /* unlink() */

#include "debug.h"
#include "errors.h"
#include "file.h"                     /* PATH_MAX */

#include "../thumbnail_private.h"
#include "../thumbnail_plugin.h"

#include "Epeg.h"



/*****************************************************************************/



/**
 * thumbnail a JPEG (using ePEG).
 *
 * called from thumbnail_generate() only.
 * @return an error-code
 * @param e    - the thumbnail struct
 */

static inline int evidence_thumb_epeg_image_preview(Evidence_Thumb *e) {
  Epeg_Image *im;
  int         s=EVIDENCE_THUMB_BASE*e->size,
              sw,sh,
              dw,dh;
  char       *p,*tmp=NULL;
  int         ret=NOIR_FAIL;

  if(!(p=evidence_thumbnail_path(e->size,e->hashedname,EXT_JPG)))
    return NOIR_OOM;

  if(e->thumbfile)
    free(e->thumbfile);
  e->thumbfile=p;

  if(!(tmp=evidence_thumb_get_tmpname(p,EXT_JPG))) {
    ret=NOIR_OOM;
    goto fail; }

  if(!(im=epeg_file_open(e->origfile))) {
    E(2,"failed to open \"%s\"...\n",e->thumbfile);
    ret=NOIR_NOFILE;
    goto fail; }

  epeg_size_get(im,&sw,&sh);

  if(sw>sh) {
    dw=s;
    dh=(s*sh)/sw; }
  else {
    dh=s;
    dw=(s*sw)/sh; }

  epeg_decode_size_set(          im,dw,dh);

  epeg_quality_set(              im,75);
  epeg_thumbnail_comments_enable(im,1);
  epeg_comment_set(              im,"thumbnail created by evidence");
  epeg_file_output_set(          im,tmp);

  epeg_encode(                   im);

  epeg_close(                    im);

  if(!rename(tmp,e->thumbfile)) {
    if(chmod(e->thumbfile,S_IWUSR|S_IRUSR))
      E(0,"could not set permissions on \"%s\"!?\n",e->thumbfile);
    else {
      if(tmp)
        free(tmp);
      return NOIR_SUCC; }}

 fail:
  if(tmp) {
    unlink(tmp);
    free(tmp); }
  return ret; }






/*****************************************************************************/



/**
 * initialize the thumbnailer-plugin and register it for the MIME-types and extensions it knows.
 * @return an error-code <0, or a set of features supported by the plugin
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_init(thumbnail_plugin *tp) {
  thumbnail_plugin_register_mime(tp,"image/jpeg",TP_PRIO_TOP,TP_ICONS);
  thumbnail_plugin_register_mime(tp,"image/jpg", TP_PRIO_TOP,TP_ICONS);  /* defensive */
  return (TP_IMAGE|TP_FILE); }



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
  int ret=NOIR_FAIL;
  if(evidence_thumb_epeg_image_preview(e)==NOIR_SUCC) {
    E(3,"created ePEG thumbnail, now try to read it.\n");
    ret=NOIR_SUCC; }
  else
    E(1,"ePEG failed for \"%s\"...\n",e->origfile);
  return ret; }



/*****************************************************************************/
