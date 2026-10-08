/** @file  model.c
 *  @brief internal model of file-system
 */

#define HASH_DIRS

#include "config.h"

#include <sys/types.h> /* uid_t etc. */
#include <stdlib.h>    /* malloc() */
#include <string.h>    /* strdup() */
#include <stdio.h>     /* stderr */
#include <sys/stat.h>  /* S_ISDIR() */
#include <errno.h>     /* errno */
#include <pthread.h>   /* mutex fu */

#include <unistd.h>    /* geteuid(), getegid() */

#include "debug.h"     /* E() */
#include "file.h"      /* file_...dir() */
#include "util.h"      /* hashes */
#include "errors.h"
#include "meta.h"
#include "mime.h"      /* MIME_FOLDER */
#include "model_private.h"
#include "cache.h"     /* file_save_cache() */
#include "thumbnailer/thumbnail.h" /* thumbnail_enqueue() */



/* how often should we jump into the read/stat-queue?  default: every 250ms */
#define PULSE_FREQ      250



static inline void     *dcd_dst_data(dcd *d);

pthread_mutex_t         ccl=PTHREAD_MUTEX_INITIALIZER;   /* client chain lock */

static void            *timer_pulse=NULL;
static file_info       *anchor=NULL;         /* root of model */
static client_funs     *clients=NULL;        /* list of clients/views */
#ifdef HASH_DIRS
static void            *dir_accels=NULL;     /* hash of directories */
#endif






/*****************************************************************************/



/**
 * vfolder path rewrites
 * @return a newly allocated rewritten path
 * @param path  the path to rewrite
 */
char *path_rewrite(const char *path) {
#ifdef WITH_DOT_DIR
  const char *p,*s;
  char       *d,*m;
  int         c=0;

  /* /.bla/ => /./.bla/ */
  for(p=path;(p=strstr(p,"/."));p++)
    if(p[2]&&(p[2]!='/'))
      c++;

  if(!c)
    return strdup(path);

  if(!(m=malloc(strlen(path)+c*2+1)))
    return NULL;

  for(d=m,s=p=path;(p=strstr(p,"/."));p++)
    if(p[2]) {           /* not final "/." */
      size_t l=p-s;
      if(p[2]!='/') {    /* not "/./" */
        strncpy(d,s,l);
        d+=l;
        s+=l;
        strncpy(d,"/.",2);
        d+=2; }
      else {             /* "/./" in source, skip */
        strncpy(d,s,l);
        d+=l;
        s+=l+2; }}

  strcpy(d,s);

  return m;
#else
  return strdup(path);
#endif
}






/*****************************************************************************/
/* sort function */



/**
 * comparator
 * @return -1,0,+1 for a is less than/equal to/greater than b (see qsort())
 * @param flags  sort-flags (folders first etc.)
 * @param a_ptr  1st element's name
 * @param a_fi   1st element's file-info (or NULL)
 * @param b_ptr  2nd element's name
 * @param b_fi   2nd element's file-info (or NULL)
 */
int model_sort_fun(sort_flags flags,const char *a_ptr,file_info *a_fi,const char *b_ptr,file_info *b_fi) {
  const char *a_name,*b_name;
  char       *a_mime,*b_mime;
  posixmd    *a_md,*b_md;
  int         a_isdir,b_isdir;
  int         ret;

  if(!a_ptr)
    return -1;
  if(!b_ptr)
    return 1;

  a_md=a_fi?a_fi->intrinsic:NULL;
  b_md=b_fi?b_fi->intrinsic:NULL;

  a_isdir=a_md?a_md->mode:0;
  b_isdir=b_md?b_md->mode:0;

/* E(0,"%s:%x  %s:%x\n",a_ptr,a_isdir,b_ptr,b_isdir); */

  if(flags&SORT_UNSORTED)
    return -1;

  if(!strcmp(a_ptr,DIR_PARENT))
    return -1;
  if(!strcmp(b_ptr,DIR_PARENT))
    return 1;

#ifdef WITH_DOT_DIR
  if(!strcmp(a_ptr,DIR_DOT))
    return -1;
  if(!strcmp(b_ptr,DIR_DOT))
    return 1;
#endif

  if(!a_fi||!(a_name=file_name(a_fi->tag)))
    a_name=a_ptr;
  if(!b_fi||!(b_name=file_name(b_fi->tag)))
    b_name=b_ptr;

  if((flags&SORT_BANDNAMES)) {
    if(!strncasecmp(a_name,"the ",4)||!strncasecmp(a_name,"die ",4))
      a_name+=4;
    if(!strncasecmp(b_name,"the ",4)||!strncasecmp(b_name,"die ",4))
      b_name+=4; }

  if(!(flags&SORT_DIRS1ST)||(S_ISDIR(a_isdir)==S_ISDIR(b_isdir))) {

    if(flags&SORT_BYMETA) {
      char *a_mval=(a_fi&&a_fi->sortkey)?a_fi->sortkey:"",
           *b_mval=(b_fi&&b_fi->sortkey)?b_fi->sortkey:"";
      if((ret=strcmp(a_mval,b_mval)))
        return ret; }

    if(a_md&&b_md) {
      if((flags&SORT_BYUSER)&&(a_md->uid!=b_md->uid))
        return a_md->uid<b_md->uid?-1:1;

      if((flags&SORT_BYSIZE)&&(a_md->size!=b_md->size))
        return a_md->size<b_md->size?-1:1;

      if((flags&SORT_BYDATE)&&(a_md->mtime!=b_md->mtime))
        return a_md->mtime<b_md->mtime?-1:1; }

    if(flags&SORT_BYMIME) {
      a_mime=(a_fi&&a_fi->mime)?a_fi->mime:"";
      b_mime=(b_fi&&b_fi->mime)?b_fi->mime:"";
      if((ret=strcmp(a_mime,b_mime))!=0) /* if MIME-types differ, return */
        return ret; }                    /* sorted by MIME, else by name */

/* E(0,"by name  \"%s\"  \"%s\": %d\n",a_name,b_name,(flags&SORT_CASEFOLD)?strcasecmp(a_name,b_name):strcmp(a_name,b_name)); */
    return (flags&SORT_CASEFOLD)?strcasecmp(a_name,b_name)
                                :strcmp(a_name,b_name); }
  else
    return S_ISDIR(a_isdir)?-1:1; }






/*****************************************************************************/
/* dir-hash for speedy lookup */



#ifdef HASH_DIRS



/**
 * destruct a dir-accelator
 * @param key  the path the accelerator is for
 */
static inline int dir_accel_dst(char *key) {
  if(!dir_accels) {
    E(0,"trying to destroy accelerator for \"%s\" in non-existing hashtable!?\n",key);
    return NOIR_FAIL; }
  return hash_table_remove(dir_accels,key)?NOIR_SUCC:NOIR_FAIL; }



/**
 * get a directory via accelerator-hash
 * @return the file-info for the directory, or NULL
 * @param key  the path to look up
 */
static inline file_info *dir_accel_get(const char *key) {
#  ifdef DEBUG_MODEL
  if(!dir_accels) {
    E(0,"called on empty hash-table!?\n");
    return NULL; }
#  endif
  return (file_info *)hash_table_fetch(dir_accels,key); }



