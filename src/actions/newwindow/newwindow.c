/** @file  newwindow.c
 *  @brief context-menu action "new file-manager window"
 */

#include "gui/common/alert.h"
#include "gui/common/session.h"
#include "action.h"
#include "meta.h"



/******************************************************************************/



/**
 * context-menu action: create a new session/window
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  noir_sess  *ns=noir_sess_get_by_serial(serial);
  if(ns)
    return noir_sess_create(SESSION_COPY,ns,filename);
  return NOIR_FAIL; }



/******************************************************************************/
