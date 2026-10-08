/** @file  monitors.c
 *  @brief file-monitors (notice new/changed/deleted items in file-system)
 */

#include "config.h"

#include <string.h>      /* str*() */
#include <stdlib.h>      /* free() */
#include <sys/stat.h>    /* S_ISDIR() */

#include "debug.h"
#include "errors.h"
#include "gui/common/session.h"  /* noir_sess_focus() */
#include "monitors.h"
#include "util.h"        /* esprintf() */
#include "gui/common/alert.h"       /* form_dial() */
#include "gui/common/session.h"     /* noir_sess_drop() */
#include "file.h"        /* dir_entry */
#include "file_fun.h"    /* file_get_backend() */

#include "model_private.h"  /* ### FIXME */



static noir_monitor      *monitors=NULL;
static char              *fmt[]={ "DIR", "FILE" };



/*****************************************************************************/



/**
 * get anchor of monitor-chain
 */
noir_monitor *file_monitor_get_1st(void) {
  return monitors; }






/*****************************************************************************/

/**
 * something changed in the real world, update our model of it!
 * @return an error-code
 * @param fi       the file-system item this event is about
 * @param etype    the kind of event we talk about (added, deleted, ...)
 * @param session  what session
 */
static int file_monitor_update_fi(file_info *fi,model_event etype,void *session) {
  int           ret;
  const char   *tag;
  noir_monitor *m;

  if(!(tag=file_info_get_tag(fi)))
    return NOIR_FAIL;

  if((ret=(file_get_backend(tag)->file_get_info(session,tag,&fi)))==NOIR_SUCC) {
    file_get_meta_with_lock(fi,NULL);  /* FIXME ### potential race, try &qh->qchg */
    file_info_add_infobits(fi,FF_META|FF_STAT); }
  else {
    meta_del_all(file_info_get_meta(fi));
    file_info_clr_infobits(fi,FF_META|FF_STAT); }

  if(fi) {
#if 0
    meta *m=meta_in_group(file_info_get_meta_pointer(fi),"Size",META_DFLT_GRP);
    E(1,"item was modified: %s  (%ld,%s)  %d\n",tag,file_info_get_size(fi),(char *)(m?m->val:"???"),ret);
#endif
    ret=model_evt(fi,MODEL_UPD,file_info_get_infobits(fi));

    for(m=monitors;m;m=m->next)
      if(m->file_event_handler&&!strcmp(tag,m->want))
        m->file_event_handler(session,fi,MODEL_UPD,file_info_get_infobits(fi),m->cb_data); }

  return ret; }



/**
 * remove 'file' from all sessions' selections.
 * @param fi       the file-info
 * @param session  the session, or NULL
 */
static int file_monitor_selection_remove(file_info *fi,void *session) {
  int             ret=NOIR_NOTFOUND;
  action_context *ac;
  action_file    *af;
  noir_sess      *ns;
  const char     *tag=file_info_get_tag(fi);
  noir_monitor   *m;

  for(ns=noir_sess_get_1st();ns;ns=noir_sess_get_next(ns)) {
    ac=noir_sess_get_selection(ns);
    if((af=action_context_selection_contains(ac,tag))) {
      action_context_remove_and_dst_file(ac,af);
      ret=NOIR_SUCC; }}

  for(m=monitors;m;m=m->next)
    if(m->file_event_handler&&!strcmp(tag,m->want))
      m->file_event_handler(session,fi,MODEL_DEL,FF_NONE,m->cb_data);

  return ret; }






/*****************************************************************************/
/* file-monitors */



/**
 * check whether a monitor for this file already exists in list
 * @return monitor-struct if it already exists, or NULL otherwise
 * @param ns    the session the monitor should be part of, or NULL for any
 * @param file  the file the monitor is for
 * @param prv   location where a pointer to the preceeding list-element will
 *              be stored, or NULL (so caller can remove found element)
 */
noir_monitor *file_monitor_exists(void *ns,char *file,noir_monitor **prv) {
  noir_monitor *p=NULL,*e;
  char         *s;

  if(!file||!*file)
    return NULL;

  if((s=strrchr(file,DIR_DELIMITER))&&(s!=file)&&!s[1]) {
    E(0,"trailing slash in \"%s\"!?\n",file);
    *s='\0'; }

  e=monitors;
  while(e&&((ns&&(e->session!=ns))||strcmp(e->want,file))) {
    p=e;
    e=e->next; }

  if(prv)
    *prv=e?p:NULL;

  return e; }



