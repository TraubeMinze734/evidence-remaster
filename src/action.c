/** @file  action.c
 *  @brief selections
 */



#include "action.h"
#include "errors.h"
#include "gui/common/session.h"

#include <stdlib.h>
#include <string.h>



/** a file in an action-context
 */
struct _action_file {
  char                *tag;   /**< the file's tag (path, UUID, URL, ...) */
  noir_md             *md;    /**< the file's meta-data */
  struct _action_file *next;  /**< next file in list (for selection only) */
};


/** an action-context for context-menu ("right click") actions
 */
struct _action_context {
  action_flags  flags;        /**< flags */
  char         *cwd;          /**< directory the selected files live in */
  action_file  *selection;    /**< selection (linked list), if any */
  action_file  *single;       /**< file that was right-clicked on */
  long          serial;       /**< serial no. of the session */
  void         *data;         /**< user-data */
};



/******************************************************************************/
/* context-menu helpers */



/**
 * destroy a file-entry in a menu-action context
 * @param af  the file-entry
 */
action_file *action_file_dst(action_file *af) {
  if(af) {
    if(af->tag)
      free(af->tag);
    if(af->md)
      meta_dst(&af->md);
    free(af); }
  return NULL; }

action_file *action_file_dst_all(action_file *af) {
  action_file *a2;
  while(af) {
    a2=af->next;
    action_file_dst(af);
    af=a2; }
  return NULL; }

action_file *action_file_get_next(action_file *af) {
  return af?af->next:NULL; }

action_file *action_file_set_next(action_file *af,action_file *n) {
  action_file *a2=NULL;
  if(af) {
    a2=af->next;
    af->next=n; }
  return a2; }

action_file *action_file_get_by_tag(action_file *af,const char *tag) {
  if(tag) {
    while(af) {
      if(!strcmp(tag,af->tag))
        return af;
      af=af->next; }}
  return NULL; }

const char *action_file_get_tag(action_file *af) {
  return af?af->tag:NULL; }


noir_md *action_file_get_meta(action_file *af) {
  return af?af->md:NULL; }

int action_file_set_meta(action_file *af,noir_md *m) {
  if(!af)
    return NOIR_FAIL;
  meta_dst(&af->md);
  af->md=m;
  return NOIR_SUCC; }



action_file *action_context_remove_file(action_context *ac,action_file *af) {
  action_file *a2;
  if(!af)
    return NULL;
  if(ac->selection==af) {
    ac->selection=af->next;
    af->next=NULL;
    return ac->selection; }
  a2=ac->selection;
  while(a2) {
    if(a2->next==af) {
      a2->next=af->next;
      af->next=NULL;
      return a2; }
    a2=a2->next; }
  return NULL; }



action_file *action_context_remove_and_dst_file(action_context *ac,action_file *af) {
  action_file *a2;
  if(!ac||!af)
    return NULL;
  a2=action_context_remove_file(ac,af);
  action_file_dst(af);
  return a2; }



#if 0
action_file *action_context_remove_and_dst_file_by_tag(action_context *ac,char *tag) {
  if(!ac||!tag)
    return NULL;
  return action_context_remove_and_dst_file(ac,action_file_get_by_tag(ac->selection,tag)); }
#endif



/**
 * add a file-entry to a menu-action context
 * @param ac   an action context to add to, or NULL
 * @param tag  the file's name
 */
action_file *action_file_new(action_context *ac,const char *tag) {
  action_file *af;
  if((af=malloc(sizeof(action_file)))) {
    memset(af,0,sizeof(action_file));
    if(!(af->tag=strdup(tag))) {
      free(af);
      goto done; }
    if(ac) {
      action_file *ar=ac->selection;
      if(!ar)
        ac->selection=af;
      else {
        while(ar) {
          if(!strcmp(ar->tag,tag))
            goto dupe;
          if(!ar->next) {
            ar->next=af;
            goto done; }
          ar=ar->next; }}}}
 done:
  return af;
 dupe:
  if(af) {
    if(af->tag)
      free(af->tag);
    free(af); }
  return NULL; }



/**
 * destroy an action-context
 * @param ac  the action-context to destroy
 */
void action_context_dst(action_context *ac) {
  action_file *af;
  if(ac) {
    if(ac->cwd)
      free(ac->cwd);
    action_file_dst(ac->single);
    while((af=ac->selection)) {
      ac->selection=ac->selection->next;
      action_file_dst(af); }
    free(ac); }}


action_file *action_context_get_selection(action_context *ac) {
  return ac?ac->selection:NULL; }

action_file *action_context_set_selection(action_context *ac,action_file *af) {
  action_file *a2=NULL;
  if(ac) {
    a2=ac->selection;
    ac->selection=af; }
  return a2; }



action_file *action_context_selection_contains(action_context *ac,const char *tag) {
  return ac?action_file_get_by_tag(ac->selection,tag):NULL; }

