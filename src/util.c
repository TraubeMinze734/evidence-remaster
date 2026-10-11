/** @file  util.c
 *  @brief handy helpers - ADT, asprintf, dlopen()-wrapper, ...
 */

#include "config.h"

#ifdef HAVE_ECORE
#  include <Ecore.h>
#else
#  include <signal.h>
#  include <sys/wait.h>
#endif
#ifdef HAVE_GLIB2
#  include <glib.h>
#endif
#ifdef HAVE_GTK2
#  include <gtk/gtk.h>
#endif

#include <iconv.h>
#include <limits.h>       /* PATH_MAX */
#include <sys/types.h>
#include <sys/time.h>    /* gimetimeofday() */
#include <stdio.h>       /* NULL */
#include <stdlib.h>      /* malloc(), free() */
#include <string.h>      /* str...() */
#include <ctype.h>       /* isspace() */
#include <errno.h>
#include <langinfo.h>
#include <pthread.h>

#include <stdarg.h>      /* varargs in dlmulti() */
#include <stdint.h>      /* uintptr_t for GLib source-id <-> pointer */
#include <dlfcn.h>       /* dlopen() and friends for dlmulti() */

#include "util.h"
#include "debug.h"
#include "errors.h"



#define CHUNKLEN 4096
#define CHUNKCNV 256



/*****************************************************************************/




typedef struct _sigchld_handler {
  int                     (*func)(void *data,int type,void *event);
  const void               *data;
  struct _sigchld_handler  *next; } sigchld_handler;

static sigchld_handler    *sigchld_handlers=NULL;

static volatile siginfo_t  sigchld_info={0};
pthread_mutex_t            sigchld_handlers_lock=PTHREAD_MUTEX_INITIALIZER;



void util_sigchld_handler(int num,siginfo_t *si,void *data) {
  Ecore_Event_Exe_Exit e;
  pid_t                pid;
  int                  status,exitstatus,cont;
  sigchld_handler     *s;

  if(si)
    sigchld_info=*si;
  else
    sigchld_info.si_signo=0;

  /* wait for child */
  while((pid=waitpid((pid_t)-1,&status,WNOHANG))>0) {
    memset(&e,0,sizeof(Ecore_Event_Exe_Exit));

    if(WIFEXITED(status)) {
      e.exit_code=WEXITSTATUS(status);
      e.exited=1;
      E(2,"child %d exited (%d).\n",(int)pid,e.exit_code); }
    else if(WIFSIGNALED(status)) {
      e.exit_signal=WTERMSIG(status);
      e.signalled=1;
      E(1,"child %d terminated by signal %d.\n",(int)pid,e.exit_signal); }
    else if(WIFSTOPPED(status)) {
      exitstatus=WSTOPSIG(status);
      E(1,"child %d stopped by signal %d.\n",(int)pid,exitstatus); }
    else
      E(0,"child %d exited in some new and exotic way!?\n",(int)pid);

    e.pid=pid;
    e.exe=NULL;

    if(sigchld_info.si_signo)
      e.data=sigchld_info;

    pthread_mutex_lock(&sigchld_handlers_lock);

    for(cont=1,s=sigchld_handlers;s&&cont;s=s->next)
      cont=s->func((void *)s->data,0,&e);

    pthread_mutex_unlock(&sigchld_handlers_lock); }}



void *util_sigchld_handler_add(int (*func)(void *data,int type,void *event),const void *data) {
  sigchld_handler *s=malloc(sizeof(sigchld_handler));
  if(!s)
    return NULL;
  memset(s,0,sizeof(sigchld_handler));
  s->data=data;
  s->func=func;
  pthread_mutex_lock(&sigchld_handlers_lock);
  s->next=sigchld_handlers;
  sigchld_handlers=s;
  pthread_mutex_unlock(&sigchld_handlers_lock);
  return (void *)NULL; }



void *util_sigchld_handler_del(void *handle) {
  sigchld_handler *s,*p=NULL;
  const void      *r;

  pthread_mutex_lock(&sigchld_handlers_lock);
  for(s=sigchld_handlers;s&&(s!=handle);s=s->next)
    p=s;
  if(!s)
    return NULL;
  r=s->data;
  if(!p)
    sigchld_handlers=sigchld_handlers->next;
  else
    p->next=s->next;
  free(s);
  pthread_mutex_unlock(&sigchld_handlers_lock);
  return (void *)r; }









/*****************************************************************************/



/**
 * satisfy symbols from a shared object. dlopen() on steroids.
 * NOIR_SUCC: got all symbols
 * NOIR_PARTIAL: got all required symbols, but not all optional symbols
 * NOIR_NOTFOUND: didn't get all of the required symbols
 * NOIR_FAIL: invalid parameters
 * NOIR_NODATA: caller asked for empty symbol name
 * NOIR_NOFILE: couldn't open shared object
 * NOIR_OOM: out of memory, symbol name too long
 *
 * @param name - a logical name for the library, or NULL
 * @param file - the shared object; will be dlopen()ed and hence looked for in the
 *  usual places for shared objects (defined by ld.so.conf on linux)
 * @param flag - dlopen()-flags (RTLD_*)
 * @param libr - where to store the handle for the library
 * @param fmt - a space-separated list of symbols to try and load.
 *  symbols prefixed by '?' are optional, symbols prefixed by '!' are required.
 * @param ... - where to store the function-pointers we get (or NULL for failure on
 *  an optional symbol)
 */
