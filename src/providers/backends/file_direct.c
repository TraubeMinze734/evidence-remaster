/** @file  file_direct.c
 *  @brief direct access to file-system, no abstraction layer.
 *
 * may be compiled with support for libmagic (the "file" command).  if this
 * support was compiled in but the library is not present at run-time, this
 * backend will fall back on a simple filename-extension (".mp3") to MIME-type
 * ("audio/mp3") mapping instead.
 *
 * may be compiled with support for libacl (for Access Control Lists -- the
 * file-system you use must support these; if they are optional, a mount-
 * option ("acl") may also be necessary).  if this support was compiled in and
 * the library is present at run-time, ACLs and default-ACLs can be viewed and
 * modified in evidence.
 *
 * may be compiled with support for libattr (for Extended Attributes -- the
 * file-system you use must support these; if they are optional, a mount-
 * option ("user_xattr") may also be necessary).  if this support was compiled
 * in and the library is present at run-time, EAs can be viewed in evidence.
 *
 * may be compiled with support for F_NOTIFY (on Linux >=2.4) and/or libfam
 * (for FAM, the File Alteration Monitor).  if F_NOTIFY was compiled in and a
 * suitable kernel is deteced at run-time, F_NOTIFY will be used to learn about
 * changes in the file-system.  otherwise, if support for FAM was compiled in
 * and the library is present at run-time and the FAM-daemon speaks with us,
 * FAM will be used.  otherwise, this backend will fall back on polling
 * directories as a last resort.
 *
 * by Azundris
 */

#define _GNU_SOURCE

#include "config.h"

#include <string.h>      /* str...() */
#include <utime.h>       /* utime() */
#include <errno.h>       /* errno */
#include <stdlib.h>      /* malloc */
#include <stdio.h>       /* stderr */
#include <ctype.h>       /* isspace() */
#include <dirent.h>      /* dirent */
#include <sys/stat.h>    /* stat */
#include <sys/types.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>

#include <fcntl.h>

#include <pthread.h>



#ifdef F_NOTIFY
#  include <signal.h>
#endif



#ifdef I_NOTIFY
#  include <sys/inotify.h>
#  include <sys/select.h>
#endif



#include <dlfcn.h>       /* RTLD_NOW */

#include "acl_tags.h"
#include "debug.h"
#include "errors.h"
#include "mime.h"
#include "file.h"        /* MY_NAME, fs_dir */
#include "file_lcl.h"    /* file_if_local() */
#include "monitors.h"    /* file_monitor_do_add() */
#include "setup.h"       /* get_setup_file() */
#include "model.h"       /* file_...(), file_info */
#include "meta.h"        /* meta_...() */
#include "util.h"        /* now(), timeout_add() */

#include "file_direct.h" /* fs_...() */



/* how often should we check for changed files?  default 3000ms */
#define MONITOR_FREQ 1200
/* don't allow further updates while we're busy updating */
pthread_mutex_t mel=PTHREAD_MUTEX_INITIALIZER;   /* monitor event lock */



#define BM_INIT(fq)  (fq)->td=0; (fq)->tt=0
#define BM_START(fq) (fq)->td=now(0)
#define BM_STOP(fq)  (fq)->td=now(0)-(fq)->td; (fq)->tt+=(fq)->td



typedef enum {
  NOIR_FAM_DISABLED,
  NOIR_FAM_OFFLINE,
  NOIR_FAM_ONLINE
} noir_fam_status;



static void           *timer_monitor=NULL;
static void           *ht_mime=NULL,
                      *ht_handler=NULL;

#ifdef HAVE_LIBMAGIC
struct _libmagic      *libmagic=NULL;
magic_t                magic=NULL;
#endif

#ifdef HAVE_LIBSHAREDMIME
struct _libsharedmime *libsharedmime=NULL;
#endif

#ifdef HAVE_FAM
struct _libfam        *libfam=NULL;
static FAMConnection   fc;
#endif


#ifdef HAVE_LIBACL
struct _libacl        *libacl=NULL;
#endif



#ifdef HAVE_LIBATTR
struct _libattr       *libattr=NULL;
#endif

static int             groups=-1;
static gid_t          *gids=NULL;

static noir_fam_status fam_online=NOIR_FAM_OFFLINE;
static int             fnotify_avail=0;



#ifdef HAVE_LIBCURL
struct _libcurl       *libcurl=NULL;
#endif






/*****************************************************************************/
/* helpers */
/***********/



/**
 * get all supplementary groups of this process
 * sets 'groups' (number of groups) and 'gids' (array of group-IDs)
 */
static void groups_init(void) {
  /* init if needed */
  if(groups<0) {
    if((groups=getgroups(0,NULL))>=0) {
      if((gids=malloc(sizeof(gid_t)*(groups+2)))) {
        int c=0,se=0;
        E(1,"found %d supplementary groups...\n",groups);
        groups=getgroups(groups,gids);
        while(c<groups) {
          if(gids[c]==getegid())
            se=1;
          c++; }
        if(!se)
          gids[groups++]=getegid(); }
      else
        groups=-1; }}}



/**
 * check whether we have access to group 'gid'
 * this is used from get_xs() -- if 'gid' is this process' group (or one of
 * its supplementary groups), we benefit from the owning/named group ACE in
 * question
 * @return the group's index in the list of supplementary groups, or -1 for not found
 * @param gid  the ID of the group to look for
 */
static inline int in_group(gid_t gid) {
  int c=0;
  while(c<groups) {
    if(gid==gids[c])
      return c;
    c++; }
  return -1; }



/**
 * determine access-info (deny all, grant some)
 * @return the updated access-info
 * @param mode  POSIX permissions as per stat()
 * @param ret   access-info (must be set to deny-all before first call to calc_mode())
 */
static inline item_flags calc_mode(mode_t mode,item_flags *ret) {
  if(mode&(S_IROTH|S_IXOTH))
    *ret&=~IF_NOUSE;
  if(mode&S_IWOTH)
    *ret&=~IF_NOMODIFY;
  return *ret; }



/**
 * determine access-info from ACLs and/or POSIX permissions
 * @return access-info (read-only, no-access, ...)
 * @param file  the file we're interested in (path, not URL)
 * @param fuid  the file's owner
 * @param fgid  the file's owning group
 * @param mode  the file's mode (as per stat())
 */
static item_flags get_xs(const char *file,uid_t fuid,gid_t fgid,mode_t mode) {
  item_flags ret=IF_NOUSE|IF_NOMODIFY;

  if(groups<0)
    groups_init();

  if(groups>=0) {
#ifdef HAVE_LIBACL
    if(libacl) {
      acl_t       acl=NULL;

      if(!(acl=libacl->acl_get_file(file,ACL_TYPE_ACCESS))) {
        if(!((errno==ENOSYS)||(errno==ENOTSUP))) {
          if(errno!=ENOENT)
            E(0,"(\"%s\"): acl_get_file() failed: #%d -- %s\n",file,errno,strerror(errno));
          return ret; }}
      else {
        acl_entry_t    ent;
        int            rv=0,found=0,grp;
        int            mask=S_IROTH|S_IWOTH|S_IXOTH;
        item_flags     worst=ret,
                       best=~ret;
        mode_t         val,
                       bsf=0; /* best so far */
        acl_tag_t      e_type;
        acl_permset_t  e_perms;
        const id_t    *id_p;

        if((rv=libacl->acl_get_entry(acl,ACL_FIRST_ENTRY,&ent))!=1)
          goto acl_done;

        while(rv>0) {
          libacl->acl_get_tag_type(ent,&e_type);

          if(e_type==ACL_MASK) {
            libacl->acl_get_permset(ent,&e_perms);
            val=0;
            val|=libacl->acl_get_perm(e_perms,ACL_READ)?S_IROTH:0;
            val|=libacl->acl_get_perm(e_perms,ACL_WRITE)?S_IWOTH:0;
            val|=libacl->acl_get_perm(e_perms,ACL_EXECUTE)?S_IXOTH:0;
            mask=val; }
          rv=libacl->acl_get_entry(acl,ACL_NEXT_ENTRY,&ent); }



        rv=libacl->acl_get_entry(acl,ACL_FIRST_ENTRY,&ent);

        while(rv>0) {
          val=0;

          libacl->acl_get_tag_type(ent,&e_type);
          libacl->acl_get_permset(ent,&e_perms);

          val|=libacl->acl_get_perm(e_perms,ACL_READ)?S_IROTH:0;
          val|=libacl->acl_get_perm(e_perms,ACL_WRITE)?S_IWOTH:0;
          val|=libacl->acl_get_perm(e_perms,ACL_EXECUTE)?S_IXOTH:0;
          switch(e_type) {
            case ACL_USER_OBJ:
              if(fuid==geteuid()) {
                calc_mode(val,&ret);
                goto acl_done; }
              break;

            case ACL_USER:
              id_p=libacl->acl_get_qualifier(ent);
              if(id_p&&(*id_p==geteuid())) {
                calc_mode(val&mask,&ret);
E(3,"%s  named user %d => 0%o&0%o => 0%o\n",file,*id_p,val,mask,ret);
              goto acl_done; }
              break;

            case ACL_GROUP_OBJ:
              if((grp=in_group(fgid))>=0) {
                int tmp=ret;
                calc_mode(val&mask,&ret);
E(3,"%s  owning group %d (%d) => 0%o&0%o => 0%o\n",file,fgid,grp,val,mask,ret);
                if((ret&worst)==best)
                  goto acl_done;
                found=1;
                if((ret&worst)!=worst)
                  bsf=val;
                ret=tmp; }
              break;

            case ACL_GROUP:
              id_p=libacl->acl_get_qualifier(ent);
              if(id_p&&((grp=in_group(*id_p))>=0)) {
                int tmp=ret;
                calc_mode(val&mask,&ret);
E(3,"%s  named group %d => 0%o&0%o => 0%o\n",file,*id_p,val,mask,ret);
                if((ret&worst)==best)
                  goto acl_done;
                found=1;
                if((ret&worst)!=worst)
                  bsf=val;
                ret=tmp; }
              break;

            case ACL_OTHER:
              if(found)
                val=bsf;
              calc_mode(val,&ret);
E(3,"%s  other => 0%o => 0%o  => %s (0%o)\n",file,val,ret,found?"best group so far":"other",bsf);
              goto acl_done;
              break; }

          rv=libacl->acl_get_entry(acl,ACL_NEXT_ENTRY,&ent); }

       acl_done:
        libacl->acl_free(acl);
        return ret; }}
#endif

    /* if ACLs are not supported on that FileSystem, we fall back to here */

    /* user */
    if(fuid==geteuid()) {
      if(mode&(S_IRUSR|S_IXUSR))
        ret&=~IF_NOUSE;
      if(mode&S_IWUSR)
        ret&=~IF_NOMODIFY;
        return ret; }

    /* group */
    if(in_group(fgid)>=0) {
      if(mode&(S_IRGRP|S_IXGRP))
        ret&=~IF_NOUSE;
      if(mode&S_IWGRP)
        ret&=~IF_NOMODIFY;
      return ret; }

    /* other */
    calc_mode(mode,&ret);
  }
  else
    E(0,"couldn't get supplementary groups list!?\n");

  return ret; }



