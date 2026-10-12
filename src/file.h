#ifndef FILE_H
#  define FILE_H

#include "meta.h"               /* struct _meta */
#include "model.h"              /* struct _file_info */
#include "action.h"             /* AF_YES_TO_ALL ... */
#include "config.h"             /* ulong for FreeBSD */

#  ifndef MY_NAME
#    define MY_NAME META_DFLT_GRP
#    define EA_NAME "EA"
#  endif

#  define PROTOCOL_DELIMITER "://"
#  define PROTOCOL_FILE      "file" PROTOCOL_DELIMITER

#  include <limits.h>

#  ifndef PATH_MAX
#    define PATH_MAX         4096
#  endif



typedef enum {  /* index==(number of bits to shift)/3 (one octal in permset) */
  NOIR_PERMSET_USER=2,
  NOIR_PERMSET_GROUP=1,
  NOIR_PERMSET_OTHER=0 } noir_permset;






typedef enum {
  FA_NONE=0,
  FA_USER=1,
  FA_MEDIA=2,
  FA_ROOT=4
} falvl;



typedef struct _file_annotation {
  char  *tag;
  char  *sub;
  falvl  lvl;
  char  *dsc;
} file_annotation;






typedef enum {
  FGD_NONE=0,
  FGD_STAT=1,
  FGD_DEEP=2
} fgd_flags;



/** an entry returned by file_getdir() */
typedef struct _dir_entry {
  struct _dir_entry *child;      /**< file's children */
  char              *name;       /**< file's name */
  char              *tag;        /**< file's UUID/path/URL/... */
  ulong              children;   /**< file's children (number of) */
  ulong              descendents;/**< file's descendents (number of) */
  off_t              size;       /**< file's size */
  off_t              net;        /**< file's size (dirs: direct children only) */
  mode_t             mode;       /**< file's mode */
  time_t             mtime;      /**< when the file-contents were last modified */
  struct _dir_entry *next; } dir_entry;






typedef enum {
  NOIR_JOB_DK,                   /**< unspecified type */
  NOIR_JOB_DIAL,                 /**< dial only */
  NOIR_JOB_CP,                   /**< copy items */
  NOIR_JOB_MV,                   /**< move items */
  NOIR_JOB_RM                    /**< delete items */
} noir_job_type;
extern char *job_type[];



typedef enum {
  NOIR_JOB_UNCHANGED=0,          /**< nothing has changed since UI last saw this job */
  NOIR_JOB_START,                /**< job was just created, initialize display */
  NOIR_JOB_ADD,                  /**< adding item (in scan) */
  NOIR_JOB_DEL,                  /**< finished with item, clock it in and remove it */
  NOIR_JOB_PROGRESS,             /**< show progress */
  NOIR_JOB_EDIT,                 /**< open a requester that allows editing of the target file-name */
  NOIR_JOB_QUESTION,             /**< open a requester (YES/ALL/CANCEL/SKIP/...) */
  NOIR_JOB_DONE                  /**< job was completed, release display resources */
} noir_job_status;
extern char *job_status[];



typedef struct _noir_job noir_job;
typedef int (*job_ui)(noir_job *);
/** a (copy, move, delete, ...) job */
struct _noir_job {
  void             *session;     /**< the session that created this job */
  int               async;       /**< operation is async, need GUI locks */
  noir_job_type     type;        /**< job-type: copy/move/delete/... */
  int               status,      /**< job asks: show progress/ask/... */
                    err;         /**< error-code */
  action_flags      flags;       /**< UI flags: yes-to-all, cancel-all, ... */
  int               reply;       /**< UI says: skip/cancel/... */
  job_ui            callback;    /**< progress indicator */
  off_t             file_bytes,  /**< total bytes in file */
                    file_bytes_copied; /**< of which x have been copied */
  ulong             file_index;  /**< nth file of m */
  off_t             total_bytes, /**< total bytes in job */
                    total_bytes_copied; /**< of which y have been processed */
  ulong             total_files; /**< total number of items in job */
  ulong             pits;        /**< process indicator timestamp */
  action_context   *ac;          /**< all source-items (selection) */
  char             *src_path,    /**< current source-item */
                   *dst_path;    /**< target-dir or target name */
  char             *src,         /**< current source-name (below src_path) */
                   *dst;         /**< current target-name (below dst_path) */
  dir_entry        *tree;        /**< source-tree */
  ulong             id;          /**< a ticket for the caller */
  pthread_t         thread;      /**< the thread that handles this job */
  pthread_mutex_t   datalock;    /**< pull while changing this struct */
  pthread_cond_t    backendpause;/**< backend cond-var */