/**
 * release struct for an abstract monitor
 * @param g  the monitor to release
 */
static noir_monitor *noir_monitor_dst(noir_monitor *g) {
  if(g) {
    g->system="destroyed";
    g->next=NULL;
    if(g->want)
      free(g->want);
    if(g->have)
      free(g->have);
    free(g); }
  return NULL; }



/**
 * remove all monitors for a given file in a given session, or in all sessions
 * @return an error-code
 * @param ns    the session to remove the monitors in, or NULL for all sessions
 * @param file  the file the monitors are for
 */
int file_monitor_remove(void *ns,char *file) {
  noir_monitor *p,*e;
  int ret=NOIR_NOTFOUND;

  while((e=file_monitor_exists(ns,file,&p))) {
    E(1,"removing \"%s\"...\n",file);

    ret=ns?NOIR_PARTIAL:NOIR_SUCC;   /* removed one or many entries */

    /* let backend remove "real" monitor */
    file_get_backend(file)->file_m_rem(e);

    /* remove found monitor from chain */
    if(p)
      p->next=e->next;
    else
      monitors=e->next;

    /* free() monitor */
    noir_monitor_dst(e); }

  return ret; }



/**
 * remove a given monitor
 * @return an error-code
 * @param m  the monitor to remove
 */
static int file_monitor_remove_one(noir_monitor *m) {
  noir_monitor *p=NULL,*e;

  e=monitors;
  while(e&&(e!=m)) {
    p=e;
    e=e->next; }

  if(!e)
    return NOIR_NOTFOUND;

  file_get_backend(m->have)->file_m_rem(e);

  /* remove found monitor from chain */
  if(p)
    p->next=e->next;
  else
    monitors=e->next;

  /* free() monitor */
  noir_monitor_dst(e);

  return NOIR_SUCC; }



/**
 * remove all monitors (for a given session, or all sessions).
 * @return an error-code
 * @param ns  the session whose monitors to remove, or NULL for all sessions
 */
int file_monitor_remove_all(void *ns) {
  noir_monitor *e,        /* current  monitor */
               *f=NULL,   /* previous monitor */
               *g;

  e=monitors;

  while(e) {
    /* if no session is given, or monitor is in given session, remove it */
    if(!ns||(e->session==ns)) {
      E(1,"removing \"%s\"...\n",e->want);
      file_get_backend(e->have)->file_m_rem(e);

      if(!f)                   /* first monitor, reset anchor to remove it */
        monitors=e->next;
      else
        f->next=e->next;       /* !1st monitor, unhook it */

      g=e;
      e=e->next;
      noir_monitor_dst(g); }

    else {             /* do nothing with monitor, */
      f=e;             /* but remember it as previous monitor */
      e=e->next; }}

  return NOIR_SUCC; }




/**
 * find deepest existing element in given path
 * @return a newly-allocated buffer containing the deepest existing element, or NULL
 * @param want   the path to look in
 */
static inline char *file_monitor_find_real(char *want) {
  int   fs=0;
  char *have=strdup(want),*p;

  while(1) {
    if((fs=file_exists(have)))
      return have;

    if(!(p=strrchr(have,DIR_DELIMITER))) {  /* this should never happen */
      free(have);
      return NULL; }
    p[(p==have)?1:0]='\0'; }}   /* hack off deepest item, try again */



/**
 * add a monitor for a file/directory
 * calls backend_monitor_add() in backend to add "real" monitor.
 * @return an error-code
 * @param ns                  the session that wants to register this monitor
 * @param type                the type of the monitor (file, dir, ...)
 * @param file                the tag of the item to monitor
 * @param file_event_handler  a callback, or NULL for model only
 * @param data                additional data to hand to the callback
 */
