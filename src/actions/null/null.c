/** @file  null.c
 *  @brief do nothing
 */

#include "errors.h"
#include "action.h"
#include "meta.h"



/*****************************************************************************/



/**
 * context-menu action: do nothing
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  return NOIR_IGNORED; }



/*****************************************************************************/