/**
 * set a directory in accelerator-hash
 * @param dir  the path
 */
static int dir_accel_set(file_info *dir) {
  hash_table_replace(dir_accels,dir->tag,dir);
  return NOIR_SUCC; }



#else

static file_info *dir_accel_get(const char *key) {
  return NULL; }

#endif



/*****************************************************************************/
/* debug helpers */



/**
 * debug: list sub-tree of model
 * @param f  root of sub-tree to list
 * @param i  indentation (0)
 */
void model_ls(file_info *f,int i) {
  if(!f)
    f=anchor;
  while(f) {
    if(f->child) {
      fprintf(stderr,"%*s%-20s %p\n",-i,dir_accel_get(f->tag)?" ":"!",f->name,f);
      model_ls(f->child,i+2); }
    else
      fprintf(stderr,"%*s%-20s %p: %s\n",-i,dir_accel_get(f->tag)?" ":"?",f->name,f,f->tag);
    f=f->next; }}



/**
 * debug: find item by name in (sub-) tree
 * @return the number of hits
 * @param fi    the root of the sub-tree to search
 * @param name  the item to find
 */
int model_find_by_name(file_info *fi,char *name) {
  int ret=0;

  if(!fi)
    fi=anchor;

  while(fi) {
    if(!strcmp(fi->name,name)) {
      E(-1,"\"%s\" \"%s\"  $%x  ($%x)...\n",fi->name,fi->tag,fi->intrinsic?fi->intrinsic->mode:0,fi->flags);
      ret++; }

    if(fi->child)
      ret+=model_find_by_name(fi->child,name);

    fi=fi->next; }

  return ret; }





/*****************************************************************************/
/* file-entry routines */



/***********************/



posixmd *posixmd_new(void) {
  posixmd *r=malloc(sizeof(struct _posixmd));
  if(r)
    memset(r,0,sizeof(struct _posixmd));
  return r; }



posixmd *posixmd_virtual(mode_t mode) {
  posixmd *r=posixmd_new();
  if(r) {
    r->hardlinks=S_ISDIR(mode)?2:1;
    r->uid=geteuid();
    r->gid=getegid();
    r->atime=
    r->ctime=
    r->mtime=time(NULL);
    r->mode=mode; }
  return r; }



mode_t posixmd_get_mode(posixmd *i) {
  return i?i->mode:0; }

mode_t posixmd_get_lmode(posixmd *i) {
  return i?i->lmode:0; }

nlink_t posixmd_get_hardlinks(posixmd *i) {
  return i?i->hardlinks:0; }


uid_t posixmd_get_uid(posixmd *i) {
  return i?i->uid:-1; }

gid_t posixmd_get_gid(posixmd *i) {
  return i?i->gid:-1; }

off_t posixmd_get_size(posixmd *i) {
  return i?i->size:-1; }



time_t posixmd_get_atime(posixmd *i) {
  return i?i->atime:-1; }

time_t posixmd_get_mtime(posixmd *i) {
  return i?i->mtime:-1; }

time_t posixmd_get_ctime(posixmd *i) {
  return i?i->ctime:-1; }



/***********************/



mode_t file_info_get_mode(file_info *fi) {
  return (fi&&fi->intrinsic)?fi->intrinsic->mode:0; }

mode_t file_info_get_lmode(file_info *fi) {
  return (fi&&fi->intrinsic)?fi->intrinsic->lmode:0; }

nlink_t file_info_get_hardlinks(file_info *fi) {
  return (fi&&fi->intrinsic)?fi->intrinsic->hardlinks:0; }


uid_t file_info_get_uid(file_info *fi) {
  return (fi&&fi->intrinsic)?fi->intrinsic->uid:-1; }

gid_t file_info_get_gid(file_info *fi) {
  return (fi&&fi->intrinsic)?fi->intrinsic->gid:-1; }

off_t file_info_get_size(file_info *fi) {
  return (fi&&fi->intrinsic)?fi->intrinsic->size:-1; }



time_t file_info_get_atime(file_info *fi) {
  return (fi&&fi->intrinsic)?fi->intrinsic->atime:-1; }

time_t file_info_get_mtime(file_info *fi) {
  return (fi&&fi->intrinsic)?fi->intrinsic->mtime:-1; }

time_t file_info_get_ctime(file_info *fi) {
  return (fi&&fi->intrinsic)?fi->intrinsic->ctime:-1; }



/***********************/



const char *file_info_dirname(file_info *info) {
  return (info&&info->parent)?info->parent->tag:NULL; }

const char *file_info_filename(file_info *info) {
  const char *p;
  if(info&&(p=strrchr(info->tag,DIR_DELIMITER)))
    return (p==info->tag)?p:p+1;
  return NULL; }

char *file_info_basename(file_info *info,const char *suffix) {
  char *p,*q;
  if(info&&(p=strrchr(info->tag,DIR_DELIMITER))) {
    if(p!=info->tag)
      p++;
    q=strdup(p);
    if(!suffix)
      return q;
    if(q&&(p=strstr(q,suffix))) {
      *p='\0';
      if(!(p=strdup(q)))
        return q;
      free(q);
      return p;  }
    return q; }
  return NULL; }



/***********************/

file_info *file_info_get_next(file_info *info) {
  return info?info->next:NULL; }

file_info *file_info_get_parent(file_info *info) {
  return info?info->parent:NULL; }

file_info *file_info_get_1st_child(file_info *info) {
  return info?info->child:NULL; }

int file_info_get_num_children(file_info *info) {
  return info?info->children:-1; }

int file_info_get_refcount(file_info *info) {
  return info?info->refcount:-1; }


dcd *file_info_get_clientdata(file_info *info) {
  return info?info->clientdata:NULL; }


file_flags file_info_get_infobits(file_info *info) {
  return info?info->have:-1; }

item_flags file_info_get_flags(file_info *info) {
  return info?info->flags:-1; }


const char *file_info_get_tag(file_info *info) {
  if(!info)
    return NULL;
  if(info->linkee)
    return info->linkee;
  return info->tag; }

const char *file_info_get_linkee(file_info *info) {
  return (info&&info->linkee)?info->linkee:NULL; }

const char *file_info_get_ltag(file_info *info) {
  return info?info->tag:NULL; }

const char *file_info_get_name(file_info *info) {
  return info?info->name:NULL; }

const char *file_info_get_mime(file_info *info) {
  return info?info->mime:NULL; }

const char *file_info_get_sortkey(file_info *info) {
  return info?info->sortkey:NULL; }

const char *file_info_get_thumbfile(file_info *info) {
  return info?info->thumbfile:NULL; }

posixmd *file_info_get_intrinsic(file_info *info) {
  return info?info->intrinsic:NULL; }

noir_md *file_info_get_meta(file_info *info) {
  return info?info->meta:NULL; }



/***********************/



int file_info_is_folder(file_info *info) {
  return info&&info->intrinsic&&S_ISDIR(info->intrinsic->mode); }

