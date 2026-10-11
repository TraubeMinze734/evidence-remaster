/** @file  typebuffer.c
 *  @brief handle "typebuffer" (micro-shell)
 */

#include "config.h"

#include "session_private.h"

#include "mime.h"        /* MIME_* */
#include "file.h"        /* file_fork(), ... */
#include "util.h"        /* util_parse_line(), hashes */
#include "setup.h"       /* get_setup_file(), get_parse_file(),
                            replace_variables() */
#include "alert.h"       /* form_alert(), form_yes_no() */
#include "typebuffer.h"

#include "file_lcl.h"

#include "gui/gui.h"     /* gui_quit() */
#include "gui/efl/common/theme.h"

#ifdef HAVE_EVAS2
#  include "icon_list.h"
#  include "gui/gtk2/icon_view/gevas_bind.h"         /* ICON_LIST_GET_FROM_WIDGET() */
#  include "gui/efl/icon_view/evas_bind.h"
#endif

#include "model_private.h"  /* ### FIXME   DIR_DELIMITER DIR_DELIM_STR DIR_PARENT DIR_MYSELF */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

#ifdef HAVE_DOODLE
#  include "search.h"
#endif

#ifdef HAVE_PCRE
#  include <pcre.h>
#  define RE_ELS  30
#endif



/****************************************************************************/



#define WINDOW_TYPEBUFFER "Enter a command or hit Escape to cancel"

#define TB_MAX   4096
#define HIST_MAX  128



static void *ht_handler=NULL;

typedef enum { HIST_GENERIC, HIST_URL } hist_type;

typedef struct _hist_entry {
  char               *line;
  time_t              timestamp;
  hist_type           type;
  struct _hist_entry *prvs,*next;
} hist_entry;

typedef struct _history {
  hist_entry *anchor;
  int         num;
  int         refcount;
} history;



struct _typebuffer {
  size_t      tb_max;       /**< maximum input length for type-buffer */
  void       *tb_obj;       /**< evas: type-buffer object(s) */
  char       *tb_string,    /**< input-buffer for our "microShell" */
             *tb_tabbed,    /**< input-buffer last tab pattern; this is used only when the pattern actually is a glob/regex (as in a shell's tab-completion), as in this case, the resulting typebuffer (match) could not be tabbed again */
             *tb_pcre;      /**< last regex, as clear-text */
  int         tb_entered;   /**< tab-completing: how many chars were typed? */
  tb_flags    tb_flags;
  history    *tb_history;
  hist_entry *tb_histlast;
#  ifdef HAVE_PCRE
  pcre       *tb_re;        /**< typebuffer: regex */
  pcre_extra *tb_pe;        /**< typebuffer: regex */
#  endif
};



int re_init(void);
static history *history_dst(history **hi);
static int typebuffer_history_del(history *hi,hist_entry *h);



/****************************************************************************/
/* setup */



/**
 * add URL-handler
 * @param prot     the protocol ("http")
 * @param handler  the handler (string with substitutions, %u, %f, ...)
 */
static int typebuffer_add_handler(char *prot,char *handler) {
  hash_table_insert(ht_handler,strdup(prot),strdup(handler));
  return NOIR_SUCC; }



/**
 * initialize typebuffer subsystem
 * @return an error-code
 */
int typebuffer_init(void) {
  int         ret=NOIR_NOFILE;
  char       *handlers=get_setup_file(PROTOCOL_HANDLERS);

  if(handlers) {
    FILE *fh;
    char l[MIME_MAX_LEN];
    char *p,*p3;

    if((fh=fopen(handlers,"r"))) {
      E(2,"URL-handlers in type-buffer:\n");

      if(!(ht_handler=hash_table_new(free,free))) {
        ret=NOIR_OOM;
        goto close; }

      while(fgets(l,MIME_MAX_LEN,fh)) {
        if(util_parse_line(l,&p,&p3,NULL,NULL)==2) {
          E(2,"%s" PROTOCOL_DELIMITER " => %s\n",p,p3);
          typebuffer_add_handler(p,p3); }}
    close:
      fclose(fh); }
    else
      ret=NOIR_FAIL;

    free(handlers); }
  else
    E(0,"no protocol handlers found...\n");

  return ret; }



/**
 * deinitialize typebuffer subsystem
 * @return an error-code
 */
int typebuffer_exit(void) {
  return NOIR_IGNORED; }



/**
 * initialize typebuffer subsystem for given session
 * @return an error-code
 * @param ns  the session
 */
int typebuffer_new(noir_sess *ns) {
  int         ret=NOIR_SUCC;
  typebuffer *tb;

  if((tb=noir_sess_get_typebuffer(ns)))  /* "this should never happen" */
    free(tb);

  if((tb=malloc(sizeof(typebuffer))))
    memset(tb,0,sizeof(typebuffer));
  noir_sess_set_typebuffer(ns,tb);

  if(!tb)
    return NOIR_OOM;

  if((tb->tb_string=malloc((tb->tb_max=TB_MAX)+1))) {
    tb->tb_string[0]='\0';
    tb->tb_entered=-1; }

  re_init();

  return ret; }



/**
 * deinitialize typebuffer subsystem for given session
 * @return an error-code
 * @param ns  the session
 */
int typebuffer_dst(noir_sess *ns) {
  typebuffer *tb;

  if(ht_handler)
    hash_table_dst(&ht_handler);
  /* tb_data free()d in theme.c */
#if 0
  if(((noir_theme *)ns->theme)&&((noir_theme *)ns->theme)->tb_data) {
    free(((noir_theme *)ns->theme)->tb_data);
    ((noir_theme *)ns->theme)->tb_data=NULL; }
#endif

  if((tb=noir_sess_get_typebuffer(ns))) {
    if(tb->tb_string)  free(tb->tb_string);
    if(tb->tb_tabbed)  free(tb->tb_tabbed);
#ifdef HAVE_PCRE
    if(tb->tb_pcre)    free(tb->tb_pcre);
    if(tb->tb_pe)      free(tb->tb_pe);
    if(tb->tb_re)      free(tb->tb_re);
#endif
#ifdef HAVE_EVAS2
    if(tb->tb_obj) {
      noir_theme *t=noir_sess_get_theme(ns);
      if(t&&t->tb_dst)
        t->tb_dst(&tb->tb_obj);
      else
        evas_object_del(tb->tb_obj); }
#endif

    history_dst(&tb->tb_history);

    memset(tb,0,sizeof(typebuffer));
    free(tb);

    noir_sess_set_typebuffer(ns,NULL); }

  return NOIR_SUCC; }



