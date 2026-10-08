/** @file  evidence.c
 *  @brief Evidence. GTK2 file-manager. by Azundris. GPL applies.
 */

#include "config.h"

#include "evidence_private.h"  /* GC_INTERVAL_MAIN */
#include <stdlib.h>         /* getenv() */
#include <string.h>
#include <stdio.h>
#include <locale.h>
#ifdef HAVE_BTRACE
#  include <btrace.h>
#endif
#ifdef HAVE_VALGRIND
#  include <valgrind/valgrind.h>
#endif



#include "gui/common/menu.h"          /* menu_root_context(), menu_lib_gc() */



#include "gui/gui.h"                  /* gui_init(), gui_start(), gui_exit() */

#include "util.h"                     /* timeout_add(), now(), esprintf() */
#include "setup.h"                    /* paths_init(),     paths_exit() */
#include "properties.h"               /* prop_bag_new(),   prop_bag_dst() */
#include "ipc_main.h"                 /* ipc_init(),       ipc_exit() */
#include "model.h"                    /* model_init(),     model_exit()  */
#include "search.h"                   /* search_init(),    search_exit() */
#include "mounts.h"                   /* mounts_init(),    mounts_exit() */
#include "thumbnailer/thumbnail.h"    /* thumbnail_init(), thumbnail_exit() */
#include "gui/common/options.h"       /* view_name[],      parse_options() */






q_head           *queue=NULL;

char             *home=NULL,
                 *temp=NULL,
                 *appName=PACKAGE;  /* FAM wants this spelling */
int               debug=0;

pthread_t         main_thread;



noir_prop_bag    *props_global=NULL;



/*****************************************************************************/



/**
 * marshal garbage collections.
 * @param data - reserved
 */
static int marshal_gc(void *data) {
  static int  state=0;

  E(4,"see whether there are unused thumbnailers...\n");
  thumbnail_lib_gc();

  switch(++state) {
  case 1:
    E(4,"see whether there are unused meta-data providers...\n");
    meta_lib_gc();
    break;

  case 2:
    E(4,"see whether there are unused menu-action providers...\n");
    menu_lib_gc();
    break;

  default:
    state=0; }

  return TRUE; }  /* TRUE == keep handler */



/*****************************************************************************/



/**
 * is this the first time the user starts this version?
 * @return TRUE or FALSE
 */
int new_features_p() {
  char *vf=file_user_setup("version");
  int   updated=FALSE;

  if(!vf)
    updated=TRUE;
  else {
    int  l;
    char buf[PATH_MAX+1];
    
    if((l=readlink(vf,buf,PATH_MAX))<0)
      updated=TRUE;
    else {
      buf[l]='\0';
      E(1,"last used version \"%s\", this is \"%s\".\n",buf,VERSION);
      if(strcmp(buf,VERSION))
        updated=TRUE; }

    if(updated) {
      unlink(vf);
      symlink(VERSION,vf); }}

  if(vf)
    free(vf);
  return updated; }



/**
 * create an instance of evidence, and run it : )
 */