action_file *action_context_contains(action_context *ac,const char *tag) {
  if(!ac||!tag)
    return NULL;
  if(ac->single&&!strcmp(ac->single->tag,tag))
    return ac->single;
  return action_file_get_by_tag(ac->selection,tag); }



const char *action_context_get_cwd(action_context *ac) {
  return ac?ac->cwd:NULL; }

int action_context_set_cwd(action_context *ac,const char *cwd) {
  if(ac) {
    if(ac->cwd) {
      free(ac->cwd);
      ac->cwd=NULL; }
    if(cwd&&!(ac->cwd=strdup(cwd)))
      return NOIR_OOM;
    return NOIR_SUCC; }
  return NOIR_FAIL; }


action_file *action_context_set_single(action_context *ac,action_file *af) {
  if(ac) {
    if(ac->single)
      action_file_dst(ac->single);
    return ac->single=af; }
  return NULL; }

action_file *action_context_get_single(action_context *ac) {
  return ac?ac->single:NULL; }

action_flags action_context_get_flags(action_context *ac) {
  return ac?ac->flags:0; }

action_flags action_context_tst_flags(action_context *ac,action_flags af) {
  return ac?(ac->flags&af):0; }

action_flags action_context_set_flags(action_context *ac,action_flags af) {
  action_flags o=0;
  if(ac) {
    o=ac->flags;
    ac->flags=af; }
  return o; }

action_flags action_context_add_flags(action_context *ac,action_flags af) {
  if(ac)
    return ac->flags|=af;
  return 0; }

action_flags action_context_clr_flags(action_context *ac,action_flags af) {
  if(ac)
    return ac->flags&=(~af);
  return 0; }


long action_context_get_serial(action_context *ac) {
  return ac?ac->serial:-1; }

long action_context_get_seletion_size(action_context *ac) {
  long         ret=0;
  action_file *af;
  if(!ac)
    return -1;
  for(af=ac->selection;af;af=af->next)
    ret++;
  return ret; }



/**
 * create a new action-context
 * @return an action-context. serial is that of the calling session.
 * @param serial  the serial of the calling session
 * @param cwd     the work-directory
 */
action_context *action_context_new(long serial,const char *cwd) {
  action_context *ac;
  if(!(ac=malloc(sizeof(action_context))))
    return NULL;
  memset(ac,0,(sizeof(action_context)));
  ac->serial=serial;
  ac->flags=AF_LAST; /* assuming only one entry (then it'll be the First and Last) */
  if(cwd&&!(ac->cwd=strdup(cwd))) {
    free(ac);
    return NULL; }
  return ac; }



action_context *action_context_new2(const char *src,const char *tgt) {
  action_context *ac=action_context_new(0,NULL);
  action_file    *af;
  if(ac) {
    if(src&&!action_file_new(ac,src))
      goto fail;
    if(tgt) {
      if(!(af=action_file_new(NULL,tgt)))
        goto fail;
      else
        ac->single=af; }}
  return ac;
 fail:
  if(ac)
    action_context_dst(ac);
  return NULL; }



void *action_context_get_data(action_context *ac) {
  return ac?ac->data:NULL; }

void *action_context_set_data(action_context *ac,void *data) {
  void *r=NULL;
  if(ac) {
    r=ac->data;
    ac->data=data; }
  return r; }

action_context *action_context_clone(action_context *ac) {
  action_context *an;
  action_file    *af,*at;

  if(!ac||!(an=action_context_new(ac->serial,ac->cwd)))
    return NULL;

  an->flags=ac->flags;
  an->data=ac->data;

  for(af=ac->selection;af;af=af->next) {
    if((at=action_file_new(an,af->tag))) {
      if(af->md)
        ;  /* copy MD */
    }
    else
      goto fail; }

  if(ac->single) {
    if((at=action_file_new(NULL,ac->single->tag))) {
      an->single=at;
      if(ac->single->md) {
        ;  /* copy MD */
      }}
    else
      goto fail; }

  return an;

 fail:
  action_context_dst(an);
  return NULL; }



char *action_context_files_to_quoted_string(action_context *ac) {
  action_file *af;
  size_t       l=0;
  char        *p,*r=NULL,*w;

  if(!ac)
    return strdup("");

  for(af=ac->selection;af;af=af->next) {
    l+=strlen(af->tag)+3; /* "bla"_ */
    for(p=af->tag-1;(p=strchr(p+1,'"'));)
      l++; }

  if(!l)
    return strdup("");

  if((r=malloc(l))) {
    w=r;
    for(af=ac->selection;af;af=af->next) {
      *(w++)='"';
      if((p=strchr(af->tag,'"'))) {
        p=af->tag;
        while(*p) {
          if(*p=='"')
            *(w++)='\\';
          *(w++)=*(p++); }}
      else {
        l=strlen(af->tag);
        strncpy(w,af->tag,l);
        w+=l; }
      *(w++)='"';
      *(w++)=(af->next)?' ':'\0'; }}
  return r; }