static int download(const char *url,const char *lcl) {
#ifdef HAVE_LIBCURL
  FILE     *th; /* real file name here */
  CURL     *cu;
  int       ret=NOIR_SUCC;
  char      em[CURL_ERROR_SIZE];
  CURLcode  cr;

  if(!libcurl) {
    E(0,"download impossible, libcurl not loaded...\n");
    return NOIR_NOTSUPP; }
  if(!(cu=libcurl->cc_curl_easy_init())) {
    E(0,"curl_easy_init() failed for \"%s\", \"%s\"!?\n",url,lcl);
    return NOIR_FAIL; }

  if(!(th=fopen(lcl,"wb"))) {
    if(errno==EISDIR) {
      char *tf,*p;
      if((p=strrchr(url,'/'))&&(tf=file_compose_path(lcl,p+1,NULL))) {
        th=fopen(tf,"wb");
        free(tf);
        if(th)
          goto cont; }}
    ret=NOIR_NOFILE;
    goto done; }
  cont:

  libcurl->cc_curl_easy_setopt(cu,CURLOPT_VERBOSE,    (debug>0)?1:0);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_NOPROGRESS, (debug>=0)?0:1);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_HEADER,     0);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_NOSIGNAL,   1L);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_WRITEDATA,  th);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_ERRORBUFFER,em);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_FAILONERROR,1);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_URL,        url);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_NETRC,      CURL_NETRC_OPTIONAL);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_FOLLOWLOCATION,1);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_UNRESTRICTED_AUTH,0);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_MAXREDIRS,  8);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_REFERER,    url);
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_USERAGENT,  "evidence");
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_COOKIEFILE, "");
  libcurl->cc_curl_easy_setopt(cu,CURLOPT_PRIVATE,    lcl);

  if((cr=libcurl->cc_curl_easy_perform(cu))!=CURLE_OK) {
    E(0,"failed for \"%s\": %d - %s\n",url,cr,em);
    ret=NOIR_FAIL; }

  fclose(th);

 done:
  libcurl->cc_curl_easy_cleanup(cu);
  return ret;
#else
  E(0,"download impossible with this backend, built without support for libcurl...\n");
  return NOIR_NOTSUPP;
#endif
}






/*****************************************************************************/
/* abstraction */
/***************/



/**
 * get item's name (UTF8) from its tag (path, UUID, ...)
 * @return a newly-allocated UTF8-string containing the item's name
 * @param path  the tag/URL/UUID denoting the item
 */
char *backend_tag2name(const char *path) {
  static int  fs_uses_utf8=-1;
  const char *name=file_name(path);

  if(fs_uses_utf8<0) {
    const char *el=elocale();
    fs_uses_utf8=0;
    if(el&&(!strcasecmp(el,"UTF-8")||!strcasecmp(el,"UTF8")))
      fs_uses_utf8=1;
    if(fs_uses_utf8)
      E(1,"system uses UTF8, assuming file-names are also in UTF8...\n");
    else
      E(1,"system uses %s, internally converting to UTF8...\n",el); }

  return fs_uses_utf8?strdup(name):e2utf8(name); }






/*****************************************************************************/
/* file-ops (mv, cp, mkdir, ...) */
/*********************************/



/**
 * check whether an item exists in the file-system
 * @return 0 for not found, the file's mode as per stat() otherwise
 * @param myfile  the item to check for
 */
int backend_exists(const char *myfile) {
  struct stat  st;
  const char  *file;

  if((file=file_if_local(myfile))) {
    if(!stat(file,&st))
      return st.st_mode; }

  return 0; }



/**
 * move item to folder, or rename item
 * @return an error-code
 * @param job  the job
 */
int backend_mv(noir_job *job) {
  const char *fm,*to;
  const char *p[2];
  size_t      lf,lt;

  if(!job)
    return NOIR_FAIL;

  if(!(fm=file_if_local(job->src_path)))
    return job->err=NOIR_NOTSUPP;
  if(!(to=file_if_local(job->dst_path)))
    return job->err=NOIR_NOTSUPP;

  if(((lf=strlen(fm))<(lt=strlen(to)))&&
     (to[lf]==DIR_DELIMITER)&&
     !strncmp(fm,to,lf)) {
    E(0,"can't move folder into itself (\"%s\" => \"%s\")!\n",fm,to);
    return job->err=NOIR_IGNORED; }

  p[0]=fm;
  p[1]=to;

  return job->err=mv_fs_obj(job,2,p,FS_OP_FORCE|FS_OP_RECURSIVE)?NOIR_SUCC:NOIR_FAIL; }



/**
 * copy item to folder, or copy item renaming the copy
 * @return an error-code
 * @param job  the job
 */
int backend_cp(noir_job *job) {
  const char *fm,*to;
  const char *p[2];

  if(!job)
    return NOIR_FAIL;

  if(!(to=file_if_local(job->dst_path)))
    return job->err=NOIR_NOTSUPP;

  if(!(fm=file_if_local(job->src_path))) {
    int ret;
    if((ret=download(job->src_path,to))!=NOIR_SUCC)
      E(0,"download \"%s\" => \"%s\": %d\n",job->src_path,to,ret);
    return ret; }

  E(0,"cp \"%s\" \"%s\"\n",fm?fm:"---",to?to:"---");

  if(!fm||!to)
    return job->err=NOIR_NOTSUPP;

  p[0]=fm;
  p[1]=to;
  return job->err=cp_fs_obj(job,2,p,FS_OP_FORCE|FS_OP_RECURSIVE)?NOIR_SUCC:NOIR_FAIL; }



/**
 * remove an item from the file-system (including any items it may contain)
 * @return an error-code
 * @param job  the job
 */
int backend_rm(noir_job *job) {
  const char *file;

  if(!job)
    return NOIR_FAIL;

  if(!(file=file_if_local(job->src_path)))
    return job->err=NOIR_NOTSUPP;

  return job->err=rm_fs_obj(job,file,FS_OP_FORCE|FS_OP_RECURSIVE); }



/**
 * make directory
 * @return an error-code
 * @param dir    the path to create
 * @param perms  the permissions for the path to be created
 * @param hier   FALSE: try to create last element in path only; TRUE: try to create any missing items along the path
 */
int backend_mkdir(char *dir,int perms,int hier) {
  int ret=0;
  char *p;

  if(!dir||!*dir)
    return NOIR_FAIL;

  p=(dir[0]=='/')?(dir+1):dir;

  if(hier) {
    do {
      if((p=strchr(p,'/')))
        *p='\0';
      if((ret=mkdir(dir,perms))&&(errno==EEXIST)&&S_ISDIR(backend_exists(dir)))
        ret=0;
      if(p)
        *(p++)='/';
    } while(p); }
  else if((ret=mkdir(dir,perms))&&(errno==EEXIST)&&S_ISDIR(backend_exists(dir)))
    ret=0;

  if(ret)
    E(0,"mkdir \"%s\" %o => #%d -- %s\n",dir,perms,errno,strerror(errno));

  return ret?NOIR_FAIL:NOIR_SUCC; }



/**
 * remove an empty directory
 * @return an error-code
 * @param dir  the directory to remove
 */
int backend_rmdir(char *dir) {
  const char *file;

  if(!(file=file_if_local(dir)))
    return NOIR_NOTSUPP;
  if(!(S_ISDIR(backend_exists(file))))
    return NOIR_TYPEMISMATCH;

  return rm_fs_obj(NULL,file,FS_OP_FORCE)?NOIR_SUCC:NOIR_FAIL; }



/**
 * symlink
 * @return an error-code
 * @param xfm  from item...
 * @param xto  ...to item
 */
int backend_symlink(char *xfm,char *xto) {
  const char *fm,*to;
  int         ret;

  if(!(fm=file_if_local(xfm)))
    return NOIR_NOTSUPP;
  if(!(to=file_if_local(xto)))
    return NOIR_NOTSUPP;

  if((ret=symlink(fm,to)))
    E(0,"ln -s \"%s\" \"%s\" => %d-%s\n",fm,to,errno,strerror(errno));

  return ret?NOIR_FAIL:NOIR_SUCC; }



/**
 * hardlink
 * @return an error-code
 * @param xfm  from item...
 * @param xto  ...to item
 */
int backend_hardlink(char *xfm,char *xto) {
  const char *fm,*to;
  int         ret;

  if(!(fm=file_if_local(xfm)))
    return NOIR_NOTSUPP;
  if(!(to=file_if_local(xto)))
    return NOIR_NOTSUPP;

  if((ret=link(fm,to)))
    E(0,"ln \"%s\" \"%s\" => %d-%s\n",fm,to,errno,strerror(errno));

  return ret?NOIR_FAIL:NOIR_SUCC; }



/**
 * where does this symlink point to?
 * @return a newly-allocated tag describing the target of the symlink, or NULL
 * @param myfile  where to find the symlink in the file-system
 */
const char *backend_readlink(const char *myfile) {
  char buf[PATH_MAX+1];

  if((myfile=file_if_local(myfile))) {
    int ret=readlink(myfile,buf,PATH_MAX);
    if(ret>=0) {
      buf[ret]='\0';
      return strdup(buf); }}
  return NULL; }






/*****************************************************************************/
/* JOBS */
/********/


/**
 * worker thread -- async copy, move, delete...
 * @return NULL (reserved)
 * @param data  the job-struct
 */
static void *backend_job_do(void *data) {
  noir_job *job=(noir_job *)data;

  file_job_update(job,NOIR_JOB_START,NOIR_SUCC,NULL,NULL);

/* E(2,"Job type %d, id %ld, \"%s\", \"%s\"\n",job->type,job->id,job->srcs?job->srcs:"",job->dst_path?job->dst_path:""); */

  /* ... do stuff ... */

  switch(job->type) {
    case NOIR_JOB_MV:
      job->err=file_mv_do(job);
      break;
    case NOIR_JOB_CP:
      job->err=file_cp_do(job);
      break;
    case NOIR_JOB_RM:
      job->err=file_rm_do(job);
      break;
    default:
/*    E(1,"unknown job type %d!?  id %ld, \"%s\", \"%s\"\n",job->type,job->id,job->srcs,job->dst_path?job->dst_path:""); */
      break; }

  file_job_update(job,NOIR_JOB_DONE,NOIR_SUCC,NULL,NULL);

  return NULL; }



/**
 * the job is done, clean-up (join thread or whatever)
 * optional function, may not exist in all backends.
 * if it exists, it must check whether the job was actually created by the
 * backend (might be a synch job of the front-end's).
 * @return an error-code
 * @param job
 */
int backend_job_collect(noir_job *job) {
  if(job->id>0)  /* did we create this job, or is it actually sync? */
    pthread_join(job->thread,NULL);
  return NOIR_SUCC; }



/**
 * start a new async job
 * @return a negative error-code, or a positive job-ID
 * @param job  the job to start
 */
int backend_job_start(noir_job *job) {
  static int id=0;

  job->id=++id;

  if(pthread_create(&job->thread,NULL,backend_job_do,job)) {
    E(0,"error creating thread...\n");
    return NOIR_FAIL; }

  return id; }






/*****************************************************************************/
/* MIME */
/********/



/**
 * set default (double-click) action for files of a given MIME-type
 * @return an error-code
 * @param mime     the MIME-type we want to register a handler for (may contain *)
 * @param handler  the handler to register (app-name or template, cf file_run())
 */
int backend_mime_handler_set(char *mime,char *handler) {
  void     *res;
  char     *k,*v;

  if(!ht_handler)
    ht_handler=hash_table_new(free,free);

  if((res=hash_table_fetch(ht_handler,mime)))
    return NOIR_IGNORED;

  if(!(k=strdup(mime)))
    return NOIR_OOM;

  if((v=strchr(k,'*')))
    *v='\0';

  if(!(v=strdup(handler))) {
    free(k);
    return NOIR_OOM; }

  E(2,"adding MIME-handler for %s: %s\n",k,v);

  hash_table_insert(ht_handler,k,v);
  if(!hash_table_fetch(ht_handler,k)) {
    E(0,"adding MIME-handler for %s failed!?\n",k);
    return NOIR_FAIL; }

  return NOIR_SUCC; }



/**
 * get default (double-click) action for files of a given MIME-type
 * @return the handler (app-name or template, cf file_run()), or NULL
 * @param mime   the MIME-type
 * @param match  where to store the MIME-type that was matched (or NULL)
 */
char *backend_mime_handler_get(const char *mime,char **match) {
  char *ret=NULL,*d=NULL,*p;

  if(match)
    *match=NULL;

  if(!mime||!strcmp(mime,MIME_FOLDER)) /* no action, hence no match-type */
    goto bmhg_done;

  if(ht_handler&&mime) {
    if((ret=(char *)hash_table_fetch(ht_handler,mime))) {  /* try all */
      if(match)
        *match=strdup(mime);
      goto bmhg_done; }

    if(!(d=strdup(mime)))
      goto bmhg_done;

    if((p=strchr(d,';'))) {                                /* remove ;... */
      *p='\0';
      if((ret=(char *)hash_table_fetch(ht_handler,d)))
        if(match)
          *match=strdup(d);
        goto bmhg_free; }

    if((p=strchr(d,MIME_DELIMITER))) {
      *(p+1)='\0';
      if((ret=(char *)hash_table_fetch(ht_handler,d)))
        esprintf(match,"%s*",d);
      goto bmhg_free; }

    if((ret=(char *)hash_table_fetch(ht_handler,""))) {
      if(match)
        *match=strdup("*/*");
      goto bmhg_free; }

    E(1,"(\"%s\") => NULL\n",mime);

  bmhg_free:
    free(d); }

 bmhg_done:
  if(ret&&match&&!*match)  /* failure! (OOM?) */
    return NULL;
  return ret; }