/****************************************************************************/
/* regex */



#ifdef HAVE_PCRE

static int re_opts=PCRE_DOTALL;



/**
 * initialize regular expression matching
 */
int re_init(void) {
  int have_utf8=0;
  pcre_config(PCRE_CONFIG_UTF8,&have_utf8);
  if(have_utf8)
    re_opts|=PCRE_UTF8|PCRE_NO_UTF8_CHECK;
  else
    E(2,"PCRE (regular expression library) built without support for UTF-8, emulating it so type-to-find won't only work for the western alphabet...\n");
  return re_opts; }



/**
 * compile a regular expression
 * @return an error-code
 * @param ns      the session
 * @param re_txt  the cleartext-representation of the regex
 * @param mode    glob or RE?
 */
int re_add(noir_sess *ns,const char *re_txt,re_mode mode) {
  const char *error;
  int         erroffset,opts=re_opts;
  pcre       *re;
  pcre_extra *pe;
  char       *n;
  typebuffer *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  switch(mode) {
    case REM_GLOB: {
      size_t      l=strlen(re_txt);
      const char *p=re_txt;
      char       *q;
      while(*p) {
        if(strchr("*?.",*p))
          l++;
        l++; /* for ^ */
        p++; }
      if(!(n=malloc(l+1)))
        return NOIR_OOM;
      strcpy(n,"^");
      for(p=re_txt,q=n+1;*p;) {
        if(strchr("*?",*p))
          *(q++)='.';
        else if(*p=='.')
          *(q++)='\\';
        *(q++)=*(p++); }
      *q='\0'; }
      break;
    case REM_RE:
      if(!(n=strdup(re_txt)))
        return NOIR_OOM;
      break;
    default:
      return NOIR_NOTSUPP; }

  if(ns->sortflags&SORT_CASEFOLD)   /* if sort is case-insensitive, then so */
    opts|=PCRE_CASELESS;            /* type-to-find */

  if(opts&PCRE_UTF8) {
    char *utf8=e2utf8(n);
    free(n);
    if(!(n=utf8))
      return NOIR_FAIL; }

  if(!(re=pcre_compile(n,opts,&error,&erroffset,NULL))) {
    form_alert(ns,"Invalid regular expression \"%s\".\nPerl-compatible syntax applies.",n);
    free(n);
    return NOIR_FAIL; }

  pe=pcre_study(re,0,&error);

  if(tb->tb_pcre)  free(tb->tb_pcre);
  if(tb->tb_re)    free(tb->tb_re);
  if(tb->tb_pe)    free(tb->tb_pe);

  tb->tb_pcre=n;
  tb->tb_re=re;
  tb->tb_pe=pe;

  return NOIR_SUCC; }



/**
 * check whether current regex matches given file
 * @return an error-code
 * @param ns  the session
 * @param fn  the file-name
 */
int re_match(noir_sess *ns,const char *fn) {
  int         rc;
  int         ovector[RE_ELS];
  char       *lcl=NULL;
  typebuffer *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  if(!(re_opts&PCRE_UTF8)) {
    if((lcl=e2locale(fn)))
      fn=lcl; }

  rc=pcre_exec(tb->tb_re,tb->tb_pe,fn,strlen(fn),0,0,ovector,RE_ELS);

  if(lcl)
    free(lcl);

  if(rc<0) {
    switch(rc) {
    case PCRE_ERROR_NOMATCH:
      return NOIR_NOTFOUND;
      break;
    default:
      E(0,"error %d...\n",rc);
      return NOIR_FAIL; }}

  return NOIR_SUCC; }



#else
int re_init(void) { return NOIR_NOTSUPP; }
#endif
int re_exit(void) { return NOIR_IGNORED; }






/****************************************************************************/
/* display */



int typebuffer_is_open(noir_sess *ns) {
  typebuffer *tb;
  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return FALSE;
  return (tb->tb_flags&TB_OPEN); }



tb_flags typebuffer_get_flags(noir_sess *ns) {
  typebuffer *tb;
  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return 0;
  return tb->tb_flags; }



int typebuffer_set_flags(noir_sess *ns,tb_flags fl) {
  typebuffer *tb;
  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;
  tb->tb_flags=fl;
  return NOIR_SUCC; }



int typebuffer_add_flags(noir_sess *ns,tb_flags fl) {
  typebuffer *tb;
  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;
  tb->tb_flags|=fl;
  return NOIR_SUCC; }



int typebuffer_clr_flags(noir_sess *ns,tb_flags fl) {
  typebuffer *tb;
  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;
  tb->tb_flags&=(~fl);
  return NOIR_SUCC; }



const char *typebuffer_get_tab(noir_sess *ns) {
  typebuffer *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NULL;
  if(!(tb->tb_flags&TB_OPEN))
    return NULL;
  if(!tb->tb_tabbed) {
    tb->tb_tabbed=strdup("");
    tb->tb_entered=0; }
  return tb->tb_tabbed; }



int typebuffer_set_tab(noir_sess *ns,const char *str) {
  typebuffer *tb;
  int         ret=NOIR_SUCC;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  if(tb->tb_tabbed)
    free(tb->tb_tabbed);
  if(!str)
    tb->tb_tabbed=NULL;
  else
    tb->tb_tabbed=strdup(str);

  return ret; }



const char *typebuffer_get(noir_sess *ns) {
  typebuffer *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NULL;
  if(!(tb->tb_flags&TB_OPEN))
    return NULL;
  return tb->tb_string; }



int typebuffer_set(noir_sess *ns,const char *str) {
  typebuffer *tb;
  int         ret=NOIR_SUCC;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  if(!str)
    tb->tb_string[0]='\0';
  else if(strlen(str)>=tb->tb_max) {
    size_t  l=strlen(str)+TB_MAX;
    char   *nb=malloc(l+1);
    if(!nb) {
      tb->tb_string[0]='\0';
      ret=NOIR_OOM; }
    else {
      tb->tb_max=l;
      free(tb->tb_string);
      tb->tb_string=nb; }}
  else
    strcpy(tb->tb_string,str);

  return ret; }



