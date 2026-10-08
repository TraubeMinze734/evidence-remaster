/** @file  cache.c
 *  @brief On-disk cache for directory-info (to speed up large dirs over NFS etc.)
 */

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>             /* malloc(), free() */

#include "evidence.h"
#include "cache.h"              /* structs */
#include "md5.h"                /* 3rd party free md5 hash-fu */
#include "util.h"               /* now() */
#include "setup.h"              /* path_setup() */
#include "file.h"               /* file_exists() */

#include "model_private.h"

#define CF_VERSION "0.02"


#define cache_exists(a) file_exists(a)

#define WHAT (FF_NAME|FF_STAT|FF_MIME)



/*****************************************************************************/



/**
 * hash_name - for a submitted path, created a hashed name in ~/DIR_CACHE.
 * called from file_load_cach() and file_save_cache().
 * @return a buffer containing a file-name in ~/DIR_CACHE that must be free()d.
 * @param name - the path and name of the directory we'll cache
 */
static char *hash_name(char *name) {
  size_t             l=33+strlen(path_setup)+1+sizeof(DIR_CACHE);
  char              *nn=malloc(l),*hr;
  MD5_CTX            ctx;
  unsigned char      hash[MD5_HASHBYTES];
  static const char  hex[]="0123456789abcdef";
  int                n;

  if(!nn)
    return NULL;

  /* FIXME: should go in /var/cache in FHS */
  snprintf(nn,l,"%s/" DIR_CACHE "/",path_setup);
  hr=nn+strlen(nn);

  MD5Init(&ctx);
  MD5Update(&ctx,name,strlen(name));
  MD5Final(hash,&ctx);

  for(n=0;n<MD5_HASHBYTES;n++) {
    hr[2*n]  =hex[hash[n]>>4];
    hr[2*n+1]=hex[hash[n]&0x0f]; }
  hr[2*n]='\0';

  return nn; }



/*****************************************************************************/



/**
 * cache_save_el - save a single entry in cache.
 * @return the number of characters written (length_of_data + length_of_header)
 * @param fd - the file-descriptor for the cache-file to save in
 * @param tag - tag describing the type of the entry
 * @param len - the size of the entry (excluding its header)
 * @param data - the data to write (len bytes)
 */
static ulong cache_save_el(int fd,char *tag,ulong len,void *data) {
  cache_el ce;
  memcpy(ce.magic,tag,4);
  ce.len=len;
  write(fd,&ce,sizeof(cache_el));
  write(fd,data,len);
  return sizeof(cache_el)+len; }



/**
 * cache_save_fi - save a single file-entry in a cache (convenience function)
 * @return the number of characters written (length_of_data + length_of_header)
 * @param fd - the file-descriptor for the cache-file to save in
 * @param fi - file-info to save
 */
static inline ulong cache_save_fi(int fd,file_info *fi) {
  void   *p;
  size_t  l=0,ln,lt,lm;

  if(!fi->intrinsic) return 0;
  if(!fi->tag)       return 0;
  if(!fi->name)      fi->name="";
  if(!fi->mime)      fi->mime="";

  l+=(ln=strlen(fi->name)+1);
  l+=(lt=strlen(fi->tag)+1);
  l+=(lm=strlen(fi->mime)+1);
  l+=sizeof(posixmd);

  if(!(p=malloc(l)))
    return 0;
  memset(p,0,l);

  strcpy(p,fi->name);
  strcpy(p+ln,fi->tag);
  strcpy(p+ln+lt,fi->mime);
  memcpy(p+l-sizeof(posixmd),fi->intrinsic,sizeof(posixmd));

  l=cache_save_el(fd,TAG_CACHE_FILE,l,p);

  free(p);

  return l; }



/**
 * cache_save_th - save a single thumbnail in a cache (convenience function)
 * @return  the number of characters written (length_of_data + length_of_header)
 * @param fd - the file-descriptor for the cache-file to save in
 * @param fi - file-info for thumbnail to save
 */
static inline ulong cache_save_th(int fd,file_info *fi) {
  size_t l=strlen(fi->tag)+1;
  E(3,"  saving thumbnail for \"%s\"...\n",fi->tag);
  l=cache_save_el(fd,TAG_CACHE_THUMB,l,fi->tag);
  return l; }