/**
 * add element to MIME-hash (ext -> mime-type lookup)
 * @return an error-code
 * @param ext   a filename extension (".mp3")
 * @param mime  the MIME-type it is usually associated with
 */
static int mime_add(char *ext,char *mime) {
  void *res;
  char *k,*v;

  if(!ht_mime)
    ht_mime=hash_table_new(free,free);

  if((res=hash_table_fetch(ht_mime,ext)))
    return NOIR_IGNORED;

  if(!(k=strdup(ext)))
    return NOIR_OOM;
  if(!(v=strdup(mime))) {
    free(k);
    return NOIR_OOM; }

  hash_table_insert(ht_mime,k,v);

  return NOIR_SUCC; }



/**
 * deinitialize ext->mime_type hash
 * @return an error-code
 */
static int mime_exit(void) {
#ifdef HAVE_LIBMAGIC
  if(libmagic) {
    if(magic) {
      libmagic->magic_close(magic);
      magic=NULL; }
    dlclose(libmagic->handle);
    free(libmagic); }
#endif
#ifdef HAVE_LIBSHAREDMIME
  if(libsharedmime) {
    /* this has no exit function. : ( */
    if(libsharedmime->handle)
      dlclose(libsharedmime->handle);
    free(libsharedmime);
    libsharedmime=NULL; }
#endif
  if(ht_mime)
    hash_table_dst(&ht_mime);
  return NOIR_SUCC; }



/**
 * helper for mime_init()
 * @return the smaller non-NULL pointer, or NULL if both pointers are NULL
 * @param a  the first pointer
 * @param b  the second pointer
 */
static inline char *str1st(char *a,char *b) {
  if(!a) return b;
  if(!b) return a;
  return (a<b)?a:b; }



/**
 * initialize ext->mime_type hash from MIME_FILE and load libmagic
 * try to open libmagic (the "file" command) and resolve the symbols we'll need
 * on success, libmagic will point to a filled-in struct _libmagic
 * @return an error-code
 */
static int mime_init(void) {
  FILE *fh;
  char *p,*fn;
  char  l[MIME_MAX_LEN];

  /* create hash for ext -> MIME-type lookup */
  if((fn=get_setup_file(MIME_FILE))) {
    if((fh=fopen(fn,"r"))) {
      while(fgets(l,MIME_MAX_LEN,fh)) {
        p=l;
        while(isspace(*p))
          p++;

        if(*p!='#') {
          char *p2,*p3;
          char save;

          p2=p;
          while(*p2&&!isspace(*p2))
            p2++;

          if(*p2) {
            *(p2++)='\0';  /* now p references the type */
            while(*p2&&*p2!='\n'&&*p2!='\r'&&*p2!='#') {
              while(isspace(*p2))
                p2++;
              p3=p2;
              while(*p3&&*p3!='\n'&&*p3!='\r'&&!isspace(*p3))
                p3++;
              save=*p3;
              *p3='\0';

              mime_add(p2,p);

              *p3=save;
              p2=p3; }}}}
      fclose(fh); }
    free(fn); }
  else
    E(0,"*** CRITICAL *** couldn\'t find \"%s\"!\n",MIME_FILE);



  /* create MIME-handlers */
  if((fn=get_setup_file(MIME_HANDLERS))) {
    if((fh=fopen(fn,"r"))) {
      void *mhv=hash_table_new(free,free);

      if(!mhv)
        E(0,"Couldn't create hash-table for %s!?\n",MIME_HANDLERS);

      while(fgets(l,MIME_MAX_LEN,fh)) {
        p=l;
        while(isspace(*p))   /* ignore leading spaces */
          p++;

        if(*p!='#') {        /* first element not REM */
          char *p2,*p3,*p4;
          int   vardef=0;

          p2=p;
          while(*p2&&(*p2!='=')&&!isspace(*p2))
            p2++;
          while(*p2&&((*p2=='=')||isspace(*p2))) {
            if(*p2=='=')
              vardef=1;
            *(p2++)='\0'; } /* now p references the type */

          if(*p2) {
            p3=str1st(strchr(p2,'\n'),strchr(p2,'\r'));  /* remove trailing garbage */
            if((p4=str1st(strchr(p2,'#'),p3))) {
              p3=p4;
              while(p4>p2) {
                --p4;
                if(isspace(*p4))
                  p3=p4;
                else
                  p4=NULL; }
              while(*p3)
                *(p3++)='\0'; }

            if(vardef) {
              char *k=NULL,*v=NULL;
              if((k=strdup(p))&&(v=strdup(p2))) {
                if(hash_table_insert(mhv,k,v)!=NOIR_SUCC)
                  E(0,"setting %s to %s FAILED!?\n",p,p2);
                else {
                  E(4,"setting %s to %s\n",p,p2);
                  continue; }}
              if(k) free(k);
              if(v) free(v); }

            else {
              if(*p2=='$') {
                p2++;
                if(!(p4=hash_table_fetch(mhv,p2)))
                  E(0,"%s not defined in " MIME_HANDLERS "!? (%s)\n",p2,p);
                p2=p4; }

              if(p2)
                backend_mime_handler_set(p,p2); }}}}

      if(mhv)
        hash_table_dst(&mhv);
      fclose(fh); }
    free(fn); }



#ifdef HAVE_LIBMAGIC
  if((libmagic=malloc(sizeof(struct _libmagic)))) {
    int fail=1,ret;
    magic=NULL;
    memset(libmagic,0,sizeof(struct _libmagic));
    ret=dlmulti("MIME-detection","libmagic.so.1",RTLD_NOW,&libmagic->handle,
                "!magic_open !magic_close !magic_load !magic_file",
                &libmagic->magic_open,&libmagic->magic_close,&libmagic->magic_load,&libmagic->magic_file);
    if(ret!=NOIR_SUCC) {
      if(ret==NOIR_NODATA)
        E(0,"basic MIME-dection only (libmagic.so not found).\n"); }
    else if((magic=libmagic->magic_open(MAGIC_MIME))) {
      if(libmagic->magic_load(magic,NULL)) {
        libmagic->magic_close(magic);
        magic=NULL; }
      else
        fail=0; }
    if(fail&&libmagic) {
      if(libmagic->handle)
        dlclose(libmagic->handle);
      free(libmagic);
      libmagic=NULL; }}
#endif

#ifdef HAVE_LIBSHAREDMIME
  if((libsharedmime=malloc(sizeof(struct _libsharedmime)))) {
    int fail=1,ret;
    memset(libsharedmime,0,sizeof(struct _libsharedmime));

    /* try GPL variant. */
    ret=dlmulti("MIME-detection","libsharedmime1.so",RTLD_NOW,&libsharedmime->handle,
                "!mimetypes_init !mimetypes_get_file_type !mimetypes_get_type_info",
                &libsharedmime->mimetypes_init,&libsharedmime->mimetypes_get_file_type,&libsharedmime->mimetypes_get_type_info);

    /* try LGPL variant. */
    if(ret==NOIR_NOFILE)
      ret=dlmulti("MIME-detection","libsharedmime1d.so",RTLD_NOW,&libsharedmime->handle,
                  "!mimetypes_init !mimetypes_get_file_type !mimetypes_get_type_info",
                  &libsharedmime->mimetypes_init,&libsharedmime->mimetypes_get_file_type,&libsharedmime->mimetypes_get_type_info);

    if(ret!=NOIR_SUCC) {
      if(ret==NOIR_NODATA)
        E(0,"basic MIME-dection only (libsharedmime1d.so not found).\n"); }
    else {
      libsharedmime->mimetypes_init();
      fail=0; }
    if(fail&&libsharedmime) {
      if(libsharedmime->handle)
        dlclose(libsharedmime->handle);
      free(libsharedmime);
      libsharedmime=NULL; }}
#endif

  return NOIR_SUCC; }



/**
 * check whether a file-name reeks of backup
 * @param file  a file-name
 */
static inline char *mime_chk_backup(const char *file) {
  size_t l;
  if((l=strlen(file)-1)>=0) {
    if((file[l]=='~')||(file[l]=='#'))
      return MIME_BACKUP; }
  return NULL; }



#ifdef HAVE_LIBMAGIC

/**
 * figure out a file's MIME-type using libmagic (the "file" command)
 * libmagic support needs to be compiled in, libmagic needs to be present at
 * run-time, its database must have been opened successfully, and the lookup
 * must succeed.  otherwise, MIME_DK_TYPE is returned.
 * @return a MIME-type that must not be free()d
 * @param file  the file to analyze
 */
static inline const char *libmagic_get_mime(const char *file) {
  if(magic) {
    const char *m;
    if((m=libmagic->magic_file(magic,file)))
      return (char *)m; }
  return MIME_DK_TYPE; }
#endif



#ifdef HAVE_LIBSHAREDMIME

/**
 * figure out a file's MIME-type using libsharedmime (the fd.o standard)
 * libsharedmime support needs to be compiled in, libsharedmime needs to be
 * present at run-time, its database must have been opened successfully, and
 * the lookup must succeed.  otherwise, MIME_DK_TYPE is returned.
 * @return a MIME-type that must not be free()d
 * @param file  the file to analyze
 */
static inline const char *libsharedmime_get_mime(const char *file) {
  const char *m=NULL;
  if(libsharedmime&&(m=libsharedmime->mimetypes_get_file_type(file,MIMETYPES_CHECK_ALL)))
    return m;
  return MIME_DK_TYPE; }
#endif



/**
 * lookup MIME-type by magic, using whatever lib we can get our hands on
 * @return a MIME-type that must not be free()d
 * @param file  a file-name
 */
static const char *mime_get_by_magic(const char *file) {
  const char *mime=NULL;
#if defined(HAVE_LIBMAGIC)
  mime=libmagic_get_mime(file);
#endif
#if defined(HAVE_LIBSHAREDMIME)
  if(!mime) {
    if((mime=libsharedmime_get_mime(file))&&!strcmp(mime,"application/x-not-recognised"))
      mime=NULL; }
#endif
  return mime?mime:MIME_DK_TYPE; }



/**
 * lookup MIME-type for this extension (ext -> MIME-type)
 * @return a MIME-type that must not be free()d
 * @param file  a file-name
 */
static const char *mime_get_by_ext(const char *file) {
  char *ext,*ret=NULL;

  if(file&&*file&&!(ret=mime_chk_backup(file))) {
    if((file=file_if_local(file))) {
      char *p=strrchr(file,DIR_DELIMITER);
      if(ht_mime&&(ext=strrchr(file,'.'))&&(!p||(p<ext))) {
	ret=(char *)hash_table_fetch(ht_mime,&ext[1]); }}}
  return ret; }



/**
 * check whether a file is a binary, a shell-script, or something other
 * @return an error-code
 * @param file  the file to look at
 * @param m     where to store the MIME-type we worked out
 */
static inline int mime_chk_exec(const char *file,const char **m) {
#define MCEBL 2
  int         fd;
  char        buf[MCEBL];
  int         ret=NOIR_FAIL;
  const char *mime=MIME_EXECUTABLE;

  if(!m)
    return NOIR_FAIL;
  if(!file) {
    *m=NULL;
    return NOIR_NOFILE; }

  if((mime=mime_chk_backup(file)))   /* backup? */
    ret=NOIR_SUCC;
  else if((fd=open(file,0))>=0) {    /* shell-script? */
    off_t size=MCEBL;
    if(read(fd,buf,size)==size) {
      if((buf[0]=='#')&&(buf[1]=='!')) {
        ret=NOIR_SUCC;
        mime=MIME_SCRIPT; }}
    else
      E(2,"couldn\'t read \"%s\"...\n",file);
    close(fd); }
  else
    E(2,"couldn't open \"%s\"...\n",file);

#ifdef FS_SETS_X_ON_EVERYTHING
  if(ret!=NOIR_SUCC) {
    if(!(mime=mime_get_by_ext(file)))
      mime=mime_get_by_magic(file);
    if(!mime||!strcmp(mime,MIME_DK_TYPE)) {
      mime=MIME_EXECUTABLE;
      ret=NOIR_PARTIAL; }
    else
      ret=NOIR_SUCC; }
#endif

  *m=mime;

  return ret; }