/**
 * display current contents of a session's typebuffer
 * @param ns the session
 */
int typebuffer_update(noir_sess *ns) {
#if defined(HAVE_EVAS2)
  noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
#endif
  char          *tmp=NULL;
  typebuffer    *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  if(tb->tb_flags&TB_OPEN) {
    char *msg=(tb->tb_string&&strlen(tb->tb_string))?tb->tb_string:WINDOW_TYPEBUFFER;
    char *utf8;

    esprintf(&tmp,"evidence shell: %s",msg);

    if(tmp) {
      if((utf8=e2utf8(tmp))) {
	noir_sess_set_window_title(ns,utf8);
        free(utf8); }
      free(tmp); }
#ifdef HAVE_EVAS2
    if((utf8=e2utf8(msg))) {
      tb->tb_obj=((noir_theme *)ns->theme)->tb_set(ICON_LIST_GET_FROM_WIDGET(iv->icon_view),tb->tb_obj,utf8,tb->tb_entered);
      free(utf8); }
#endif
  }
  else {
#ifdef HAVE_EVAS2
    /* command might have discarded theme! ("quit" etc.) */
    if(((noir_theme *)ns->theme)&&((noir_theme *)ns->theme)->tb_dst&&tb->tb_obj)
      ((noir_theme *)ns->theme)->tb_dst(&tb->tb_obj);
#endif
    esprintf(&tmp,"evidence - %s",ns->cwd);
    if(tmp) {
      noir_sess_set_window_title(ns,tmp);
      free(tmp); }}
#ifdef HAVE_EVAS2
  if(((noir_theme *)ns->theme)&&((noir_theme *)ns->theme)->tb_redraw)
    ((noir_theme *)ns->theme)->tb_redraw(ICON_LIST_GET_FROM_WIDGET(iv->icon_view));
#endif

  return NOIR_SUCC; }



/**
 * close a session's typebuffer
 * @param ns the session
 */
int typebuffer_close(noir_sess *ns) {
  typebuffer *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  tb->tb_flags&=(~(TB_OPEN|TB_RSEARCH|TB_RRSEARCH));
  tb->tb_string[0]='\0';
  typebuffer_update(ns);

  if(tb->tb_tabbed) {
    free(tb->tb_tabbed);
    tb->tb_tabbed=NULL; }

  /* if traversing the typebuffer history added an empty entry, remove it now */
  if(tb->tb_history&&tb->tb_history->anchor&&  /* ignore duplicate lines */
     !strlen(tb->tb_history->anchor->line))
    typebuffer_history_del(tb->tb_history,tb->tb_history->anchor);

  return NOIR_SUCC; }



/**
 * open a session's typebuffer
 * @param ns the session
 */
int typebuffer_open(noir_sess *ns) {
  typebuffer *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;
  if(!tb->tb_string)
    return NOIR_NODATA;

  tb->tb_flags|=TB_OPEN;
  tb->tb_flags&=(~(TB_RSEARCH|TB_RRSEARCH));
  tb->tb_string[0]='\0';

  tb->tb_histlast=NULL;

  return NOIR_SUCC; }



/****************************************************************************/
/* handle URL */



/**
 * contents assumed URL. find handler, start it with the URL.
 * @param ns the session
 * @param url the URL
 */
static int typebuffer_handle_URL(noir_sess *ns,char *url) {
  char *r,*h;
  int   ret=NOIR_SUCC;

  if(!url||!*url)
    return NOIR_NODATA;

  if(!ht_handler) {
    form_alert(ns,"No protocol-handlers registered...\nCannot handle \"%s\".",url);
    return NOIR_NOTFOUND; }

  if(!ns)
    return NOIR_FAIL;

  E(1,"\"%s\"\n",url);

  if(!(r=strstr(url,PROTOCOL_DELIMITER)))
    return NOIR_FAIL;

  *r='\0';
  h=(char *)hash_table_fetch(ht_handler,url);

  if(!h) {
    form_alert(ns,"No handler registered for protocol \"%s\"...",url);
    ret=NOIR_NOTFOUND; }

  *r=':';
  if(h)
    ret=file_run(ns,h,url,"");

  return ret; }



/****************************************************************************/
/* handle magic comma operator */



/**
 * contents assumed "'propname value"; modify theme.
 * @return an error-code
 * @param ns the session
 * @param tb the typebuffer-contents
 */

#if defined(HAVE_EVAS2)

static int typebuffer_modify_theme(noir_sess *ns,char *tb) {
  char *p=&tb[1],*k,*v;

  k=&p[strlen(p)-1];
  while(isspace(*k))                  /* remove trailing space */
    *(k--)='\0';

  while(*p&&isspace(*p))              /* skip initial space */
    p++;

  if(!*p)                             /* nothing left?  go home */
    return NOIR_IGNORED;

  k=p;
  while(*p&&(strchr(":= ",*p)==NULL))
    p++;

  if(*p) {                            /* multiple words? */
    size_t l;

    *(p++)='\0';
    while(*p&&(isspace(*p)||(*p==':')||(*p=='=')))
      p++;

    prop_canonize_key(k,TRUE,FALSE);

    if(*p&&(l=strlen(k))) {
      noir_prop     *e;
      noir_prop_bag *b=NULL;

      v=p;

      E(1,"(\"%s\",\"%s\")\n",k,v);

      if(!(e=prop_bags_prop_get(2,&b,k,props_global,
                                ((noir_theme *)ns->theme)->data))) {
        if(form_yes_no(ns,"No such entity to modify, append as new entity?")!=NOIR_SUCC)
          return NOIR_IGNORED; }
      if((l>4)&&!strcasecmp(k+l-4,"font")) {
        int ret=NOIR_FAIL;
#ifdef HAVE_EVAS2
	if(b==props_global)
          E(0,"cannot set fonts in global theme...\n");
        else if((v=theme_font_find(((noir_theme *)ns->theme),v))) {
          if(file_exists(v))
            ret=prop_bag_prop_set(((noir_theme *)ns->theme)->data,k,v);
          else
            form_alert(ns,"No such font \"%s\"...",v);
          free(v); }
#else
	form_alert(ns,"Can't set font for GNOME-icon-view...");
#endif
        return ret; }
      else if(b==props_global)
        return prop_bag_prop_set(b,k,v);
      else
        return theme_parse_line(ns,k,v); }}

  else if(!strcmp(k,"save")||!strcmp(k,"dump"))
    return theme_dump((void *)ns);

  else {
    noir_prop     *e;
    noir_prop_bag *b=NULL;

    if((v=&k[strlen(k)-1])&&(*v=='?'))
      *v='\0';

    if(!(e=prop_bags_prop_get(2,&b,k,props_global,
                              ((noir_theme *)ns->theme)->data)))
      E(0,"no such property, \"%s\".\n",k);
    else {
      char *r=prop_get_as_string(e);
      form_alert(ns,"bag \"%s\"\nkey \"%s\"\nvalue %s",
                 (b==props_global)?"global":((noir_theme *)ns->theme)->name,
                 k,
                 r?r:"<null>");
      if(r)
        free(r); }}

  return NOIR_IGNORED; }

