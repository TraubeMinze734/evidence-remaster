/** @file  delete.c
 *  @brief context-menu action "delete"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui/common/alert.h"      /* form_*() */
#include "gui/common/session.h"    /* noir_sess_*() */
#include "file.h"       /* file_job_create() */
#include "meta.h"       /* struct _meta */
#include "util.h"       /* estring_*() */
#include "action.h"       /* action_file_dst() */


#define TMPL "Really delete\n%s?"



/******************************************************************************/



/**
 * context-menu action: delete a file
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  noir_sess      *ns=noir_sess_get_by_serial(serial);
  int             ret=NOIR_IGNORED;
  estring        *msg=estring_new(2048);
  action_context *an;

  if(!filename)
    return NOIR_FAIL;

  if(!msg)
    return NOIR_OOM;

  estring_printf(msg,TMPL,filename);

  if(action_context_get_flags(ac)&AF_SELECTION) {
    if((an=action_context_clone(ac))) {
      action_context_remove_and_dst_file(an,action_file_get_by_tag(action_context_get_1st(an),filename));
      estring_appendf(msg,"\n\nRemaining files:\n%s",action_context_files_to_quoted_string(an));
      action_context_dst(an); }}

  if(!ESTRING_GET_CSTRING(msg))
    return NOIR_OOM;

  if(form_yes_no_all(ns,ESTRING_GET_CSTRING(msg),ac)==NOIR_SUCC) {
    if(action_context_get_flags(ac)&AF_YES_TO_ALL) {
      if((an=action_context_clone(ac))) {
        E(0,"deleting all remaining files...\n%s\n\n",action_context_files_to_quoted_string(an));
        ret=(file_job_create(ns,NOIR_JOB_RM,an,NULL)>=0)?NOIR_SUCC:NOIR_FAIL;
        action_context_set_flags(ac,action_context_get_flags(ac)|AF_LAST); }}
    else {
      an=action_context_new2(filename,NULL);
      if(an) {
        E(0,"deleting \"%s\"...\n",filename);
        ret=(file_job_create(ns,NOIR_JOB_RM,an,NULL)>=0)?NOIR_SUCC:NOIR_FAIL; }}}

  estring_free(msg,TRUE);

  return ret; }



/******************************************************************************/
