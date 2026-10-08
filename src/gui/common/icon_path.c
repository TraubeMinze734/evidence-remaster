/** @file  icon_path.c
 *  @brief find individual icons of a given icon-set
 */

#include "config.h"

#include <ctype.h>       /* isspace() */
#include <stdlib.h>      /* free() */
#include <stdio.h>       /* FILE */
#include <string.h>      /* str*() */

#include "evidence.h"    /* DIR_ICONS */
#include "debug.h"       /* E() */
#include "errors.h"      /* NOIR_* */
#include "file.h"        /* file_exists() */
#include "file_lcl.h"    /* file_compose() */
#include "util.h"        /* hash_*() */
#include "setup.h"       /* path_setup */
#include "mime.h"        /* MIME_* */
#include "alert.h"       /* form_alert() */






static void   *ht_icon_path=NULL;
static eslist *paths_icons=NULL;






/*****************************************************************************/



/**
 * destruct a string (in ht_icon_path hashtable)
 * @param data  a string
 */
static void icon_path_dst(void *data) {
  free(data); }



/**
 * remove path for icon 'key' from hashtable
 * @param key  the icon's symbolic name
 */
int icon_path_del(char *key) {
  return hash_table_remove(ht_icon_path,key)?NOIR_SUCC:NOIR_FAIL; }



/**
 * add icon to hash
 * @return an error-code
 * @param key        the icon's symbolic name
 * @param icon_path  where the image-data for the icon is in the file-system
 */
int icon_path_add(const char *key,const char *icon_path) {
  char *k,*v;

  if(!ht_icon_path) {
    if(!(ht_icon_path=hash_table_new(icon_path_dst,icon_path_dst))) {
      int warned=0;
      if(!warned) {
        warned++;
        E(0,"couldn't create hashtable!?\n"); }
      return NOIR_FAIL; }}

  if(hash_table_fetch(ht_icon_path,key))
    return NOIR_IGNORED;

  if(!(k=strdup(key)))
    return NOIR_OOM;

/* if(!(v=get_setup_file(icon_path))) { */  /* immediate-resolve */
  if(!(v=strdup(icon_path))) {   /* lazy-resolve */
    free(k);
    return NOIR_OOM; }

#if 0
  hash_table_insert(ht_icon_path,k,v);
#else
  hash_table_replace(ht_icon_path,k,v);
#endif

  return NOIR_SUCC; }






/*****************************************************************************/



#ifndef ICON_PATH_USE_SETUP
/**
 * find 'file' in one of the setup-dirs.
 * @return a buffer that must be free()d, or NULL for failure.
 * @param file - name of file to find
 */
char *icon_path_find_file(const char *file) {
  char         *ret;
  eslist       *e;

  if(debug>0) {
    if(!file||!*file) {                   /* empty?  fail */
      E(0,"no icon-'file' given!?\n");
      return NULL; }

    if(!file_if_local(file)) {
      E(0,"icons must be local.\n");
      return NULL; }}

  if(file_if_absolute(file))
    return strdup(file);

  for(e=paths_icons;e;ESLIST_NEXT(e)) {
    if((ret=file_compose_path((char*)ESLIST_PAYLOAD(e),file,NULL))) {
      if(file_if_absolute(ret)&&file_exists(ret)) {
        return ret; }
      free(ret); }}
  return NULL; }
#endif



/**
 * get the name and path of an icon.
 * if the hash returns a relative file-name, qualify it and update the hash
 * @return name and path of an icon (image), or NULL for failure (no hash, not found in hash, not found on disk)
 * @param file  the name and path of the file we want to represent, or NULL
 * @param mime  the file's MIME-type, or NULL
 */
char *icon_path_get(const char *file,const char *mime) {
  char *res=NULL;
  char *f=file?strrchr(file,'/'):NULL;
  char *m=NULL,*key=NULL;

  if(!ht_icon_path)
    return (char *)NULL;

  if(file&&*file) {
    if(!(res=(char *)hash_table_fetch(ht_icon_path,key=(char *)file))) {  /* entry for this file */
      if(f)
        res=(char *)hash_table_fetch(ht_icon_path,key=&f[1]); }}   /* entry for this name */

  if(!res) {
    if((m=(mime&&strlen(mime))?strdup(mime):NULL)) {
      char *p=strchr(m,';'),*q=strchr(m,',');
      if(p) {
        if(!q||(p<q))
          *p='\0';
        else
          *q='\0'; }
      else if(q)
        *q='\0'; }

    if((key=m)) {
      if(!(res=(char *)hash_table_fetch(ht_icon_path,m))) { /* entry for this mime-type */
        char *m2=strchr(m,'/');
        if(m2&&m2[1]) {
          m2[2]='\0';
          m2[1]='*';
          if(!(res=(char *)hash_table_fetch(ht_icon_path,m))) {  /* entry for this mime-category */
            m2[1]='\0';
            res=(char *)hash_table_fetch(ht_icon_path,m); }}}}}

  if(res&&file_if_relative(res)&&
#ifndef ICON_PATH_USE_SETUP
     (res=icon_path_find_file(res))
#else
     (res=get_setup_file(res))
#endif
    ) { /* icon_path_find_file */
/*  E(0,"icon_path_get() hot-resolved {%s:%s} => %s: %s\n",file?file:"?",mime?mime:"?",key,f); */
    hash_table_replace(ht_icon_path,strdup(key),res); }

  if(m)
    free(m);

  return res; }



