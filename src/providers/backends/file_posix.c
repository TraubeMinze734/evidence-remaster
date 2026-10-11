/** @file  file_posix.c
 *  @brief provides services for file_direct.c
 *
 * by Azundris, based on code from efsd by Christian Kreibich
 */


/*
Copyright (C) 2000, 2001 Christian Kreibich <cK@whoop.org>.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies of the Software and its documentation and acknowledgment shall be
given in the documentation and software packages that this Software was
used.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/



#include "config.h"
#include "debug.h"
#include "file.h"
#include "file_direct.h"
#include "errors.h"
#include "util.h"               /* now() */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>



/*********************************************************************************************/
/* lowest level routines */



/**
 * checks if file paths are identical after making them canonic
 * @return -2 on error, FALSE if files differ, TRUE otherwise.
 * @param file1  first file
 * @param file2  second file
 */
static int posix_files_identical(const char *file1,const char *file2) {
  char real1[MAXPATHLEN];
  char real2[MAXPATHLEN];

  if(realpath(file1,real1)&&realpath(file2,real2)) {
    if(!strcmp(real1,real2))
      return TRUE;
    return FALSE; }

  E(0,"couldn't realpath files (\"%s\",\"%s\").\n",file1,file2);
  perror("posix_files_identical: \n");
  return -2; }



static int posix_remove(const char *filename) {
  struct stat st;

  if(!filename||!filename[0]) {
    errno=EINVAL;
    return NOIR_NODATA; }

  if(lstat(filename,&st)<0)
    return NOIR_NOFILE;

  if(remove(filename)==0)
    return NOIR_SUCC;
  return NOIR_FAIL; }



static int posix_rename(const char *file1,const char *file2) {
  D_ENTER;

  if(!file1||!file1[0]||
     !file2||!file2) {
    errno=EINVAL;
    D_RETURN_(FALSE); }

  if(rename(file1,file2)==0)
    D_RETURN_(TRUE);
  
  D_RETURN_(FALSE); }



static char  *posix_get_filename_only(const char *path) {
  char *result=NULL;

  D_ENTER;

  if(!path||!path[0])
    D_RETURN_(NULL);

  if((result=strrchr(path,'/'))!=NULL)
    result++;

  D_RETURN_(result); }






/*********************************************************************************************/



/**
 * copy the actual contents of a file
 * @return an error-code
 * @param job       the job or NULL (for file_job_stats())
 * @param src_path  the source-file
 * @param src_st    stat() info of source-object
 * @param dst_path  the destination-file
 */
static inline int posix_copy_data(noir_job *job,const char *src_path,struct stat *src_st,const char *dst_path) {
  int           ret=NOIR_FAIL;
  int           src_fd=-1,
                dst_fd=-1,
                block_size=4096;
  char         *buf=NULL,
                hole_at_end=FALSE,
                check_for_holes=FALSE;
  long          got=0;

  E(1,"\"%s\" to \"%s\"...\n",src_path,dst_path);

  if((src_fd=open(src_path,O_RDONLY))<0)
    return NOIR_NOFILE;

  umask(000);

  if((dst_fd=open(dst_path,O_WRONLY|O_CREAT|O_TRUNC,src_st->st_mode))<0) {
    umask(077);
    goto done; }

  umask(077);

#ifdef HAVE_ST_BLKSIZE
  { struct  stat  dst_st;
    if(stat(dst_path,&dst_st)==0)
      block_size=(int)dst_st.st_blksize;
#  ifdef HAVE_ST_BLOCKS
    if(S_ISREG(src_st->st_mode)&&
      ((src_st->st_size/src_st->st_blksize)>src_st->st_blocks))
      check_for_holes=TRUE;
#  endif
  }
#else
#  warning no st_blksize in struct stat...
#endif

  if(check_for_holes)
    E(0,"Using holes, buffer size %i\n",block_size);

  buf=malloc(block_size);
  { long last_stats=-1;   /* mtime of last progress update; the 2005 code called
                             file_job_stats() (mutex + GUI handshake) for
                             EVERY 4K block, which dominated copy time */

  for(;;) {
    int  i;
    int  num_read;
    char write_hole=FALSE;

    if((num_read=read(src_fd,buf,block_size))<0) {
      if(errno==EINTR)
        continue;
      goto done; }

    if(!num_read) /* End of file */
      break;

    got+=num_read;

    if(check_for_holes) {
      write_hole=TRUE;

      for(i=0;i<num_read;i++) {
        if(buf[i]!=0) {
          write_hole=FALSE;
          break; }}}

    if(write_hole) {
      if(lseek(dst_fd,(off_t)num_read,SEEK_CUR)<0L)
        goto done;

      hole_at_end=TRUE; }
    else {
      char *buf_ptr;
      int   n;

      /* Write out stuff, decreasing num_read again */

      buf_ptr=buf;

      while(num_read>0) {
        if((n=write(dst_fd,buf_ptr,num_read))<0) {
          if(errno==EINTR)
            continue;

          goto done; }

        num_read-=n;
        buf_ptr+=n; }

      hole_at_end=FALSE; }

    { long tme=now(0);
      if(last_stats!=tme/100) {   /* ~every 100ms, and ... */
        file_job_stats(job,src_st->st_size,got,0,0,0,0);
        last_stats=tme/100; } } }

  file_job_stats(job,src_st->st_size,got,0,0,0,0);   /* ... final update so the file counts fully */
  
  /* Make sure things end properly at destination file */

  if(hole_at_end) {
    if(lseek(dst_fd,-1,SEEK_CUR)<0L) {
      E(0,"Adding hole at eof.\n");

      for(;;) {
        if(write(dst_fd,"",1)<0) {
          if(errno==EINTR)
            continue;
          goto done; }

        break; }}}

  E(1,"succeeded.\n");

  ret=NOIR_SUCC;

 done:
  if(buf)
    free(buf);
  if(src_fd>=0)
    close(src_fd);
  if(dst_fd>=0)
    close(dst_fd);
  
  return ret; }
  }  /* end of last_stats scope */