int file_info_is_dotfolder(file_info *info) {
  return info&&(info->flags&IF_DOTDIR)&&
         info->intrinsic&&S_ISDIR(info->intrinsic->mode); }

int file_info_is_vfolder(file_info *info) {
  return info&&(info->flags&IF_VIRTUAL)&&
         info->intrinsic&&S_ISDIR(info->intrinsic->mode); }

int file_info_is_virtual(file_info *info) {
  return (info&&(info->flags&IF_VIRTUAL)); }

int file_info_is_pure_virtual(file_info *info) {
  return (info&&(info->flags&IF_VIRTUAL)&&!info->linkee); }



/***********************/



file_info *file_info_set_next(file_info *info,file_info *next) {
  if(info) info->next=next;
  return info; }

file_info *file_info_set_parent(file_info *info,file_info *parent) {
  if(info) info->parent=parent;
  return info; }

file_info *file_info_set_1st_child(file_info *info,file_info *child) {
  if(info) info->child=child;
  return info; }

int file_info_set_num_children(file_info *info,int nc) {
  if(info)
    return info->children=nc;
  return -1; }

int file_info_update_num_children(file_info *info) {
  if(info) {
    int c=0;
    file_info *child=info->child;
    while(child) {
      c++;
      child=child->next; }
    info->children=c;
    return c; }
  return -1; }


dcd *file_info_set_clientdata(file_info *info,dcd *cd) {
  if(info) {
    dcd *old=info->clientdata,*nxt;
    info->clientdata=cd;
    while(old) {
      nxt=old;
      dcd_dst_data(old);
      old=nxt; }
    return cd; }
  return NULL; }


file_flags file_info_set_infobits(file_info *info,file_flags have) {
  if(info) return info->have=have;
  return -1; }

file_flags file_info_add_infobits(file_info *info,file_flags have) {
  if(info) return info->have|=have;
  return -1; }

file_flags file_info_clr_infobits(file_info *info,file_flags have) {
  if(info) return info->have&=(~have);
  return -1; }

item_flags file_info_set_flags(file_info *info,item_flags flags) {
  if(info) return info->flags=flags;
  return -1; }


const char *file_info_set_tag(file_info *info,const char *tag) {
  if(info&&tag) {
    char *nt=strdup(tag),*y=info->tag;
    if(nt) { /* must always have tag */
      info->tag=nt;
      free(y);
      return nt; }}
  return NULL; }

const char *file_info_set_name(file_info *info,const char *name) {
  if(info&&name) {
    char *nn=strdup(name),*y=info->name;
    if(nn) { /* must always have name */
      info->name=nn;
      free(y);
      return nn; }}
  return NULL; }


const char *file_info_set_linkee(file_info *info,const char *linkee) {
  if(info) {
    char *x=NULL,*y=info->linkee;
    if(linkee&&!(x=strdup(linkee)))
      return y;
    info->linkee=x;
    if(y)
      free(y);
    return x; }
  return NULL; }

const char *file_info_set_mime(file_info *info,const char *mime) {
  if(info) {
    char *x=NULL,*y=info->mime;
    if(mime&&!(x=strdup(mime)))
      return y;
    info->mime=x;
    if(x)
      info->have|=FF_MIME;
    else
      info->have&=(~FF_MIME);
    if(y)
      free(y);
    return x; }
  return NULL; }

const char *file_info_set_sortkey(file_info *info,const char *sortkey) {
  if(info) {
    char *x=NULL,*y=info->sortkey;
    if(sortkey&&!(x=strdup(sortkey)))
      return y;
    info->sortkey=x;
    if(y)
      free(y);
    return x; }
  return NULL; }

const char *file_info_set_thumbfile(file_info *info,const char *tf) {
  if(info) {
    char *x=NULL,*y=info->thumbfile;
    if(tf&&!(x=strdup(tf)))
      return y;
    info->thumbfile=x;
    if(x)
      info->have|=FF_THUMB;
    else
      info->have&=(~FF_THUMB);
    if(y)
      free(y);
    return x; }
  return NULL; }



posixmd *file_info_set_intrinsic(file_info *info,posixmd *intrinsic) {
  if(info) {
    posixmd *y=info->intrinsic;
    info->intrinsic=intrinsic;
    if(intrinsic)
      info->have|=FF_STAT;
    else
      info->have&=(~FF_STAT);
    if(y)
      free(y);
    return intrinsic; }
  return NULL; }

noir_md *file_info_set_meta(file_info *info,noir_md *md) {
  if(info) {
    noir_md *y=info->meta;
    info->meta=md;
    if(md)
      info->have|=FF_META;
    else
      info->have&=(~FF_META);
    if(y)
      meta_dst(&y);
    return md; }
  return NULL; }



/***********************/



/**
 * destruct a file-info element
 * @param info  the file-info to destruct
 */
static file_info *file_info_dst(file_info *info) {
  if(info) {
    if(info->name)      { free(info->name);      info->name=NULL; }
    if(info->mime)      { free(info->mime);      info->mime=NULL; }
    if(info->tag)       { free(info->tag);       info->tag=NULL; }
    if(info->linkee)    { free(info->linkee);    info->linkee=NULL; }
    if(info->sortkey)   { free(info->sortkey);   info->sortkey=NULL; }
    if(info->thumbfile) { free(info->thumbfile); info->thumbfile=NULL; }
    if(info->intrinsic) { free(info->intrinsic); info->intrinsic=NULL; }
    if(info->meta)      { meta_dst(&(info->meta)); }
    free(info); }
  return NULL; }



/**
 * destruct file-info element and its accelerator
 * @param info  the file-info to destruct
 */
static inline file_info *file_info_dst_with_accel(file_info *info) {
  file_info *p;

  if(!info)
    return NULL;

#ifdef HASH_DIRS
  /* if there is an acclerator for this tag *and* it points to this file-info,
     destroy the accelerator.  this test is needed as someone may have created
     an item for this tag that is not actually in the model. */
  if(info->tag&&(dir_accel_get(info->tag)==info))
    dir_accel_dst(info->tag);
#endif

  /* if it's in the model (or part of some other tree, really), remove it
     from the tree. */
  if((p=info->parent)) {
    info->parent->children--;
    if(p->child==info)
      p->child=info->next;
    else {
      p=p->child;
      while(p&&(p->next!=info))
        p=p->next;
      if(p)
        p->next=info->next; }}

  /* release all associated memory. */
  return file_info_dst(info); }



/**
 * destruct file-info and all its children
 * @param info  the top file-info to destroy
 */
static file_info *file_info_dst_all(file_info *info) {
  file_info *dst;
  while(info) {
    if(info->child) {
      file_info_dst_all(info->child);
      info->child=NULL; }
    dst=info;
    info=info->next;
    file_info_dst_with_accel(dst); }
  return NULL; }



/**
 * refcount a file-info item
 * @param info  the file-info to increase the refcount for
 */
file_info *file_info_ref(file_info *info) {
  if(info)
    info->refcount++;
  return info; }



/**
 * unrefcount a file-info item
 * @param info  the file-info to decrease the refcount for
 */
