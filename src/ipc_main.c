/** @file  ipc_main.c
 *  @brief common functions for IPC-plugins.  plugin-loader.
 */

#include <signal.h>
#include <dlfcn.h>
#include <stdio.h>
#include <glob.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>     /* malloc(), free() */

#include "gui/efl/common/theme.h"

#include "debug.h"
#include "errors.h"
#include "util.h"
#include "ipc.h"
#include "model.h"
#include "file.h"
#include "file_lcl.h"



#ifndef TRUE
#  define FALSE 0
#  define TRUE (!FALSE)
#endif



typedef struct _ipc {
  void *lib;
  void *data;
  int (*ipc_init)(void **data);
  int (*ipc_exit)(void **data);
  int (*ipc_poll)(void **data);
  struct _ipc *next;
} ipc;



static ipc   *ipc_modules=NULL;
static void  *ipc_timer=NULL;



/*****************************************************************************/
/* INTERFACE FOR IPC MODULES */
/*****************************/



/**
 * give all props for session identified by 'serial'.
 * @param serial  session identifier
 */
char *ipc_prop_list(const long serial) {
  noir_theme    *theme=noir_sess_get_theme(noir_sess_get_by_serial(serial));
  noir_prop_bag *b=theme?theme->data:NULL;
  noir_prop     *e=prop_bag_prop_get_1st(b);
  estring       *s=estring_new(8192);
  char          *d;
  int            f=0;

  while(e) {
    if(e&&(d=prop_desc(e))) {
      estring_appendf(s,"%s%s",f?"\n":"",d);
      free(d);
      f=1; }
    e=prop_get_next(e); }

  return estring_disown(s); }



/**
 * describe a specific property in a given session.
 * @param serial  session identifier
 * @param key     property-name
 */
char *ipc_prop_desc(const long serial,const char *key) {
  noir_theme    *theme=noir_sess_get_theme(noir_sess_get_by_serial(serial));
  noir_prop     *e=prop_bag_prop_get(theme->data,key);
  char          *ret;

  if(e&&(ret=prop_desc(e)))
    return ret;

  return strdup("<undefined>"); }



/**
 * get the value of a specific property in a given session.
 * @param serial  session identifier
 * @param key     property-name
 */
char *ipc_prop_get(const long serial,const char *key) {
  char          *v=NULL;
  noir_theme    *theme=noir_sess_get_theme(noir_sess_get_by_serial(serial));
  noir_prop     *e=prop_bag_prop_get(theme->data,key);

  if(!e)
    return strdup("<no_such_property>");
  if((v=prop_get_as_string(e))) {
    char *ret=NULL;
    esprintf(&ret,"%s:%s=%s",prop_get_key(e),prop_get_type_as_string(e),v);
    E(1,"|%s|\n",ret?ret:"");
    free(v);
    return ret?ret:strdup("<failure>"); }
  return strdup("<undefined>"); }



/**
 * set the value of a specific property in a given session.
 * @param serial  session identifier
 * @param key     property-name
 * @param val     the new value for the property
 */
int ipc_prop_set(const long serial,const char *key,const char *val) {
#ifdef HAVE_EVAS2
  int ret;
  noir_theme *theme=noir_sess_get_theme(noir_sess_get_by_serial(serial));
  ret=prop_bag_prop_set(theme->data,key,(char *)val);
  E(1,"set(%s->%s,\"%s\") => %d\n",theme->name,key,val,ret);
  return ret;
#else
  return NOIR_NOTSUPP;
#endif
}






/*****************************************************************************/



/**
 * get meta-data for a given file.
 * @param file  the file to get info on
 */
char *ipc_cmd_info(const char *file) {
  noir_sess   *ns=noir_sess_get_by_serial(0);
  noir_md     *md=NULL;
  int          ret;
  char        *f=NULL,*r;
  const char  *q;

  if(!file||!*file||!ns)
    return strdup("<must_provide_file>");

  if((q=file_if_local(file))) {
    if(file_if_absolute(q))
      f=strdup(q);
    else
      f=file_compose_path(noir_sess_get_cwd(ns),q,NULL); }
  else
    f=strdup(file);

  r=NULL;
  if((ret=file_get_meta_only(ns,f,&md))==NOIR_SUCC) {
    if(!(r=meta_to_string(md)))
      r=strdup("<internal_error>"); }
  else
    esprintf(&r,"<error_%d>",ret);

  meta_dst(&md);
  if(f)
    free(f);

  return r; }



/**
 * list a given directory.
 * @param file  the directory to list
 */
char *ipc_cmd_ls(const char *file) {
  noir_sess   *ns=noir_sess_get_by_serial(0);
  char        *f,*r=NULL;
  file_info   *fi;

  if(!file||!*file||!ns)
    return strdup("<must_provide_dir>");

  if((file=file_if_local(file))) {
    if(file_if_absolute(file))
      f=strdup(file);
    else
      f=file_compose_path(noir_sess_get_cwd(ns),file,NULL); }
  else
    f=strdup(file);

  if((fi=file_info_fetch(NULL,f))) {
    if(!(fi=file_info_get_1st_child(fi)))
      r=strdup("<file_not_a_directory>");
    else {
      estring       *s=estring_new(8192);
      int            f=0;

      while(fi) {
        estring_appendf(s,"%s%s",f?"\n":"",file_info_get_name(fi));
        f=1;
        fi=file_info_get_next(fi); }

      r=estring_disown(s); }}
  else
    r=strdup("<file_not_found>");

  if(f)
    free(f);

  return r; }