/**
 * cache_save - save a cache-file for a folder
 * @param dir - the directory to save a cache-file for
 * @param name - the (hashed) name and path of the cache-file
 */
static int cache_save(file_info *dir,char *name) {
  int            ret=NOIR_FAIL;
  int            fd;
  ulong          n=0,s=0,t=now(0);
  file_info     *fi;

  if(!(fi=dir->child))
    return NOIR_FAIL;
  do {
    n++;
  } while((fi=fi->next));

  if((fd=open(name,O_CREAT|O_TRUNC|O_WRONLY,S_IRUSR|S_IWUSR))>=0) {
    cache_hdr ch;
    memcpy(ch.version,CF_VERSION,4);
    ch.ctime=dir->intrinsic?dir->intrinsic->ctime:time(NULL);
    ch.nfiles=n;

    s+=cache_save_el(fd,TAG_CACHE_FOR,strlen(dir->tag)+1,dir->tag);
    s+=cache_save_el(fd,TAG_CACHE_HDR,sizeof(cache_hdr),&ch);
/*  s+=cache_save_el(fd,TAG_CACHE_WINXY,sizeof(cache_winxy),&ch); */

    fi=dir->child;
    while(fi) {
      s+=cache_save_fi(fd,fi);
#if 0
      if(fi->have&FF_THUMB)
        s+=cache_save_th(fd,fi);
#endif
      fi=fi->next; }

    close(fd);

    t=now(0)-t;
    E(2,"dumped \"%s\" to \"%s\".\n",dir->tag,name);

    ret=NOIR_SUCC; }
  else
    E(0,"could not create \"%s\"!? (%d -- %s)\n",name,errno,strerror(errno));
  return ret; }



/*****************************************************************************/



/**
 * cache_load_el - load a single cache-element from a cache-file.
 * @return the address of the new cache-element's header (which must be freed).
 * the data begin directly after the returned header (and will be free()d along
 * with the header).
 * @param fd - the file-descriptor for the cache-file to save in
 * @param len - where (if non-NULL) the number of data-bytes read will be stored, excluding the length of the header
 */
cache_el *cache_load_el(int fd,ulong *len) {
  cache_el  ce;
  void     *r;

  if(len)
    *len=0;

  if(read(fd,&ce,sizeof(cache_el))!=sizeof(cache_el))
    return NULL;

  if(!(r=malloc(sizeof(cache_el)+ce.len)))
    return NULL;

  if(read(fd,r+sizeof(cache_el),ce.len)!=((ssize_t)ce.len)) {
    free(r);
    return NULL; }

  memcpy(r,&ce,sizeof(cache_el));

  if(len)
    *len=ce.len;

  return (cache_el *)r; }



/**
 * cache_load - load a file containing the cache for a folder
 * do not call model_evt() from within; we're called by the scanner rather
 * than the GUI thread!
 * @param q - the directory the contents of which are to be loaded
 * @param fn - the path and name of the file to load
 */
