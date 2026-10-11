/** @file  file.c
 *  @brief file-related functions abstracting the backends
 *
 * other parts of evidence interact with these exclusively. if the op
 * is common to all backends, it is handled in here; otherwise, it is
 * forwarded to the selected backend (which in turn may call generic
 * helpers in here).
 */

#include "config.h"

#include "debug.h"
#include "file.h"                 /* forward decls */
#include "mime.h"                 /* MIME_* */
#include "meta.h"                 /* meta_...() */
#include "setup.h"                /* get_setup_file() */
#include "gui/common/alert.h"     /* form_...() */
#include "gui/common/icon_path.h" /* form_...() */
#include "gui/common/session.h"   /* noir_sess_get_cwd() ### FIXME */
#include "cache.h"                /* file_load_cache() */
#include "util.h"                 /* int hashes, unit_size() */
#include "errors.h"               /* NOIR_* */
#include "file_fun.h"
#include "monitors.h"             /* file_monitor_remove_all() */
#include "action.h"               /* action_file_dst(), action_file_new() */
#include "properties.h"           /* prop_*() */
#include "file_lcl.h"             /* file_if_lcl() */

#include <ctype.h>                /* isspace(), tolower() */
#include <stdarg.h>               /* varargs in file_job_ask() */
#include <pwd.h>                  /* getpwuid() */
#include <grp.h>                  /* getgrnam() */
#include <sys/types.h>            /* getpwuid() */
#include <sys/stat.h>             /* S_...(), S_... */
#include <dlfcn.h>                /* dl...() */
#include <errno.h>                /* errno */
#include <stdio.h>                /* stderr */
#include <stdlib.h>               /* malloc(), free() */
#include <string.h>               /* str...() */
#include <unistd.h>               /* fork(), exec*(), write(), close() */
#include <netdb.h>                /* gethostbyname() */
#include <pthread.h>              /* mutex fu */
#include <signal.h>               /* signal() */
#include <sys/wait.h>             /* waitpid(), W* */
#include <fcntl.h>                /* creat() */
#include <sys/file.h>             /* flock() */
#ifdef HAVE_WORDEXP
#  include <wordexp.h>
#else
#  include <glob.h>               /* glob() */
#endif

#include "model_private.h"        /* ### FIXME */



#define LISTENER "file"



typedef enum {
  FI_CHDIR_WARNINGS,
  FI_PRESCAN_CP,
  FI_PRESCAN_MV,
  FI_PRESCAN_RM
} fi_tag_type;



struct _fi_param {
  int chdir_warnings;
  int prescan_cp,
      prescan_mv,
      prescan_rm;
} fi_param;



void                     *lib;

static struct _file_fun   ff;

static void              *ht_user=NULL,
                         *ht_group=NULL,
                         *ht_annotation=NULL,
                         *ht_mime_alii=NULL;

static noir_job          *jobs=NULL;

pthread_mutex_t           fe_pause=PTHREAD_MUTEX_INITIALIZER; /* front-end */



static noir_job          *file_job_new(void *,noir_job_type,action_context *,void *);
static int                file_job_dst(noir_job **);
static int                file_job_scan(noir_job *job,const char *path);
static int                file_job_paths(noir_job *job,const char *src_path,const char *dst_path,char **src_old,char **dst_old);



/*****************************************************************************/



char *job_type[]  ={ "???", "dial", "copy", "move", "delete" };
char *job_status[]={ "unchanged", "start", "add", "del", "progress", "edit", "question", "done" };



/*****************************************************************************/



/**
 * listen for changes in our numeric properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int file_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  long              val=prop_get_int(e);
  const char       *key=prop_get_key(e);
  struct _fi_param *fip=(struct _fi_param *)data;

  if(!fip)
    return NOIR_FAIL;

  switch(tag) {
    case FI_CHDIR_WARNINGS:
      fip->chdir_warnings=val;
      break;
    case FI_PRESCAN_CP:
      fip->prescan_cp=val;
      break;
    case FI_PRESCAN_MV:
      fip->prescan_mv=val;
      break;
    case FI_PRESCAN_RM:
      fip->prescan_rm=val;
      break;
    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld, %p)\n",key,val,data);

  return NOIR_SUCC; }



#if 0
/**
 * listen for changes in our string-properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int file_str_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  char          *val=prop_get_string(e);
  const char    *key=prop_get_key(e);

  if(val) {
    return NOIR_SUCC; }

  return NOIR_IGNORED; }
#endif






/*****************************************************************************/



/**
 * get backend that handles this URL
 * @return a struct of function pointers
 * @param tag  the URL
 */
file_fun *file_get_backend(const char *tag) {
  return &ff; }  /* always default -- for now */



/*****************************************************************************/



/**
 * get hostname. singleton.
 * @return the local host's name
 */
char *get_hostname(void) {
  static char *hostname=NULL;

  if(!hostname) {
    char           hb[4096];

    if(!gethostname(hb,4096)) {
      struct hostent *ent;
      hb[4095]='\0';
      ent=gethostbyname(hb);
      hostname=ent?ent->h_name:hb; }
    else {
      hostname="localhost";
      E(0,"identity crisis -- can't remember my own name!?\n"); }

    hostname=strdup(hostname); }

  return hostname; }



/*****************************************************************************/
/* helpers for the backend */



/**
 * return next string in a \n separated list
 * @return start of a C-string
 * @param drop  pointer to remainder of list
 */
#if 0
char *file_get_from_selection(char **drop) {
  char *p,*r;

  if(!drop||!*drop)
    return NULL;

  r=*drop;                       /* remember head */

  p=strchr(r,'\0')+1;
  *drop=p;                       /* set drop to tail or NULL */

  if(strstr(r,PROTOCOL_DELIMITER)) {
    if((p=strchr(r,'#')))
      *p='\0'; }

  /* remove file:// if present */
  if((p=(char *)file_if_local(r)))
    r=p;

  /* if rightmost element is /. complain and return next element */
  if((p=strrchr(r,DIR_DELIMITER))&&!strcmp(p,"/.")) {
    E(0,"Cannot use the magic " DIR_DOT " folder in this way yet...\n");
    /* form_alert(NULL,"Cannot use the magic " DIR_DOT "\nfolder in this way yet..."); */  /* ### FIXME */
    r=file_get_from_selection(drop); }

  return r; }
#endif



#define MAX_NUMLEN 65

/**
 * get user-name for UID
 * @return a user-name or "". the application should not free() this.
 * @param uid  the numeric user-ID
 */
char *get_username(uid_t uid) {
  void          *res;
  struct passwd *pwe;
  char           u[MAX_NUMLEN];
  int            r;

  if(!ht_user) {
    if(!(ht_user=hash_table_new(free,free))) {
      int warned=0;
      if(!warned) {
        warned++;
        E(0,"couldn't create hashtable!?\n"); }
      return ""; }}

  if(((r=snprintf(u,MAX_NUMLEN,"%ld",(long)uid))<0)||(r>=MAX_NUMLEN)) {
    if((pwe=getpwuid(uid)))
      return pwe->pw_name;
    return ""; }

  if((res=hash_table_fetch(ht_user,u)))
    return res;

  if((pwe=getpwuid(uid))) {
    char *k,*v;
    if((k=strdup(u))) {
      if((v=strdup(pwe->pw_name)))
        hash_table_insert(ht_user,k,v);
      else
        free(k); }
    return pwe->pw_name; }
  else
    return ""; }



/**
 * get group-name for GID
 * if ENUM_GROUPS is defined, group members are enumerated
 * @return a group-name or "". the application should not free() this.
 * @param gid  the numeric group-ID
 */
