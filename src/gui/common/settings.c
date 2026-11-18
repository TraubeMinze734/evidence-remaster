/** @file  settings.c
 *  @brief persistent user-settings (~/.evidence/evidence.conf).
 *
 *  The 2005 original delegated persistent configuration to the external
 *  tool "examine", which no longer exists.  This module replaces that
 *  with a small "key=value" file below ~/.evidence.  Values are kept as
 *  properties in props_global, so they are also reachable via IPC
 *  (evsh "prop.get/set") and fire property-listeners when changed at
 *  runtime (e.g. thumbnail-live).
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#include "evidence.h"       /* DIR_THEME, E() via debug.h */
#include "properties.h"     /* props_global, prop_bag_prop_*() */
#include "setup.h"          /* path_setup */
#include "file.h"           /* file_user_setup(), file_exists() */
#include "file_lcl.h"       /* file_compose_path() -- NOT in file.h; without this
                               prototype the call compiles as implicit int() and
                               the returned pointer is truncated to 32 bit */
#include "util.h"           /* esprintf(), eslist, ESLIST_* */

#include "gui/efl/common/theme.h"  /* THEME_CFG */

#include "settings.h"



/* keys persisted to/from the settings file -- extend here when new
   settings become configurable in the settings dialog */
static const char *settings_keys[] = {
  "theme",
  "view",
  "thumbnail-live",
  "thumbnail-live-sound",
  NULL };



static int settings_key_known(const char *k) {
  int i;
  if(!k)
    return 0;
  for(i=0;settings_keys[i];i++)
    if(!strcmp(settings_keys[i],k))
      return 1;
  return 0; }



/**
 * path of the settings file below ~/.evidence (dir is created on demand).
 * @return a buffer that must be free()d, or NULL
 */
char *settings_file_path(void) {
  return file_user_setup(SETTINGS_FILE); }



/**
 * read the settings file and apply all known keys to props_global.
 * safe to call before the subsystem defaults are registered: existing
 * props keep their value across prop_bag_prop_default(), and listeners
 * registered later fire immediately for props that already have a value.
 * @return NOIR_SUCC if the file was read (even if empty), NOIR_NOTFOUND
 *         if there is no settings file yet, NOIR_PARTIAL on parse errors
 */
int settings_load(void) {
  char *fn=settings_file_path();
  FILE *f;
  int   ret=NOIR_NOTFOUND;

  if(!fn)
    return NOIR_FAIL;

  if(!(f=fopen(fn,"r"))) {
    E(1,"no settings file \"%s\" yet\n",fn);
    free(fn);
    return NOIR_NOTFOUND; }

  ret=NOIR_SUCC;

  { char line[1024];
    while(fgets(line,sizeof(line),f)) {
      char *p=line,*k,*v;

      /* strip trailing whitespace/newline */
      { size_t l=strlen(line);
        while(l&&isspace((unsigned char)line[l-1]))
          line[--l]='\0'; }

      /* skip empty lines and comments */
      while(isspace((unsigned char)*p))
        p++;
      if(!*p||*p=='#'||*p==';')
        continue;

      k=p;
      while(*p&&*p!='='&&!isspace((unsigned char)*p))
        p++;
      if(*p!='=')          /* no '=': not a key=value line */
        continue;
      *p='\0';
      v=p+1;
      while(isspace((unsigned char)*v))
        v++;
      { size_t l=strlen(v);   /* trim value's tail (already done above, but be safe) */
        while(l&&isspace((unsigned char)v[l-1]))
          v[--l]='\0'; }

      /* trim key's tail */
      { size_t l=strlen(k);
        while(l&&isspace((unsigned char)k[l-1]))
          k[--l]='\0'; }

      if(!*k||!settings_key_known(k))
        continue;

      E(1,"settings: \"%s\" = \"%s\"\n",k,v);
      if(prop_bag_prop_set(props_global,k,v)!=NOIR_SUCC)
        ret=NOIR_PARTIAL; } }

  fclose(f);
  free(fn);
  return ret; }



/**
 * get a setting's value as a string.
 * @return a buffer that must be free()d, or NULL if the setting is unknown
 */
