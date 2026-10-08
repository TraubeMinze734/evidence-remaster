#ifndef NOIR_UTIL
#  define NOIR_UTIL

#  include "config.h"
#  include <sys/types.h>

#define TIMER_STOP 0
#define TIMER_CONT 1



#define NOIR_MAX(a,b)  (((a)>(b))?(a):(b))



#  include <signal.h>

typedef struct _Ecore_Event_Exe_Exit { /** Process exit event */
  pid_t      pid;           /**< The process ID of the process that exited */
  int        exit_code;     /**< The exit code of the process */
  void      *exe;           /**< The handle to the exited process, or NULL if not found */
  int        exit_signal;   /**< The signal that caused the process to exit */
  char       exited   :1;   /**< set to 1 if the process exited of its own accord */
  char       signalled:1;   /**< set to 1 id the process exited due to uncaught signal */
  void      *ext_data;      /**< Extension data - not used */
  siginfo_t  data;          /**< Signal info */
} Ecore_Event_Exe_Exit;



typedef enum {
  UEO_IS_DATA=0,
  UEO_IS_FILE=1,
  UEO_SPACE2PLUS=2
} urlencode_opts;

typedef struct _estring {
  char *str;
  int   alloc,used;
} estring;

typedef struct _eslist {
  void           *payload;
  struct _eslist *next;
} eslist;



void     util_sigchld_handler(int num,siginfo_t *si,void *data);
void    *util_sigchld_handler_add(int (*func)(void *data,int type,void *event),const void *data);
void    *util_sigchld_handler_del(void *handle);

char    *util_replace_variables(char *in,const char *cwd,const char *sel);
char    *util_get_token(char **begin,int cook,const char *cwd,const char *sel);
int      util_parse_line(char *,char **,char **,char **,char **);

char    *unit_size(char *size);
ulong    now(long delay);
void     qsrt(void *a[],void *data,int lo,int hi,int (*compare)(const void *,const void *,const void *));
int      dlmulti(char *name,char *file,int flag,void **libr,const char *fmt, ...);



typedef  void (*hash_walker)(char *key,void *value,void *data);

void    *hash_table_new(    void (*freekey)(void *),void (*freeval)(void *));
void    *hash_table_fetch(  void *hashtable,const char *key);
int      hash_table_insert( void *hashtable,const char *key,void *value);
int      hash_table_replace(void *hashtable,const char *key,void *value);
int      hash_table_remove( void *hashtable,char *key);
int      hash_table_dst(   void **hashtable);
int      hash_table_walk(   void *hashtable,hash_walker fun,void *data);

int      eslist_free(eslist **,void (*free_payload)(void *));
int      eslist_next(eslist **);
#define  ESLIST_NEXT(e) (e=e->next)
void    *eslist_payload(eslist **);
#define  ESLIST_PAYLOAD(e) ((e)->payload)
int      eslist_prepend(eslist **,void *);
int      eslist_append(eslist **,void *);

estring *estring_new(int size);
estring *estring_dst(estring *e);
char    *estring_disown(estring *e);
char    *estring_free(estring *e,int release_payload);   /* glib compat */
size_t   estring_length(estring *e);
int      estring_printf(estring *e,char *fmt, ...);
int      estring_appendf(estring *e,char *fmt, ...);
int      estring_appendb(estring *e,size_t len,char *bytes);
int      estring_truncate(estring *e,int size);
#define  ESTRING_GET_CSTRING(a) ((a)->str)

int      esprintf(char **result,char *fmt, ...);
int      eappendf(char **result,char *fmt, ...);
int      ejoin(char **result,char *delim, ...);
int      ecat(char **result, ...);

const char *elocale(void);
char    *e2utf8(const char *ascii);
char    *e2locale(const char *utf8);
char    *eiconv(const char *src,const char *enc_src,const char *enc_dst);

char    *url_encode(const char *file,urlencode_opts opt);
char    *url_decode(char *url,int in_place);

int      thread_sleep(float seconds);

void    *timeout_add(uint f,int(*fun)(void *),void *data);
int      timeout_remove(void **handle);

void    *idle_enter_add(int prio,int (*fun)(void *),void *data);
int      idle_enter_remove(void **handle);

int      events_pending(void);

int      mainloop_once(void);
int      mainloop_quit(void);

#endif
