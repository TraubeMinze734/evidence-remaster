/** @file  meta.c
 *  @brief handling of meta-data lists.
 */

#include "meta.h"           /* structs */
#include "mime.h"
#include "util.h"           /* hashes, replace_variables() */
#include "errors.h"         /* error-codes */
#include "setup.h"          /* get_setup_file() */
#include "debug.h"

#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <ctype.h>



#ifndef TRUE
#  define FALSE (0)
#  define TRUE  (!FALSE)
#endif

#ifndef GC_UPDATE_TIMESTAMP
#  define GC_UPDATE_TIMESTAMP(a)   (time(NULL)+(a))
#endif
#ifndef GC_INTERVAL_META
#  define GC_INTERVAL_META     600
#endif

#define SET_FUN "meta_set_for_file"
#define GET_FUN "meta_get_for_file"
#define INT_FUN "meta_plugin_init"
#define EXT_FUN "meta_plugin_exit"






/*****************************************************************************/



struct _noir_provider {
  char                   *file;  /**< name and path of shared object */
  char                   *mime;  /**< same as hash-key, do not free()! */
  void                   *lib;   /**< library-handle */
  void                   *data;  /**< plug-in specific: returned from init-function (if any), passed to exit-function (if any) */
  time_t                  ts;    /**< time-stamp (last use) */
  struct _noir_provider  *next;  /**< next provider in list */
};

struct _noir_meta {
  char              *key;        /**< name of entry */
  char              *grp;        /**< group entry is part of */
  char              *dsc;        /**< description of entry (if any) */
  void              *val,        /**< value, as C-string */
                    *prvs;       /**< previous value, as C-string, if any */
  meta_flags         flg;        /**< flags (read-only etc.) */
  meta_type          type;       /**< type (UTF8, integer, ACL, ...) */
  noir_provider     *prov;       /**< provider that created this entry */
  void              *user1;      /**< user-data (application-specific) */
  long               user2;      /**< user-data (application-specific) */
  void              *user3;      /**< user-data (application-specific) */
  struct _noir_meta *next;       /**< next meta-data entry in list */
};

struct _noir_md {
  noir_meta *anchor;
};

static void          *ht_providers=NULL;
static noir_provider *pa=NULL;






/*****************************************************************************/



/**
 * iterate over each element in a meta-data set
 * @return an error-code
 * @param a     the set
 * @param fun   the function to apply to each entry
 * @param data  user-supplied data for the function
 */
