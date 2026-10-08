/** @file  session.c
 *  @brief sessions (windows), and views (notebook-pages within a given session-window).
 */

#include "session_private.h"
#include "typebuffer.h"    /* TB_MAX */
#include "monitors.h"      /* file_monitor_remove_all() */
#include "file.h"          /* file_exists() */

#include "gui/common/tooltip.h"

#include "gui/efl/common/theme.h"

#include <stdlib.h>

#include <pthread.h>
#include <string.h>



#include "model_private.h"



noir_sess *sa=NULL;        /**< session anchor, co-used from evidence.c */



/*****************************************************************************/



/**
 * get the view object for a certain view in a given session
 * @return the view-object, or NULL
 * @param ns   the session/window
 * @param tag  the name/tag of the view
 */
client_funs *noir_sess_client_get(noir_sess *ns,const char *tag) {
  client_funs *cf;
  if(!ns||!tag)
    return NULL;
  for(cf=ns->clients;cf;cf=(client_funs *)cf->user1) {
    if(!strcmp(cf->name,tag))
      return cf; }
  return NULL; }



/**
 * get user-data from the view-object for a certain view in a given session.
 * the user-data usually hold administrative struct (browser_view *, icon_view *, ...).
 * @return the user-data, or NULL
 * @param ns   the session/window
 * @param tag  the name/tag of the view
 */
void *noir_sess_client_get_data(noir_sess *ns,const char *tag) {
  client_funs *cf=noir_sess_client_get(ns,tag);
  if(cf) {
 /* E(0,"sess-%ld %s\t %p\n",ns->serial,cf->name,cf->data); */
    return cf->data; }
  return NULL; }



/**
 * register a view with both the session and the model.
 * @return the new view object, or NULL
 * @param ns    the session to register in (will become part of ns->clients chain)
 * @param name  the name-tag for the view
 * @param data  user-data (usually administrative struct, browser_view *, icon_view *, ...)
 */
client_funs *noir_sess_client_register(noir_sess *ns,const char *name,void *data) {
  client_funs *cf=NULL;
  if(!ns||(!(cf=client_register(name,data))))
    return NULL;
  cf->user1=(void *)ns->clients;
  cf->user2=(void *)ns;
  cf->flags|=CF_PAUSED;
  ns->clients=cf;
  return cf; }



/**
 * register a view with both the session and the model and set callbacks.
 * @return the new view object, or NULL
 * @param ns             the session to register in (will become part of ns->clients chain)
 * @param name           the name-tag for the view
 * @param data           user-data (usually administrative struct, browser_view *, icon_view *, ...)
 * @param evt_view       what to call when the contents of the model change (new file, ...)
 * @param dst_data       when this entry is destroyed, call this function to release user-data
 * @param set_selection  what to call to set ns->selection on the view
 */
client_funs *noir_sess_client_register_full(noir_sess *ns,const char *name,void *data,
                                            int (*evt_view)(file_info *,model_event type,file_flags changed,void *),
                                            int (*dst_data)(void **),
                                            int (*set_selection)(void *)) {
  client_funs *cf=NULL;
  if(!ns||(!(cf=client_register_full(name,data,evt_view,dst_data,set_selection))))
    return NULL;
  cf->user1=(void *)ns->clients;
  cf->user2=(void *)ns;
  cf->flags|=CF_PAUSED;
  ns->clients=cf;
  return cf; }



/**
 * remove a view from a session.
 * @return NOIR_SUCC (done), NOIR_FAIL (einval), NOIR_NOTFOUND (unknown view)
 * @param ns  the session to remove the view from
 * @param cf  the view-object
 */
int noir_sess_client_unregister(noir_sess *ns,client_funs *cf) {
  if(!ns||!cf)
    return NOIR_FAIL;

  /* remove from per-session chain of views */
  if(ns->clients==cf)
    ns->clients=(client_funs *)cf->user1;
  else {
    client_funs *r=ns->clients,*p=NULL;
    while(r&&(r!=cf)) {
      p=r;
      r=(client_funs *)r->user1; }
    if(!r)
      return NOIR_NOTFOUND;
    p->user1=r->user1; }

  E(4,"releasing view \"%s\" in session %ld...\n",cf->name,ns->serial);
  cf->user1=NULL;  /* paranoia */
  client_unregister(cf);
  return NOIR_SUCC; }