/* Azundris 2003/05/27 */

#ifdef HAVE_LIBATTR
/* this routine adapted from GPL'd code by Andreas Gruenbacher <agruen@suse.de>
   SuSE Linux AG. */
static int posix_copy_ea_check(const char *name) {
  /* skip POSIX ACLs */
  if(!strncmp(name,"system.posix_acl_",17)&&
     (!strcmp(name+17,"access")||!strcmp(name+17,"default")))
    return 0;

  /* Skip permissions attributes which are used on IRIX, and
     hence are part of the XFS ondisk format (incl. ACLs) */
  if(!strncmp(name,"trusted.SGI_",12)&&
     (!strcmp(name+12,"ACL_DEFAULT")||
      !strcmp(name+12,"ACL_FILE")||
      !strcmp(name+12,"CAP_FILE")||
      !strcmp(name+12,"MAC_FILE")))
    return 0;

  /* The xfsroot namespace mirrored attributes, some of which are also also
     available via the system.* and trusted.* namespaces.  To avoid the
     problems this would cause, we skip xfsroot altogether.
     Note: xfsroot namespace has now been removed from XFS. */
  if(!strncmp(name,"xfsroot.",8))
    return 0;

  return 1; }
#endif



#if defined(HAVE_ATTR_COPY_FILE)
/* this routine adapted from GPL'd code by Andreas Gruenbacher <agruen@suse.de>
   SuSE Linux AG. */
static void posix_copy_ea_error(struct error_context *ctx,const char *fmt,...) {
  va_list ap;
  va_start(ap,fmt);
  if(vfprintf(stderr,fmt,ap))
    fprintf(stderr,": ");
  fprintf(stderr,"#%d -- %s\n",errno,strerror(errno));
  va_end(ap); }
#endif



static int posix_copy_ea(const char *src_path,const char *dst_path) {
  int   success=TRUE;

#ifdef HAVE_LIBATTR
  if(!libattr)
    D_RETURN_(FALSE);

#  if defined(HAVE_ATTR_COPY_FILE)
  if(libattr->attr_copy_file) {
    struct error_context ctx;
    ctx.error=posix_copy_ea_error;
    ctx.quote=NULL;
    ctx.quote_free=NULL;
    if(libattr->attr_copy_file(src_path,dst_path,NULL,&ctx))
      success=FALSE; }
  else
#  endif
/* this routine adapted from GPL'd code by Andreas Gruenbacher <agruen@suse.de>
   SuSE Linux AG. */
  { ssize_t size;
    char *names=NULL,*end_names,*name,*value=NULL;

    success=FALSE;

    /* get list of EAs */
    if((size=libattr->listxattr(src_path,NULL,0))<0) {
      E(0,"could not get EAs for \"%s\"...\n",src_path);
      goto done; }

    if(!(names=(char *)malloc(size+1)))
      goto done;

    if((size=libattr->listxattr(src_path,names,size))<0) {
      free(names);
      goto done; }

    success=TRUE;

    names[size]='\0';
    end_names=names+size;
    
    for(name=names;name!=end_names;name=strchr(name,'\0')+1) {
      void *old_value;
      
      /* check if this attribute shall be preserved */
      if(!*name||!posix_copy_ea_check(name))
        continue;
      
      size=getxattr(src_path,name,NULL,0);
      if(size<0) {
        E(0,"couldn't get %s from \"%s\"...\n",name,src_path);
        continue; } /* may not have permission to access */

      if(size&&!(value=(char *)realloc(old_value=value,size))) {
        free(old_value);
        success=FALSE;
        continue; }

      if((size=libattr->getxattr(src_path,name,value,size))<0) {
        success=FALSE;
        continue; }

      if(libattr->setxattr(dst_path,name,value,size,0)) {
        E(0,"couldn't set %s on \"%s\"...\n",name,dst_path);
        if(errno==ENOSYS) {
          success=FALSE;
          break; }}}
  done:
    if(value)
      free(value);
    free(names); }
#endif

  D_RETURN_(success); }



