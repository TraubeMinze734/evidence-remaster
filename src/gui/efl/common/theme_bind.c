/** @file  theme_bind.c
 *  @brief theme/preferences setup.
 *
 * theming of evas-based icon-list: read theme-setup from file,
 * return an initiliazed theme-struct.
 */


#include "config.h"

#include "gui/common/session_private.h"

#ifdef HAVE_EVAS2
#  include "evas_macros.h"                      /* get_evas_from_iconlist() */
#  include "gui/gtk2/icon_view/gevas_bind.h"    /* ICON_LIST_GET_FROM_WIDGET() */
#  include "gui/gtk2/common/icon.h"             /* icon_init() */
#endif

#include "gui/gtk2/common/shelf.h"              /* CLIENT_SHELF */

#include "theme.h"

#include "setup.h"         /* get_setup_file() */

#include "util.h"          /* esprintf() */

#include "file.h"          /* PROTOCOL_DELIMITER */
#include "file_lcl.h"      /* file_if_*() */

#include <stdlib.h>        /* free() */
#include <string.h>        /* str*() */






/*****************************************************************************/
/* scan for all engines of a given type. forthcoming. */



#if 0
static noir_prop *et_get_els_for(noir_theme *t,char *engine) {
  return NULL; }



int theme_engine_scan(char *engine) {
  noir_theme    *t=theme_new(NULL);
  noir_prop     *e,*f;
  int            ret=NOIR_SUCC;

  if(!t)
    return NOIR_OOM;

  if(!(e=et_get_els_for(t,engine))) {
    E(1,"failed to load \"%s\"...  (Azundris-only message :)\n",engine);
    ret=NOIR_FAIL; }
  else {
    for(f=e;f;f=e) {
      if(prop_get_flags(f)&EF_BOUNDS) {
        if(prop_get_type(f)==ET_INT)
          printf("  %s{%s}: %ld - %ld\n",prop_get_key(f),prop_get_type_as_string(f),f->lo,f->hi);
        else if(f->type==ET_FLT)
          printf("  %s{%s}: %lf - %lf\n",prop_get_key(f),prop_get_type_as_string(f),((float)f->lo)/NOIR_THEME_FLOAT_PRECISION,((float)f->hi)/NOIR_THEME_FLOAT_PRECISION);
        else
          printf("  %s{%s}: <unknown bounds type>\n",prop_get_key(f),prop_get_type_as_string(f)); }
      else
        printf("  %s{%s}\n",prop_get_key(f),prop_get_type_as_string(f));
      e=prop_get_next(f);
      prop_bag_prop_dst(t->data,f); }}

  free(t);

  return ret; }
#endif






/*****************************************************************************/
/* helpers for other modules */



/**
 * get icon dimensions
 * @return an error-code
 * @param t - the theme
 * @param w - where to store icon-width,  NULL for don't
 * @param h - where to store icon-height, NULL for don't
 */
int theme_get_iconsize(const noir_theme *t,int *w,int *h) {
  int ret=NOIR_IGNORED;
  if(!t)
    return NOIR_FAIL;
  if(w) {
    if((*w=t->icon_width)<=0)
      ret=NOIR_NODATA;
    else
      ret=NOIR_SUCC; }
  if(h) {
    if((*h=t->icon_height)<=0)
      ret=NOIR_NODATA;
    else if(ret==NOIR_NODATA)
      ret=NOIR_PARTIAL;
    else
      ret=NOIR_SUCC; }
  return ret; }



/**
 * get icon-mode
 * @return the icon-mode
 * @param t - the theme
 */
ic_mode theme_get_iconmode(const noir_theme *t) {
  return t?t->icon_mode:IC_NONE; }






/*****************************************************************************/
/* save theme */



#define SAVE_AS "/" DIR_USER "/" THEME_CFG

/**
 * theme_dump - save a setup to a plaintext file.
 * @param what - the session to save
 */
int theme_dump(const void *what) {
  noir_sess  *ns=(noir_sess *)what;
  noir_theme *t=((noir_theme *)noir_sess_get_theme(ns));
  FILE       *fh;
  char       *fn,*v;
  const char *p;
  size_t      l=strlen(home)+sizeof(SAVE_AS);
  int         ret=NOIR_FAIL;

  if(!(fn=malloc(l)))
    return NOIR_OOM;

  strcat(strcpy(fn,home),SAVE_AS);

  E(0,"saving theme as \"%s\"...\n",fn);

  if((fh=fopen(fn,"wb"))) {
    noir_prop *e=prop_bag_prop_get_1st(t->data);
    ret=NOIR_SUCC;

/* #define SAVE_FMT "%22.22s" */
    while(e) {
      if((v=prop_get_as_string(e))) {
        fprintf(fh,"%s%s: %s\n",
                ((prop_get_type(e)==ET_PTR)||
                 !(prop_get_flags(e)&EF_MODIFIED))?"; ":"",
                prop_get_key(e),v);
        free(v); }
      e=prop_get_next(e); }

    if(ns->iconset) {
      if(strstr(ns->iconset,PROTOCOL_DELIMITER)||file_if_absolute(ns->iconset))
        p=ns->iconset;
      else if(file_if_nested(ns->iconset))
        p=file_name(ns->iconset);
      else
        p=ns->iconset;
      if(p)
        fprintf(fh,"%s: %s\n","iconset",p); }

    { int c=0,m=t->icon_mode;
      char *im[]={ "none", "w", "h", "{w,h}/w", "{w,h}/h", "auto", "fill", NULL, NULL };
      while(m&&im[c]) {
     /* printf("%d %d ",c,m); printf("%s\n",im[c]); */
        c++;
        m=m>>1; }
      if(im[c])
        fprintf(fh,"%s: %s\n","iconmode",im[c]); }

    fclose(fh); }
  free(fn);
  return ret; }






