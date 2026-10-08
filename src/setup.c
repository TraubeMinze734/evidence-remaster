/** @file  setup.c
 *  @brief find data-files.
 */

#include "evidence.h"

#include <stdlib.h>      /* getenv() */
#include <string.h>
#include <unistd.h>      /* access() */
#include <sys/stat.h>    /* stat */
#include <ctype.h>       /* isspace() */
#include <pwd.h>         /* getpwuid() */
#include "file.h"
#include "file_lcl.h"    /* file_if_local() */
#include "util.h"        /* esprintf() */



eslist *paths_setup=NULL;
eslist *paths_env=NULL;

char   *prefix=NULL;
char   *path_process=NULL,
       *path_setup=NULL;



/*****************************************************************************/



/**
 * find 'file' in $PATH.
 * @return a buffer that must be free()d, or NULL for failure.
 * @param cwd - current working directory of session
 * @param file - name of file to find
 */
char *get_path_file(const char *cwd,const char *file) {
  char         *ret;
  const char   *pre;
  eslist       *e;

  if(!file||!*file)
    return NULL;

  for(e=paths_env;e;ESLIST_NEXT(e)) {
    pre=(char*)ESLIST_PAYLOAD(e);
    if(!strcmp(pre,".")) {
      E(4,"(%s,%s): pre <- cwd\n",cwd?cwd:"",file?file:"");
      pre=cwd; }
    if(pre) {
      ret=NULL;
      esprintf(&ret,"%s/%s",pre,file);
      if(file_exists(ret))
        return ret;
      free(ret); }}
  return NULL; }



/**
 * find 'url' in one of the setup-dirs.
 * @return a buffer that must be free()d, or NULL for failure.
 * @param url - name of file to find
 */
char *get_setup_file(const char *url) {
  char         *ret;
  const char   *file;
  eslist       *e;

  if(!url||!*url)                       /* empty?  fail */
    return NULL;

  if(!file_if_local(url)||file_if_absolute(url)) {
    if(file_exists(url))                /* absolute (URL or other)?  use. */
      return strdup(url); }

  if(!(file=file_if_local(url)))        /* strip URL. */
    return NULL;

  for(e=paths_setup;e;ESLIST_NEXT(e)) {
    if((ret=file_compose_path((char*)ESLIST_PAYLOAD(e),file,NULL))) {
      if(file_if_absolute(ret)&&file_exists(ret)) {
        return ret; }
      free(ret); }}
  return NULL; }



/**
 * add a path to search-paths for setup-files.
 * called e.g. from theme.c to add theme->path.
 * @param p - path to add
 */
int paths_add(char *p) {
  int ret;
  if(!p||!*p) {
    E(0,"called with no path!?  (%p)\n",p);
    return NOIR_NODATA; }
  else if(!(ret=file_exists(p))) {
    E(0,"(\"%s\"): directory does not exist!?\n",p);
    return NOIR_NOTFOUND; }
  else if(!S_ISDIR(ret)) {
    E(0,"(\"%s\"): parameter is not a directory.\n",p);
    return NOIR_FAIL; }
  return eslist_prepend(&paths_setup,p); }



/*****************************************************************************/



/**
 * deinitialize paths sub-system.
 */
void paths_exit(void) {
  eslist_free(&paths_setup,free);
  eslist_free(&paths_env,free);
  free(prefix);
  prefix=NULL; }



/**
 * initialize paths sub-system.
 * determine and set search-paths.
 * @param cwd - the current working-directory
 * @param argv0 - the name the program was invoked under
 */
void paths_init(const char *cwd,const char *argv0) {
  char        *p=NULL;
  char        *g,*e;
  const char  *path_home;

  /* ----------------------------------------------------------------------- */

  if((e=getenv("PATH"))&&(e=strdup(e))) {
    char *a,*b;
    a=e;
    do {
      if((b=strchr(a,':')))
        *(b++)='\0';
      eslist_append(&paths_env,strdup(a));
      E(3,"$PATH.=%s\n",a);
      a=b;
    } while(b);
    free(e); }

  /* ----------------------------------------------------------------------- */

  if(!file_if_absolute(argv0)) {         /* relative path? */
    if(!file_if_nested(argv0)) {         /* file-name only, look in $PATH */
      char *f=get_path_file(cwd,argv0);

      if(f) {
        path_process=file_dirname(f);
        free(f); }}

    else {                               /* relative path, make absolute */
      char *f,*p;
      if((f=file_dirname(argv0))) {
        path_process=file_compose_path(cwd,f,NULL);
        free(f); }

      while((f=strstr(path_process,"/./")))
        memmove(f,f+2,strlen(f+2)+1);

      while((f=strstr(path_process,"/../"))) {
        *f='\0';
        if((p=strrchr(path_process,'/')))
          memmove(p,f+3,strlen(f+3)+1);
        else
          memmove(f,f+3,strlen(f+3)+1); }}}

  else {   /* absolute path */
    path_process=file_dirname(argv0); }

  /* ----------------------------------------------------------------------- */

  if(!(path_home=getenv("HOME"))) {
    struct passwd *pwe=getpwuid(getuid());
    if(!pwe)
      E(0,"Go away, you don't exist!\n");
    else
      path_home=pwe->pw_dir; }

  if(getenv("ETC_HOME")) {
    action_context *ac=file_glob(getenv("ETC_HOME"),NULL);
    if(!ac)
      E(0,"ETC_HOME set to illegal value, using HOME instead...\n");
    else if(action_context_get_seletion_size(ac)!=1)
      E(0,"ETC_HOME set to wildcard value that expands to several files, using HOME instead...\n");
    else
      esprintf(&path_setup,"%s/" DIR_USER,action_file_get_tag(action_context_get_selection(ac)));
    action_context_dst(ac); }

  if(!path_setup)
    esprintf(&path_setup,"%s/" DIR_USER,path_home);

  /* ----------------------------------------------------------------------- */

  /* default search paths */
  if(path_process&&(p=strrchr(path_process,'/'))) {
    if(!strcmp(p,"/bin")) {
      *p='\0';
      prefix=strdup(path_process);
      *p='/'; }
    else
      p=NULL; }
  if(!prefix)
    prefix=strdup(PACKAGE_PREFIX);

  { char *g3;
    g=strdup(prefix);

    /* /usr/local/share/evidence */
    g3=strdup(PACKAGE_DATA_DIR);
    eslist_prepend(&paths_setup,g3); }

  if(!p)
    eslist_prepend(&paths_setup,strdup(path_process));

  free(g);
  g=NULL;

  /* /home/azundris/.evidence */
  eslist_prepend(&paths_setup,strdup(path_setup));

  /* / */
  eslist_append(&paths_setup,strdup(""));

  /* /usr/local -- needed for stuff in PREFIX/lib/evidence/... */
  eslist_append(&paths_setup,strdup(prefix));


#if 0
  /* no longer free these, they're globally available now */
  free(path_process);
  free(path_setup);
#endif

  { eslist *e;
    char   *p;
    for(e=paths_setup;e;ESLIST_NEXT(e)) {
      p=(char*)ESLIST_PAYLOAD(e);
      E(2,"SearchDir: %s\n",p&&strlen(p)?p:"."); }}}



/*****************************************************************************/
