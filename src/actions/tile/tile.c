/** @file  tile.c
 *  @brief set image as backdrop
 */

#include <stdio.h>

#include "debug.h"
#include "errors.h"
#include "action.h"
#include "meta.h"
#include "properties.h"
#include "gui/common/session.h"
#include "gui/efl/common/theme.h"



/*****************************************************************************/



/**
 * context-menu action: set image as backdrop
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  noir_sess  *ns=noir_sess_get_by_serial(serial);
  noir_theme *theme;

  if(ns) {
    if(!(theme=noir_sess_get_theme(ns))||!theme->data)
      return NOIR_IGNORED;
    E(0,"trying to set \"%s\" as backdrop...\n",filename);
    prop_bag_prop_set(theme->data,"iconview",filename);
    prop_bag_prop_set(theme->data,"backgroundfactory","tile");
    return NOIR_SUCC; }
  else
    return NOIR_FAIL; }



/*****************************************************************************/