file_info *file_info_unref(file_info *info) {
  if(info) {
    if(info->refcount<=0) {
      E(0,"file_info_unref(%p): subzero refcount, badness 1000\n",info);
      return NULL; }
    if(--info->refcount<=0)
      return file_info_dst_with_accel(info); }
  return info; }



file_info *file_info_copy(file_info *src) {
  file_info *dst=NULL;

  if(!(dst=malloc(sizeof(file_info))))
    goto fail;
  memset(dst,0,sizeof(file_info));

  if(src->name     &&!(dst->name=strdup(src->name)))           goto fail;
  if(src->tag      &&!(dst->tag =strdup(src->tag)))            goto fail;
  if(src->mime     &&!(dst->mime=strdup(src->mime)))           goto fail;
  if(src->thumbfile&&!(dst->thumbfile=strdup(src->thumbfile))) goto fail;
  if(src->sortkey  &&!(dst->sortkey=strdup(src->sortkey)))     goto fail;

  dst->have    =src->have&(~FF_META);
  dst->flags   =src->flags;
  dst->refcount=1;

  if(src->intrinsic) {
    if(!(dst->intrinsic=malloc(sizeof(posixmd))))
      goto fail;
    memcpy(dst->intrinsic,src->intrinsic,sizeof(posixmd)); }

  return dst;

 fail:
  file_info_dst(dst);
  return NULL; }



/**
 * constructor for file-entries.
 * this creates a file-info; it doesn't add it to the model.
 * @return newly allocated and initialized file-info element
 * @param name  display as...
 * @param tag   file-system knows it as... (UUID, URL, or full path)
 */
file_info *file_info_new(const char *name,const char *tag) {
  file_info   *f;
  const char  *p;

  if(!(p=strrchr(tag,DIR_DELIMITER)))
    p=tag;
  else if(!strcmp(p,"/..")) {
    E(0,"right-most element is \"..\" (\"%s\")!?\n",tag);
    return NULL; }

  /* FIXME ###
     While \n is the item-separator, we can't have it inside of an item.
     \n will be part of an item a) if the user is really weird, or
     b) in a hypothetical scenario where file-names are encoded in some
     really wonky encoding (western encodings will work, as will UTF-8,
     so that theoretically leaves some "ethnic" encoding I'm not familiar
     with.
     For now, we obviously rather fail than have file-ops go bad later. */
  if(strchr(tag,'\n'))
    E(0,"\\n detected in file-name \"%s\". DANGER WILL ROBINSON.\n",tag);

  if(!strcmp(p,"/.")) {
#ifdef WITH_DOT_DIR
    /* dot-dir, give it a decent name */
    name=DIR_DOT;
#else
    return NULL;
#endif
  }

  if(!tag||!*tag||!(f=malloc(sizeof(file_info))))
    goto fail;

  memset(f,0,sizeof(file_info));

  if(!(f->tag=strdup(tag)))
    goto free_file;

  if(!(f->name=name?strdup(name):file_tag2name(tag)))
    goto free_tag;

  f->have=FF_NAME;
  f->mime=NULL;
  f->refcount=1;
  return f;

 free_tag:
  free(f->tag);
 free_file:
  free(f);
 fail:
  return NULL; }



/**
 * find tag any where in (sub-) tree, using accelerator hash where possible
 * @return the file-info struct in question, or NULL
 * @param root   the root of the sub-tree to search in, or NULL for top
 * @param uctag  the item (path, UUID, ...) to look for
 */
file_info *file_info_fetch(file_info *root,const char *uctag) {
  file_info   *ret=NULL;
  char        *p,*t,*tag;

  if(!(tag=path_rewrite(uctag))) {/* canonize tag */
    E(0,"failed to rewrite tag \"%s\"!?\n",uctag);
    return NULL; }

#ifdef HASH_DIRS
  if(!dir_accels)
    goto done;

  if((ret=dir_accel_get(tag)))    /* is it a dir? => immediate result */
    goto done;

  if((t=strdup(tag))) {           /* might be file in dir, look up dir part */
    if((p=strrchr(t,'/'))) {
      file_info   *rf=NULL;
      if(p==t)                    /* special case: root-dir */
        p++;
      *p='\0';
      if((rf=dir_accel_get(t))&&(rf=rf->child)) {  /* dir part found */
        while(rf) {                          /* scan files in that dir */
          if(!strcmp(rf->tag,tag)) {         /* match? */
            ret=rf;                          /* ...yes */
            rf=NULL; }
          else
            rf=rf->next; }}
      else if(!rf)
        E(5,"failed to find \"%s\" (for \"%s\")!?\n",t,tag); /* FIXME ### */
      else if(!rf->child)
        E(0,"\"%s\" has no leaves (for \"%s\")!?\n",t,tag); }
    free(t); }
#else
  { size_t  l=0;
    if(!root)
      root=anchor;
    while(root) {
      if(!strncmp(root->tag,tag,l=strlen(root->tag))) { /* tag in root so far? */
        if(!strcmp(root->tag,tag)) {                    /* found? */
          ret=root;
          goto done; }                                  /* yes, return */
        /* current item is not wanted item, but WI's expected to be *in* CI */
        if((root->child)&&(tag[l]=='/')&&(ret=file_info_fetch(root->child,tag))) {
          goto done; }}                                 /* results from subdir */
      root=root->next; }}                               /* next on current lvl */
#endif

 done:
  free(tag);
  return ret; }



/**
 * create a new file-entry with given data and insert it into given sub-tree
 * @return a newly-created file-info, or NULL
 * @param root      the root of the sub-tree to insert to info in, or NULL for top
 * @param origtag   the path/UUID/... of the element
 */
file_info *file_info_insert(file_info *root,const char *origtag) {
  file_info *ret=NULL,*fi,*parent;
  char      *t,*tag;

  if(!(tag=path_rewrite(origtag)))        /* normalize tag */
    return NULL;

  if((fi=file_info_fetch(NULL,tag))) {    /* check whether we already have this item */
    free(tag);
    return fi; }

  if((fi=file_info_new(NULL,tag))) {      /* doesn't exist, make new */
    char *n;

    if((n=strdup(tag))) {
      if((t=strrchr(n,DIR_DELIMITER))) {  /* get path (file_dirname()) */
        if(t==n)                          /* on first char, keep "/" */
          t++;
        *t='\0';

        if((parent=file_info_fetch(root,n))) {  /* find parent, make child */
#ifdef WITH_DOT_DIR
          if(!strcmp(fi->name,DIR_DOT)) {
            fi->flags|=IF_DOTDIR;
            file_info_set_mime(fi,MIME_VFOLDER);
#  ifdef HASH_DIRS
            dir_accel_set(fi);
#  endif
          }
#endif
          ret=fi;
          parent->have|=FF_NAME;          /* mark parent as having 'some stuff' */
          parent->children++;
          fi->parent=parent;
          fi->next=parent->child;
          parent->child=fi; }

        else if(!strcmp(tag,"/")) {       /* bootstrap model's root */
          char *host=strdup(get_hostname()),*p;
          if(host) {
            if((p=strchr(host,'.')))
              *p='\0';
            file_info_set_name(fi,host);
            free(host); }
          dir_accel_set(fi);
          anchor=fi; }

        else {
          E(0,"(%s): parent (\"%s\") not found!  (XL: %s)\n",tag,n,dir_accel_get(n)?"yes":"no");
  /*      abort(); */
        }}

      free(n); }                          /* search done, release search key */

    else {
      E(0,"out of memory!? (%s)\n",tag);
      file_info_dst_with_accel(fi); }}

  else
    E(0,"out of memory!? (%s)\n",tag);

  free(tag);
  return ret; }



