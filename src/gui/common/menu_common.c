/** @file  menu_common.c
 *  @brief implements the widget-library independent part of the context-menus.
 */



#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>          /* chdir() */

#include "session.h"
#include "action.h"          /* action_file_*(), action_context_*() */
#include "mime.h"            /* MIME_* */
#include "meta.h"            /* meta_in_group(), meta_add(), meta_del_all() */
#include "menu.h"            /* struct menu_action */
#include "menu_private.h"    /* struct selection_action */
#include "alert.h"           /* form_alert() */
#include "file.h"            /* file_exists(), file_run(),
                                file_get_meta_only(), file_get_mime() */
#include "setup.h"           /* get_setup_file(), get_path_file() */
#include "util.h"            /* parse_line(), replace_variables() */
#include "file_lcl.h"        /* file_if_absolute(), file_dirname() */



menu_action *ma=NULL;



/**
 * throw away all info on context-menu actions. close SOs.
 *
 * called from info_exit().
 * @param ml  anchor of list of menu-actions
 */
static menu_action *menu_del_all(menu_action **ml) {
  menu_action *m,*n;

  if(!ml)
    return NULL;

  m=*ml;
  while(m) {
    n=m;
    m=m->next;
    free(n->title);
    free(n->file);
    free(n->mime);
    if(n->lib)
      dlclose(n->lib);
    free(n); }

  return *ml=NULL; }



/**
 * garbage collection: close SOs that have idled out.
 */
int menu_lib_gc(void) {
  time_t       t=time(NULL);
  menu_action *m=ma;
  int          c=0;

  while(m) {
    if(m->lib&&(m->ts<=t)) {
      dlclose(m->lib);
      m->lib=NULL; }
    m=m->next; }

  return c; }



/**
 * add context-menu action.
 * @param ns     session
 * @param ml     anchor of list of menu-actions
 * @param title  the action's label in the menu
 * @param mime   the MIME-type the handler can be applied to
 * @param file   the action (name of shared library, or template for executable)
 */
static int menu_add_handler(noir_sess *ns,menu_action **ml,char *title,char *mime,char *file) {
  char        *p,*q,*r;
  menu_action *m,*x;
  int          exists;

  if(!ml)
    return NOIR_FAIL;

  if(!title||!*title||!file||!*file||!mime||!*mime)
    return NOIR_NODATA;

  if(!(p=q=strdup(file)))
    return NOIR_OOM;
  if(*p=='\"')
    q++;
  r=q;
  while(*r&&(*r!=' ')&&(*r!='\"')) {
    if((*r=='\\')&&r[1])
      memmove(r,r+1,strlen(r));
    r++; }
  *r='\0';

  if(file_if_absolute(q))
    ;
  else if(file_if_nested(q)) { /* relative path (in setup-dir) */
    if((q=get_setup_file(q))) {
      free(p);
      p=q; }}
  else if(ns) {                 /* no path, find in $PATH */
    if((q=get_path_file(noir_sess_get_cwd(ns),q))) {
      free(p);
      p=q; }}

  exists=q?file_exists(q):0;
  free(p);

  if(!((exists&S_IFREG)&&(exists&(S_IXUSR|S_IXGRP|S_IXOTH)))) {
    E(2,"\"%s\" (for \"%s\") not found in $PATH, ignoring...\n",file,title);
    return NOIR_NOTFOUND; }

  if(!(m=malloc(sizeof(menu_action))))
    return NOIR_OOM;
  memset(m,0,sizeof(m));

  if(!(m->mime=strdup(mime)))
    goto free_m;

  if(!(m->title=strdup(title)))
    goto free_mime;

  if(!(m->file=strdup(file)))
    goto free_title;

  m->lib =NULL;
  m->ts  =0;
  m->used=0;
  m->next=NULL;

  if((p=strchr(m->mime,'*')))
    *p='\0';

  /* put in order */
  x=*ml;
  while(x&&strcmp(x->mime,m->mime))
    x=x->next;
  if(x) {
    m->next=x->next;
    x->next=m; }
  else {
    m->next=*ml;
    *ml=m; }

  return NOIR_SUCC;

  free_title:
  free(m->title);
  free_mime:
  free(m->mime);
  free_m:
  free(m);

  return NOIR_OOM; }



/**
 * find anchor of menu-actions
 * @return first in list of menu-actions
 */
menu_action *menu_action_get_anchor(void) {
  return ma; }



/**
 * find first action for MIME-type 'mime'.
 * @param ml    anchor of list of menu-actions
 * @param mime  the MIME-type we're interested in
 */
