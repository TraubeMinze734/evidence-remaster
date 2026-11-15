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
#include <dirent.h>
#include <limits.h>   /* PATH_MAX */
#include <errno.h>
#include <time.h>
#include <pthread.h>

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

/* serializes thumbnail_plugin_load(): with a worker pool, several workers
   can lazy-load the same not-yet-loaded plugin at once -- dlopen() itself
   is thread-safe, but the check-then-init below is not (double init()) */
static pthread_mutex_t plugin_load_mutex=PTHREAD_MUTEX_INITIALIZER;

/* set by thumbnail_exit() before workers are joined (declared in
   thumbnail_plugin.h) */
int thumbnail_plugins_shutting_down=0;

/* robust lock for plugin_load_mutex (EOWNERDEAD recovery, see
   imlib_mutex_lock in thumbnail.c) */
static void plugin_load_lock(void) {
  int r=pthread_mutex_lock(&plugin_load_mutex);
  if(r==EOWNERDEAD)
    pthread_mutex_consistent(&plugin_load_mutex);
  else if(r)
    E(0,"pthread_mutex_lock(plugin_load_mutex): #%d - %s\n",r,strerror(r)); }

#ifdef HAVE_EVAS2
#  include <Ecore.h>
#  include "evidence.h"   /* main_thread */

/* handshake for running a plugin load in the GUI thread: plugin init()
   (e.g. emotion_init() -> gstreamer/ecore EO objects) must happen in the
   main thread's EO domain -- a worker doing it showed up as
   "Eo ID ... is not a valid object" EINA errors and broken decoding.
   heap-allocated: if the waiter times out (main loop dead at shutdown) and
   leaves, the dispatched callback frees the struct via "abandoned". */
typedef struct {
  thumbnail_plugin *tp;
  int               ret;
  pthread_mutex_t   mtx;
  pthread_cond_t    cond;
  int               done;
  int               abandoned;
} plugin_load_hs;

static void _plugin_load_in_main(void *data) {
  plugin_load_hs *hs=(plugin_load_hs *)data;
  int             ret;
  pthread_mutex_lock(&hs->mtx);
  if(hs->abandoned) {               /* waiter gave up: nobody will read ret */
    pthread_mutex_unlock(&hs->mtx);
    free(hs);
    return; }
  pthread_mutex_unlock(&hs->mtx);
  ret=thumbnail_plugin_load(hs->tp);   /* recursion is safe: we ARE main now */
  pthread_mutex_lock(&hs->mtx);
  hs->ret=ret;
  hs->done=1;
  pthread_cond_signal(&hs->cond);
  pthread_mutex_unlock(&hs->mtx); }
#endif



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

#ifdef HAVE_EVAS2
  if(!pthread_equal(pthread_self(),main_thread)) {
    /* called from a thumbnailer worker: dispatch the load (dlopen + init)
       to the GUI thread and wait for it.  plugin init may create EO
       objects (emotion_init/gstreamer), which must live in the main
       thread's domain -- done inline here it produced EINA "Eo ID not
       valid" errors and left decoding broken for the whole session. */
    plugin_load_hs *hs;
    struct timespec wto;
    int             i;

    if(thumbnail_plugins_shutting_down)
      return NOIR_FAIL;   /* exiting: main loop will never dispatch this */

    if(!(hs=calloc(1,sizeof(plugin_load_hs))))
      return NOIR_OOM;
    hs->tp=tp;
    pthread_mutex_init(&hs->mtx,NULL);
    pthread_cond_init(&hs->cond,NULL);

    ecore_main_loop_thread_safe_call_async(_plugin_load_in_main,hs);

    /* wait in slices so we notice thumbnail_plugins_shutting_down (set at
       exit, when the queued call will never run) without hanging */
    clock_gettime(CLOCK_REALTIME,&wto);
    wto.tv_sec+=30;
    pthread_mutex_lock(&hs->mtx);
    for(i=0;!hs->done&&!thumbnail_plugins_shutting_down;i++) {
      struct timespec slice=wto;
      slice.tv_sec=time(NULL)+1;   /* wake at least once a second */
      if(slice.tv_sec>wto.tv_sec)
        slice=wto;
      if(pthread_cond_timedwait(&hs->cond,&hs->mtx,&slice)==ETIMEDOUT&&
         slice.tv_nsec==wto.tv_nsec&&slice.tv_sec==wto.tv_sec)
        break; }
    if(hs->done) {
      ret=hs->ret;
      pthread_mutex_unlock(&hs->mtx);
      pthread_mutex_destroy(&hs->mtx);
      pthread_cond_destroy(&hs->cond);
      free(hs);
      return ret; }
    /* timed out or shutting down: leave -- the dispatched callback (if it
       ever runs) frees hs via "abandoned" */
    hs->abandoned=1;
    pthread_mutex_unlock(&hs->mtx);
    return NOIR_FAIL; }