/**
 * add a virtual folder
 * @return the file-info element of the newly-created vfolder, or NULL
 * @param root  the sub-tree to insert the vfolder into
 * @param tag   the tag for the vfolder
 */
file_info *file_info_vfolder(file_info *root,const char *tag) {
  file_info *vf;

  if((vf=file_info_fetch(root,tag))) {
    E(0,"deleting pre-existing vfolder \"%s\"...\n",tag);
    model_evt(vf,MODEL_DEL,FF_NONE); }

  if(!(vf=file_info_insert(root,(char *)tag)))
    return NULL;

  vf->have |=FF_UNSEEN|FF_FIRST|FF_LAST|
             (FF_STAT_|FF_MIME_|FF_META_|FF_THUMB_);
  vf->flags|=IF_VIRTUAL;

  file_info_set_intrinsic(vf,posixmd_virtual(S_IFDIR|S_IRUSR|S_IXUSR));

  file_info_set_mime(vf,MIME_VFOLDER);

#ifdef HASH_DIRS
  dir_accel_set(vf);
#endif

  model_evt(vf,MODEL_UPD,vf->have);

  return vf; }



/**
 * add an item to a virtual folder
 * @return the file-info element of the newly-created item, or NULL
 * @param vfolder  the vfolder to insert the item into
 * @param tag      the tag of the corresponding item in the real file-system
 * @param vim      how to handle duplicates
 */
file_info *file_info_virtual(file_info *vfolder,const char *tag,vi_mode vim) {
  char       *rt=NULL;
  const char *fn;
  file_info  *fi;

  esprintf(&rt,"%s/%s",vfolder->tag,fn=file_name(tag));

  if(!rt)
    return NULL;

  if((vim!=VIM_REPLACE)&&(fi=file_info_fetch(vfolder,rt))) {
    file_info *fi2,*fia=NULL;
    int        index=0;

    E(0,"dupe handling (%d) for \"%s\"...\n",vim,fn);

    for(fi2=file_info_get_1st_child(vfolder);fi2;fi2=file_info_get_next(fi2)) {
      /* compare vs file_name(fi2->tag), not fi2->name -- the latter might
         already contain an index... */
      if(!strcmp(fn,file_name(file_info_get_tag(fi2)))) {
        E(0,"delinquent \"%s\"...\n",file_info_get_tag(fi2));

        file_info_set_tag(fi2,rt);

        index++; }}

    esprintf(&rt,"%s/%s (%d)",vfolder->tag,fn=file_name(tag),index); }

  if(!rt) {
    E(0,"oops!?\n");
    return NULL; }

  fi=file_info_insert(vfolder,rt);
  free(rt);

  if(!fi) {
    E(0,"failed to create virt-item for \"%s\" -> \"%s\"\n",tag,(vfolder&&vfolder->tag)?vfolder->tag:"");
    return NULL; }

  file_info_set_linkee(fi,tag);

  if(file_get_info(NULL,tag,&fi)!=NOIR_SUCC)
    E(0,"stat(\"%s\") failed!?\n",fi->tag);

  fi->have |=FF_UNSEEN;
  fi->flags|=IF_VIRTUAL;
  file_info_set_intrinsic(fi,posixmd_virtual(S_IFLNK|S_IRUSR));

  model_evt(fi,MODEL_UPD,fi->have);

  return fi; }



/*****************************************************************************/



/**
 * find out what data we need to gather.  most demanding view decides.
 * (if for instance we don't have any view that shows thumbnails, we
 * won't have to create any of those.)
 * @return a set describing what properties we need to determine
 * @param fi   which folder to determine the above set for
 * @param m    PG_ENQUEUE_LAST for deepest dir in newly enqueued path
 */
static inline file_flags clients_calc_want(file_info *fi,pg_mode m) {
  E(4,"%s => $%x\n",fi->tag,m);
  return (m==PG_ENQUEUE_LAST)
         ?FF_NAME_|FF_STAT_|FF_MIME_|FF_META_|FF_THUMB_
         :FF_NAME_|FF_STAT_|FF_MIME_; }



/*****************************************************************************/



/**
 * enqueue folder to be read (LIFO stack)
 * @return an error-code
 * @param qh      the queue to add the folder to
 * @param folder  the folder to enqueue for reading
 * @param m       mode
 */
static inline int dir_push(q_head *qh,file_info *folder,pg_mode m) {
  file_flags  have=FF_NONE,want;
  q_element  *qe=NULL,*last=NULL;
  int         ret=NOIR_IGNORED;

  if(folder->have&FF_FIRST) {
    have|=folder->have;
    E(3,"folder \"%s\" already in model...\n",folder->tag); }

  qe=qh->anchor;
  while(qe) {
    if(qe->dir==folder) {
      last=qe;
      have|=qe->dir->have;
      E(3,"folder \"%s\" already in queue...\n",folder->tag); }
    qe=qe->next; }

  have=(have&FF_ANY_);
  want=clients_calc_want(folder,m)&(~have);
  if(!want) {
    E(3,"folder \"%s\" already has requested info...\n",folder->tag);
    goto done; }

  if(!(qe=malloc(sizeof(q_element))))
    return NOIR_OOM;
  memset(qe,0,sizeof(q_element));
  qe->dir=folder;
  qe->wanted=qe->want=want;
  qe->backend_data=NULL;
  qe->head=qh;

  if(!last) {
    qe->next=qh->anchor;
    qh->anchor=qe; }
  else {  /* if we already have this dir enqueued, but additional data are
             requested now, enqueue the add'l request *after* the basic one */
    qe->next=last->next;
    last->next=qe; }

  ret=NOIR_SUCC;

 done:
  return ret; }



/*****************************************************************************/



/**
 * create a file-info element, add it to the model, and enQ it for reading
 * called from model_path_get(), calls dir_push() for the actual enqueuing
 * @return the newly-created file-info, or NULL
 * @param qh    the queue to add the folder to
 * @param dir   the folder the new element will be a child of
 * @param tag   the tag (path/UUID/...) of the folder to add
 * @param m     the enQ-mode
 */
static inline file_info *model_path_get_dir(q_head *qh,file_info *dir,char *tag,pg_mode m) {
  file_info *pe;

  if(!(pe=file_info_fetch(dir,tag))) {  /* check if we already have the item */
    if(!(pe=file_info_insert(NULL,tag)))
      return NULL;
    dir_accel_set(pe);
    model_evt(pe,MODEL_UPD,FF_NAME); }  /* send creation event to views */

  if(m!=PG_ENQUEUE_NOT) {
    if(m==PG_ENQUEUE_PATH) {
      pthread_mutex_lock(&qh->qchg);
      file_get_meta_with_lock(pe,&qh->qchg); /* stat() only dirs along path */
      pthread_mutex_unlock(&qh->qchg); }
    else
      dir_push(qh,pe,m); }                 /* put in LIFO(!) Q */

  return pe; }