menu_action *menu_action_by_mime_first(menu_action **ml,char *mime) {
  menu_action *m;

  if(!ml)
    return NULL;

  m=*ml;

  while(m&&strcmp(m->mime,mime))
    m=m->next;
  return *ml=m; }



/**
 * find next action for MIME-type 'mime'.
 * @param ml    anchor of list of menu-actions
 * @param mime  the MIME-type we're interested in
 */
menu_action *menu_action_by_mime_next(menu_action **ml,char *mime) {
  menu_action *m;

  if(!ml)
    return NULL;

  m=(*ml)->next;

  return *ml=(m&&!strcmp(m->mime,mime))?m:NULL; }






/******************************************************************************/
/* context-menu (for single file) */



/**
 * run context action 'm' on file described in 'r'.
 * @return NOIR_SUCC or an error-code.
 * @param serial  the session's serial
 * @param m       the menu-action to apply to the file
 * @param r       the meta-data for the file to be acted on
 * @param ac      the action-context (or NULL)
 */
int menu_action_by_entry(long serial,menu_action *m,noir_md *r,action_context *ac) {
  noir_meta       *x;
  action_context  *a2=NULL;
  char            *cwd=NULL;
  int              ret;

  if(!m||!r) {
    E(0,"no data!?\n");
    return NOIR_FAIL; }

  if((x=meta_in_group(r,"Tag",MY_NAME))) {
    char *file=(char *)meta_get_val(x);

    if(!strstr(m->file,".so")) {
      noir_meta *mime=meta_in_group(r,"MIME",MY_NAME);
      if(action_context_get_cwd(ac))
        chdir(action_context_get_cwd(ac));
      return file_run(NULL,m->file,file,mime?(char *)meta_get_val(mime):""); }

    else if(!m->lib) {
      m->used=0;
      if(!(m->lib=dlopen(m->file,RTLD_NOW))) {
        const char *err;
        if((err=dlerror()))
          E(0,"failed to load \"%s\": %s\n",m->file,err); }}

    if(m->lib) {
      int (*efun)(long serial,char *filename,noir_md *md,action_context *ac);
      m->ts=GC_UPDATE_TIMESTAMP(GC_INTERVAL_MENU);
      m->used++;
      efun=dlsym(m->lib,"efun");

      if(!ac) {  /* if there is no action context, make one up! */
        action_file *af;
        ret=NOIR_OOM;
        if(!(cwd=file_dirname(file_if_absolute(file))))
          goto done;
        if(!(a2=ac=action_context_new(serial,cwd))||
           !(af=action_file_new(NULL,file)))
          goto done;
	action_context_set_single(a2,af); }

      if(action_context_get_cwd(ac))
        chdir(action_context_get_cwd(ac));

      ret=efun(serial,file,r,ac); }

    else
      return NOIR_NOEXEC; }

  else {
    E(0,"cannot determine file-name!?\n");
    return NOIR_NOTFOUND; }

 done:
  if(a2)
    action_context_dst(a2);
  if(cwd)
    free(cwd);
  return ret; }



/**
 * run context the action for 'mime' on  a single file given in 'file'.
 * @return NOIR_SUCC or an error-code.
 * @param serial  the session's serial
 * @param mime    the MIME-type of the file, or NULL to get from meta-data
 * @param file    the file to be acted on
 * @param ac      the action-context, or NULL
 */
static int menu_action_by_mime_one(long serial,char *mime,const char *file,action_context *ac) {
  file_info      *fi=NULL;
  menu_action    *m=ma;
  noir_md        *r=NULL;
  int             ret=NOIR_FAIL;

  if(file) {
    if((fi=file_info_fetch(NULL,file)))
      ret=file_get_meta_only(NULL,file,&r);
    if(ret!=NOIR_SUCC)
      goto done; }

  if(!mime) {
    noir_meta *mm;
    if((mm=meta_in_group(r,"MIME",MY_NAME)))
      mime=(char *)meta_get_val(mm);
    if(!mime) {
      ret=NOIR_NOTFOUND;
      goto done; }}

  menu_action_by_mime_first(&m,mime);

  ret=m?menu_action_by_entry(serial,m,r,ac):NOIR_NOTFOUND;

 done:
  meta_dst(&r);
  return ret; }



/**
 * run context the action for 'mime' on a file or a selection
 * @return NOIR_SUCC or an error-code.
 * @param serial  the session's serial
 * @param mime    the MIME-type of the file, or NULL to get from meta-data
 * @param file    the file to be acted on
 * @param ac      the action-context to get the selection from, or NULL
 */