int file_monitor_add_with_handler(void *ns,noir_file_monitor type,char *file,int (*file_event_handler)(void *session,file_info *fi,model_event ev,file_flags fc,void *data),void *data) {
  int           ms;
  noir_monitor *m;
  char         *p,*have;
  file_info    *fi;
  item_flags    fif;

  if(!ns||!file||!*file)
    return NOIR_FAIL;

#if 0
  if(((l=strlen(file))<2)||!strcmp("/.",&file[l-2]))
    return NOIR_IGNORED;
#endif

  if((p=strrchr(file,DIR_DELIMITER))) {  /* flag trailing / if any */
    if((p!=file)&&!p[1]) {
      E(0,"trailing slash in \"%s\"!?\n",file);
      *p='\0'; }}

  /* do not monitor virtual folders for now */
  if((fi=file_info_fetch(NULL,file))&&((fif=file_info_get_flags(fi))>0)&&(fif&IF_VIRTUAL))
    return NOIR_NOTSUPP;

  if(file_monitor_exists(ns,file,NULL))
    return NOIR_IGNORED;

  if(!(have=file_monitor_find_real(file)))
    return NOIR_FAIL;

  if(!strcmp(file,have)&&
     ((S_ISDIR((ms=file_exists(file)))&&(type!=NOIR_MONITOR_DIR))||
      (!S_ISDIR(ms)&&(type!=NOIR_MONITOR_FILE)))) {
    E(0,"monitor type %d (%s) does not match type of %s ($%x-%s)!\n",type,(type<NOIR_MONITOR_TYPES)?fmt[type]:"ILL",file,ms,S_ISDIR(ms)?"directory":"file");
    free(have);
    return NOIR_MONMIS; }



  /* add administrative structure */

  if(!(m=malloc(sizeof(noir_monitor))))
    goto oom;

  memset(m,0,sizeof(noir_monitor));  /* so noir_monitor_dst() can work */

  /* m->system must remain NULL so it will be set by the backend */

  if(!(m->have=strdup(have)))
    goto oom;

  free(have);
  have=NULL;

  if(!(m->want=strdup(file)))
    goto oom;

  E(1,"adding type %d (%s) for \"%s\" (\"%s\")\n",type,(type<NOIR_MONITOR_TYPES)?fmt[type]:"ILL",m->want,m->have);

  m->file_event_handler=file_event_handler;

  m->session  =ns;
  m->ts       =time(NULL);
  m->handle   =NULL;
  m->type     =!strcmp(m->want,m->have)?type:NOIR_MONITOR_DIR;
  m->cb_data  =data;
  m->next     =monitors;
  monitors    =m;

  /* add "real" monitor in backend */
  if(file_get_backend(file)->file_m_add(m)!=NOIR_SUCC) {
    E(0,"failed for \"%s\"...\n",m->want);
    file_monitor_remove_one(m);
    return NOIR_FAIL; }

  m->type=type;                  /* fix to what was originally requested */
  E(4,"added \"%s\" as %p\n",m->want,m->handle);
  return NOIR_SUCC;

 oom:
  if(m)
    noir_monitor_dst(m);
  if(have)
    free(have);
  return NOIR_OOM; }



/**
 * add a monitor for a file/directory
 * calls backend_monitor_add() in backend to add "real" monitor.
 * @return an error-code
 * @param ns                  the session that wants to register this monitor
 * @param type                the type of the monitor (file, dir, ...)
 * @param file                the tag of the item to monitor
 */
int file_monitor_add(void *ns,noir_file_monitor type,char *file) {
  return file_monitor_add_with_handler(ns,type,file,NULL,NULL); }



/**
 * create per-file events from dir-update event.
 * this is a helper function.  ideally, if a directory is monitored, the
 * backend should send us individual events for each added, deleted modified
 * item.  some backends cannot do this; all they send is a change event for
 * the directory itself.  if this happens, this routine finds out what really
 * changed and sends more specific events accordingly.
 * @return an error-code
 * @param m    the monitor that the unspecified directory-event occurred on
 * @param tag  the tag of the directory that changed
 */
