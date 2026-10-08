/** @file  image.c
 *  @brief meta-data provider plug-in "image" (imlib2)
 *
 *  get and set image dimensions and format
 */

#define MY_NAME "Imlib2"

#include "config.h"

#include "debug.h"

#include "meta.h"
#include "errors.h"
#include "gui/common/alert.h"
#include "util.h"

#include <string.h>
#include <stdlib.h>

#include <unistd.h>

#include <stdio.h>

#include <sys/stat.h>

#include <string.h>

#ifdef HAVE_IMLIB2
#  define X_DISPLAY_MISSING 1
#  include <Imlib2.h>
#  include "imlib.h"
#endif



/**
 * write meta-data for this file
 * @return an error-code
 * @param ret       the list of (all) meta-data
 * @param filename  the file to set the meta-data on
 * @param prov      the provider-entry for this plugin
 */
int meta_set_for_file(noir_md *ret,char *filename,noir_provider *prov) {
  int               rv=NOIR_FAIL;
#ifdef HAVE_IMLIB2
  int               sw,sh,
                    dw=-1,dh=-1,
                    resized=0,dp;
  float             f=0;
  Imlib_Image       src,dst;
  char             *p=strrchr(filename,'.'),
                   *q=strrchr(filename,'/'),
                   *old_format=NULL,
                   *new_format=NULL;
  noir_meta        *r,*m;
  Imlib_Load_Error  err;

  if(!ret||!filename)
    return NOIR_FAIL;
  if(!(r=meta_get_1st(ret)))
    return NOIR_NODATA;

  IMLIB_ACQUIRE;

  if(!(src=imlib_load_image(filename))) {
    rv=NOIR_META_FILE;
    goto done; }

  imlib_context_set_image(src);

  if(p&&(!q||(p>q)))
    imlib_image_set_format(old_format=(p+1));

  imlib_context_set_anti_alias(1);

  dw=sw=imlib_image_get_width();
  dh=sh=imlib_image_get_height();

  while(r) {
    if((meta_get_flags(r)==META_DO)&&
       (meta_get_group(r)&&!strcmp(MY_NAME,meta_get_group(r)))&&meta_get_val(r)) {
      E(2,"%s(%s)\n",meta_get_key(r),filename);
      if(!strcasecmp(meta_get_key(r),"Alpha-Channels"))
        imlib_image_set_has_alpha(0);

      else if(!strcasecmp(meta_get_key(r),"Width")) {
        if(!resized) {
          resized=1;
          dw=atoi(meta_get_val(r));
          dp=atoi(meta_get_prvs(r));
          f=((float)dw)/dp;
          dh=-1;
          if((m=meta_in_group(ret,"Height",MY_NAME))) {
            dh=atoi(meta_get_val(m));
            if(dh<=0)
              dh=(atof(meta_get_prvs(m))*f); }
          else
            fprintf(stderr,"image: image-height not set in meta-data!?\n"); }}

      else if(!strcasecmp(meta_get_key(r),"Height")) {
        if(!resized) {
          resized=1;
          dh=atoi(meta_get_val(r));
          dp=atoi(meta_get_prvs(r));
          f=((float)dh)/dp;
          dw=-1;
          if((m=meta_in_group(ret,"Width",MY_NAME))) {
            dw=atoi(meta_get_val(m));
            if(dw<=0)
              dw=(atof(meta_get_prvs(m))*f); }
          else
            fprintf(stderr,"image: image-width not set in meta-data!?\n"); }}

      else if(!strcasecmp(meta_get_key(r),"Image-Format"))
        imlib_image_set_format(new_format=(char *)meta_get_val(r)); }

    r=meta_get_next(r); }


  imlib_context_set_cliprect(0,0,dw,dh);

  if(resized) {
    E(0,"(\"%s\"): size %dx%d => %dx%d, factor is %f\n",filename,sw,sh,dw,dh,f);
    if((dw<=0)||(dh<=0)) {
      rv=NOIR_FAIL;
      goto done; }
    dst=imlib_create_cropped_scaled_image(0,0,sw,sh,dw,dh);
    imlib_free_image();
    if(!dst) {
      rv=NOIR_FAIL;
      goto done; }
    imlib_context_set_image(dst); }

  rv=NOIR_SUCC;

  if(new_format) {
    if(!(q=malloc(strlen(filename)+strlen(new_format)+2))) {
      rv=NOIR_OOM;
      goto done; }
    strcpy(q,filename);
    if(old_format) {
      p=q+(old_format-filename-1);
      *p='\0'; }
    strcat(strcat(q,"."),new_format);
    p=q; }
  else {
    p=filename;
    q=NULL; }

  imlib_save_image_with_error_return(p,&err);

  if(err!=IMLIB_LOAD_ERROR_NONE) {
    fprintf(stderr,"image: \"%s\" -- ",p);
    switch(err) {
    case IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_WRITE:
      form_alert(NULL,"Sorry, could not save image, no permission to write file...\n");
      break;
    case IMLIB_LOAD_ERROR_UNKNOWN:
      form_alert(NULL,"Sorry, could not save image for reasons unknown...\n"
                      "In all likelihood, the Imlib2-loader for the\n"
                      "format you requested does not support saving...");
      break;
    case IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT:
      form_alert(NULL,"Sorry, could not save image; there is no\n"
                      "Imlib2-loader for the format you requested...");
      break;
    default:
      form_alert(NULL,"Sorry, could not save image... (#%d)",err); break; }}

  if(q)
    free(q);

  imlib_free_image();

done:
  IMLIB_RELEASE;

#endif
  return rv; }