/**
 * get the name and path of an icon.
 * if the hash returns a relative file-name, qualify it and update the hash.
 * if no icon is found for file/MIME, return path to a default icon.
 * @return name and path of an icon (image), or NULL for failure (no hash, default-icon not found in hash, default-icon not found on disk)
 * @param file  the name and path of the file we want to represent, or NULL
 * @param mime  the file's MIME-type, or NULL
 */
char *icon_path_get_always(const char *file,const char *mime) {
  char *ret;
  if(!(ret=icon_path_get(file,mime))) {
    if(!(ret=icon_path_get("default","default")))
      ret=icon_path_get("unknown","unknown"); }
  return ret; }






/*****************************************************************************/



/**
 * save a single icon<->MIME-type mapping
 * @param key    the icon's symbolic name
 * @param value  where the image-data for the icon is in the file-system
 * @param data   the FILE to save to
 */
static void icon_path_save_entry(char *key,void *value,void *data) {
  fprintf((FILE *)data,"%s \t%s\n",(char *)key,(char *)value); }



/**
 * save icon<->MIME-type mappings in user-dir
 * @return an error-code
 */
int icon_path_save(void) {
  FILE *fh;
  char *fn;
  int   ret;

  if((fn=file_user_setup(MIME_ICONS))) {
    if((fh=fopen(fn,"w"))) {
      hash_table_walk(ht_icon_path,icon_path_save_entry,(void *)fh);
      fclose(fh);
      ret=NOIR_SUCC; }
    else
      ret=NOIR_FAIL; }
  else
    ret=NOIR_NOTFOUND;

  if(ret!=NOIR_SUCC)
    form_alert(NULL,"Could not save icon<=>MIME-type assignments in \"%s\"!? (%d)",fn?fn:MIME_ICONS,ret);
  if(fn)
    free(fn);

  return ret; }



/**
 * destruct icon-path hashtable
 */
static void icon_path_unload(void) {
  if(ht_icon_path)
    hash_table_dst(&ht_icon_path);
  ht_icon_path=NULL; }



/**
 * create icon-path hashtable from file
 * @return an error-code
 */
int icon_path_load(void) {
  FILE *fh;
  char *p,*fn;
  char  l[MIME_MAX_LEN];

  if((fn=icon_path_find_file(MIME_ICONS))) {
    if((fh=fopen(fn,"r"))) {

      icon_path_unload();

      while(fgets(l,MIME_MAX_LEN,fh)) {
        p=l;
        while(isspace(*p))
          p++;

        if(*p!='#') {
          char *p2,*p3;
          char  save;

          p2=p;
          while(*p2&&!isspace(*p2))
            p2++;

          if(*p2) {
            *(p2++)='\0';  /* now p references the type */
            while(*p2&&*p2!='\n'&&*p2!='\r'&&*p2!='#') {
              while(isspace(*p2))
                p2++;
              p3=p2;
              while(*p3&&*p3!='\n'&&*p3!='\r'&&!isspace(*p3))
                p3++;
              save=*p3;
              *p3='\0';

              icon_path_add(p,p2);

              *p3=save;
              p2=p3; }}}}
      fclose(fh); }
    free(fn);
    return NOIR_SUCC; }
  else
    E(0,"MIME-init: *** CRITICAL *** couldn\'t find \"%s\"!\n",MIME_ICONS);
  return NOIR_NOFILE; }






/*****************************************************************************/



int icon_path_exit(void) {
  eslist_free(&paths_icons,free);
  if(ht_icon_path)
    hash_table_dst(&ht_icon_path);
  return NOIR_SUCC; }



#undef ICON_PATH_USE_SETUP


int icon_path_add_dir(char *p) {
#ifdef ICON_PATH_USE_SETUP
  return paths_add(p);
#else
  eslist_prepend(&paths_icons,p);
  return NOIR_SUCC;
#endif
}



int icon_path_init(const char *iconset) {
  char *g=NULL;

  /* /usr/local/share/evidence/icons/default */
  if(iconset&&file_if_absolute(iconset))
    icon_path_add_dir(strdup(iconset));
  else {
    char *g2;
    if((g2=file_compose_path(PACKAGE_DATA_DIR,DIR_ICONS,iconset?iconset:"default",NULL)))
      icon_path_add_dir(g2);
    else
      return NOIR_OOM; }

  /* /home/azundris/.evidence/icons/default */
  if((g=file_compose_path(path_setup,DIR_ICONS,iconset?iconset:"default",NULL)))
    icon_path_add_dir(g);
  else
    return NOIR_OOM;

  return NOIR_SUCC; }






/*****************************************************************************/