int menu_action_by_mime(long serial,char *mime,char *file,action_context *ac) {
  action_file    *af=NULL;
  int             ret=NOIR_SUCC;
  const char     *tag=NULL;

  /* make local context */
  if(!ac||!(ac=action_context_clone(ac)))
    return menu_action_by_mime_one(serial,mime,file,NULL);

  if((af=action_file_new(NULL,file)))
    action_context_set_single(ac,af);

  action_context_add_flags(ac,AF_SELECTION); /* we have more than one file, set "selection" flag */
  action_context_clr_flags(ac,AF_LAST);      /* we have more than one file, clear "last" flag */

#ifdef WITH_DOT_DIR
  /* expand dot-dir to its contents if present */
  af=action_context_get_selection(ac);
  while(af) {
    if((tag=action_file_get_tag(af))&&file_if_dotdir(tag)) {
      file_info *fi=file_info_fetch(NULL,tag);
#if 0
      char      *t2,*p;  /* drop unneeded /./ */
#endif

      if(!fi)
        form_alert(NULL,"could not find \"%s\" in model!?",tag);
      else if((fi=file_info_get_1st_child(fi))) {
        while(fi) {
#if 0
          if((tag=file_info_get_tag(fi))&&(t2=strdup(tag))) {
            if((p=strstr(t2,"/./")))
              memmove(p,p+2,strlen(p)-1);
            action_file_new(ac,t2);
            free(t2); }
#else
          if((tag=file_info_get_tag(fi)))
            action_file_new(ac,tag);
#endif
          fi=file_info_get_next(fi); }}
      af=action_context_remove_and_dst_file(ac,af); }
    else
      af=action_file_get_next(af); }
#endif

  af=action_context_get_selection(ac);
  while(af) {
    if(!(action_context_get_flags(ac)&AF_CANCEL_ALL)) {
      if(!action_file_get_next(af))            /* if it's the last item, flag it */
        action_context_set_flags(ac,action_context_get_flags(ac)|AF_LAST);
      ret=menu_action_by_mime_one(serial,mime,action_file_get_tag(af),ac); }
    if((action_context_get_flags(ac)&AF_LAST)||!action_context_get_selection(ac))  /* action may steal selection */
      af=NULL;
    else
      af=action_context_remove_and_dst_file(ac,af); }

  /* dest local context */
  action_context_dst(ac);

  return ret; }



/******************************************************************************/



/**
 * add action 'm' to list of actions available for current selection ('e')
 * unless already present.
 * @param ac  the action-context
 * @param e   list of actions available for current selection
 * @param m   action to add to the list
 * @param mm  MIME match quality (full, partial, any match)
 */
static int menu_selection_action_add(action_context *ac,selection_action **e,menu_action *m,mime_match mm) {
  selection_action *r=*e,*p=NULL;

  while(r) {
    p=r;
    if(r->menu==m)
      return NOIR_IGNORED;
    r=r->next; }

  if(!(r=malloc(sizeof(struct _selection_action))))
    return NOIR_OOM;

  r->menu=m;
  r->ac=ac;
  r->section=mm;
  r->next=NULL;
  if(p)
    p->next=r;
  else
    *e=r;

  return NOIR_SUCC; }



/**
 * get MIME-type of file.  get actions for that MIME-type.  add to list of
 * available actions for selection, if not already present.
 * @param ac    the action-context
 * @param file  a file in the selection
 * @param e     list of available actions for current selection.
 */
int menu_selection_file_add(action_context *ac,const char *file,selection_action **e) {
  menu_action     *m;
  const char      *mi=file_get_mime(file);
  char            *p,*mime;
  int              ret=0,internal=0;
  action_file     *af;

  if(!file||!mi)
    return NOIR_FAIL;

  if(!(af=action_file_new(ac,file)))
    return NOIR_OOM;

  if(!(mime=strdup(mi))) {
    action_file_dst(af);
    return NOIR_OOM; }

  if(!strcmp(mime,"unknown"))
    *mime='\0';

  /* handlers for this MIME-type: text/plain */
  if(strlen(mime)) {
    m=ma;
    menu_action_by_mime_first(&m,mime);
    while(m) {
      ret++;
      menu_selection_action_add(ac,e,m,MIME_FULL);
      menu_action_by_mime_next(&m,mime); }

    /* handlers for this MIME-group: text/ */
    if((p=strchr(mime,'/'))) {
      *(++p)='\0';
      if(strcmp(mime,MIME_INTL)) {
        m=ma;
        menu_action_by_mime_first(&m,mime);
        while(m) {
          ret++;
          menu_selection_action_add(ac,e,m,MIME_PARTIAL);
          menu_action_by_mime_next(&m,mime); }}
      else
        internal=1; }
    *mime='\0'; }

  /* wild-card handlers */
  if(!internal) {
    m=ma;
    menu_action_by_mime_first(&m,mime);
    while(m) {
      ret++;
      menu_selection_action_add(ac,e,m,MIME_ANY);
      menu_action_by_mime_next(&m,mime); }}

  free(mime);

  return ret; }