#ifdef HAVE_LIBACL
static inline int posix_min_acl_p(acl_t acl) {
  acl_entry_t ent;
  acl_tag_t   ent_type;
  int         min=TRUE,got;

  got=libacl->acl_get_entry(acl,ACL_FIRST_ENTRY,&ent);
  while(min&&(got>0)) {
    libacl->acl_get_tag_type(ent,&ent_type);
    if((ent_type==ACL_USER)||
       (ent_type==ACL_GROUP)||
       (ent_type==ACL_UNDEFINED_TAG))
      min=FALSE;
    got=libacl->acl_get_entry(acl,ACL_NEXT_ENTRY,&ent); }
  return min; }
#endif



static int posix_copy_acl(const char *src_path,struct stat *src_st,const char *dst_path) {
  int   success=TRUE;

  chmod(dst_path,0700);

  if(!posix_copy_ea(src_path,dst_path)) {
    E(0,"EA-copy failed\n");
    success=FALSE; }

#ifdef HAVE_LIBACL
  if(libacl) {
    acl_t       acl;

    if((acl=libacl->acl_get_file(src_path,ACL_TYPE_ACCESS))) {
      /* if it's the minum ACL, don't try to copy it, chmod() will set
         the perms, anyway.  more importantly, this prevents problems
         with copying from a file-system with ACLs to one without. */
      if(posix_min_acl_p(acl))
        E(2,"ACL is minimum ACL, do nothing...\n");
      else if(libacl->acl_set_file(dst_path,ACL_TYPE_ACCESS,acl)) {
        success=FALSE;
        E(0,"could not set ACL for \"%s\": #%d - %s\n",dst_path,errno,strerror(errno)); }
      else
        E(1,"ACL copied!\n");
      libacl->acl_free(acl); }
    else if(errno==ENOTSUP) {
      E(2,"using traditional permissions on \"%s\", ACLs not supported on source file-system.\n",src_path);
      goto acl_done; }
    else {
      success=FALSE;
      E(0,"could not read ACL from \"%s\": #%d - %s\n",src_path,errno,strerror(errno)); }

    if(S_ISDIR(src_st->st_mode)) {
      if((acl=libacl->acl_get_file(src_path,ACL_TYPE_DEFAULT))) {
        acl_entry_t ent;
        if(libacl->acl_get_entry(acl,ACL_FIRST_ENTRY,&ent)!=1) {
          E(1,"default ACL empty, do nothing...\n"); }
        else if(libacl->acl_set_file(dst_path,ACL_TYPE_DEFAULT,acl)) {
          success=FALSE;
          E(0,"could not set default-ACL for \"%s\": #%d - %s\n",dst_path,errno,strerror(errno)); }
        else
          E(1,"default ACL copied!\n");
        libacl->acl_free(acl); }
      else {
        success=FALSE;
        E(0,"could not read default-ACL from \"%s\": #%d - %s\n",src_path,errno,strerror(errno)); }}}
  else
    E(2,"no libacl...\n");

  if(!success)
    E(0,"ACL-copy failed\n");

 acl_done:
#endif
  chmod(dst_path,src_st->st_mode);

  D_RETURN_(success); }






/*********************************************************************************************/



