/* GNOME-VFS access to file-system.
   TODO:  'engine' and 'getdir' should use async functions */


#include "evidence.h"
#include "file.h"
#include "file_fun.h"
#include "file_lcl.h"
#include "tree.h"

#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-ops.h>  /* monitor */
#include <libgnomevfs/gnome-vfs-mime-utils.h> /* get type */
#include <libgnomevfs/gnome-vfs-mime-handlers.h> /* get app */

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>  /* malloc */
#include <unistd.h>



typedef struct _vfs_rec {
  char *op;
  int state; } vfs_rec;



static int backend_xlate_perms(GnomeVFSFilePermissions p) {
  int m=0;

  m|=(p&GNOME_VFS_PERM_SUID)?S_ISUID:0;
  m|=(p&GNOME_VFS_PERM_SGID)?S_ISGID:0;
  m|=(p&GNOME_VFS_PERM_STICKY)?01000:0;

  m|=(p&GNOME_VFS_PERM_USER_READ)?S_IRUSR:0;
  m|=(p&GNOME_VFS_PERM_USER_WRITE)?S_IWUSR:0;
  m|=(p&GNOME_VFS_PERM_USER_EXEC)?S_IXUSR:0;

  m|=(p&GNOME_VFS_PERM_GROUP_READ)?S_IRGRP:0;
  m|=(p&GNOME_VFS_PERM_GROUP_WRITE)?S_IWGRP:0;
  m|=(p&GNOME_VFS_PERM_GROUP_EXEC)?S_IXGRP:0;

  m|=(p&GNOME_VFS_PERM_OTHER_READ)?S_IROTH:0;
  m|=(p&GNOME_VFS_PERM_OTHER_WRITE)?S_IWOTH:0;
  m|=(p&GNOME_VFS_PERM_OTHER_EXEC)?S_IXOTH:0;

  return m; }



void backend_monitor_event(GnomeVFSMonitorHandle *handle,const gchar *monitor_uri,const gchar *info_uri,GnomeVFSMonitorEventType event_type,gpointer user_data) {
  /* if it's a dir-monitor, the file is in info_uri */

  const char *r=NULL;
  int nfe[]={ NOIR_MONITOR_CHG,
              NOIR_MONITOR_DEL,
              NOIR_MONITOR_NEW,
              NOIR_MONITOR_META };
  int nme=nfe[event_type];

  if(info_uri&&strcmp(info_uri,monitor_uri)) {
    r=info_uri;
    E(0,"event %d for %s -> %s\n",nme,monitor_uri,r); }
  else {
    r=monitor_uri;
    E(0,"event %d for %s/\n",nme,r); }

  file_monitor_event((noir_monitor *)user_data,nme); }



int backend_monitor_add(noir_monitor *m) {
  GnomeVFSMonitorHandle *h;

  if(!m)
    return NOIR_FAIL;

  if(gnome_vfs_monitor_add(&h,m->have,
                           (type==NOIR_MONITOR_FILE)?GNOME_VFS_MONITOR_FILE:
                                                     GNOME_VFS_MONITOR_DIRECTORY,
                           backend_monitor_event,m))
    return NOIR_FAIL;
  return NOIR_SUCC; }




int backend_monitor_remove(noir_monitor *m) {
  if(handle) {
    gnome_vfs_monitor_cancel((GnomeVFSMonitorHandle *)m->handle);
    return NOIR_SUCC; }
  return NOIR_NODATA; }



/* ext -> mime-type */
char *backend_mime_get(char *file) {
  return gnome_vfs_get_mime_type(file); }



