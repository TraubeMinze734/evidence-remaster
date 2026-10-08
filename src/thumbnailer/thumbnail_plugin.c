/** @file  thumbnailer/thumbnail_plugin.c
 *  @brief plugin-API of the threaded modular thumbnailer.  plugin-cache.
 *
 * by Azundris.
 */


#include "config.h"

#include "thumbnail.h"
#include "thumbnail_private.h"
#include "thumbnail_plugin.h"

#include <dlfcn.h>    /* RTLD_NOW */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>   /* PATH_MAX */
#include <errno.h>
#include <time.h>

#include "errors.h"
#include "debug.h"
#include "util.h"     /* dlmulti() */
#include "mime.h"     /* MIME_DELIMITER */
#include "file_lcl.h" /* file_compose_path() */



#define TNP_DIR       PACKAGE_PREFIX "/lib/evidence/thumbnailer"
#define TNS_DIR       "thumbnailers"
#define TNS_ICONS     "icons.lst"
#define TNS_LIVE      "live.lst"
#define TNS_MOUSEOVER "mouseover.lst"



#ifndef GC_INTERVAL_THUMBNAILERPLUGINS
#  define GC_INTERVAL_THUMBNAILERPLUGINS  600
#endif






/*****************************************************************************/



typedef struct _tnp_register {
  thumbnail_plugin *tp;
  int               prio;
} tnp_register;



static void *ht_mappings[TP_COUNT];
static void *ht_plugins=NULL;



/*****************************************************************************/



/**
 * create a thumbnailer record
 * @return the newly-created record, or NULL
 * @param file  the plugin (path+file) this is for (or NULL)
 */
static thumbnail_plugin *thumbnail_plugin_new(const char *file) {
  thumbnail_plugin *tp;

  if((tp=malloc(sizeof(thumbnail_plugin)))) {
    memset(tp,0,sizeof(thumbnail_plugin));
    if(file) {
      if(!(tp->file=strdup(file))) {
        free(tp);
        return NULL; }
      hash_table_replace(ht_plugins,tp->file,tp); }
    return tp; }
  return NULL; }



/**
 * load a thumbnailer-plugin
 * @return an error-code
 * @param tp  the plugin to load (plugin-struct)
 */
int thumbnail_plugin_load(thumbnail_plugin *tp) {
  int ret;

  tp->ts=time(NULL);

  if(tp->lib)
    return NOIR_IGNORED;

  ret=dlmulti("thumbnail-plugin",tp->file,RTLD_NOW|RTLD_GLOBAL,&tp->lib,
              "!thumbnail_plugin_init !thumbnail_plugin_make ?thumbnail_plugin_exit ?thumbnail_plugin_live",
              &tp->init,&tp->make,&tp->exit,&tp->live);
  if((ret!=NOIR_SUCC)&&(ret!=NOIR_PARTIAL)) {
    if(ret==NOIR_NOTFOUND) {
      E(0,"\"%s\" is not a valid thumbnailer!\n",tp->file);
      ret=NOIR_TYPEMISMATCH;
      goto fail; }
    else {
      const char *err;
      if(!(err=dlerror()))
        err="???";
      E(0,"failed to load \"%s\": %s\n",tp->file,err);
      ret=NOIR_FAIL;
      goto fail; }}

  E(1,"loading thumbnailer  \"%s\"...\n",file_name(tp->file));
/* E(3,"init %s @ %p (%d) { %p, %p, %p }...\n",tp->file,tp->lib,ret,tp->init,tp->make,tp->exit); */

  if((ret=tp->init(tp))>=0)
    tp->can=ret;
  return ret;

 fail:
  if(tp->lib) {
    dlclose(tp->lib);
    tp->lib=NULL; }
  return ret; }



/**
 * unload a thumbnailer-plugin
 * @return an error-code
 * @param tp  the plugin
 */
static int thumbnail_plugin_unload(thumbnail_plugin *tp) {
  if(!tp||!tp->lib)
    return NOIR_IGNORED;

  if(tp->exit)
    tp->exit(tp);
  if(dlclose(tp->lib))
    E(0,"dlclose(\"%s\") failed: %d - %s\n",tp->file,errno,strerror(errno));
  tp->lib=NULL;

  return NOIR_SUCC; }



/**
 * garbage-collect a plugin. maybe.
 */
