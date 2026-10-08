/** @file  tag2name.c
 *  @brief context-menu action "tag2name"
 *  set file-name from ID3 or Ogg-tag
 *  TODO ### should get template for file-name from properties
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "evidence.h"
#include "gui/common/alert.h"
#include "gui/common/session.h"
#include "file.h"               /* file_mv() */
#include "file_lcl.h"           /* file_compose_path() */
#include "meta.h"
#include "util.h"
#include "properties.h"



typedef struct _tag {
  char *no,*artist,*title,*album,*genre,*year,*ext;
} tag;



/******************************************************************************/



/**
 * get the value of a meta-data item
 * @return a value, or ""
 * @param md   the list of meta-data
 * @param key  the key of the item to look for
 */
static char *get_val(noir_md *md,char *key) {
  noir_meta *m;
  if((m=meta_in_group(md,key,NULL)))
    return meta_get_val(m);
  return ""; }



/**
 * get the value of the first existing of two meta-data items
 * @return a value, or ""
 * @param md  the list of meta-data
 * @param k1  the 1st key, or NULL
 * @param k2  the 2nd key, or NULL
 */
static char *get_two(noir_md *md,char *k1,char *k2) {
  noir_meta *m;
  if(k1&&(m=meta_in_group(md,k1,NULL)))
    return meta_get_val(m);
  if(k2&&(m=meta_in_group(md,k2,NULL)))
    return meta_get_val(m);
  return ""; }



/******************************************************************************/



/**
 * replace a substring with another, possibly with a different length
 * @param s  replace from this position
 * @param d  remove this many chars
 * @param n  insert this string
 */
static char *rep(char *s,size_t d,char *n) {
  char   *p;
  size_t  l=strlen(n);  /* length of new item */

  p=s+d;
  memmove(s+l,p,strlen(p)+1);
  strncpy(s,n,l);
  return s; }



/**
 * expand %-sequences in user-defined template for new name
 * @return an error-code
 * @param t    the data from the tag
 * @param buf  the buffer for the new name
 */
static int tp_do(tag *t,char *buf) {
  char *b=NULL,*e=NULL,*s=buf,*p,*n;
  char  nb[5];

  while(*s) {
    switch(*s) {
      case '%':
        p=s+1;
        n=NULL;
        switch(*p) {
          case '[': rep(s,2,"["); s++; continue;
          case ']': rep(s,2,"]"); s++; continue;
          case '%': rep(s,2,"%"); s++; continue;

          case 'a': n=t->artist;        break;
          case 'n': n=t->title;         break;
          case 'd': n=t->album;         break;
          case 'G': n=t->genre;         break;
          case 'X': n=t->ext;           break;
          case 'y':
            if((n=t->year)) {
              if(strlen(n)<4) {
                int v=atoi(n);
                if(v>=100) {
                  E(0,"weird year \"%s\"!?\n",t->year);
                  n=NULL; }
                else {
                  snprintf(nb,5,"%d",(v>=70)?1900+v:2000+v);
                  n=nb; }}}
            break;
          case 't':
            if((n=t->no)) {
              if(strlen(n)==1) {
                strcpy(nb,"0");
                strcpy(&nb[1],n);
                n=nb; }}
            break;
          case '\0':
            E(0,"trailing %% skipped...\n");
            *s='\0';
            break;
          default:
            E(0,"unknown sequence %%%c skipped...\n",*p);
            E(2,"?|%s|\n",buf);
            n="";
            break; }

        /* find ] */
        e=NULL;
        p++;
        while(*p&&!e) {
          if((*p=='%')&&p[1])
            p+=2;
          else if(*p==']')
            e=p;
          else
            p++; }

        E(3,"<|%s|\n",buf);

        if(n&&strlen(n)) {
          if(e)
            memmove(e,e+1,strlen(e));
          rep(s,2,n);
          if(b)
            memmove(b,b+1,strlen(b)); }
        else if(b&&e)
          rep(b,e-b+1,"");
        else
          rep(s,2,"");

        E(3,">|%s|\n\n",buf);
        if(b)
          s=b;
        b=e=NULL;
        break;

      case '[':
        b=s++;
        break;

      case ']':
        e=s++;
        break;

      default:
        s++; }}

  return NOIR_SUCC; }



/**
 * create file-name by user-defined rule
 * sprintf() with automatic buffer allocation.
 * grip compatible switches:
 * %d disc-name
 * %t trackno
 * %a artist (track)
 * %n name (title)
 * %y year
 * %G genre
 *
 * @return a newly-allocated buffer holding the new name
 * @param t    the data from the tag
 * @param fmt  the user-defined template for the new name
 */