int dlmulti(char *name,char *file,int flag,void **libr,const char *fmt, ...) {
#define MAX_SYM_LEN 256
  va_list  ap;
  void    *lib;
  int      ret=NOIR_SUCC;
  char     buf[MAX_SYM_LEN]="\0";

  if(!libr)
    return NOIR_FAIL;

  if(!name)
    name=file;

  *libr=NULL;

  if((lib=dlopen(file,flag))) {
    void   **funr,*fun;
    char    *b,*e;
    size_t   l;
    int      required=1;

    va_start(ap,fmt);
    while(*fmt) {
      switch(*fmt) {
        case '?':
          required=0;
          fmt++;
          break;

        case '!':
        case '.':
          required=1;
          fmt++;
          break;

        case '\t':
        case '\n':
        case '\r':
        case ';':
        case ',':
        case ' ':
          fmt++;
          break;

        default:
          e=b=(char *)fmt;
          while(*e&&(strchr("?!.,; \t\n\r",*e)==NULL))
            e++;

          fmt=e;
          if(e==b)
            ret=NOIR_NODATA;
          else if((l=(e-b))>=MAX_SYM_LEN)
            ret=NOIR_OOM;
          else {
            int r2;
            memcpy(buf,b,l);
            buf[l]='\0';
            funr=va_arg(ap,void **);
            if(!(fun=dlsym(lib,buf))) {
              if((r2=required?NOIR_NOTFOUND:NOIR_PARTIAL)==NOIR_NOTFOUND)
                E(1,"library/plugin/engine \"%s\" (\"%s\") did not contain required function \"%s\"...\n",name,file,buf);
              if(ret!=NOIR_NOTFOUND)
                ret=r2; }
            E(2,"%p => %p %c\"%s\"\n",fun,funr,required?'!':'?',buf);
            if(funr)
              *funr=fun; }
          required=1; }}

    va_end(ap);

    if((ret==NOIR_SUCC)||(ret==NOIR_PARTIAL)) {
      E(2,"dlmulti()d \"%s\"\n",file);
      *libr=lib; }
    else
      dlclose(lib); }
  else
    ret=NOIR_NOFILE;

  return ret; }



/*****************************************************************************/



/**
 * return current time, plus an optional offset.
 * @param delay - offset ("what time is it ten seconds from now?")
 */
ulong now(long delay) {
  static struct timeval tv;
  ulong                 r;
  gettimeofday(&tv,NULL);
  r=tv.tv_sec*1000+(((float)tv.tv_usec)/1000.0)+delay;
  return r; }



/*****************************************************************************/



/**
 * replace $VARIABLES in a string.  {} recognized.
 * @return a buffer that must be free()d.
 * @param in - the string to replace variables in
 * @param cwd - current working directory (for $PWD), or NULL
 * @param sel - current selection (for $SEL), or NULL
 */
char *util_replace_variables(char *in,const char *cwd,const char *sel) {
  char *i=in;
  char *o,*m;
  char  buf[PATH_MAX+1];

  buf[0]='\0';
  o=buf;
  m=buf+PATH_MAX;

  while(*i&&(o<m)) {
    if((*i=='\\')&&(i[1])) {
      i++;
      *(o++)=*i; }
    else if(*i=='$') {
      char *b,*e;

      if(*(++i)=='{') {
        b=++i;
        while(*i&&(*i!='}'))
          i++; }
      else {
        b=i;
        while(isalnum(*i)||(*i=='_'))
          i++; }

      e=i;

      if(e>b) {
        char        s=*e;
        const char *r;
        size_t      l;

        *e='\0';

        if(cwd&&!strcasecmp(b,"pwd"))
          r=cwd;
        else if(sel&&(!strcasecmp(b,"sel")||!strcasecmp(b,"selection")))
          r=sel;
        else if(!(r=getenv(b))) {  /* try in environment */
          E(0,"referencing unknown variable \"%s\"...\n",b);
          r=""; }

        E(3,"%s -> \"%s\"...\n",b,r);

        l=strlen(r);
        if((o+l)>=m) {
          E(0,"path \"%s\" is too long...\n",in);
          return NULL; }

        strncat(o,r,l);
        o+=l;

        *e=s; }
      else {
        E(0,"broken variable(s) in \"%s\"\n",in);
        i=b; }

      i--; }
    else
      *(o++)=*i;

    i++; }

  *(o++)='\0';

  if(o>=m) {
    E(0,"path \"%s\" is too long...\n",in);
    return NULL; }

  E(3,"\"%s\" -> \"%s\"\n",in,buf);

  return strdup(buf); }



/**
 * get token
 * @return       a buffer containing the current token. this must be free()d by the caller.
 * @param begin  start of current token
 * @param cook   resolve $VARIABLES in input?
 * @param cwd    current working directory (for $PWD), or NULL
 * @param sel    current selection (for $SEL), or NULL
 */