#else

static int typebuffer_modify_theme(noir_sess *ns,char *tb) {
  return NOIR_NOTSUPP; }

#endif



/****************************************************************************/



/**
 * debug helper:  print out a single file-info
 * @return an error-code
 * @param ns the session
 * @param tb the typebuffer's contents
 */
static int typebuffer_dump_fi(noir_sess *ns,char *tb) {
  char      *n=&tb[1];
  file_info *fi=file_info_fetch(NULL,n);
  int        ret=NOIR_SUCC;

  if(fi) {
    E(0,"\"%s\" \"%s\"  $%x ($%x)...\n",file_info_get_name(fi),file_info_get_tag(fi),file_info_get_mode(fi),file_info_get_flags(fi));
/*  model_ls(fi,0); */
  }
  else {
    if(!model_find_by_name(NULL,n)) {
      E(0,"\"%s\" not found in store...\n",n);
      ret=NOIR_NOTFOUND; }}
  return ret; }



/****************************************************************************/
/* search */



/**
 * search doodle-database
 * @return an error-code
 * @param ns the session
 * @param tb the typebuffer's contents
 */
#ifdef HAVE_DOODLE
static int typebuffer_search(noir_sess *ns,char *tb) {
  char      *n=&tb[1];
  int        ret;

  E(1,"search: \"%s\"\n",n);

#define PREFIX "/Searches"

  /* SEARCH_APPROX, SEARCH_NONE */
  if((ret=search_file_by_md(NULL,PREFIX,n,SEARCH_CASEFOLD))==NOIR_SUCC) {
    char *cwd=file_compose_path(PREFIX,n,NULL);
    if(cwd) {
      ret=noir_sess_focus(ns,cwd);
      free(cwd); }
    else
      return NOIR_OOM; }
  else
    form_alert(ns,"Search for \"%s\" failed: %d",n,ret);

  return ret; }
#endif



/****************************************************************************/
/* cd */



/**
 * cd -- change working directory
 * @return an error-code
 * @param ns the session
 * @param path the new wd
 */
int typebuffer_focus(noir_sess *ns,char *path) {
  int ret=noir_sess_focus(ns,path);
  return ret; }



/****************************************************************************/
/* parser helpers */



/**
 * normalize a path (resolve /./ and /../)
 * @return              an error-code
 * @param myfile        the path to normalize
 * @param leave_single  leave in single-dot entities "/./"
 */
static int typebuffer_path_normalize(char *myfile,int leave_single) {
  /* can't use realpath because we want to keep symlinks */
  char *t=myfile;
  while((t=strstr(t,"/..")))  /* eliminate /../ */
    if((!t[3]||t[3]=='/')&&(t>myfile)) {
      char *t2=t;
      do {
        t2--;
      } while((t2>myfile)&&(*t2!='/'));
      if(*t2=='/') {
        t+=3;
        memmove(t2,t,strlen(t)+1);
        t=t2; }
      else
        t++; }
    else
      t++;

  if(!leave_single) {
    t=myfile;
    while((t=strstr(t,"/."))) { /* eliminate /./ */
      if(!t[2]||t[2]=='/')
        memmove(t,t+2,strlen(t+2));
      t++; }}

  return NOIR_SUCC; }


/****************************************************************************/
/* history */



static int typebuffer_history_dump(typebuffer *tb) {
  history    *hi;
  hist_entry *he;

  if(!tb)
    return NOIR_FAIL;

  if(!(hi=tb->tb_history)) {
    E(0,"no history.\n");
    return NOIR_NODATA; }
  else if(!hi->anchor) {
    E(0,"no anchor.\n");
    return NOIR_NODATA; }
  else {
    int c=0;
    for(he=hi->anchor;he;he=he->next)
      E(0,"%d: (%p)  %s\n",++c,he,he->line);

    E(0,"current: %s\n",tb->tb_string);
    if(tb->tb_entered>0)
      E(0,"entered: %*s\n",tb->tb_entered,"^");

    if(tb->tb_tabbed)
      E(0,"last used glob/regex: %s\n",tb->tb_tabbed);

    if(tb->tb_histlast)
      E(0,"last matching history element: %s\n",tb->tb_histlast->line);
    else
      E(0,"last matching history element: none\n"); }
  return NOIR_SUCC; }



static history *history_dst(history **hi) {
  history *h;
  if(hi&&*hi) {
    h=*hi;
    *hi=NULL;
    if(--h->refcount<=0) {
      hist_entry *e,*f;
      for(e=h->anchor;e;e=f) {
        f=e->next;
        if(e->line)
          free(e->line);
        memset(e,0,sizeof(hist_entry));
        free(e); }
      memset(h,0,sizeof(history));
      free(h); }}
  return NULL; }



/**
 * remove history-element from history (if linked) and release it
 * @return an error-code
 * @param hi  the history the element may be in, or NULL
 * @param h   the history-element
 */
static int typebuffer_history_del(history *hi,hist_entry *h) {
  if(hi&&(hi->anchor==h))
    hi->anchor=h->next;

  if(h->prvs)
    h->prvs->next=h->next;
  if(h->next)
    h->next->prvs=h->prvs;
  if(h->line)
    free(h->line);
  free(h);

  if(hi)
    hi->num--;

  return NOIR_SUCC; }



/**
 * add an item to shared typebuffer-history
 * @return an error-code
 * @param tb  the typebuffer
 * @param l   the line to add to the history
 */