/* must return stat */
int backend_exists(char *file) {
  int              m=0;
  GnomeVFSFileInfo fi;

  if(!gnome_vfs_get_file_info(file,&fi,0)) {
    int is_link=0;
    if((fi.valid_fields&GNOME_VFS_FILE_INFO_FIELDS_TYPE)&&(fi.flags&GNOME_VFS_FILE_FLAGS_SYMLINK)) {
      is_link=(!gnome_vfs_get_file_info(fi.symlink_name,&fi,GNOME_VFS_FILE_INFO_FOLLOW_LINKS))?1:2;
      m=S_IFLNK; }

    if(fi.valid_fields&GNOME_VFS_FILE_INFO_FIELDS_TYPE) {
       int tu[]= { 0, S_IFREG, S_IFDIR, S_IFIFO, S_IFSOCK, S_IFCHR, S_IFBLK, S_IFLNK };
       m|=tu[fi.type]; }

    if(fi.valid_fields&GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS)
      m|=backend_xlate_perms(fi.permissions); }

  return m; }



int backend_activate(char *file,char *mime,char *app) {
  GList *handlers,*h;

  if(!mime)
    mime=backend_mime_get(file);

  if((handlers=gnome_vfs_mime_get_short_list_applications(mime))) {
    for(h=g_list_first(handlers);h;h=g_list_next(h))
      fprintf(stderr,"evidence: \"%s\": (%s/%s/%s)...\n",file,
              ((GnomeVFSMimeApplication *)h->data)->id,
              ((GnomeVFSMimeApplication *)h->data)->name,
              ((GnomeVFSMimeApplication *)h->data)->command);

    fprintf(stderr,"evidence: activating \"%s\" (%s => %s)...\n",file,mime,app);

    g_list_free(handlers); }

  return NOIR_SUCC; }