/*****************************************************************************/
/* helpers */



/**
 * multi-search: try and find an engine in the theme-dir, in the engines-dir, or basically, wherever
 * @return the path denoting the actual position of the engine, or NULL
 * @param t       the theme (for theme-dir etc.), or NULL
 * @param path    the path-prefix ("/usr/share/evidence/themes"?), or NULL
 * @param dir     the theme-dir   ("tosh","engines", ...), or NULL
 * @param prefix  the kind of engine ("ia_"), or NULL
 * @param file    the name ("nautilus")
 * @param suffix  the extension (".so"), or NULL
 */
char *theme_ms(const noir_theme *t,const char *path,const char *dir,const char *prefix,const char *file,const char *suffix) {
  char *r=NULL,*a=NULL,*b=NULL;

  esprintf(&r,"%s/%s/%s%s%s",path?path:"",dir?dir:"",prefix?prefix:"",
                             file?file:"",suffix?suffix:"");

  if(r) {
    if(!file_exists(r)) {
      /* not found in typed dir in same path as theme-dir, try theme-dir */
      if(t&&dir) {
        if(strcmp(t->name,dir)) /* try in theme-dir parallel to this dir */
          if(!(a=theme_ms(NULL,path,t->name,prefix,file,suffix))) {
            /* try in main theme-dir */
            if((b=get_setup_file(DIR_THEME))) {
              if(strcmp(b,path)) {
                a=theme_ms(t,b,dir,prefix,file,suffix); }
              free(b); }}}
      free(r);
      r=a;
      if(dir&&!strcmp(dir,"engines")&&(!r||!file_exists(r))) {
        esprintf(&r,PACKAGE_PREFIX "/lib/evidence/gui/efl/engines/%s%s%s",prefix?prefix:"",file?file:"",suffix?suffix:"");
        if(!r)
          return NULL;
        else if(!file_exists(r)) {
          free(r);
          r=NULL; }}}}

  return r; }



#ifdef HAVE_EVAS2

inline int theme_font_chk(Evas *e,char *font,char *desc) {
  Evas_Object *o;
  Evas_Coord   x,y,w=0,h=0;

  if(!font)
    font="<none>";
  if(!desc)
    desc="unspecified font";

  if(!e) {
    E(0,"could not create a text-object with %s without an evas-canvas!\n",desc);
    return NOIR_FAIL; }

  if((o=evas_object_text_add(e))) {
    evas_object_text_font_set(o,font,40);
    evas_object_text_text_set(o,"abcABC123-_.");
    evas_object_geometry_get(o,&x,&y,&w,&h);
    evas_object_del(o);
    if(w*h)
      return NOIR_SUCC; }
  else {
    E(0,"could not create a text-object with %s on evas at %p!?\n",desc,e);
    return NOIR_FAIL; }

  if(file_exists(font))
    E(0,"could not create a text-object with %s "
      "(\"%s\", requested in \"evidence.theme\"); the font "
      "or your version of freetype may be broken. (size %.0fx%.0f)\n\n",desc,font,(float)w,(float)h);
  else
    E(0,"could not create a text-object with %s "
      "(\"%s\", requested in \"evidence.theme\"); it doesn't exist.\n\n",desc,font);
  return NOIR_FAIL; }

#endif



int theme_font_fallback(noir_theme *t,char *font,char *key,int got) {
  if(!got) {
    E(0,"%s falling back on font \"%s\" -- things will not look the way they should...\n",key,font);
    prop_bag_prop_set(t->data,key,font);
    return NOIR_SUCC; }
  return NOIR_IGNORED; }



char *theme_font_find(const noir_theme *t,const char *v) {
  char *f;
  if(!(f=theme_ms(t,t->path,"fonts","",v,".ttf"))) {
    if(!(f=(char *)get_setup_file(v))) {
      char   *fds[]={ "/usr/X11R6/lib/X11/fonts",
                      "/usr/X11R6/lib/X11/fonts/truetype",
                      "/usr/X11R6/lib/X11/fonts/TrueType",
                      "/usr/X11R6/lib/X11/fonts/msttcorefonts",
                      "/usr/share/fonts",
                      "/usr/share/fonts/truetype",
                      NULL };
      char   *fd;
      int     c=0;
      size_t  l2,l=4+2+strlen(v);

      while(!f&&(fd=fds[c++])) {
        if((f=malloc(l2=(strlen(fd)+l)))) {
          snprintf(f,l2,"%s/%s.ttf",fd,v);
          if(!file_exists(f)) {
            fd=&f[strlen(f)-3];
            strcpy(fd,"TTF");
            if(!file_exists(f)) {
              free(f);
              f=NULL; }}}}

      if(!f)
        f=strdup(v); }}

  if(debug>=4) {
    if(!strcmp(v,f))
      E(0,"font \"%s\" found...\n",v);
    else
      E(0,"font \"%s\" found as \"%s\"...\n",v,f); }
  return f; }