char *util_get_token(char **begin,int cook,const char *cwd,const char *sel) {
  char   *b,
         *e=NULL,
         *r=NULL,
         *x;
  char    t=' ';     /* terminator. by default, the token is space-terminated,
                        unless it starts with a quote (") */
  size_t  l;

  if(!begin||!*begin)
    return NULL;

  for(b=*begin;*b==' ';b++)                   /* skip leading spaces */
    ;

  /* if the token starts with a quote, scan up to unquote rather than space */
  if(*b=='\"') {
    t='\"';
    b++; }

  /* find end of token */
  e=b;                                        /* jump to end of element */
  while(*e&&*e!=t) {
    if((*e=='\\')&&e[1])                      /* skip escaped char */
      e+=2;
    else
      e++; }

  if(!(l=(e-b)))
    return NULL;

  if(!(r=malloc(l+1)))
    return NULL;

  strncpy(r,b,l);
  r[l]='\0';

  /* removed \ escapes */
  for(x=r;*x;x++)
    if(*x=='\\')
      memmove(x,x+1,strlen(x));

  if(cook) {
    if((t=='\"')&&(*e==t))
      *(e++)='\0';
    while(*e==' ')                            /* delimit */
      *(e++)='\0';
    if((x=util_replace_variables(r,cwd,sel))) {
      free(r);
      r=x; }}

  /* skip ahead to next token */
  if((t=='\"')&&(*e==t))
    e++;
  while(*e==' ')
    e++;
  *begin=e;

  E(3," ::%s::\n",r);

  return r; }






/*****************************************************************************/



/**
 * split a line into tokens, weeding out comments etc.
 * @return number of tokens it found.
 * @param in - the line to split
 * @param o1 - where to store the address of the first  token
 * @param o2 - where to store the address of the second token
 * @param o3 - where to store the address of the third  token
 * @param o4 - where to store the address of the fourth token
 *
 * (could arguably be turned into a varargs function at some point)
 */
int util_parse_line(char *in,char **o1,char **o2,char **o3,char **o4) {
#define PLMAX 16
  int   c;
  char *p=in;
  char *r[PLMAX];

  for(c=0;c<PLMAX;c++)
    r[c]=NULL;

  c=0;
  if(!in||!*in)
    goto pl_end;

  do {
    while(isspace(*p))
      *(p++)='\0';

    if(!*p||(strchr("#;",*p)&&(!p[1]||isspace(p[1]))))
      *p='\0';  /* it ends HERE */
    else {
      if(*p=='\"') {  /* segment quoted */
        r[c++]=++p;
        while(*p&&(*p!='\"')) {
          if((*p=='\\')&&p[1])
            memmove(p,p+1,strlen(p));
            p++; }
        if(*p)
          *(p++)='\0';
        else
          E(0,"segment |%s| starts with a quote, but doesn't end with one?\n",r[c-1]); }

      else {          /* segment not quoted */
        r[c++]=p;
        while(*p&&!isspace(*p))
          p++; }}

  } while(*p);

 pl_end:
  if(o1) *o1=r[0];
  if(o2) *o2=r[1];
  if(o3) *o3=r[2];
  if(o4) *o4=r[3];

  return c; }



/*****************************************************************************/



/**
 * rewrite a C-string containing a number of bytes to a more
 * readable format.
 * @param size - the buffer to rewrite
 */
char *unit_size(char *size) {
#ifdef HAVE_STRTOULL
  unsigned long long s=strtoull(size,NULL,10);
#elif defined(HAVE_STRTOUL)
  unsigned long      s=strtoul(size,NULL,10);
#else
  off_t              s=atol(size);
#endif
  /* scale once, format once.  the 2005 code stacked TWO division
     schemes (the unit-picking divided a second time in the formatter),
     which worked by accident for KB/MB and printed "0.0 GB" for
     everything above 1GB. */
  if(s>=(1024LL*1024*1024*1024))
    sprintf(size,"%.1f %s",((double)s)/(1024LL*1024*1024*1024),"TB");
  else if(s>=(1024LL*1024*1024))
    sprintf(size,"%.1f %s",((double)s)/(1024LL*1024*1024),"GB");
  else if(s>=(1024*1024))
    sprintf(size,"%.1f %s",((double)s)/(1024*1024),"MB");
  else if(s>=1024)
    sprintf(size,"%.1f %s",((double)s)/1024,"KB");
  else
    sprintf(size,"%llu %s",(unsigned long long)s,"byte");
  return size; }



/*****************************************************************************/



/**
 * generic quick-sort algorithm that hands user-data (sort-order, ...)
 * to the compare function and sorts pointers to the data rather than the data.
 * @param a - array of pointers to the data
 * @param data - user-data
 * @param lo - index of first element (0)
 * @param hi - index of last element (n-1)
 * @param compare - compare-function that takes pointers to user-data, first and
 *  second element and returns -1, 0, 1 for first element is "less than",
 *  "equal to", "greater than" the second element
 */
void qsrt(void *a[],void *data,int lo,int hi,int (*compare)(const void *,const void *,const void *)) {
  int   h,l;
  void *p,*t;

  if(lo<hi) {
    l=lo;
    h=hi;
    p=a[hi];

    do {
      while((l<h)&&(compare(data,a[l],p)<=0))
        l=l+1;
      while((h>l)&&(compare(data,a[h],p)>=0))
        h=h-1;
      if(l<h) {
        t=a[l];
        a[l]=a[h];
        a[h]=t; }
    } while(l<h);

    t=a[l];
    a[l]=a[hi];
    a[hi]=t;

    qsrt(a,data,lo,l-1,compare);
    qsrt(a,data,l+1,hi,compare); }}





/*****************************************************************************/
/* TIMERS */
/**********/



