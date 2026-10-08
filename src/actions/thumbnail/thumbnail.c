/** @file  actions/thumbnail/thumbnail.c
 *  @brief rebuild thumbnail(s)
 */

#include <stdio.h>

#include "config.h"

#include "errors.h"
#include "debug.h"
#include "action.h"
#include "meta.h"
#include "model.h"

#include "thumbnailer/thumbnail.h"
#include "gui/common/alert.h"



/*****************************************************************************/



/**
 * re-generate thumbnail for the given item
 */
static int rethumb(file_info *fi) {
  int ret=NOIR_IGNORED;
  if(!fi||!file_info_get_tag(fi))
    E(0,"fi: %p, tag: \"%s\"\n",fi,fi?file_info_get_tag(fi):"???");
  else if(((ret=thumbnail_disk_gc_one(file_info_get_tag(fi)))==NOIR_SUCC)||
          (ret==NOIR_IGNORED)) {
    ret=thumbnail_enqueue(fi);
    if(ret==NOIR_SUCC)  /* instant kama-sutra. can happen for folder-icons. */
      model_evt(fi,MODEL_UPD,FF_THUMB);
    else if(ret==NOIR_PARTIAL)  /* request to re-gen thumbnail enqueued. */
      file_info_set_infobits(fi,file_info_get_infobits(fi)&(~FF_THUMB));
    else if(ret==NOIR_TYPEMISMATCH)
      form_alert(NULL,"Sorry, don't know how to thumbnail\n\"%s\"...",file_info_get_tag(fi));
    else {
      E(0,"enqueue failed for \"%s\": %d\n",file_info_get_tag(fi),ret);
      file_info_set_infobits(fi,file_info_get_infobits(fi)&(~FF_THUMB)); }}
  else
    E(0,"could not delete thumbnail(s) for \"%s\" => %d\n",file_info_get_tag(fi),ret);
  return ret; }



/**
 * context-menu action: rethumbnail items
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  file_info *fi=file_info_fetch(NULL,filename);

  if(fi) {
    rethumb(fi);

    fi=file_info_get_1st_child(fi);
    while(fi) {
      rethumb(fi);
      fi=file_info_get_next(fi); }

    return NOIR_SUCC; }

  return NOIR_NODATA; }



/*****************************************************************************/