/**
 * add all dirs along a given path to the model and enQ them for reading.
 * last element in path with be dequeued first (LIFO), as
 * that's the one the user is most likely interested in.
 * @return an error-code
 * @param qh    the queue to place the directories/read-requests in
 * @param root  the sub-tree of the model, or NULL for top
 * @param path  the path to examine
 * @param pgm   the enQ-mode (PG_ENQUEUE enQs all dirs along path for reading,
 *              PG_ENQUEUE_LAST enqueues deepest dir in path only)
 */
int model_path_get(q_head *qh,file_info *root,char *path,pg_mode pgm) {
  file_info *pp;
  char      *p,*b=NULL,*e,*x;
  pg_mode    m;

  if(pgm==PG_ENQUEUE_NOT)
    return NOIR_IGNORED;
  m=(pgm==PG_ENQUEUE_LAST)?PG_ENQUEUE_NOT:pgm;

  if(!path)
    return NOIR_NODATA;

/*
  if(!root) {
    if(!(root=anchor))
      return NOIR_NODATA; }
*/

  if(path[0]!=DIR_DELIMITER)
    return NOIR_FAIL;

  if(!(p=malloc(strlen(path)+2)))
    return NOIR_OOM;
  strcpy(p,path);

  pp=root;
  b=p+1;
  x=strchr(p,'\0');

  pp=model_path_get_dir(qh,pp,"/",m);

  /* walk path downwards starting from root(!) */
  do {
    if(!(e=strchr(b,DIR_DELIMITER))) {  /* deepest element in path */
      e=x;
      m=PG_ENQUEUE_LAST; }

    *e='\0';

#ifdef WITH_DOT_DIR
    if((b[0]=='.')&&b[1]) { /* some hidden item, create intermediate .folder */
      char       sn=b[1];
      b[1]='\0';
      pp=model_path_get_dir(qh,pp,p,PG_ENQUEUE_NOT);
      b[1]=sn; }

    if(strcmp(b,"."))       /* not else if */
#endif
    pp=model_path_get_dir(qh,pp,p,m);
    *(e++)=DIR_DELIMITER;
    b=e;
  } while(b<x);

  free(p);

  file_scanner_activate(qh);

  return NOIR_SUCC; }






/*****************************************************************************/
/* each client may set one pointer on a given file-entry (icon, column, ...) */



/**
 * get a client-specific info-struct from file-info element
 * @return the directory client-data structure, or NULL
 * @param fi      the file-info element we want client-specific data from
 * @param client  the client/view that stored the info
 */
dcd *dcd_get(file_info *fi,client_funs *client) {
  dcd *d;
  if(!fi)
    return NULL;
  for(d=fi->clientdata;d;d=d->next) {
    if(d->client==client)
      return d; }
  return NULL; }



/**
 * get a client's private pointer from file-info element
 * @return the client-specific pointer, or NULL
 * @param fi      the file-info element we want client-specific data from
 * @param client  the client/view that stored the info
 */
void *dcd_get_data(file_info *fi,client_funs *client) {
  dcd *d=dcd_get(fi,client);
  return d?d->data:NULL; }



/**
 * destroy client-specific data set on a file-info
 * this uses the client-specific destroy-callback (method from client-object)
 * @return NULL
 * @param d  the DCD
 */
static inline void *dcd_dst_data(dcd *d) {
  if(d->client->dcd_dst_data)
    d->client->dcd_dst_data(&d->data);
  else
    d->data=NULL;
  return NULL; }



/**
 * set client-specific data on a file-info
 * this uses the client-specific set-callback (method from client-object)
 * @return NULL
 * @param client  the client that wants to set data
 * @param d       the DCD-structure to modify
 * @param data    a pointer to the new data to associate with the file-info
 */
static inline void *dcd_set_data(client_funs *client,dcd *d,void *data) {
  if(d->data)                 /* if there's existing data, remove it first */
    dcd_dst_data(d);
  if(d->client->dcd_set_data) /* if there's a setter, use that */
    d->data=d->client->dcd_set_data(data);
  else                        /* otherwise just store the pointer */
    d->data=data;
  return NULL; }



/**
 * remove client-specific data from a file-info element
 * @return NULL
 * @param fi      the file-info we want to remove our data from
 * @param client  the client that originally set the data
 */
dcd *dcd_dst(file_info *fi,client_funs *client) {
  dcd *d;
  if(!fi||!(d=fi->clientdata))
    return NULL;
  if(d->client==client)
    fi->clientdata=d->next;
  else {
    dcd *p;
    do {
      p=d;
      d=d->next;
    } while(d&&(d->client!=client));
    if(!d)
      return NULL;
    p->next=d->next; }
  dcd_dst_data(d);
  free(d);
  return NULL; }



/**
 * add client-specific data to a file-info element
 * @return the DCD
 * @param fi      the file-info element to associate the data with
 * @param client  the client that wants to set the data
 * @param data    the data to associate with the element
 */
static dcd *dcd_add(file_info *fi,client_funs *client,void *data) {
  dcd *d=malloc(sizeof(dcd));
  if(!d||!fi)
    return NULL;
  d->next  =fi->clientdata;
  d->client=client;
  dcd_set_data(client,d,data);
  return fi->clientdata=d; }



/**
 * add to or modify client-specific data associated with a file-info element
 * @return the DCD
 * @param fi      the file-info element to associate the data with
 * @param client  the client that wants to set the data
 * @param data    the data to associate with the element
 */
dcd *dcd_set(file_info *fi,client_funs *client,void *data) {
  dcd *d=dcd_get(fi,client);
  if(!d)
    return dcd_add(fi,client,data);
  dcd_dst_data(d);
  dcd_set_data(client,d,data);
  return d; }





/*****************************************************************************/
/* register views */



/**
 * lock the linked list of views
 * @return an error-code
 * @param  caller  a textual description of the caller (__FUNCTION__)
 */
int client_chain_lock(const char *caller) {
  return pthread_mutex_lock(&ccl)?NOIR_FAIL:NOIR_SUCC; }



/**
 * unlock the linked list of views
 * @return an error-code
 * @param  caller  a textual description of the caller (__FUNCTION__)
 */
int client_chain_unlock(const char *caller) {
  return pthread_mutex_unlock(&ccl); }



/**
 * lock a specific view; if already locked, return
 * @return an error-code
 * @param  c       the client-object
 * @param  caller  a textual description of the caller (__FUNCTION__)
 */
int client_trylock(client_funs *c,const char *caller) {
  int ret;
/* E(0,"trying to lock \"%s\" for %s\n",c->name,caller); */
  ret=pthread_mutex_trylock(&c->viewlock)?NOIR_FAIL:NOIR_SUCC;
  return ((ret==NOIR_FAIL)&&(errno==EBUSY))?NOIR_MUTEX:ret; }