/**
 * check whether there are events in the Q
 * @return 0 for no, !0 for yes
 */
int events_pending(void) {
#if defined(HAVE_ECORE)
  return ecore_main_loop_iterate(), 0;
#elif defined(HAVE_GTK2)
  return gdk_events_pending();
#elif defined(HAVE_GLIB2)
  return g_main_context_pending(NULL);
#else
#  warning uh-oh love has come to town ### FIXME
#endif
}



/**
 * delayed execution of a function.  the function will be executed every 'f'
 * ms until it returns FALSE or is dequeued.
 * @return a handle for the scheduled entry.
 * @param f - how long to wait before execution the function (ms)
 * @param fun - the function to call
 * @param data - user-data to hand to "fun" when it is called
 */
void *timeout_add(uint f,int(*fun)(void *),void *data) {
#ifdef HAVE_ECORE
/* void *h=(void*)ecore_timer_add(f/1000.0,fun,data); E(0,"%p\n",h); return h; */
  return (void *)ecore_timer_add(((float)f)/1000.0,(Ecore_Task_Cb)fun,data);
#elif defined(HAVE_GLIB2)
  /* GLib hands out guint source ids; widen them through uintptr_t so the
     pointer-typed handle is well-defined on 64-bit */
  return (void *)(uintptr_t)g_timeout_add((guint)f,(GSourceFunc)fun,(gpointer)data);
#endif
  return (void *)0; }



/**
 * cancel delayed execution of a function enqueued with timeout_add().
 * @param h - the handle of the function to dequeue (from timeout_add())
 */
int timeout_remove(void **h) {
  void *handle;
  if(!h)
    return NOIR_FAIL;
  if(!(handle=*h))
    return NOIR_IGNORED;
  *h=NULL;
#if 0
  if(debug>=2) {
    long *p=handle;
    E(0,"handle: %p\n",handle);
    E(0,"handle: { %lx, %lx }\n",p[0],p[1]); }
#endif
#ifdef HAVE_ECORE
  ecore_timer_del(handle);
  return NOIR_SUCC;
#elif defined(HAVE_GLIB2)
  return g_source_remove((guint)(uintptr_t)handle)?NOIR_SUCC:NOIR_FAIL;
#endif
  return NOIR_NOTSUPP; }



void *idle_enter_add(int prio,int (*fun)(void *),void *data) {
#if defined(HAVE_ECORE)
  return ecore_idle_enterer_add((Ecore_Task_Cb)fun,data);
#elif defined(HAVE_GLIB2)
  { (void)prio; return (void *)(uintptr_t)g_idle_add((GSourceFunc)fun,data); }
#elif defined(HAVE_GTK2)
  return (void *)gtk_idle_add_priority(prio,fun,data);
#endif
  return NULL; }



int idle_enter_remove(void **h) {
  void *handle;
  if(!h)
    return NOIR_FAIL;
  if(!(handle=*h))
    return NOIR_IGNORED;
  *h=NULL;
#if defined(HAVE_ECORE)
  ecore_idle_enterer_del(handle);
  return NOIR_SUCC;
#elif defined(HAVE_GLIB2)
  g_source_remove((guint)(uintptr_t)handle);
  return NOIR_SUCC;
#elif defined(HAVE_GTK2)
  gtk_idle_remove((guint)(uintptr_t)handle);
  return NOIR_SUCC;
#endif
  return NOIR_NOTSUPP; }



int mainloop_once(void) {
#if defined(HAVE_GTK3)
  /* GTK3 runs gtk_main(): nested pumps (modal menus etc.) must drive
     the GLib loop so GTK events -- e.g. a menu's "selection-done" --
     actually arrive. ecore_main_loop_iterate() would only serve
     ecore's own sources, deadlocking the modal menu loop. */
  g_main_context_iteration(NULL,TRUE);
  return NOIR_SUCC;
#elif defined(HAVE_ECORE)
  ecore_main_loop_iterate();
  return NOIR_SUCC;
#elif defined(HAVE_GLIB2)
  g_main_context_iteration(NULL,TRUE);
  return NOIR_SUCC;
#endif
  return NOIR_NOTSUPP; }



int mainloop_quit(void) {
#if defined(HAVE_GTK3)
  /* under GTK3 we run gtk_main() (the ecore loop is only integrated
     into GLib, not the other way round) -- quitting ecore would be
     a no-op and gtk_main() would never return. */
  gtk_main_quit();
  return NOIR_SUCC;
#elif defined(HAVE_ECORE)
  ecore_main_loop_quit();
  return NOIR_SUCC;
#elif defined(HAVE_GTK2)
  gtk_main_quit();
  return NOIR_SUCC;
#elif defined(HAVE_GLIB2)
  { GMainContext *ctx=g_main_context_default();
    if(g_main_context_acquire(ctx)) {
      g_main_context_wakeup(ctx);
      g_main_context_release(ctx); } }
  return NOIR_SUCC;
#endif
  return NOIR_NOTSUPP; }






/*****************************************************************************/
/* LISTS */
/*********/



/**
 * free a list.
 * @param l - first element to free
 * @param free_payload - function that free()s an element's payload, or NULL for none.
 */
int eslist_free(eslist **l,void (*free_payload)(void *)) {
  eslist *e,*f;
  if(!l)
    return NOIR_NODATA;
  for(e=*l;e;e=f) {
    f=e->next;
    if(free_payload)
      free_payload(e->payload);
    free(e); }
  *l=NULL;
  return NOIR_SUCC; }



