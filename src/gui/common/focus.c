/** @file  focus.c
 *  @brief load (where necessary) and display a directory and all directories leading up to it.
 */

#include <stdlib.h>         /* free */
#include <string.h>         /* strstr() et al. */
#include <stdio.h>          /* fprintf() et al. */
#include <sys/stat.h>       /* S_ISDIR() */
#include <errno.h>          /* errno */
#include <unistd.h>         /* chdir() */

#include "session.h"
#include "session_private.h"
#include "model.h"          /* path_get(), CLIENT_PAUSED() */
#include "alert.h"          /* form_alert() */
#include "file.h"           /* file_exists() */
#include "monitors.h"       /* file_monitor_*() */

#include "gui/common/tooltip.h"        /* tooltip_cancel() */

#include "gui/efl/common/theme.h"

#include "file_lcl.h"



/****************************************************************************/



/**
 * check whether the new path exists and consists of directories only.
 * @return NOIR_SUCC if we don't object to newdir becoming ns->cwd.
 * @param ns     - the session we want to change the CWD for
 * @param olddir - the CWD for that session
 * @param newdir - the directory we want to become the new CWD
 */
static inline int focus_is_path_valid(noir_sess *ns,const char *olddir,char *newdir) {
  int         xs;
  char       *e,*p;
  long        err=0;
  file_info  *fi;

#ifndef WITH_DOT_DIR
  while((e=strstr(newdir,"/./")))
    memmove(e,e+2,strlen(e+2)+1);
#endif

  while((e=strstr(newdir,"/../"))) {
    *e='\0';
    if((p=strrchr(newdir,'/')))
      memmove(p,e+3,strlen(e+3)+1);
    else
      memmove(e,e+3,strlen(e+3)+1); }

  if((p=strrchr(newdir,'/'))) {
    if(!strcmp(p,"/..")) {
      *p='\0';
      if((p=strrchr(newdir,'/')))
        *p='\0'; }
#ifndef WITH_DOT_DIR
    else if(!strcmp(p,"/."))
      *p='\0';
#endif
    else if(!p[1]&&(p>newdir))   /* discard trailing / */
      *p='\0'; }

  if((fi=file_info_fetch(NULL,newdir))&&
     S_ISDIR(file_info_get_mode(fi))&&
     (file_info_get_flags(fi)>=0))
    xs=!(file_info_get_flags(fi)&IF_NOUSE);
  else
    xs=(S_ISDIR(file_exists(newdir))&&!chdir(newdir));

  if(!xs) {
    char *e;
    int   et;
    chdir(newdir);
    err=errno;
    do {
      et=xs;
      if((e=strrchr(newdir,'/')))
        *e='\0';
    } while((e>newdir)&&!S_ISDIR(xs=file_exists(newdir)));

    E(0,"(,\"%s\"): last valid dir was \"%s\"...\n",olddir,!strcmp(olddir,newdir)?"<none>":newdir);

    if(e) {
      char *ets=(err==EACCES)?"is inaccessible":(et?"is not a directory":"not found");
      *e='/';
      form_alert(ns,"%s %s!",newdir,ets); }

    chdir(olddir);

    return NOIR_NOTFOUND; }

  return NOIR_SUCC; }



/****************************************************************************/



/**
 * change the current working-directory of a session.
 *
 * called by focus() to do the actual work.
 * @param ns - the session
 * @param tag - the desired new working-directory
 */
static inline int do_cwd(noir_sess *ns,char *tag) {
  file_info   *fi,*fs=NULL;
  const char  *p;

  if(!(p=file_name(tag)))
    return NOIR_FAIL;

#if 0
  if(!strcmp(p,DIR_PARENT)) {
    E(0,"DIR_PARENT: %s\n",tag);
    return NOIR_IGNORED; }
#  ifndef WITH_DOT_DIR
  if(!strcmp(p,DIR_MYSELF)) {
    E(0,"DIR_MYSELF: %s\n",tag);
    return NOIR_IGNORED; }
#  endif
#endif

  if(!(fi=file_info_fetch(NULL,tag))) {
    ns->cd=NULL;
    mode_t m=file_exists(tag);
    if(!m)
      return NOIR_NOFILE;
    if(!S_ISDIR(m))
      return NOIR_TYPEMISMATCH;
    E(2,"dir \"%s\" not found in model, making it up...\n",tag);
    if(!(fs=file_info_new(NULL,tag)))  /* create fi, but don't insert into model */
      return NOIR_OOM;
    fi=fs; }
  else
    ns->cd=fi;

  E(2,"(%s) => %s\n",tag,file_info_get_tag(fi));

  noir_sess_tell_clients(ns,fi,MODEL_CWD,FF_NONE);  /* signal pre-cwd */

  model_path_get(queue,NULL,tag,PG_ENQUEUE_ALL);    /* signal _LAST */
  ns->cd=file_info_fetch(NULL,tag);

  if(noir_sess_get_selection(ns))
    action_context_set_cwd(noir_sess_get_selection(ns),tag);

  noir_sess_tell_clients(ns,fi,MODEL_CWD,FF_NAME);  /* signal post-cwd */

  file_info_unref(fs);

  return NOIR_SUCC; }



