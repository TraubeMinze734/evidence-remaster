/** @file  model.h
 *  @brief internal model of file-system
 */

#ifndef FS_MODEL

#  define FS_MODEL

#if defined(__cplusplus)
extern "C" {
#endif

#  include <sys/types.h>
#  include "meta.h"



/*****************************************************************/



typedef struct _posixmd posixmd;
typedef struct _dcd dcd;
typedef struct _file_info file_info;
typedef struct _client_funs client_funs;
typedef struct _q_element q_element;
typedef struct _q_head q_head;



/*****************************************************************/



/** how to handle duplicate names (not tags) in vfolders */
typedef enum {
  VIM_REPLACE=0,      /**< allow only one item with the same name */
  VIM_INDEX=1,        /**< index: "foo", "foo (1)", "foo (2)"... */
  VIM_DIFF=2          /**< diff: "foo (in bar)", "foo (in bazong)"... */
} vi_mode;



/** what state a queue's worker is in */
typedef enum {
  QUEUE_NONE=0,       /**< thread does not exist */
  QUEUE_SCANNING=1,   /**< thread is busy */
  QUEUE_ZOMBIE=2      /**< thread has terminated, must be joined */
} queue_flags;



/** path_get() mode */
typedef enum {
  PG_ENQUEUE_NOT,     /**< do note enqueue this directory */
  PG_ENQUEUE_ALL,     /**< read all directories along the path */
  PG_ENQUEUE_LAST,    /**< read only deepest directory in path */
  PG_ENQUEUE_PATH     /**< create and examine elements of path only */
} pg_mode;



/** how to sort */
typedef enum {
  SORT_UNSORTED=1,    /**< don't sort */
  SORT_CASEFOLD=2,    /**< sort is case-insensitive */
  SORT_DIRS1ST=4,     /**< don't put directories in front */
  SORT_BANDNAMES=8,   /**< musicshop sort: ignore "the" etc. in band-names */
  SORT_BYMIME=16,     /**< sort as "mp3.mysong" rather than "mysong.mp3" */
  SORT_BYSIZE=32,     /**< by size */
  SORT_BYDATE=64,     /**< by date */
  SORT_BYUSER=128,    /**< by user */
  SORT_BYMETA=256     /**< by meta */
} sort_flags;



/** bit-vector describing what data we already have */
typedef enum {
  FF_NONE  = 0,       /**< got nothing */
  FF_NAME  = 1,       /**< got name */
  FF_STAT  = 2,       /**< got stat()-info */
  FF_MIME  = 4,       /**< got MIME-type */
  FF_META  = 8,       /**< got meta-data */
  FF_THUMB = 0x10,    /**< got thumbnail */

  FF_ANY   = 0x1f,    /**< any of the "got"-flags? */

  FF_FIRST = 0x20,    /**< issued when about to read first item in a folder */
  FF_LAST  = 0x100,   /**< issued after last item in a folder was read */

  FF_NAME_ = 0x100,   /**< got names for all items in folder */
  FF_STAT_ = 0x200,   /**< got stat-info for all items in folder */
  FF_MIME_ = 0x400,   /**< got mime-type for all items in folder */
  FF_META_ = 0x800,   /**< got other meta-data for all items in folder */
  FF_THUMB_= 0x1000,  /**< got thumbnails for all (applicable) items in folder */
  FF_ANY_  = 0x1f00,  /**< any of the "got all"-flags? */

  FF_SEMA  = 0x10000, /**< for directories */
  FF_UNSEEN= 0x20000, /**< this item already clocked in? */

  FF_SORTED= 0x40000  /**< deprecated */
} file_flags;



/** non-POSIX flags for a file-info */
typedef enum {
  IF_NONE    =0,      /**< no flags */
  IF_NOUSE   =1,      /**< no 'x' */
  IF_NOMODIFY=2,      /**< no 'w' */
  IF_VIRTUAL =4,      /**< virtual item */
  IF_DOTDIR  =8       /**< ".folder" */
} item_flags;



/** flags for a client/view */
typedef enum {
  CF_NONE  =0,        /**< no flags */
  CF_PAUSED=1         /**< client/view isn't interested in updates right now */
} client_flags;



/**< what kind of events the model can send */
typedef enum {
  MODEL_UPD=1,        /**< update an item */
  MODEL_DEL=2,        /**< remove an item */
  MODEL_CWD=3,        /**< change directory */
  MODEL_MSC=4         /**< miscellaneous, see file_flags for sub-type */
} model_event;



/*****************************************************************/



posixmd     *posixmd_new(void);
posixmd     *posixmd_virtual(mode_t mode);
/* file_info   *file_info_dst(file_info *); */
file_info   *file_info_new(const char *name,const char *tag);
file_info   *file_info_ref(file_info *info);
file_info   *file_info_unref(file_info *info);
file_info   *file_info_fetch(file_info *root,const char *tag);
file_info   *file_info_insert(file_info *root,const char *tag);
file_info   *file_info_copy(file_info *src);
file_info   *file_info_virtual(file_info *vfolder,const char *tag,vi_mode vim);
file_info   *file_info_vfolder(file_info *root,const char *tag);

void        *dcd_get_data(file_info *,client_funs *);
dcd         *dcd_get(file_info *,client_funs *);
dcd         *dcd_dst(file_info *,client_funs *);
dcd         *dcd_set(file_info *,client_funs *,void *);

q_element   *dir_dequeue(q_head *);
q_element   *dir_requeue(q_head *);

int          client_chain_lock(const char *);
int          client_chain_unlock(const char *);

int          client_trylock(client_funs *,const char *);
int          client_lock(client_funs *,const char *);
int          client_unlock(client_funs *,const char *);

#define      client_get_data(a) ((a)->data)
void         client_unregister(client_funs *);
client_funs *client_register(const char *name,void *data);
client_funs *client_register_full(const char *name,void *data,
                                  int (*evt_view)(file_info *,model_event type,file_flags changed,void *),
                                  int (*dst_data)(void **),
                                  int (*set_selection)(void *));

char        *path_rewrite(const char *path);

int          model_path_get(q_head *qh,file_info *root,char *path,pg_mode m);

void         model_ls(file_info *root,int indent);
int          model_find_by_name(file_info *root,char *name);

int          model_evt(file_info *,model_event etype,file_flags eflags);

inline int   model_sort_fun(sort_flags,const char *,file_info *,const char *,file_info *);

q_head *     model_init(void);
int          model_exit(q_head *);



/*****************************************************************/



/* intrinsic (POSIX meta-data) getters */
mode_t      posixmd_get_mode(posixmd *i);
mode_t      posixmd_get_lmode(posixmd *i);
nlink_t     posixmd_get_hardlinks(posixmd *i);
uid_t       posixmd_get_uid(posixmd *i);
gid_t       posixmd_get_gid(posixmd *i);
off_t       posixmd_get_size(posixmd *i);
time_t      posixmd_get_atime(posixmd *i);
time_t      posixmd_get_mtime(posixmd *i);
time_t      posixmd_get_ctime(posixmd *i);


mode_t      file_info_get_mode(file_info *fi);
mode_t      file_info_get_lmode(file_info *fi);
nlink_t     file_info_get_hardlinks(file_info *fi);
uid_t       file_info_get_uid(file_info *fi);
gid_t       file_info_get_gid(file_info *fi);
off_t       file_info_get_size(file_info *fi);
time_t      file_info_get_atime(file_info *fi);
time_t      file_info_get_mtime(file_info *fi);
time_t      file_info_get_ctime(file_info *fi);



/* coreutils (dirname, basename, filename) */
const char *file_info_dirname(file_info *info);
const char *file_info_filename(file_info *info);
char       *file_info_basename(file_info *info,const char *suffix);



/* file-info getters */
file_info  *file_info_get_next(file_info *info);
file_info  *file_info_get_parent(file_info *info);
file_info  *file_info_get_1st_child(file_info *info);
int         file_info_get_num_children(file_info *info);
int         file_info_get_refcount(file_info *info);
dcd        *file_info_get_clientdata(file_info *info);
file_flags  file_info_get_infobits(file_info *info);
item_flags  file_info_get_flags(file_info *info);
const char *file_info_get_tag(file_info *info);
const char *file_info_get_ltag(file_info *info);
const char *file_info_get_linkee(file_info *info);
const char *file_info_get_name(file_info *info);
const char *file_info_get_mime(file_info *info);
const char *file_info_get_sortkey(file_info *info);
const char *file_info_get_thumbfile(file_info *info);
posixmd    *file_info_get_intrinsic(file_info *info);
noir_md    *file_info_get_meta(file_info *info);


int         file_info_is_virtual(file_info *info);
int         file_info_is_pure_virtual(file_info *info);
int         file_info_is_folder(file_info *info);
int         file_info_is_vfolder(file_info *info);
int         file_info_is_dotfolder(file_info *info);



/* file-info setters */
file_info  *file_info_set_next(file_info *info,file_info *next);
file_info  *file_info_set_parent(file_info *info,file_info *parent);
file_info  *file_info_set_1st_child(file_info *info,file_info *child);
int         file_info_set_num_children(file_info *info,int nc);
int         file_info_update_num_children(file_info *info);
dcd        *file_info_set_clientdata(file_info *info,dcd *cd);
file_flags  file_info_set_infobits(file_info *info,file_flags have);
file_flags  file_info_add_infobits(file_info *info,file_flags have);
file_flags  file_info_clr_infobits(file_info *info,file_flags have);
item_flags  file_info_set_flags(file_info *info,item_flags flags);
const char *file_info_set_tag(file_info *info,const char *tag);
const char *file_info_set_name(file_info *info,const char *name);
const char *file_info_set_mime(file_info *info,const char *mime);
const char *file_info_set_sortkey(file_info *info,const char *sortkey);
const char *file_info_set_thumbfile(file_info *info,const char *tf);
posixmd    *file_info_set_intrinsic(file_info *info,posixmd *intrinsic);
noir_md    *file_info_set_meta(file_info *info,noir_md *md);



#if defined(__cplusplus)
}
#endif

#endif
