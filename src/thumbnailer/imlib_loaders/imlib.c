/** @file  imlib.c
 *  @brief imlib2-loaders thumbnailer
 */

#include "config.h"

#include <stdio.h>
#include <sys/stat.h>

#include "debug.h"
#include "errors.h"
#include "util.h"
#include "file.h"            /* file_exists() */

#include "../thumbnail.h"    /* thumbnail_imlib_gc() */
#include "../thumbnail_private.h"
#include "../thumbnail_plugin.h"

#include <stdlib.h>      /* malloc(), free() */



/*****************************************************************************/

/**
 * thumbnail an image (using Imlib2)
 *
 * @return an error-code
 * @param e    - the thumbnail struct
 */
static Imlib_Image evidence_thumb_imlib_image_preview(Evidence_Thumb *e) {
  Imlib_Image      tmp=NULL;
  Imlib_Load_Error err;
  int              try=0;
  struct stat      st;
  off_t            oldsiz=0;
  file_info       *fi;
  static char *em[]={ "","file not found","dir","may not read file",
                      "no loader for format","path too long",
                      "non-existant element in path","non-dir in path",
                      "funky path","nlinks","out of memory","out of FDs",
                      "read-only","out of space","unknown error" };

  E(3,"loading \"%s\"...\n",e->origfile);

  if((fi=file_info_fetch(NULL,e->origfile))) {
    if((file_info_get_lmode(fi)>0)&&S_ISLNK(file_info_get_lmode(fi))) {
      if(!stat(e->origfile,&st))
        oldsiz=st.st_size; }
    else if(file_info_get_size(fi)>=0)
      oldsiz=file_info_get_size(fi); }

 try_load:
  try++;
  thumbnail_imlib_gc();  /* in case this is a re-try... */

  if((tmp=imlib_load_image_immediately_without_cache(e->origfile))) {
/* if((tmp=imlib_load_image_with_error_return(e->origfile,&err))) { */
    imlib_context_set_image(tmp);

    /* check if file has changed while we thumbnailed it.
       this happens if ev notices a new file before the creator is done
       writing it -- there are some pigs that take forever to write a file
       and don't place advisory locks on the file while doing it. */
    if(!stat(e->origfile,&st)&&(st.st_size!=oldsiz)) {
      E(1,"file \"%s\" changed size (%lld->%lld) while thumbnailing!?\n",
        e->origfile,(unsigned long long)oldsiz,(unsigned long long)st.st_size);
      imlib_free_image_and_decache();
      if(try>=8) {   /* patience running out */
        E(0,"I looked at \"%s\" %d times, and it had a different size each time. Whoever is writing this file sure takes their time. I give up. (%lld,%lld)\n",
          e->origfile,
          try,
          (unsigned long long)oldsiz,
          (unsigned long long)st.st_size);
        return NULL; }
      oldsiz=st.st_size;
      thread_sleep(1);
      goto try_load; }

    if(!(tmp=evidence_thumb_imlib_standardize(e))) {
      if(try>=8)
        return NULL;
      E(0,"evidence_thumb_imlib_standardize() failed!? trying again!\n");
      goto try_load; }

    return tmp; }

  /* diagnostics */
  if((tmp=imlib_load_image_with_error_return(e->origfile,&err))) {
    char *v=NULL;
    imlib_context_set_image(tmp);

    esprintf(&v,"%d",imlib_image_get_width());
    thumbnail_attribute_add(e,"Thumb::Image::Width",v);
    v=NULL;

    esprintf(&v,"%d",imlib_image_get_height());
    thumbnail_attribute_add(e,"Thumb::Image::Height",v);
    v=NULL;

    imlib_free_image_and_decache(); }

  /* fix misleading error occurring when a loader in stacked loaders
     (.xcf.gz etc.) is missing */
  if((err==IMLIB_LOAD_ERROR_PATH_COMPONENT_NON_EXISTANT)&&
     file_exists(e->origfile))
    err=IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT;

  E((err==IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT)?1:0,
    "Imlib2 wouldn't load \"%s\" (%d -- \"%s\").\n",e->origfile,(int)err,
    (err<=IMLIB_LOAD_ERROR_UNKNOWN)?em[err]:"???");
  return NULL; }






/*****************************************************************************/



/**
 * initialize the thumbnailer-plugin and register it for the MIME-types and extensions it knows.
 * @return an error-code <0, or a set of features supported by the plugin
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_init(thumbnail_plugin *tp) {
  thumbnail_plugin_register_mime(tp,"image/*",TP_PRIO_LOW,TP_ICONS);
  thumbnail_plugin_register_mime(tp,"image/*",TP_PRIO_LOW,TP_MOUSEOVER);
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
  Imlib_Image im=evidence_thumb_imlib_image_preview(e);
  *((Imlib_Image *)data)=im;
  return im?NOIR_SUCC:NOIR_FAIL; }



/*****************************************************************************/
