/** @file  action.h
 *  @brief action-contexts for context-menu ("right click") actions
 */

#ifndef ACTION_H
#  define ACTION_H

#  include "meta.h"



/** context for an action selected from the context-menu.
 * this is interesting mostly when the action is to be applied to a
 * seletion of several objects. */
typedef enum {
  AF_NONE=0,           /**< no flags */
  AF_YES_TO_ALL=1,     /**< "yes to all", stop asking */
  AF_CANCEL_ALL=2,     /**< "no to all",  stop asking */
  AF_SELECTION=4,      /**< selection set on action-context (multiple files) */
  AF_MENUDONE=8,       /**< terminate menu-loop */
  AF_LAST=16,          /**< last item to process, free any resources. plugins using this should check this last thing before returning (specifically, after any calls to form_yes_no_all()) */
} action_flags;



typedef struct _action_file action_file;
typedef struct _action_context action_context;



action_file     *action_file_dst(action_file *);
action_file     *action_file_dst_all(action_file *af);
action_file     *action_file_new(action_context *ac,const char *tag);
action_file     *action_file_get_next(action_file *curr);
action_file     *action_file_set_next(action_file *af,action_file *n);
const char      *action_file_get_tag(action_file *af);
noir_md         *action_file_get_meta(action_file *af);
int              action_file_set_meta(action_file *af,noir_md *m);
action_file     *action_file_get_by_tag(action_file *af,const char *tag);
action_file     *action_context_remove_file(action_context *ac,action_file *af);
action_file     *action_context_remove_and_dst_file(action_context *ac,action_file *af);
const char      *action_context_get_cwd(action_context *ac);
int              action_context_set_cwd(action_context *ac,const char *cwd);
action_file     *action_context_get_selection(action_context *ac);
action_file     *action_context_set_selection(action_context *ac,action_file *af);
action_file     *action_context_get_single(action_context *ac);
action_file     *action_context_set_single(action_context *ac,action_file *af);
#define          action_context_get_1st(ac) action_context_get_selection(ac)
action_flags     action_context_get_flags(action_context *ac);
action_flags     action_context_set_flags(action_context *ac,action_flags af);
action_flags     action_context_add_flags(action_context *ac,action_flags af);
action_flags     action_context_clr_flags(action_context *ac,action_flags af);
action_flags     action_context_tst_flags(action_context *ac,action_flags af);
void             action_context_dst(action_context *ac);
action_context  *action_context_new(long serial,const char *cwd);
action_context  *action_context_new2(const char *src,const char *tgt);
action_context  *action_context_clone(action_context *ac);
action_file     *action_context_selection_contains(action_context *ac,const char *tag);
action_file     *action_context_contains(action_context *ac,const char *tag);
void            *action_context_get_data(action_context *ac);
void            *action_context_set_data(action_context *ac,void *data);
long             action_context_get_serial(action_context *ac);
long             action_context_get_seletion_size(action_context *ac);
char            *action_context_files_to_quoted_string(action_context *ac);
#endif