/**
 * lock a specific view; if already locked, wait till it becomes available
 * @return an error-code
 * @param  c       the client-object
 * @param  caller  a textual description of the caller (__FUNCTION__)
 */
int client_lock(client_funs *c,const char *caller) {
/* E(0,"trying to lock \"%s\" for %s\n",c->name,caller); */
  return pthread_mutex_lock(&c->viewlock)?NOIR_FAIL:NOIR_SUCC; }



/**
 * unlock a specific view
 * @return an error-code
 * @param  c       the client-object
 * @param  caller  a textual description of the caller (__FUNCTION__)
 */
int client_unlock(client_funs *c,const char *caller) {
/* E(0,"unlocking \"%s\" for %s\n",c->name,caller); */
  return pthread_mutex_unlock(&c->viewlock); }






/**
 * register a view ("client")
 * @return a new client-object
 * @param name  a name for the view
 * @param data  view-specific data
 */
client_funs *client_register(const char *name,void *data) {
  client_funs *c=malloc(sizeof(client_funs));

  if(!c||!name||!*name)
    return NULL;

  memset(c,0,sizeof(client_funs));

  if(pthread_mutex_init(&c->viewlock,NULL)) {
    E(0,"failed to initialize mutex: #%d - %s\n",errno,strerror(errno));
    free(c);
    return NULL; }

  client_chain_lock(__FUNCTION__);

  c->next=clients;
  clients=c;
  c->name=name;  /* not a copy! */
  c->data=data;

  client_chain_unlock(__FUNCTION__);

  return c; }



/**
 * register a view ("client") with callbacks
 * @return a new client-object
 * @param name           a name for the view
 * @param data           view-specific data
 * @param evt_view       callback for changes in the model
 * @param dst_data       callback to destroy view-specific 'data'
 * @param set_selection  callback to highlight the session's 'selection' in the view
 */
client_funs *client_register_full(const char *name,void *data,
                                  int (*evt_view)(file_info *,model_event type,file_flags changed,void *data),
                                  int (*dst_data)(void **),
                                  int (*set_selection)(void *)) {
  client_funs *cf=client_register(name,data);
  if(cf) {
    cf->evt_view=evt_view;
    cf->dst_data=dst_data;
    cf->set_selection=set_selection; }
  return cf; }



/**
 * unregister a single view
 * (of which there can be several in a session/window)
 * @param c  the view-object
 */
void client_unregister(client_funs *c) {
  if(!c)
    return;

  client_chain_lock(__FUNCTION__);
  client_lock(c,__FUNCTION__);

  if(clients==c)           /* remove from list */
    clients=clients->next;
  else {
    client_funs *a=clients,*b;
    do {
      b=a;
      a=a->next;
    } while(a&&(a!=c));
    if(!a) {
      E(0,"couldn't find view (%p) to unregister!?\n",c);
      client_unlock(c,__FUNCTION__);
      goto done; }
    b->next=a->next; }

  if(c->data) {            /* delete view-related data */
    if(c->dst_data)
      (c->dst_data)(&c->data);
    c->data=NULL; }

  E(2,"destroying view \"%s\" at %p\n",c->name,c);

  client_unlock(c,__FUNCTION__);
  pthread_mutex_destroy(&c->viewlock);
  free(c);

 done:
  client_chain_unlock(__FUNCTION__); }






/*****************************************************************************/



/**
 * propagate an event to all subscribed views
 * @param fi      the file-system item this event is about
 * @param etype   the kind of event we talk about (added, deleted, ...)
 * @param eflags  what changed in 'fi'
 */
static inline void model_tell_clients(file_info *fi,model_event etype,file_flags eflags) {
  client_funs *cf=clients;

  while(cf) {
    if(CLIENT_LISTENS(cf,fi))
      cf->evt_view(fi,etype,eflags,cf->data);
    cf=cf->next; }}



/**
 * something changed in the real world, update our model of it!
 * may be called from cache.c, monitors.c, or the backend (via model_pulse())
 * @return an error-code
 * @param fi     the file-system item this event is about
 * @param etype  the kind of event we talk about (added, deleted, ...)
 * @param fc     what changed in 'fi'
 */
int model_evt(file_info *fi,model_event etype,file_flags fc) {
  static char *mon[]={ "???", "UPD", "DEL", "CWD", "MSC" };

  if(!fi) {
    E(0,"no file-info given!?\n");
    E(0,"%s/$%-4x   \"%s\"  \"%s\"\n",mon[etype],fc,fi?fi->tag:"<NULL>",(fi&&fi->name)?fi->name:"<NULL>");
    return NOIR_NODATA; }




  switch(etype) {
    case MODEL_CWD:
      E(0,"(\"%s\"): was sent MODEL_CWD!?\n",fi->tag);
      model_tell_clients(fi,etype,fc);
      break;



    case MODEL_UPD:

      /* NAME */
      if(fc==FF_NAME)         /* if it's only the name, */
        return NOIR_IGNORED;  /* don't tell the views yet */


      /* STAT */
      if(fc&FF_STAT) {
	int ter;
#ifdef HASH_DIRS
        if(fi&&((fi->intrinsic&&S_ISDIR(fi->intrinsic->mode))||fi->child))
          dir_accel_set(fi);
#endif

        /* try to (re-) thumbnail item; if we get an immediate success
           (like for "folder.jpg" etc.), add a thumbnail event to make
           it visible right away. */
        if((ter=thumbnail_enqueue(fi))==NOIR_SUCC)
          fc|=FF_THUMB; }


      /* THUMBNAIL */
      if(fc&FF_THUMB) {
        const char *thumb=file_info_get_thumbfile(fi);
        if(thumb) {
          E(2,"re thumb for \"%s\" (\"%s\")\n",fi->tag,thumb);
/*        fi->have|=FF_THUMB; */ /* do NOT set this. we have a change in thumbnail-content OR THUMBNAIL-STATUS: we might not actually have a thumbnail anymore! */
        }
        else {
          E(0,"request to load thumb for \"%s\" with no thumb-file given!?\n",fi->tag);
          fi->have&=(~FF_THUMB); }}




      /* META */
      if(fc&FF_META)
        E(4,"got meta for \"%s\"\n",fi->tag);



      if(fi)
        model_tell_clients(fi,etype,fc);
      break;



    case MODEL_DEL:
      if(fi) {
        file_info *c;

        /* if it's a folder, throw out its children first. */
        while((c=fi->child))
          model_evt(c,MODEL_DEL,FF_NONE);

        /* FIXME ### this is so wrong. should register as client. */
        /* selection_remove(NULL,fi->tag); */

        model_tell_clients(fi,etype,FF_NONE);
        file_info_unref(fi); }
      break;



    case MODEL_MSC:
      model_tell_clients(fi,etype,fc);
      break;



    default:
      E(1,"MODEL-EVENT  type %d, changed $%x for %s\n",etype,fc,fi&&fi->tag?fi->tag:"???");
      model_tell_clients(fi,etype,fc);
      break; }

  return NOIR_SUCC; }



