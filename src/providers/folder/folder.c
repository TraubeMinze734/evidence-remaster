/** @file  folder.c
 *  @brief meta-data provider for folders
 *
 * reacts on folder containing certain files, like a CVS-folder
 */

#define MY_NAME "folder"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "errors.h"
#include "meta.h"
#include "file_lcl.h"
#include "model.h"
#include "file.h"          /* file_get_meta_with_lock() */
#include "util.h"          /* esprintf() */



static char *combine_labels(file_info *pa,const char *label) {
  noir_meta *m;
  char      *c=NULL;

  if(!(file_info_get_infobits(pa)&FF_META))  /* JIT parent */
    file_get_meta_with_lock(pa,NULL);

  if((m=meta_in_group(file_info_get_meta(pa),"Labels",MY_NAME)))
    esprintf(&c,"%s,%s",(char *)meta_get_val(m),label);
  else
    c=strdup(label);

  return c; }



static int label(file_info *pa,file_info *fi,const char *fn,const char *label,noir_provider *prov) {
  if(!strcmp((char *)fn,label)) {
    char      *labels=combine_labels(pa,"CVS");
    int        ret=NOIR_FAIL;

    E(0,"found \"%s\", trying to label \"%s\" \"%s\"...\n",fn,file_info_get_tag(pa),labels);

    if(meta_set(file_info_get_meta(pa),META_RO,META_STR,"Labels",
                "Labels provide some additional information about an item and are set automatically by evidence. For instance, directories that have revision information may get the \"CVS\" label.",MY_NAME,labels,prov)!=NOIR_SUCC)
      E(0,"fail for \"%s\"!?\n",file_info_get_tag(pa)?file_info_get_tag(pa):"???");
    else
      ret=NOIR_SUCC;

    if(labels)
      free(labels);

    return ret; }
  else
    return NOIR_IGNORED; }




/**
 * get meta-data for this file
 * @return an error-code
 * @param md    the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_get_for_file(noir_md *md,char *file,noir_provider *prov) {
  int         ret=NOIR_IGNORED;
  file_info  *fi=file_info_fetch(NULL,file),*pa=NULL;
  const char *fn;

  if(fi)
    pa=file_info_get_parent(fi);
  else {
    char *dn=file_dirname(file);
    E(1,"no file-info for \"%s\"!?\n",file);
    if(!dn)
      return NOIR_NOTFOUND;
    pa=file_info_fetch(NULL,dn);
    free(dn); }
  if(!pa)
    return NOIR_NOTFOUND;
  if(!(fn=file_info_get_name(fi)))
    fn=file_name(file);
  if(!fn)
    return NOIR_FAIL;

  label(pa,fi,fn,"CVS",prov);

  return ret; }



/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
int layoutp(void) {
  return NOIR_FAIL; }
