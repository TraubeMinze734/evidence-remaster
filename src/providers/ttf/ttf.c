/** @file  providers/ttf/ttf.c
 *  @brief meta-data provider plug-in "ttf" (TrueType Fonts, using freetype)
 *
 *  get TrueType Font info
 */

#include "config.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <freetype/ttnameid.h>
#include <freetype/ftsnames.h>

#include "meta.h"
#include "errors.h"
#include "util.h"

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>

#include <stdio.h>

#define MY_NAME "TrueType"

#define CHUNK 1024



/**
 * write meta-data for this file
 * @return an error-code
 * @param met       the list of (all) meta-data
 * @param filename  the file to set the meta-data on
 * @param prov      the provider-entry for this plugin
 */
int meta_set_for_file(noir_md *met,char *filename,noir_provider *prov) {
  return NOIR_FAIL; }



/**
 * get meta-data for this file
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_get_for_file(noir_md *ret,char *file,noir_provider *prov) {
  int             err,cnt,i,have_family=0,have_style=0;
  unsigned int    l=0;
  FT_Library      ft_lib;
  FT_Face         face;
  FT_SfntName     name;
  char           *val=NULL,*lcl;
  gsize           foo;

  char *key[]= { "Copyright", "Family", "Subfamily", "ID", "Name", "Version",
                 "PSName", "Trademark", "Manufacturer", "Designer",
                 "Description", "Vendor URL", "Designer URL", "License",
                 "License URL", NULL,
                 "Preferred family", "Preferred Subfamily", "Mac full name",
                 "Sampe text" };
  int keys=sizeof(key)/sizeof(char *);

  if(!ret)
    return NOIR_FAIL;

  if((err=FT_Init_FreeType(&ft_lib)))
    return NOIR_FAIL;

  if((err=FT_New_Face(ft_lib,file,0,&face)))
    return NOIR_FAIL;

  cnt=FT_Get_Sfnt_Name_Count(face);
  for(i=0;i<cnt;i++) {
    if(!FT_Get_Sfnt_Name(face,i,&name)&&(name.string_len>0)) {
      if(name.string_len>l) {
        if(val)
          free(val);
        l=(((name.string_len+1)/CHUNK)+2)*CHUNK;
        if(!(val=malloc(l)))
          goto done;
        memset(val,0,l); }

      strncpy(val,name.string,name.string_len);
      val[name.string_len]='\0';

      if(name.name_id>=keys) {
        fprintf(stderr,"ttf: meta_get_for_file: unknown TrueType nameID %d ignored...\n",name.name_id);
        continue; }

      lcl=NULL;
      switch(name.platform_id) {
        case TT_PLATFORM_APPLE_UNICODE:
          if((name.encoding_id==TT_APPLE_ID_DEFAULT)||      /* Unicode 1.0 */
             (name.encoding_id==TT_APPLE_ID_UNICODE_1_1)||  /* specify Hangul at U+34xx */
             (name.encoding_id==TT_APPLE_ID_ISO_10646))
            lcl=g_locale_from_utf8(val,-1,&foo,&foo,NULL);
          break;

        case TT_PLATFORM_ISO:
        case TT_PLATFORM_MACINTOSH:
          lcl=strdup(val);
          break;

        case TT_PLATFORM_MICROSOFT:
          if((name.encoding_id==TT_MS_ID_UNICODE_CS))
            lcl=g_locale_from_utf8(val,-1,&foo,&foo,NULL);
          break; }

      if(!lcl||!strlen(lcl))
        continue;

      if(name.name_id==TT_NAME_ID_FONT_FAMILY)
        have_family=1;
      else if(name.name_id==TT_NAME_ID_FONT_SUBFAMILY)
        have_style=1;

      meta_add(ret,META_RO,META_STR,key[name.name_id],NULL,MY_NAME,lcl,prov);

      free(lcl); }}

 done:
  if(val)
    free(val);

  if(!have_family)
    meta_add(ret,META_RO,META_STR,"Family","The group of fonts this style belongs to",MY_NAME,face->family_name,prov);
  if(!have_style)
    meta_add(ret,META_RO,META_STR,"Subfamily","The style (bold, italics, ...) of the font",MY_NAME,face->style_name,prov);

  FT_Done_Face(face);

  { noir_meta *mff=NULL,*mfsf=NULL;
    if((mff=meta_in_group(ret,"Family",MY_NAME))&&!meta_get_val(mff))
      mff=NULL;
    if(mff) {
      if((mfsf=meta_in_group(ret,"Subfamily",MY_NAME))&&!meta_get_val(mfsf))
        mfsf=NULL;
      if(mfsf) {
        char *sk=NULL;
        esprintf(&sk,"%s %s",meta_get_val(mff),meta_get_val(mfsf));
        if(sk) {
          meta_add(ret,META_RO,META_STR,"sortkey","If you enable 'Sort by meta-data', files will be sorted according to this field.",MY_NAME,sk,prov);
          free(sk); }}
      else
        meta_add(ret,META_RO,META_STR,"sortkey","If you enable 'Sort by meta-data', files will be sorted according to this field.",MY_NAME,(char *)meta_get_val(mff),prov); }}

  return NOIR_SUCC; }



/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
int layoutp(void) {
  return NOIR_FAIL; }