/**
 * get next element in list
 * the ESLIST_NEXT() macro may be used in the main-program (not in plugins).
 * @param e - current element in list
 */
int eslist_next(eslist **e) {
  if(!e||!*e)
    return NOIR_NODATA;
   *e=(*e)->next;
   return NOIR_SUCC; }



/**
 * get the payload of a list-element.
 * the ESLIST_PAYLOAD() macro may be used in the main-program (not in plugins).
 * @param e - list-element
 */
void *eslist_payload(eslist **e) {
  return (!e||!*e)?NULL:(*e)->payload; }



/**
 * prepend an element to a list.
 * @param e - the element
 * @param p - the list
 */
int eslist_prepend(eslist **e,void *p) {
  eslist *f;

  if(!e)
    return NOIR_NODATA;

  if(!(f=malloc(sizeof(eslist))))
    return NOIR_OOM;

  f->payload=p;
  f->next=*e;

  *e=f;

  return NOIR_SUCC; }



/**
 * append an element to a list.
 * @param e - the element
 * @param p - the list
 */
int eslist_append(eslist **e,void *p) {
  eslist *f;

  if(!e)
    return NOIR_NODATA;

  if(!(f=malloc(sizeof(eslist))))
    return NOIR_OOM;

  f->payload=p;
  f->next=NULL;

  if(!*e)
    *e=f;
  else {
    eslist *g=*e;
    while(g->next)
      g=g->next;
    g->next=f; }

  return NOIR_SUCC; }






/*****************************************************************************/
/* HASHES */
/**********/



/**
 * create a hash-table.
 * @param freekey - when a hash-element is destroyed, call this to free the key
 * @param freeval - when a hash-element is destroyed, call this to free the payload
 */
void *hash_table_new(void (*freekey)(void *),void (*freeval)(void *)) {
#ifdef HAVE_GLIB2
  return g_hash_table_new_full(g_str_hash,g_str_equal,freekey,freeval);
#endif
  return NULL; }



/**
 * find an element in hash-table.
 * @param hashtable - the hash-table to search in
 * @param key - the key of the element to look for
 */
void *hash_table_fetch(void *hashtable,const char *key) {
#ifdef HAVE_GLIB2
  return g_hash_table_lookup(hashtable,key);
#endif
  return NULL; }



/**
 * insert a new element into a hash-table.
 * @param hashtable - the hash-table
 * @param key - the key for the new element
 * @param value - the payload of the new element
 */
int hash_table_insert(void *hashtable,const char *key,void *value) {
#ifdef HAVE_GLIB2
  g_hash_table_insert(hashtable,(char *)key,value);
  return NOIR_SUCC;
#endif
 return NOIR_NOTSUPP; }



/**
 * change the value of a hash-table entry.
 * @param hashtable - the hash-table
 * @param key - the key for the new element
 * @param value - the new payload of element
 */
int hash_table_replace(void *hashtable,const char *key,void *value) {
#ifdef HAVE_GLIB2
  g_hash_table_replace(hashtable,(char *)key,value);
  return NOIR_SUCC;
#endif
 return NOIR_NOTSUPP; }



/**
 * remove an element from a hash-table.
 * @param hashtable - the hash-table
 * @param key - the key of element to discard
 */
int hash_table_remove(void *hashtable,char *key) {
#ifdef HAVE_GLIB2
  g_hash_table_remove(hashtable,key);
  return NOIR_SUCC;
#endif
 return NOIR_NOTSUPP; }



/**
 * free hashtable (administrative data as well as payload).
 * @param hashtable - the hashtable about to die
 */
int hash_table_dst(void **hashtable) {
#ifdef HAVE_GLIB2
  g_hash_table_destroy(*hashtable);
  *hashtable=NULL;
  return NOIR_SUCC;
#endif
 return NOIR_NOTSUPP; }



/**
 * apply a function to all entries of a hash-table.
 * @param hashtable - the table to walk
 * @param fun - the function to apply to all elements (cf. "map")
 * @param data - user-data to pass to the walker
 */
int hash_table_walk(void *hashtable,hash_walker fun,void *data) {
#ifdef HAVE_GLIB2
  g_hash_table_foreach(hashtable,(GHFunc)fun,data);
  return NOIR_SUCC;
#endif
 return NOIR_NOTSUPP; }






/*****************************************************************************/
/* STRINGS */
/***********/



/**
 * create a string.  pre-allocate the estimated size.
 * @param size - the estimated size.  (string can grow larger or smaller than that.)
 */
estring *estring_new(int size) {
  estring *e=malloc(sizeof(estring));
  if(e) {
    memset(e,0,sizeof(estring));
    if((size>0)&&(e->str=malloc(size)))
      e->alloc=size; }
  return e; }



/**
 * destruct an estring, both payload and administrative data.
 * @param e - the estring to destruct
 */
estring *estring_dst(estring *e) {
  if(e) {
    if(e->str)
      free(e->str);
    free(e); }
  return NULL; }



/**
 * throw away administrative data of estring, keep C-string.
 * @return the address of the C-string, which must later be free()d.
 * @param e - the estring to disown
 */
char *estring_disown(estring *e) {
  if(e) {
    char *r=e->str;
    free(e);
    return r; }
  return NULL; }