/**
 * propagate an event to all of a given session's views
 * @return the count of views who received this event
 * @param ns     the session whose views to inform
 * @param fi     the file this event is about
 * @param etype  the event type (changed, deleted, ...)
 * @param eval   the event's flags or value
 */
int noir_sess_tell_clients(noir_sess *ns,void *fi,int etype,int eval) {
  int          c=0;
  client_funs *cf=ns->clients;

  while(cf) {
    if(!CLIENT_PAUSED(cf)) {
      c++;
      E(2,"Sending %s-%ld event %d/$%x \"%s\"...\n",cf->name,ns->serial,etype,eval,(fi&&((file_info *)fi)->tag)?((file_info *)fi)->tag:"???");
      cf->evt_view(fi,etype,eval,cf->data); }
    cf=cf->user1; }
  return c; }



/**
 * get a view object and lock it.
 * @return a view-object, or NULL
 * @param serial  the serial number of the session to acquire
 * @param tag     the name of the view
 */
client_funs *noir_sess_client_lock(long serial,const char *tag) {
  noir_sess   *ns;
  client_funs *c=NULL;

  client_chain_lock(__FUNCTION__);
  if((ns=noir_sess_get_by_serial(serial))&&(c=noir_sess_client_get(ns,tag)))
    client_lock(c,__FUNCTION__);
  client_chain_unlock(__FUNCTION__);
  return c; }



/**
 * release a lock on a view object
 * @param c the view-object
 */
void noir_sess_client_unlock(client_funs *c) {
  client_chain_lock(__FUNCTION__);
  if(c)
    client_unlock(c,__FUNCTION__);
  client_chain_unlock(__FUNCTION__); }






/*****************************************************************************/



/**
 * create a new session struct
 */
noir_sess *noir_sess_new(void) {
  static long  ss=0; /* session serial */
  noir_sess   *nw=malloc(sizeof(noir_sess));

  if(nw) {
    memset(nw,0,sizeof(noir_sess));

    if(noir_sess_uidata_new(nw)!=NOIR_SUCC) {
      free(nw);
      return NULL; }

    if(!(nw->theme=theme_new(nw))) {
      noir_sess_set_uidata(nw,NULL);
      free(nw);
      return NULL; }

    tooltip_init(nw);

    if(typebuffer_new(nw)==NOIR_SUCC) {
      nw->serial=++ss;
      nw->sortflags=SORT_DIRS1ST|SORT_BANDNAMES|SORT_CASEFOLD|SORT_BYMIME;
      nw->next=sa;
      sa=nw;
      return nw; }
    free(nw); }
  return NULL; }



/**
 * copy session struct 's' to struct 'd', a proto-struct obtained with noir_sess_new().
 * 'd' will retain the serial issued by noir_sess_new() rather than duplicate
 * that of 's'.
 * @return the new session
 * @param d  buffer for the destination-session
 * @param s  the source-session
 */
noir_sess *noir_sess_copy(noir_sess *d,noir_sess *s) {
  long    ss=d->serial;    /* save serial of destination */

  d->sortflags             =s->sortflags;
  d->thumbnails_in_iconlist=s->thumbnails_in_iconlist;
  d->thumbnails_in_shelf   =s->thumbnails_in_shelf;
  d->without_notebook      =s->without_notebook;
  d->without_shelf         =s->without_shelf;
  d->without_decorations   =s->without_decorations;
  d->with_tooltips         =s->with_tooltips;
  d->start_in              =s->start_in;
  d->icon_view_last_tooltip=s->icon_view_last_tooltip;

  d->sema_focus=0;
  d->fullscreen=0;

  if(s->geometry)          d->geometry=strdup(s->geometry);
  if(s->cwd)               d->cwd=strdup(s->cwd);
  if(s->iconset)           d->iconset=strdup(s->iconset);
  if(s->cfg_file)          d->cfg_file=strdup(s->cfg_file);

  d->clients=NULL;

  d->serial=ss;            /* must not duplication serial no */

  return d; }



/**
 * free memory associated with a session.
 * session is expected to already have been removed from session-list.
 * if the session still has views ("clients"), these will be removed.
 * called from noir_sess_drop().
 * @param ns the session to remove
 */
