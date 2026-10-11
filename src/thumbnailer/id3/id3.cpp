/** @file  id3.cpp
 *  @brief thumbnail plug-in "MP3/ID3"
 *
 *  use id3lib to get thumbnails store in MP3 ID3 tags
 */

extern "C" {

#include "config.h"
#include "debug.h"

#include "util.h"
#include "meta.h"
#include "errors.h"
#include "mime.h"
#include "file.h"
#include "file_lcl.h"

#include "../thumbnail_private.h"
#include "../thumbnail_plugin.h"

#include <string.h>
#include <sys/stat.h>

}

#include <id3/tag.h>
#include <id3/misc_support.h>

#include <stdio.h>
#include <ctype.h>

#ifndef TRUE
#  define FALSE (0)
#  define TRUE (!(FALSE))
#endif



/*****************************************************************************/



/**
 * @return a string representation of the picture-type (cover, artist, ...)
 * @param pictype  index of the picture-type (cover, artist, ...)
 */
static const char *pic_type(int pictype) {
  const char *pt[]={
          "Other", "icon (32x32 PNG)", "file icon",
          "Cover (front)", "Cover (back)",
          "Leaflet page",
          "Media",
          "Lead artist/lead performer/soloist",
          "Artist/performer","Conductor","Band/Orchestra",
          "Composer","Lyricist/text writer",
          "Recording Location",
          "During recording","During performance",
          "Movie/video screen capture",
          "A bright coloured fish (WTF??? : )",
          "Illustration",
          "Band/artist logotype","Publisher/Studio logotype",
          NULL };
  int   types=sizeof(pt)/sizeof(char *);
  return (pictype<types)?pt[pictype]:"???"; }



/**
 * load an image (using Imlib2)
 * @return an error-code
 * @param e        the thumbnail struct
 * @param tmpfile  the temporary image
 */
static Imlib_Image evidence_thumb_imlib_image_preview(Evidence_Thumb *e,char *tmpfile) {
  Imlib_Image      tmp=NULL;
  Imlib_Load_Error err;
  static const char *em[]={ "","file not found","dir","may not read file",
                      "no loader for format","path too long",
                      "non-existant element in path","non-dir in path",
                      "funky path","nlinks","out of memory","out of FDs",
                      "read-only","out of space","unknown error" };

  if((tmp=imlib_load_image_with_error_return(tmpfile,&err))) {
    imlib_context_set_image(tmp);
    return evidence_thumb_imlib_standardize(e); }
  E((err==IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT)?1:0,
    "Imlib2 wouldn't load \"%s\" (%d -- \"%s\").\n",tmpfile,(int)err,
    (err<=IMLIB_LOAD_ERROR_UNKNOWN)?em[err]:"???");
  return NULL; }



/**
 * read an image from an MP3/ID3-tag into an Imlib image, and resize it
 * @return an error-code
 * @param e   the thumbnail struct
 * @param im  where to store a pointer to the resulting image (or NULL)
 */
static inline int extract_pic_from_id3(Evidence_Thumb *e,Imlib_Image *im) {
  ID3_Tag          tag(e->origfile);
  const ID3_Frame *frame=tag.Find(ID3FID_PICTURE);
  const ID3_Field *field=NULL;

  int              s=EVIDENCE_THUMB_BASE*e->size,
                   sw,sh,
                   dw,dh;
  char            *p,*tmp=NULL,*mime=NULL,*q=NULL;
  int              ret=NOIR_FAIL,pictype=0;

  if(!im)
     return NOIR_FAIL;
  *im=NULL;

  if(!frame)
    return NOIR_NODATA;

  if(!(p=evidence_thumbnail_path(e->size,e->hashedname,EXT_JPG)))
    return NOIR_OOM;

  if(e->thumbfile)
    free(e->thumbfile);
  e->thumbfile=p;

  if((field=frame->GetField(ID3FN_PICTURETYPE))) {
    if(field->GetType()==ID3FTY_INTEGER) {
      pictype=field->Get();
      E(2,"Image-Type 0x%x - %s\n",pictype,pic_type(pictype)); }}

  if(frame->Contains(ID3FN_MIMETYPE)) {
    char *t=ID3_GetString(frame,ID3FN_MIMETYPE);

    /*************************************************************************/

    if(!strcmp(t,"-->")) {                    /* contains URL, not image *****/
      char *u=ID3_GetString(frame,ID3FN_URL),*a=NULL;

      if(u) {
        if(!strstr(u,PROTOCOL_DELIMITER))     /* relative URL */
          a=file_compose_path(e->uri,u,NULL); /* make absolute */
        else
          a=strdup(u);
        delete [] u; }

      delete [] t;

      if(a) {                                 /* have absolute URL */
        if(file_if_local(a)) {                /* local URL */
          tmp=a;
          ret=NOIR_SUCC; }
        else {                                /* remote URL, download */
          if(!(tmp=evidence_thumb_get_tmpname(p,NULL))) {
            ret=NOIR_OOM;
            goto fail; }

          ret=file_cp(a,tmp);
          free(a);

          if(ret!=NOIR_SUCC)
            goto fail; }

        if((mime=(char *)file_get_mime(tmp))&& /* try to determine MIME-type */
           (mime=strchr(mime,MIME_DELIMITER))) {
          char *tn=NULL;
          esprintf(&tn,"%s.%s",tmp,mime+1);
          if(tn) {                            /* rename file based on MIME */
            if(file_mv(tmp,tn)==NOIR_SUCC) {
              free(tmp);
              tmp=tn; }
            else
              free(tn); }
          mime=NULL; }

        goto done; }

      else {
        ret=NOIR_OOM;
        goto fail; }}



    /*************************************************************************/



    else if(strchr(t,MIME_DELIMITER))         /* contains image, cqt MIME */
      mime=strdup(t);



    else
      esprintf(&mime,"image/%s",t);           /* contains image, broken MIME */



    delete [] t;

    if(mime)
      E(3,"MIME::%s::\n",mime); }

  if(!mime) {
    E(0,"no MIME info for image\n");
    ret=NOIR_TYPEMISMATCH;
    goto fail; }

  if(!(q=strrchr(mime,MIME_DELIMITER))) {
     E(0,"wonky MIME-type!?\n");
     ret=NOIR_FAIL;
     goto fail; }
  q++;

  if(!(tmp=evidence_thumb_get_tmpname(p,q))) {
    ret=NOIR_OOM;
    goto fail; }

  if(frame->Contains(ID3FN_DATA))
    frame->Field(ID3FN_DATA).ToFile(tmp);
  else {
    ret=NOIR_NODATA;
    goto fail; }

 done:
  *im=evidence_thumb_imlib_image_preview(e,tmp);
  ret=*im?NOIR_SUCC:NOIR_FAIL;

 fail:
  if(mime)
     free(mime);
  if(tmp) {
    unlink(tmp);
    free(tmp); }

  return ret; }



extern "C" {

/*****************************************************************************/



/**
 * initialize the thumbnailer-plugin and register it for the MIME-types and extensions it knows.
 * @return an error-code <0, or a set of features supported by the plugin
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_init(thumbnail_plugin *tp) {
  thumbnail_plugin_register_mime(tp,"audio/mpeg",TP_PRIO_HIGH,TP_ICONS);
  thumbnail_plugin_register_mime(tp,"audio/mpeg",TP_PRIO_HIGH,TP_MOUSEOVER);
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
   return extract_pic_from_id3(e,data); }



/*****************************************************************************/

}