/**
 * release an estring, optionally leaving a C-string.
 * for glib2 compatibility.
 * @return the remaining C-string (which must be free()d later), or NULL.
 * @param e - the estring
 * @param release_payload - if TRUE, the payload will be free()d. otherwise, it
 *  will be returned to the caller.
 */
char *estring_free(estring *e,int release_payload) {
  if(release_payload) {
    estring_dst(e);
    return NULL; }
  return estring_disown(e); }



/**
 * the length of an e-string
 * @return the length (including \\0) of an e-string, or 0 for failure
 * @param e - the e-string
 */
size_t estring_length(estring *e) {
  return e?e->used:0; }



/**
 * truncate estring at a given byte-index.
 * @param e - the estring
 * @param size - the byte-index (careful when using UTF8/multi-byte characters!)
 */
int estring_truncate(estring *e,int size) {
  char *p;
  if(!e||(size<0))
    return NOIR_FAIL;
  if(e->used<=size)
    return e->used;
  e->str[size]='\0';
  e->used=size;
  if((p=(char*)realloc(e->str,e->used+1))) {
    e->alloc=e->used+1;
    e->str=p; }
  return size; }



/**
 * sprintf() to an auto-resizing estring.
 * @param e - the estring
 * @param fmt - the format-string, as per printf()
 * @param ... - the variables for the %-substitutions in the format-string
 */
int estring_printf(estring *e,char *fmt, ...) {
  int      need;
  va_list  ap;
  char    *p;

  if(!e)
    return NOIR_FAIL;

  if(!(e->str)) {
    if(!(e->str=(char*)malloc(e->alloc=512)))
      return NOIR_OOM; }

retry:
  va_start(ap,fmt);
  need=vsnprintf(e->str,e->alloc,fmt,ap);
  va_end(ap);

  if((need>=e->alloc)||(need<0)) {
    if(need<0)
      need=2*e->alloc;
    else
      need++;
    if(!(p=(char*)realloc(e->str,need))) {
      free(e->str);
      e->alloc=e->used=0;
      return NOIR_OOM; }
    e->alloc=need;
    e->str=p;
    goto retry; }

  return e->used=need; }



/**
 * sprintf() to the end of an auto-resizing estring.
 * @param e - the estring
 * @param fmt - the format-string, as per printf()
 * @param ... - the variables for the %-substitutions in the format-string
 */
int estring_appendf(estring *e,char *fmt, ...) {
  int      need;
  va_list  ap;
  char    *p;

  if(!e)
    return NOIR_FAIL;

  if(!e->str) {
    e->used=e->alloc=0;
    if(!(e->str=(char*)malloc(e->alloc=512)))
      return NOIR_OOM; }

retry:
  va_start(ap,fmt);
  need=vsnprintf(e->str+e->used,e->alloc-e->used,fmt,ap);
  va_end(ap);

  if((need>=(e->alloc-e->used))||(need<0)) {
    if(need<0)
      need=2*e->alloc;
    else
      need++;
    need+=e->used;
    need+=(CHUNKLEN-(need%CHUNKLEN));

    if(!(p=(char*)realloc(e->str,need))) {
      free(e->str);
      e->alloc=e->used=0;
      return NOIR_OOM; }
    e->alloc=need;
    e->str=p;
    goto retry; }

  return e->used+=need; }



/**
 * concatenate bytes to the end of an auto-resizing estring.
 * @param e - the estring
 * @param len - number of bytes to append
 * @param bytes - the bytes to append
 */
int estring_appendb(estring *e,size_t len,char *bytes) {
  int      need=len;
  char    *p;

  if(!e)
    return NOIR_FAIL;
  if(len<=0)
    return NOIR_IGNORED;

  if(!e->str) {
    e->used=e->alloc=0;
    if(!(e->str=(char*)malloc(e->alloc=512)))
      return NOIR_OOM; }

  if(need>=(e->alloc-e->used)) {
    if(need<0)
      need=2*e->alloc;
    else
      need++;
    need+=e->used;
    need+=(CHUNKLEN-(need%CHUNKLEN));

    if(!(p=(char*)realloc(e->str,need))) {
      free(e->str);
      e->alloc=e->used=0;
      return NOIR_OOM; }
    e->alloc=need;
    e->str=p; }

  memmove(e->str+e->used,bytes,len);
  e->used+=len;
  e->str[e->used]='\0';

  return e->used; }






/*****************************************************************************/



/**
 * sprintf() with automatic buffer allocation. (asprintf-clone)
 * keep in mind that result must have a sane value (NULL, or an existing string
 * that will be freed). address of string may change.
 * @param result - where to store the address of the composed string
 * @param fmt - the format-string to use in composition, see printf()
 * @param ... - the parameters for the %-substitutions in the format-string, see printf()
 */
int esprintf(char **result,char *fmt, ...) {
  int      need,have;
  va_list  ap;
  char    *n;

  if(!result)
    return NOIR_FAIL;

  if(!(n=(char*)malloc(have=512)))
    return NOIR_OOM;

retry:
  va_start(ap,fmt);
  need=vsnprintf(n,have,fmt,ap);
  va_end(ap);

  if((need>=have)||(need<0)) {
    char *p;
    if(need<0)
      need=2*have;
    else
      need++;
    if(!(p=(char*)realloc(n,need))) {
      free(n);
      return NOIR_OOM; }
    have=need;
    n=p;
    goto retry; }

  if(*result)
    free(*result);
  *result=n;

  return need; }