#endif

  plugin_load_lock();

  if(tp->lib) {   /* another worker loaded it while we waited */
    pthread_mutex_unlock(&plugin_load_mutex);
    return NOIR_IGNORED; }

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
      /* a missing runtime dependency (e.g. libid3-3.8.so.3 after id3lib
         was removed) is a deployment issue, not a plugin bug -- don't
         scream at level 0 on every list-regenerate.  dlerror() text is
         locale-dependent, so match both English and German glibc wording */
      if(strstr(err,"shared object")||strstr(err,"Shared-Object")||
         strstr(err,"shared library")||strstr(err,"Shared library"))
        E(1,"skipping thumbnailer \"%s\", missing dependency: %s\n",tp->file,err);
      else
        E(0,"failed to load \"%s\": %s\n",tp->file,err);
      ret=NOIR_FAIL;
      goto fail; }}

  E(1,"loading thumbnailer  \"%s\"...\n",file_name(tp->file));
/* E(3,"init %s @ %p (%d) { %p, %p, %p }...\n",tp->file,tp->lib,ret,tp->init,tp->make,tp->exit); */

  if((ret=tp->init(tp))>=0)
    tp->can=ret;
  pthread_mutex_unlock(&plugin_load_mutex);
  return ret;

 fail:
  if(tp->lib) {
    dlclose(tp->lib);
    tp->lib=NULL; }
  pthread_mutex_unlock(&plugin_load_mutex);
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


/* the plugin directory's mtime does not change when a plugin file is
   overwritten ('make install' just cp's over it), so measure the newest
   plugin file instead of the directory containing them */
static int thumbnail_plugins_setup_plugins_get_date(const char *path,time_t *r) {
  DIR           *dir;
  struct dirent *de;
  struct stat    st;
  char          *n;
  *r=0;
  if(!(dir=opendir(path)))
    return NOIR_FAIL;
  while((de=readdir(dir))) {
    size_t l=strlen(de->d_name);
    if((l>3)&&!strcmp(de->d_name+l-3,".so")) {
      if((n=file_compose_path(path,de->d_name,NULL))) {
        if(!stat(n,&st)&&(st.st_mtime>*r))
          *r=st.st_mtime;
        free(n); } } }
  closedir(dir);
  return *r?NOIR_SUCC:NOIR_FAIL; }



static int thumbnail_plugins_setup_uptodate_p(const char *setup) {
  time_t  tnp,icons,live,mouseover;
  int     ret=TRUE;
  char   *path=file_compose_path(setup,TNS_DIR,NULL);

  if(!path)
    return FALSE;

  thumbnail_plugins_setup_plugins_get_date(TNP_DIR,          &tnp);
  if(!tnp)
    goto done;

  /* '>=': an install that touches plugin dir and cache in the same second
     must still trigger a regenerate, or newly installed thumbnailers
     stay invisible until the next plugin-dir change */
  thumbnail_plugins_setup_file_get_date(path,TNS_ICONS,    &icons);     if(tnp>=icons)     ret=FALSE;
  thumbnail_plugins_setup_file_get_date(path,TNS_LIVE,     &live);      if(tnp>=live)      ret=FALSE;
  thumbnail_plugins_setup_file_get_date(path,TNS_MOUSEOVER,&mouseover); if(tnp>=mouseover) ret=FALSE;

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
  char              l[PATH_MAX+64];
  thumbnail_plugin *tp=NULL;
  tnp_register     *tnp=NULL;


  if(!cp)
    return NOIR_OOM;

  ht_mappings[type]=hash_table_new(free,tnp_register_dst);

  if(!(fh=fopen(cp,"r"))) {
    E(1,"failed to open \"%s\" for reading...\n",cp);
    ret=NOIR_NOFILE;
    goto done; }

  while(fgets(l,sizeof(l),fh)) {
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
  /* robust plugin_load_mutex (a GC-cancelled worker must not leave it
     locked forever); runs in the GUI thread before any worker exists */
  {
    pthread_mutexattr_t pattr;
    pthread_mutexattr_init(&pattr);
    pthread_mutexattr_setrobust(&pattr,PTHREAD_MUTEX_ROBUST);
    pthread_mutex_destroy(&plugin_load_mutex);
    pthread_mutex_init(&plugin_load_mutex,&pattr);
    pthread_mutexattr_destroy(&pattr); }

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

  thumbnail_plugins_shutting_down=1;   /* belt & braces: also set by thumbnail_exit() */

  for(c=0;c<TP_COUNT;c++)
    if(ht_mappings[c])
      hash_table_dst(&ht_mappings[c]);

  if(ht_plugins)   /* may be NULL if plugins were never inited (no main loop) */
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