static int typebuffer_history_add(typebuffer *tb,char *l) {
  hist_entry *h;
  char       *p,*q;

  if(!tb)
    return NOIR_FAIL;

  if(!l)
    return NOIR_IGNORED;

  if(tb->tb_history&&tb->tb_history->anchor&&  /* ignore duplicate lines */
     !strcmp(tb->tb_history->anchor->line,l))
    return NOIR_IGNORED;

  if(tb->tb_history&&tb->tb_history->anchor&&  /* ignore duplicate lines */
     !strlen(tb->tb_history->anchor->line))
    typebuffer_history_del(tb->tb_history,tb->tb_history->anchor);

  if(!(h=malloc(sizeof(hist_entry))))
    return NOIR_OOM;

  memset(h,0,sizeof(hist_entry));

  if(!(h->line=strdup(l))) {
    free(h);
    return NOIR_OOM; }

  h->timestamp=now(0);
  p=strchr(l,' ');
  q=strstr(l,PROTOCOL_DELIMITER);
  h->type=(q&&(!p||(p>q)))?HIST_URL:HIST_GENERIC;
  h->next=NULL;
  h->prvs=NULL;

  if(!tb->tb_history) {
    if(!(tb->tb_history=malloc(sizeof(history)))) {
      typebuffer_history_del(NULL,h);
      return NOIR_OOM; }
    memset(tb->tb_history,0,sizeof(history));
    tb->tb_history->refcount++; }

  tb->tb_history->num++;

  if(!tb->tb_history->anchor)
    tb->tb_history->anchor=h;
  else {
    h->next=tb->tb_history->anchor;
    tb->tb_history->anchor->prvs=h;
    tb->tb_history->anchor=h;

    while(tb->tb_history->num>HIST_MAX) {  /* expire old entries */
      if(tb->tb_histlast==tb->tb_history->anchor)  /* invalidate ref to element we'll delete */
        tb->tb_histlast=NULL;
      typebuffer_history_del(tb->tb_history,tb->tb_history->anchor); }}

  return NOIR_SUCC; }



/**
 * make an element from a typebuffer's history the current typebuffer-contents
 * @return an error-code
 * @param tb  the typebuffer
 * @param h   the history-element
 */
static int typebuffer_history_activate(typebuffer *tb,hist_entry *h) {
  int    ret=NOIR_SUCC;
  size_t l=strlen(h->line);

  if(l>=tb->tb_max) {
    char *n=malloc(l+TB_MAX+1);
    if(!n)
      return NOIR_OOM;
    free(tb->tb_string);
    tb->tb_string=n;
    tb->tb_max=l+TB_MAX; }
  strcpy(tb->tb_string,h->line);
  tb->tb_histlast=h;
  return ret; }



/**
 * search an element in history
 * @return an error-code
 * @param tb      the typebuffer
 * @param substr  the substring to search for, or NULL
 */
int typebuffer_history_search(typebuffer *tb,const char *substr) {
  hist_entry *h,*h2;

  if(!tb)
    return NOIR_FAIL;

  if(!tb->tb_history||!(h=tb->tb_history->anchor))
    return NOIR_NODATA;

  if(!substr||!*substr)
    substr=tb->tb_tabbed;

  if(!substr||!*substr)
    return NOIR_IGNORED;

  E(2,"searching history for \"%s\"...\n",substr);

  /* ^R ^R -- find previous match */
  if((tb->tb_flags&TB_RRSEARCH)&&tb->tb_histlast) {
    if(tb->tb_histlast->next&&
       strstr(tb->tb_histlast->next->line,substr))
      typebuffer_history_activate(tb,tb->tb_histlast->next); }

  /* ^R -- simple reverse search */
  else {
    for(h2=h;h2&&!strstr(h2->line,substr);h2=h2->next)
      ;
    if(h2)
      typebuffer_history_activate(tb,h2); }

  tb->tb_flags&=(~TB_RRSEARCH);  /* clear ^R^R /here/ */

  return NOIR_SUCC; }



/**
 * traverse a typebuffer-history (cursor-up/dn etc.)
 * @return an error-code
 * @param tb     the typebuffer
 * @param count  the number of elements (<0 older, >0 newer entries)
 */
int typebuffer_history_traverse(typebuffer *tb,int count) {
  hist_entry *h;
  int         ret=NOIR_SUCC;

  if(!tb)
    return NOIR_FAIL;
  if(!count)
    return NOIR_IGNORED;
  if(!tb->tb_history||!tb->tb_history->anchor)
    return NOIR_NODATA;

  if(debug>1)
    typebuffer_history_dump(tb);

  if(!(h=tb->tb_histlast)) {
    if((ret=typebuffer_history_add(tb,tb->tb_string))!=NOIR_SUCC)
      return ret;
    h=tb->tb_history->anchor; }

  if(count<0) {
    for(count*=(-1);h&&h->next&&(count>0);count--)
      h=h->next; }
  else if(h) {
    for(;h&&h->prvs&&(count>0);count--)
      h=h->prvs; }

  if(h)
    typebuffer_history_activate(tb,h);  /* will set tb->tb_histlast */
  else {
    tb->tb_histlast=tb->tb_history->anchor;
    ret=NOIR_NOTFOUND; }

  return ret; }



/****************************************************************************/
/* tab-completion */



/***********/
/* helpers */
/***********/

/**
 * qsort_helper: compare two entries in tab-completion array
 * @return -1,0,1 -- see qsort()
 * @param pa first entry
 * @param pb second entry
 */
static int tab_sort_fun(const void *pa, const void *pb) {
  char *a=*((char **)pa),*b=*((char **)pb);
  return strcmp(a,b); }



/**
 * find token in typebuffer in array of possible completions
 * @param ns the session
 * @param tb the token to complete
 * @param e entered -- the first e characters must match
 * @param n number of possible completions in array
 * @param s array of possible completions
 */
int tab(noir_sess *ns,char *tb,int e,int n,char **s) {
  int   i=0,l;

  l=(int)strlen(tb);

  while((i<n)&&strncmp(tb,s[i],l))
    i++;

  E(1,"looking for \"%s\"... e=%d, s[%d]=\"%s\"\n",tb,e,i,(i>=n)?"<undefined>":s[i]);

  if(i>=n)
    return NOIR_NOTFOUND;

  if(e>=0) {
    i++;
    if((i>=n)||strncmp(tb,s[i],e)) {
      i=0;
      while((i<n)&&strncmp(tb,s[i],e))
        i++; }}

  return (i<n)?i:NOIR_NOTFOUND; }