#ifdef WITH_DOT_DIR
static inline file_info *model_get_dot_folder(file_info *pa) {
  file_info *df;
  if(!pa)
    return NULL;
  df=pa->child;
  while(df) {
    if(df&&!strcmp(df->name,DIR_DOT))
      return df;
    else
      df=df->next; }
  return NULL; }
#endif



/**
 * handle events generated by other threads (backend, thumbnailer etc.)
 * @return TRUE for "don't dequeue this routine"
 * @param data  the queue to service
 */
static int model_pulse(void *data) {
  q_head     *qh=(q_head *)data;  /* list of queues */
  q_element  *fq;                 /* current file-Q */
  file_info  *fi,*pa,*df;         /* file, parent, dot-folder */
  static int  mp=0;               /* soft semaphore */

  file_job_progress_show_all();   /* display meters */

  while(!events_pending()&&(fi=thumbnailer_pending()))
    model_evt(fi,MODEL_UPD,FF_THUMB);

  if(!qh->anchor)                     /* short-circuit the default */
    return TRUE;

  if(mp) {
    E(0,"too slow\n");
    return TRUE; }

  E(6,"trying to get CHG mutex (flags: $%x)\n",qh->flags);
  pthread_mutex_lock(&qh->qchg);
  E(7,"got CHG mutex (flags: $%x)\n",qh->flags);

  mp++;

  if((fq=qh->anchor)) {
    while(fq&&(pa=fq->dir)) {

      /* send WHATEVER events for dir-contents */
      for(fi=pa->child;fi;fi=fi->next) {
        if(events_pending()) {
          E(3,"event -- stepping back so GUI can respond\n");
          goto done; }

        if(fi->have&FF_UNSEEN) {
          fi->have&=(~FF_UNSEEN);
          model_evt(fi,MODEL_UPD,fi->have&FF_ANY); }}

      /* send WHATEVER_ event for dir
         fq->want is set in backend (a flag is cleared each time a run (stat,
         meta-data, ...) for a given directory is completed.
         fq->wanted is only used in here, to compare against fq->want.
         when they are different, some run was completed; we'll act accordingly
         and clear the flag in fq->wanted, too. */
      if(fq->wanted!=fq->want) {
        E(2,"change in \"%s\" (was $%x, is $%x)...\n",pa->tag,fq->wanted,fq->want);
        pa->have|=(fq->wanted&~fq->want);
#ifdef WITH_DOT_DIR
        /* handle .files */
        if((df=model_get_dot_folder(pa))) {
          df->have=df->parent->have;
          model_evt(df,MODEL_MSC,df->have&FF_ANY_); }
#endif
        model_evt(pa,MODEL_MSC,pa->have&FF_ANY_);
        fq->wanted&=fq->want; }

      else    /* send redraw suggestion */
        model_evt(pa,MODEL_MSC,FF_NONE);

      if(!fq->want) {
        model_evt(pa,MODEL_MSC,FF_ANY_);
        dir_dequeue(qh);
        fq=qh->anchor; }
      else
        fq=fq->next; }}

  E(3,"DONE\n\n\n");

 done:
  E(6,"releasing CHG mutex\n");
  pthread_mutex_unlock(&qh->qchg);

  mp--;

  return TRUE; }






/*****************************************************************************/
/* for use in backend_pulse() */


/**
 * dequeue all directories in queue that we're done with, maybe saving a cache
 * @return the top directory in the adjusted queue (NULL for empty Q)
 * @param qh  the queue to check
 */
q_element *dir_dequeue(q_head *qh) {
  q_element *qe=qh->anchor,*qn=NULL,*qp=NULL;

  while(qe) {
    qn=qe->next;
/*  E(0,"%p | %p | %p\n",qp,qe,qn); */
    if(!qe->wanted) {     /* nothing left to do for this dir, de-Q it */
      if(qh->anchor==qe)
        qh->anchor=qn;
      else
        qp->next=qn;

      /* dump a cache where of advantage */
      if(CACHE_SLOWER_THAN&&(qe->tt>CACHE_SLOWER_THAN)) {
        E(0,"processing \"%s\" took %.1fs; anything longer than %.1fs is unacceptable.  creating a cache for this dir so things will be faster next time...\n",qe->dir->tag,(float)(qe->tt/1000.0),((float)CACHE_SLOWER_THAN)/1000.0);
        file_save_cache(qe->dir); }
      else
        E(3,"processing \"%s\" took %.1fs.\n",qe->dir->tag,(float)(qe->tt/1000.0));

      free(qe); }
    else
      qp=qe;
    qe=qn; }

  return qh->anchor; }



/**
 * requeue first element in queue so it becomes last element.
 * this is useful if we want to get all items in all dirs, then all stat()-info
 * in all dirs etc.
 * @return the new first element in the Q
 * @param qh  the Q
 */
q_element *dir_requeue(q_head *qh) {
  q_element *old=qh->anchor,
            *last=old;
  if(old&&old->next) {              /* do we actually need to re-Q? */
    while(last->next)
      last=last->next;
    qh->anchor=old->next;           /* pop dir */
    old->next=NULL;                 /* terminate list after popped element */
    last->next=old; }               /* append popped el to end of list */
  return qh->anchor; }



/*****************************************************************************/



/**
 * initialize model
 * @return a new queue associated with the new model
 */
q_head *model_init(void) {
  q_head *qh;

  if(!(qh=malloc(sizeof(q_head))))
    return NULL;
  memset(qh,0,sizeof(q_head));

  if(pthread_mutex_init(&qh->qchg,NULL)) {
    E(0,"failed to initialize mutex: #%d - %s\n",errno,strerror(errno));
    free(qh);
    return NULL; }

#ifdef HASH_DIRS
  dir_accels=hash_table_new(NULL,NULL);
#endif

  timer_pulse=timeout_add(PULSE_FREQ,model_pulse,qh);

  return qh; }



/**
 * deinitialize model
 * @return an error-code
 * @param qh  the queue for this model
 */
int model_exit(q_head *qh) {
  if(timer_pulse)
    timeout_remove(&timer_pulse);

  while(clients)
    client_unregister(clients);
#ifdef HASH_DIRS
  if(dir_accels)
    hash_table_dst(&dir_accels);
#endif

  if(qh->flags!=QUEUE_NONE) {
    if(qh->flags&QUEUE_SCANNING) {
      E(1,"stopping scanner in medias res.\n");
      qh->flags&=~QUEUE_SCANNING;
      qh->flags|=QUEUE_ZOMBIE; }

    E(2,"joining scanner.\n");
    pthread_join(qh->scanner,NULL);
    E(2,"joined scanner.\n");
    qh->flags&=~(QUEUE_SCANNING|QUEUE_ZOMBIE); }

  if(qh->anchor) {
    E(1,"there are still items on-Q, dequeueing...\n");
    while(qh->anchor) {
      E(2,"dequeuing \"%s\"...\n",qh->anchor->dir->tag);
      qh->anchor->wanted=FF_NONE;
      dir_dequeue(qh); }}

  pthread_mutex_destroy(&qh->qchg);

  free(qh);

  return NOIR_SUCC; }



/*****************************************************************************/