/**
 * sprintf() with automatic buffer allocation. (asprintf-clone)
 * keep in mind that result must have a sane value (NULL, or an existing string
 * that the arguments will be appended to). address of string may change.
 * @param result - where to store the address of the composed string
 * @param fmt - the format-string to use in composition, see printf()
 * @param ... - the parameters for the %-substitutions in the format-string, see printf()
 */
int eappendf(char **result,char *fmt, ...) {
  int      need,have,old=0;
  va_list  ap;
  char    *n;

  if(!result)
    return NOIR_FAIL;

  if(*result)
    old=strlen(*result);

  have=512;
  if(!(n=(char*)malloc(old+have)))
    return NOIR_OOM;
  if(*result)
    strcpy(n,*result);

retry:
  va_start(ap,fmt);
  need=vsnprintf(n+old,have,fmt,ap);
  va_end(ap);

  if((need>=have)||(need<0)) {
    char *p;
    if(need<0)
      need=2*have;
    else
      need++;
    if(!(p=(char*)realloc(n,old+need))) {
      free(n);
      return NOIR_OOM; }
    have=need;
    n=p;
    goto retry; }

  if(*result)
    free(*result);
  *result=n;

  return need; }



/**
 * join strings separated by a delimiter
 * keep in mind that result must have a sane value (NULL, or an existing string
 * that will be released). address of string may change.
 * @return an error-code
 * @param result  where to store the resulting string
 * @param delim   a delimiter (C-string)
 * @param ...  a list of char *, NULL marks end of list
 */
int ejoin(char **result,char *delim, ...) {
  int      dl,cl,ret=NOIR_SUCC;
  va_list  ap;
  char    *e,*n;

  if(!result)
    return NOIR_FAIL;
  if(!delim)
    delim="";
  dl=strlen(delim);

  va_start(ap,delim);
  cl=-dl;
  while((e=va_arg(ap,char *)))
    cl+=strlen(e)+dl;
  va_end(ap);

  if(cl<=0) {
    if(!(n=strdup("")))
      ret=NOIR_OOM; }
  else if(!(n=malloc(cl+1)))
    ret=NOIR_OOM;
  else {
    char *p=n;

    va_start(ap,delim);
    while((e=va_arg(ap,char *))) {
      if(dl&&(p!=n)) {
	strcpy(p,delim);
	p+=dl; }
      strcpy(p,e);
      p+=strlen(p); }
    va_end(ap); }

  if(*result)
    free(*result);
  *result=n;

  return ret; }



/**
 * join strings
 * keep in mind that result must have a sane value (NULL, or an existing string
 * that the other arguments will be appended to). address of string may change.
 * @return an error-code
 * @param result  where to store the resulting string
 * @param ...  a list of char *, NULL marks end of list
 */
int ecat(char **result, ...) {
  int      cl,ret=NOIR_SUCC;
  va_list  ap;
  char    *e,*n;

  if(!result)
    return NOIR_FAIL;

  va_start(ap,result);
  cl=*result?strlen(*result):0;
  while((e=va_arg(ap,char *)))
    cl+=strlen(e);
  va_end(ap);

  if(cl<=0) {
    if(!(n=strdup("")))
      ret=NOIR_OOM; }
  else if(!(n=malloc(cl+1)))
    ret=NOIR_OOM;
  else {
    char *p=n;

    if(*result) {
      strcpy(p,*result);
      p+=strlen(*result); }

    va_start(ap,result);
    while((e=va_arg(ap,char *))) {
      strcpy(p,e);
      p+=strlen(p); }
    va_end(ap); }

  if(*result)
    free(*result);
  *result=n;

  return ret; }






/*****************************************************************************/



/**
 * convert text-buffer from one encoding to another, using iconv.
 * @return a destination buffer that must be free()d.
 * @param src - the source-text
 * @param enc_src - a C-string describing the source-encoding ("ISO_8859-1")
 * @param enc_dst - a C-string describing the destination-encoding ("UTF8")
 */
char *eiconv(const char *src,const char *enc_src,const char *enc_dst) {
  iconv_t  ic;
  char    *dst,*d,*s;
  size_t   sr,dr,dro,    /* characters remaining in src/destination */
           da,du=0,      /* destination allocated/used */
           ret;          /* characters iconv() converted, or <0 for failure */

  if(!src||!enc_src||!enc_dst)
    return NULL;

  if(!strcmp(enc_src,enc_dst))  /* optimization */
    return strdup(src);

  if((ic=iconv_open(enc_dst,enc_src))<0) {
    E(0,"#%d -- %s\n",errno,strerror(errno));
    return NULL; }

  if(!(sr=strlen(src)))
    return strdup("");

/* da=(((sr-(sr%CHUNKCNV))/CHUNKCNV)+1)*CHUNKCNV; */
  da=2*sr+1;

  if(!(d=dst=malloc(dr=da)))
    return NULL;

  s=(char *)src;  /* iconv() wants char * rather than const char * */

  while(sr) {
    dro=dr;
    ret=iconv(ic,&s,&sr,&d,&dr);
    du+=dro-dr;

    if(ret==-1) {
      switch(errno) {
      case E2BIG:
        E(0,"growing buffer\n");
        if(!(d=realloc(dst,da+=CHUNKCNV))) {
          E(0,"growing buffer failed!?\n");
          free(dst);
          return NULL; }
        dst=d;
        E(0,"new buffer at %p...\n",dst);
        dr+=CHUNKCNV;
        break;
      case EILSEQ:
        E(0,"invalid multibyte sequence found (%s->%s):\n%s",enc_src,enc_dst,src);
        sr=0;
        break;
      case EINVAL:
        E(0,"incomplete multibyte sequence found at end of input\n");
        sr=0;
        break; }}}

  iconv_close(ic);

  if(da==du)  /* pathological case */
    dst=realloc(dst,++da);
  dst[du]='\0';
  d=strdup(dst);
  free(dst);

  return d; }



