/** @file  session_private.h
 *  @brief the session-struct.  one per window.  a session may hold several views.
 */

#ifndef SESSION_PRIVATE_H
#  define SESSION_PRIVATE_H
#  include "evidence.h"
#  include "action.h"
#  include "typebuffer.h"
#  include "session.h"



/** a session */
struct _noir_sess {
  long          serial;       /**< "primary key" : ) */
  unsigned long focused;      /**< when was this window last focused? */

  client_funs  *clients;      /**< list of views in this window */
  client_funs  *client;       /**< current view in this window */

  typebuffer   *tb;

  int sortflags,              /**< sort-hints: unsorted, folders first, ... */
      thumbnails_in_iconlist, /**< show thumbnails rather than icons in icon-view? */
      thumbnails_in_shelf,    /**< show thumbnails rather than icons in shelf? */
      without_notebook,       /**< hide notebook-tabs? */
      without_shelf,          /**< disable "shelf"? */
      without_decorations,    /**< disable window-borders? */
      sema_focus,             /**< "semaphore" */
      start_in,               /**< which view to show first */
      fullscreen,             /**< maximize window? */
      icon_view_last_tooltip; /**< GTK: index of last icon we made a tooltip for */

  noir_tooltip *tooltip;      /**< tooltip, if any */

  tooltip_flags with_tooltips;/**< mouse-over + delay tooltips? (can still be activated
                                   by key-press/mouse-clicke otherwise) */

  char *geometry,             /**< geometry string */
       *cwd,                  /**< current work-dir */
       *pwd,                  /**< previous work-dir */
       *cfg_file;             /**< setup-file (forthcoming) */

  action_context *selection;  /**< currently selected files */
  action_context *drag_files; /**< which files were last dragged *from* ev */

  file_info  *cd;             /**< current dir (file-info) */

  char       *iconset;       /**< option takes path or name, will set path+file */
  void       *theme;         /**< theme data */

  void       *uidata;        /**< widgets */

  struct _noir_sess *next;   /**< next session in list */
};



int noir_sess_uidata_free(struct _noir_sess *ns);
int noir_sess_uidata_new(struct _noir_sess *ns);



#endif