static int file_copy(noir_job *job,const char *src_path,struct stat *src_st,const char *dst_path) {
  int  success=FALSE;

  D_ENTER;

  E(1,"\"%s\" to \"%s\"\n",src_path,dst_path);

  if(backend_exists(dst_path)&&
      posix_files_identical(src_path,dst_path)==TRUE) {
    E(0,"files identical, aborting.\n");
    errno=EEXIST;
    D_RETURN_(FALSE); }
    
  if(S_ISLNK(src_st->st_mode)) {
    char realfile[MAXPATHLEN];
    int  length;

    if((length=readlink(src_path,realfile,MAXPATHLEN))<0) {
      perror("Readlink error");
      success=FALSE; }

    /* Terminate the thing, dammit. */
    realfile[length]='\0';

    if(realfile[0]!='/') {
      char realcopy[MAXPATHLEN];
      char *lastslash;

      snprintf(realcopy,MAXPATHLEN,src_path);
      lastslash=strrchr(realcopy,'/');

      if(!lastslash) {
        E(0,"Source file \"%s\" is not absolute...\n",src_path);
        D_RETURN_(FALSE); }

      snprintf(lastslash+1,MAXPATHLEN-(lastslash-realcopy),"%s",realfile);
      snprintf(realfile,MAXPATHLEN,realcopy); }

    if(symlink(realfile,dst_path)<0) {
      E(0,"Error symlinking from \"%s\" to \"%s\"...\n",realfile,dst_path);
      perror("Symlink error");
      success=FALSE; }
    else {
      success=TRUE;
      E(1,"created symlink from \"%s\" to \"%s\".\n",realfile,dst_path); }}

  else if(S_ISFIFO(src_st->st_mode)) {
    if(mkfifo(dst_path,src_st->st_mode)==0)
      success=FALSE; }
  else if(S_ISCHR(src_st->st_mode)||
          S_ISBLK(src_st->st_mode)||
          S_ISSOCK(src_st->st_mode)) {
    if(mknod(dst_path,src_st->st_mode,src_st->st_dev)==0)
      success=FALSE; }
  else
    success=(posix_copy_data(job,src_path,src_st,dst_path)==NOIR_SUCC);

  if(!success) {
    /* Whatever we have here as src (e.g a directory),
       we cannot copy onto a normal file. */
    E(0,"file copy error.\n");
    errno=EIO;
    D_RETURN_(FALSE); }

  /* Azundris 2003/05/27 */
  else
    success=posix_copy_acl(src_path,src_st,dst_path);

  E(1,"file \"%s\" finished.\n",src_path);

  /* Return final status. */
  D_RETURN_(success); }



static int dir_copy(noir_job *job,const char *src_path,struct stat *src_st,const char *dst_path) {
  char           src[MAXPATHLEN];
  char           dst[MAXPATHLEN];
  char          *src_ptr,*dst_ptr;
  int            src_len,dst_len;
  struct stat    src_st2;
  DIR           *dir;
#ifdef HAVE_READDIR_R
  struct dirent  de;
#endif
  struct dirent  *de_ptr;
  int            success=TRUE;

  D_ENTER;
  E(1,"\"%s\" to \"%s\"...\n",src_path,dst_path);

  file_job_update(job,NOIR_JOB_PROGRESS,NOIR_NOERRORCODE,src_path,dst_path);

  umask(000);

  if(mkdir(dst_path,src_st->st_mode)<0) {
    umask(077);
    if(errno==EEXIST) {
      file_job_update(job,NOIR_JOB_PROGRESS,NOIR_FAIL,NULL,NULL);
      if(file_job_ask(job,NULL,"Target folder \"%s\" already exists -- overwrite?",dst_path)!=NOIR_SUCC)
        D_RETURN_(FALSE);}
    else {
      file_job_update(job,NOIR_JOB_PROGRESS,NOIR_FAIL,NULL,NULL);
      if(file_job_ask(job,NULL,"Could not create target folder\n\"%s\".\nContinue with the next source-item rather than abort?",dst_path)!=NOIR_SUCC)
        D_RETURN_(FALSE);}}

  umask(077);

  /* Azundris 2003/05/27 */
  if(!posix_copy_acl(src_path,src_st,dst_path)) {
    file_job_update(job,NOIR_JOB_PROGRESS,NOIR_FAIL,NULL,NULL);
    if(file_job_ask(job,NULL,"Failed to set permissions on target folder \"%s\", continue?",dst_path)!=NOIR_SUCC) {
      rmdir(dst_path);
      D_RETURN_(FALSE); }}

  E(2,"Directory \"%s\" created.\n",dst_path);

  if((dir=opendir(src_path))==NULL) {
    file_job_update(job,NOIR_JOB_PROGRESS,NOIR_FAIL,NULL,NULL);
    if(file_job_ask(job,NULL,"Could not read entries in source-directory\n\"%s\",\ncontinue without copying its contents?",src_path)!=NOIR_SUCC)
      D_RETURN_(FALSE);
    else
      D_RETURN_(TRUE); }

  snprintf(src,MAXPATHLEN,"%s/",src_path);
  snprintf(dst,MAXPATHLEN,"%s/",dst_path);
  src_len=strlen(src);
  dst_len=strlen(dst);
  src_ptr=src+src_len;
  dst_ptr=dst+dst_len;
  
  /* Read dir, using readdir_r() when we're
     threaded, readdir() otherwise... */

  chmod(dst_path,0700);

  for(READDIR(dir,de,de_ptr);de_ptr;READDIR(dir,de,de_ptr)) {
    if(!strcmp(de_ptr->d_name,".")||!strcmp(de_ptr->d_name,".."))
      continue;

    snprintf(src_ptr,MAXPATHLEN-src_len,"%s",de_ptr->d_name);
    snprintf(dst_ptr,MAXPATHLEN-dst_len,"%s",de_ptr->d_name);

    /* UNCHANGED + NOERRORCODE: update src/dst without GUI handshake */
    file_job_update(job,NOIR_JOB_UNCHANGED,NOIR_NOERRORCODE,src,dst);

    if(lstat(src,&src_st2)<0)
      continue;

    if(S_ISDIR(src_st2.st_mode))
      success=dir_copy(job,src,&src_st2,dst);
    else
      success=file_copy(job,src,&src_st2,dst); }

  chmod(dst_path,src_st->st_mode);

  closedir(dir);

  D_RETURN_(success); }