char *settings_get(const char *key) {
  noir_prop *e=key?prop_bag_prop_get(props_global,key):NULL;
  return e?prop_get_as_string(e):NULL; }



/**
 * set a setting. fires property-listeners, so runtime behaviour (e.g.
 * live thumbnails) changes immediately.
 * @return an error-code
 */
int settings_set(const char *key,const char *val) {
  return prop_bag_prop_set(props_global,key,val); }



/**
 * write all known settings from props_global to the settings file.
 * @return NOIR_SUCC, or NOIR_PARTIAL if some key had no value
 */
int settings_save(void) {
  char *fn=settings_file_path();
  FILE *f;
  int   i,ret=NOIR_SUCC;

  if(!fn)
    return NOIR_FAIL;

  if(!(f=fopen(fn,"w"))) {
    E(0,"could not write settings file \"%s\"!?\n",fn);
    free(fn);
    return NOIR_FAIL; }

  fprintf(f,"# evidence-remaster settings -- written by the settings dialog\n");

  for(i=0;settings_keys[i];i++) {
    char *v=settings_get(settings_keys[i]);
    if(v&&*v) {
      fprintf(f,"%s=%s\n",settings_keys[i],v);
      free(v); }
    else {
      E(1,"settings: no value for \"%s\", not saved\n",settings_keys[i]);
      if(v) free(v);
      ret=NOIR_PARTIAL; } }

  fclose(f);

  E(1,"settings saved to \"%s\"\n",fn);
  free(fn);
  return ret; }



/*****************************************************************************/
/* theme enumeration */



static int theme_name_cmp(const void *a,const void *b) {
  return strcmp(*(char *const *)a,*(char *const *)b); }



/**
 * list theme-names found in the setup-paths: every sub-directory of a
 * "themes" dir in any setup-path that contains an evidence.theme file.
 * @return array of malloc'd names (sorted), or NULL; free with
 *         settings_theme_list_free()
 * @param count  where to store the number of entries
 */
char **settings_theme_list(int *count) {
  extern eslist *paths_setup;   /* defined in setup.c */
  char   **list=NULL;
  int      n=0,cap=0;
  eslist  *e;

  if(count)
    *count=0;

  for(e=paths_setup;e;ESLIST_NEXT(e)) {
    char        *base=(char *)ESLIST_PAYLOAD(e);
    char        *tdir=NULL;
    DIR         *d;
    struct dirent *de;

    if(!base)
      continue;
    if(!(tdir=file_compose_path(base,DIR_THEME,NULL)))
      continue;

    if(!(d=opendir(tdir))) {
      free(tdir);
      continue; }

    while((de=readdir(d))) {
      char        *sub=NULL,*cfg=NULL;
      struct stat  st;

      if(de->d_name[0]=='.')
        continue;

      if(!(sub=file_compose_path(tdir,de->d_name,NULL)))
        continue;
      if((stat(sub,&st)==0)&&S_ISDIR(st.st_mode)) {
        if((cfg=file_compose_path(sub,THEME_CFG,NULL))) {
          if(file_exists(cfg)) {
            /* got a theme; append unless already listed (a theme may be
               visible via several setup-paths) */
            int i,dup=0;
            for(i=0;i<n;i++)
              if(!strcmp(list[i],de->d_name)) { dup=1; break; }
            if(!dup) {
              if(n>=cap) {
                int   nc=cap?cap*2:8;
                char **nl=realloc(list,nc*sizeof(char *));
                if(!nl) { free(cfg); free(sub); continue; }
                list=nl; cap=nc; }
              if((list[n]=strdup(de->d_name)))
                n++;
              else
                E(0,"out of memory!?\n"); }
            free(cfg); }
          else
            free(cfg); } }
      free(sub); }

    closedir(d);
    free(tdir); }

  if(list&&n>1)
    qsort(list,n,sizeof(char *),theme_name_cmp);

  if(count)
    *count=n;

  return list; }



/**
 * free a list returned by settings_theme_list().
 */
void settings_theme_list_free(char **list,int count) {
  int i;
  if(!list)
    return;
  for(i=0;i<count;i++)
    free(list[i]);
  free(list); }