/**
 * figure out a file's MIME-type
 * @return a MIME-type that must not be free()d
 * @param myfile  the file to analyze
 */
const char *backend_get_mime(const char *myfile) {
  const char *file,*mime=NULL;
  int         m;

  if((file=file_if_local(myfile))) {
    mime=MIME_DK_TYPE;
    m=backend_exists(file);
    if(S_ISREG(m)) {
      if((m&S_IXUSR)||(m&S_IXGRP)||(m&S_IXOTH))
        mime_chk_exec(file,&mime);
      else if(!(mime=mime_get_by_ext(file)))
        mime=MIME_DK_TYPE; }
    else if(S_ISDIR(m))
      mime=MIME_FOLDER;
    else if(S_ISCHR(m))
      mime=MIME_CHAR;
    else if(S_ISBLK(m))
      mime=MIME_BLK;
    else if(S_ISFIFO(m))
      mime=MIME_FIFO;
    else if(S_ISSOCK(m))
      mime=MIME_SOCKET;

    if(!strcmp(mime,MIME_DK_TYPE))
      mime=mime_get_by_magic(file); }

  return mime; }






/*****************************************************************************/
/* FNotify (optional) */
/**********************/



/* inotify via the modern sys/inotify.h API (kernel >= 2.6.13).  the
   original code used the 2003 pre-mainline interface (/dev/inotify,
   ioctl(2), struct inotify_watch_request), which never existed in any
   released kernel. */
#ifdef I_NOTIFY
static const char *mon_inotify="I_NOTIFY";
static int inotify_fd=-1;

static int inotify_init_all(void) {
  if(inotify_fd>=0)
    return NOIR_SUCC;
  if((inotify_fd=inotify_init1(IN_NONBLOCK))<0) {
    E(1,"inotify not available, disabled. (%s)\n",strerror(errno));
    return NOIR_NOTSUPP; }
  return NOIR_SUCC; }

static int inotify_exit(void) {
  if(inotify_fd<0)
    return NOIR_IGNORED;
  if(close(inotify_fd)<0) {
    E(0,"couldn't close inotify fd: %s\n",strerror(errno));
    return NOIR_FAIL; }
  inotify_fd=-1;
  return NOIR_SUCC; }

/**
 * add a monitor -- tell us if the directory changes
 * @return the watch-descriptor, or a negative error-code
 * @param dir  the directory to watch
 */
static int inotify_add(char *dir) {
  int wd;

  if(inotify_fd<0) {
    E(0,"was called when there is no inotify_fd!?\n");
    return NOIR_IGNORED; }

  if((wd=inotify_add_watch(inotify_fd,dir,
                           IN_MODIFY|IN_CLOSE_WRITE|IN_MOVED_FROM|IN_MOVED_TO|
                           IN_CREATE|IN_DELETE|IN_DELETE_SELF|IN_ATTRIB))<0) {
    E(0,"inotify_add_watch(\"%s\"): %s\n",dir,strerror(errno));
    return NOIR_FAIL; }
  return wd; }

/**
 * remove a monitor -- we're no longer interested in that directory
 * @return an error-code
 * @param wd  the watch-descriptor to remove
 */
static int inotify_remove(int wd) {
  if(inotify_fd<0) {
    E(0,"was called when there is no inotify_fd!?\n");
    return NOIR_IGNORED; }
  if(inotify_rm_watch(inotify_fd,wd)<0) {
    E(0,"inotify_rm_watch(%d): %s\n",wd,strerror(errno));
    return NOIR_FAIL; }
  return NOIR_SUCC; }

static int inotify_check(void) {
  char              buf[8192];
  ssize_t           got;
  int               cnt=0;

  if(inotify_fd<0)
    return NOIR_IGNORED;

  while((got=read(inotify_fd,buf,sizeof(buf)))>0) {
    char *p=buf;
    while(p<buf+got) {
      struct inotify_event *event=(struct inotify_event *)p;
      noir_monitor *m=file_monitor_get_1st();
      p+=sizeof(struct inotify_event)+event->len;

      if(!(event->mask&(IN_MODIFY|IN_CLOSE_WRITE|
                        IN_MOVED_FROM|IN_MOVED_TO|IN_ATTRIB|
                        IN_CREATE|IN_DELETE|IN_DELETE_SELF)))
        continue;

      while(m) {
        if((m->system==mon_inotify)&&(event->wd==(int)m->handle)) {
          noir_file_event e=NOIR_MONITOR_DK;

          if(event->mask&(IN_MODIFY|IN_CLOSE_WRITE))
            e=NOIR_MONITOR_CHG;
          else if(event->mask&(IN_CREATE|IN_MOVED_TO))
            e=NOIR_MONITOR_NEW;
          else if(event->mask&(IN_DELETE|IN_MOVED_FROM|IN_DELETE_SELF))
            e=NOIR_MONITOR_DEL;
          else if(event->mask&IN_ATTRIB)
            e=NOIR_MONITOR_META;

          if(e==NOIR_MONITOR_DK)
            E(0,"INOTIFY event $%x for \"%s\" ignored...\n",(int)event->mask,event->name);
          else {
            E(0,"INOTIFY event $%x for \"%s\"...\n",(int)e,event->name);
            file_monitor_event(m,e,event->name); }
          cnt++;
          break; }   /* one monitor per wd */
        m=m->next; } } }

  if(got<0&&errno!=EAGAIN)
    E(0,"inotify read(): %s\n",strerror(errno));

  return cnt?NOIR_SUCC:NOIR_IGNORED; }
#endif






/*****************************************************************************/
/* FNotify (optional) */
/**********************/



#ifdef F_NOTIFY

static const char *mon_fnotify="F_NOTIFY";



/**
 * signal-handler for F_NOTIFY
 * @param sig   signal number
 * @param si    siginfo struct
 * @param data  ucontext_t*
 */
static void fnotify_handler(int sig,siginfo_t *si,void *data) {
  noir_monitor *e=file_monitor_get_1st();
  int           fnotify_fd=si->si_fd;
  static int    sigio_warn=0;

  while(e) {
    if(fnotify_fd==((int)e->handle)) {
      e->data=(void *)(((int)e->data)+1);
      if((sig==SIGIO)&&!(sigio_warn&1)) {
        sigio_warn|=1;
        E(0,"overflow (SIGIO) for \"%s\"...\n",e->want); }
      return; }
    e=e->next; }

  if(sig==SIGIO) {
    if(!(sigio_warn&2)) {
      sigio_warn|=2;
      E(0,"overflow (SIGIO) for F_NOTIFY...  not good.\n\twill fall back on polling; there will be a nominal performance penalty.\n");
      fam_online=NOIR_FAM_DISABLED;
      fnotify_avail=0; }}
  else
    E(0,"F_NOTIFY for unknown fd %d!?\n",fnotify_fd); }



/**
 * remove a monitor -- we're no long interested in that file/directory
 * @return an error-code
 * @param dir  the directory to stop watching
 */
static int fnotify_remove(int fd) {
  return (close(fd))?NOIR_FAIL:NOIR_SUCC; }



/**
 * add a monitor -- tell us if the file/directory changes
 * @return an error-code
 * @param dir  the directory to watch
 */
static int fnotify_add(char *dir) {
  int ret=NOIR_FAIL,fd;

  if((fd=open(dir,O_RDONLY))<0)
    ret=NOIR_NOTFOUND;
  else {
    if(!fcntl(fd,F_SETSIG,SIGRTMIN)) {
      if(!fcntl(fd,F_NOTIFY,
                (DN_MODIFY|DN_CREATE|DN_DELETE|DN_RENAME|DN_ATTRIB)|
                 DN_MULTISHOT)) {
        E(4,"added \"%s\" as %d\n",dir,fd);
        return fd; }
      else if(errno==EINVAL) /* compiled with F_NOTIFY, but run without */
        ret=NOIR_NOTSUPP; }
    else
      ret=NOIR_FAIL;
    close(fd); }
  return ret; }



/**
 * initialize FNotify subsystem
 * @return an error-code
 */
static int fnotify_init(void) {
  static struct sigaction act;
  int                     fd;

  act.sa_sigaction=fnotify_handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags=SA_SIGINFO;
  if(sigaction(SIGRTMIN,&act,NULL))
    E(0,"sigaction(RT) failed!?\n");

  act.sa_sigaction=fnotify_handler;
  sigemptyset(&act.sa_mask);
  if(sigaction(SIGIO,&act,NULL))
    E(0,"sigaction(IO) failed!?\n");

  if((fd=fnotify_add("/"))==NOIR_NOTSUPP) {
    E(1,"F_NOTIFY not supported by OS, disabled.\n");
    return NOIR_NOTSUPP; }
  else if(fd<0) {
    E(0,"fnotify_init() failed (#%d), trying other methods...\n",fd);
    return NOIR_FAIL; }
  else {
    /* symlink("/tmp/test","/tmp/test"); */
    fnotify_avail=1;
    E(1,"F_NOTIFY support enabled.\n");
    close(fd); }

  return NOIR_SUCC; }



/**
 * deinitialize fnotify subsystem
 * @return an error-code
 */
static int fnotify_exit(void) {
  return NOIR_SUCC; }
#endif






/*****************************************************************************/
/* FAM (optional) */
/******************/



#ifdef HAVE_FAM

static const char *mon_fam="FAM";



/**
 * connect to File Alteration Monitor daemon
 * @return an error-code
 */
static inline int fam_connect(void) {
  static int noted=0;
  int        ret=NOIR_IGNORED;

  if(!libfam)
    return NOIR_FAIL;

  if(fam_online==NOIR_FAM_OFFLINE) {
    if((ret=libfam->FAMOpen(&fc)?NOIR_FAIL:NOIR_SUCC)==NOIR_SUCC)
      fam_online=NOIR_FAM_ONLINE;
    else if(!noted) {
      noted++;
/*    fam_online=NOIR_FAM_DISABLED; */
      E(fnotify_avail,"could not open FAM!\n"); }}
  return ret; }



/**
 * try to open libfam (File Alteration Monitor) and resolve the symbols we'll need
 * on success, libfam will point to a filled-in struct _libfam
 * @return an error-code
 */
static int libfam_init(void) {
  int ret=NOIR_SUCC;

  if(!(libfam=malloc(sizeof(struct _libfam))))
    return NOIR_OOM;
  memset(libfam,0,sizeof(struct _libfam));

  ret=dlmulti("File Alteration Monitor","libfam.so.0",RTLD_NOW,&libfam->handle,
              "!FAMOpen !FAMClose !FAMNextEvent !FAMPending "
              "!FAMMonitorDirectory !FAMMonitorFile !FAMCancelMonitor",
              &libfam->FAMOpen,&libfam->FAMClose,&libfam->FAMNextEvent,&libfam->FAMPending,
              &libfam->FAMMonitorDirectory,&libfam->FAMMonitorFile,&libfam->FAMCancelMonitor);
  if(ret==NOIR_SUCC)
    ret=fam_connect();
  else {
    if(ret==NOIR_NODATA)
      E(0,"File Alteration Monitor not available, must poll (libfam.so not found).\n");
    free(libfam);
    libfam=NULL; }
  return ret; }



/**
 * deinitialize libfam
 * @return an error-code
 */
static int libfam_exit(void) {
  if(!libfam)
    return NOIR_IGNORED;
  libfam->FAMClose(&fc)?NOIR_FAIL:NOIR_SUCC;
  dlclose(libfam->handle);
  free(libfam);
  libfam=NULL;
  return NOIR_SUCC; }