static char *tprintf(tag *t,char *fmt) {
  int      i,els=sizeof(tag)/sizeof(char *);
  char   **x=(char **)t;
  char    *n=NULL;
  size_t   l=strlen(fmt)+2;

  for(i=0;i<els;i++) {
    if(*x)
      l+=strlen(*x);
    x++; }

  if((n=malloc(l))) {
    strcpy(n,fmt);  /* do not strdup(), we'll need more space later! */
    tp_do(t,n); }

  return n; }



/******************************************************************************/



/**
 * check whether required meta-data item is present
 * @return TRUE for present, FALSE otherwise
 * @param ns  the session, or NULL
 * @param ac  the action-context
 * @param f   the file's name
 * @param p   the pointer to the meta-data item's value, or NULL
 * @param l   a textual description of the meta-data item
 */
static int check(void *ns,action_context *ac,char *f,char *p,char *l) {
  if(p&&*p)
    return TRUE;
  if(ac&&action_context_tst_flags(ac,AF_SELECTION)&&!action_context_tst_flags(ac,AF_LAST))
    E(0,"need %s to rename \"%s\"...\n",l,f);
  else
    form_alert(ns,"Need %s to rename\n%s",l,f);
  return FALSE; }



/**
 * get formatting mask for renamed files from prop-list, or return the default.
 * @return the mask as a C-string, which must be free()d.
 * @param ns  the session
 */
static char *tag2name_get_mask(noir_sess *ns) {
  char           *mask="[%t-]%a - %n.%X",*tmp=NULL;
  noir_prop      *el=NULL;
  noir_prop_bag  *props=NULL;

/* n=tprintf(t,"[%t-]%a - %n.%X ([%y's ]%G[ from %d]) %Z %[keks%%dose%]"); */
/* n=tprintf(t,"[%t-]%a - %n.%X ([%y's ]%G[ from \"%d\"])"); */

  if((el=prop_bags_prop_get(2,&props,"tag-to-name-mask",
                              (noir_prop_bag *)noir_sess_get_theme_props(ns),
                              props_global)))
    tmp=prop_get_string(prop_bag_prop_get(props,"tag-to-name-mask"));

  if(tmp) {      /* valid property */
    mask=tmp;
    E(1,"found mask: \"%s\".\n",mask); }
  else if(!el) { /* no property */
    prop_bag_prop_default(props_global,"tag-to-name-mask",mask,0,0,0);
    E(1,"installed mask: \"%s\".\n",mask); }
  else           /* NULL property */
    E(0,"found NULL mask, reverting to default: \"%s\".\n",mask);

  return strdup(mask); }



/**
 * context-menu action: derive file-name from ID3/Vorbis-tag
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  noir_sess     *ns=noir_sess_get_by_serial(serial);
  tag           *t;
  char          *n=NULL;
  char          *mask=NULL;
  int            ret=NOIR_NODATA;

  if(!ac||!action_context_get_cwd(ac))
    return NOIR_NODATA;

  if(!(t=malloc(sizeof(tag))))
    return NOIR_OOM;
  memset(t,0,sizeof(tag));

  t->title =get_val(md,"title");
  t->artist=get_val(md,"artist");
  t->album =get_val(md,"album");
  t->genre =get_val(md,"genre");
  t->no    =get_two(md,"tracknumber","track #");
  if(!strlen(t->no))
    t->no  =get_val(md,"track");
  t->year  =get_two(md,"year","date");
  t->ext   =strrchr(filename,'.');

  if(!check(ns,ac,filename,t->artist,"artist")||
     !check(ns,ac,filename,t->title,"title")||
     !check(ns,ac,filename,t->ext,"extension"))
    goto done;

  t->ext++;

  if(!(mask=tag2name_get_mask(ns))) {
    ret=NOIR_FAIL;  /* this should never happen... */
    goto done; }

  n=tprintf(t,mask);

  E(2,"1|%s-%s - %s.%s (%s's %s from %s)|\n",
    t->no,t->artist,t->title,t->ext,
    t->year,t->genre,t->album);

  if(n) {
    char *f=file_compose_path(action_context_get_cwd(ac),n,NULL);
    free(n);
    if(!f) {
      ret=NOIR_OOM;
      goto done; }
    if((n=strrchr(f,'.'))) {
      while(*n) {
	*n=tolower(*n);
	n++; }}
    E(1,"rename \"%s\" => \"%s\"...\n",filename,f);
    if(!strcmp(filename,f))
      ret=NOIR_IGNORED;
    else
      ret=file_mv(filename,f);
    free(f); }
  else
    ret=NOIR_NODATA;

 done:
  if(mask)
    free(mask);
  free(t);

  return ret; }



/******************************************************************************/