char *get_groupname(gid_t gid) {
  void         *res;
  struct group *pwe;
  char          g[MAX_NUMLEN];
  int           r;

  if(!ht_group) {
    if(!(ht_group=hash_table_new(free,free))) {
      int warned=0;
      if(!warned) {
        warned++;
        E(0,"couldn't create hashtable!?\n"); }
      return ""; }}

  if(((r=snprintf(g,MAX_NUMLEN,"%ld",(long)gid))<0)||(r>=MAX_NUMLEN)) {
    if((pwe=getgrgid(gid)))
      return pwe->gr_name;
    else
      return ""; }

  if((res=hash_table_fetch(ht_group,g)))
    return res;

  if((pwe=getgrgid(gid))) {
    size_t  l;
    int     nm;
    char   *k,*v,**b;

    if(!(k=strdup(g)))
      return pwe->gr_name;

#ifdef ENUM_GROUPS
    l=strlen(pwe->gr_name)+4;
    b=pwe->gr_mem;
    nm=0;
    while(*b) {
      nm++;
      l+=strlen(*b)+1;
      b++; }
    if(!(v=malloc(l))) {
#else
    if(!(v=strdup(pwe->gr_name))) {
#endif
      free(k);
      return pwe->gr_name; }

#ifdef ENUM_GROUPS
    else {
      strcpy(v,pwe->gr_name);
      if(nm) {
        strcat(v," (");
        b=pwe->gr_mem;
        while(*b) {
          strcat(strcat(v,*b),",");
          b++; }
        v[strlen(v)-1]=')'; }}
#endif
    hash_table_insert(ht_group,k,v);
    return v; }
  else
    return ""; }



/**
 * Make sure $HOME/.evidence exists.
 * @return $HOME/.evidence/(fn), or NULL
 * @param fn  the file to look for
 */
char *file_user_setup(const char *fn) {
  char   *r=NULL;

  if(esprintf(&r,"%s/" DIR_USER,home)>0) {
    mkdir(r,S_IRWXU);  /* may fail, we don't care */
    E(1,"\"%s\": %d\n",r,S_ISDIR(file_exists(r)));
    if((S_ISDIR(file_exists(r)))&&
       (esprintf(&r,"%s/" DIR_USER "/%s",home,fn)>0))
      return r;
    free(r); }
  else
    E(0,"could not esprintf()!?\n");
  return NULL; }



/**
 * get date from string, return as time_t (seconds since epoch)
 * @return a time_t
 * @param date  a date (and time?) in the form [YY]YY-MM-DD[ HH:MM]
 */
time_t file_date_parse(const char *date) {
  char *d=strdup(date);
  time_t ret=0;
  if(d) {
    int        found;
    char      *p=strchr(d,' ');
    struct tm  t;

    memset(&t,0,sizeof(struct tm));

    if(p)
      *(p++)='\0';

    if((found=sscanf(d,"%u-%u-%u",&t.tm_year,&t.tm_mon,&t.tm_mday))!=3)
      form_alert(NULL,"Could not understand date part \"%s\" of date/time entry?",d);
    else {
      t.tm_mon--;
      if(t.tm_year>1900)
        t.tm_year-=1900; }
    E(1,"found/date: %d\n",found);

    t.tm_hour=12;
    if(p&&*p) {
      if((found=sscanf(p,"%u:%u",&t.tm_hour,&t.tm_min))!=2)
        form_alert(NULL,"Could not understand time part \"%s\" of date/time entry?",p); }
    E(1,"found/time: %d\n",found);

    ret=mktime(&t);

    if(debug>1) {
      char tb[100];
      strftime(tb,100,"%Y-%m-%d %H:%M",&t);
      E(0,"time %s (%d-%d-%d %d:%d) => %s\n",date,
        t.tm_year+1900,t.tm_mon+1,t.tm_mday,
        t.tm_hour,t.tm_min,
        tb); }

    free(d); }
  return ret; }



/**
 * parse standard UNIX perms
 * @return an error-code < 0 or perm-bits
 * @param p    the row ("rwt azundris" etc.)
 * @param ps   the permset-type we hope we've got (user/group/other)
 * @param who  where to store the user- or group-name, or NULL
 */
int file_perms_parse(const char *p,noir_permset ps,char **who) {
  char *perms=strdup(p),*d,*w=NULL;
  char  x='x';
  int   r=0,s=0;

  if(!perms)
    return NOIR_OOM;

  if((d=strchr(perms,' '))&&(ps==NOIR_PERMSET_OTHER))
    goto tmm;

  if(d) {
    while(isspace(*d))
      *(d++)='\0';
    if(*d)
      w=d; }

  x=(ps!=NOIR_PERMSET_OTHER)?'s':'t';

  if(strlen(perms)!=3)
    goto tmm;
  for(d=perms;*d;d++)
    *d=tolower(*d);

  if(perms[0]=='r')
    r|=S_IROTH;
  else if(perms[0]!='-')
    goto tmm;

  if(perms[1]=='w')
    r|=S_IWOTH;
  else if(perms[1]!='-')
    goto tmm;

  if(perms[2]=='x')
    r|=S_IXOTH;
  else if(perms[2]!='-') {
    if(perms[2]!=x)
      goto tmm;
    switch(ps) {
      case NOIR_PERMSET_USER:  s|=S_ISUID; break;
      case NOIR_PERMSET_GROUP: s|=S_ISGID; break;
      case NOIR_PERMSET_OTHER: s|=S_ISVTX; break; }}

  if(who)
    *who=w?strdup(w):NULL;
  free(perms);

  return (r<<(ps*3))|s;

 tmm:
  free(perms);
  return NOIR_TYPEMISMATCH; }



/**
 * action consists of several sub-actions; allow for partial success
 * @return NOIR_SUCC (all succeeded), NOIR_FAIL (all failed), NOIR_PARTIAL (some succeeded)
 * @param retval  the result of the current sub-action
 * @param r       where the cumulative status is stored. must contain NOIR_IGNORED before first call to file_partial()
 */
int file_partial(int retval,int *r) {
  if(retval==*r)
    return *r;
  if(retval==NOIR_SUCC)
    *r=(*r==NOIR_IGNORED)?NOIR_SUCC:NOIR_PARTIAL;
  else
    *r=(*r==NOIR_IGNORED)?NOIR_FAIL:NOIR_PARTIAL;
  return *r; }



/**
 * get file-name from a tag (in local encoding)
 * @return the file-name of a tag (ignoring the path that leads up to it)
 * @param tag     the file the basename of which we want to know
 */
const char *file_name(const char *tag) {
  const char *p;
  if(!tag)
    return "";
  if((p=strrchr(tag,DIR_DELIMITER)))
    return ((p==tag)&&(!p[1]))?p:p+1;
  return tag; }



/**
 * get dir-name from a tag (in local encoding)
 * @return     the dir-name of a tag in a newly-allocated buffer
 * @param tag  the file the dirname of which we want to know
 */
char *file_dirname(const char *tag) {
  char *p,*q;
  if(!tag)
    return NULL;
  if(!strcmp(tag,DIR_DELIM_STR))
    return strdup(DIR_DELIM_STR);
  if(!strcmp(tag,DIR_MYSELF))
    return strdup(DIR_PARENT);
  if(!strcmp(tag,DIR_PARENT)) {
    q=NULL;
    esprintf(&q,"%s%s%s",DIR_PARENT,DIR_DELIM_STR,DIR_PARENT);
    return q; }
  if((p=strdup(tag))) {
    if(!(q=strrchr(p,DIR_DELIMITER))) {
      free(p);
      return strdup(DIR_MYSELF); }
    *q='\0';
    if((q=strdup(p))) {
      free(p);
      return q; }
    return p; }
  return NULL; }



/**
 * combine several components into a path. add and remove slashes as needed.
 * @return a newly-allocated C-string containing the combined path
 * @param part ... the parts to combine
 */
char *file_compose_path(const char *part,...) {
  va_list     args;
  char       *ret=NULL,*p,*q;

  if(!part||!(ret=strdup(part)))
    return NULL;

  va_start(args,part);
  while((part=va_arg(args,const char *)))
    ecat(&ret,DIR_DELIM_STR,part,NULL);
  va_end(args);

  for(q=ret;(p=strstr(q,"//"));) {
    if((p>ret)&&!strncmp(p-1,PROTOCOL_DELIMITER,sizeof(PROTOCOL_DELIMITER)-1))
      q=p+2;
    else {
      memmove(p,p+1,strlen(p));
      q=p; }}

  return ret; }



/**
 * make URL from path/filename
 * @return a file://-URL, or NULL for failure
 * @param file  the path and file-name
 */
char *file_to_URL(const char *file) {
  char       *p,*ret=NULL;

  if(!file||!*file)
    return NULL;

  if(!(p=strstr(file,PROTOCOL_DELIMITER))) {   /* isn't URL */
    if(file[0]==DIR_DELIMITER) {
      if((ret=malloc(strlen(file)+sizeof(PROTOCOL_FILE))))
        strcat(strcpy(ret,PROTOCOL_FILE),file); }}  /* ### FIXME */
  else
    ret=strdup(file);  /* already is an URL, just return copy */

  return ret; }



/**
 * return path and file-name from file://-URL or path
 * will accept /path/file, file:///path/file, file://host.tld/path/file;
 * will reject file://, file://host.tld, and file://host.tld/path/file where host.tld isn't us
 * @return local file (part of 'file', should not be explicitly free()d), or NULL for failure
 * @param file  the path or URL
 */
const char *file_if_local(const char *file) {
  char *p;

  if(!file||!*file)
    return NULL;

  if(!(p=strstr(file,PROTOCOL_DELIMITER))) {
    if(!strncmp("file:/",file,6)) {        /* wonky konquie URL */
      E(1,"malformed URL %s\n",file);
      for(p=(char *)file+5;p[1]==DIR_DELIMITER;p++);
      return *p?p:NULL; }
    return (char *)file; }                 /* file only */

  if(!strncmp(file,PROTOCOL_FILE,sizeof(PROTOCOL_FILE)-1)) {
    p+=3;

    if(!*p)                                /* malformed, protocol only */
      return NULL;
    else if(*p==DIR_DELIMITER)             /* malformed, no host */
      return p;
    else {                                 /* has host */
      char *h,*q,*m;

      if(!(h=strdup(p)))                   /* OOM */
        return NULL;

      if(!(q=strchr(h,DIR_DELIMITER))) {   /* malformed, no file */
        E(0,"malformed URL \"%s\"!?\n",file);
        p=NULL; }

      else {                               /* full URL. is it local? */
        m=get_hostname();
        *q='\0';
        p=strchr(p,DIR_DELIMITER);
        E(3,"file: host \"%s\", file \"%s\"; I am \"%s\"\n",h,p,m);
        if(strcmp(h,m))
          p=NULL; }

      free(h);
      return p; }}

  return NULL; }



/**
 * return local part if 'file' is local and absolute.
 * see file_if_local() for what constitues a local file.
 * @return the local part, or NULL for not local or not absolute
 * @param file  the file to check
 */
const char *file_if_absolute(const char *file) {
  const char *fn=file_if_local(file);
  if(fn&&((fn[0]==DIR_DELIMITER)||(fn[0]=='~')))
    return fn;
  return NULL; }



/**
 * return local part if 'file' is local and relative.
 * see file_if_local() for what constitues a local file.
 * @return the local part, or NULL for not local or not relative
 * @param file   the file to check
 */
const char *file_if_relative(const char *file) {
  const char *fn=file_if_local(file);
/* if(fn&&!strncmp(fn,"./",2)) */
  if(fn&&((fn[0]!=DIR_DELIMITER)&&(fn[0]!='~')))
    return fn;
  return NULL; }



/**
 * return local part if 'file' is local and nested.
 * see file_if_local() for what constitues a local file.
 * @return the local part, or NULL for not local or not nested
 * @param file   the file to check
 */
const char *file_if_nested(const char *file) {
  const char *fn=file_if_local(file);
  if(fn&&strrchr(fn,DIR_DELIMITER))
    return fn;
  return NULL; }



/**
 * return local part if 'file' is local and a dot-folder.
 * see file_if_local() for what constitues a local file.
 * @return the local part, or NULL for not local or not a dot-dir
 * @param file   the file to check
 */
const char *file_if_dotdir(const char *file) {
  const char *fn=file_name(file_if_local(file));
  return (fn&&!strcmp(fn,DIR_MYSELF))?fn:NULL; }






/*****************************************************************************/



/**
 * load info for an entire directory, synchroneously
 * called from file_monitor_reload_maybe()
 * @return a linked list of directory-entries
 * @param path   the directory to load
 * @param flags  any flags
 * @param show   callback (whenever a directory is entered or left)
 * @param data   any data to hand to the callback
 */
dir_entry *file_getdir(const char *path,fgd_flags flags,void (*show)(dir_entry *,void *),void *data) {
  return file_get_backend(path)->file_getdir(path,flags,show,data); }



/**
 * release a dir-entry record and all its successors
 * @return NULL
 * @param d  the head of a list of dir-entries
 */
dir_entry *dir_entry_dst_all(dir_entry *d) {
  dir_entry *e;
  while(d) {
    if(d->child)
      d->child=dir_entry_dst_all(d->child);
    e=d;
    d=d->next;
    free(e->tag);
    free(e->name);
    free(e); }
  return d; }



/*****************************************************************************/
/* these are to be called only by the backends! */



/**
 * open a directory to scan it -- unless we can get the info from the persistent on-disk cache
 * to be called by backend only!
 * @return an error-code
 * @param q  a queue-element (the directory we're interested in)
 */
int file_opendir(q_element *q) {
  /* try to load from persistent on-disk cache */
  if((!(q->dir->have&FF_LAST))&&(file_load_cache(q)==NOIR_SUCC)) {
    q->backend_data=NULL; /* don't note a dir-pointer on the Q-element, we don't have a dir */
    return NOIR_SUCC; }
  /* not cached, open dir using backend and remember the tag of this dir on
     queue element */
  return (q->backend_data=ff.file_opendir(q->dir->tag))?NOIR_SUCC:NOIR_FAIL; }



/**
 * close a directory after scanning it
 * to be called by backend only!
 * @return an error-code
 * @param q  a queue-element (the directory we're through with)
 */
int file_closedir(q_element *q) {
  int ret=ff.file_closedir(&q->backend_data);
  /* we no longer have an open dir, so don't have a pointer to one */
  q->backend_data=NULL;
  return ret; }



/*****************************************************************************/



/**
 * load a (setup?) file
 * @return an error-code
 * @param fn    the file-name
 * @param data  where the loaded data was stored
 */
int file_load(const char *fn,char **data) {
  char *d=NULL;
  int   fd,ret=NOIR_FAIL;

  if(!fn||!data)
    return NOIR_FAIL;

  if((fd=open(fn,O_RDONLY))>=0) {
    struct stat st;
    ssize_t get,got;

    if(stat(fn,&st))
      ret=NOIR_NOFILE;
    else {
      get=st.st_size;
      if(!(d=malloc(get+1)))
        ret=NOIR_OOM;
      else {
        flock(fd,LOCK_EX);

        if((got=read(fd,d,get))==get) {
          d[got]='\0';
          ret=NOIR_SUCC; }
        else if(!got)
          ret=NOIR_NODATA;
        else if(got<0)
          ret=NOIR_FAIL;
        else
          ret=NOIR_PARTIAL;

        flock(fd,LOCK_UN);

        if(ret!=NOIR_SUCC) {
          free(d);
          d=NULL; }}}

    close(fd); }
  else
    ret=NOIR_NOTFOUND;

  *data=d;

  return ret; }



/**
 * save a (setup?) file
 * @return an error-code
 * @param fn    the file-name
 * @param mode  the mode for the new file, or -1 for default (0666)
 * @param len   the number of chars to write, or -1 for strlen()
 * @param data  the data to write
 */
int file_save(const char *fn,mode_t mode,ssize_t len,const char *data) {
  int fd,ret=NOIR_FAIL;

  if(!fn)
    return NOIR_FAIL;
  if(!data)
    return NOIR_NODATA;

  if(mode<0)
    mode=S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;
  if(len<0)
    len=strlen(data);

  if((fd=creat(fn,mode))>=0) {
    ssize_t written;
    flock(fd,LOCK_EX);
    if((written=write(fd,data,len))!=len) {
      ret=NOIR_PARTIAL;
      E(0,"Error #%d writing %s...\n(%s)",errno,fn,strerror(errno)); }
    else
      ret=NOIR_SUCC;
    flock(fd,LOCK_UN);
    close(fd); }
  else {
    E(0,"Couldn't create %s...",fn);
    ret=NOIR_NOFILE; }

  return ret; }



/*****************************************************************************/

/**
 * lock a file.
 * @return an error-code < 0, or a file-descriptor
 * @param file   the file's tag
 * @param flags  usually O_RDONLY, O_WRONLY or O_RDWR
 * @param mode   the mode for the new file (if creating one)
 */
int file_lock(const char *file,int flags,mode_t mode) {
  int fd;

  if(!file)
    return NOIR_IGNORED;

  if((fd=open(file,flags,mode))<0) {
    switch(errno) {
      case ENFILE:
      case EMFILE:
      case ENOMEM:
        fd=NOIR_OOM;
        break;
      case ENOENT:
        fd=NOIR_NOFILE;
        break;
      case EISDIR:
      case EACCES:
      case ETXTBSY:
      case EROFS:
      case EEXIST:
        fd=NOIR_IGNORED;
        break;
      default:
        fd=NOIR_FAIL; }}

  else {
 try_lock:
    if(flock(fd,LOCK_EX)) {
      switch(errno) {
        case EAGAIN:
          E(0,"failed to lock \"%s\", spinning...\n",file);
          thread_sleep(1);
          goto try_lock;
        case EINTR:
          goto try_lock;
        case EINVAL:
          fd=NOIR_TYPEMISMATCH;
          break;
        case ENOLCK:
          fd=NOIR_OOM;
          break;
        default:
          fd=NOIR_FAIL; }}}

  return fd; }



/**
 * unlock a file. closes the fd!
 * @return an error-code
 * @param fd  the file-descriptor
 */
int file_unlock(int fd) {
  if(fd<0)
    return NOIR_IGNORED;
  flock(fd,LOCK_UN);
  close(fd);
  return NOIR_SUCC; }



/*****************************************************************************/



int file_is_globbable(const char *b) {
  const char *p;
  for(p=b;*p;p++) {
    if(*p=='\\') {
      if(!p[1])
        return NOIR_FAIL; }
    else if(strchr("*?[",*p))
      return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * return a list of all matches for a glob-expression
 * @return an action-context containing all matches for the pattern (which may mean an empty selection), or NULL for a failure other than "no matches"
 * @param pattern  the glob-pattern to match against
 * @param errmsg   where to store a pointer to an error-message, or NULL. if non-NULL pointer is returned, pointed-at string must not be free()d.
 */
action_context *file_glob(const char *pattern,const char **errmsg) {
  action_context *ac=NULL;
  const char     *msg="???";
#ifdef HAVE_WORDEXP
  wordexp_t       myword;
  int             n,n2,r;

  if(errmsg)
    *errmsg=NULL;

  if(!pattern)
    return NULL;

  if(!(r=wordexp(pattern,&myword,WRDE_SHOWERR))) {
    if(!(ac=action_context_new(0,NULL)))
      goto done;

    /* put results in action-context. append slashes to dirs. */
    if((n2=myword.we_wordc)) {
      int fe;
      if((n2==1)&&!file_exists(myword.we_wordv[0]))
        goto done;
      for(n=0;n<n2;n++) {
        if((fe=file_exists(myword.we_wordv[0]))) {
          if(S_ISDIR(fe)) {
            char *d=file_compose_path(myword.we_wordv[n],"",NULL);
            if(d) {
              action_file_new(ac,d);
              free(d); }}
          else
            action_file_new(ac,myword.we_wordv[n]); }}}
    else
      E(2,"wordexp produced no results\n"); }
  else {
    switch(r) {
      case WRDE_BADCHAR:
        msg="wordexp: Incorrect use of newline or one of |, &, ;, <, >, (, ), {, }.";
        break;
      case WRDE_BADVAL:
        msg="wordexp: An undefined shell variable was referenced.";
        break;
      case WRDE_CMDSUB:
        msg="wordexp: Command substitution denied.";
        break;

      case WRDE_NOSPACE:
        msg="wordexp: Out of memory!";
        break;

      case WRDE_SYNTAX:
        msg="wordexp: Syntax error, please check for unbalanced parentheses or unmatched quotes!";
        break;

      default:
        msg="wordexp: Unknown error!?"; }

    if(errmsg)
      *errmsg=msg;
    else
      E(0,"%s\n",msg);

    return NULL; }

 done:
  wordfree(&myword);

#else
  int             flags=GLOB_MARK|GLOB_BRACE|GLOB_TILDE,c,n=0;
  glob_t          myglob;

  if(errmsg)
    *errmsg=NULL;

  if(!pattern)
    return NULL;

  if(!(c=glob(pattern,flags,NULL,&myglob))||(c==GLOB_NOMATCH)) {
    if(!(ac=action_context_new(0,NULL)))
      goto done;

    n=myglob.gl_pathc;

    for(c=0;c<n;c++)
      action_file_new(ac,myglob.gl_pathv[c]); }
  else {
    switch(c) {
      case GLOB_NOSPACE:
        msg="glob: Out of memory!";
        break;
      case GLOB_ABORTED:
        msg="glob: Read error!?";
        break;
/*    case GLOB_NOMATCH:
        msg="glob: no matches";
        break; */
      default:
        msg="glob: Unknown errror!?"; }
    if(errmsg)
      *errmsg=msg;
    else
      E(0,"%s\n",msg); }

 done:
  globfree(&myglob);
#endif

  return ac; }



/*****************************************************************************/
/* file execution */



/**
 * fork and execute a given command
 * @return a PID, or -1
 * @param ns  the session
 * @param x   an array of arguments for the new process, x[0] holding the name of the binary
 */
pid_t file_fork(void *ns,char **x) {
  pid_t r;
  if(!(r=fork())) {
    /* cd.  If a view has no clear cwd (like the tree-view), it is up to the
       view to set a sensible cwd before activating a file (like the directory
       the file to be activated resides in). */
    if(ns)
      chdir(noir_sess_get_cwd(ns));

#if 0
    close(1);

    if(open("/dev/null",O_RDWR)!=1)
      E(0,"could not open /dev/null!?\n");
    else {
      dup2(1,2);
#endif
      setsid();
      execvp(x[0],x);
#if 0
    }
#endif

    E(0,"failed to execute \"%s\"!? (%d - %s)\n",
      x[0],errno,strerror(errno));
    _exit(EXIT_FAILURE); }
  else if(r<0)
    E(0,"fork failed!?\n");
  else
    E(4,"fork done; parent returns.\n");
  return r; }



/**
 * run a command line
 * takes a command-line as system() would, but does not go through the shell
 * and hence does a lot less parsing
 * @return an error-code
 * @param ns  the session
 * @param p   the command-line
 */
int file_system(void *ns,const char *p) {
#ifdef HAVE_GLIB2_
  return g_spawn_command_line_async(p,NULL)?NOIR_SUCC:NOIR_FAIL;
#else
  int         n=0,c=0;
  char       *e,*b;
  char       *x[MAX_PARS+1];

  if(!p||!*p)
    return NOIR_IGNORED;

  if(!(b=strdup(p)))
    return NOIR_OOM;

  e=b;

  do {
    if(*e=='\"') {      /* quoted (words remain one compound item) */
      x[n++]=++e;       /* do not include quote into item */

      do {
        if((*e=='\\')&&e[1]) /* handle escaped character */
          memmove(e,e+1,strlen(e));
        e++;
      } while(*e&&*e!='\"');

      if(*e)            /* do not include unquote into item */
        *(e++)='\0'; }

    else {              /* not quoted */
      x[n++]=e;
      while(*e&&(*e!=' ')) {  /* element is word, goes on till next space */
        if(*e=='\\'&&e[1])    /* handle escaped character */
          memmove(e,e+1,strlen(e));
        e++; }}

    while(*e==' ')   /* drop spaces between items */
      *(e++)='\0';
  } while(*e);

  x[n]=NULL;         /* last element in exec vector is NULL */

  if(debug>=2) { /* 2 */
    c=0;
    E(-99,"%s\n",x[c++]);
    while(c<n)
      E(-99,"    |%s|\n",x[c++]);
    E(-99,"\n\n"); }

  file_fork(ns,x);

  free(b);

  return NOIR_SUCC;
#endif
}



/**
 * append an escaped item to the command-line file_run() is generating.
 * called by file_run() only.
 * @param a       a pre-allocated buffer to store the item in
 * @param escape  how many times to we have to escape the item?
 * @param what    the actual item
 */
static void file_run_append(char *a,int escape,const char *what) {
  *(a++)=' ';
  if(escape<0) {
    E(0,"won't unescape\n");
    return; }
  else if(!escape)
    strcpy(a,what);
  else if(escape==1) {
    *(a++)='\"';
    strcpy(a,what);
    a+=strlen(what);
    *(a++)='\"'; }
  else {
    int e;
    escape--;
    for(e=escape;e;e--) {
      *(a++)='\\';
      *(a++)='\"'; }
    strcpy(a,what);
    a+=strlen(what);
    for(e=escape;e;e--) {
      *(a++)='\\';
      *(a++)='\"'; }}
  *a='\0'; }



/**
 * make a command-line from template+URL, then run it using file_system()
 * This should be rewritten to use file_fork(); that way, it will handle
 * corner cases like "--foo=%s".
 * @return an error-code
 * @param ns        the session
 * @param template  a template containing %-sequences, or just the path to an application
 * @param url       NULL, a path, or an URL
 * @param mime      'url's MIME-type, or NULL
 */
int file_run(void *ns,const char *template,const char *url,const char *mime) {
  char       *p,*question=NULL,*encoded=NULL;
  const char *r,*b,*file;
  size_t      lc,lf,lh,lu,lq,le,lm;
  int         any=FALSE,fullurl,ret=NOIR_SUCC,escape=1,open=0;

/*  if(mime)
    mime=strdup(mime); */

  if(!url)
    url="";

  E(1,"template is |%s|, url |%s|, mime |%s|\n",template,url?url:"",mime?mime:"");

  file=file_if_local(url);
  lf=file?strlen(file):0;
  fullurl=(strstr(url,PROTOCOL_DELIMITER)!=NULL);
  lu=strlen(url)+(fullurl?0:7);
  lh=strlen(template)+1;
  lc=lh;
  lq=0;
  le=0;
  lm=0;

  if(fullurl) {
    if((question=strstr(url,PROTOCOL_DELIMITER))) {
      question+=sizeof(PROTOCOL_DELIMITER)-1;
      if((lq=strlen(question))) {
        unsigned char *x=question,*y,*n;
        int c=0;
        while(*x) {
          if((*x<' ')||(*x>127))
            c++;
          x++; }
        if((n=malloc(lq+1+2*c))) {
          char hex[]="0123456789abcdef";
          for(y=n,x=question;*x;x++) {
            if(*x==' ')
              *(y++)='+';
            else if((*x<' ')||(*x>127)) {
              *(y++)='%';
              *(y++)=hex[(*x>>4)%16];
              *(y++)=hex[*x%16]; }
            else
              *(y++)=*x; }
          *y='\0';
          le=strlen(n);
          encoded=n; }}}}

  r=template;



  /* calculate size of filled-in handler string */
  while(*r) {
    if(*r=='\"') {
      if(open) {
        open=0;
        if(escape)
          escape--; }
      else
        open=++escape;
      r++; }
    else if(!r[1])
      r++;
    else if(*r=='\\')
      r+=2;
    else if(*r=='%') {
      if(r[1]!='%') {
        any=TRUE;
        lc+=4*escape; }
      switch(r[1]) {
      case '%':
        break;
      case '\0':
        r--;
        break;
      case 'e':
      case 'E':
        lc+=le;
        break;
      case 'q':
      case 'Q':
        lc+=lq;
        break;
      case 's':
      case 'u':
      case 'U':
        lc+=lu;
        break;
      case 'f':
      case 'l':  /* app doesn't grok URLs, always give local file */
        if(file) /* it's a local file anyway, never mind */
          lc+=lf;
        else {   /* if data is abroad, D/L first */
          if((p=strrchr(url,DIR_DELIMITER)))
            lc+=strlen(temp)+strlen(p);
          else
            E(0,"really weird URL \"%s\" skipped...\n",url); }
        break;
      default:
        E(0,"unknown %%-sequence %%%c in handler \"%s\"\n",r[1],template); }
      r+=3; }
    else
      r++; }

  if((escape!=1)||open) {
    E(0,"broken quoting in |%s|  (%d,%d)\n",template,escape,open);
    return NOIR_FAIL; }

  if(!*url) {
    if(any) {
      E(0,"broken %%-sequence in |%s|\n",template);
      return NOIR_NODATA; }
    E(1,"identity -- handler still is: |%s|\n",template);
    return file_system(ns,template); }

  lc+=any?0:(lu+5); /* 5: "" around app, space, "" around arg */

  if(!(p=malloc(lc+1)))
    return NOIR_OOM;



  if(any) {
    b=r=template;
    *p='\0';

    while(*r) {
      if(*r=='\"') {
        if(open) {
          open=0;
          if(escape)
            escape--; }
        else
          open=++escape;
        r++; }
      else if(!r[1])
        r++;
      else if(*r=='\\')
        r+=2;
      else if(*r=='%') {
        strncat(p,b,r-b-1);
        b=r+2;

        switch(r[1]) {
        case '%':
          strcat(p,"%");
          break;
        case '\0':
          b--;
          r--;
          break;
        case 'q':
        case 'Q':
          file_run_append(p+strlen(p),escape,question?question:"");
          break;
        case 'e':
        case 'E':
          file_run_append(p+strlen(p),escape,encoded?encoded:"");
          break;
        case 's':
        case 'u':
        case 'U':
          if(!fullurl) {
            char *nn=NULL;
            esprintf(&nn,PROTOCOL_FILE "%s",url);
            if(nn) {
              file_run_append(p+strlen(p),escape,nn);
              free(nn); }}
          else
            file_run_append(p+strlen(p),escape,url);
          break;
        case 'l':
        case 'f':
          if(!file) {
            char   *x;
            size_t  lt;
            char   *t;
            int     rcp=NOIR_FAIL;

            if((x=strrchr(url,DIR_DELIMITER))) {
              lt=strlen(temp)+strlen(x);

              if(!(t=malloc(lt+1))) {
                free(p);
                return NOIR_OOM; }

              snprintf(t,lt+1,"%s%s",temp,x);
              if((rcp=file_cp(url,t))!=NOIR_SUCC) {
                E(0,"failed to download \"%s\" to \"%s\": %d...\n",url,t,rcp);
                free(p);
                free(t);
                return rcp; }
              else
                E(1,"downloaded \"%s\" to \"%s\"...\n",url,t);

              file_run_append(p+strlen(p),escape,t);
              free(t); }}

          else { /* file's local, anyway */
            E(2,"\"%s\" is a local file...\n",file);
            file_run_append(p+strlen(p),escape,file); }
          break; }
        r+=2; }
      else
        r++; }

    if(*b)
      strcat(p,b); }

  else if(strchr(template,' '))
    snprintf(p,lc,"\"%s\" \"%s\"",template,url);
  else
    snprintf(p,lc,"%s \"%s\"",template,url);

  E(1,"filled in handler is |%s|\n",p);

  ret=file_system(ns,p);

  if(encoded)
    free(encoded);
  if(p)
    free(p);
/*
  if(mime)
    free(mime); */

  return ret; }



/**
 * open 'file' of type 'mime' using 'app' or the backend's default
 * @return an error-code
 * @param ns    the session
 * @param file  the file to start
 * @param mime  it's MIME-type (may be NULL if 'app' is supplied or 'file' is executable)
 * @param app   the application to start the file with, or NULL to use the backend's defaults
 */
int file_activate(void *ns,const char *file,const char *mime,const char *app) {
  int ret=NOIR_IGNORED;

  if(!file)
    return NOIR_NODATA;

  if(!mime)
    mime=file_get_mime(file);

  if(app) {            /* app to start the (data-) file with was given */
    int ms=file_exists(app);
    if(!S_ISREG(ms))   /* app isn't a file (or doesn't even exist) */
      ret=NOIR_NOFILE;
    else if(!(ms&(S_IXUSR|S_IXGRP|S_IXOTH)))  /* app isn't executable */
      ret=NOIR_NOEXEC;
    else               /* all is well, run app with file as data */
      ret=file_run(NULL,app,file,mime); }
  else                 /* no app was given, use default handler */
    ret=ff.file_activate(file,mime);

  switch(ret) {
    case NOIR_NOEXEC:
      form_alert(ns,"Target \"%s\" is not an executable file...",app);
      break;
    case NOIR_NOTSUPP:
      form_alert(ns,"Action not supported on \"%s\"...",file);
      break;
    case NOIR_NOFILE:
   /* form_alert(ns,"Target \"%s\" not found...",app); */
      form_stock(ns,ALERT_STALE);
      break;
    case NOIR_FAIL:
      if(app)
        form_alert(ns,"Handing \"%s\" of type \"%s\" to \"%s\" failed.",file,mime?mime:"*",app);
      else
        form_alert(ns,"File \"%s\" is not a binary executable, and no default-handler for its MIME-type \"%s\" was declared.",file,mime?mime:"*");
/*    form_stock(ns,ALERT_CANNOT_START); */
      break;
    case NOIR_SUCC:
      break;
    default:
      form_alert(ns,"Error #%d activating \"%s\"...",-ret,file); }

  return ret; }



/*****************************************************************************/
/* calling the backend */



/**
 * add POSIX/stat() meta-data to the list of meta-data elements
 * called from file_get_meta()
 * @return an error-code
 * @param ns    the session
 * @param info  the file-info element to process
 * @param md    where the MD list lives (*md may be NULL, but md mustn't be)
 */
static inline int file_info_to_meta(void *ns,file_info *info,noir_md *md) {
#define TREE_TIME_MAX    64

  int            ret=NOIR_FAIL;
  struct tm      mktm;
  char           time[TREE_TIME_MAX];
  char          *action,*mm=NULL;
  noir_provider *l=NULL;
  posixmd       *i=NULL;

  if(!md||!info)
    return NOIR_NODATA;

  if(info->name)
    meta_add(md,META_RW,META_UTF8,"Name","The file's name",MY_NAME,info->name,l);

  /* tag is read-only (as a policy decision; in practice, it should also handle
     read/write correctly, including tag being updated if name-change causes a
     rename and vice versa. */
  if(info->tag)
    meta_add(md,META_RO,META_STR,"Tag","How to retrieve the file",MY_NAME,info->tag,l);

  if((i=info->intrinsic)) {
    mode_t   m=i->mode;
    estring *it=estring_new(2048);

    ret=NOIR_SUCC;

    estring_printf(it,"%c%c%c",m&S_IRUSR?'r':'-',
                   m&S_IWUSR?'w':'-',
                   m&S_ISUID?'s':(m&S_IXUSR?'x':'-'));
    estring_appendf(it," %s",get_username(i->uid));
    meta_add(md,META_RW,META_STR,"User","Permissions for the file's owner",MY_NAME,ESTRING_GET_CSTRING(it),l);

    estring_printf(it,"%c%c%c",m&S_IRGRP?'r':'-',
                   m&S_IWGRP?'w':'-',
                   m&S_ISGID?'s':(m&S_IXGRP?'x':'-'));
    estring_appendf(it," %s",get_groupname(i->gid));
    meta_add(md,META_RW,META_STR,"Group","Permissions for people in one of the file's groups",MY_NAME,ESTRING_GET_CSTRING(it),l);

    estring_printf(it,"%c%c%c",m&S_IROTH?'r':'-',
                   m&S_IWOTH?'w':'-',
                   m&S_ISVTX?'t':(m&S_IXOTH?'x':'-'));
    meta_add(md,META_RW,META_STR,"Other","Permissions for people not the file's creator nor in one of the file's groups",MY_NAME,ESTRING_GET_CSTRING(it),l);

    if(i->lmode) {
      char *linkee=file_readlink(info->tag);
      if(linkee) {
        meta_add(md,META_RW,META_STR,"Linkee","The selected file is really a reference to this file",MY_NAME,linkee,l);
        free(linkee); }

      estring_printf(it,"link: 0%o 0x%x, linkee 0%o 0x%x",i->lmode,i->lmode,i->mode,i->mode);
      meta_add(md,META_RO,META_STR,"Mode","Mode of file and link as octal and hex",MY_NAME,ESTRING_GET_CSTRING(it),l); }
    else {
      estring_printf(it,"0%o 0x%x",i->mode,i->mode);
      meta_add(md,META_RO,META_STR,"Mode","Mode of file as octal and hex",MY_NAME,ESTRING_GET_CSTRING(it),l); }

    if(S_ISDIR(i->mode)||info->children) {
      int sub=i->hardlinks
#ifdef WITH_DOT_DIR
        -1
#else
        -2
#endif
        ,files=(info->children>sub)?info->children-sub:0;

      if(info->flags&IF_DOTDIR)
        estring_printf(it,"%d item%s",
                       info->children,info->children==1?"":"s");

      else if(info->children)
        estring_printf(it,"%d folder%s, %d file%s",
                       sub,sub==1?"":"s",
                       files,files==1?"":"s");
      else
        estring_printf(it,"%d folder%s",
                       sub,sub==1?"":"s"); }

    else {
      estring_printf(it,"%ld",i->size);
      unit_size(ESTRING_GET_CSTRING(it)); }

    meta_add(md,META_RO,META_STR,"Size","The size of the file",MY_NAME,ESTRING_GET_CSTRING(it),l);

    estring_printf(it,"F:0x%x H:0x%x",info->flags,info->have);
    meta_add(md,META_RO,META_STR,"Flags","Evidence flags",MY_NAME,ESTRING_GET_CSTRING(it),l);

    localtime_r(&i->mtime,&mktm);
    strftime(time,TREE_TIME_MAX,"%Y-%m-%d %H:%M",&mktm);
    meta_add(md,META_RW,META_STR,"MTime","The time the file's data were last modified",MY_NAME,time,l);

    localtime_r(&i->atime,&mktm);
    strftime(time,TREE_TIME_MAX,"%Y-%m-%d %H:%M",&mktm);
    meta_add(md,META_RW,META_STR,"ATime","The time the file's data were last read",MY_NAME,time,l);

    localtime_r(&i->ctime,&mktm);
    strftime(time,TREE_TIME_MAX,"%Y-%m-%d %H:%M",&mktm);
    meta_add(md,META_RO,META_STR,"CTime","The time the file's meta-data (permissions etc.) were last modified",MY_NAME,time,l);

    if(!(S_ISDIR(m))&&(i->hardlinks>1)) {
      estring_printf(it,"%d",i->hardlinks);
      meta_add(md,META_RO,META_STR,"Links","How many names (\"hardlinks\") the files has",MY_NAME,ESTRING_GET_CSTRING(it),l); }

    estring_dst(it); }

/* meta_add(md,(!info->mime||(strchr(info->mime,MIME_DELIMITER)!=NULL))?META_RW:META_RO,META_STR,"MIME","The file's MIME-type",MY_NAME,info->mime?info->mime:"",l); */
  meta_add(md,META_RO,META_STR,"MIME","The file's MIME-type",MY_NAME,info->mime?info->mime:"",l);

  if(info->tag)
    meta_add(md,META_RW,META_STR,"Icon","The path and name of the icon representing the file.",MY_NAME,icon_path_get_always(info->tag,info->mime),l);
  if((action=ff.file_mime_handler_get(info->mime,&mm))) {
    if(!strcmp(info->mime,mm)||
       (strcmp(info->mime,MIME_EXECUTABLE)&&
        strcmp(info->mime,MIME_SCRIPT))) {
      meta_add(md,META_RO,META_STR,"Action","What happens when double-clicking the icon.",MY_NAME,action,l);
      if(strcmp(info->mime,mm))
        meta_add(md,META_RO,META_STR,"Match","When double-clicking the icon, the action for this MIME-type matches.",MY_NAME,mm,l); }
    free(mm); }

  return ret; }



/**
 * get all meta-data (including those provided by plugins) for a given file
 * usually for displaying it in a tooltip or properties dialog
 * function will work on a copy of the file-info, hence if a mutex is given,
 * the model is locked briefly while copying the existing file-info, then
 * unlocked for however long it takes to acquire the meta-data, and finally
 * locked again to copy the attributes over to the caller's file-info element.
 *
 * @return an error-code
 * @param info   the file-info to store the resulting info on
 * @param mutex  the mutex of the model to lock while changing the file-info element
 */
int file_get_meta_with_lock(file_info *info,pthread_mutex_t *mutex) {
  noir_meta       *m;
  int              ret=NOIR_FAIL;
  file_info       *fi=NULL;
  noir_provider   *prv=NULL;
  file_annotation *fa=NULL;
  const char      *mime;

  if(!(info->have&FF_MIME)) {
    if(!file_info_set_mime(info,file_get_mime(file_info_get_tag(info))))
      E(0,"failed to set MIME-type for \"%s\"!?\n",file_info_get_tag(info)); }

  if(!(fi=file_info_copy(info)))
    return NOIR_OOM;

  if(!file_info_set_meta(fi,meta_new()))
    return NOIR_OOM;

  if(mutex)
    pthread_mutex_unlock(mutex);

  /* prv=meta_provider_get(info->mime); */

  if((ret=file_info_to_meta(NULL,fi,fi->meta))!=NOIR_SUCC)    /* intrinsics */
    goto done;

  if((fa=file_annotation_get(fi->tag))) {
    char *at[]={ "none!?", "user", "media", "root", "unknown" };
    int   an=sizeof(at)/sizeof(char *);
    if(fa->lvl<an)
      an=fa->lvl;
    else
      an--;
    meta_add(fi->meta,META_RO,META_STR,"Marker","Annotation: The type of this item (removabled, administrator-only, ...)",MY_NAME,at[an],prv);
    if(fa->sub)
      meta_add(fi->meta,META_RO,META_STR,"sortkey","If you enable 'Sort by meta-data', files will be sorted according to this field.",MY_NAME,fa->sub,prv);
    if(fa->dsc)
      meta_add(fi->meta,META_RO,META_UTF8,"Annotation","Annotation: A description of this item",MY_NAME,fa->dsc,prv); }


  if(file_info_get_thumbfile(fi))
    meta_add(fi->meta,META_RO,META_STR,"Thumbnail","Where the preview 'thumbnail' for this item lives.",MY_NAME,file_info_get_thumbfile(fi),prv);

  if(ff.file_get_meta)                         /* add'l data from backend (ACLs, EAs, ...) */
    ff.file_get_meta(NULL,fi->tag,fi->meta);

  if(!(mime=file_info_get_mime(fi))||
     (!strcmp(mime,MIME_CHAR)||!strcmp(mime,MIME_BLK)||
      !strcmp(mime,MIME_FIFO)||!strcmp(mime,MIME_SOCKET)))
    E(2,"skipping meta-data providers for %s \"%s\"...\n",mime?mime:"unknown item",file_info_get_tag(fi));
  else
    meta_handle_file(prv,fi->tag,fi->meta);     /* meta-data plugins */

  if(fi->meta) {
    if((m=meta_in_group(fi->meta,"sortkey",NULL))&&meta_get_val(m)) {
      if(fi->sortkey)
        free(fi->sortkey);
/* E(4,"%s :: %s\n",fi->tag,(char *)m->val); */
      fi->sortkey=strdup(meta_get_val(m)); }

    fi->have|=FF_META; }
  else
    fi->have&=(~FF_META);

 done:
  /* move over meta-data */
  if(mutex)
    pthread_mutex_lock(mutex);

  file_info_set_meta(info,fi->meta);
  info->have|=FF_UNSEEN;
  fi->have&=(~FF_META);
  fi->meta=NULL;

  /* move over sort-key */
  if(info->sortkey)
    free(info->sortkey);
  info->sortkey=fi->sortkey;
  fi->sortkey=NULL;

  file_info_unref(fi);

  return ret; }



/**
 * get all meta-data (including those provided by plugins) for a given file
 * usually for displaying it in a tooltip or properties dialog
 * @return an error-code
 * @param ns    the session
 * @param file  the file we're interested in
 * @param md    where to store a pointer to the resulting meta-data
 */
int file_get_meta_only(void *ns,const char *file,noir_md **md) {
  int          ret=NOIR_FAIL;
  file_info   *info=NULL,*fi;

#define REQUIRE (FF_MIME|FF_STAT)

  if(!file||!md)
    return NOIR_FAIL;

  /* if we already have intel on this file, reuse that */
  if((fi=file_info_fetch(NULL,file)))
    info=file_info_copy(fi);

  /* otherwise, try to get it now */
  if(!info||((info->have&REQUIRE)!=REQUIRE)) {
    if(((ret=ff.file_get_info(ns,file,&info))!=NOIR_SUCC)||!info)
      return ret; }

  /* now, get meta-data */
  if(((ret=file_get_meta_with_lock(info,NULL))!=NOIR_SUCC)||!info||!info->meta)
    goto free_info;

  *md=info->meta;
  info->meta=NULL;  /* remove from file-info so it doesn't get deref'd */

 free_info:
  file_info_unref(info);

  return ret; }



/**
 * set meta-data that no plugin exists for on a given file, using the backend
 * this will take care of meta-data intrinsic to evidence and those handled by
 * the file-system backend, but will not handle data created by meta-data
 * plugins.
 * @return an error-code
 * @param ns        the session
 * @param filename  the file we want to update
 * @param md        the meta-data to set
 */
static int file_set_meta(void *ns,const char *filename,noir_md *md) {
  int        ret=NOIR_IGNORED;
  noir_meta *r,  /* current */
            *f;  /* file-name */
  char      *p,*q;
  file_info *fi=NULL;

  if(!md)
    return NOIR_FAIL;

  if(!((f=meta_in_group(md,"Tag",MY_NAME))||
       (f=meta_in_group(md,"Tag",META_DFLT_GRP))||
       (f=meta_in_group(md,"Tag",NULL))))
    return NOIR_NODATA;

  if(!meta_get_1st(md))
    return NOIR_NODATA;

  /* remove meta-data from file, otherwise a file-update event might modify
     the MD while we're still traversing it.  potential for an ugly race right
     there.  however, removing the MD here is safe because we haven't actually
     made any changes in the file-system yet, so there cannot have been any
     update-events. */
  if((fi=file_info_fetch(NULL,filename))) {
    noir_md *tmp=meta_clone(md);
    file_info_set_meta(fi,NULL);
    md=tmp; }

  r=meta_get_1st(md);  /* do it here, md changes between prvs check and here! */

  while(r) {
    filename=(char *)meta_get_val(f);  /* get everytime, may change throughout */

    /* NAME */
    if((meta_get_flags(r)==META_DO)&&!meta_get_provider(r)&&meta_get_val(r) /* &&strlen(meta_get_val(r)) */ ) {
      E(0,"(%s): \"%s\"...\n",filename,meta_get_key(r));

      if(!strcasecmp(meta_get_key(r),"name")) {
        char   *dn=file_dirname(filename),*nn=NULL,*lcl=NULL;

        if(dn&&(nn=file_compose_path(dn,meta_get_val(r),NULL))) {
          lcl=(meta_get_type(r)==META_UTF8)?e2locale(nn):strdup(nn);
          if(lcl) {
            noir_meta *x;
            E(0,"mv \"%s\" \"%s\"\n",filename,lcl);
            if(file_mv(filename,lcl)!=NOIR_SUCC)
              form_alert(ns,"Could not move/rename \"%s\" to \"%s\"...",filename,lcl);
            else if((x=f)) {
              meta_set_string(x,nn,META_OLD);
              if(meta_get_flags(x)==META_DO)
                meta_set_flags(x,META_RW); }}}

        if(dn)
          free(dn);
        if(lcl)
          free(lcl);
        if(nn)
          free(nn); }

      /* TAG -- disabled in frontend for GUI consistency */
      else if(!strcasecmp(meta_get_key(r),"tag")) {
        noir_meta *x;

        E(1,"mv \"%s\" \"%s\"\n",(char *)meta_get_prvs(r),(char *)meta_get_val(r));

        /* we don't need UTF-8 (like we did for "name") as tag is opaque,
           whatever we got from the backend, we'll hand to the backend */
        if(meta_get_type(r)==META_UTF8)
          E(0,"tag is in UTF8!?  this should never happen.\n");
        if(file_mv(meta_get_prvs(r),meta_get_val(r))!=NOIR_SUCC)
          form_alert(ns,"Could not move/rename \"%s\" to \"%s\"...",meta_get_prvs(r),meta_get_val(r));
        else if((x=meta_in_group(md,"name",MY_NAME))) {
          if((p=strrchr((char *)meta_get_val(r),DIR_DELIMITER)))
            p++;
          else
            p=(char *)meta_get_val(r);
          meta_set_string(x,p,META_OLD);
          if(meta_get_flags(x)==META_DO)
            meta_set_flags(x,META_RW); }}

      /* ICON */
      else if(!strcasecmp(meta_get_key(r),"icon")) {
        /* p:  what to set the icon for
           q:  the path to the icon to use */
        noir_meta *m=meta_in_group(md,"MIME",MY_NAME);
        p=(char *)filename;
        q=meta_get_val(r);
        while(q[1]=='/')   /* ??? FIXME */
          q++;
        if(m) {
          int ret=form_dial(ns,ALERT_QUESTION,ALERT_BUTTONS_CUSTOM,"Set this icon for...","this file only|all files of this MIME-type");
          if(ret==2)
            p=meta_get_val(m); }
        icon_path_del(p);
        if(strlen(q))
          icon_path_add(p,q);
        icon_path_save(); }

      /* LINKEE */
      else if(!strcasecmp(meta_get_key(r),"linkee")) {
        if(file_rm(filename)==NOIR_SUCC) {
          if(file_symlink(filename,meta_get_val(r))!=NOIR_SUCC) {
            form_alert(ns,"Cannot create new link \"%s\"...",meta_get_val(r));
            if(file_symlink(filename,meta_get_prvs(r))!=NOIR_SUCC)
              form_alert(ns,"Cannot recreate old link \"%s\"...",meta_get_prvs(r)); }}
        else
          form_alert(ns,"Cannot remove old link \"%s\"...",filename); }

      /* FILE-SYSTEM SPECIFIC, set in the backend */
      else {
        int br=ff.file_set_meta(ns,filename,md,r);
        file_partial(((br==NOIR_SUCC)?br:NOIR_FAIL),&ret);
        if(br==NOIR_NOTFOUND)
          E(0,"unknown \"" MY_NAME "\" item %s=%s\n",meta_get_key(r),(char *)meta_get_val(r));
        /* now, reset current element to beginning of chain (which in turn may
           have become NULL) because the backend may have changed the chain. */
        r=meta_get_1st(md); }}

    if(r)  /* failsafe in case the chain gets emptied somewhere */
      r=meta_get_next(r); }

  meta_dst(&md);

  if(fi)
    file_get_info(ns,filename,&fi);

  return ((ret==NOIR_IGNORED)?NOIR_SUCC:ret); }



/**
 * wrapper for file_set_meta() to be called from meta_update() in meta.c
 * set meta-data that no plugin exists for on a given file, using the backend.
 * this will take care of meta-data intrinsic to evidence and those handled by
 * the file-system backend, but will not handle data created by meta-data
 * plugins.
 * @return an error-code
 * @param md    the meta-data to set
 * @param file  the file we want to update
 * @param grp   the group of meta-data we're supposed to handle now
 */
int meta_internal(noir_md *md,const char *file,const char *grp) {
  return file_set_meta(NULL,file,md); }



/*****************************************************************************/



/**
 * get a file's MIME-type
 * @return a static string describing the file's MIME-type which must not be free()d
 * @param file  the file we want to investigate
 */
const char *file_get_mime(const char *file) {
  const char *mime=NULL,*alias=NULL;
#ifdef WITH_DOT_DIR
  const char *fn=file_name(file);
  if(fn&&!strcmp(fn,DIR_MYSELF))
    mime=MIME_VFOLDER;
  else
#endif
  if(!(mime=ff.file_get_mime(file)))
    return NULL;
  return (ht_mime_alii&&(alias=hash_table_fetch(ht_mime_alii,mime)))?alias:mime; }



/**
 * check whether an item exists in the file-system
 * while bootstrapping (loading the actual file-system backend), this
 * function is not there yet, so we'll emulate it using stat().  as
 * a result, you cannot bootstrap fsxs.so over ftp/http/...  ; )
 * @return 0 for not found, the file's mode as per stat() otherwise
 * @param file  the item to check for
 */
int file_exists(const char *file) {
  file_info   *fi;
  struct stat  st;
  const char  *f=file_if_local(file);

  if(f&&(f[0]!=DIR_DELIMITER)) {
    E(0,"(\"%s\"): non-absolute path!\n",file);
    return 0; }

#ifdef HAVE_BTRACE
  if(!strcmp(file,"/usr/share/evidence/icons/indigo_128/image/image_svg.png")) {
    int a=1,b=0;
    btrace_print();
    printf("%d\n",a/b); }
#endif

  /* look in model first, save file-xs.  (necessary for vfolders, as well!) */
  if((fi=file_info_fetch(NULL,file))&&((st.st_mode=file_info_get_mode(fi))>0))
    return st.st_mode;
  else if(!f)
    return 0;
  else if(ff.file_exists)
    return ff.file_exists(f);
  else if(!stat(f,&st))
    return st.st_mode;
  return 0; }



/**
 * get "intrinsic" info on file (POSIX/stat() meta-data)
 * @return an error-code
 * @param ns    the session
 * @param file  the file we want the info for
 * @param info  where to store the info. if(!*info), a new file_info element will be allocated. likewise, if(!*info->intrinsic), a posixmd element will be allocated.
 */
int file_get_info(void *ns,const char *file,file_info **info) {
  int ret=ff.file_get_info(ns,file,info);
#ifdef WITH_DOTDIR
#endif
  return ret; }



/**
 * are source and target items (in copy, move, ...) the same?
 * heads up -- this will return same for /foo/bar/baz and /foo/bar/baz,
 * but also for /foo/bar/baz and /foo/bar (bar being a folder), so we
 * can detect "copy/move on top of itself" cases
 * @return TRUE if items are identical, FALSE otherwise
 * @param file         the source-file
 * @param file_or_dir  the target-file
 */
int file_is_same(const char *file,const char *file_or_dir) {
  int     ret=FALSE;
  size_t  la,lb;
  char   *a,*b,*p;
  char    buf[PATH_MAX];

  /* realpath() if local, then turn into URL */
  if((p=(char *)file_if_local(file))) {
    if((p=realpath(p,buf)))
      file=p; }
  a=file_to_URL(file);

  if((p=(char *)file_if_local(file_or_dir))) {
    if((p=realpath(p,buf)))
      file_or_dir=p; }
  b=file_to_URL(file_or_dir);

  /* sanity check */
  if(!a||!b) {
    E(0,"(%s,%s): invalid URLs (%s,%s)\n",file,file_or_dir,a?a:"none",b?b:"none");
    ret=(a==b);
    goto done; }

  /* same? */
  if(!strcmp(a,b))
    return TRUE;

  /* special case: if B is a directory and A is an item in B, return TRUE */
  la=strlen(a);
  lb=strlen(b);

  p=strrchr(a,DIR_DELIMITER);

  if((la>lb)&&(&a[lb]==p)&&!strncmp(a,b,lb)&&
     S_ISDIR(file_exists(b)))
    ret=TRUE;

  /* cleanup */
 done:
  if(a)
    free(a);
  if(b)
    free(b);

  return ret; }



/**
 * move item or items to folder, or rename item
 * @return an error-code
 * @param job  the job
 */
int file_mv_do(noir_job *job) {
  const char  *file;
  int          ret=NOIR_IGNORED,rt2;
  action_file *af=action_context_get_1st(job->ac);

  while((file=action_file_get_tag(af))) {
    if(!strlen(file)) {
      E(0,"empty file-name!?  skipped!\n");
      goto next; }
    if(fi_param.prescan_mv)
      file_job_scan(job,file);
    file_job_paths(job,file,NULL,NULL,NULL);
    if(file_is_same(file,job->dst_path)) {
      char *t=strdup(file);
      if(t) {
        int destroy=NOIR_SUCC;
        do {
          destroy=NOIR_SUCC;
          file_job_edt(job,"move",&t);
          if(strcmp(t,file)) {
            if(file_exists(t))
              destroy=file_job_ask(job,NULL,"There already is a file of that name.\nReplace it with this one?");
            if(destroy==NOIR_SUCC) {
              char *tmp;
              file_job_paths(job,NULL,t,NULL,&tmp);
              if((rt2=ff.file_mv(job))!=NOIR_SUCC)
                file_job_ask(job,"Oy","Could not move \"%s\" to \"%s\":\n%s",file,t,strerror(errno));
              else
                E(0,"mv \"%s\" \"%s\"\n",file,t);
              file_job_paths(job,NULL,tmp,NULL,&t);
              file_partial(rt2,&ret); }
            else {
              free(t);
              t=strdup(file); }}
          else
            E(0,"file name not changed, src (%s) still same as "
                           "target (%s), skipping...\n",file,t);
        } while(destroy!=NOIR_SUCC);
        free(t); }
      else
        E(0,"Out memory in file-API!"); }
    else {
      rt2=ff.file_mv(job);
      file_job_update(job,NOIR_JOB_PROGRESS,rt2,NULL,NULL);
      if(rt2!=NOIR_SUCC)
        file_job_ask(job,"Oy","Could not move \"%s\" to \"%s\":\n%s",file,job->dst,strerror(errno));
      else
        E(1,"mv \"%s\" \"%s\"\n",file,job->dst);
      file_partial(rt2,&ret); }
   next:
    af=action_file_get_next(af); }
  return (job->err=ret); }



/**
 * copy item or items to folder, or copy item renaming the copy
 * @return an error-code
 * @param job  the job
 */
int file_cp_do(noir_job *job) {
  const char  *file;
  int          ret=NOIR_IGNORED,rt2;
  action_file *af=action_context_get_1st(job->ac);

  while((file=action_file_get_tag(af))) {
    if(!strlen(file)) {
      E(0,"empty file-name!?  skipped!\n");
      goto next; }
    if(fi_param.prescan_cp)
      file_job_scan(job,file);
    file_job_paths(job,file,NULL,NULL,NULL);
    if(file_is_same(file,job->dst_path)) {
      char *t=strdup(file);
      if(t) {
        int destroy;
        do {
          destroy=NOIR_SUCC;
          file_job_edt(job,"copy",&t);
          if(strcmp(t,file)) {
            if(file_exists(t))
              destroy=file_job_ask(job,NULL,"There already is a file of that name.\nReplace it with a copy of this one?");
            if(destroy==NOIR_SUCC) {
              char *tmp;
              file_job_paths(job,NULL,t,NULL,&tmp);
              if((rt2=ff.file_cp(job))!=NOIR_SUCC)
                file_job_ask(job,"Oy","Could not copy \"%s\" to \"%s\":\n%s",file,t,strerror(errno));
              else
                E(0,"cp \"%s\" \"%s\"\n",file,t);
              file_job_paths(job,NULL,tmp,NULL,&t);
              file_partial(rt2,&ret); }
            else {
              free(t);
              t=strdup(file); }}
          else
            E(0,"file name not changed, src (%s) still same as "
                "target (%s), skipping...\n",file,t);
        } while(destroy!=NOIR_SUCC);
        free(t); }
      else
        E(0,"Out memory in file-API!"); }
    else {
      rt2=ff.file_cp(job);
      file_job_update(job,NOIR_JOB_PROGRESS,rt2,NULL,NULL);
      if(rt2!=NOIR_SUCC)
        file_job_ask(job,"Oy","Couldn't copy \"%s\" to \"%s\":\n%s",file,job->dst,(rt2==NOIR_NOTSUPP)?"operation not supported (maybe backend was built without support for remote URLs?)":strerror(errno));
      else
        E(1,"cp \"%s\" \"%s\"\n",file,job->dst);
      file_partial(rt2,&ret); }
   next:
    af=action_file_get_next(af); }
  return (job->err=ret); }



/**
 * remove an item from the file-system (including any items it may contain)
 * @return an error-code
 * @param job  the job
 */
int file_rm_do(noir_job *job) {
  const char  *file;
  char        *p;
  int          ret=NOIR_IGNORED,rt2;
  action_file *af=action_context_get_1st(job->ac);

  while((file=action_file_get_tag(af))) {
    if(!strlen(file)) {
      E(0,"empty file-name!?  skipped!\n");
      goto next; }
    else if(file_if_dotdir(file)) {
      E(0,"will not delete \"%s\"!  skipped!\n",file);
      goto next; }

    if(fi_param.prescan_rm)
      file_job_scan(job,file);
    file_job_paths(job,file,NULL,NULL,NULL);
    if(file_if_local(file)&&!strcmp(file_if_local(file),"/"))
      file_job_ask(job,"Fair enough, officer","No!\nYou won't delete / on my shift!");
    else {
      if((rt2=ff.file_rm(job))!=NOIR_SUCC) {
        E(1,"rm -rf \"%s\" => %d\n",file,rt2); }
      file_job_update(job,NOIR_JOB_PROGRESS,rt2,NULL,NULL);
      file_partial(rt2,&ret); }
   next:
    af=action_file_get_next(af); }

  if((p=action_context_files_to_quoted_string(job->ac))) {
    if(ret==NOIR_FAIL)
      file_job_ask(job,"Oy","Could not delete files:\n%s\n%s\n(%s)",file?file:"???",p,strerror(errno));
    else if(ret==NOIR_PARTIAL)
      file_job_ask(job,"Oy","Could not delete all files:\n%s\n%s\n(%s)",file?file:"???",p,strerror(errno));
    free(p); }

  return (job->err=ret); }



/**
 * remove an item from the file-system (including any items it may contain)  (sync wrapper)
 * @return an error-code
 * @param fm  the item to remove
 */
int file_rm(const char *fm) {
  int             ret=NOIR_OOM;
  action_context *ac=action_context_new2(fm,NULL);

  if(ac) {
    noir_job     *job=file_job_new(NULL,NOIR_JOB_RM,ac,NULL);
    if(job) {
      job->callback=NULL;
      file_job_update(job,NOIR_JOB_START,NOIR_SUCC,NULL,NULL);
      ret=file_rm_do(job);
      file_job_update(job,NOIR_JOB_DONE,NOIR_SUCC,NULL,NULL); }
    else
      action_context_dst(ac); }
  return ret; }



/**
 * copy item or items to folder, or copy item renaming the copy (sync wrapper)
 * @return an error-code
 * @param fm  source file
 * @param to  a target-folder, or a new file-name if 'fm' holds only one item
 */
int file_cp(const char *fm,const char *to) {
  int             ret=NOIR_OOM;
  action_context *ac=action_context_new2(fm,to);
  if(ac) {
    noir_job     *job=file_job_new(NULL,NOIR_JOB_CP,ac,NULL);
    if(job) {
      job->callback=NULL;
      file_job_update(job,NOIR_JOB_START,NOIR_SUCC,NULL,to);
      ret=file_mv_do(job);
      file_job_update(job,NOIR_JOB_DONE,NOIR_SUCC,NULL,NULL); }
    else
      action_context_dst(ac); }
  return ret; }



/**
 * move item or items to folder, or rename item (sync wrapper)
 * @return an error-code
 * @param fm  source-file
 * @param to  a target-folder, or a new file-name if 'fm' holds only one item
 */
int file_mv(const char *fm,const char *to) {
  int             ret=NOIR_OOM;
  action_context *ac=action_context_new2(fm,to);
  if(ac) {
    noir_job     *job=file_job_new(NULL,NOIR_JOB_MV,ac,NULL);
    if(job) {
      job->callback=NULL;
      file_job_update(job,NOIR_JOB_START,NOIR_SUCC,NULL,to);
      ret=file_mv_do(job);
      file_job_update(job,NOIR_JOB_DONE,NOIR_SUCC,NULL,NULL); }
    else
      action_context_dst(ac); }
  return ret; }



/**
 * make directory
 * @return an error-code
 * @param dir    the path to create
 * @param perms  the permissions for the path to be created
 * @param hier   FALSE: try to create last element in path only; TRUE: try to create any missing items along the path
 */
int file_mkdir(const char *dir,int perms,int hier) {
  return ff.file_mkdir(dir,perms,hier); }



/**
 * remove an empty directory
 * @return an error-code
 * @param dir  the directory to remove
 */
int file_rmdir(const char *dir) {
  return ff.file_rmdir(dir); }



/**
 * sym- or hardlink an item. check whether source and target are same.
 * @return an error-code
 * @param sym  TRUE for symlink, FALSE for hardlink
 * @param fm   existing item
 * @param to   new name for the item
 */
int file_link(int sym,const char *fm,const char *to) {
  int ret=NOIR_IGNORED;

  if(file_is_same(fm,to)) {
    char     *t;

    if((t=strdup(fm))) {
      int destroy;
      do {
        destroy=NOIR_SUCC;
        form_file_edit("You are about to create a link for an object\nin the same folder the object is in.\nThey can't both have the same name.\n\nPlease give a name for the link!",&t);
        if(strcmp(t,fm)) {
          if(file_exists(t))
            destroy=form_yes_no(NULL,"There already is a file of that name.\nReplace it with a link?");
          if(destroy==NOIR_SUCC)
            ret=sym?ff.file_symlink(fm,t):ff.file_hardlink(fm,t); }
        else
          E(0,"file name not changed, src (%s) still same as "
            "target (%s), skipping...\n",fm,t);
      } while(destroy!=NOIR_SUCC);
      free(t); }}

  else
    ret=sym?ff.file_symlink(fm,to):ff.file_hardlink(fm,to);

  return ret; }



/**
 * symlink
 * @return an error-code
 * @param fm  from item...
 * @param to  ...to item
 */
int file_symlink(const char *fm,const char *to) {
  E(2,"ln -s \"%s\" \"%s\"\n",fm,to);
  return ff.file_symlink(fm,to); }



/**
 * hardlink
 * @return an error-code
 * @param fm  from item...
 * @param to  ...to item
 */
int file_hardlink(const char *fm,const char *to) {
  E(2,"ln \"%s\" \"%s\"\n",fm,to);
  return ff.file_hardlink(fm,to); }



/**
 * where does this symlink point to?
 * @return a newly-allocated tag describing the target of the symlink, or NULL
 * @param file  where to find the symlink in the file-system
 */
char *file_readlink(const char *file) {
  if(ff.file_readlink)
    return ff.file_readlink(file);
  else
    return NULL; }



/**
 * get a file's name (UTF8) from its tag (URL, UUID, ...)
 * @return the file's name, in UTF8
 * @param tag  the file's tag (whatever that means for this backend)
 */
char *file_tag2name(const char *tag) {
  return ff.file_tag2name(tag); }



/**
 * activate directory-scanner in backend
 * @return an error-code
 */
int file_scanner_activate(q_head *qh) {
  int ret;

  if(!ff.file_scanner_activate)
    return NOIR_NOTSUPP;
  if(!qh->anchor) {
    E(1,"nothing to scan...\n");
    return NOIR_IGNORED; }

  pthread_mutex_lock(&qh->qchg);
  ret=ff.file_scanner_activate(qh);
  pthread_mutex_unlock(&qh->qchg);

  return ret; }






/*****************************************************************************/
/* jobs: cp, mv, rm async */



/**
 * destruct an async job
 * @return an error-code
 * @param job  the job struct to destruct
 */
static int file_job_dst(noir_job **job) {
  if(!job)
    return NOIR_FAIL;
  if(!*job)
    return NOIR_IGNORED;

  if((*job)->ac)
    action_context_dst((*job)->ac);

  if((*job)->src_path)
    free((*job)->src_path);
  if((*job)->dst_path)
    free((*job)->dst_path);

  if((*job)->src)
    free((*job)->src);
  if((*job)->dst)
    free((*job)->dst);

  if((*job)->tree)
    dir_entry_dst_all((*job)->tree);

  if((*job)->challenge)
    free((*job)->challenge);

  pthread_mutex_destroy(&(*job)->datalock);
  pthread_cond_destroy(&(*job)->backendpause);

  memset(*job,0,sizeof(noir_job));
  free(*job);
  *job=NULL;
  return NOIR_SUCC; }



/**
 * shorten a string putting an ellipsis in the middle if it exceeds a certain size
 * @return the buffer (not allocated!)
 * @param e  pre-allocated buffer to put the resulting string in
 * @param s  the string to shorten
 * @param l  target-length
 */
static char *file_job_ellipsis(char *e,char *s,size_t l) {
  size_t o=strlen(s);
  if(o==l)
    strcpy(e,s);
  else if(o<l) {
    strncpy(e,s,o);
    memset(e+o,' ',l-o-1);
    e[l]='\0'; }
  else {
    size_t x=(l/2)-2;
    strncpy(e,s,x);
    strcpy(e+x,"...");
    strcpy(e+x+3,&s[o-(l/2)+1]); }
  return e; }



/**
 * format "x/y zB" message (for progress-indicator)
 * @return newly-allocated buffer holding resulting string
 * @param item   the descriptor ("total", "current file", ...)
 * @param part   a bytes of...
 * @param total  ...b
 */
static char *file_job_bytes(const char *item,unsigned long long part,unsigned long long total) {
  char *bytes=NULL;
  if(total>=(100*1024*1024))
    esprintf(&bytes,"%s%s%'lld/%'lld GB",item?item:"",item?": ":"",(unsigned long long)(part/(1024*1024)),(unsigned long long)(total/(1024*1024)));
  else if(total>(1024*1024))
    esprintf(&bytes,"%s%s%.1f/%.1f MB",item?item:"",item?": ":"",((float)part)/(1024*1024),((float)total)/(1024*1024));
  else
    esprintf(&bytes,"%s%s%'lld/%'lld KB",item?item:"",item?": ":"",(unsigned long long)(part/1024),(unsigned long long)(total/1024));
  return bytes; }



/**
 * show progress of a background job using GTK2.
 * default handler if theme doesn't set one.
 * @return an error-code
 * @param job  the job to show info about
 */
int file_job_progress_show(noir_job *job) {
  int   ret=NOIR_SUCC;
  char *src=job->src?job->src:"???",
       *dst,
       *msg=NULL,
       *p,
       *bytes=NULL;

  if(job->status==NOIR_JOB_UNCHANGED)
    return NOIR_IGNORED;

  if(!(dst=job->dst)) {
    if(!(dst=job->dst_path))
      dst="???"; }

  E(2,"updating job id %ld, %s %s!\n",job->id,job_type[job->type],job_status[job->status]);

  if((p=strrchr(src,DIR_DELIMITER)))  src=p+1;
  if((p=strrchr(dst,DIR_DELIMITER)))  dst=p+1;

  switch(job->status) {
    case NOIR_JOB_START:
      form_modeless_new(&job->user1,&job->user2);
      form_modeless_label_new(job->user2,&job->user3,"Initializing...");
      form_modeless_bar_new(job->user2,&job->user4);
      form_modeless_show(job->user1);
      break;

    case NOIR_JOB_DONE:
      E(3,"dial for job id %ld should go away now (\"%s\")...\n\n",job->id,job->src_path?job->src_path:"");
      form_modeless_dst(&job->user1);
      job->user2=NULL;
      break;

    case NOIR_JOB_DEL:
      /* FALL-THRU */

    case NOIR_JOB_ADD:
      if(job->src) {
        char e[41];
        file_job_ellipsis(e,job->src,40);

        esprintf(&bytes,"Total: %lld",(unsigned long long)job->total_bytes);
        if(bytes) {
          char *bp=strchr(bytes,':');
          if(bp)
            unit_size(bp+2);
          esprintf(&msg,"%lld objects\n%s\n%s",(unsigned long long)job->total_files,bytes,e);
          free(bytes); }}

      if(msg) {
        form_modeless_label_update(job->user3,msg);
        free(msg); }

      form_modeless_bar_update(job->user4,"Scanning",-1);

      break;

    case NOIR_JOB_PROGRESS: {
      int s=(job->src_path&&strcmp(job->src_path,src))?1:0;

      if(job->tree) {
        if(!job->user5)   /* bytes in job */
          form_modeless_bar_new(job->user2,&job->user5);
        if(!job->user6)   /* files in job */
          form_modeless_bar_new(job->user2,&job->user6);

        if(job->user5) {
          if((bytes=file_job_bytes("Total",job->total_bytes_copied,job->total_bytes))) {
            form_modeless_bar_update(job->user5,bytes,((float)job->total_bytes_copied)/job->total_bytes);
            free(bytes); }}

        if(job->user6) {
          char *files=NULL;
          esprintf(&files,"Files: %lld/%lld",(unsigned long long)job->file_index,(unsigned long long)job->total_files);
          if(files) {
            form_modeless_bar_update(job->user6,files,((float)job->file_index)/job->total_files);
            free(files); }}}



      if(job->type==NOIR_JOB_RM) {
        if(s)
          esprintf(&msg,"Deleting\n%s\n%s",job->src,src);
        else
          esprintf(&msg,"Deleting\n%s",src); }
      else {
        int d=(job->dst_path&&strcmp(job->dst_path,dst))?1:0;
        switch((d<<1)+s) {
          case 1:
            esprintf(&msg,"%s\n\"%s\"\n\"%s\"\n\n\"%s\"",
                     (job->type==NOIR_JOB_CP)?"Copying":"Moving",job->src_path,src,dst);
            break;
          case 2:
            esprintf(&msg,"%s\n\"%s\"\n\n\"%s\"\n\"%s\"",
                     (job->type==NOIR_JOB_CP)?"Copying":"Moving",src,job->dst_path,dst);
            break;
          case 3:
            esprintf(&msg,"%s\n\"%s\"\n\"%s\"\n\n\"%s\"\n\"%s\"",
                     (job->type==NOIR_JOB_CP)?"Copying":"Moving",job->src_path,src,job->dst_path,dst);
            break;
          default:
            esprintf(&msg,"%s\n\"%s\"\n\"%s\"",
                     (job->type==NOIR_JOB_CP)?"Copying":"Moving",src,dst);
            break; }}

      if(!msg) {
        E(0,"out of memory!?\n");
        ret=NOIR_OOM; }
      else {
        form_modeless_label_update(job->user3,msg);
        E(4,"%s\n",msg);
        free(msg); }
      if(job->file_bytes) {
        if((bytes=file_job_bytes("File",job->file_bytes_copied,job->file_bytes))) {
          form_modeless_bar_update(job->user4,bytes,((float)job->file_bytes_copied)/job->file_bytes);
          free(bytes); }}
      else
        form_modeless_bar_update(job->user4,"evidence",-1); }
      break;

    case NOIR_JOB_EDIT:
      form_file_edit(job->challenge,job->edit);
      break;

    case NOIR_JOB_QUESTION:
      if(!job->buttons) {
        action_context *ac=action_context_new(0,NULL);
        action_context_set_flags(ac,job->flags);
        if(action_context_tst_flags(ac,AF_SELECTION))
          action_file_new(ac,"internal/file_job_progress");
        job->reply=ret=form_yes_no_all(job->session,job->challenge,ac);
        job->flags=action_context_get_flags(ac);
        action_context_dst(ac); }
      else
        job->reply=ret=form_dial(job->session,
                                 ALERT_QUESTION,ALERT_BUTTONS_CUSTOM,
                                 job->challenge,job->buttons);
      break;

    default:
      E(0,"ignoring unknown job-status %d\n",job->status);
      break; }

/* job->status=NOIR_JOB_UNCHANGED; */
  return ret; }



static int file_job_wait4gui(noir_job *job) {
  if(!job)
    return NOIR_FAIL;

  if(job->async) {
    E(6,"backend locking & updating job (%ld)...\n",job->id);
#if 0
   lock4update:
    pthread_mutex_lock(&job->datalock);   /* lock job while we edit it */
    if(job->status!=NOIR_JOB_UNCHANGED) { /* let front-end catch up -- this will turn into pthread_cond_wait() in the next incarnation. */
      E(6,"front-end isn't ready with the last update for job %ld, give it time...\n",job->id);
      pthread_mutex_unlock(&job->datalock);
      thread_sleep(0.05);
      goto lock4update; }
#else
    pthread_mutex_lock(&job->datalock);   /* lock job while we edit it */
    while(job->status!=NOIR_JOB_UNCHANGED) { /* let front-end catch up */
      E(6,"front-end isn't ready with the last update for job %ld, backend going to sleep...\n",job->id);
      pthread_cond_wait(&job->backendpause,&job->datalock); }
    E(6,"front-end has completed update for job %ld, backend waking up...\n",job->id);
#endif
    return NOIR_SUCC; }
  else
    return NOIR_IGNORED; }



static inline int file_job_gui_done(noir_job *job) {
  if(!job)
    return NOIR_FAIL;
  job->status=NOIR_JOB_UNCHANGED;
  return pthread_cond_signal(&job->backendpause)?NOIR_FAIL:NOIR_SUCC; }



static inline int file_job_finish(noir_job **job) {
  if(!job||!*job)
    return NOIR_FAIL;
/* if(ff.file_job_collect&&!pthread_equal(pthread_self(),job->thread)) */
  if(ff.file_job_collect)
    ff.file_job_collect(*job);
  E(5,"job %ld collected, desting job...\n",(*job)->id);
  file_job_dst(job);
  return NOIR_SUCC; }




/**
 * update UI (progress-bars or whatever) with job-status
 * called from model_pulse()
 * @return the number of jobs we processed
 */
int file_job_progress_show_all(void) {
  int       ret=0;
  noir_job *job=jobs,*p=NULL,*n;

  if(pthread_mutex_trylock(&fe_pause)==EBUSY) {
    E(0,"backlogging. interesting.\n");
    return NOIR_IGNORED; }

  E(6,"front-end locked.\n");

  while(job) {
    if(job->async) {
      E(4,"locking job (%ld), showing progress indicator...\n",job->id);
      pthread_mutex_lock(&job->datalock); }

    n=job->next;

    E(4,"job id %ld \"%s\" -> \"%s\"\n",job->id,job->src?job->src:"-",job->dst?job->dst:"-");

    if(job->status!=NOIR_JOB_UNCHANGED) {
      ret++;

      job->callback(job);

      E(4,"showed progress, releasing job (%ld)...\n",job->id);

      if(job->status==NOIR_JOB_DONE) {  /* the job is done, I go out.  the job gets dested. */
        if(p)
          p->next=n;
        else
          jobs=n;
        file_job_finish(&job); }
      else
        file_job_gui_done(job); }

    if(job) {
      if(job->async) {
        pthread_mutex_unlock(&job->datalock); } /* we caught the update, release back-end */
      p=job; }
    job=n; }

  E(6,"front-end released.\n");
  pthread_mutex_unlock(&fe_pause);

  return ret; }



/**
 * backend wants to update files or status in progress indicator
 * @return an error-code
 * @param job  the job
 * @param s    new status, or NOIR_JOB_UNCHANGED for no change (this will also prevent a visual update!)
 * @param e    error-code, or NOIR_NOERRORCODE for no change
 * @param src  new source file, or NULL for no change
 * @param dst  new destination name, or NULL for no change
 */
int file_job_update(noir_job *job,noir_job_status s,int e,const char *src,const char *dst) {
  E(4,"job #%ld: %s (#%d), \"%s\" -> \"%s\"\n",job->id,job_status[s],e,src?src:"-",dst?dst:"-");
  if(!job)
    return NOIR_FAIL;
  if((s==NOIR_JOB_UNCHANGED)&&(e==NOIR_NOERRORCODE)&&!src&&!dst)
    return NOIR_IGNORED;

  /* PROGRESS/START/ADD/DEL updates don't need the GUI handshake: nothing
     waits for a reply, the pulse just reads the fields under datalock.
     the 2005 code slept here until the 250ms GUI pulse consumed EVERY
     update -- with one per file/dir that throttled copy jobs to a few
     files per second.  EDIT/QUESTION/DONE keep the handshake (dialogs
     need an answer, DONE dests the job). */
  if((s==NOIR_JOB_EDIT)||(s==NOIR_JOB_QUESTION)||(s==NOIR_JOB_DONE))
    file_job_wait4gui(job);

  E(4,"locked job (%ld)...\n",job->id);

  if(s!=NOIR_JOB_UNCHANGED)
    job->status=s;
  if(e!=NOIR_NOERRORCODE)
    job->err=e;
  if(src&&(!job->src||strcmp(src,job->src))) {
    job->file_bytes=job->file_bytes_copied=0;
    job->file_index++;
    if(job->src)
      free(job->src);
    job->src=strdup(src); }
  if(dst&&(!job->dst||strcmp(dst,job->dst))) {
    if(job->dst)
      free(job->dst);
    job->dst=strdup(dst); }

  if(job->async) {
    E(4,"unlocking updated job (%ld)...\n",job->id);
    pthread_mutex_unlock(&job->datalock); }   /* unlock job */

  if((s==NOIR_JOB_EDIT)||(s==NOIR_JOB_QUESTION)) {
    E(4,"special request (edit, question, ...), waiting for front-end to complete...\n");
    file_job_wait4gui(job);
    pthread_mutex_unlock(&job->datalock); }   /* unlock job */
  else if((s==NOIR_JOB_DONE)&&(!job->callback))
    file_job_finish(&job);

  return NOIR_SUCC; }



/**
 * backend wants to update stats in progress indicator
 * @return an error-code
 * @param job  the job
 * @param fbt  file: bytes (total)
 * @param fbc  file: bytes (copied)
 * @param fno  file: index
 * @param bt   bytes (total)
 * @param bc   bytes (copied)
 * @param no   total number of items
 */
int file_job_stats(noir_job *job,ulong fbt,ulong fbc,ulong fno,ulong bt,ulong bc,ulong no) {
  if(!job)
    return NOIR_FAIL;

  if(job->async)
    pthread_mutex_lock(&job->datalock);   /* lock job while we edit it */

  if(bt>0)  job->total_bytes=bt;
  if(no>0)  job->total_files=no; /* can there even be 0 files in a job? */
  if(bc>0)  job->total_bytes_copied=bc;
  else if(fbc>0)
    job->total_bytes_copied=job->total_bytes_copied-job->file_bytes_copied+fbc;

  if(fbt>0) job->file_bytes=fbt;
  if(fbc>0) job->file_bytes_copied=fbc;
  if(fno>0) job->file_index=fno; /* do we even start counting at 0? yes. */

#if 1
  if(job->status==NOIR_JOB_UNCHANGED) {
    E(6,"%lld/%lld bytes in %lld/%lld files.\n",
      (unsigned long long)job->total_bytes_copied,
      (unsigned long long)job->total_bytes,
      (unsigned long long)job->file_index,
      (unsigned long long)job->total_files);
    job->status=NOIR_JOB_PROGRESS; }  /* so the GUI won't ignore us */
#else
  if((now(0)-job->pits)>=1000) {
    job->pits=now(0);
    if(job->status==NOIR_JOB_UNCHANGED)
      job->status=NOIR_JOB_PROGRESS;  /* so the GUI won't ignore us */
    E(4,"updating progress-bar (%ld)\n",job->id); }
#endif

  if(job->async)
    pthread_mutex_unlock(&job->datalock); /* release job */

  return NOIR_PARTIAL; }



/**
 * change paths in job
 * @return an error-code
 * @param job       the job
 * @param src_path  set current source (directory or file), or NULL for no change
 * @param dst_path  set current target (directory or file), or NULL for no change
 * @param src_old   return pointer to previous source (pointed-at path must be free()d by caller!). if NULL, previous path is free()d.
 * @param dst_old   return pointer to previous target (pointed-at path must be free()d by caller!). if NULL, previous path is free()d.
 */
static int file_job_paths(noir_job *job,const char *src_path,const char *dst_path,char **src_old,char **dst_old) {
  if(!job)
    return NOIR_FAIL;

  if(job->async) {
    E(4,"locking & updating job (%ld)...\n",job->id);
    pthread_mutex_lock(&job->datalock); }   /* lock job while we edit it */

  if(src_path&&(src_old||!job->src_path||strcmp(src_path,job->src_path))) {
    if(src_old)
      *src_old=job->src_path;
    else if(job->src_path)
      free(job->src_path);
    job->src_path=strdup(src_path); }

  if(dst_path&&(dst_old||!job->dst_path||strcmp(dst_path,job->dst_path))) {
    if(dst_old)
      *dst_old=job->dst_path;
    else if(job->dst_path)
      free(job->dst_path);
    job->dst_path=strdup(dst_path); }

  if(job->async) {
    E(4,"done updating job (%ld)...\n",job->id);
    pthread_mutex_unlock(&job->datalock); } /* release job */

  return file_job_update(job,NOIR_JOB_PROGRESS,NOIR_NOERRORCODE,src_path?job->src_path:NULL,dst_path?job->dst_path:NULL); }



/**
 * open a yes/skip/cancel/all kind of dialog
 * @return an error-code < 0 or a button-number
 * @param job        the job, or NULL
 * @param buttons    the buttons for the dialog, or NULL for yes/skip/all/cxl
 * @param fmt        the message for the dialog
 * @param ...        variables for % substitutions
 */
int file_job_ask(noir_job *job,const char *buttons,const char *fmt,...) {
  int       need,have,ret;
  va_list   ap;
  char     *n;

  if(!job) {
    E(0,"called with no job!?\n");
    return NOIR_NODATA; }

  if(job->async)
    pthread_mutex_lock(&job->datalock);  /* lock to modify the job */

  if(job->challenge)
    free(job->challenge);
  job->challenge=NULL;   /* clear in any case (if realloc() fails later) */

  if(!(n=(char*)malloc(have=512))) {  /* can't go before if(job), must hold lock */
    job->reply=NOIR_OOM;
    goto done; }

  job->buttons=buttons;
  if(job->ac)
    job->flags|=AF_SELECTION;
  else {
    job->flags&=(~AF_SELECTION);
    job->flags|=AF_LAST; }

retry:
  va_start(ap,fmt);
  need=vsnprintf(n,have,fmt,ap);
  va_end(ap);

  if((need>=have)||(need<0)) {
    char *p;
    if(need<0)
      need=2*have;
    else
      need++;
    if(!(p=(char*)realloc(n,need))) {
      free(n);
      job->reply=NOIR_OOM;
      goto done; }
    have=need;
    n=p;
    goto retry; }

  job->challenge=n;
  job->reply=NOIR_IGNORED;

done:
  if(job->async)
    pthread_mutex_unlock(&job->datalock);

  if(job->challenge)
    file_job_update(job,NOIR_JOB_QUESTION,NOIR_NOERRORCODE,NULL,NULL);

  E(1,"result: %d  ($%x)\n",job->reply,job->flags);

  ret=job->reply;

  if(job->type==NOIR_JOB_DIAL)
    file_job_dst(&job);

  return ret; }



/**
 * the target file-name is taken. allow user to edit it.
 * @return an error-code
 * @param job        the job
 * @param challenge  the message for the dialog
 * @param edit       the file-name to edit
 */
int file_job_edt(noir_job *job,const char *challenge,char **edit) {
  if(job->async)
    pthread_mutex_lock(&job->datalock);
  if(job->challenge)
    free(job->challenge);
  job->challenge=strdup(challenge);
  job->edit=edit;
  if(job->async)
    pthread_mutex_unlock(&job->datalock);
  file_job_update(job,NOIR_JOB_EDIT,NOIR_NOERRORCODE,NULL,NULL);
  E(1,"result: %s\n",*edit);
  return NOIR_SUCC; }



/**
 * debug: list sub-items of a scanned dir-entry
 * @param de  the dir-entry to print info on
 */
static void file_job_list(dir_entry *de) {
  while(de) {
    printf("%6lld  %2ld/%3ld  %s\n",(unsigned long long)de->size,de->children,de->descendents,de->tag);
    if(de->children)
      file_job_list(de->child);
    de=de->next; }}



/**
 * a single dir was scanned, remember details for stats
 * (callback for file_getdir())
 * @param e         the dir in question
 * @param data      whatever data was given when the signal was connected
 */
static void file_job_scan_dir(dir_entry *e,void *data) {
  noir_job  *job=(noir_job *)data;

  job->total_files+=e->children;
  job->total_bytes+=e->net;

#if 0
  E(1,"DONE \"%s\"  %'.1lld B (%'.2lf MB) in %ld objects (%d)\n",
    e->tag,
    (unsigned long long)e->net,
    ((double)e->net)/(1024*1024),
    (ulong)e->children,
    sizeof(e->net));
  E(0,"SUM  %'lld B (%'.1lf MB, %'.1lf GB) in %ld/%ld objects (%d)\n\n",
    (unsigned long long)job->total_bytes,
    ((double)job->total_bytes)/(1024*1024),
    ((double)job->total_bytes)/(1024*1024*1024),
    job->file_index,
    job->total_files,
    sizeof(job->total_bytes)
    );
#endif
#if 0
  if((((double)job->total_bytes)/(1024*1024*1024))>2)
    sleep(1);
#endif

  if((now(0)-job->pits)<1000)
    return;

  job->pits=now(0);

  if(job->async)
    pthread_mutex_unlock(&job->datalock);  /* unlock job so file_job_update() can have it */

  file_job_update(job,NOIR_JOB_ADD,NOIR_SUCC,e->tag,NULL);

  if(job->async)
    pthread_mutex_lock(&job->datalock); }



/**
 * calculate the size of a file-tree
 * @return an error-code
 * @param job   the job
 * @param path  the item to scan (sub-dirs inclusive)
 */
static int file_job_scan(noir_job *job,const char *path) {
  int ret=NOIR_NODATA;

  if(!job||!path||!*path)
    return NOIR_FAIL;

  if(job->async)
    pthread_mutex_lock(&job->datalock);  /* lock to modify the job */

  job->pits=now(0);

  if((job->tree=file_getdir(path,FGD_DEEP|FGD_STAT,file_job_scan_dir,job))) {
    dir_entry *ch=job->tree;

#if 0
    puts("");
    file_job_list(job->tree);
    puts("");
#endif

    job->file_index=0;
    job->total_bytes=0;
    job->total_files=1;  /* count copy-root! */

    while(ch) {
      job->total_bytes+=ch->size;   /* was '=', so the total was only ever the LAST top-level child */
      job->total_files+=ch->descendents+1;
      ch=ch->next; }

    E(1,"%lld bytes in %lld files.\n",(unsigned long long)job->total_bytes,(unsigned long long)job->total_files);
    ret=NOIR_SUCC; }

  if(job->async)
    pthread_mutex_unlock(&job->datalock);  /* unlock */

  file_job_update(job,NOIR_JOB_PROGRESS,NOIR_SUCC,path,NULL);

  return ret; }



/**
 * create a new async job struct
 * @return a filled in job-struct
 * @param session  the session this job is related to
 * @param type     the type of the job (copy, move, delete, ...)
 * @param files    the selection to work on
 * @param data     any user-data
 */
static noir_job *file_job_new(void *session,noir_job_type type,action_context *files,void *data) {
  noir_job *job;

  if(!(job=malloc(sizeof(noir_job))))
    return NULL;
  memset(job,0,sizeof(noir_job));

  if((job->ac=files)) {
    action_file *single=action_context_get_single(files);
    if(single&&!(job->dst_path=strdup(action_file_get_tag(single))))
      goto oom; }

  job->callback=file_job_progress_show;  /* default to built-in */
  job->thread=pthread_self();
  job->session=session;
  job->type=type;
  job->data=data;
  job->status=NOIR_JOB_UNCHANGED;
  job->reply=NOIR_IGNORED;
  job->flags=AF_NONE;
  job->err=NOIR_IGNORED;  /* if it's not ignored and something happens, this value will be overridden */

  pthread_mutex_init(&job->datalock,NULL);
  pthread_cond_init(&job->backendpause,NULL);

  return job;

oom:
  file_job_dst(&job);
  return NULL; }



/**
 * convenience function: create a new async job struct
 * @return a filled in job-struct
 * @param session  the session this job is related to
 * @param type     the type of the job (copy, move, delete, ...)
 */
noir_job *file_job_simple(void *session,noir_job_type type) {
  return file_job_new(session,type,NULL,NULL); }



/**
 * create a new async job (public interface)
 * @return a negative error-code, or a positive job-ID
 * @param session  the session this job is related to
 * @param type     the type of the job (copy, move, delete, ...)
 * @param files    the selection to work on
 * @param data     any user-data
 */
int file_job_create(void *session,noir_job_type type,action_context *files,void *data) {
  noir_job  *job;

  if(!(job=file_job_new(session,type,files,data)))
    return NOIR_OOM;

  job->async=TRUE;
  /* lock *front-end*, because we're chaning the session list */
  pthread_mutex_lock(&fe_pause);
  job->next=jobs;
  jobs=job;
  pthread_mutex_unlock(&fe_pause);

  return ff.file_job_start(job); }






/*****************************************************************************/



/**
 * destruct a string (in ht_mime_alii hashtable)
 * @param data  a string
 */
static void mime_alias_dst(void *data) {
  free(data); }



/**
 * remove path for icon 'key' from hashtable
 * @param key  the icon's symbolic name
 */
int mime_alias_del(char *key) {
  return hash_table_remove(ht_mime_alii,key)?NOIR_SUCC:NOIR_FAIL; }



/**
 * add a translation to MIME-alias hash
 * @return an error-code
 * @param key        the name to be translated
 * @param canonical  the canonical name
 */
int mime_alias_add(const char *key,const char *canonical) {
  char *k,*v;

  E(2,"\"%s\" is really \"%s\".\n",key,canonical);

  if(!ht_mime_alii) {
    if(!(ht_mime_alii=hash_table_new(mime_alias_dst,mime_alias_dst))) {
      int warned=0;
      if(!warned) {
        warned++;
        E(0,"couldn't create hashtable!?\n"); }
      return NOIR_OOM; }}

  if(hash_table_fetch(ht_mime_alii,key))
    return NOIR_IGNORED;

  if(!(k=strdup(key)))
    return NOIR_OOM;

  if(!(v=strdup(canonical))) {
    free(k);
    return NOIR_OOM; }

#if 0
  hash_table_insert(ht_mime_alii,k,v);
#else
  hash_table_replace(ht_mime_alii,k,v);
#endif

  return NOIR_SUCC; }



/**
 * create MIME-alias hashtable from file
 * @return an error-code
 */
int mime_alii_load(void) {
  FILE *fh;
  char *p,*fn,*k=NULL;
  char  l[MIME_MAX_LEN];
  int   canonical=TRUE,ret=NOIR_FAIL;

  if((fn=get_setup_file(MIME_ALII))) {
    if((fh=fopen(fn,"r"))) {

      ret=NOIR_SUCC;

      while(fgets(l,MIME_MAX_LEN,fh)) {
        p=l;
        while(isspace(*p))
          p++;

        if(*p!='#') {
          char *p2;

          p2=p;
          while(*p2&&!isspace(*p2))
            p2++;

          *p2='\0';

          if(!strlen(p))  /* empty line. next line will hold canonical type. */
            canonical=TRUE;

          else if(canonical) {
            if(k)
              free(k);
            if(!(k=strdup(p))) {
              ret=NOIR_OOM;
              goto done; }
            canonical=FALSE; }

          else
            mime_alias_add(p,k); }}

     done:
      if(k)
        free(k);

      fclose(fh); }

    free(fn);
    return ret; }

  else
    E(0,"MIME-init: couldn\'t find \"%s\".\n",MIME_ALII);

  return NOIR_NOFILE; }






/*****************************************************************************/



/**
 * destroy a file-name annotation
 * @param rep  the annotation
 */
static void file_annotation_del(void *rep) {
  file_annotation *dd;

  if(!(dd=(file_annotation *)rep))
    return;

  if(dd->tag) free(dd->tag);
  if(dd->sub) free(dd->sub);
  if(dd->dsc) free(dd->dsc);

  dd->lvl=FA_NONE;
  free(dd); }



/**
 * create a file-name annotation
 * @return a new annotation, or NULL
 * @param tag  the file's tag
 * @param sub  the substitution ("/" -> "My computer"), or NULL
 * @param lvl  the the "protection level"
 * @param dsc  the description of the item, or NULL
 */
static file_annotation *file_annotation_new(const char *tag,const char *sub,falvl lvl,const char *dsc) {
  file_annotation *rep;

  if(!tag)
    return NULL;

  if(!(rep=malloc(sizeof(file_annotation))))
    return NULL;

  memset(rep,0,sizeof(file_annotation));

  if(tag&&!(rep->tag=strdup(tag))) goto oom;
  if(sub&&!(rep->sub=strdup(sub))) goto oom;
  if(dsc&&!(rep->dsc=strdup(dsc))) goto oom;
  rep->lvl=lvl;

  return rep;

 oom:
  file_annotation_del(rep);
  return NULL; }



/**
 * get annotation
 */
file_annotation *file_annotation_get(const char *tag) {
  file_annotation *dd=NULL;
  if(ht_annotation)
    dd=hash_table_fetch(ht_annotation,tag);
  return dd; }



/**
 * initialize array of user-friendly file-names
 * @return an error-code
 */
static int file_annotation_init(void) {
  file_annotation annotations[]={
    { "/",            "My computer",           FA_USER,  "This is the top-most folder that contains all other files and folders on your computer." },
    { "/bin",         "User-programs",         FA_USER,  "The most basic programs live here. Most of them are intended for use from a \"shell\" (command-line interface)." },
    { "/boot",        "System start",          FA_ROOT,  "The Operating System is loaded from here when the computer is switched on." },
    { "/cdrom",       "CD ROM",                FA_MEDIA, "The CD ROM drive." },
    { "/dev",         "Hardware drivers",      FA_ROOT,  "Each recognized device is presented by one or many files in here. Unless you are root and know what you are doing, there probably isn't anything useful you could do in there." },
    { "/dvd",         "DVD",                   FA_MEDIA, "The DVD drive." },
    { "/etc",         "System setup",          FA_ROOT,  "The system's configuration (for services such as e-mail, http/WWW, and many other important things) lives here." },
    { "/etc/X11",     "X11 setup",             FA_ROOT,  "The setup for the X server (the graphics driver, in a way) lives here." },
    { "/etc/opt",     NULL,                    FA_ROOT,  "The setup for packages in /opt." },
    { "/floppy",      "Floppy",                FA_MEDIA, "The floppy disk drive. Wow, these things are still around?" },
    { "/home",        "Users' files",          FA_USER,  "Each user has a \"home\" folder." },
    { "/lib",         "Software libraries",    FA_ROOT,  "Fundamental software-libraries live here. They are used by the programs in /bin and /sbin. They contain often-used functionality so it doesn't have to be duplicated into each of your programs. This saves disk-space and means that if a problem is discovered in a library, it can be fixed in one central place." },
    { "/media",       "Removable media",       FA_MEDIA, NULL },
    { "/misc",        "Auto-mounted folders",  FA_USER,  NULL },
    { "/mnt",         "Mountable folders",     FA_USER,  NULL },
    { "/opt",         "Software packages",     FA_USER,  NULL },
    { "/proc",        "System-information",    FA_USER,  NULL },
    { "/root",        "Admin's files",         FA_ROOT,  "The system administrator's \"home\" folder, where his or her data are kept." },
    { "/sbin",        "Admin programs",        FA_ROOT,  "This folder contains vital programs for use by the system's administrator." },
    { "/srv",         "Servers' documents",    FA_USER,  "This folder contains the (semi-) public data services like ftp, http/WWW etc. hand out." },
    { "/tmp",         "Temporary files",       FA_USER,  "Short-term temporary files that are not guaranteed to last from session to session." },
    { "/usr",         "Public",                FA_USER,  "Programs and static data for use by all users live here." },
    { "/var",         "Variable files",        FA_USER,  "Variable files that survive reboots." },

    { "/Searches",    "Search-results",        FA_USER,  "This is a virtual folder that doesn't exist in the file-system. It contains the results of your searches." },
    { "/Shelves",     "Shelves",               FA_USER,  "This is a virtual folder that doesn't exist in the file-system. It contains your \"shelves\"." },

    { "/var/cache",   "Cache",                 FA_ROOT,  "Application data-cache." },
    { "/var/local",   "Local",                 FA_USER,  "Variable data for /usr/local" },
    { "/var/lock",    "Locks",                 FA_ROOT,  "Lock-files" },
    { "/var/log",     "Logs",                  FA_USER,  "Log-files" },
    { "/var/opt",     "Opt",                   FA_USER,  "Variable data for /opt" },
    { "/var/run",     "Run",                   FA_USER,  "Data relevant to running processes" },
    { "/var/spool",   "Spool",                 FA_ROOT,  "Application spool data" },
    { "/var/tmp",     "Temporary files",       FA_USER,  "Temporary files preserved between reboots." },

    { "/var/account", "Accounting",            FA_ROOT,  "Accounting logs" },
    { "/var/crash",   "Crash dumps",           FA_ROOT,  NULL },
    { "/var/games",   "Games",                 FA_USER,  "Variable game data (hiscore lists etc.)" },
    { "/var/mail",    "eMail",                 FA_ROOT,  NULL },
    { "/var/spool/mail", "eMail",              FA_ROOT,  NULL },
    { "/var/yp",      "NIS",                   FA_ROOT,  "Network Information Service (NIS) database files. Formerly YP (Yellow Pages)." },

    { "/usr/bin",     "Programs",              FA_USER,  "Programs other than the essentials (in /usr) and packages (in /opt)." },
    { "/usr/include", "Header files",          FA_USER,  "Header files for the 'C' programming language." },
    { "/usr/lib",     "Software libraries",    FA_USER,  "Software-libraries live here. They are used by your programs. They contain often-used functionality so it doesn't have to be duplicated into each of your programs. This saves disk-space and means that if a problem is discovered in a library, it can be fixed in one central place." },
    { "/usr/sbin",    "Admin programs",        FA_ROOT,  "This folder contains programs for use by the system's administrator." },
    { "/usr/share",   "Arch-independent",      FA_USER,  "Architecture-independent data that can be shared between machines with different micro-processors." },
    { "/usr/local",   "Local",                 FA_USER,  "Local software may be installed here by the system's administrator. This way it will remain safe if the system software in /usr is updated." },
    { "/usr/src",     "Source-code",           FA_USER,  "Source-code that can be transformed into programs or software library by a compiler or assembler. Of interest to software developers only." },
    { "/usr/X11R6",   "X Window",              FA_USER,  "The X Window system. The foundation of the GUI, window-managers (such as enlightenment or WindowMaker) and ultimately desktop environments (such as KDE or GNOME) build on top of this \"graphics driver.\"" },
    { "/usr/games",   "Games",                 FA_USER,  NULL },

    { "/usr/spool",   "Spool",                 FA_USER,  "A redirect for old programs that don't know about /var/spool yet." },
    { "/usr/tmp",     "Temporary files",       FA_USER,  "A redirect for old programs that don't know about /var/tmp yet." },
    { "/usr/spool/locks",   "Locks",           FA_USER,  "A redirect for old programs that don't know about /var/lock yet." },

    { "/usr/bin/X11", "X Window",              FA_USER,  "A redirect for old programs that don't know about /usr/X11R6/bin yet. X Window programs live there." },
    { "/usr/lib/X11", "X Window",              FA_USER,  "A redirect for old programs that don't know about /usr/X11R6/lib/X11 yet. X Window libraries live there." },
    { "/usr/include/X11", "X Window",          FA_USER,  "A redirect for old programs that don't know about /usr/X11R6/include/X11 yet. Header files to use X11 from the 'C' programming language live there." },

    { "/usr/local/bin",    "User-programs",    FA_USER,  "The most basic programs live here. Most of them are intended for use from a \"shell\" (command-line interface)." },
    { "/usr/local/etc",    "System setup",     FA_ROOT,  "The system's configuration (for services such as e-mail, http/WWW, and many other important things) lives here." },
    { "/usr/local/games",  "Games",            FA_USER,  NULL },
    { "/usr/local/include","Header files",     FA_USER,  "Header files for the 'C' programming language." },
    { "/usr/local/lib",   "Software libraries",FA_USER,  "Local software-libraries live here. They are used by your programs. They contain often-used functionality so it doesn't have to be duplicated into each of your programs. This saves disk-space and means that if a problem is discovered in a library, it can be fixed in one central place." },
    { "/usr/local/man",    "Manuals",          FA_USER,  "Local manuals." },
    { "/usr/local/share/man",    "Manuals",    FA_USER,  "A redirect for old programs that don't know about /usr/local/man yet." },
    { "/usr/local/sbin",   "Admin programs",   FA_ROOT,  "This folder contains vital programs for use by the system's administrator." },
    { "/usr/local/share",  "Arch-independent", FA_USER,  "Local architecture-independent data that can be shared between machines with different micro-processors." },
    { "/usr/local/src",    "Source-code",      FA_USER,  "Local source-code that can be transformed into programs or software library by a compiler or assembler. Of interest to software developers only." },

    { "/usr/share/dict",   "Word-lists",       FA_USER,  "Word-lists (for spell-checkers etc.)" },
    { "/usr/share/dic",    "Documentation",    FA_USER,  "Miscellaneous documentation" },
    { "/usr/share/games",  "Game data",        FA_USER,  "Static game-data (as opposed to dynamic high-score lists etc.) -- usually level-, sound and image-data." },
    { "/usr/share/info",   "Info",             FA_USER,  "Manuals for the GNU \"info\" system. These manuals can be viewed with either of info, tkinfo, emacs, xemacs." },

    { NULL,      NULL,                         FA_USER,  NULL }};

  if(!(ht_annotation=hash_table_new(NULL,file_annotation_del))) {
    E(0,"couldn't create hashtable!?\n");
    return NOIR_OOM; }
  else {
    file_annotation *fa;
    int n=0;
    while(annotations[n].tag) {
      if((fa=file_annotation_new(annotations[n].tag,annotations[n].sub,annotations[n].lvl,annotations[n].dsc))) {
	E(3,"\"%s\" -> \"%s\"\n",fa->tag,fa->sub?fa->sub:"");
	hash_table_insert(ht_annotation,fa->tag,fa); }
      n++; }}

  return NOIR_SUCC; }






/*****************************************************************************/



/**
 * init this module.
 * @return an error-code
 * @param qh  the queue of the model we want to work with
 * @param be  the backend to load
 */
int file_init(q_head *qh,const char *be) {
  char       *fn;
  const char *err;
  int         ret;

  memset(&ff,0,sizeof(struct _file_fun));

  if(!(fn=get_setup_file(be))) {
    be="lib/evidence/backend/posix.so";
    if(!(fn=get_setup_file(be))) {
      E(0,"supposed backend \"%s\" not found!  STOP.\n",be);
      goto fail; }
    else
      E(0,"found backend \"%s\".\nPlease symlink one of the backends in that directory to \"fsxs.so\" at some point.\n",be); }

  ret=dlmulti("file-system backend",fn,RTLD_NOW,&lib,
              "!backend_activate !backend_getdir "
              "!backend_opendir !backend_readdir !backend_closedir "
              "!backend_init !backend_exit "
              "!backend_exists !backend_get_info !backend_get_meta "
              "!backend_set_meta !backend_get_mime "
              "!backend_monitor_add !backend_monitor_remove "
              "!backend_cp !backend_hardlink !backend_symlink "
              "!backend_mv !backend_rm "
              "!backend_rmdir !backend_mkdir "
              "!backend_mime_handler_get !backend_mime_handler_set "
              "?backend_readlink ?backend_scanner_activate !backend_tag2name "
              "!backend_job_start !backend_job_collect",
              &ff.file_activate,&ff.file_getdir,
              &ff.file_opendir,&ff.file_readdir,&ff.file_closedir,
              &ff.file_init,&ff.file_exit,
              &ff.file_exists,&ff.file_get_info,&ff.file_get_meta,
              &ff.file_set_meta,&ff.file_get_mime,
              &ff.file_m_add,&ff.file_m_rem,
              &ff.file_cp,&ff.file_hardlink,&ff.file_symlink,
              &ff.file_mv,&ff.file_rm,
              &ff.file_rmdir,&ff.file_mkdir,
              &ff.file_mime_handler_get,&ff.file_mime_handler_set,
              &ff.file_readlink,&ff.file_scanner_activate,
              &ff.file_tag2name,&ff.file_job_start,&ff.file_job_collect);
  if((ret!=NOIR_SUCC)&&(ret!=NOIR_PARTIAL)) {
    if(ret==NOIR_NOTFOUND) {
      E(0,"\"%s\" is not a valid backend!  STOP.\n",fn);
      goto fail_free; }
    else {
      if(!(err=dlerror()))
        err="???";
      E(0,"failed to load \"%s\": %s\n",fn,err);
      goto fail_free; }}

  free(fn);
  ff.file_init(qh);

  { size_t l=strlen(path_setup)+sizeof(DIR_CACHE)+1;
    char *dir=malloc(l);
    if(dir) {
      snprintf(dir,l,"%s/" DIR_CACHE,path_setup);
      file_mkdir(dir,S_IRWXU,TRUE);
      free(dir); }}

  prop_bag_prop_default(props_global,"chdir-warnings","1",0,1,1);
  prop_bag_prop_default(props_global,"prescan-cp","1",0,1,1);
  prop_bag_prop_default(props_global,"prescan-mv","1",0,1,1);
  prop_bag_prop_default(props_global,"prescan-rm","1",0,1,1);

  prop_bag_prop_listen(props_global,LISTENER,"chdir-warnings",file_num_listener,FI_CHDIR_WARNINGS,&fi_param);
  prop_bag_prop_listen(props_global,LISTENER,"prescan-cp",    file_num_listener,FI_PRESCAN_CP,    &fi_param);
  prop_bag_prop_listen(props_global,LISTENER,"prescan-mv",    file_num_listener,FI_PRESCAN_MV,    &fi_param);
  prop_bag_prop_listen(props_global,LISTENER,"prescan-rm",    file_num_listener,FI_PRESCAN_RM,    &fi_param);

  file_annotation_init();

  mime_alii_load();

  return NOIR_SUCC;

  fail_free:
  free(fn);
  fail:
/* return NOIR_META_DLFAIL; */
  exit(EXIT_FAILURE); }



/**
 * cleanup.
 * @return an error-code
 */
int file_exit(void) {
  file_monitor_remove_all(NULL);
  ff.file_exit();
  dlclose(lib);
  memset(&ff,0,sizeof(struct _file_fun));

  if(ht_mime_alii)
    hash_table_dst(&ht_mime_alii);
  if(ht_user)
    hash_table_dst(&ht_user);
  if(ht_group)
    hash_table_dst(&ht_group);
  if(ht_annotation)
    hash_table_dst(&ht_annotation);

  prop_bag_prop_deaf(props_global,LISTENER,NULL,NULL);

  return NOIR_SUCC; }