int main(int argc,char *argv[]) {
  char           *dir,*ctype;
  action_context *sel=NULL,*dirs=NULL;
  char            env_dflt_locale[]="LC_CTYPE=de_DE.UTF-8";
  noir_sess      *ns;
  void           *marshal_gc_handle;
  static char     cwd[PATH_MAX+1];



#ifdef HAVE_BTRACE
  if(
#  ifdef HAVE_VALGRIND
     !(RUNNING_ON_VALGRIND)
#  else
     1
#  endif
    ) {
    btrace();
    btrace_level=12; }
  else
    btrace_level=0;
#endif

  if(!(props_global=prop_bag_new())) {
    E(0,"out of memory...\n");
    return 1; }



  /***************************************************************************/
  /* init toolkit */


  if(!setlocale(LC_ALL,""))
    E(0,"setlocale() failed!?\n");

  if(strcasecmp(elocale(),"UTF-8")) {
    char *p=strrchr(env_dflt_locale,'.');
    if(p)
      *p='\0'; }

  if(!(ctype=getenv("LC_ALL"))||!strlen(ctype))
    ctype=getenv("LC_CTYPE");
  if(!ctype||!strlen(ctype)||!strcmp(ctype,"C")) {
    char *lang,*a=NULL;
    if((lang=getenv("LANG"))&&strlen(lang)&&strcmp(lang,"C"))
      esprintf(&a,"LC_CTYPE=%s",lang);
    if(!a) {
      E(0,"LC_CTYPE is %s, falling back to \"%s\"...\n",ctype?ctype:"not set",env_dflt_locale);
      putenv(env_dflt_locale); }
    else {
      E(0,"LC_CTYPE is %s, falling back to LANG (\"%s\")...\n",ctype?ctype:"not set",lang);
      putenv(a); }}



  main_thread=pthread_self();



  /***************************************************************************/
  /* options */

  home=getenv("HOME");
  if((dir=getenv("PWD"))&&(strlen(dir)<PATH_MAX))
    strcpy(cwd,dir);  /* avoid realpath() where possible */
  else
    getcwd(cwd,PATH_MAX);

  paths_init(cwd,argv[0]);

  if(prefix)
    setenv("PREFIX",prefix,1);



  if(!(ns=gui_init(argc,argv))) {  /* let GUI remove special args */
    E(0,"failed to initialize GUI...\n");
    return 1; }



  noir_sess_set_cwd(ns,cwd);
  parse_options(ns,argc,argv,&dirs,&sel);
  if(dirs)
    dir=(char *)action_file_get_tag(action_context_get_selection(dirs));
  if(dir)
    noir_sess_set_cwd(ns,dir);

  if(debug>=0)
    print_version();

  if(new_features_p())
    E(0,"\n\n -- UPDATED! -- \n\n");



  /***************************************************************************/
  /* tmpdir */

  srandom(now(0));

  { char *t=NULL;
    esprintf(&t,"/tmp/evidence-%s-XXXXXX",get_username(geteuid()));
    if(!t) {
      E(0,"out of memory!?\n");
      exit(1); }
    if((temp=mkdtemp(t)))
      E(1,"tmpdir is \"%s\"...\n",temp);
    else {
      perror("tmpdir-creation failed");
      free(t); }}



  /***************************************************************************/
  /* init evidence */

  if(!(queue=model_init())) {
    E(0,"fatal -- could not initialize model!?\n");
    goto no_model; }

  ipc_init(); /* do this before file_init() so the latter can override some signal()s */
  file_init(queue,"lib/evidence/backend/fsxs.so");
  meta_init();
  menu_init(ns);
  search_init();

  if(debug>1) {
    noir_prop *e;
    const char *k="thumbnail-protected-mode";
    if((e=prop_bag_prop_get(props_global,k))) {
      char *v,*d;
      v=prop_get_as_string(e);
      d=prop_desc(e);
      E(0,"pre  \"%s\" %s  %s\n",k,v?v:"<null>",d?d:"<null>");
      if(v) free(v);
      if(d) free(d); }
    else
      E(0,"pre  no prop \"%s\"\n",k); }

  thumbnail_init(path_setup);

  if(debug>1) {
    noir_prop *e;
    const char *k="thumbnail-protected-mode";
    if((e=prop_bag_prop_get(props_global,k))) {
      char *v,*d;
      v=prop_get_as_string(e);
      d=prop_desc(e);
      E(0,"post \"%s\" %s  %s\n",k,v?v:"<null>",d?d:"<null>");
      if(v) free(v);
      if(d) free(d); }
    else
      E(0,"post no prop \"%s\"\n",k); }

  marshal_gc_handle=timeout_add(GC_INTERVAL_MAIN,marshal_gc,NULL);
  mounts_init(queue);
/* file_monitor_add_with_cb(ns,NOIR_MONITOR_FILE,"/home/azundris/m1/m2/m3"); */



  if(gui_start(ns)!=NOIR_SUCC)
    E(0,"failed to run GUI...\n");



  /***************************************************************************/
  /* exit evidence */

  ipc_exit();
  search_exit();
  mounts_exit();
  model_exit(queue);

 no_model:
  menu_exit(ns);
  thumbnail_exit();
  file_exit();
  meta_exit();

  gui_exit();

  unsetenv("PREFIX");

  paths_exit();

  prop_bag_dst(props_global);

  return 0; }