static int file_monitor_reload_maybe(noir_monitor *m,char *tag) {
  file_info *dir=file_info_fetch(NULL,tag),*fi,*fd=NULL;
  int        ret=NOIR_NOTFOUND;
  time_t     ts;

  if(dir&&file_info_get_intrinsic(dir))
    ts=NOIR_MAX(file_info_get_mtime(dir),file_info_get_ctime(dir));
  else
    ts=time(NULL);

  if(dir) {
    dir_entry *d,*e;

    E(1,"%s  RELOAD/MAYBE\n",dir->tag);

    if((d=file_getdir(dir->tag,FGD_STAT,NULL,NULL))) {
      char *tag;

      for(e=d;e;e=e->next) {  /* normalize items from monitors */
        if((tag=path_rewrite(e->tag))) {
          free(e->tag);
          e->tag=tag; }}

      /* find deleted items (present in fi, but not in e) */
      fi=dir->child;
      while(fi) {
        e=d;
        while(e&&strcmp(fi->tag,e->tag))
          e=e->next;
        fd=fi;
        fi=fi->next;
        if(!e) {
          if(!strcmp(fd->name,DIR_DOT))
            E(2,"keeping " DIR_DOT " in dir: %s\n",fd->tag);
          else {
            E(1,"del item in dir: %s (%s)\n",fd->tag,fd->name);
            file_monitor_selection_remove(fd,m->session);
            model_evt(fd,MODEL_DEL,FF_NONE); }}}

      /* find added items (present in e, but not in fi) */
      for(e=d;e;e=e->next) {
        fi=dir->child;
        while(fi&&strcmp(fi->tag,e->tag))
          fi=fi->next;
        if(!fi) {
          char *p=e->tag+strlen(dir->tag)+1;
          if(strcmp(p,DIR_MYSELF)&&strcmp(p,DIR_PARENT)) {
            /* check if we already have the item.  this can happen for
               .items for instance. */
            if((fi=file_info_fetch(NULL,e->tag))) {
              /* it's a known item, but its timestamp changed. update! */
              if(e->mtime>file_info_get_mtime(fi)) {
                E(1,"updating \"%s\" (%ld)...\n",e->tag,e->mtime-file_info_get_mtime(fi));
                file_monitor_update_fi(fi,MODEL_UPD,m->session); }
              else
                E(3,"ignoring known item \"%s\"...\n",e->tag); }

            /* it's actually a new item. add it. */
            else if((fi=file_info_insert(NULL,e->tag))) {
              /* don't go through deferred stat for the time being --
                 making things easy for ourselves for now.  not good
                 if very many things changed (were copied here) at the
                 same time. */
              E(1,"new item in dir: %s\n",e->tag);
              file_monitor_update_fi(fi,MODEL_UPD,m->session); }
            else
              E(0,"could not create new item \"%s\"!?\n",e->tag); }}
        else if(e->mtime>=ts)
          file_monitor_update_fi(fi,MODEL_UPD,m->session); }

      dir_entry_dst_all(d);

      return NOIR_SUCC; }}

  return ret; }



/**
 * something along the path of a not-yet-existing item we're waiting for has changed
 * update monitor accordingly
 * @return an error-code
 * @param m      the monitor in question
 * @param etype  the event that was received for this monitor
 */
static int file_monitor_update(noir_monitor *m,noir_file_event etype) {
  int                ret=NOIR_OOM;
  noir_file_monitor  mt=m->type;
  char              *ho=m->have,*hn=file_monitor_find_real(m->want);

  if(!hn)
    return NOIR_FAIL;

  if(!strcmp(hn,ho)) {
    free(hn);
    return NOIR_IGNORED; }

  if(!(m->have=strdup(hn))) {  /* if we can't copy it, we'll use the original */
    m->have=hn;
    hn=NULL; }

  E(0,"\"%s\" (\"%s\" => \"%s\")\n",m->want,ho,m->have);

  m->type=!strcmp(m->want,m->have)?m->type:NOIR_MONITOR_DIR;

  file_get_backend(ho)->file_m_rem(m);
  m->handle=NULL;
  if(file_get_backend(m->have)->file_m_add(m)!=NOIR_SUCC) {
    file_monitor_remove_one(m);
    ret=NOIR_FAIL; }
  else {
    m->type=mt;
    ret=(!strcmp(m->have,m->want)||!strcmp(ho,m->want))?NOIR_SUCC
                                                       :NOIR_PARTIAL; }

  if(hn)
    free(hn);
  if(ho)
    free(ho);
  return ret; }



/**
 * the backend detected a change on an item we were monitoring.
 * @return an error-code
 * @param m     the monitor the event is for
 * @param type  the type of the event (item deleted, added, modified...)
 * @param tag   the tag of the file we got an event for
 */