static void thumbnail_plugins_gc_one(char *key,void *value,void *data) {
  thumbnail_plugin *tp=(thumbnail_plugin *)value;
  if(tp->lib&&(tp->ts<((time_t)data))) {
    E(0,"garbage-collecting \"%s\"...\n",tp->file);
    thumbnail_plugin_unload(tp); }}



/**
 * garbage-collect plugins
 */
int thumbnail_plugins_gc(void) {
  return ht_plugins?hash_table_walk(ht_plugins,thumbnail_plugins_gc_one,(void *)(time(NULL)-GC_INTERVAL_THUMBNAILERPLUGINS)):NOIR_IGNORED; }



/**
 * discard a thumbnailer-plugin and dst its entry
 * @return an error-code
 * @param tp  the plugin
 */
static int thumbnail_plugin_dst(thumbnail_plugin *tp) {
  int ret;

  if(!tp)
    return NOIR_IGNORED;

  if(thumbnail_plugin_unload(tp)==NOIR_SUCC)
    ret=NOIR_SUCC;
  else
    ret=NOIR_PARTIAL;

  if(tp->file) {
    hash_table_remove(ht_plugins,tp->file);
    free(tp->file); }

  free(tp);
  return ret; }



static int thumbnail_plugin_unref(thumbnail_plugin *tp) {
  if(!tp)
    return NOIR_IGNORED;
  if(tp->refcount<=0)
    return NOIR_FAIL;

  if(--tp->refcount<=0)
    return thumbnail_plugin_dst(tp);
  return NOIR_SUCC; }



static int thumbnail_plugin_ref(thumbnail_plugin *tp) {
  if(!tp)
    return NOIR_IGNORED;
  tp->refcount++;
  return NOIR_SUCC; }



/**
 * discard a MIME-type / thumbnailer assignment and 
 */
static void tnp_register_dst(void *f) {
  tnp_register *tnp=(tnp_register *)f;
  if(tnp) {
    thumbnail_plugin_unref(tnp->tp);
    free(tnp); }}






/*****************************************************************************/
/* setup */



static int thumbnail_plugins_setup_file_get_date(const char *path,const char *file,time_t *r) {
  struct stat  st;
  int          ret;
  char        *n=file_compose_path(path,file,NULL);

  *r=0;

  if(!n)
    return NOIR_OOM;

  if(!stat(n,&st)) {
    *r=st.st_mtime;
    ret=NOIR_SUCC; }
  else
    ret=NOIR_FAIL;

  E(3,"\"%s\": time-stamp %ld\n",n,*r);

  free(n);

  return ret; }



static int thumbnail_plugins_setup_uptodate_p(const char *setup) {
  time_t  tnp,icons,live,mouseover;
  int     ret=TRUE;
  char   *path=file_compose_path(setup,TNS_DIR,NULL);

  if(!path)
    return FALSE;

  thumbnail_plugins_setup_file_get_date(TNP_DIR,NULL,      &tnp);
  if(!tnp)
    goto done;

  thumbnail_plugins_setup_file_get_date(path,TNS_ICONS,    &icons);     if(tnp>icons)     ret=FALSE;
  thumbnail_plugins_setup_file_get_date(path,TNS_LIVE,     &live);      if(tnp>live)      ret=FALSE;
  thumbnail_plugins_setup_file_get_date(path,TNS_MOUSEOVER,&mouseover); if(tnp>mouseover) ret=FALSE;

  free(path);

 done:
  return ret; }



static void thumbnail_plugins_setup_regenerate_save_entry(char *key,void *value,void *data) {
  tnp_register *tnp=(tnp_register *)value;
  fprintf((FILE *)data,"%-30.30s    %s\n",key,tnp->tp->file); }



static int thumbnail_plugins_setup_regenerate_save(const char *path,const char *file,thumbnail_plugin_register_for type) {
  char *cp=file_compose_path(path,TNS_DIR,file,NULL);
  FILE *fh=NULL;
  int   ret=NOIR_SUCC;

  if(!cp)
    return NOIR_OOM;

  if(!(fh=fopen(cp,"w"))) {
    E(0,"failed to open \"%s\" for writing...\n",cp);
    ret=NOIR_NOFILE;
    goto done; }

  hash_table_walk(ht_mappings[type],thumbnail_plugins_setup_regenerate_save_entry,fh);

  fclose(fh);

 done:
  free(cp);

  return ret; }



