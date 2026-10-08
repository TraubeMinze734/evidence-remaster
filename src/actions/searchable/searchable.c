/** @file  searchable.c
 *  @brief context-menu action "Make folder searchable"
 */

#include <stdio.h>

#include "debug.h"
#include "errors.h"
#include "action.h"
#include "meta.h"
#include "properties.h"
#include "search.h"
#include "gui/common/session.h"



/******************************************************************************/

/**
 * context-menu action: make a new directory
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  return search_scan_dir(NULL,filename,0); }



/******************************************************************************/