/**
 * poll File Alteration Monitor daemon
 * @return an error-code
 */
static int fam_listen(void) {
  FAMEvent      e;
  noir_monitor *m;
  int           ret=NOIR_IGNORED;
  static char  *en[]={ "???", "CHG", "DEL", "EXE", "exe", "NEW", "MOV", "ACK", "EXI", "FIN" };
  static int    map[]={ NOIR_MONITOR_DK, NOIR_MONITOR_CHG, NOIR_MONITOR_DEL,
                        NOIR_MONITOR_EXECSTART, NOIR_MONITOR_EXECSTOP,
                        NOIR_MONITOR_NEW, NOIR_MONITOR_DK, NOIR_MONITOR_DK,
                        NOIR_MONITOR_DK, NOIR_MONITOR_DK };

  memset(&e,0,sizeof(FAMEvent));

  while(libfam->FAMPending(&fc)) {   /* while there R FAM-events to process */
    if(libfam->FAMNextEvent(&fc,&e)<0) {
      ret=NOIR_PARTIAL;              /* if we couldn't get the event, some- */
      return fam_connect(); }        /* thing broke; try to reconnect to FAM */

    if(!e.filename||!e.filename[0])  /* did we actually get a useful record? */
      continue;

    if((m=(noir_monitor *)e.userdata)) {  /* get ev mon struct from event */
      int t=map[e.code];
      if(t!=NOIR_MONITOR_DK) {       /* is the event-type supported by ev? */
        char *f=NULL,*n=e.filename;

        if((m->type==NOIR_MONITOR_DIR)&&!strchr(n,DIR_DELIMITER)) {
          esprintf(&f,"%s/%s",m->have,n);
          if(!f)
            return NOIR_OOM;
          n=f; }

        E(2,"Handling FAM event type %s %d->%d for \"%s\" (\"%s\")...\n",
          en[e.code],e.code,t,n,m->want);

        file_monitor_event(m,t,n);
        if(f)
          free(f); }

      else
        E(3,"Ignoring FAM event type %s %d->%d for \"%s\"...\n",
          en[e.code],e.code,t,e.filename);

      ret=NOIR_SUCC; }}

  return ret; }
#endif






/*****************************************************************************/
/* poll (last resort) */
/**********************/



static const char *mon_poll="poll";



/**
 * poll directory -- last resort when neither FAM nor FNotify are available.
 */
static void backend_monitor_poll(void) {
  noir_monitor *e=file_monitor_get_1st();
  struct stat   st;

  while(e) {
    E(5,"checking \"%s\" (for \"%s\")...\n",e->have,e->want);
    if(!stat(e->have,&st)) {
      time_t ft=NOIR_MAX(st.st_mtime,st.st_ctime);
      if(ft>e->ts) {  /* changed? */
        e->ts=ft;     /* remember chg-time */
        /* without FAM, we cannot easily determine deletes, so we re-read
           the whole dir for now */
        if(S_ISDIR(st.st_mode))
          e->data=(void *)(((int)e->data)+1);  /* put on Q */
        else
          file_monitor_event(e,NOIR_MONITOR_CHG,e->have); }}
    else  /* can't stat, must've been deleted */
      file_monitor_event(e,NOIR_MONITOR_DEL,e->have);
    e=e->next; }}






/*****************************************************************************/
/* monitors */
/************/



/**
 * check monitors (using FNotify, FAM, or, if all else fails, polling)
 * @return TRUE for go on checking
 * @param data  reserved
 */
static int backend_monitor_check(void *data) {
  noir_monitor *e;
  time_t        tn,ts;
  file_info    *fi;
  int           n;

  if(pthread_mutex_trylock(&mel)) {
    E(3,"events deferred, still busy handling the last one...\n");
    return TRUE; }

  /* check queue. it will contain any F_NOTIFY-events, and any deferred
     dir-events (reload-requests coming in while we don't even have the first
     one loaded) from the other methods. */
  tn=time(NULL);
  for(e=file_monitor_get_1st();e;e=e->next) {
    if(e->data) {
      fi=file_info_fetch(NULL,e->have);
      n=file_info_get_num_children(fi);
      ts=e->ts2+((n>0)?(n/50):0);   /* throttle */

      if(tn<ts)
        E(2,"throttle for \"%s\"!\n",e->have);
      else if(fi&&(file_info_get_infobits(fi)&FF_STAT_)) {
        E(2,"dir-event for \"%s\"! (%p notifications)\n",e->have,e->data);
        e->data=NULL;
        file_monitor_event(e,NOIR_MONITOR_RLD,e->have); }
      else
        E(3,"reload deferred for \"%s\", still loading the previous one...\n",e->have); }}

#ifdef I_NOTIFY
  if(inotify_fd>=0) {
    inotify_check();
    goto done; }
#endif

#ifdef F_NOTIFY
  /* if in F_NOTIFY mode, the above checking of the Q is all we need to do, so the
     the byte stops here. */
  if(fnotify_avail)
    goto done;
#endif

#ifdef HAVE_FAM
  switch(fam_online) {
    case NOIR_FAM_OFFLINE:
      if(fam_connect()==NOIR_SUCC)
        goto done;
      break;
    case NOIR_FAM_ONLINE:
      fam_listen();
      goto done;
    case NOIR_FAM_DISABLED:
      break; }
#endif

  backend_monitor_poll();

 done:
  pthread_mutex_unlock(&mel);
  return TRUE; }



/**
 * add a monitor -- tell us if the file/directory changes
 * @return an error-code
 * @param m  the monitor to add (prepared in file_monitor_add())
 */
int backend_monitor_add(noir_monitor *m) {
  const char *myfile=file_if_local(m->have);

  if(!myfile) {
    E(0,"was asked to register monitor for remote file \"%s\"!?\n",m->have);
    return NOIR_NOTSUPP; }

  if(m->system!=NULL) {
    E(0,"was asked to register monitor that was also registered with sub-system \"%s\"!?\n",m->system);
    return NOIR_IGNORED; }

#ifdef I_NOTIFY
  if(inotify_fd>=0) {
    int fd=inotify_add(m->have);
    if(fd>=0) {
      m->system=mon_inotify;
      m->handle=(void *)fd;
      return NOIR_SUCC; }
    else if(fd!=NOIR_NOTSUPP) { /* not supported by kernel, try next method */
      return fd; }}
#endif

#ifdef F_NOTIFY
  if(fnotify_avail) {
    int fd=fnotify_add(m->have);
    if(fd>=0) {
      m->system=mon_fnotify;
      m->handle=(void *)fd;
      return NOIR_SUCC; }
    else if(fd!=NOIR_NOTSUPP) { /* not supported by kernel, try next method */
      return fd; }}
#endif

#ifdef HAVE_FAM
  { int         ret=NOIR_FAIL;
    FAMRequest *fr;

    if(fam_connect()==NOIR_FAIL)
      goto poll;

    if(!(fr=malloc(sizeof(FAMRequest))))
      return NOIR_OOM;

    if(m->type==NOIR_MONITOR_DIR)
      ret=libfam->FAMMonitorDirectory(&fc,(char *)myfile,fr,m);
    else
      ret=libfam->FAMMonitorFile(&fc,(char *)myfile,fr,m);

    m->handle=(void *)fr;
    m->system=mon_fam;

    return ret?NOIR_FAIL:NOIR_SUCC; }
#endif

 poll:
  m->system=mon_poll;
  return NOIR_SUCC; }



/**
 * remove a monitor -- we're no long interested in that file/directory
 * @return an error-code
 * @param m  the monitor to add (prepared in file_monitor_add())
 */
int backend_monitor_remove(noir_monitor *m) {
#ifdef I_NOTIFY
  if(m->system==mon_inotify) {
    m->system=NULL;
    return inotify_remove((int)m->handle); }
#endif

#ifdef F_NOTIFY
  if(m->system==mon_fnotify) {
    m->system=NULL;
    return fnotify_remove((int)m->handle); }
#endif

#ifdef HAVE_FAM
  if(m->system==mon_fam) {
    int         ret=NOIR_FAIL;
    FAMRequest *fr;

    if(fam_connect()==NOIR_FAIL)
      goto fail;

    if((fr=(FAMRequest *)m->handle)) {
      ret=libfam->FAMCancelMonitor(&fc,fr)?NOIR_FAIL:NOIR_SUCC;
      free(fr); }
    else
      ret=NOIR_NODATA;
    m->system=NULL;
   fail:
    return ret; }
#endif

  if(m->system==mon_poll) {
    m->system=NULL;
    return NOIR_SUCC; }

  if(m->system==NULL) {
    E(1,"was asked to unregister inactive monitor for \"%s\"!?\n",m->have);
    return NOIR_IGNORED; }

  E(0,"was asked to unregister monitor for \"%s\" for unknown sub-system \"%s\"!?\n",m->have,m->system);
  return NOIR_NOTSUPP; }






/*****************************************************************************/
/* activation */
/**************/



/**
 * start a file (double-click etc.)
 * if there is a default action for files of this MIME-type, use that.
 * otherwise, if file is executable, start the file itself.
 * @return an error-code
 * @param myfile  a local file
 * @param mime    the file's MIME-type, or NULL for auto-determine
 */
int backend_activate(char *myfile,const char *mime) {
  const char *file;
  char       *mh=NULL,*mm;
  int         st;

  if(!(file=file_if_local(myfile)))
    return NOIR_NOTSUPP;
  else if(!(st=backend_exists(file)))
    return NOIR_NOFILE;

  if(!mime) {
    /* don't go directly to backend_get_mime() so we can have global
       MIME-type translations/alii */
    mime=file_get_mime(file);
    E(0,"MIME type was not given, is \"%s\"...\n",mime?mime:"unknown"); }

  if(mime&&(mh=backend_mime_handler_get(mime,&mm))) {  /* do we have handler? */
    /* need exact match to override execution of executables -- as in,
       this is pretty unlikely, only do it if the user absolutely insists. */
    E(2,"handler for MIME-type \"%s\": \"%s\" (for \"%s\")\n",mime?mime:"",mh?mh:"",mm?mm:"");
    if(!strcmp(mime,mm)||                 /* if it's an exact match */
       (strcmp(mime,MIME_EXECUTABLE)&&    /* or it's not a script or binary */
        strcmp(mime,MIME_SCRIPT))) {
      free(mm);
      return file_run(NULL,mh,file,mime); } /* run it w/ the handler now */
    free(mm); }
  else
    E(2,"no handler for MIME-type \"%s\"...\n",mime?mime:"");

  if((st&S_IFREG)&&(st&(S_IXUSR|S_IXGRP|S_IXOTH)))  /* if it's an executable */
    return file_run(NULL,file,NULL,mime?mime:MIME_EXECUTABLE); /* run it */

  E(0,"(\"%s\",\"%s\") => cannot start\n",file,mime?mime:"???");

  return NOIR_FAIL; }






/*****************************************************************************/
/* meta-data */
/*************/

#ifdef HAVE_LIBACL

/**
 * set ACL or default-ACL on file from meta-data using libACL
 * @return an error-code
 * @param filename  the file
 * @param md        the list of meta-data
 * @param r         first meta-data item in ACL to set (ACL or default-ACL)
 */