/**
 * load all plugins from plugin dir and work out what they can do
 * @return an error-code
 * @param setup  the setup (not the plugins-!) dir
 */
static int thumbnail_plugins_setup_regenerate(const char *setup) {
  dir_entry        *de=file_getdir(TNP_DIR,FGD_STAT,NULL,NULL),
                   *te;
  thumbnail_plugin *tp;
  char             *p;
  int               c;

  if(!de)
    return NOIR_NOFILE;

  for(c=0;c<TP_COUNT;c++)
    ht_mappings[c]=hash_table_new(free,tnp_register_dst);

  for(te=de;te;te=te->next) {
    p=te->name?strrchr(te->name,'.'):NULL;

    if(p&&!strcmp(p,".so")&&S_ISREG(te->mode)) {
      if((tp=hash_table_fetch(ht_plugins,te->tag)))
        E(0,"thumbnailer \"%s\" already loaded!?\n",te->name);

      else if((tp=thumbnail_plugin_new(te->tag))) {
        if(thumbnail_plugin_load(tp)>=0) {
          E(4,"thumbnailer \"%s\" loaded.\n",te->name);
          thumbnail_plugin_ref(tp); }
        else /* don't print this at debug==0, thumbnail_plugin_load() will. */
          E(2,"failed to load potential thumbnailer \"%s\"...\n",te->name); }

      else
        E(0,"out of memory!?\n"); }

    else
      E(2,"skipping \"%s\"...\n",te->name); }

  dir_entry_dst_all(de);

  { char *cp=file_compose_path(setup,TNS_DIR,NULL);
    if(cp) {
      file_mkdir(cp,0700,TRUE);
      free(cp); }}

  thumbnail_plugins_setup_regenerate_save(setup,TNS_ICONS,    TP_ICONS);
  thumbnail_plugins_setup_regenerate_save(setup,TNS_MOUSEOVER,TP_MOUSEOVER);
  thumbnail_plugins_setup_regenerate_save(setup,TNS_LIVE,     TP_LIVE);

  return NOIR_SUCC; }



static int thumbnail_plugins_setup_load(const char *path,const char *file,thumbnail_plugin_register_for type) {
  char             *cp=file_compose_path(path,TNS_DIR,file,NULL),*p,*p3;
  FILE             *fh=NULL;
  int               ret=NOIR_SUCC;
  char              l[MIME_MAX_LEN];
  thumbnail_plugin *tp=NULL;
  tnp_register     *tnp=NULL;


  if(!cp)
    return NOIR_OOM;

  ht_mappings[type]=hash_table_new(free,tnp_register_dst);

  if(!(fh=fopen(cp,"r"))) {
    E(1,"failed to open \"%s\" for reading...\n",cp);
    ret=NOIR_NOFILE;
    goto done; }

  while(fgets(l,PATH_MAX+64,fh)) {
    if(util_parse_line(l,&p,&p3,NULL,NULL)==2) {
      if(!(tp=hash_table_fetch(ht_plugins,p3)))
        tp=thumbnail_plugin_new(p3);

      if(!(tnp=malloc(sizeof(tnp_register)))) {
        ret=NOIR_OOM;
        goto fail; }

      if(!(p=strdup(p))) {
        ret=NOIR_OOM;
        goto fail; }

      tnp->tp=tp;
      tnp->prio=TP_PRIO_MID;

      thumbnail_plugin_ref(tp);

      E(3,"%s: %s => %s\n",cp,p,tp->file);

      hash_table_replace(ht_mappings[type],p,tnp);

      tp=NULL;
      tnp=NULL; }}

 fail:
  if(tp)
    thumbnail_plugin_dst(tp);
  if(tnp)
    free(tnp);
  fclose(fh);

 done:
  free(cp);

  return ret; }







/*****************************************************************************/
/* thumbnailer-main API */
/************************/



/**
 * try to load all available thumbnailer-plugins
 * @return an error-code
 * @param setup  the directory containing the setup
 */
