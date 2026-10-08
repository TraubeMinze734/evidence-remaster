/** @file  sortkey2name.c
 *  @brief rename from sortkey
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "debug.h"
#include "errors.h"
#include "action.h"
#include "meta.h"
#include "file.h"
#include "file_lcl.h"
#include "util.h"
#include "gui/common/alert.h"



/*****************************************************************************/



/**
 * context-menu action: rename from sortkey
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  int         ret=NOIR_SUCC;
  noir_meta  *m;
  const char *fn=NULL,*ext=NULL;
  char       *nn=NULL,*dn=NULL,*p=NULL;

  if(!filename||!md)
    return NOIR_FAIL;
  if(!(m=meta_in_group(md,"sortkey",NULL))||!meta_get_val(m)||!strlen(meta_get_val(m)))
    return NOIR_NODATA;

  if(!(fn=file_name(filename)))
    return NOIR_FAIL;
  if((ext=strrchr(fn,'.')))
    esprintf(&p,"%s%s",meta_get_val(m),ext);
  else
    p=strdup(meta_get_val(m));
  if(!p) {
    ret=NOIR_OOM;
    goto done; }

  if(!(dn=file_dirname(filename))) {
    ret=NOIR_OOM;
    goto done; }

  if(!(nn=file_compose_path(dn,p,NULL))) {
    ret=NOIR_OOM;
    goto done; }

  if(!strcmp(filename,nn))
    ret=NOIR_IGNORED;
  else {
    E(1,"rename \"%s\" => \"%s\"...\n",filename,nn);
    ret=file_mv(filename,nn);
    if(ret!=NOIR_SUCC)
      form_alert(NULL,"Renaming\n\"%s\" to\n\"%s\" failed!\n(code %d, error %d - %s)",filename,nn,ret,errno,strerror(errno)); }

 done:
  if(dn)
    free(dn);
  if(p)
    free(p);
  if(nn)
    free(nn);

  return ret; }



/*****************************************************************************/