int cache_load(q_element *q,char *fn) {
  int        fd,ret=NOIR_FAIL;
  cache_el  *chunk;
  void      *data;
  time_t     tn,tl=0;
  long       ts=now(0);
  file_info *dir=q->dir;

  if((fd=open(fn,O_RDONLY))>=0) {
    file_info *fi;
    char       magic[5];
    ulong      len;

    ret=NOIR_SUCC;
    magic[4]='\0';
    E(2,"reading \"%s\"...\n",fn);

    dir->have|=FF_SEMA;

    while((ret==NOIR_SUCC)&&(chunk=cache_load_el(fd,&len))) {
      memcpy(magic,chunk->magic,4);
      data=((void *)chunk)+sizeof(cache_el);
      E(5,"chunk %s, length %ld\n",magic,chunk->len);

      if(!strcmp(magic,TAG_CACHE_FOR)) {
        dir->have|=FF_FIRST;
/*      model_evt(dir,MODEL_MSC,FF_FIRST); */
        E(3,"evidence cache for %s\n",(char *)data); }

      else if(!strcmp(magic,TAG_CACHE_HDR)) {
        cache_hdr *ch=(cache_hdr *)data;
        char       v[5];
        time_t     dt;

        v[4]='\0';
        memcpy(v,ch->version,4);

        /* is cache still valid? */
        if(strncmp(v,CF_VERSION,4)) {
          E(0,"cache for \"%s\" is v%4s, I only speak v%s, rebuilding...\n",dir->tag,v,CF_VERSION);
          file_rm(fn);
          ret=NOIR_IGNORED;
          free(chunk);
          goto done; }

        if(!dir->intrinsic)
          file_get_info(NULL,dir->tag,&dir);
        if(dir->intrinsic)
          dt=dir->intrinsic->ctime;
        else {
          dt=ch->ctime+1;
          E(0,"cannot get fs-info for \"%s\"!?\n",dir->tag); }

        if(dt>ch->ctime) {
          E(1,"cache for \"%s\" is not up to date, rebuilding...\n",dir->tag);
          file_rm(fn);
          ret=NOIR_IGNORED;
          free(chunk);
          goto done; }

        E(3,"cache v%s contains %ld entries from %s",v,ch->nfiles,ctime(&ch->ctime)); }

      else if(!strcmp(magic,TAG_CACHE_FILE)) {
        char    *n=(char *)data,*t,*m;
        posixmd  md;

        t=n+strlen(n)+1;
        m=t+strlen(t)+1;
        data=(void *)(m+strlen(m)+1);
        memcpy(&md,data,sizeof(posixmd));
        E(4,"  cache entry for %s (%s, %s, $%x)\n",n,t,m,md.mode);

        if((fi=file_info_fetch(NULL,t)))
          E(2,"info for \"%s\" already exists.\n",t);
        else if(!(fi=file_info_insert(NULL,t)))
          E(0,"couldn't create info for \"%s\"!?\n",t);

        if(fi&&!fi->intrinsic&&!(fi->intrinsic=posixmd_new()))
          fi=file_info_unref(fi);

        if(fi) {
          memcpy(fi->intrinsic,&md,sizeof(posixmd));
          file_info_set_mime(fi,m);
          fi->have|=WHAT|FF_UNSEEN;
/*        model_evt(fi,MODEL_UPD,WHAT); */
        }
        else
          E(0,"out of memory!?\n"); }

      else if(!strcmp(magic,TAG_CACHE_THUMB)) {
        char    *n=(char *)data;
        len-=(strlen(n)+1);
        E(0,"  thumbnail for \"%s\" (%ld bytes) ignored...\n",n,len); }

      else {
        ret=NOIR_OOM;
        E(0,"unrecognized chunk type \"%s\"...\n",magic); }

      free(chunk);

      tn=time(NULL);
      if(events_pending()||(tn>tl)) {
/*      model_evt(dir,MODEL_MSC,FF_NONE); */
        pthread_mutex_unlock(&q->head->qchg);  /* let front-end update */
        pthread_mutex_lock(&q->head->qchg);    /* enough already! : ) */
        tl=tn; }}

    E(1,"done loading cache for \"%s\".\n",dir->tag);

  done:
    dir->have&=~FF_SEMA;
    close(fd);

    if(ret==NOIR_SUCC) {
      ts=now(0)-ts;
      dir->have|=FF_LAST|FF_NAME_|FF_STAT_|FF_MIME_;
/*    model_evt(dir,MODEL_MSC,FF_LAST|FF_NAME_|FF_STAT_|FF_MIME_); */
      E(2,"loaded \"%s\" from \"%s\" in %.1f.  ($%x)\n",dir->tag,fn,((float)ts)/1000.0,dir->have); }}

  return ret; }




/*****************************************************************************/



/**
 * file_save_cache - save a cache-file for a folder.
 * @param dir - the folder to save a cache for
 */
int file_save_cache(file_info *dir) {
  char *fn;
  if((fn=hash_name(dir->tag))) {
    cache_save(dir,fn);
    free(fn);
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * file_load_cache - load a cache-file for a folder.
 * @param q - a queue-element denoting the directory to load (in its "dir" element)
 */
int file_load_cache(q_element *q) {
  char *fn;
  int   ret=NOIR_FAIL;

  if((fn=hash_name(q->dir->tag))) {
    if(cache_exists(fn)) {
      if((ret=cache_load(q,fn))==NOIR_SUCC) {
        q->want&=~WHAT; }}
    else {
      E(2,"No cache for \"%s\" (as \"%s\")...\n",q->dir->tag,fn);
      ret=NOIR_NODATA; }
    free(fn); }
  return ret; }



/*****************************************************************************/