static void noir_sess_free(noir_sess *ns) {
  if(!ns)
    return;

  E(1,"(%p): %ld-\"%s\"\n",ns,ns->serial,ns->cwd?ns->cwd:"");

  /* remove idle-handlers */
  theme_idle_destall(ns);

  /* remove views */
  while(ns->clients)
    noir_sess_client_unregister(ns,ns->clients);

  /* remove monitors */
  file_monitor_remove_all(ns);

  /* negate serial */
  ns->serial=-ns->serial;

  /* release, ooh, all kinds of stuff */

  noir_sess_uidata_free(ns);
  ns->uidata=NULL;

  tooltip_exit(ns);

  typebuffer_dst(ns);

  theme_exit(ns);

  if(ns->cwd)              free(ns->cwd);

  if(ns->drag_files)       action_context_dst(ns->drag_files);
  if(ns->selection)        action_context_dst(ns->selection);

  ns->next=NULL;

  memset(ns,0,sizeof(noir_sess));

  free(ns); }



/**
 * drop the session associated with a window. hook this into the "window-destroyed" callback of your toolkit.
 *
 * lock the session-chain's lock, find the session, remove it from the chain,
 * then call noir_sess_free() on it. unlock.
 *
 * called from evidence::closed().
 * @return an error-code
 * @param ns the session to drop
 */
int noir_sess_drop(noir_sess *ns) {
  int ret=NOIR_PARTIAL;

  client_chain_lock(__FUNCTION__);

  if(ns==sa) {
    if(!(sa=ns->next))   /* no more elements?  flag in return-code */
      ret=NOIR_SUCC; }
  else {
    noir_sess *s=sa;
    while(s&&(s->next!=ns))
      s=s->next;
    if(s)
      s->next=ns->next;
    else {
      E(0,"trying to destroy session that is not in session list!?\n");
      ret=NOIR_NOTFOUND; }}

  client_chain_unlock(__FUNCTION__);

  /* this calls client_unregister(), so we must drop the session-chain lock
     first!! */
  if((ret==NOIR_SUCC)||(ret==NOIR_PARTIAL))
    noir_sess_free(ns);
  return ret; }






/*****************************************************************************/



/**
 * get first session in session-chain.  (session-anchor is read-only)
 */
noir_sess *noir_sess_get_1st(void) {
  return sa; }



/**
 * get next session in chain
 * @return succeeding session
 * @param ns  current session
 */
noir_sess *noir_sess_get_next(noir_sess *ns) {
  return ns?ns->next:NULL; }



/**
 * find session that has 'serial'
 * @return the session, or NULL
 * @param serial  the serial-no to look for
 */
noir_sess *noir_sess_get_by_serial(long serial) {
  noir_sess *ns=sa;

  if(serial<0)
    return NULL;
  else if(serial==0) {
    noir_sess *r=sa;
#ifdef HAVE_X_
    while(ns) {
      if(ns->focused>r->focused)
        r=ns;
      ns=ns->next; }
#endif
    return r; }

  while(ns) {
    if(ns->serial==serial)
      return ns;
    ns=ns->next; }
  return NULL; }



/**
 * find (first) session that has 'dir' as current working directory
 * @return the session, or NULL
 * @param dir  the working directory to look for
 */
noir_sess *noir_sess_get_by_cwd(const char *dir) {
  noir_sess *ns=sa;

  while(ns) {
    if(ns->cwd&&!strcmp(ns->cwd,dir))
      return ns;
    ns=ns->next; }
  return NULL; }



/**
 * get serial number of a session
 * @return the serial
 * @param ns  the session
 */
long noir_sess_get_serial(noir_sess *ns) {
  return ns?ns->serial:-1; }



/**
 * get current working directory of a session
 * @return the working directory
 * @param ns  the session
 */
char *noir_sess_get_cwd(noir_sess *ns) {
  return ns?ns->cwd:NULL; }



/**
 * set current working directory of a session (in data only)
 * this does not actually change the directory shown in the view; the front-end
 * should never call this!
 * @return an error-code
 * @param ns   the session
 * @param cwd  the working directory
 */
int noir_sess_set_cwd(noir_sess *ns,const char *cwd) {
  if(!ns)
    return NOIR_FAIL;
  if(ns->cwd)
    free(ns->cwd);
  ns->cwd=NULL;
  if(cwd) {
    if(!(ns->cwd=strdup(cwd)))
      return NOIR_OOM; }
  return NOIR_SUCC; }