  void             *data;        /**< data the caller wants the progress indicator to have */
  void             *user1,       /**< the progress indicator plugin can save state here, default GTK2 PI: window */
                   *user2,       /**< the progress indicator plugin can save state here, default GTK2 PI: container */
                   *user3,       /**< the progress indicator plugin can save state here, default GTK2 PI: label */
                   *user4,       /**< the progress indicator plugin can save state here, default GTK2 PI: progress-bar 1, bytes in file */
                   *user5,       /**< the progress indicator plugin can save state here, default GTK2 PI: progress-bar 2, bytes in job */
                   *user6;       /**< the progress indicator plugin can save state here, default GTK2 PI: progress-bar 3, files in job */
  char             *challenge;   /**< challenge text ("this file-name is taken!") */
  const char       *buttons;     /**< buttons for dialog box (NOIR_JOB_QUESTION only) */
  char            **edit;        /**< a file-name to edit (NOIR_JOB_EDIT only) */
  struct _noir_job *next; };






/* get POSIXly correct readdir_r() prototypes on Suns */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199506L
#endif
#ifndef _POSIX_PTHREAD_SEMANTICS
#define _POSIX_PTHREAD_SEMANTICS
#endif



#if HAVE_READDIR_R
#  define READDIR(dir,de,de_ptr)    ((readdir_r(dir,&de,&de_ptr))?NULL:de_ptr)
#else
#  define READDIR(dir,de,de_ptr)    (de_ptr=readdir(dir))
#endif



dir_entry       *file_getdir(const char *path,fgd_flags,void (*show)(dir_entry *,void *),void *data);
dir_entry       *dir_entry_dst_all(dir_entry *de);

inline int       file_opendir(q_element *q);
inline int       file_closedir(q_element *q);

int              file_scanner_activate(q_head *qh);

pid_t            file_fork(void *ns,char **x);
int              file_run(void *ns,const char *tmpl,const char *url,const char *mime);
int              file_system(void *ns,const char *cmdline);



/* convenience */
char            *get_username(uid_t);
char            *get_groupname(gid_t);
char            *get_hostname(void);

int              file_partial(int,int *);

char            *file_user_setup(const char *);

time_t           file_date_parse(const char *);
int              file_perms_parse(const char *perms,noir_permset ps,char **who);

int              file_is_same(const char *file,const char *file_or_dir);

char            *file_tag2name(const char *tag);

int              file_is_globbable(const char *b);
action_context  *file_glob(const char *pattern,const char **errmsg);

int              file_load(const char *fn,char **data);
int              file_save(const char *fn,mode_t mode,ssize_t len,const char *data);

int              file_lock(const char *file,int flags,mode_t mode);
int              file_unlock(int fd);



/* enquire about the item */
file_annotation *file_annotation_get(const char *tag);
const char      *file_get_mime(const char *);
char            *file_readlink(const char *);
int              file_exists(const char *);
int              file_get_info(void *,const char *,file_info **);
int              file_get_meta_with_lock(file_info *info,pthread_mutex_t *mutex);
int              file_get_meta_only(void *,const char *,noir_md **); /* use file_exists() if you only need mode! */
/* int           file_set_meta(void *,const char *,meta **); */



/* do something with the item */
int              file_mv_do(noir_job *);
int              file_cp_do(noir_job *);
int              file_rm_do(noir_job *);

int              file_mv(const char *,const char *);  /* sync wrapper */
int              file_cp(const char *,const char *);  /* sync wrapper */
int              file_rm(const char *);         /* sync wrapper */

int              file_symlink(const char *,const char *);
int              file_hardlink(const char *,const char *);
int              file_link(int sym,const char *fm,const char *to);
int              file_mkdir(const char *,int,int);
int              file_rmdir(const char *);
int              file_activate(void *,const char *,const char *,const char *);

/* jobs */
noir_job        *file_job_simple(void *session,noir_job_type type);
int              file_job_create(void *session,noir_job_type type,action_context *files,void *data);
int              file_job_wait_all(void);
int              file_job_update(noir_job *,noir_job_status,int,const char *,const char *);
int              file_job_stats(noir_job *job,ulong fbt,ulong fbc,ulong fno,ulong bt,ulong bc,ulong no);
int              file_job_progress_show_all(void);
int              file_job_edt(noir_job *,const char *,char **);
int              file_job_ask(noir_job *,const char *,const char *,...);

/* init/exit */
int              file_init(q_head *,const char *);
int              file_exit(void);
#endif