/****************************************************************************/



/**
 * check whether change the current working-directory of a session to the
 * given path would succeed.
 *
 * provides the supporting structure to the actual work done in do_cwd().
 * @param ns      the session
 * @param reqdir  the desired new working-directory
 * @param rd      what the new dir would be
 */
int noir_sess_focus_verify(noir_sess *ns,const char *reqdir,char **rd) {
  file_annotation *fa;
  int              ret=NOIR_SUCC;
  char            *newdir=NULL;

  if(rd)
    *rd=NULL;

  if(!reqdir||!*reqdir)
    return NOIR_NODATA;

  /* update cwd */
  if(!file_if_absolute(reqdir)) {                 /* relative path */
    if(!(newdir=file_compose_path(ns->cwd,reqdir,NULL)))
      return NOIR_OOM; }
  else if(!(newdir=strdup(reqdir)))               /* absolute path */
    return NOIR_OOM;

  if((ret=focus_is_path_valid(ns,ns->cwd,newdir))!=NOIR_SUCC)
    goto done;

  if((fa=file_annotation_get(newdir))) {
    int reply=NOIR_SUCC;
    if((fa->lvl&FA_ROOT)&&(geteuid()!=0)) {
      if((reply=form_yes_no(ns,"You are about to enter a folder only relevant to\nthe system-administrator while logged in as a user. Proceed?"))==NOIR_SUCC)
        fa->lvl&=(~FA_ROOT); }
    if(fa->lvl&FA_MEDIA) {
      if((reply=form_yes_no(ns,"You are about to enter a folder that lives on a removable media\n(such as a CD, floppy disk, or USB stick). Proceed?"))==NOIR_SUCC)
        fa->lvl&=(~FA_MEDIA); }
    if(reply!=NOIR_SUCC)
      ret=NOIR_IGNORED; }

 done:
  if((ret==NOIR_SUCC)&&rd)
    *rd=newdir;
  else
    free(newdir);

  return ret; }




/**
 * change the current working-directory of a session.
 *
 * provides the supporting structure to the actual work done in do_cwd().
 * @param ns  the session
 * @param reqdir  the desired new working-directory
 */
int noir_sess_focus(noir_sess *ns,const char *reqdir) {
  int              ret=NOIR_SUCC;
  char            *newdir;

  /* this should only happen when a property-change forces a redraw
     (e.g. "icon-font") while in focus().  the canonical example is
     start-up, but this can also happen if a remote config-tool
     changes a prop. */
  if(ns->sema_focus) {
    E(2,"already in focus(), call ignored\n");
    return NOIR_IGNORED; }

  ns->sema_focus++;

  tooltip_cancel(ns);

  if((ret=noir_sess_focus_verify(ns,reqdir,&newdir))!=NOIR_SUCC)
    goto done;

  E(1,"%s => %s\n",noir_sess_get_cwd(ns)?noir_sess_get_cwd(ns):"",newdir?newdir:"");

  /* save old cwd in pwd etc. */
  if(!noir_sess_get_cwd(ns)||strcmp(newdir,noir_sess_get_cwd(ns))) {
    if(ns->pwd)
      free(ns->pwd);
    ns->pwd=noir_sess_get_cwd(ns); }
  else
    free(ns->cwd);
  ns->cwd=newdir;

  /* do the actual cd */
  if(do_cwd(ns,noir_sess_get_cwd(ns))==NOIR_SUCC) {
    /* set monitors for directories along the path */
    file_info *fi=file_info_fetch(NULL,noir_sess_get_cwd(ns));
/*  file_monitor_remove_all(ns); */  /* check ### */
    while(fi) {
      file_monitor_add(ns,NOIR_MONITOR_DIR,(char *)file_info_get_tag(fi));
      fi=file_info_get_parent(fi); }}

 done:
  ns->sema_focus=0;

  return ret; }



/****************************************************************************/