/*********************************************************************************************/



static int file_move(noir_job *job,const char *src_path,struct stat *src_st,const char *dst_path) {
  int success=0;

  D_ENTER;

  E(1,"\"%s\" to \"%s\"...\n",src_path,dst_path);

  /* Try simple rename... */
  if(file_copy(job,src_path,src_st,dst_path)) {
    E(1,"Removing source \"%s\"...\n",src_path);
    success=(posix_remove(src_path)==NOIR_SUCC);
    D_RETURN_(success); }
  D_RETURN_(FALSE); }



static int dir_move(noir_job *job,const char *src_path,const char *dst_path) {
  char           src[MAXPATHLEN];
  char           dst[MAXPATHLEN];
  char          *src_ptr,*dst_ptr;
  int            src_len,dst_len;
  struct stat    src_st;
  DIR           *dir;
#ifdef HAVE_READDIR_R
  struct dirent  de;
#endif
  struct dirent  *de_ptr;

  D_ENTER;
  E(1,"\"%s\" to \"%s\"...\n",src_path,dst_path);

  if(stat(src_path,&src_st)<0)
    D_RETURN_(FALSE);

  umask(000);

  file_job_update(job,NOIR_JOB_PROGRESS,NOIR_NOERRORCODE,src_path,dst_path);

  /* The dst directory should not exist yet. */

  if(mkdir(dst_path,src_st.st_mode)<0) {
    umask(077);
    if(errno==EEXIST) {
      file_job_update(job,NOIR_JOB_PROGRESS,NOIR_FAIL,NULL,NULL);
      if(file_job_ask(job,NULL,"Target folder \"%s\" already exists -- overwrite?",dst_path)!=NOIR_SUCC)
        D_RETURN_(FALSE);}
    else {
      file_job_update(job,NOIR_JOB_PROGRESS,NOIR_FAIL,NULL,NULL);
      if(file_job_ask(job,NULL,"Could not create target folder\n\"%s\".\nContinue with the next source-item rather than abort?",dst_path)!=NOIR_SUCC)
        D_RETURN_(FALSE);}}

  umask(077);

  /* Azundris 2003/05/27 */
  if(!posix_copy_acl(src_path,&src_st,dst_path)) {
    file_job_update(job,NOIR_JOB_PROGRESS,NOIR_FAIL,NULL,NULL);
    if(file_job_ask(job,NULL,"Failed to set permissions on target folder \"%s\", continue?",dst_path)!=NOIR_SUCC) {
      rmdir(dst_path);
      D_RETURN_(FALSE); }}

  E(2,"Directory \"%s\" created.\n",dst_path);

  if((dir=opendir(src_path))==NULL) {
    file_job_update(job,NOIR_JOB_PROGRESS,NOIR_FAIL,NULL,NULL);
    if(file_job_ask(job,NULL,"Could not read entries in source-directory\n\"%s\",\ncontinue without copying its contents?",src_path)!=NOIR_SUCC)
      D_RETURN_(FALSE);
    else
      D_RETURN_(TRUE); }

  snprintf(src,MAXPATHLEN,"%s/",src_path);
  snprintf(dst,MAXPATHLEN,"%s/",dst_path);
  src_len=strlen(src);
  dst_len=strlen(dst);
  src_ptr=src+src_len;
  dst_ptr=dst+dst_len;

  /* Read dir, using readdir_r() when we're
     threaded, readdir() otherwise... */

  chmod(dst_path,0700);

  for(READDIR(dir,de,de_ptr);de_ptr;READDIR(dir,de,de_ptr)) {
    if(!strcmp(de_ptr->d_name,".")||!strcmp(de_ptr->d_name,".."))
      continue;

    snprintf(src_ptr,MAXPATHLEN-src_len,"%s",de_ptr->d_name);
    snprintf(dst_ptr,MAXPATHLEN-dst_len,"%s",de_ptr->d_name);

    /* status UNCHANGED + NOERRORCODE only updates src/dst without the
       GUI handshake -- dir_copy() fires one of these per entry, and the
       2005 code slept until the 250ms GUI pulse had consumed each one */
    file_job_update(job,NOIR_JOB_UNCHANGED,NOIR_NOERRORCODE,src,dst);

    if(lstat(src,&src_st)<0) {
      E(0,"source \"%s\" not stat()able!\n",src);
      goto error_exit; }
      
    if(posix_rename(src,dst))
      continue;
      
    if(S_ISDIR(src_st.st_mode)) {
      if(!dir_move(job,src,dst)) {
        E(0,"error moving \"%s\" to \"%s\"!\n",src,dst);
        goto error_exit; }}

    else if(file_move(job,src,&src_st,dst)) {
      continue; }

    else {
      E(0,"error -- we should never get here!?  : (\n");
      goto error_exit; }}

  /* Close dir. It's empty now, so also remove it. */

  chmod(dst_path,src_st.st_mode);

  closedir(dir);
  D_RETURN_(posix_remove(src_path)==NOIR_SUCC);

 error_exit:

  closedir(dir);
  D_RETURN_(FALSE); }