/**
 * determine locale.
 */
const char *elocale(void) {
  static char *locale=NULL;
  if(!locale) {
#ifdef HAVE_LANGINFO_CODESET
    locale=nl_langinfo(CODESET);
#else
    char *l=setlocale(LC_CTYPE,NULL),*p;
    if(l&&(p=strchr(l,'.'))&&(!strncasecmp(p,".UTF8",5)||!strcasecmp(p,".UTF-8",6)))
      locale="UTF-8";
    else
      locale="ISO_8859-1";
#endif
  }
  return locale; }



/**
 * convert C-string in locale-dependent encoding to UTF8.
 * @return a UTF8-string that must be free()d.
 * @param ascii - C-string in locale-dependent encoding
 */
char *e2utf8(const char *ascii) {
  return eiconv(ascii,elocale(),"UTF-8"); }



/**
 * convert C-string in UTF8 encoding to locale-dependent encoding.
 * @return a locale-dependent string that must be free()d.
 * @param utf8 - C-string in UTF8 encoding
 */
char *e2locale(const char *utf8) {
  return eiconv(utf8,"UTF-8",elocale()); }






/*****************************************************************************/



char *url_encode(const char *file,urlencode_opts opt) {
  size_t      l=0;
  const char *s,*h="0123456789ABCDEF";
  char       *t;

  if(!file)
    return NULL;

#define NEED_ESCAPE(x) (((x)<32)||((x)>=127)||strchr(" $&+,/:;=?@<>#%{}|\\^~[]`",(x)))
#define IS_FILE(x) strchr(" $&+,/:;=?@<>#%{}|\^~[]`",(x))

  if((s=file)) {
    while(*s) {
      if(NEED_ESCAPE(*s))
        l+=3;
      else
        l++;
      s++; }

    if(!(t=malloc(l+1)))
      return NULL;

    for(s=file;*s;s++) {
      if((*s==' ')&&(opt&UEO_SPACE2PLUS))
        *(t++)='+';
      else if((opt&UEO_IS_FILE)&&strchr(":/",*s))
        *(t++)='+';
      else if(NEED_ESCAPE(*s)) {
        *(t++)='%';
        *(t++)=h[(*s>>4)&0x0f];
        *(t++)=h[*s&0x0f]; }
      else
        *(t++)=*s; }
    *t='\0';
    return t; }

#undef NEED_ESCAPE
#undef IS_FILE

  return NULL; }



char *url_decode(char *url,int to_utf8) {
  char   *c,*p,*e;
  size_t  l=0,l2;
  char    h[3];

  h[2]='\0';

  if(!(c=strdup(url)))
    return c;

  p=c;
  while(*p&&(*p!=' ')&&(*p!='\n')) {
    l++;
    p++; }
  l2=strlen(p);

  p=c;
  while(l) {
    if(*p=='+')
      *p=' ';
    else if(*p=='%') {
      long z;
      if(!(h[0]=p[1])||!(h[1]=p[2])) {
        E(0,"premature end in %% sequence in URL \"%s\"...\n",url);
        goto fail; }
      z=strtol(h,&e,16);
      if(e!=(h+2)) {
        E(0,"failed to convert %% sequence in URL \"%s\"...\n",url);
        goto fail; }
      if(!z) {
        E(0,"illegal character \\0 in %% sequence in URL \"%s\"...\n",url);
        goto fail; }
      *p=z;
      l-=2;
      memmove(p+1,p+3,l+l2); }
    l--;
    p++; }

  if(to_utf8&&c) {                  /* to UTF8 */
    if((p=e2utf8(c))) {
      free(c);
      c=p; }}

  return c;

 fail:
  if(c)
    free(c);
  return NULL; }



/*****************************************************************************/



/**
 * portable sleep() for threads
 * @return an error-code
 * @param seconds  how long to nap
 */
int thread_sleep(float seconds) {
  pthread_cond_t   cond=PTHREAD_COND_INITIALIZER;
  pthread_mutex_t  mutex=PTHREAD_MUTEX_INITIALIZER;
  struct timeval   now;
  struct timespec  timeout;
#define NANO 1000000000
  int              s=(int)seconds,us=(seconds-s)*1000000;

  pthread_mutex_lock(&mutex);

  gettimeofday(&now,NULL);
  timeout.tv_sec=now.tv_sec+s;
  timeout.tv_nsec=(now.tv_usec+us)*1000;
  while(timeout.tv_nsec>NANO) {
    timeout.tv_nsec-=NANO;
    timeout.tv_sec++; }
  while(pthread_cond_timedwait(&cond,&mutex,&timeout)!=ETIMEDOUT)
    ;

  return NOIR_SUCC; }



/*****************************************************************************/