int backend_set_acl(char *filename,noir_md *md,noir_meta *r) {
  acl_t          acl;
  acl_entry_t    entry;
  noir_meta     *a,*f=NULL;
  int            ret=NOIR_SUCC,cnt=0;
  acl_type_t     type;
  id_t           id=1;
  char          *p,*q;
  acl_permset_t  permset;
  int            pr,pw,px;

  if(!strcmp(meta_get_group(r),ACL_TAG_DFLT))
    type=ACL_TYPE_DEFAULT;
  else if(!strcmp(meta_get_group(r),ACL_TAG_XS))
    type=ACL_TYPE_ACCESS;
  else
    return NOIR_FAIL;

  for(a=meta_get_1st(md);a;a=meta_get_next(a)) {
    if(!strcmp(meta_get_group(a),meta_get_group(r))&&
       (meta_get_type(a)==META_ACL)&&(meta_get_flags(a)!=META_EF)) {
      if(!f)
        f=a;
      cnt++; }}

  if(!(acl=libacl->acl_init(cnt))) {
    E(0,"(\"%s\"): could not create ACL (#%d)\n",filename,errno);
    return NOIR_OOM; }

  E(1,"ACL(%d) {\n",cnt);
  a=f;
  while(a) {
    const char *key=meta_get_key(a);

    if(!(q=strdup((char *)meta_get_val(a)))) {
      ret=NOIR_OOM;
      goto done; }
    if(!(p=strchr(q,':'))) {
      ret=NOIR_TYPEMISMATCH;
      free(q);
      goto done; }

    *(p++)='\0';

    E(1,"  %s: %s:%s %s\n",meta_get_group(a),meta_get_key(a),q,p);
    meta_set_flags(a,META_RW);   /* avoid processing twice */

    pr=(p[0]=='r')?ACL_READ:0;
    pw=(p[1]=='w')?ACL_WRITE:0;
    px=(p[2]=='x')?ACL_EXECUTE:0;

    libacl->acl_create_entry(&acl,&entry);
    libacl->acl_get_permset(entry,&permset);
    libacl->acl_add_perm(permset,pr|pw|px);
    libacl->acl_set_permset(entry,permset);

    if(!strcasecmp(ACL_TAG_OWNER,key))
      libacl->acl_set_tag_type(entry,ACL_USER_OBJ);

    else if(!strcasecmp(ACL_TAG_NUSER,key)) {
      const id_t    *id_p;
      struct passwd *us=getpwnam(q);
      if(!us) {
        E(0,"(\"%s\"): unknown group \"%s\"!?\n",filename,q);
        ret=NOIR_NOTFOUND;
        goto free_qualifier; }
      libacl->acl_set_tag_type(entry,ACL_USER);
      id=us->pw_uid;
      if(libacl->acl_set_qualifier(entry,&id))
        E(0,"acl_set_qualifier() failed: #%d\n",errno);
      id_p=libacl->acl_get_qualifier(entry); }

    else if(!strcasecmp(ACL_TAG_NGROUP,key)) {
      const id_t   *id_p;
      struct group *gr=getgrnam(q);
      if(!gr) {
        E(0,"(\"%s\"): unknown group \"%s\"!?\n",filename,q);
        ret=NOIR_NOTFOUND;
        goto free_qualifier; }
      libacl->acl_set_tag_type(entry,ACL_GROUP);
      id=gr->gr_gid;
      if(libacl->acl_set_qualifier(entry,&id))
        E(0,"acl_set_qualifier() failed: #%d\n",errno);
      id_p=libacl->acl_get_qualifier(entry); }

    else if(!strcasecmp(ACL_TAG_MASK,key))
      libacl->acl_set_tag_type(entry,ACL_MASK);
    else if(!strcasecmp(ACL_TAG_GROUP,key))
      libacl->acl_set_tag_type(entry,ACL_GROUP_OBJ);
    else if(!strcasecmp(ACL_TAG_OTHER,key))
      libacl->acl_set_tag_type(entry,ACL_OTHER);
    else
      E(0,"unknown type \"%s\" ignored!?\n",key);

  free_qualifier:
    free(q);

    /* find next ACE in meta-data */
    do {
      a=meta_get_next(a);
    } while(a&&(strcmp(meta_get_group(a),meta_get_group(r))||
                (meta_get_type(a)!=META_ACL)||(meta_get_flags(a)==META_EF))); }
  E(1,"} ACL\n");

  if(libacl->acl_set_file(filename,type,acl)) {
    E(0,"could not set ACL for \"%s\": #%d - %s\n",filename,errno,strerror(errno));
    ret=NOIR_FAIL; }

 done:
  libacl->acl_free(acl);
  return ret; }
#endif



/**
 * set group of meta-data on file
 * @return an error-code
 * @param ns        the session
 * @param filename  the file
 * @param md        the list of meta-data
 * @param r         first meta-data item in group to set
 */
int backend_set_meta(void *ns,char *filename,noir_md *md,noir_meta *r) {
  E(0,"(\"%s\": %s)\n",filename,meta_get_key(r)?meta_get_key(r):"???");

#ifdef HAVE_LIBACL
  if(!strcmp(meta_get_group(r),ACL_TAG_DFLT)||
     !strcmp(meta_get_group(r),ACL_TAG_XS))
    backend_set_acl(filename,md,r);
  else
#endif

#ifdef HAVE_LIBATTR
  if(!strcmp(meta_get_group(r),EA_NAME)) {
    const char *name=meta_get_key(r),*val=meta_get_val(r);

    if(meta_get_flags(r)==META_DO)
      meta_set_flags(r,META_RW);

    if(!libattr)
      E(0,"libattr's gone away!?\n");
    else if(!name||!*name)
      E(0,"EA to set on \"%s\" has no name!?\n",filename);
    else if(libattr->setxattr(filename,name,(void *)val,val?strlen(val):0,0)) {
      E(0,"could not set EA \"%s\" on file \"%s\": error %d (\"%s\")\n",
          name,filename,errno,strerror(errno));
      switch(errno) {
        case ENOSPC:  return NOIR_OOM;     break;
        case EDQUOT:  return NOIR_OOM;     break;
        case ENOTSUP: return NOIR_NOTSUPP; break;
        default:      return NOIR_FAIL;    break; }}
    else {
      E(2,"updated EA \"%s\" on file \"%s\"\n",name,filename);
      return NOIR_SUCC; }}
  else
#endif

  /* user/group/other: chmod/chown */
  if(!strcasecmp(meta_get_key(r),"user")||
     !strcasecmp(meta_get_key(r),"group")||
     !strcasecmp(meta_get_key(r),"other")) {

    char      *mg=strdup(meta_get_group(r)),*usr=NULL,*grp=NULL;
    int        p=0; /* ,c=file_exists(filename); */
    noir_meta *u=meta_in_group(md,"User",mg),
              *g=meta_in_group(md,"Group",mg),
              *o=meta_in_group(md,"Other",mg);

    if(u) {
      p|=file_perms_parse(meta_get_val(u),NOIR_PERMSET_USER,&usr);
      meta_del(md,"user",mg); }
    else
      E(0,"no \"user\" entry?\n");

    if(g) {
      p|=file_perms_parse(meta_get_val(g),NOIR_PERMSET_GROUP,&grp);
      meta_del(md,"group",mg); }
    else
      E(0,"no \"group\" entry?\n");

    if(o) {
      p|=file_perms_parse(meta_get_val(o),NOIR_PERMSET_OTHER,NULL);
      meta_del(md,"other",mg); }
    else
      E(0,"no \"other\" entry?\n");

    if(mg)
      free(mg);

    E(1,"chmod 0%o \"%s\"\n",p,filename?filename:"???");
    if(chmod(filename,p)) {
      E(0,"Could not adjust permissions of \"%s\" to %o: %s\n",filename,p,strerror(errno));
      file_job_ask(file_job_simple(ns,NOIR_JOB_DIAL),"Oy","Could not adjust permissions of \"%s\" to %o: %s",filename,p,strerror(errno)); }

    if(usr||grp) {
      id_t           uid=-1,gid=-1;
      struct passwd *us;
      struct group  *gr;

      if(usr) {
        if(!(us=getpwnam(usr)))
          E(0,"(\"%s\"): unknown group \"%s\"!?\n",filename,usr);
        else
          uid=us->pw_uid;
        free(usr); }

      if(grp) {
        char *q=strchr(grp,' ');
        if(q) /* group entry may contain list of members which we don't need */
          *q='\0';
        if(!(gr=getgrnam(grp)))
          E(0,"(\"%s\"): unknown group \"%s\"!?\n",filename,grp);
        else
          gid=gr->gr_gid;
        free(grp); }

      if((uid>=0)||(gid>=0)) {
        E(0,"chown %ld:%ld\n",(long)uid,(long)gid);
        if(chown(filename,uid,gid))
          file_job_ask(file_job_simple(ns,NOIR_JOB_DIAL),"Oy","Could not adjust ownership of \"%s\" to %d/%d: %s",filename,uid,gid,strerror(errno)); }}}



  /* MIME */
  else if(!strcasecmp(meta_get_key(r),"mime")) {
    return NOIR_IGNORED;  /* TODO? */
  }



  /* Action (mailcap) */
  else if(!strcasecmp(meta_get_key(r),"action")) {
    return NOIR_IGNORED;  /* TODO */
  }



  /* MTime */
  else if(!strcasecmp(meta_get_key(r),"mtime")) {
    struct utimbuf  ut;
    noir_meta      *atime=meta_in_group(md,"atime",MY_NAME);

    ut.modtime=file_date_parse(meta_get_val(r));
    ut.actime=atime?file_date_parse(meta_get_val(atime)):0;
    if(utime((const char *)filename,&ut))
      return NOIR_FAIL; }

  /* ATime */
  else if(!strcasecmp(meta_get_key(r),"atime")) {
    struct utimbuf  ut;
    noir_meta      *mtime=meta_in_group(md,"mtime",MY_NAME);

    ut.actime=file_date_parse(meta_get_val(r));
    ut.modtime=mtime?file_date_parse(meta_get_val(mtime)):0;
    if(utime((const char *)filename,&ut))
      return NOIR_FAIL; }


  else
    return NOIR_NOTFOUND;

  return NOIR_SUCC; }



#ifdef HAVE_LIBACL
/* functions in this ifdef adapted from getfacl,
   GPL'd code by Andreas Gruenbacher, <a.gruenbacher@computer.org> */

struct acl_perm_def {
  acl_tag_t tag;
  char      c; };

struct acl_perm_def acl_perm_defs[] = {
  { ACL_READ,   'r' },
  { ACL_WRITE,  'w' },
  { ACL_EXECUTE,'x' },
  { 0, 0 }};

/**
 * create meta-data from an ACL
 * @return an error-code
 * @param md    list of meta-data to add to
 * @param st    stat()-info (for owner-UID and owning GID)
 * @param acl   the ACL to create meta-data for
 * @param dflt  TRUE for default-ACL, ACL otherwise
 */
static int acl_walk_list(noir_md *md,const struct stat *st,acl_t acl,int dflt) {
  acl_entry_t    ent;
  int            ret=0,n;
  acl_tag_t      e_type;
  acl_permset_t  e_perms;
  const id_t    *id_p;
  const char    *name,*tag;
  char           perms[]="---";
  noir_provider *prv=NULL;

  if(acl)
    ret=libacl->acl_get_entry(acl,ACL_FIRST_ENTRY,&ent);
  if(ret!=1)
    return NOIR_NODATA;

  while(ret>0) {
    name=tag=NULL;

    libacl->acl_get_tag_type(ent,&e_type);
    libacl->acl_get_permset(ent,&e_perms);

    for(n=0;acl_perm_defs[n].c;n++)
      perms[n]=libacl->acl_get_perm(e_perms,acl_perm_defs[n].tag)?acl_perm_defs[n].c:'-';

    switch(e_type) {
      case ACL_USER_OBJ:
        tag=ACL_TAG_OWNER;
        name=get_username(st->st_uid);
        break;

      case ACL_USER:
        tag=ACL_TAG_NUSER;
        id_p=libacl->acl_get_qualifier(ent);
        if(id_p)
          name=get_username(*id_p);
        break;

      case ACL_GROUP_OBJ:
        tag=ACL_TAG_GROUP;
        name=get_groupname(st->st_gid);
        break;

      case ACL_GROUP:
        tag=ACL_TAG_NGROUP;
        id_p=libacl->acl_get_qualifier(ent);
        if(id_p)
          name=get_groupname(*id_p);
        break;

      case ACL_MASK:
        tag=ACL_TAG_MASK;
        name="";
        break;

      case ACL_OTHER:
        tag=ACL_TAG_OTHER;
        name="";
        break;

      case ACL_UNDEFINED_TAG:
        E(0,"ACL_UNDEFINED_TAG tag ignored...\n");
        break;

      default:
        E(0,"unknown tag type %d ignored...\n",e_type);
        break; }

    if(!name)
      E(0,"cannot get qualifier!?\n");
    else if(tag) {
      char *p,*q;
      size_t l=strlen(tag)+1+strlen(name)+1+strlen(perms)+1;
      if((p=malloc(l))) {
        snprintf(p,l,"%s:%s:%s",tag,name,perms);
        if((q=strchr(p,':'))) {
#if 0
          noir_meta *x;
#endif
          const char *group=dflt?ACL_TAG_DFLT:ACL_TAG_XS;

          *(q++)='\0';
          meta_add(md,META_RW,META_ACL,p,ACL_TAG_ACEDSC,group,q,prv);
#if 0
          if((x=meta_in_group(ret,myFrame->GetTextID(),group))) {
            x->user1=(myFrame->GetID()==ID3FID_CONTENTTYPE)?genre_table:NULL;
            x->user2=(long)myFrame->GetID(); }
#endif
        }
        free(p); }}

    ret=libacl->acl_get_entry(acl,ACL_NEXT_ENTRY,&ent); }

  meta_add(md,META_EF,META_ACL,ACL_TAG_NEWUSR,ACL_TAG_ACEDSC,dflt?ACL_TAG_DFLT:ACL_TAG_XS,":---",prv);
  meta_add(md,META_EF,META_ACL,ACL_TAG_NEWGRP,ACL_TAG_ACEDSC,dflt?ACL_TAG_DFLT:ACL_TAG_XS,":---",prv);

  return NOIR_SUCC; }