int thumbnail_plugins_init(const char *setup) {
  if(!(ht_plugins=hash_table_new(NULL,NULL)))
    return NOIR_OOM;

  if(!thumbnail_plugins_setup_uptodate_p(setup)) {
    E(0,"regenerating lists of thumbnailers...\n");
    thumbnail_plugins_setup_regenerate(setup); }
  else {
    thumbnail_plugins_setup_load(setup,TNS_ICONS,    TP_ICONS);
    thumbnail_plugins_setup_load(setup,TNS_MOUSEOVER,TP_MOUSEOVER);
    thumbnail_plugins_setup_load(setup,TNS_LIVE,     TP_LIVE); }
  return NOIR_SUCC; }



/**
 * discard all thumbnailer-plugins
 */
int thumbnail_plugins_exit(void) {
  int               c=0;

  for(c=0;c<TP_COUNT;c++)
    if(ht_mappings[c])
      hash_table_dst(&ht_mappings[c]);

  hash_table_dst(&ht_plugins);

  return c; }



/**
 * find a thumbnailer-plugin that handles the given MIME-type
 * @return the plugin, or NULL
 * @param mime  the MIME-type
 * @param type  icons/mouseover/live
 */
struct _thumbnail_plugin *thumbnail_plugin_find_by_mime(const char *mime,thumbnail_plugin_register_for type) {
  tnp_register *r=NULL;
  char         *m,*p;
  if(!mime||!ht_mappings[type])
    return NULL;
  if(!(r=hash_table_fetch(ht_mappings[type],mime))) {
    if((m=strdup(mime))) {
      if((p=strchr(m,MIME_DELIMITER))) {
        *(++p)='\0';
        r=hash_table_fetch(ht_mappings[type],m); }
      free(m); }}
  return r?r->tp:NULL; }






/*****************************************************************************/
/* plugin API */
/**************/



/**
 * lets thumbnailer-plugins announce they know how to thumbnail files of this MIME-type
 * @return an error-code
 * @param tp    the plugin
 * @param mime  the MIME-type
 * @param prio  priority
 * @param type  can provide icon-size thumbs, quality previews, and/or live thumbs
 */
int thumbnail_plugin_register_mime(thumbnail_plugin *tp,const char *mime,thumbnail_plugin_prio prio,thumbnail_plugin_register_for type) {
  char             *m=strdup(mime),*q;
  const char       *p,*x;
  tnp_register     *ep,*np=NULL;

  if(!m)
    return NOIR_OOM;
  if((q=strchr(m,'*')))
    *q='\0';

  p=file_name(tp->file);

  if((ep=hash_table_fetch(ht_mappings[type],m))&&
#if (TP_PRIO_TOP>TP_PRIO_BOTTOM)
     (ep->prio>=prio)
#else
     (ep->prio<=prio)
#endif
    ) {
    x=file_name(ep->tp->file);
    if(tp!=ep->tp)
      E(0,"ignoring \"%s\"'s request to be registered for %s; \"%s\" wins %d:%d.\n",p,m,x,prio,ep->prio);
    free(m);
    return NOIR_IGNORED; }

  E(2,"registering \"%s\" for %s\n",p,m);

  if(!(np=malloc(sizeof(tnp_register))))
    return NOIR_OOM;
  memset(np,0,sizeof(tnp_register));

  np->tp=tp;
  np->prio=prio;

  thumbnail_plugin_ref(tp);

  return hash_table_replace(ht_mappings[type],m,np); }



/**
 * lets a thumbnailer-plugin set private data
 * this data persists from call to call, but not from session to session and
 * is normally used to store a pointer to memory allocated when the plugin
 * was initialized, so it may be free()d when the plugin is unloaded
 * @return an error-code
 * @param tp    the plugin
 * @param data  the data to store on the plugin-struct
 */
int thumbnail_plugin_data_set(thumbnail_plugin *tp,void *data) {
  if(!tp)
    return NOIR_FAIL;
  tp->data=data;
  return NOIR_SUCC; }



/**
 * lets a thumbnailer-plugin get its private data
 * this data persists from call to call, but not from session to session and
 * is normally used to store a pointer to memory allocated when the plugin
 * was initialized, so it may be free()d when the plugin is unloaded
 * @return an error-code
 * @param tp    the plugin
 * @param data  where to store from the plugin-struct
 */
int thumbnail_plugin_data_get(thumbnail_plugin *tp,void **data) {
  if(!tp)
    return NOIR_FAIL;
  if(!data)
    return NOIR_IGNORED;
  *data=tp->data;
  return NOIR_SUCC; }






/*****************************************************************************/