/*********************************************************************************************/



static int  file_remove(const char *path,struct stat *st) {
  char           s[MAXPATHLEN];
  char          *s_ptr;
  int            s_len;
  int            ret;
  DIR           *dir;
#ifdef HAVE_READDIR_R
  struct dirent  de;
#endif
  struct dirent *de_ptr;
  struct stat    st2;

  E(1,"\"%s\"\n",path);

  /* Simply try if it works. */
  if(posix_remove(path)==NOIR_SUCC) {
    thumbnail_disk_op(NOIR_JOB_RM,path,NULL);
    return NOIR_SUCC; }

  /* It's a directory. Remove everything in it recursively,
     then try removing the empty directory. */
  if(S_ISDIR(st->st_mode)) {
    if((dir=opendir(path))==NULL)
      return NOIR_FAIL;

    snprintf(s,MAXPATHLEN,"%s/",path);
    s_len=strlen(s);
    s_ptr=s+s_len;
      
    /* Read dir, using readdir_r() when we're
       threaded, readdir() otherwise... */

    for(READDIR(dir,de,de_ptr);de_ptr;READDIR(dir,de,de_ptr)) {
      if(!strcmp(de_ptr->d_name,".")||!strcmp(de_ptr->d_name,".."))
        continue;
  
      snprintf(s_ptr,MAXPATHLEN-s_len,"%s",de_ptr->d_name);
  
      if(posix_remove(s)==NOIR_SUCC) {
        thumbnail_disk_op(NOIR_JOB_RM,s,NULL);
        continue; }

      if(lstat(s,&st2)<0) {
        /* We couldn't stat it and we couldn't remove it -- report error. */
        closedir(dir);
        return NOIR_FAIL; }

      if(S_ISDIR(st2.st_mode)) {
        int r2;
        /* It's a directoy -- remove recursively */
        if((r2=file_remove(s,&st2))!=NOIR_SUCC) {
          closedir(dir);
          return r2; }}
      else {
        closedir(dir);
        return NOIR_NOTSUPP; }}

    /* We deleted everything in the directory.
       Report result of removing directory itself. */
    closedir(dir);
    if((ret=posix_remove(path))==NOIR_SUCC)
      thumbnail_disk_op(NOIR_JOB_RM,s,NULL);
    return ret; }

  /* It's not a directory either. Report error. */
  return NOIR_NOTSUPP; }






/*********************************************************************************************/



