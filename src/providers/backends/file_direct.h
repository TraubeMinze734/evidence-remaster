/* supports code by Christian Kreibich <cK@whoop.org>.and code by Azundris */

#include "model_private.h"

#define D_ENTER
#define D_RETURN       return
#define D_RETURN_(x)   return (x)



#ifndef MAXPATHLEN
#  define MAXPATHLEN PATH_MAX
#endif



typedef enum {
  FS_OP_FORCE    =1,
  FS_OP_RECURSIVE=2 } FsOpt;



#ifdef HAVE_LIBSHAREDMIME
typedef int bool;

#  include <mime-types.h>

struct _libsharedmime {
  void       *handle;
  void        (*mimetypes_init)();
  const char *(*mimetypes_get_file_type)(const char *,mimetypes_flags);
  const char *(*mimetypes_get_type_info)(const char *mime_type,const char *lang);
};
#endif



#ifdef HAVE_LIBCURL
#  include <curl/curl.h>

struct _libcurl {
  void       *handle;
  CURLcode    (*curl_global_init)(long flags);
  void        (*curl_global_cleanup)(void);
  CURL       *(*curl_easy_init)(void);
  void        (*curl_easy_cleanup)(CURL *handle);
  CURLcode    (*curl_easy_setopt)(CURL *handle,CURLoption option,...);
  CURLcode    (*curl_easy_perform)(CURL *handle);
  CURLcode    (*curl_easy_getinfo)(CURL *curl, CURLINFO info,...);
};
#endif



#ifdef HAVE_LIBMAGIC
#  include <magic.h>

struct _libmagic {
  void       *handle;
  magic_t     (*magic_open)(int flags);
  void        (*magic_close)(magic_t cookie);
  int         (*magic_load)(magic_t cookie,const char *filename);
  const char *(*magic_file)(magic_t cookie,const char *filename);
};
#endif



#ifdef HAVE_FAM
#  include <fam.h>

struct _libfam {
  void *handle;
  int   (*FAMOpen)(FAMConnection *fc);
  int   (*FAMClose)(FAMConnection *fc);
  int   (*FAMNextEvent)(FAMConnection *fc,FAMEvent *fe);
  int   (*FAMPending)(FAMConnection *fc);
  int   (*FAMMonitorDirectory)(FAMConnection *fc,char *file,FAMRequest *fr,void *userData);
  int   (*FAMMonitorFile)(FAMConnection *fc,char *file,FAMRequest *fr,void *userData);
  int   (*FAMCancelMonitor)(FAMConnection *fc,FAMRequest *fr);
};
#endif



#if defined(HAVE_ATTR_COPY_FILE)
#  include <attr/error_context.h>
typedef struct error_context ctx;
#else
typedef void ctx;
#endif



#ifdef HAVE_LIBACL
#  include <sys/acl.h>
#  include <acl/libacl.h>
#  include "acl_tags.h"


struct _libacl {
  void  *handle;
  int   (*acl_set_file)(const char *path_p,acl_type_t type,acl_t acl);
  acl_t (*acl_init)(int cnt);
  acl_t (*acl_get_file)(const char *path_p,acl_type_t type);
  int   (*acl_create_entry)(acl_t *acl,acl_entry_t *entry_p);
  int   (*acl_get_entry)(acl_t acl,int entry_id,acl_entry_t *entry_p);
  int   (*acl_get_tag_type)(acl_entry_t entry_d,acl_tag_t *tag_type_p);
  int   (*acl_get_permset)(acl_entry_t entry_d,acl_permset_t *permset_p);
  int   (*acl_set_permset)(acl_entry_t entry_d,acl_permset_t permset_d);
  int   (*acl_add_perm)(acl_permset_t permset_d,acl_perm_t perm);
  int   (*acl_get_perm)(acl_permset_t permset_d,acl_perm_t perm);
  void *(*acl_get_qualifier)(acl_entry_t entry_d);
  int   (*acl_set_qualifier)(acl_entry_t entry_d,const void *qualifier_p);
  int   (*acl_set_tag_type)(acl_entry_t entry_d,acl_tag_t tag_type);
  int   (*acl_free)(void *obj_p);
  acl_t (*acl_from_mode)(mode_t mode);
  int   (*acl_entries)(acl_t acl);
  int   (*acl_equiv_mode)(acl_t acl,mode_t *mode_p);
  int   (*perm_copy_file)(const char *,const char *,ctx *);
};

extern struct _libacl *libacl;
#endif



#ifdef HAVE_LIBATTR
#  include <attr/xattr.h>
#  ifdef HAVE_ATTR_COPY_FILE
#    include <attr/libattr.h>
#  endif

struct _libattr {
  void *handle;
  ssize_t (*listxattr)(const char *path,char *list,size_t size);
  ssize_t (*getxattr)(const char *path,const char *name,void *value,size_t size);
  int     (*setxattr)(const char *path,const char *name,const void *value,size_t size,int flags);
  int     (*attr_copy_file)(const char *,const char *,int (*)(const char *,ctx *),ctx *);
};

extern struct _libattr *libattr;
#endif



int backend_exists(const char *myfile);

int cp_fs_obj(noir_job *job,int num_files,const char **paths,FsOpt opt);
int mv_fs_obj(noir_job *job,int num_files,const char **paths,FsOpt opt);
int rm_fs_obj(noir_job *job,const char *path,FsOpt opt);
