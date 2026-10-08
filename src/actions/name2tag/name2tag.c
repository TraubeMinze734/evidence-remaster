/** @file  name2tag.c
 *  @brief context-menu action "name2tag"
 *  set ID3 or Ogg-tag from file-name
 *  TODO ### should get template for file-name from properties
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>            /* isspace() */

#include "gui/common/session.h"
#include "gui/common/alert.h"
#include "file_lcl.h"
#include "meta.h"
#include "util.h"



#define SND "-:"



/**
 * skip a delimiter
 * @return the first character after the delimiter (or \\0 for *x==end)
 * @param x    the current position (updated)
 * @param end  the end of the string
 */
char *skip(char **x,char *end) {
  char *p=*x;
  while((p<end)&&(isspace(*p)||(strchr(SND,*p)!=NULL)))
    p++;
  *x=p;
  return p; }



/**
 * check whether path is generic or a potential album-title
 * @return TRUE or FALSE
 * @param at  the potential album-title
 */
int is_reasonable_album_title(char *at) {
  char *uat[]={ "incoming", "tmp", "temp", "here", "new",
                "BT", "donkey", "gnutella", "ft", "fasttrack" };
  int n=sizeof(uat)/sizeof(char *),i;

  for(i=0;i<n;i++) {
    if(!strcasecmp(uat[i],at))
      return FALSE; }
  return TRUE; }



/**
 * context-menu action: derive file-name from ID3/Vorbis-tag
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  noir_sess *ns=noir_sess_get_by_serial(serial);
  int        ret=NOIR_SUCC;
  char      *buf=NULL,
            *msg=NULL,
            *fn,*path=NULL,
            *p,*e,
            *artist=NULL,*title=NULL,*album=NULL,*ext=NULL;

  unsigned long int trackno=0,tn;

  if(!ac||!action_context_get_cwd(ac))
    return NOIR_NODATA;

  /* copy file-name */
  if(!(buf=strdup(filename)))
    return NOIR_OOM;

  /* skip path */
  p=(char *)file_name(buf);
  if(!p||!strlen(p)) {       /* failed, no file */
    ret=NOIR_FAIL;
    goto done; }

  if(file_if_nested(p)) {    /* absolute path: file was "/" */
    ret=NOIR_IGNORED;
    goto done; }

  if(p!=buf) {               /* nested path */
    if(!(path=file_dirname(buf))) {
      ret=NOIR_OOM;
      goto done; }}



  fn=p;

  if(!(ext=strrchr(p,'.')))
    ext=p+strlen(p);



  /* '_'->' ' normalization */
  while(p<ext) {
    if(*p=='_')
      *p=' ';
    p++; }
  p=fn;



  /* skip leading trash */
  skip(&p,ext);



  /* find track no. if any */
  if((trackno=strtoul(p,&e,10))||(p!=e)) {
    p=e;
    if(!*e) {  /* track no. was last element in name!? */
      ret=NOIR_NODATA;
      goto done; }

    /* skip delim after track no. */
    skip(&p,ext); }



  /* find out whether we have 2 elements (artist + title) or just 1 (title) */
  if(!(e=strstr(p," - "))) {
    e=p;
    while((e<ext)&&(strchr(SND,*e)==NULL))
      e++; }

  if(e<ext) {  /* found two elements (artist + title) */
    artist=p;
    do {
      e--;
    } while((e>fn)&&isspace(*e));
    *(++e)='\0';
    e++;
    skip(&e,ext);
    title=e;

    if(!trackno) {
      p=e;
      if((tn=strtoul(p,&e,10))||(p!=e)) {
        p=e;
        if(*e) {
          trackno=tn;
          skip(&e,ext);
          title=e; }}}}
  else  /* found only one element (title) */
    title=p;



  /* try to get album- and/or band-name from path if it's not in file-name */
  if(path) {
    char *x=(char *)file_name(path);
    if(x) {
      if(x>path)
        *(x-1)='\0';

      if(is_reasonable_album_title(x)) {
        if(!album)
          album=x;
        if((x>path)&&(x=(char *)file_name(path))) {
          if(x>path)
            x[-1]='\0';
          if(!artist)
            artist=x; }}}}



  *(ext++)='\0';



  /* normalize strings */
  for(p=ext;*p;p++)
    *p=tolower(*p);
  if(album)
    *album=toupper(*album);
  if(artist)
    *artist=toupper(*artist);
  if(title)
    *title=toupper(*title);



  /* we've got our proposal. ask for permission to go ahead with it. */
  if(album) {
    if(trackno)
      esprintf(&msg,"Tag thus?\n"
                  "\"%s\" by %s,\ntrack #%d on \"%s\""
               ,title?title:"",artist?artist:"",(int)trackno,album);
    else
      esprintf(&msg,"Tag thus?\n"
                  "\"%s\" by %s,\nfrom \"%s\""
               ,title?title:"",artist?artist:"",album); }
  else
    esprintf(&msg,"Tag thus?\n"
                "\"%s\" by %s"
             ,title?title:"",artist?artist:"");

  if(!msg) {
    ret=NOIR_OOM;
    goto done; }

  if(form_yes_no_all(ns,msg,ac)==NOIR_SUCC) {
    noir_meta *mt,*mn,*mw,*ma;
    char      *tnb=NULL;

    if(trackno)
      esprintf(&tnb,"%02d",(int)trackno);

    mt=mn=mw=ma=NULL;

    if(!(mn=meta_in_group(md,"track #",NULL)))
      mn=meta_in_group(md,"tracknumber",NULL);
    mw=meta_in_group(md,"artist",NULL);
    mt=meta_in_group(md,"title",NULL);
    ma=meta_in_group(md,"album",NULL);

    if(mn&&tnb)
      meta_chg(mn,META_DO,META_OLD,meta_get_key(mn),tnb);
    if(mw&&artist)
      meta_chg(mw,META_DO,META_OLD,meta_get_key(mw),artist);
    if(mt&&title)
      meta_chg(mt,META_DO,META_OLD,meta_get_key(mt),title);
    if(ma&&album)
      meta_chg(ma,META_DO,META_OLD,meta_get_key(ma),album);

    if(tnb)
      free(tnb);

    ret=meta_update(md,filename);

    if((debug>0)||(ret!=NOIR_SUCC)) {
      tnb=artist=title=album=NULL;
      if(mn) tnb   =meta_get_val(mn);
      if(mw) artist=meta_get_val(mw);
      if(mt) title =meta_get_val(mt);
      if(ma) album =meta_get_val(ma);
      E(0,"tagging \"%s\" (|#%s|%s| by |%s| on |%s| .%s => %d) %s.\n",
	filename,tnb?tnb:"",title?title:"",artist?artist:"",album?album:"",ext?ext:"",ret,(ret==NOIR_SUCC)?"succeeded":"failed"); }}
  else
    ret=NOIR_IGNORED;

  free(msg);

 done:
  if(buf)
    free(buf);
  if(path)
    free(path);

  return ret; }



/******************************************************************************/