int cp_fs_obj(noir_job *job,int num_files,const char **paths,FsOpt ops) {
  const char  *dst_path=paths[num_files-1];
  const char  *src_path;
  char         s[MAXPATHLEN];
  struct stat  dst_st;
  struct stat  src_st;
  char         orig_dst_stat_succeeded,
               dst_stat_succeeded;
  int          i;

  D_ENTER;

  if(!dst_path||!dst_path[0]) {
      errno=EINVAL;
      D_RETURN_(FALSE); }

  dst_stat_succeeded=orig_dst_stat_succeeded=(stat(dst_path,&dst_st)==0);
  
  /* If we're copying multiple files to a target,
     it has to be a directory. */
  if((num_files>2)&&orig_dst_stat_succeeded&&!S_ISDIR(dst_st.st_mode)) {
    errno=EINVAL;
    D_RETURN_(FALSE); }
  
  /* Go through all files and copy to target (which
     is the last file in the char** array). */

  for(i=0;i<num_files-1;i++) {     
    src_path=paths[i];
    dst_path=paths[num_files-1]; /* Yes -- need to set this every time. */

    if(!src_path||!src_path[0]) {
      errno=EINVAL;
      D_RETURN_(FALSE); }
      
    if(backend_exists(dst_path)&&
       posix_files_identical(src_path,dst_path)==TRUE) {
      E(0,"src and dst are equal in copy -- doing nothing.\n");

      if(ops&FS_OP_FORCE)
        continue;
  
      errno=EEXIST;
      D_RETURN_(FALSE); }
      
    E(1,"copying \"%s\" to \"%s\",\n\trec [%s], force [%s]\n",
      src_path,dst_path,
      (ops&FS_OP_RECURSIVE)?"X":" ",
      (ops&FS_OP_FORCE)?"X":" ");

    if(lstat(src_path,&src_st)<0) {
      E(0,"could not stat source \"%s\".\n",src_path);
      D_RETURN_(FALSE); }
         
    /* If dest exists and is a dir, adjust dest by
       appending last component of file name. */
      
    if(orig_dst_stat_succeeded&&S_ISDIR(dst_st.st_mode)) {
      E(2,"dest exists as dir.\n");
      snprintf(s,MAXPATHLEN,"%s/%s",dst_path,
               posix_get_filename_only(src_path));
      dst_path=s;
      E(1,"adjusted dest to: \"%s\"\n",dst_path);
      dst_stat_succeeded=(stat(dst_path,&dst_st)==0); }
      
      /* Check target -- if it exists and FORCE is
         not used, abort. Also abort if FORCE is given but
         target cannot be removed.
      */
      
    if(dst_stat_succeeded&&!S_ISDIR(dst_st.st_mode)) {
      if((ops&FS_OP_FORCE)==0) {
        E(0,"dest \"%s\" exists and no force used -- aborting.\n",dst_path);
        errno=EEXIST;
        D_RETURN_(FALSE); }

        /* Azundris 2002/12/25 */
      if((posix_files_identical(src_path,dst_path)==TRUE)||
         (strstr(dst_path,src_path)==dst_path)) {
        E(0,"trying to copy file \"%s\" on top of itself -- aborting.\n",src_path);
        errno=EEXIST;
        D_RETURN_(FALSE); }

      if(rm_fs_obj(job,dst_path,FS_OP_RECURSIVE)!=NOIR_SUCC) {
        E(0,"could not remove existing dest \"%s\".\n",dst_path);
        D_RETURN_(FALSE); }

      E(2,"existing target removed.\n"); }

      /* If it's a dir and we're recursive, copy directory. */
    if(S_ISDIR(src_st.st_mode)) {
      if((ops&FS_OP_RECURSIVE)==0) {
        E(0,"src \"%s\" is directory but we're not recursive -- aborting.\n",src_path);
        D_RETURN_(FALSE); }

      if(!dir_copy(job,src_path,&src_st,dst_path))
        D_RETURN_(FALSE);

      continue; }

    /* Otherwise, copy single regular file. */
    if(!file_copy(job,src_path,&src_st,dst_path))
      D_RETURN_(FALSE); }

  D_RETURN_(TRUE); }



