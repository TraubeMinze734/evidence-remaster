/** @file  taglib.cpp
 *  @brief meta-data provider plug-in for taglib
 *
 *  get/edit tags for ogg/vorbis, MP3/ID3, FLAC, and possibly others -- fast!
 */

extern "C" {

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "errors.h"
#include "meta.h"
#include "util.h"

}

#include <taglib.h>
#include <tag.h>
#include <fileref.h>



#define MY_NAME    "taglib"



/**
 * make sort-key from artist and title
 * @return an error-code
 * @param ret   the meta-data list
 * @param prov  the provider (us)
 */
static int meta_make_sortkey(noir_md *ret,noir_provider *prov) {
  noir_meta *x;
  char      *t=NULL,*s=NULL;
  const char *a=NULL;

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



static int meta_add_tag_el(noir_md *md,const char *k,const char *d,const char *v,noir_provider *prov) {
  char *x=NULL;
  int   ret=NOIR_FAIL;

  if(!v||!(x=strdup(v))) {
    if((ret=meta_add(md,META_RW,META_STR,k,d,MY_NAME,"",prov))==NOIR_SUCC)
      return NOIR_PARTIAL; }
  else {
    ret=meta_add(md,META_RW,META_STR,k,d,MY_NAME,x,prov);
    free(x); }
  return ret; }



static int cpp_meta_get_for_file(noir_md *md,char *file,noir_provider *prov) {
  TagLib::FileRef  f(file);     // ,TRUE,Average);
  TagLib::Tag     *tag=f.tag();
  char            *y=NULL,
                  *x=NULL,
                  *i=NULL;

  if(tag->year())
    esprintf(&y,"%d",(int)tag->year());

  if(tag->track())
    esprintf(&i,"%d",(int)tag->track());

  meta_add_tag_el(md,"Title",  "The song's title",tag->title().toCString(),prov);
  meta_add_tag_el(md,"Artist", "The artist/band who created or performed this song",tag->artist().toCString(),prov);
  meta_add_tag_el(md,"Album",  "The album this song appears on",tag->album().toCString(),prov);
  meta_add_tag_el(md,"Genre",  "The genre of this song",tag->genre().toCString(),prov);
  meta_add_tag_el(md,"Comment","The comment for this song",tag->comment().toCString(),prov);
  meta_add_tag_el(md,"Track #","The track-number of this song (1..)",i?i:"",prov);
  meta_add_tag_el(md,"Year",   "The year this track was created",y?y:"",prov);

  if(y)
    free(y);
  if(i)
    free(i);

  return NOIR_SUCC; }



static const char *meta_get_str(noir_md *md,const char *key,const char *group) {
  noir_meta *m=meta_in_group(md,key,group);

  if(m&&(meta_get_type(m)==META_STR))
    return (const char *)meta_get_val(m);

  return NULL; }



/**
 * write meta-data for this file
 * extractor framework cannot write meta-data, only read it
 * @return an error-code
 * @param md    the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int cpp_meta_set_for_file(noir_md *md,char *file,noir_provider *prov) {
  TagLib::FileRef  f(file);     // ,TRUE,Average);
  TagLib::Tag     *tag=f.tag();
  const char      *y=NULL,
                  *i=NULL;

  if((y=meta_get_str(md,"Year",MY_NAME)))
    tag->setYear(atoi(y));

  if((i=meta_get_str(md,"Track #",MY_NAME)))
    tag->setTrack(atoi(i));

  tag->setTitle(  meta_get_str(md,"Title",  MY_NAME));
  tag->setArtist( meta_get_str(md,"Artist", MY_NAME));
  tag->setAlbum(  meta_get_str(md,"Album",  MY_NAME));
  tag->setGenre(  meta_get_str(md,"Genre",  MY_NAME));
  tag->setComment(meta_get_str(md,"Comment",MY_NAME));

  f.save();

  return NOIR_SUCC; }






/*****************************************************************************/



extern "C" {

/**
 * get meta-data for this file
 * @return an error-code
 * @param md    the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_get_for_file(noir_md *md,char *file,noir_provider *prov) {
   return cpp_meta_get_for_file(md,file,prov); }



/**
 * write meta-data for this file -- C wrapper
 * extractor framework cannot write meta-data, only read it
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_set_for_file(noir_md *ret,char *file,noir_provider *prov) {
  return cpp_meta_set_for_file(ret,file,prov); }



/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
int layoutp(void) {
  return NOIR_FAIL; }

}