/*****************************************************************************/



/**
 * list all windows/sessions.
 */
char *ipc_win_list(void) {
  noir_sess   *ns=noir_sess_get_1st();
  estring     *s=estring_new(8192);
  int          f=0;

  if(!ns)
    return strdup("<no_windows_open>");

  while(ns) {
    estring_appendf(s,"%s%d: %s",f?"\n":"",noir_sess_get_serial(ns),noir_sess_get_cwd(ns));
    f=1;
    ns=noir_sess_get_next(ns); }

  return estring_disown(s); }



/**
 * open a new window showing a specified folder.
 * @param cwd  the folder to show
 */
int ipc_win_new(const char *cwd) {
  noir_sess *ns=noir_sess_get_by_serial(0);
  return noir_sess_create(SESSION_COPY,ns,cwd); }



/**
 * get the current selection of a given window.
 * @param serial  a session-identifier
 */
char *ipc_win_sel_get(const long serial) {
  noir_sess *ns=noir_sess_get_by_serial(serial);
  if(!ns)
    return "<no such window>";
  else {
    char *sel=action_context_files_to_quoted_string(noir_sess_get_selection(ns));
    if(sel)
      return sel;
    return strdup(""); }}



/**
 * set the current selection of a given window.
 * @param serial  session identifier
 * @param sel     the selection to set
 */
int ipc_win_sel_set(const long serial,const char *sel) {
  noir_sess *ns=noir_sess_get_by_serial(serial);
/* return noir_sess_set_selection(ns,(char *)sel); */
  return NOIR_NOTSUPP; }



/**
 * get the current working directory of a given window.
 * @param serial  session-identifier
 */
char *ipc_win_cwd_get(const long serial) {
  noir_sess *ns=noir_sess_get_by_serial(serial);
  char      *cwd=noir_sess_get_cwd(ns);
  return strdup(cwd?cwd:"<no such window>"); }



/**
 * set the current working directory of a given window.
 * @param serial  session identifier
 * @param cwd     the folder to show
 */
int ipc_win_cwd_set(const long serial,const char *cwd) {
  noir_sess *ns=noir_sess_get_by_serial(serial);
  return noir_sess_focus(ns,(char *)cwd); }



/**
 * find the (first) session showing a given working directory.
 * @param cwd  the folder to show
 */
long ipc_win_cwd_find(const char *cwd) {
  noir_sess *ns=noir_sess_get_by_cwd(cwd);
  return ns?noir_sess_get_serial(ns):-1; }






/*****************************************************************************/
/* INTERFACE FOR EVIDENCE */
/**************************/



/**
 * check all IPC-handlers for new data.
 * @param data  list of IPC handlers
 */
static int ipc_poll(void *data) {
  ipc *m=(ipc *)data;

  while(m) {
    m->ipc_poll(&m->data);
    m=m->next; }

  return TRUE; }



/**
 * deinitialize IPC sub-system.
 */
int ipc_exit(void) {
  ipc *m;
  if(ipc_timer)
    timeout_remove(&ipc_timer);
  while(ipc_modules) {
    m=ipc_modules;
    ipc_modules=ipc_modules->next;
    m->ipc_exit(&m->data);
    free(m); }
  return NOIR_IGNORED; }



/**
 * initialize IPC sub-system.
 */
int ipc_init(void) {
  char         buf[PATH_MAX];
  glob_t       globbuf;
  int          ret;
  unsigned int c;
  ipc         *nm=NULL;

  if(((ret=snprintf(buf,PATH_MAX,PACKAGE_PREFIX "/lib/evidence/ipc/ipc_*.so"))<0)||
     (ret>=PATH_MAX))
    return NOIR_FAIL;

  glob(buf,0,NULL,&globbuf);
  if(!globbuf.gl_pathc)
    return NOIR_NOTFOUND;

  for(c=0;c<globbuf.gl_pathc;c++) {
    if(!(nm=malloc(sizeof(ipc)))) {
      ret=NOIR_OOM;
      goto done; }
    memset(nm,0,sizeof(ipc));

    E(1,"checking \"%s\"...\n",globbuf.gl_pathv[c]);
    ret=dlmulti("IPC-plugin",globbuf.gl_pathv[c],RTLD_NOW,&nm->lib,
                "!ipc_module_init !ipc_module_exit !ipc_module_poll",
                &nm->ipc_init,&nm->ipc_exit,&nm->ipc_poll);
    if(ret==NOIR_NODATA)
      E(0,"could not load \"%s\": %s...\n",globbuf.gl_pathv[c],dlerror());
    else if(ret==NOIR_SUCC) {
      if((ret=nm->ipc_init(&nm->data))!=NOIR_SUCC)
        E(0,"could not initialize \"%s\": %d\n",globbuf.gl_pathv[c],ret);
      else {
        const char *p=globbuf.gl_pathv[c];
        if(debug<2)
          p=file_name(p);
        E(1,"adding \"%s\"...\n",p);
        nm->next=ipc_modules;
        ipc_modules=nm; }}
    if(ret!=NOIR_SUCC)
      free(nm); }

 done:
  globfree(&globbuf);

  if(ipc_modules) {
    /* ### temporary evilness */
    if((debug>0)||(getenv("USER")&&!strcmp(getenv("USER"),"azundris"))) {
      signal(SIGINT,SIG_DFL);
      signal(SIGSEGV,SIG_DFL); }
    ipc_timer=timeout_add(20,ipc_poll,ipc_modules); }

  return ret; }



/*****************************************************************************/