/**********************************/
/* create array of tab-completion */
/* options according to context   */
/**********************************/

/**
 * tab-complete a shell-command (complete from files in $PATH)
 * @return an error-code
 * @param ns the session
 */
static int tab_shell(noir_sess *ns) {
#define STM (2*PATH_MAX)
  int              n=0,m,ret=NOIR_IGNORED,g;
  char            *b,*e,*f=NULL,*t,*sel;
  char             np[STM+3];
  const char      *errmsg=NULL;
  char           **s=NULL,**p;
  typebuffer      *tb;
  action_context  *ac=NULL;
  action_file     *af;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  E(1,"\n");

  b=tb->tb_string;
  if(!b||!*b)
    return NOIR_NODATA;
  if(strlen(b)>=(STM))
    return NOIR_OOM;

  sel=action_context_files_to_quoted_string(noir_sess_get_selection(ns));

  /* find last token, store in t */
  for(f=NULL,e=NULL,t=b;b;) {
    if((e=util_get_token(&b,TRUE,noir_sess_get_cwd(ns),sel))&&strlen(e)) {
      if(f)
        free(f);
      f=e;
      if(b&&*b)
        t=b; }
    else
      b=NULL; }

  if(sel)
    free(sel);

  if(!f)
    return NOIR_FAIL;
  if(!t) {
    free(f);
    return NOIR_FAIL; }



  g=(file_is_globbable(f)==NOIR_SUCC);

  /* do we already have something to tab?  put it in nb! */
  if(!g&&tb->tb_tabbed) {
    strcpy(np,tb->tb_tabbed);
    if(!file_if_absolute(np))
      snprintf(np,STM,"%s/%s",noir_sess_get_cwd(ns),tb->tb_tabbed); }

  else {
    if(file_if_absolute(f))
      strcpy(np,f);
    else
      snprintf(np,STM,"%s/%s",noir_sess_get_cwd(ns),f); }

  typebuffer_path_normalize(np,FALSE);

  if(strlen(np)>=PATH_MAX) {
    ret=NOIR_FAIL;
    goto fail; }



  E(1,"glob \"%s\", last match \"%s\"\n",np,f);



  /* glob! */
  if(!(ac=file_glob(np,&errmsg))) {
    ret=NOIR_FAIL;
    goto done; }



  /* "*.h" */
  else if(g) {
    char *x=NULL,*y;
    if(!(n=action_context_get_seletion_size(ac)))
      ret=NOIR_NOFILE;
    else if((y=action_context_files_to_quoted_string(ac))) {
      *t='\0';
      esprintf(&x,"%s%s%s",tb->tb_string,strlen(tb->tb_string)>0?" ":"",y);
      if(x) {
        ret=NOIR_SUCC;
        typebuffer_set(ns,x);
        typebuffer_update(ns);
        ret=NOIR_SUCC;
        free(x); }
      else
        ret=NOIR_OOM;
      free(y); }
    else
      ret=NOIR_OOM;

    goto done; }



  /* no matches, try rewriting glob:  "/u/l/b" => "/usr/local/bin" */
  else if(!(n=action_context_get_seletion_size(ac))) {
    char *x=np,*y;
    char  tp[STM+3]="";

    while((y=strchr(x+1,DIR_DELIMITER))) {  /* find next path-segment in input */
      strncat(tp,x,y-x);
      if(!S_ISDIR(file_exists(tp))) {       /* does the path exist this far? */
        strcat(tp,"*");
        action_context_dst(ac);
        if(!(ac=file_glob(tp,&errmsg))) {
          ret=NOIR_FAIL;
          goto done; }
        else if(!(n=action_context_get_seletion_size(ac))) {
          ret=NOIR_NOTFOUND;
          goto done; }}
      x=y; }
    strcat(tp,x);
    action_context_dst(ac);
    if(!(ac=file_glob(tp,&errmsg))) {
      ret=NOIR_FAIL;
      goto done; }
    else if(!(n=action_context_get_seletion_size(ac))) {
      strcat(tp,"*");
      action_context_dst(ac);
      if(!(ac=file_glob(tp,&errmsg))) {
        ret=NOIR_FAIL;
        goto done; }
      else if(!(n=action_context_get_seletion_size(ac))) {
        ret=NOIR_NOTFOUND;
        goto done; }}

    strcpy(np,tp); }



  /* create vector for qsort() et al. */
  if(!(p=s=malloc(n*sizeof(char *)))) {
    ret=NOIR_OOM;
    goto fail; }

  /* put glob-results in vector */
  m=0;
  for(af=action_context_get_selection(ac);af;af=action_file_get_next(af)) {
    const char *l,*x;
    /* allow only non-empty results that don't end in /./ and /../ */
    if((l=action_file_get_tag(af))&&strlen(l)) {
      x=l+strlen(l)-1; /* last char in file */
      if((*x==DIR_DELIMITER)&&(x>l)) {
        do {
          x--;
        } while((x>l)&&(*x!=DIR_DELIMITER));
        if((*x==DIR_DELIMITER)&&
           ((!strcmp(x,DIR_DELIM_STR DIR_MYSELF DIR_DELIM_STR))||
            (!strcmp(x,DIR_DELIM_STR DIR_PARENT DIR_DELIM_STR))))
          l=NULL; }

      if(l) {
        m++;
        E(3,"adding match \"%s\"...\n",l);
        *(p++)=(char *)l; }}}

  /* sort glob-results */
  qsort(s,n,sizeof(char *),tab_sort_fun); /* according to the man-page, glob should return a sorted array to begin with */



  /* yes, it's tb_tabbed, typebuffer */
  if((ret=tab(ns,(tb->tb_entered>0)?f:"",0,m,s))!=NOIR_NOTFOUND) {
    char *x=NULL;

    E(1,"found \"%s\"!\n",s[ret]);

    if(!tb->tb_tabbed)
      tb->tb_tabbed=strdup(f);

    *t='\0';

    if(strchr(s[ret],' '))
      esprintf(&x,"%s%s\"%s\"",tb->tb_string,strlen(tb->tb_string)>0?" ":"",s[ret]);
    else
      esprintf(&x,"%s%s%s",tb->tb_string,strlen(tb->tb_string)>0?" ":"",s[ret]);

    if(x) {
      ret=NOIR_SUCC;
      typebuffer_set(ns,x);
      typebuffer_update(ns);
      ret=NOIR_SUCC;
      free(x); }}

  free(s);



done:
  action_context_dst(ac);
fail:
  if(f)
    free(f);

  return ret; }