int mv_fs_obj(noir_job *job,int num_files,const char **paths,FsOpt ops) {
  const char  *dst_path=paths[num_files-1];
  const char  *src_path;
  char         s[MAXPATHLEN];
  struct stat  dst_st;
  struct stat  src_st;
  char         orig_dst_stat_succeeded,
               dst_stat_succeeded;
  int          i;

  D_ENTER;

  if(!dst_path||!dst_path[0]) {
    errno=EINVAL;
    D_RETURN_(FALSE); }

  dst_stat_succeeded=orig_dst_stat_succeeded=(stat(dst_path,&dst_st)==0);

  /* If we're moving multiple files to a target,
     it has to be a directory. */
  if((num_files>2)&&orig_dst_stat_succeeded&&!S_ISDIR(dst_st.st_mode)) {
    errno=EINVAL;
    D_RETURN_(FALSE); }

  /* Go through all files and copy to target (which
     is the last file in the char** array). */

  for(i=0;i<num_files-1;i++) {     
    src_path=paths[i];
    dst_path=paths[num_files-1]; /* Yes -- need to set this every time. */

    if(backend_exists(dst_path)) {
      if((posix_files_identical(src_path,dst_path)==TRUE)||
         (strstr(dst_path,src_path)==dst_path)) {
        E(0,"cannot move \"%s\" to \"%s\" -- doing nothing.\n",src_path,dst_path);
        if(ops&FS_OP_FORCE)
          continue;

        errno=EEXIST;
        D_RETURN_(FALSE); }}

      E(2,"moving \"%s\" to \"%s\", force [%s]\n",src_path,dst_path,
        (ops&FS_OP_FORCE)?"X":" ");
      
      if(lstat(src_path,&src_st)<0) {
        E(0,"Could not stat source \"%s\".\n",src_path);
        D_RETURN_(FALSE); }
  
      /* If dest exists and is a dir, adjust dest by
         appending last component of file name.
      */
      
      if(orig_dst_stat_succeeded&&S_ISDIR(dst_st.st_mode)) {
        E(2,"dest \"%s\" exists as dir.\n",dst_path);

        snprintf(s,MAXPATHLEN,"%s/%s",dst_path,
                 posix_get_filename_only(src_path));
        dst_path=s;

        E(1,"adjusted dest to \"%s\"\n",dst_path);

        dst_stat_succeeded=(stat(dst_path,&dst_st)==0); }
  
      /* Check target -- if it exists and FORCE is not used, abort.
         Also abort if FORCE is given but target cannot be removed. */
  
      if(dst_stat_succeeded) {
        if((ops&FS_OP_FORCE)==0) {
          E(0,"dest \"%s\" exists and no force used -- aborting.\n",dst_path);
          errno=EEXIST;
          D_RETURN_(FALSE); }

        /* Azundris 2002/12/25 */
        if((posix_files_identical(src_path,dst_path)==TRUE)||
           (strstr(dst_path,src_path)==dst_path)) {
          E(0,"trying to move \"%s\" file on top of itself -- aborting.\n",src_path);
          errno=EEXIST;
          D_RETURN_(FALSE); }

        E(2,"dest %s exists, and force used. Removing.\n",dst_path);

        if(rm_fs_obj(job,dst_path,FS_OP_RECURSIVE)!=NOIR_SUCC) {
          E(0,"could not remove existing dest \"%s\".\n",dst_path);
          D_RETURN_(FALSE); }}

      /* Try and see if simple rename() is enough. */

      E(2,"trying simple rename() from \"%s\" to \"%s\"...\n",src_path,dst_path);

      if(!posix_rename(src_path,dst_path)) {
        int success;

        E(2,"... failed.\n");

        /* If it's a dir, move directory. */
        if(S_ISDIR(src_st.st_mode)) {	  
          E(1,"src is dir. Moving \"%s\" to \"%s\" recursively.\n",src_path,dst_path);
          success=dir_move(job,src_path,dst_path); }
        else {
          /* Otherwise, move single regular file. */
          E(2,"src is file. Moving single file \"%s\" to \"%s\".\n",src_path,dst_path);
          success=file_move(job,src_path,&src_st,dst_path); }

        if(!success)
          D_RETURN_(FALSE); }}

  /* Yay! A simple rename was enough... */

  E(2,"... succeeded.\n");
  
  D_RETURN_(TRUE); }



int rm_fs_obj(noir_job *job,const char *path,FsOpt ops) {
  struct stat st;

  if(lstat(path,&st)<0) {  /* File doesn't exist -- error unless we're using force. */
    if(ops&FS_OP_FORCE)
      return NOIR_IGNORED;
    return NOIR_NOFILE; }

  /* If it's a directory and we're not recursive, it's an error. */
  if(S_ISDIR(st.st_mode)&&!(ops&FS_OP_RECURSIVE)) {
    errno=EINVAL;
    return NOIR_NOFILE; }

  file_job_update(job,NOIR_JOB_PROGRESS,NOIR_NOERRORCODE,NULL,path);

  /* Otherwise, try to remove and report result. */
  return file_remove(path,&st); }






/*********************************************************************************************/