int meta_walk(noir_md *a,meta_walker fun,void *data) {
  noir_meta *m,*n;
  if(a&&(m=a->anchor)) {
    do {
      n=m->next;
      fun(m,data);
    } while((m=n));
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * get 1st meta-data element in chain
 * @return the element, or NULL
 * @param a  the chain
 */
noir_meta *meta_get_1st(noir_md *a) {
  return a?a->anchor:NULL; }




/**
 * get 1st meta-data element in chain
 * @return the element, or NULL
 * @param m  the current element
 */
noir_meta *meta_get_next(noir_meta *m) {
  return m?m->next:NULL; }



const char    *meta_get_key(noir_meta *m)      { return m?m->key:NULL; }
const char    *meta_get_group(noir_meta *m)    { return m?m->grp:NULL; }
const char    *meta_get_desc(noir_meta *m)     { return m?m->dsc:NULL; }
void          *meta_get_val(noir_meta *m)      { return m?m->val:NULL; }
void          *meta_get_prvs(noir_meta *m)     { return m?m->prvs:NULL; }
noir_provider *meta_get_provider(noir_meta *m) { return m?m->prov:NULL; }
void          *meta_get_user1(noir_meta *m)    { return m?m->user1:NULL; }
long           meta_get_user2(noir_meta *m)    { return m?m->user2:0; }
void          *meta_get_user3(noir_meta *m)    { return m?m->user3:NULL; }
meta_flags     meta_get_flags(noir_meta *m)    { return m?m->flg:META_NA; }
meta_type      meta_get_type(noir_meta *m)     { return m?m->type:META_OLD; }

meta_flags     meta_set_flags(noir_meta *m,meta_flags f) { return m->flg=f; }
void          *meta_set_user1(noir_meta *m,void *d)      { return m->user1=d; }
long           meta_set_user2(noir_meta *m,long  d)      { return m->user2=d; }
void          *meta_set_user3(noir_meta *m,void *d)      { return m->user3=d; }


void *meta_provider_get_data(noir_provider *p) {
  return p->data; }

void *meta_provider_set_data(noir_provider *p,void *d) {
  return p->data=d; }






/**
 * find an entry in a given group, or in all groups.
 * @param a  the list of meta-data
 * @param k  the name of the entry to find
 * @param g  the group to search in, or NULL for "all groups, r/w preferred"
 */
noir_meta *meta_in_group(noir_md *a,const char *k,const char *g) {
  noir_meta *m,*r=NULL;

  if(!(m=meta_get_1st(a))||!k||!*k)
    return NULL;
  if(g&&!*g)
    g=NULL;

  while(m) {
    if(m->key&&!strcasecmp(k,m->key)&&
       (!g||(m->grp&&!strcasecmp(g,m->grp)))) {
      r=m;
      if((m->flg==META_RW)||(m->flg==META_DO))
        m=NULL;
      else
        m=meta_get_next(m); }
    else
      m=meta_get_next(m); }
  return r; }






/*****************************************************************************/



/**
 * dst a single meta-data element (this does not remove it from any lists)
 * @return an error-code
 * @param m - the element to dst
 */
int meta_dst_one(noir_meta *m) {
  if(!m)
    return NOIR_FAIL;
  if(m->val)
    free(m->val);
  if(m->prvs)
    free(m->prvs);
  if(m->dsc)
    free(m->dsc);
  if(m->grp)
    free(m->grp);
  free(m->key);
  memset(m,0,sizeof(noir_meta));
  free(m);
  return NOIR_SUCC; }



/**
 * delete all meta-data entries in a list
 * @param a - the list of meta-data
 */
int meta_del_all(noir_md *a) {
  noir_meta *m,*p;

  if(!a)
    return NOIR_FAIL;
  if(!(p=meta_get_1st(a)))
    return NOIR_IGNORED;

  while(p) {
    m=p;
    p=meta_get_next(p);
    meta_dst_one(m); }

  a->anchor=NULL;

  return NOIR_SUCC; }



/**
 * dst header and all meta-data entries in a list
 * @param a - the list of meta-data
 */
int meta_dst(noir_md **a) {
  if(!a||!*a)
    return NOIR_FAIL;
  meta_del_all(*a);
  free(*a);
  *a=NULL;
  return NOIR_SUCC; }



/**
 * remove a single entry from list and destruct it
 * @return an error-code
 * @param a     the list
 * @param del   the entry to remove
 */
static int meta_del_one(noir_md *a,noir_meta *del) {
  noir_meta *prvs=NULL,*curr=NULL;

  if(!(curr=meta_get_1st(a)))
    return NOIR_FAIL;

  while(curr) {
    if(curr==del) {
      if(prvs)
        prvs->next=curr->next;
      else
        a->anchor=curr->next;
      return meta_dst_one(curr); }
    prvs=curr;
    curr=meta_get_next(curr); }

  return NOIR_NOTFOUND; }



/**
 * delete a group of meta-data
 * @param a - the list of meta-data
 * @param grp - the group
 */
int meta_del_by_group(noir_md *a,const char *grp) {
  noir_meta *p=NULL,*m;
  int        ret=NOIR_NOTFOUND;

  if((m=meta_get_1st(a)))
    return NOIR_FAIL;

  while(m&&
        ((grp&&m->grp&&!strcmp(grp,m->grp))||
         (!grp&&!m->grp))){
    p=m;
    m=meta_get_next(m);
    meta_del_one(a,p);
    ret=NOIR_SUCC; }

  return ret; }



/**
 * delete all meta-data from a given provider.
 * @param a - the list of meta-data
 * @param prov - the provider that created the meta-data we want to delete now
 */
int meta_del_by_provider(noir_md *a,noir_provider *prov) {
  noir_meta *p=NULL,*m;
  int        ret=NOIR_NOTFOUND;

  if((m=meta_get_1st(a)))
    return NOIR_FAIL;

  while(m&&(prov==m->prov)) {
    p=m;
    m=meta_get_next(m);
    meta_del_one(a,p);
    ret=NOIR_SUCC; }

  return ret; }



/**
 * delete a single meta-data entry.
 * @param a - the list of meta-data
 * @param k - the name of the entry to delete
 * @param g - the group the entry is in
 */
int meta_del(noir_md *a,char *k,char *g) {
  noir_meta *m;

  if(!(m=meta_get_1st(a)))
    return NOIR_FAIL;

  while(m&&(strcasecmp(k,m->key)||(g&&strcasecmp(g,m->grp))))
    m=meta_get_next(m);

  if(m)
    meta_del_one(a,m);

  return NOIR_NOTFOUND; }






/*****************************************************************************/



/**
 * create a meta-data entry.
 * @return an error-code
 * @param mp - where to return the new entry, or NULL for failure
 * @param flags - the flags for the entry (read-only etc.)
 * @param type - the type of the entry (integer, UTF8, ...)
 * @param k - the name of the entry
 * @param d - the description for the entry, or NULL
 * @param g - the group the entry should appear in
 * @param v - the value for the entry, or NULL
 * @param p - the provider that created the entry
 */
static int meta_new_one(noir_meta **mp,meta_flags flags,meta_type type,const char *k,const char *d,const char *g,const char *v,noir_provider *p) {
  noir_meta *m;

  if(!mp)
    return NOIR_FAIL;
  *mp=NULL;

  if(!k||!*k)
    return NOIR_FAIL;
  if(!(m=(noir_meta *)malloc(sizeof(noir_meta))))
    return NOIR_OOM;
  memset(m,0,sizeof(noir_meta));

  if(!(m->key=strdup(k)))
    goto release_m;
  if(v) {
    if(!(m->val=strdup(v)))
      goto release_mk; }
  else
    m->val=NULL;

  if(d) {
    if(!(m->dsc=strdup(d)))
      goto release_mv; }
  else
    m->dsc=NULL;

  if(g) {
    if(!(m->grp=strdup(g)))
      goto release_md; }
  else
    m->grp=NULL;

  m->prov=p;
  m->flg=flags;
  m->next=NULL;
  m->prvs=NULL;
  m->type=type;

  *mp=m;

  return NOIR_SUCC;

 release_md:
  free(m->dsc);
 release_mv:
  free(m->val);
 release_mk:
  free(m->key);
 release_m:
  free(m);

  return NOIR_OOM; }



/**
 * copy a meta-data entry
 * @return the new entry
 * @param src  the entry to copy
 */
static noir_meta *meta_clone_one(noir_meta *src) {
  noir_meta *dst=NULL;

  if(src) {
    if(meta_new_one(&dst,src->flg,src->type,src->key,src->dsc,src->grp,src->val,src->prov)==NOIR_SUCC) {
      dst->user2=src->user2; }}
  return dst; }



/**
 * allocate a new meta-data set
 * @return a new set, or NULL for OOM
 */
noir_md *meta_new(void) {
  noir_md *n=malloc(sizeof(noir_md));
  if(n)
    memset(n,0,sizeof(noir_md));
  return n; }




/**
 * clone a set of meta-data
 * @return a (possibly empty) set of meta-data, or NULL for OOM
 * @param src  the source chain, or NULL
 */
noir_md *meta_clone(noir_md *src) {
  noir_md *dst=meta_new();

  if(dst&&src) {
    noir_meta *si=meta_get_1st(src),*di,*lp=NULL;

    while(si) {
      if((di=meta_clone_one(si))) {
        if(!lp)
          dst->anchor=di;
        else
          lp->next=di;
        lp=di; }
      si=meta_get_next(si); }
    /* rebuild hashes */ }

  return dst; }






/*****************************************************************************/



/**
 * add a meta-data entry to the list for a file.
 * @param a - the list
 * @param flags - the flags for the entry (read-only etc.)
 * @param type - the type of the entry (integer, UTF8, ...)
 * @param k - the name of the entry
 * @param d - the description for the entry, or NULL
 * @param g - the group the entry should appear in
 * @param v - the value for the entry, or NULL
 * @param p - the provider that created the entry
 */
int meta_add(noir_md *a,meta_flags flags,meta_type type,const char *k,const char *d,const char *g,const char *v,noir_provider *p) {
  noir_meta *m;
  int        ret;

  if(!a)
    return NOIR_FAIL;

  if((ret=meta_new_one(&m,flags,type,k,d,g,v,p))!=NOIR_SUCC)
    return ret;

  if(!meta_get_1st(a))
    a->anchor=m;
  else {
    noir_meta *x=meta_get_1st(a),
              *r=meta_get_1st(a),
              *lg=NULL;

    while(x) {
      if(g&&r->grp&&(!strcasecmp(g,r->grp)))
        lg=x;
      r=x;
      x=meta_get_next(x); }
    /* failsafe: if EAs of one group do not come in one block, reunite them */
    if(lg) {
      m->next=lg->next;
      lg->next=m; }
    else {
      r->next=m; }}

  return NOIR_SUCC; }



/**
 * set meta-data entry to a string
 * @return an error-code
 * @param m  the entry to modify
 * @param s  the string to set
 * @param t  META_UTF8: string is UTF8-encoded; META_STR: string is locale-encoded; META_OLD: keep old encoding (caller must ensure 's' is correct type!)
 */
int meta_set_string(noir_meta *m,const char *s,meta_type t) {
  char *f=NULL;  /* enable meta_set_string(m,meta_get_val(m),META_OLD) */

  if(!m)
    return NOIR_FAIL;

  if(t==META_OLD)
    t=m->type;

  if(!((t==META_STR)||(t==META_UTF8)))
    return NOIR_TYPEMISMATCH;

  /* was already a string-type?  handle 'prvs' */
  if((m->type==META_STR)||(m->type==META_UTF8)) {
    if(m->prvs) {
      f=m->prvs;
      m->prvs=NULL; }

    if((t!=m->type)&&m->val) {
      if(t==META_UTF8)
        m->prvs=e2utf8(m->val);
      else if(t==META_STR)
        m->prvs=e2locale(m->val);
      free(m->val); }
    else  /* same string type, no conversions */
      m->prvs=m->val; }

  /* handle new val */
  if(s)
    m->val=strdup(s);
  else
    m->val=NULL;

  if(f)
    free(f);

  return NOIR_SUCC; }



/**
 * set a meta-data entry in the list for a file.
 * @param a - the list
 * @param flags - the flags for the entry (read-only etc.)
 * @param type - the type of the entry (integer, UTF8, ...)
 * @param k - the name of the entry
 * @param d - the description for the entry, or NULL
 * @param g - the group the entry should appear in
 * @param v - the value for the entry, or NULL
 * @param p - the provider that created the entry
 */
int meta_set(noir_md *a,meta_flags flags,meta_type type,const char *k,const char *d,const char *g,const char *v,noir_provider *p) {
  noir_meta *m;
  char      *nv=NULL,*nd=NULL;

  if(!(m=meta_in_group(a,k,g)))
    return meta_add(a,flags,type,k,d,g,v,p);

  if(v) {
    if(!(nv=strdup(v)))
      return NOIR_OOM; }

  if(d) {
    if(!(nd=strdup(d)))
      goto free_nv; }

  if(nv) {
    if(m->prvs)
      free(m->prvs);
    m->prvs=m->val;
    m->val=nv; }

  if(nd) {
    if(m->dsc)
      free(m->dsc);
    m->dsc=nd; }

  if(p)
    m->prov=p;

  if(m->flg!=META_NA)
    m->flg=flags;
  if(m->type!=META_OLD)
    m->type=type;

  return NOIR_SUCC;

free_nv:
  if(nv)
    free(nv);
  return NOIR_OOM; }



/**
 * change a meta-data entry in the list for a file.
 * @param m - entry to change
 * @param flags - the flags for the entry (read-only etc.)
 * @param type - the type of the entry (integer, UTF8, ...)
 * @param k - the name of the entry
 * @param v - the value for the entry, or NULL
 */
int meta_chg(noir_meta *m,meta_flags flags,meta_type type,const char *k,const char *v) {
  char *nv=NULL;

  if(v) {
    if(!(nv=strdup(v)))
      return NOIR_OOM; }
  if(m->prvs)
    free(m->prvs);
  m->prvs=m->val;
  m->val=nv;

  if(m->flg!=META_NA)
    m->flg=flags;
  if(m->type!=META_OLD)
    m->type=type;

  return NOIR_SUCC; }



/**
 * set a default for a meta-data entry:
 * meta_add() only if value does not already exist.
 *
 * sometimes, a meta-data entry must be created even if it is not warranted by
 * the file we wish to describe.  for instance, if no "genre" information is
 * set on a MP3 song, we will still want to create a "genre"-entry in the
 * meta-data so the info-dialog will create a "genre" edit-field.
 * to do this, the provider's meta_get_for_file() function should first create
 * entries for all meta-data it can get from the file, and then call meta_dflt()
 * for all items that an entry must exist for "no matter what."
 *
 * @return an error-code
 * @param a - the list
 * @param flags - the flags for the entry (read-only etc.)
 * @param type - the type of the entry (integer, UTF8, ...)
 * @param k - the name of the entry
 * @param d - the description for the entry, or NULL
 * @param g - the group the entry should appear in
 * @param v - the value for the entry, or NULL
 * @param p - the provider that created the entry
 */
int meta_dflt(noir_md *a,meta_flags flags,meta_type type,const char *k,const char *d,const char *g,const char *v,noir_provider *p) {
  if(!(meta_in_group(a,k,g)))
    return meta_add(a,flags,type,k,d,g,v,p);
  return NOIR_IGNORED; }






/*****************************************************************************/



/**
 * release a meta-data provider
 * @return an error-code
 * @param p  the provider to be released
 */
static int meta_lib_close(noir_provider *p) {
  if(p->lib) {
    int (*meta_plugin_exit)(void *);
    if((meta_plugin_exit=dlsym(p->lib,EXT_FUN)))
      meta_plugin_exit(p->data);
    dlclose(p->lib);
    p->lib=NULL;
    return NOIR_SUCC; }
  return NOIR_IGNORED; }



/**
 * garbage collect meta-data providers:
 * if a provider isn't used for a long time, it is unloaded.
 */
int meta_lib_gc(void) {
  time_t         t=time(NULL);
  noir_provider *p=pa;
  int            c=0;

  while(p) {
    if(p->ts<=t)
      meta_lib_close(p);
    p=p->next; }
  return c; }



/**
 * get the shared object for a provider record. open as needed.
 * @param p - the provider we're interested in.
 */
void *meta_lib_get(noir_provider *p) {
  if(!p)
    return NULL;
  if(p->file) {
    if(!p->lib) {
      void *(*meta_plugin_init)(void);
      if(!(p->lib=dlopen(p->file,RTLD_NOW|RTLD_GLOBAL))) {
        char *err=dlerror();
        E(0,"failed to load \"%s\": %s\n",p->file,err?err:"unknown error"); }
      else if((meta_plugin_init=dlsym(p->lib,INT_FUN)))
        p->data=meta_plugin_init(); }
    p->ts=GC_UPDATE_TIMESTAMP(GC_INTERVAL_META); }
  return p->lib; }






/*****************************************************************************/



/**
 * convenience: concatenate meta-data.
 * @return a single C-string in locale-dependent encoding, with the individual
 * items separated by '\\n'.  this string must later be free()d.
 * @param md  the bag of meta-data
 */
char *meta_to_string(noir_md *md) {
  int        cnt=0;
  noir_meta *p,*v=NULL;
  estring   *s;

  if(!(p=meta_get_1st(md))||!(s=estring_new(2048)))
    return NULL;

  while(p) {
    if(v&&
       ((v->grp!=p->grp&&(!v->grp||!p->grp))||
        (v->grp&&p->grp&&strcasecmp(v->grp,p->grp)))) {
      if(!(p->type==META_ACL))
        estring_appendf(s,"\n:"); }

    if(p->type==META_UTF8) {
      char *a=e2locale(p->val);
      if(a) {
        estring_appendf(s,"%s%s: %s",(cnt++?"\n":""),p->key,(char *)a);
        free(a); }}
    else if(!(p->type==META_ACL))
      estring_appendf(s,"%s%s: %s",(cnt++?"\n":""),p->key,(char *)p->val);
    v=p;
    p=p->next; }

  return estring_free(s,FALSE); }






/*****************************************************************************/



#ifndef STANDALONE

/**
 * get (first) provider for given MIME-type.
 * @param mime - the MIME-type we need a provider for
 * @param fallback - if FALSE, will only return exact matches. otherwise, will fall
 *  back on first provider for "foo/" or even "" if "foo/bar" cannot be found
 */
static noir_provider *meta_provider_get(char *mime,int fallback) {
  noir_provider *p;

  if(!ht_providers||!mime)
    return NULL;

  if(!(p=(noir_provider *)hash_table_fetch(ht_providers,mime))&&fallback) {
    char *m=strdup(mime),*q;
    if(m) {
      if((q=strchr(m,MIME_DELIMITER))) {
        *q='\0';
        p=(noir_provider *)hash_table_fetch(ht_providers,m); }
      free(m); }

    if(!p)
      p=(noir_provider *)hash_table_fetch(ht_providers,""); }

  return p; }



/**
 * release a meta-data provider.
 *
 * @param mp - the provider to release
 */
static void meta_provider_free(void *mp) {
  noir_provider *p=(noir_provider *)mp;
  if(p) {
    meta_lib_close(p);
    if(p->mime) {
      free(p->mime);
      p->mime=NULL; }
    if(p->file) {
      free(p->file);
      p->file=NULL; }
    free(p); }}



/**
 * add a meta-data provider to the internal list.
 * @param key - the MIME-type this provider handles
 * @param prov - the path to the shared object
 */
static int meta_provider_add(char *key,char *prov) {
  char          *k,*v;
  noir_provider *p,*e;

  if(!ht_providers)
    ht_providers=hash_table_new(NULL,NULL);

  if((k=strchr(key,'*')))
    *k='\0';

  if(!(k=strdup(key)))
    return NOIR_OOM;

  if(!(v=get_setup_file(prov))) {
    E(1,"provider \"%s\" not found...\n",prov);
    free(k);
    return NOIR_NOTFOUND; }

  if(!(p=malloc(sizeof(noir_provider)))) {
    free(k);
    free(v);
    return NOIR_OOM; }

  memset(p,0,sizeof(noir_provider));

  p->file=v;
  p->lib=NULL;
  p->mime=k;

  if(!(e=hash_table_fetch(ht_providers,k))) {
    hash_table_insert(ht_providers,k,p);
    p->next=pa;
    pa=p;
    return NOIR_SUCC; }
  else {
    E(4,"appending %s to %s for %s\n",p->file,e->file,e->mime);
    p->next=e->next;
    e->next=p;
    return NOIR_PARTIAL; }}






/*****************************************************************************/



/**
 * get meta-data for a given file.
 * @param p - provider to use, or NULL to auto-determine
 * @param file - file to get meta-data for
 * @param r - where to append the results
 */
int meta_handle_file(noir_provider *p,char *file,noir_md *r) {
  const char *err;
  int         ret=NOIR_META_DLFAIL;
  void       *lib;

  if(!p) {
    noir_meta *x;
    char      *q,*m;
    if((x=meta_in_group(r,"MIME",NULL))&&x->val&&(m=strdup(x->val))) {
      if((p=meta_provider_get(m,FALSE))) {
        do {
          E(2,"primary \"%s\": %s\n",m,p->file);
          ret=meta_handle_file(p,file,r);
          p=p->next;
        } while(p&&!strcmp(p->mime,m)); }

      if((q=strchr(m,MIME_DELIMITER))) {
        q[1]='\0';
        if((p=meta_provider_get(m,FALSE))) {
          do {
            E(2,"secondary \"%s*\": %s\n",m,p->file);
            meta_handle_file(p,file,r);
            p=p->next;
          } while(p&&!strncmp(p->mime,m,strlen(m))); }}

      if((p=meta_provider_get("",FALSE))) {
        do {
          E(2,"tertiary \"*\": %s\n",p->file);
          meta_handle_file(p,file,r);
          p=p->next;
        } while(p&&!strcmp(p->mime,"")); }

      free(m);
      return ret; }}

  if(!p||!r)
    ret=NOIR_FAIL;
  else if((lib=meta_lib_get(p))) {
    int (*meta_get_for_file)(noir_md *,char *,void *);
    meta_get_for_file=dlsym(lib,GET_FUN);
    ret=meta_get_for_file(r,file,p); }
  else {
    if((err=dlerror()))
      E(0,"failed to load \"%s\": %s\n",p->file,err);
    hash_table_remove(ht_providers,p->mime); }
  return ret; }



/**
 * write meta-data back to the file they describe.
 * @param a - the list of meta-data
 * @param file - the file
 */
int meta_update(noir_md *a,char *file) {
  noir_meta     *m=meta_get_1st(a),*f=NULL;
  char          *grp=m->grp,*filename;
  int            upd,upc=0,intl;
  noir_provider *prov;

  if(!((f=meta_in_group(a,"Tag",META_DFLT_GRP))||
       (f=meta_in_group(a,"Tag",NULL)))&&(!file||!*file)) {
    E(0,"no EA 'Name', and no filename given...\n");
    return NOIR_NOFILE; }

  while(m) {
    filename=f?f->val:file;  /* get from entry every time as it may change */
    upd=0;
    prov=NULL;
/*  E(3,"Checking group %s for updates...\n",grp?grp:"???"); */
    while(m&&((m->grp==grp)||(grp&&m->grp&&!strcasecmp(m->grp,grp)))) {
/*    E(4,"-- checking %s for updates (%s,0x%x)...\n",m->key?m->key:"???",m->val?(char *)m->val:"<null>",m->flg); */
      if(m->flg==META_DO)
        upd=1;
      if(m->prov)   /* should be set on all MD in group, but you never know */
        prov=m->prov;
      else
        m->prov=prov;
      m=meta_get_next(m); }

    if(upd) {
      upc++;

      E(1,"Group %s needs to be updated.\n",grp);

      if(prov) {
        void *lib=meta_lib_get(prov);
        int  (*meta_set_for_file)(noir_md *md,char *filename,void *l);
        int   ret;

        if(!lib) {
          E(0,"Group %s cannot be updated -- no library \"%s\"!?\n",grp,prov->file?prov->file:"\"no file!?\""); }
        else if((meta_set_for_file=dlsym(lib,SET_FUN))) {
          E(1,"Updating %s, group %s, using %s...\n",filename,grp,prov->file);
          if((ret=meta_set_for_file(a,filename,prov))!=NOIR_SUCC) {
            switch(ret) {
            case NOIR_FAIL:        E(0,"general failure\n"); break;
            case NOIR_OOM:         E(0,"out of memory\n"); break;
            case NOIR_META_FILE:   E(0,"file I/O error -- maybe you may change the file, but may not create new files in the directory it is in?\n"); break;
            case NOIR_META_FORMAT: E(0,"invalid data in file handed to meta-data provider\n"); break;
            case NOIR_NODATA:      E(0,"no MetaData\n"); break;
            default:               E(0,"error %d\n",ret); break;
            }}}
        else 
          E(0,"Group %s cannot be updated -- " SET_FUN " not in library!?\n",grp); }
      else if(((intl=meta_internal(a,filename,grp))!=NOIR_SUCC)&&(intl!=NOIR_PARTIAL))
        E(0,"Group %s cannot be updated -- no provider!?  (%d)\n",grp,intl); }

    if(m)
      grp=m->grp; }

  return upc?NOIR_SUCC:NOIR_IGNORED; }






/*****************************************************************************/



/**
 * initialize meta-data sub-system.
 */
int meta_init(void) {
  FILE  *fh;
  char   l[META_MAX_LEN];
  char  *p,*p2;
  char *f;

  if((f=get_setup_file(TOOLTIP_PROVIDERS))) {
    if((fh=fopen(f,"r"))) {
      while(fgets(l,META_MAX_LEN,fh)) {
        if(util_parse_line(l,&p,&p2,NULL,NULL)==2) {
          if((p2=util_replace_variables(p2,NULL,NULL))) {
            meta_provider_add(p,p2);
            free(p2); }}}
      fclose(fh); }
    else
      E(0,"could not read providers (\"%s\")...\n",f);
    free(f); }
  else
    E(0,"could not read providers (\"" TOOLTIP_PROVIDERS "\")...\n");

  return NOIR_SUCC; }



/**
 * deinitialize meta-data sub-system.
 */
int meta_exit(void) {
  if(ht_providers)
    hash_table_dst(&ht_providers);
  if(pa) {
    noir_provider *p;
    while((p=pa)) {
      pa=pa->next;
      meta_provider_free(p); }}
  return NOIR_SUCC; }
#endif






/*****************************************************************************/