int file_monitor_event(noir_monitor *m,noir_file_event type,char *tag) {
  char       *types[]={ "???", "CHG", "DEL", "XSTART", "XSTOP", "NEW", "META", "RLD" };
  int         ret=NOIR_FAIL,t=sizeof(types)/sizeof(char *);
  file_info  *fi=NULL,*ft=NULL;
  time_t      tn=time(NULL);

  /* use MAX() -- backend may have already set some date from the file-system,
     and that date may be in the future... */
  m->ts=NOIR_MAX(m->ts,tn);
  m->ts2=tn;

  /* are we monitoring the actual item we're interested in already,
     or just something along the path? */
  if(strcmp(m->want,m->have)||
     ((type==NOIR_MONITOR_DEL)&&!strcmp(m->have,tag))) {
    int r2=file_monitor_update(m,type);
    E(1,"updating monitor \"%s\" => \"%s\"\n",m->have,m->want);
    if(r2!=NOIR_SUCC)
      return r2; }

  E(2,"%d-%s  %s  (%s)\n",type,type<t?types[type]:"???",tag,m->have);

/* if(!(fi=file_info_insert(NULL,tag))) { */
  if(!(fi=file_info_fetch(NULL,tag))) {
/*  E(1,"cannot find \"%s\" in model?\n",tag); */
    /* if the event is for an item that doesn't exist in the model, as might
       be the case with monitors that were not created by the model, make up
       a temporary item. this item will not become part of the model. */
    if(!(fi=ft=file_info_new(NULL,tag)))
      return NOIR_FAIL; }

  switch(type) {
    case NOIR_MONITOR_DEL:
      if(tag) {
        noir_sess *ns=noir_sess_get_1st();
        char      *cwd;

        file_monitor_selection_remove(fi,m->session);

        while(ns) {
          if((cwd=noir_sess_get_cwd(ns))&&!strcmp(cwd,tag)) {
            int   but=2;
            char *msg=NULL;

            noir_sess_present(ns);
            esprintf(&msg,"%s\nThe folder that was shown in this window has disappeared.\nYou may close this window,\nor go up to the next folder that still exists.",cwd);
            if(msg) {
/*            but=form_dial(ns,ALERT_QUESTION,ALERT_BUTTONS_CUSTOM,msg,"_Close window|_Go up"); */
              free(msg); }
            if(but==1)          /* close window */
              ns=noir_sess_close(ns); /* don't do this, might still have events pending that reference our monitor structs : ( ### FIXME */
            else if(but==2) {   /* up to deepest existing dir in path */
              file_info *pa=fi;
              do {
                pa=pa->parent;
              } while(pa&&!S_ISDIR(file_exists(pa->tag)));
              noir_sess_focus(ns,pa?pa->tag:"/"); }}
          ns=noir_sess_get_next(ns); }}
      ret=model_evt(fi,MODEL_DEL,FF_NONE);
   /* file_monitor_remove_one(m); */  /* item is gone, drop monitor(?) */
      break;

    case NOIR_MONITOR_NEW:
    case NOIR_MONITOR_CHG:
      ret=file_monitor_update_fi(fi,MODEL_UPD,m->session);
      if(ret!=NOIR_SUCC)
        E(0,"item \"%s\" has gone; discarding event. (%d)\n",tag,ret);  /* events came in wrong order. we're getting info on files in a dir that a previous event deleted from the store. */
#if 0
      else {
        if(fi&&fi->intrinsic&&S_ISDIR(fi->intrinsic->mode)) {
          E(0,"reload/maybe from _CHG (%s | %s)\n",tag,m->have);
          file_monitor_reload_maybe(m,tag); }
      }
#endif
      break;

    case NOIR_MONITOR_META:
      ret=NOIR_IGNORED;
      break;

    case NOIR_MONITOR_RLD:
      E(3,"reload/maybe from _RLD (%s | %s)\n",tag,m->have);
      ret=file_monitor_reload_maybe(m,tag);
      break;

    default:
      E(0,"unhandled event-type %d for \"%s\" in \"%s\"|\"%s\"...\n",type,!strcmp(tag,m->want)?".":tag,m->want,!strcmp(m->want,m->have)?"...":m->have);
      ret=NOIR_IGNORED;
      break; }

  file_info_unref(ft);

  return ret; }



/*****************************************************************************/