int backend_get_info(noir_sess *ns,char *file,file_info **info) {
  int               ret=NOIR_FAIL,
                    made_new=0,
                    got_stat=FALSE;
  posixmd          *i=NULL;
  char             *mime=NULL;
  GnomeVFSFileInfo  fi;

  if(!info)
    return NOIR_FAIL;

  /* no file_info was handed in, make new one */
  if(!*info) {
    file_info *ap=file_info_fetch(NULL,file);
    made_new=1;
    if(!(*info=file_info_new(ap?ap->name:NULL,file)))
      return NOIR_FAIL;
    (*info)->children=ap?ap->children:0; }



  /* make room for stat-data if needed */
  if(!(i=(*info)->intrinsic)) {
    if(!(i=malloc(sizeof(posixmd)))) {
      if(made_new) {
        file_info_dst(*info);
        *info=NULL; }
      return NOIR_OOM; }
    else
      memset(i,0,sizeof(posixmd)); }



  /* fill in stat-data */
  if(!gnome_vfs_get_file_info(file,&fi,GNOME_VFS_FILE_INFO_GET_MIME_TYPE)) {
    int      is_link=0;

    if(i)
      i->lmode=i->mode=0;

    if((fi.valid_fields&GNOME_VFS_FILE_INFO_FIELDS_TYPE)&&(fi.flags&GNOME_VFS_FILE_FLAGS_SYMLINK)) {
      /* we will call this a symlink, but return the perms of the referenced
         file. 0 - not a link, 1 - symlink, 2 - broken link */
      i->lmode|=S_IFLNK|backend_xlate_perms(fi.permissions);
      is_link=(!gnome_vfs_get_file_info(fi.symlink_name,&fi,
                                        GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
                                        GNOME_VFS_FILE_INFO_FOLLOW_LINKS))?1:2; }

    if(fi.valid_fields&GNOME_VFS_FILE_INFO_FIELDS_TYPE) {
       int tu[]= { 0, S_IFREG, S_IFDIR, S_IFIFO, S_IFSOCK, S_IFCHR, S_IFBLK, S_IFLNK };
       i->mode|=tu[fi.type]; }

    if(fi.valid_fields&GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS) {
      /* GNOME perms are identical to the UNIX ones for now, but let's keep
         this portable just in case. */
      i->mode|=backend_xlate_perms(fi.permissions); }

    ret=NOIR_SUCC;

    if(i) {
      i->hardlinks=fi.link_count;
      i->uid      =fi.uid;
      i->gid      =fi.gid;
      i->size     =fi.size;
      i->atime    =fi.atime;
      i->mtime    =fi.mtime;
      i->ctime    =fi.ctime;

      got_stat=TRUE;

#if 0
      (*info)->flags&=~(IF_NOUSE|IF_NOMODIFY);
      (*info)->flags|=get_xs(file,i->uid,i->gid,l?l:m);
#endif

      (*info)->have|=FF_STAT;
      (*info)->intrinsic=i; }

    if(fi.valid_fields&GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE)
      mime=fi.mime_type;

#if 0
    mime2=NULL;
    if(is_link)
      mime2=((is_link==1)?"symlink":"broken link");
    else if(S_ISREG(m)) {
      if((m&S_IXUSR)||(m&S_IXGRP)||(m&S_IXOTH))
        mime2=MIME_EXECUTABLE; }
    else if(S_ISDIR(m))
      mime2=MIME_FOLDER;
    else if(S_ISCHR(m))
      mime2=MIME_CHAR;
    else if(S_ISBLK(m))
      mime2=MIME_BLK;
    else if(S_ISFIFO(m))
      mime2=MIME_FIFO;
    else if(S_ISSOCK(m))
      mime2=MIME_SOCKET;
    else
      mime2=MIME_DK_FILE;

    mime=mime?mime:(mime2?mime2:(mime?mime:MIME_DK_TYPE));
#else
    if(!mime)
      mime=MIME_DK_TYPE;
#endif

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



static gint backend_progress(GnomeVFSXferProgressInfo *info,gpointer data) {
  char    *ps[]= { "init", "checking destination", "collecting file-list",
                   "collected file-list", "opening source", "opening target",
                   "copying", "moving", "reading", "writing", "closing source",
                   "closing target", "deleting source",
                   "setting attributes on target", "file completed",
                   "cleaning up", "all done" };
  vfs_rec *myrec=(vfs_rec *)data;
  int      s=myrec->state;

  myrec->state=0;

  fprintf(stderr,
          "%s::%s \t %4ld/%4ld file(s) \t %ld/%ld byte(s) total \t "
          "%ld/%ld byte(s) in file\n\t %s -> %s (%s)\n",
          myrec->op,ps[info->phase],
          info->file_index,info->files_total,
          (long)info->total_bytes_copied,(long)info->bytes_total,
          (long)info->bytes_copied,(long)info->file_size,
          info->source_name?info->source_name:"",
          info->target_name?info->target_name:"",
          info->duplicate_name?info->duplicate_name:"");

  if(info->status==GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR) {
    switch(info->vfs_status) {
    case GNOME_VFS_ERROR_FILE_EXISTS:
      return GNOME_VFS_XFER_OVERWRITE_ACTION_REPLACE_ALL;

    case GNOME_VFS_ERROR_NOT_FOUND:
    case GNOME_VFS_ERROR_GENERIC:
    case GNOME_VFS_ERROR_INTERNAL:
    case GNOME_VFS_ERROR_TOO_BIG:
    case GNOME_VFS_ERROR_NO_SPACE:
    case GNOME_VFS_ERROR_READ_ONLY:
    case GNOME_VFS_ERROR_ACCESS_DENIED:
    case GNOME_VFS_ERROR_TOO_MANY_OPEN_FILES:
    case GNOME_VFS_ERROR_IN_PROGRESS:
    case GNOME_VFS_ERROR_LOOP:
    case GNOME_VFS_ERROR_NOT_PERMITTED:
    case GNOME_VFS_ERROR_NO_MEMORY:
    case GNOME_VFS_ERROR_HOST_NOT_FOUND:
    case GNOME_VFS_ERROR_INVALID_HOST_NAME:
    case GNOME_VFS_ERROR_HOST_HAS_NO_ADDRESS:
    case GNOME_VFS_ERROR_LOGIN_FAILED:
    case GNOME_VFS_ERROR_DIRECTORY_BUSY:
    case GNOME_VFS_ERROR_DIRECTORY_NOT_EMPTY:
    case GNOME_VFS_ERROR_TOO_MANY_LINKS:
    case GNOME_VFS_ERROR_READ_ONLY_FILE_SYSTEM:
    case GNOME_VFS_ERROR_SERVICE_NOT_AVAILABLE:
      if((s++)>3)
        return GNOME_VFS_XFER_ERROR_ACTION_SKIP;
      sleep(myrec->state=s);

    case GNOME_VFS_ERROR_INTERRUPTED:
      return GNOME_VFS_XFER_ERROR_ACTION_RETRY;

    default:
       return GNOME_VFS_XFER_ERROR_ACTION_SKIP; }}

  return 0; }



static int backend_engine(char *op,char *fm,char *to,GnomeVFSXferOptions xferopts) {
  int             ret=NOIR_FAIL;
  GnomeVFSResult  res;
  GnomeVFSURI    *src=gnome_vfs_uri_new(fm);
  GnomeVFSURI    *tgt=gnome_vfs_uri_new(to);
  vfs_rec         myrec;

  myrec.op        =op;
  myrec.state     =0;

  if(!src||!tgt) {
    if(src) gnome_vfs_uri_unref(src);
    if(tgt) gnome_vfs_uri_unref(tgt);
    return ret; }

  res=gnome_vfs_xfer_uri(src,tgt,
                         GNOME_VFS_XFER_RECURSIVE|
                         GNOME_VFS_XFER_NEW_UNIQUE_DIRECTORY|xferopts,
                         GNOME_VFS_XFER_ERROR_MODE_QUERY,
                         GNOME_VFS_XFER_OVERWRITE_MODE_QUERY,
                         backend_progress,&myrec);

  gnome_vfs_uri_unref(src);
  gnome_vfs_uri_unref(tgt);

  return res?NOIR_FAIL:NOIR_SUCC; }



int backend_mv(char *fm,char *to) {
  return backend_engine("Moving",fm,to,GNOME_VFS_XFER_FOLLOW_LINKS|GNOME_VFS_XFER_DELETE_ITEMS); }



int backend_cp(char *fm,char *to) {
  return backend_engine("Copying",fm,to,GNOME_VFS_XFER_FOLLOW_LINKS); }



int backend_mkdir(char *dir,int perms,int hier) {
  return NOIR_IGNORED; }



int backend_rm(char *fm) {
  return backend_engine("Copying",fm,NULL,GNOME_VFS_XFER_REMOVESOURCE); }



int backend_symlink(char *fm,char *to) {
   /* use gnome_vfs_create_symbolic_link(src_uri,target_str); ? */
  return backend_engine("Linking",fm,to,GNOME_VFS_XFER_LINK_ITEMS); }



int backend_hardlink(char *fm,char *to) {
 return NOIR_IGNORED; }



gboolean backend_get_dir_entry(const gchar *rel_path,GnomeVFSFileInfo *info,gboolean recursing_will_loop,gpointer data,gboolean *recurse) {
  noir_sess *ns=(noir_sess *)data;
  char      *abs;
  size_t     lp=strlen(ns->gdb);
  size_t     l=strlen(rel_path)+lp+2;

  if(!(abs=malloc(l)))
    return FALSE;

  strcpy(abs,ns->gdb);

  if(lp>1) {
    abs[lp]='/';
    strcpy(&abs[lp+1],rel_path); }
  else
    strcpy(&abs[lp],rel_path);

/*  tree_get_file(ns,abs,(char *)rel_path,NULL); */   /* FIXME ### */

/*
  gnome_vfs_file_info_unref(info); */

  *recurse=FALSE;
  free(abs);
  return TRUE; }



int backend_get_dir(noir_sess *ns,char *path) {
  int ret;
  while(ns->gdb);
  ns->gdb=path;
  ret=gnome_vfs_directory_visit(path,GNOME_VFS_FILE_INFO_FOLLOW_LINKS,GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK,backend_get_dir_entry,ns)?NOIR_FAIL:NOIR_SUCC;
  ns->gdb=NULL;
  return ret; }



int backend_exit(void) {
  gnome_vfs_shutdown();
  return NOIR_SUCC; }



int backend_init(noir_sess *ns) {
  gnome_vfs_init();
  return NOIR_SUCC; }