#endif



/**
 * get backend-specific meta-data (ACL, EA)
 * @return an error-code
 * @param ns      the session
 * @param myfile  the file
 * @param md      the meta-data list to add to
 */
int backend_get_meta(void *ns,char *myfile,noir_md *md) {
  const char *file=file_if_local(myfile);
  int         ret=NOIR_IGNORED;

  if(!file)
    return NOIR_NOTSUPP;

#ifdef HAVE_LIBACL
  if(libacl) {
    acl_t       acl=NULL,dacl=NULL;
    struct stat st;

    if(stat(file,&st)) {
      int  l;
      char buf[PATH_MAX];
      if(!lstat(file,&st)&&S_ISLNK(st.st_mode)) {
        if((l=readlink(file,buf,PATH_MAX))>=0) {
          buf[l]='\0';
          E(0,"broken symlink \"%s\" -> \"%s\"...\n",file,buf); }
        else
          E(0,"broken symlink \"%s\"...\n",file); }
      goto cleanup; }

    acl=libacl->acl_get_file(file,ACL_TYPE_ACCESS);
    if(!acl&&((errno==ENOSYS)||(errno==ENOTSUP)))
      acl=libacl->acl_from_mode(st.st_mode);
    if(!acl)
      goto fail;

    if(S_ISDIR(st.st_mode)) {
      if(!(dacl=libacl->acl_get_file(file,ACL_TYPE_DEFAULT))) {
        if((errno!=ENOSYS)&&(errno!=ENOTSUP))
          goto fail; }
      else if(!libacl->acl_entries(dacl)) {
        libacl->acl_free(dacl);
        dacl=NULL; }}

    if((!acl||
        (libacl->acl_equiv_mode&&!libacl->acl_equiv_mode(acl,NULL)))&&!dacl)
      goto cleanup;

    acl_walk_list(md,&st, acl,0);
    acl_walk_list(md,&st,dacl,1);

    ret=NOIR_SUCC;
    goto cleanup;

  fail:
    E(0,"couldn't get ACL for \"%s\", #%d - %s\n",file,errno,strerror(errno));
    ret=NOIR_FAIL;

  cleanup:
    if(acl)
      libacl->acl_free(acl);
    if(dacl)
      libacl->acl_free(dacl); }
#endif

#ifdef HAVE_LIBATTR
  if(libattr) {
    ssize_t        size;
    char          *names=NULL,*end_names,*name,*value=NULL;
    noir_provider *prv=NULL;
    int            success=FALSE;

    if(((size=libattr->listxattr(file,NULL,0))<0)||
       !(names=(char *)malloc(size+1))||
       ((size=libattr->listxattr(file,names,size))<0))
      goto done;

    success=TRUE;

    names[size]='\0';
    end_names=names+size;
    
    for(name=names;name!=end_names;name=strchr(name,'\0')+1) {
      void    *ov;
      char    *v;
      ssize_t  l=0,lm=42;
      
      /* check if this attribute shall be preserved */
      if(!*name)
        continue;
      
      size=libattr->getxattr(file,name,NULL,0);
      if(size<0) {
        E(0,"couldn't get %s from \"%s\"...\n",name,file);
        continue; } /* may not have permission to access */

      if(size&&!(value=(char *)realloc(ov=value,size))) {
        free(ov);
        success=FALSE;
        continue; }

      if((size=libattr->getxattr(file,name,value,size))<0) {
        success=FALSE;
        continue; }

      v=value;
      lm=(size<lm)?size:lm;
      while(*v&&(l<lm)) {
        v++;
        l++; }
      if((v=malloc(l+1))) {
        strncpy(v,value,l);
        v[l]='\0';
        /* if we have libacl, ACLs will appear in their own sections, anyway;
           no need to add the raw versions here. */
#ifdef HAVE_LIBACL
        if(!libacl||(strcmp(name,"system.posix_acl_access")&&strcmp(name,"system.posix_acl_default")))
#endif
          meta_add(md,META_RW,META_STR,name,"Extended Attribute",EA_NAME,v,prv);
/* E(3,"looking at EA %s\n",name); */
        free(v); }}

  done:
    if(ret==NOIR_IGNORED)
      ret=success?NOIR_SUCC:NOIR_FAIL;
    else {
      if(success)
        ret=(ret==NOIR_FAIL)?NOIR_PARTIAL:NOIR_SUCC;
      else
        ret=(ret==NOIR_SUCC)?NOIR_PARTIAL:NOIR_FAIL; }

    if(value)
      free(value);
    if(names)
      free(names); }
#endif

  return ret; }



/**
 * get stat-info and put into posixmd struct ("intrinsic" of file_info)
 * @return an error-code
 * @param ns      the session
 * @param myfile  the file
 * @param info    where the pointer to a file_info is store (*info may be NULL, but info mustn't be). allocate file-info and/or posixmd as applicable.
 */
int backend_get_info(void *ns,const char *myfile,file_info **info) {
  int          ret=NOIR_FAIL,
               made_new=0,
               got_stat=FALSE;
  posixmd     *i=NULL;
  const char  *mime,*file;
  struct stat  st;

  if(!info)
    return NOIR_FAIL;

  if(!(file=file_if_local(myfile)))
    return NOIR_NOTSUPP;

  /* no file_info was handed in, make new one.  usually means "tooltip." */
  if(!*info) {
    char *utf8=NULL;
    file_info *ap=file_info_fetch(NULL,file);
    made_new=1;
    if(!ap)
      utf8=file_tag2name(file);
    *info=file_info_new(ap?ap->name:utf8,file);
    if(utf8)
      free(utf8);
    if(!*info)
      return NOIR_FAIL;
    (*info)->children=ap?ap->children:0; }



  /* make room for stat-data if needed */
  if(!(i=(*info)->intrinsic)) {
    if(!(i=malloc(sizeof(posixmd)))) {
      if(made_new) {
        file_info_unref(*info);
        *info=NULL; }
      return NOIR_OOM; }
    else
      memset(i,0,sizeof(posixmd)); }



  /* fill in stat-data */
  if(!lstat(file,&st)) {
    mode_t m=st.st_mode,l=0;
    int    is_link=0;

    if(S_ISLNK(m)) {
      struct stat      st2;

      l=m;
      m=0;

      if((is_link=(!stat(file,&st2))?1:2)!=2) /* broken link!? */
        m=st2.st_mode; }

    ret=NOIR_SUCC;

    if(i) {
      i->mode     =m;
      i->lmode    =l;
      i->hardlinks=st.st_nlink;
      i->uid      =st.st_uid;
      i->gid      =st.st_gid;
      i->size     =st.st_size;
      i->atime    =st.st_atime;
      i->mtime    =st.st_mtime;
      i->ctime    =st.st_ctime;

      got_stat=TRUE;

      (*info)->flags&=~(IF_NOUSE|IF_NOMODIFY);
      (*info)->flags|=get_xs(file,i->uid,i->gid,l?l:m);

      { char *p=strrchr(file,'/');
        (*info)->flags&=~IF_DOTDIR;
        if(p&&!strcmp(p,"/."))
          (*info)->flags|=IF_DOTDIR; }

      (*info)->have|=FF_STAT;
      (*info)->intrinsic=i; }

    mime=file_get_mime(file); /* don't go directly to backend_get_mime() so we can have global MIME-type translations/alii */

    if((*info)->mime)
      free((*info)->mime);
    if(mime&&((*info)->mime=strdup(mime)))
      (*info)->have|=FF_MIME;
    else {
      (*info)->mime=NULL;
      (*info)->have&=~FF_MIME; }}

  if(!got_stat) {
    (*info)->have&=~FF_STAT;
    (*info)->intrinsic=NULL;
    if(i)
      free(i); }

  return ret; }






/*****************************************************************************/
/* directories */



/**
 * open a directory (for use in directory scanner only)
 * @return a handle for this directory
 * @param path  the directory to open
 */
void *backend_opendir(char *path) {
  return (void *)opendir((const char *)path); }



/**
 * read an entry from a directory (for use in directory scanner only)
 * @return an error-code
 * @param data      a handle for this directory (from backend_opendir())
 * @param dir_path  path to the directory
 * @param fp        a buffer (char[PATH_MAX+1]) to return the entry's full path/tag in
 */
int backend_readdir(void **data,char *dir_path,char *fp) {
  struct dirent *de,buf;

  if(data&&*data&&(READDIR((DIR *)*data,buf,de))) {
    size_t      ld=strlen(dir_path);

    if((ld==1)&&(dir_path[0]=='/'))
      strcat(strcpy(fp,dir_path),de->d_name);
    else {
      size_t le=ld+strlen(de->d_name)+2;
      if(le>=PATH_MAX)
        return NOIR_PATHEX;
      snprintf(fp,PATH_MAX,"%s/%s",dir_path,de->d_name); }
    return NOIR_SUCC; }

  E(2,"no more files in \"%s\"...\n",dir_path);
  return NOIR_NOFILE; }



/**
 * close a directory (for use in directory scanner only)
 * @return an error-code
 * @param data   a handle for this directory
 */
int backend_closedir(void **data) {
  closedir((DIR *)*data);
  return NOIR_SUCC; }



/**
 * read an entire directory (ftw(), basically)
 * used in file_monitor_reload_maybe()
 * @return a linked list of dir_entry structs, or NULL
 * @param path   the directory to read
 * @param flags  flags (recursive, with stat(), ...)
 * @param show   callback (whenever a directory is entered or left)
 * @param data   any data to hand to the callback
 */
dir_entry *backend_getdir(char *path,fgd_flags flags,void (*show)(dir_entry *,void *),void *data) {
  dir_entry     *d=NULL,*c=NULL,*n;
  struct dirent *de,buf;
  DIR           *dir;
  char          *p;
  size_t         l2,l=strlen(path)+2;
  struct stat    st;

  flags|=(flags&FGD_DEEP)?FGD_STAT:FGD_NONE;

  if((dir=opendir((const char *)path))) {
    while(READDIR(dir,buf,de)) {
      if(!strcmp(de->d_name,DIR_PARENT)||!strcmp(de->d_name,DIR_MYSELF))
        continue;

      if((n=malloc(sizeof(dir_entry)))) {
        memset(n,0,sizeof(dir_entry));
        if((p=malloc(l2=strlen(de->d_name)+l))) {
          snprintf(p,l2,"%s/%s",path,de->d_name);
          n->name=e2utf8(de->d_name);
          n->tag=p;
/*        n->next=NULL; */  /* already done in memset() */

          if(flags&FGD_STAT) {
/*            if(!stat(n->tag,&st)) { */  /* debugging sth, s/b lstat() normally */
            if(!lstat(n->tag,&st)) {
              n->size=st.st_size;
              n->mode=st.st_mode;
              n->mtime=st.st_mtime;
              if((flags&FGD_DEEP)&&S_ISDIR(st.st_mode)) {
                if((n->child=backend_getdir(n->tag,flags,show,data))) {
                  dir_entry *ch=n->child;
                  while(ch) {
                    n->children++;
                    n->descendents+=ch->descendents+1;
                    n->size+=ch->size;
                    if(!S_ISDIR(ch->mode))
                      n->net+=ch->size;
                    ch=ch->next; }
                  if(show)
                    show(n,data); }}}}

          if(!d)
            d=n;
          else
            c->next=n;
          c=n; }
        else
          free(n); }}
    closedir(dir); }

  return d; }



