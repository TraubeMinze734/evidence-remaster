/** @file  vorbis.c
 *  @brief meta-data provider plug-in "vorbis"
 *
 *  get/set Ogg Vorbis tags
 */

#include "config.h"

#include "meta.h"
#include "errors.h"
#include "util.h"

#include "vcedit.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include <unistd.h>

#include <stdio.h>

#define MY_NAME "Vorbis"



/**
 * make sort-key from artist and title
 * @return an error-code
 * @param ret   the meta-data list
 * @param prov  the provider (us)
 */
static int meta_make_sortkey(noir_md *ret,noir_provider *prov) {
  noir_meta *x;
  char      *t=NULL,*a=NULL,*s=NULL;

  if((x=meta_in_group(ret,"title",MY_NAME))&&meta_get_val(x))
    t=(char *)meta_get_val(x);
  if((x=meta_in_group(ret,"artist",MY_NAME))&&meta_get_val(x))
    a=(char *)meta_get_val(x);

  if(t||a) {
    if(!a)
      a="diverse";

    if(t)
      esprintf(&s,"%s - %s",a,t);
    else
      s=strdup(a);

    if(s) {
      meta_add(ret,META_RO,META_STR,"sortkey","If you enable 'Sort by meta-data', files will be sorted according to this field.",MY_NAME,s,prov);
      free(s);
      return NOIR_SUCC; }
    else
      return NOIR_OOM; }
  return NOIR_NODATA; }



/**
 * write meta-data for this file
 * extractor framework cannot write meta-data, only read it
 * @return an error-code
 * @param met       the list of (all) meta-data
 * @param filename  the file to set the meta-data on
 * @param prov      the provider-entry for this plugin
 */
int meta_set_for_file(noir_md *met,char *filename,noir_provider *prov) {
  vcedit_state   *state=vcedit_new_state();
  vorbis_comment *vc;
  int             ret=NOIR_SUCC;
  noir_meta      *r;
  FILE           *src;
  FILE           *dst;
  char           *nn;

  if(!met||!filename)
    return NOIR_FAIL;
  if(!(r=meta_get_1st(met)))
    return NOIR_NODATA;

  if(!(nn=malloc(strlen(filename)+8)))
    return NOIR_OOM;

  if(!(src=fopen(filename,"rb"))) {
    ret=NOIR_META_FILE;
    goto free_nn; }

  strcat(strcpy(nn,filename),".evitmp");
  if(!(dst=fopen(nn,"wb"))) {
    ret=NOIR_META_FILE;
    goto close_src; }

  if(vcedit_open(state,src)<0) {
    fprintf(stderr,"Failed to open file \"%s\" as vorbis: %s\n",filename,vcedit_error(state));
    ret=NOIR_META_FILE;
    goto close_dst; }

  /* grab and clear the exisiting comments */

  vc=vcedit_comments(state);
  vorbis_comment_clear(vc);
  vorbis_comment_init(vc);

  /* add comments */
  while(r) {
    if(((meta_get_flags(r)==META_DO)||(meta_get_flags(r)==META_RW))&&
       (meta_get_group(r)&&!strcmp(MY_NAME,meta_get_group(r)))&&
       strcasecmp(meta_get_key(r),"encoder")&&meta_get_val(r)&&
       strlen((char *)meta_get_val(r))) {
      vorbis_comment_add_tag(vc,(char *)meta_get_key(r),meta_get_val(r)); }
    r=meta_get_next(r); }

  /* write out the modified stream */
  if(vcedit_write(state,dst)<0)
    fprintf(stderr,"Failed to write comments to output file \"%s\": %s\n",filename,vcedit_error(state));
  else
    rename(nn,filename);

  /* done */
  vcedit_clear(state);

  close_dst:
  fclose(dst);
  unlink(nn);
  close_src:
  fclose(src);
  free_nn:
  free(nn);

  return ret; }



/**
 * get meta-data for this file
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_get_for_file(noir_md *ret,char *file,noir_provider *prov) {
  OggVorbis_File  vf;
  FILE           *fh;

  if(!ret)
    return NOIR_FAIL;

  if(!(fh=fopen(file,"r")))
    return NOIR_META_FILE;

  if(ov_open(fh,&vf,NULL,0)<0) {
    fprintf(stderr,"Input does not appear to be an Ogg bitstream.\n");
    return NOIR_META_FORMAT; }
  else {
    char        **ptr=ov_comment(&vf,-1)->user_comments;
    char         *p,*s=NULL;
    vorbis_info  *vi=ov_info(&vf,-1);
    long          ml=ov_pcm_total(&vf,-1);
    int           hh,mm,ss;

    while(*ptr){
      if((p=strchr(*ptr,'='))) {
        *(p++)='\0';
        meta_add(ret,META_RW,META_STR,*ptr,NULL,MY_NAME,p,prov); }
      ++ptr; }

    esprintf(&s,"%d",vi->channels);
    meta_add(ret,META_RO,META_INT,"channels",NULL,MY_NAME,s,prov);

    esprintf(&s,"%ld",vi->rate);
    meta_add(ret,META_RO,META_INT,"rate",NULL,MY_NAME,s,prov);

    ml=(long)(ml/vi->rate);
    ss=(ml%60);
    ml=(long)(ml/60);
    mm=(ml%60);
    ml=(long)(ml/60);
    hh=ml;

    if(hh)
      esprintf(&s,"%d:%2d:%2d",hh,mm,ss);
    else
      esprintf(&s,"%d:%2d",mm,ss);
    meta_add(ret,META_RO,META_STR,"length",NULL,MY_NAME,s,prov);

    meta_add(ret,META_RW,META_STR,"encoder",NULL,MY_NAME,ov_comment(&vf,-1)->vendor,prov);

    meta_init_str(ret,"title","The song's title");
    meta_init_str(ret,"artist","The artist/band who created or performed this song");
    meta_init_str(ret,"album","The album this song appears on");
    meta_init_str(ret,"tracknumber","The track-number of this song (1..)");
    meta_init_str(ret,"date","The date this song was created");
    meta_dflt(ret,META_RW,META_STR,"genre","The genre of this song",MY_NAME,"Other",prov);

    meta_make_sortkey(ret,prov);

    if(s)
      free(s); }

  ov_clear(&vf); /* takes care of fclose() */
  return NOIR_SUCC; }



/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
int layoutp(void) {
  return NOIR_FAIL; }
