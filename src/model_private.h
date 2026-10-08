/** @file  model_private.h
 *  @brief internal model of file-system
 */

#ifndef FS_MODEL_PRIVATE

#  define FS_MODEL_PRIVATE

#if defined(__cplusplus)
extern "C" {
#endif

#  include "config.h"
#  include "meta.h"
#  include "userconfig.h"
#  include <sys/types.h>
#  include <pthread.h>



#ifdef HIDE_FILES_STARTING_WITH_HASH
#  define IS_HIDDEN(name) (name&&((name[0]=='.')||(name[0]=='#')||(name[0]=='%')||(name[strlen(name)-1]=='~')))
#else
#  define IS_HIDDEN(name) (name&&((name[0]=='.')||(name[0]=='%')||(name[strlen(name)-1]=='~')))
#endif

#define IS_DOTDIR(name) (name&&(!strcmp(name,DIR_MYSELF)))
#define IS_IGNOBLE(name) (name&&(!strcmp(name,DIR_PARENT)))



#define DIR_DELIMITER      '/'
#define DIR_DELIM_STR      "/"
#define DIR_PARENT         ".."
#define DIR_MYSELF         "."
#define DIR_DOT            ".files"



#ifndef FALSE
#  define FALSE 0
#  define TRUE (!(FALSE))
#endif



/*****************************************************************/



#define CLIENT_PAUSED(cf)     (((cf)->flags)&CF_PAUSED)
// #define CLIENT_LISTENS(cf,fi) ((!(((cf)->flags)&CF_PAUSED))&&(dcd_get((fi),(cf))||(((fi)->parent)&&dcd_get((fi)->parent,(cf)))))
#define CLIENT_LISTENS(cf,fi)  (!CLIENT_PAUSED(cf))



/*****************************************************************/



/** POSIX compatible meta-data */
struct _posixmd {
  mode_t  mode,lmode;                   /**< mode (cf stat()) of file, symlink to file */
  nlink_t hardlinks;                    /**< hardlinks (# of names) */
  uid_t   uid;                          /**< ID of owning user */
  gid_t   gid;                          /**< ID of owning group */
  off_t   size;                         /**< the content's size in bytes */
  time_t  atime;                        /**< when the data was last accessed */
  time_t  mtime;                        /**< when the file's content was last changed */
  time_t  ctime; };                     /**< when the (POSIX) meta-data were last changed */



/** a file-info entry */
struct _file_info {
  char              *name,*tag,         /**< file name, full path/UUID/URL */
                    *mime,*linkee,      /**< MIME-type, indirection (for virtuals) */
                    *sortkey;           /**< sort key */
  file_flags         have;              /**< what info is already set on f/i */
  item_flags         flags;             /**< "read-only" etc. */
  struct _posixmd   *intrinsic;         /**< stat()-info */
  struct _noir_md   *meta;              /**< meta-data */
  char              *thumbfile;         /**< thumbnail (name and path) */
  struct _dcd       *clientdata;        /**< list of client-data items */
  int                children;          /**< children (count) */
  int                refcount;          /**< references to this item */
  struct _file_info *parent,            /**< the parent file_info */
                    *child;             /**< the first child's file_info */
  struct _file_info *next;              /**< next sibling (in same folder) */
};



/** a client/view object */
struct _client_funs {
  const char          *name;            /**< the view's name (usually type) */
  void                *data;            /**< view's data (struct _myview *) */
  client_flags         flags;           /**< flags: paused etc. */
  int                (*evt_view)(file_info *,model_event type,file_flags changed,void *); /**< event callback */
  int                (*dst_data)(void **);     /**< destructor for private 'data' */
  int                (*set_selection)(void *); /**< method to highlight session's selection in this view */
  void              *(*dcd_set_data)(void *);  /**< DCD setter */
  int                (*dcd_dst_data)(void **); /**< DCD destructor */
  pthread_mutex_t      viewlock;        /**< view mutex */
  void                *user1,           /**< next view in per-session chain */
                      *user2;           /**< session this view belongs to */
  struct _client_funs *next; };



/** client/view-specific data to associate with a single file-entry */
struct _dcd {
  client_funs  *client;                 /**< the client this data is for */
  void         *data;                   /**< a client-specific pointer */
  struct _dcd  *next; };                /**< next DCD for this file-info */



/** entry in (directories-to-be-read) queue */
struct _q_element {
  struct _file_info *dir;               /**< the file-info of the directory we're reading */
  file_flags         want,wanted;       /**< we want some things, we got some things */
  unsigned long      td,tt;             /**< int'l benchmarking: time-delta, time-total */
  void              *backend_data;      /**< backend-specific pointer */
  struct _q_head    *head;              /**< head */
  struct _q_element *next; };           /**< next dir in to-read queue */



/** a queue */
struct _q_head {
  pthread_mutex_t    qchg;              /**< mutex - frontend and backend shouldn't modify the Q at the same time */
  queue_flags        flags;             /**< busy/idle/dead etc. */
  pthread_t          scanner;           /**< the worker-thread associated with this queue */
  struct _q_element *anchor; };         /**< list of items in queue */




#if defined(__cplusplus)
}
#endif

#endif
