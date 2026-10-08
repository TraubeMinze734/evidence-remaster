#ifndef FILE_FUN_H
#  define FILE_FUN_H

#  include "meta.h"
#  include "model.h"
#  include "file.h"
#  include "monitors.h"



typedef struct _file_fun {
  int   (*file_get_info)(void *,const char *,file_info **);
  int   (*file_get_meta)(void *,const char *,noir_md *md);
  int   (*file_set_meta)(void *,const char *,noir_md *md,noir_meta *r);

    dir_entry *(*file_getdir)(const char *,fgd_flags,void (*show)(dir_entry *,void *),void *);
  void *(*file_opendir)(const char *path);
  int   (*file_readdir)(void **data,const char *dir_name,const char *file_name);
  int   (*file_closedir)(void **data);

  char *(*file_mime_handler_get)(const char *mime,char **);
  int   (*file_mime_handler_set)(const char *mime,const char *handler);
  char *(*file_get_mime)(const char *);

  int   (*file_mv)(noir_job *);
  int   (*file_cp)(noir_job *);
  int   (*file_rm)(noir_job *);

#if 0
  int   (*file_mv)(const char *,const char *);
  int   (*file_cp)(const char *,const char *);
  int   (*file_rm)(const char *);
#endif

  int   (*file_mkdir)(const char *,int,int);
  int   (*file_rmdir)(const char *);
  int   (*file_symlink)(const char *,const char *);
  int   (*file_hardlink)(const char *,const char *);

  char *(*file_readlink)(const char *);

  int   (*file_exists)(const char *);

  int   (*file_activate)(const char *,const char *);

  int   (*file_m_add)(noir_monitor *);
  int   (*file_m_rem)(noir_monitor *);

  int   (*file_scanner_activate)(q_head *qh);

  int   (*file_job_collect)(noir_job *job);
  int   (*file_job_start)(noir_job *job);

  char *(*file_tag2name)(const char *tag);

  int   (*file_init)(q_head *);
  int   (*file_exit)(void);
} file_fun;

/* extern struct _file_fun ff; */



file_fun *file_get_backend(const char *tag);
#endif