/**
 * get typebuffer of a session
 * @return the typebuffer
 * @param ns  the session
 */
typebuffer *noir_sess_get_typebuffer(noir_sess *ns) {
  return ns?ns->tb:NULL; }



/**
 * set typebuffer of a session
 * @return an error-code
 * @param ns  the session
 * @param tb  the typebuffer
 */
int noir_sess_set_typebuffer(noir_sess *ns,typebuffer *tb) {
  if(!ns) return NOIR_FAIL;
  ns->tb=tb;
  return NOIR_SUCC; }



/**
 * get current selection of a session
 * @return the selection
 * @param ns  the session
 */
action_context *noir_sess_get_selection(noir_sess *ns) {
  return ns?ns->selection:NULL; }



/**
 * set current selection of a session
 * @return an error-code
 * @param ns   the session
 * @param sel  the selection
 */
int noir_sess_set_selection(noir_sess *ns,action_context *sel) {
  if(!ns)
    return NOIR_FAIL;

  if(ns->selection)
    action_context_dst(ns->selection);

  ns->selection=sel;

  return NOIR_SUCC; }



/**
 * get theme of a session
 * @return the theme
 * @param ns  the session
 */
void *noir_sess_get_theme(noir_sess *ns) {
  return ns?ns->theme:NULL; }



/**
 * get theme-props of a session (this should possibly go into theme.c mid-term)
 * @return the theme
 * @param ns  the session
 */
void *noir_sess_get_theme_props(noir_sess *ns) {
  return (ns&&ns->theme)?((noir_theme *)ns->theme)->data:NULL; }



/**
 * set theme of a session
 * @return an error-code
 * @param ns  the session
 * @param t   the theme
 */
int noir_sess_set_theme(noir_sess *ns,void *t) {
  if(!ns) return NOIR_FAIL;
  ns->theme=t;
  return NOIR_SUCC; }



/**
 * desktopp
 * @return the theme
 * @param ns  the *address of* the session's sortflags
 */
int noir_sess_get_desktop(noir_sess *ns) {
  return ns?ns->fullscreen:-1; }



/**
 * get sortflags of a session
 * @return the theme
 * @param ns  the *address of* the session's sortflags
 */
void *noir_sess_get_sortflags(noir_sess *ns) {
  return ns?&ns->sortflags:NULL; }



/**
 * set sortflags of a session
 * @return an error-code
 * @param ns  the session
 * @param sf  new sort-flags
 */
int noir_sess_set_sortflags(noir_sess *ns,int sf) {
  if(!ns) return NOIR_FAIL;
  ns->sortflags=sf;
  return NOIR_SUCC; }



/**
 * get UI-specific struct (widgets et al.)
 * @return a pointer to the struct, or NULL
 * @param ns  the session
 */
void *noir_sess_get_uidata(noir_sess *ns) {
  return ns?ns->uidata:NULL; }



/**
 * set UI-specific struct (widgets et al.)
 * @param ns  the session
 * @param w    a pointer to the struct, or NULL
 */
int noir_sess_set_uidata(noir_sess *ns,void *w) {
  if(!ns)
    return NOIR_FAIL;
  noir_sess_uidata_free(ns);
  ns->uidata=w;
  return NOIR_SUCC; }



/**
 * get UI-specific struct (widgets et al.)
 * @return a pointer to the struct, or NULL
 * @param ns  the session
 */
tooltip_flags noir_sess_get_tooltip_flags(noir_sess *ns) {
  return ns?ns->with_tooltips:TOOLTIP_DISABLE; }



/**
 * set tooltip flags
 * @param ns  the session
 * @param f   the flags to set
 */
int noir_sess_set_tooltip_flags(noir_sess *ns,tooltip_flags f) {
  if(!ns)
    return NOIR_FAIL;
  ns->with_tooltips=f;
  return NOIR_SUCC; }



noir_tooltip *noir_sess_get_tooltip(noir_sess *ns) {
  return ns?ns->tooltip:NULL; }



int noir_sess_set_tooltip(noir_sess *ns,noir_tooltip *tt) {
  if(!ns)
    return NOIR_NODATA;
  ns->tooltip=tt;
  return NOIR_SUCC; }



/*****************************************************************************/