/**
 * tab-complete a property (complete from list of properties in session)
 * @return an error-code
 * @param ns the session
 */
#if defined(HAVE_EVAS2)

static int tab_property(noir_sess *ns) {
  noir_prop      *e;
  int             n=0,ret=NOIR_NODATA;
  char          **s=NULL,**p;
  char           *t;
  typebuffer     *tb;

  if(!ns||!((noir_theme *)ns->theme)||!((noir_theme *)ns->theme)->data)
    return NOIR_NODATA;

  if(!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  for(e=prop_bag_prop_get_1st(((noir_theme *)ns->theme)->data);e;e=prop_get_next(e))  /* local props */
    n++;

  for(e=prop_bag_prop_get_1st(props_global);e;e=prop_get_next(e))                     /* global props */
    n++;

  if(!(p=s=malloc(n*sizeof(char *))))
    return NOIR_OOM;

  for(e=prop_bag_prop_get_1st(((noir_theme *)ns->theme)->data);e;e=prop_get_next(e))
    *(p++)=(char *)prop_get_key(e);

  for(e=prop_bag_prop_get_1st(props_global);e;e=prop_get_next(e))
    *(p++)=(char *)prop_get_key(e);

  qsort(s,n,sizeof(char *),tab_sort_fun);

  if((t=prop_canonize_key(tb->tb_string+1,FALSE,FALSE))) {
    ret=tab(ns,t,tb->tb_entered-1,n,s);

    if(ret!=NOIR_NOTFOUND) {
      char *x=NULL;

      E(2,"found \"%s\"\n",s[ret]);

      if(!tb->tb_tabbed)
        tb->tb_tabbed=strdup(tb->tb_string);

      esprintf(&x,"'%s",s[ret]);
      if(x) {
        typebuffer_set(ns,x);
        typebuffer_update(ns);
        ret=NOIR_SUCC;
        free(x); }}
    else
      E(0," not found\n");

    free(t); }
  else
    ret=NOIR_OOM;

  free(s);

  return ret; }

#else

static int tab_property(noir_sess *ns) {
  return NOIR_NOTSUPP; }

#endif



/**
 * tab-complete an URL (complete from history)
 * @return an error-code
 * @param ns the session
 */
static int tab_url(noir_sess *ns) {
  hist_entry  *h;
  int          n=0,ret=NOIR_NODATA;
  char       **s=NULL,**p;
  typebuffer  *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  if(!tb->tb_history||!tb->tb_history->anchor)
    return NOIR_NODATA;

  for(h=tb->tb_history->anchor;h;h=h->next)
    n++;

  if(!(p=s=malloc(n*sizeof(char *))))
    return NOIR_OOM;

  for(h=tb->tb_history->anchor;h;h=h->next)
    *(p++)=h->line;

  qsort(s,n,sizeof(char *),tab_sort_fun);

  E(1,"\"%s\"\n",tb->tb_string);

  if((ret=tab(ns,tb->tb_string,tb->tb_entered,n,s))!=NOIR_NOTFOUND) {
    if(!tb->tb_tabbed)
      tb->tb_tabbed=strdup(tb->tb_string);
    typebuffer_set(ns,s[ret]);
    typebuffer_update(ns);
    ret=NOIR_SUCC; }

  free(s);

  return ret; }



/***********************************/

/**
 * find out what's in type-buffer, and tab-complete accordingly
 * @return an error-code
 * @param ns the session
 */
int typebuffer_tab(noir_sess *ns) {
  typebuffer *tb;
  char       *t;
  int         ret=NOIR_IGNORED;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;  /* return *now*, can't tb->tb_entered... later */

  tb->tb_flags&=~(TB_RSEARCH|TB_RRSEARCH);

  if(!(t=tb->tb_string))
    return NOIR_NODATA;

  if(!strlen(t))
    ret=tab_shell(ns);
  else if((t[0]==',')||((t[0]=='\'')))
    ret=tab_property(ns);
  else {
    char *p,*s=strchr(t,' ');
    if(((p=strstr(t,PROTOCOL_DELIMITER))!=NULL)&&
       ((s==NULL)||(p<s)))
      ret=tab_url(ns);
    else
      ret=tab_shell(ns); }

  if(tb->tb_entered<1)
    tb->tb_entered=strlen(tb->tb_tabbed?tb->tb_tabbed:tb->tb_string);

  return ret; }



int typebuffer_tab_reset(noir_sess *ns,size_t l) {
  typebuffer *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  if(tb->tb_flags&TB_RSEARCH) {
    E(2,"RSEARCH %s, %d\n",tb->tb_tabbed?tb->tb_tabbed:"",tb->tb_entered);
    tb->tb_entered=l;
    tb->tb_flags&=(~TB_RRSEARCH);
    return typebuffer_history_search(noir_sess_get_typebuffer(ns),NULL); }

  else if(l!=strlen(tb->tb_string)) {
    tb->tb_entered=-1;
    if(tb->tb_tabbed) {
      free(tb->tb_tabbed);
      tb->tb_tabbed=NULL; }
    return NOIR_SUCC; }

  return NOIR_IGNORED; }




/****************************************************************************/
/* main */



static char *typebuffer_interprete_token(noir_sess *ns,char *b) {
  char        myfile[PATH_MAX+1]="";



  if(!b||!*b)
    return NULL;



  /* URL ******************************************************************/

  if(strstr(b,PROTOCOL_DELIMITER)) { /* if it contains ://, we assume URL */
    if(!strncmp(b,PROTOCOL_FILE,7))
      return strdup(b+7);
    typebuffer_handle_URL(ns,b);
    return NULL; }



  /* file *****************************************************************/


  /* make absolute where needed */
  if(!strcmp(b,DIR_MYSELF)||!strncmp(b,DIR_MYSELF DIR_DELIM_STR,2)||
     !strcmp(b,DIR_PARENT)||!strncmp(b,DIR_PARENT DIR_DELIM_STR,3)) {
    char *x=file_compose_path(noir_sess_get_cwd(ns),b);
    if(x) {
      strcpy(myfile,x);
      free(x); }
    else
      return NULL; }

  else if(file_if_relative(b)) {                          /* in $PATH? */
    char *bp=NULL;
    char *x=file_compose_path(noir_sess_get_cwd(ns),b,NULL);

    if(x) {                               /* try in current dir first */
      if(file_exists(x))
        strcpy(myfile,x);
      free(x); }
    else
      return NULL;

    if(!strlen(myfile)) {
      if((bp=get_path_file(noir_sess_get_cwd(ns),b))) {   /* try in $PATH */
        int exc=(strlen(bp)>=PATH_MAX);
        if(!exc)
          strcpy(myfile,bp);
        free(bp);
        if(exc)
          return NULL; }
      else
        strcpy(myfile,b); }}

  else
    strcpy(myfile,b);



  /* handle  /../ and /./ ***********************************************/
#ifdef WITH_DOT_DIR
  typebuffer_path_normalize(myfile,TRUE);
#else
  typebuffer_path_normalize(myfile,FALSE);
#endif

  return strdup(myfile); }



/**
 * process contents of typebuffer
 * @return an error-code
 * @param ns the session
 */
int typebuffer_do(noir_sess *ns) {
  char       *b,*e,*t;
  char       *x[MAX_PARS+1];
  int         stat,c=0,ret=NOIR_FAIL;
  size_t      l,l_cwd;
  char       *typebuf,*sel;
  typebuffer *tb;

  if(!ns||!(tb=noir_sess_get_typebuffer(ns)))
    return NOIR_FAIL;

  typebuf=tb->tb_string;
  if(!typebuf||!*typebuf||!ns)
    return NOIR_NODATA;

  if(!(t=strdup(typebuf)))
    return NOIR_OOM;

  if(!tb->tb_histlast||strcmp(tb->tb_histlast->line,t))
    typebuffer_history_add(tb,t);

  if((t[0]==',')||(t[0]=='\'')) {            /* magic comma -- properties */
    ret=typebuffer_modify_theme(ns,t);
    free(t);
    return ret; }

  if(t[0]=='%') {                            /* magic percentage -- search */
#ifdef HAVE_DOODLE
    ret=typebuffer_search(ns,t);
    free(t);
#else
    E(0,"built without doodle, no searching!\n");
    ret=NOIR_NOTSUPP;
#endif
    return ret; }

  if(t[0]=='\\') {                            /* magic backslash -- dump fi */
    ret=typebuffer_dump_fi(ns,t);
    free(t);
    return ret; }



  /**************************************************************************/

  l=strlen(e=t);
  l_cwd=strlen(ns->cwd);

  sel=action_context_files_to_quoted_string(noir_sess_get_selection(ns));

  while((b=util_get_token(&e,TRUE,noir_sess_get_cwd(ns),sel))) {
    char *myfile=NULL;
    int   isglob=(file_is_globbable(b)==NOIR_SUCC);

    if(isglob||!c) {
      char *r=typebuffer_interprete_token(ns,b);
      free(b);
      if(r)
        myfile=r;
      else if(e)
        e=NULL; }
    else
      myfile=b;

    if(myfile) {
      if(isglob) {
        const char     *errmsg=NULL;
        action_context *ac=file_glob(myfile,&errmsg);
        action_file    *af;

        if(ac) {
          if(!(af=action_context_get_selection(ac)))
            x[c++]=path_rewrite(myfile);
          else
            while(af) {
              if((x[c]=path_rewrite(action_file_get_tag(af)))) {
                E(1,"file_glob{ \"%s\" -> \"%s\" }\n",myfile,x[c]);
                c++; }
              af=action_file_get_next(af); }
          action_context_dst(ac); }
        else if(errmsg)
          form_alert(ns,errmsg);
        free(myfile); }
      else
        x[c++]=myfile; }}

  if(sel)
    free(sel);






  /* done tokenizing, exec! *************************************************/

  x[c]=NULL;

  if(c) {                          /* is there something to do? (metal mix) */

    /* cd *******************************************************************/
    if(!strcmp(x[0],"quit")||!strcmp(x[0],"exit"))
      gui_quit();

    else if(!strcmp(x[0],"cd")||!strcmp(x[0],"ls")||!strcmp(x[0],"ll")) {
      ret=NOIR_SUCC;
      E(1,"built-in cd\n");
      if(!x[1])                           /* cd */
        ret=typebuffer_focus(ns,home);
      else if(!strcmp(x[1],"-")) {        /* cd - */
        if(!ns->pwd) {
          form_alert(ns,"No previous directory...");
          ret=NOIR_IGNORED; }
        else
          ret=typebuffer_focus(ns,ns->pwd); }
      else                                /* cd ~  cd whatever */
        ret=typebuffer_focus(ns,x[1]);
      goto tb_exit; }

    else if(!strcmp(x[0],"rm")) {
      form_alert(ns,"No rm yet...");
      ret=NOIR_IGNORED;
      goto tb_exit; }

    stat=file_exists(x[0]);

    E(1,"\"%s\": $%x\n",x[0],stat);

    if(S_ISDIR(stat))               /* it's a dir. focus. */
      ret=typebuffer_focus(ns,x[0]);

    else {                          /* it's a file. execute, if we can. */
      if((!stat)||(S_ISREG(stat)&&(stat&(S_IXUSR|S_IXGRP|S_IXOTH)))) {
        int n=0;                    /* it's an executable. execute. */
        while(n<c) {
          E(0,"%2d: \"%s\"\n",n,x[n]);
          n++; }
        file_fork(ns,x);
        ret=NOIR_SUCC; }
      else                          /* it's a data file. run w/ cqt app. */
        ret=file_activate(ns,x[0],NULL,NULL); }}

 tb_exit:

  if(b)
    free(b);

  if(debug>=0) {
    switch(ret) {
      case NOIR_PATHEX:
        E(0,"maximum path length exceeded...\n");
        break;
      default:
        break; }}

  while(--c>=0)
    free(x[c]);

  free(t);

  return ret; }