/*****************************************************************************/



/**
 * directory-scanner thread
 * @return NULL
 * @param data  the queue to the model this scanner is associated with
 */
static void *backend_scanner(void *data) {
  q_head    *qh=(q_head *)data;
  q_element *file_q;
  char       fp[PATH_MAX+2]; /* file-name buffer */
  int        ret;

  while(1) {
    E(6,"trying to get CHG mutex\n");
    pthread_mutex_lock(&qh->qchg);
    E(7,"got CHG mutex\n");

    if(!(file_q=qh->anchor))
      break;
    if(qh->flags&QUEUE_ZOMBIE)
      break;

    /* FF_NAME */

    if(file_q->want&FF_NAME_) {
      /* new dir to read, open it */
      if(!file_q->backend_data) {
        E(1,"reading \"%s\"... ($%x)\n",file_q->dir->tag,file_q->want);

        /* if it can't be opened, dequeue it */
        if(file_opendir(file_q)!=NOIR_SUCC) {
          E(0,"could not open directory \"%s\"...\n",file_q->dir->tag);
          BM_INIT(file_q);       /* so it doesn't get flagged for dumping */
          file_q->want=file_q->wanted=FF_NONE;
          dir_dequeue(qh);
          goto loop; }
        else if(file_q->want) {  /* might have been satisfied from cache */
          BM_INIT(file_q);
          BM_START(file_q);
          file_q->dir->have|=FF_FIRST; }}

      /* readdir() */
      if(!file_q->backend_data) /* couldn't/needn't open dir */
        ;
      else if((ret=backend_readdir(&file_q->backend_data,file_q->dir->tag,fp))==NOIR_SUCC) {
        file_info *fi;
        char      *p=backend_tag2name(fp); /* don't call file_tag2name(), it might do substitutions which would mess with IS_IGNOBLE */

        if(!IS_IGNOBLE(p)&&(fi=file_info_insert(NULL,fp))) { /* sets FF_NAME */
          /* sets FF_STAT, FF_MIME if it can */
          if((fi->have&(FF_STAT|FF_MIME))!=(FF_STAT|FF_MIME)) {
            if(backend_get_info(NULL,fi->tag,&fi)!=NOIR_SUCC)
              E(0,"stat(\"%s\") failed!?\n",fi->tag);
            fi->have|=FF_UNSEEN; }}

        if(p)
          free(p); }

      /* readdir failed, End Of Dir */
      else if(ret==NOIR_NOFILE) {
        file_flags done=FF_NAME_|FF_STAT_|FF_MIME_;

        /* closedir() */
        file_closedir(file_q);  /* NULLs backend_data */
        BM_STOP(file_q);
        file_q->want&=(~done); }}



    /* FF_META */

    else if(file_q->want&FF_META_) {
      if(!file_q->backend_data) { /* start meta-scanning */
        E(2,"starting meta-scan for \"%s\"...\n",file_q->dir->tag);
        if(!(file_q->backend_data=file_q->dir->child)) {
          file_q->dir->have|=FF_META_;
          file_q->want&=(~FF_META_); }}
      else {
        file_info *fi=file_q->backend_data;

        E(5,"getting meta-data for \"%s\"...\n",fi->tag);
/*      E(0,"getting meta-data for \"%s\" (%s)\n",fi->tag,fi->mime?fi->mime:""); */
        if(!(fi->have&FF_META)) {
          if(fi->mime&&
             (!strncmp(fi->mime,"audio/",6)||!strncmp(fi->mime,"text/html",9)||
              !strcmp(fi->mime,"folder")||
              !strcmp(fi->mime,"application/x-font-ttf")))
            file_get_meta_with_lock(fi,&qh->qchg); }

        if(!(file_q->backend_data=fi->next)) {
          file_q->dir->have|=FF_META_;
          file_q->want&=(~FF_META_); }}}



    /* FF_xxxx */

    else if(file_q->want) {
      if(file_q->want&FF_THUMB_)
        file_q->want&=(~FF_THUMB_);
      else {
        E(0,"FAILSAFE -- ignoring unknown remaining requirement $%x\n",file_q->want);
        file_q->want=FF_NONE;
        ret=NOIR_FAIL; }
      dir_requeue(qh); }



    else
      dir_requeue(qh);



  loop:
    E(6,"releasing CHG mutex\n");
    pthread_mutex_unlock(&qh->qchg); }

  qh->flags&=(~QUEUE_SCANNING);

  E(6,"releasing CHG mutex\n");
  pthread_mutex_unlock(&qh->qchg);

  E(3,"ends ($%x)\n",qh->flags);
  return NULL; }



/**
 * create and start a directory-scanner.
 * @return an error-code
 * @param qh  the queue to the model this scanner is associated with
 */
int backend_scanner_activate(q_head *qh) {
  if(qh->flags&QUEUE_SCANNING) {
    E(2,"scanner already active (top: \"%s\")...\n",qh->anchor?qh->anchor->dir->tag:"<empty>");
    return NOIR_IGNORED; }
  if(pthread_create(&qh->scanner,NULL,backend_scanner,qh)) {
    E(0,"couldn't start scanner!?\n");
    return NOIR_FAIL; }

  E(3,"starting scanner (top: \"%s\")...\n",qh->anchor?qh->anchor->dir->tag:"<empty>");
  qh->flags|=QUEUE_SCANNING;
  return NOIR_FAIL; }






/*****************************************************************************/
/* init/exit */



#ifdef HAVE_LIBCURL

/**
 * try to open libcurl (read non-local URLs) and resolve the symbols we'll need
 * on success, libcurl will point to a filled-in struct _libcurl
 * @return an error-code
 */
static int libcurl_init(void) {
  CURLcode  cr;
  int       ret;

  if(!(libcurl=malloc(sizeof(struct _libcurl))))
    return NOIR_OOM;
  memset(libcurl,0,sizeof(struct _libcurl));

  ret=dlmulti("remote URLs","libcurl.so",RTLD_NOW,&libcurl->handle,
              "!curl_global_init !curl_global_cleanup "
              "!cc_curl_easy_init   !cc_curl_easy_cleanup "
              "!cc_curl_easy_setopt !cc_curl_easy_perform "
              "?cc_curl_easy_getinfo",
              &libcurl->curl_global_init,
              &libcurl->curl_global_cleanup,
              &libcurl->cc_curl_easy_init,
              &libcurl->cc_curl_easy_cleanup,
              &libcurl->cc_curl_easy_setopt,
              &libcurl->cc_curl_easy_perform,
              &libcurl->cc_curl_easy_getinfo);

  if((ret!=NOIR_SUCC)&&(ret!=NOIR_PARTIAL)) {
    if(ret==NOIR_NODATA)
      E(0,"unable to support remote URLs in this backend (libcurl.so not found).\n");
    free(libcurl);
    libcurl=NULL; }
  else if((cr=libcurl->curl_global_init(CURL_GLOBAL_ALL))!=CURLE_OK) {
    E(0,"curl_global_init() failed (%d)!? external URLs will not be supported.\n",cr);
    free(libcurl);
    libcurl=NULL; }

  return ret; }
#endif



#ifdef HAVE_LIBATTR

/**
 * try to open libattr (Extended Attributes) and resolve the symbols we'll need
 * on success, libattr will point to a filled-in struct _libattr
 * @return an error-code
 */
static int libattr_init(void) {
  int ret;

  if(!(libattr=malloc(sizeof(struct _libattr))))
    return NOIR_OOM;
  memset(libattr,0,sizeof(struct _libattr));

  ret=dlmulti("extended attributes","libattr.so.1",RTLD_NOW,&libattr->handle,
              "!listxattr, !getxattr, !setxattr, ?attr_copy_file",
              &libattr->listxattr,&libattr->getxattr,&libattr->setxattr,&libattr->attr_copy_file);
  if((ret!=NOIR_SUCC)&&(ret!=NOIR_PARTIAL)) {
    if(ret==NOIR_NODATA)
      E(0,"unable to support Extended Attributes (libattr.so not found).\n");
    free(libattr);
    libattr=NULL; }
  return ret; }
#endif



#ifdef HAVE_LIBACL

/**
 * try to open libacl (Access Control Lists) and resolve the symbols we'll need
 * on success, libacl will point to a filled-in struct _libacl
 * @return an error-code
 */
static int libacl_init(void) {
  int ret;

  if(!(libacl=malloc(sizeof(struct _libacl))))
    return NOIR_OOM;
  memset(libacl,0,sizeof(struct _libacl));

  ret=dlmulti("access control lists","libacl.so.1",RTLD_NOW,&libacl->handle,
              "!acl_get_file, !acl_get_entry, !acl_get_tag_type, "
              "!acl_get_permset, !acl_get_perm, !acl_get_qualifier, "
              "!acl_free, !acl_from_mode, !acl_entries, !acl_equiv_mode, "
              "?perm_copy_file, !acl_set_file, !acl_init, !acl_create_entry, "
              "!acl_set_permset, !acl_set_qualifier, !acl_set_tag_type, "
              "!acl_add_perm",
              &libacl->acl_get_file, &libacl->acl_get_entry, &libacl->acl_get_tag_type,
              &libacl->acl_get_permset, &libacl->acl_get_perm, &libacl->acl_get_qualifier,
              &libacl->acl_free, &libacl->acl_from_mode, &libacl->acl_entries, &libacl->acl_equiv_mode,
              &libacl->perm_copy_file,&libacl->acl_set_file,&libacl->acl_init,
              &libacl->acl_create_entry,&libacl->acl_set_permset,
              &libacl->acl_set_qualifier,&libacl->acl_set_tag_type,
              &libacl->acl_add_perm);
  if((ret!=NOIR_SUCC)&&(ret!=NOIR_PARTIAL)) {
    if(ret==NOIR_NODATA)
      E(0,"unable to support Access Control Lists (libacl.so not found).\n");
    free(libacl);
    libacl=NULL; }
  return ret; }
#endif



/**
 * initialize this backend
 * load libraries etc.
 * @return an error-code
 * @param qh  the queue to use to speak with the model
 */
int backend_init(q_head *qh) {
  groups_init();
  mime_init();

#ifdef I_NOTIFY
  inotify_init_all();
#endif
#ifdef F_NOTIFY
  fnotify_init();
#endif
#ifdef HAVE_FAM
  libfam_init();
#endif
#ifdef HAVE_LIBACL
  libacl_init();
#endif
#ifdef HAVE_LIBATTR
  libattr_init();
#endif
#ifdef HAVE_LIBCURL
  libcurl_init();
#endif

  timer_monitor=timeout_add(MONITOR_FREQ,backend_monitor_check,NULL);

  return NOIR_SUCC; }



/**
 * deinitialize this backend
 * free resources, unload libraries, stop threads
 * @return an error-code
 */
int backend_exit(void) {
  if(timer_monitor) {
    timeout_remove(&timer_monitor);
    timer_monitor=NULL; }

  mime_exit();

  if(ht_handler)
    hash_table_dst(&ht_handler);

#ifdef HAVE_LIBCURL
  if(libcurl) {
    libcurl->curl_global_cleanup();
    dlclose(libcurl->handle);
    free(libcurl); }
#endif

#ifdef F_NOTIFY
  fnotify_exit();
#endif

#ifdef I_NOTIFY
  inotify_exit();
#endif

#ifdef HAVE_FAM
  libfam_exit();
#endif

#ifdef HAVE_LIBACL
  if(libacl) {
    dlclose(libacl->handle);
    free(libacl); }
#endif

#ifdef HAVE_LIBATTR
  if(libattr) {
    dlclose(libattr->handle);
    free(libattr); }
#endif

  return NOIR_SUCC; }