/**
 * get meta-data for this file
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_get_for_file(noir_md *ret,char *file,noir_provider *prov) {
  int            rv=NOIR_FAIL;
#ifdef HAVE_IMLIB2
  int            sw,sh;
  Imlib_Image    src;
  char          *p=strrchr(file,'.'),
                *q=strrchr(file,'/');
  struct stat    filestatus;
  noir_meta     *m=NULL;

  if(!ret)
    return NOIR_FAIL;

  if(stat(file,&filestatus))
    return NOIR_NOFILE;
  if(!filestatus.st_size)
    return NOIR_NODATA;

  if((m=meta_in_group(ret,"MIME",NULL))&&meta_get_val(m)) {
    size_t      l=strlen(file);
    const char *mime=(const char *)meta_get_val(m);
#if 0
    if(!strcmp("application/x-bzip2",mime)&&(l>8)&&!strcmp(&file[l-8],".tar.bz2"))
      return NOIR_IGNORED;
    if(!strcmp("application/x-gzip",mime)&&
       ((l>4)&&!strcmp(&file[l-4],".tgz"))||
       ((l>7)&&!strcmp(&file[l-7],".tar.gz")))
      return NOIR_IGNORED;
#else
    if((!strcmp("application/x-gzip",mime)&&(l>7)&&strcmp(&file[l-7],".xcf.gz"))||
       (!strcmp("application/x-bzip2",mime)&&(l>8)&&strcmp(&file[l-8],".xcf.bz2")))
      return NOIR_IGNORED;
#endif
  }

  IMLIB_ACQUIRE;

  if(!(src=imlib_load_image(file))) {
    rv=NOIR_META_FILE;
    goto done; }

  imlib_context_set_image(src);

  if(p&&(!q||(p>q)))
    imlib_image_set_format(p+1);

  sw=imlib_image_get_width();
  sh=imlib_image_get_height();

  { char *s=NULL;

    esprintf(&s,"%d",sw);
    meta_add(ret,META_RW,META_INT,"Width",NULL,MY_NAME,s,prov);

    esprintf(&s,"%d",sh);
    meta_add(ret,META_RW,META_INT,"Height",NULL,MY_NAME,s,prov);

    esprintf(&s,"%d",imlib_image_has_alpha());
    meta_add(ret,imlib_image_has_alpha()?META_RW:META_RO,META_INT,"Alpha-Channels",NULL,MY_NAME,s,prov);

    meta_add(ret,META_RW,META_STR,"Image-Format",NULL,MY_NAME,imlib_image_format(),prov);

    if(s)
      free(s);

    rv=NOIR_SUCC; }

  imlib_free_image();

 done:
  IMLIB_RELEASE;

#else
  fprintf(stderr,"image meta-data provider compiled without Imlib2, empty shell cannot determine anything, will signal failure...\n");
#endif
  return rv; }



/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
int layoutp(void) {
  return NOIR_FAIL; }