/******************************************************************************/



/**
 * top-level function that will open a context-menu for the given file.
 * the menu will also contain possible actions for the session's current
 * selection if there is one.
 * @param ns    the session
 * @param cwd   the working-dir
 * @param file  the file the context-menu was opened for
 */

int menu_file_context(noir_sess *ns,const char *cwd,const char *file) {
  noir_md        *r=NULL;
  action_context *ac,*ss;
  action_file    *af;

  E(2,"menu for \"%s\" in \"%s\"...\n",file?file:"",cwd?cwd:"");

  if(!(ac=action_context_new(noir_sess_get_serial(ns),cwd)))
    return NOIR_OOM;
  if(!(af=action_file_new(NULL,file))) {
    action_context_dst(ac);
    return NOIR_OOM; }
  action_context_set_single(ac,af);

  /* if there is a selection, and the file clicked on is in it, but
     isn't the whole selection...
     make a combined (for all files in selection/for this file only) menu. */
  if((ss=noir_sess_get_selection(ns))&&
     action_context_selection_contains(ss,file)&&
     (action_context_get_seletion_size(ss)!=1)) {
    if(file_get_meta_only(ns,file,&r)==NOIR_SUCC) {
      action_file_set_meta(af,r);
      return menu_run(ac,menu_selection_create(ac)); }}

  /* no selection; make menu for clicked file. */
  else if(file_get_meta_only(ns,file,&r)==NOIR_SUCC) {
    action_file_set_meta(af,r);
    return menu_run(ac,NULL); }

  action_context_dst(ac);

  form_alert(ns,"Could not get meta-data for file \"%s\"...",file);
  return NOIR_FAIL; }






/******************************************************************************/



/**
 * create a title for a sub-menu
 * @return a malloc()'d string the caller will have to free, or NULL
 * @param mime  the MIME-type  ("text/html", "text/", "")
 */
char *menu_submenu_title_from_mime_type(const char *mime) {
  size_t      l;
  char       *ret,*p;
  const char *m;

  if(!mime||!(l=strlen(mime)))          /* catch-all type "" */
    return strdup("General");

  if(!strcmp(mime,MIME_INTL_MB3))       /* internal (context-menu) */
    return strdup("Current directory");

  if((ret=malloc(l+2))) {
    if((p=strchr(mime,MIME_DELIMITER)))
      p++;
    if(!p||!*p) {                       /* major only, "image/" */
      strcpy(ret,mime);
      if((p=strchr(ret,MIME_DELIMITER)))
        *p='\0';
      *ret=toupper(*ret); }
    else {                              /* major and minor, "image/png" */
      char *q;
      if(!strncmp(p,"x-",2))
        p+=2;
#if 0
      for(q=ret;*p;*(q++)=toupper(*(p++)));  /* uppercase first word */
#else
      q=ret;
      *(q++)=toupper(*(p++));           /* capitalize first word */
      for(;*p;*(q++)=*(p++));
#endif
      if(strncmp(mime,"application/",12)) {
        *(q++)=' ';
        for(m=mime;(*m&&(*m!=MIME_DELIMITER));*(q++)=*(m++)); }
      *q='\0'; }}
  return ret; }






/******************************************************************************/
/* init/exit */



/**
 * initialize menu sub-system.
 * @param ns  session
 */
int menu_init(noir_sess *ns) {
  char *f;

  if((f=get_setup_file(MENU_HANDLERS))) {
    FILE *fh;
    char  l[MIME_MAX_LEN];
    char *p,*p3,*p4;
    int   res;

    if((fh=fopen(f,"r"))) {
      E(2,"file-actions in right-click menu:\n");
      while(fgets(l,MIME_MAX_LEN,fh)) {
        if(util_parse_line(l,&p,&p3,&p4,NULL)==3) {
          E(3,"|%s| \t |%s| \t |%s|\n",p,p3,p4);
          if((p4=util_replace_variables(p4,NULL,NULL))) {
            res=menu_add_handler(ns,&ma,p,p3,p4);
            free(p4); }}}
      fclose(fh); }
    else
      E(0,"could not read actions (\"%s\")...\n",f);
    free(f); }
  else
    E(0,"could not read actions (\"" MENU_HANDLERS "\")...\n");

  return NOIR_SUCC; }



/**
 * deinitialize menu sub-system
 * @param ns  session
 */
int menu_exit(noir_sess *ns) {
  menu_del_all(&ma);
  return NOIR_SUCC; }






/******************************************************************************/
