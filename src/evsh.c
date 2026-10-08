/** @file  evsh.c
 *  @brief shell-specific parts of evsh of evsh (evidence shell).
 *  generic client library in evclient.c
 */

#include "config.h"

#include <Ecore.h>
#include <Ecore_Ipc.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_LIBREADLINE
#  define HISTORY "/.evidence/evsh_history"
#  if defined(HAVE_READLINE_READLINE_H)
#    include <readline/readline.h>
#  elif defined(HAVE_READLINE_H)
#    include <readline.h>
#  else
extern char *readline();
#  endif
char *cmdline=NULL;
#endif

#ifdef HAVE_READLINE_HISTORY
#  if defined(HAVE_READLINE_HISTORY_H)
#    include <readline/history.h>
#  elif defined(HAVE_HISTORY_H)
#    include <history.h>
#  else
extern void add_history();
extern int  write_history();
extern int  read_history();
#  endif
#endif



#include "evidence.h"
#include "errors.h"
#include "ipc.h"
#include "evclient.h"



#define OUT "< "
#define ERR "! "
#define IN  "> "

#define MI 4096

#define HELP_ALL "evsh -- evidence shell"


typedef enum {
  P_NONE=0,
  P_SERIAL=1,
  P_DIR=2,
  P_FILE=4,
  P_KEY=8,
  P_VALUE=16,
  P_SILENT=32,
  P_HELPONLY=64,
  P_LAST=P_HELPONLY } para;


typedef struct _call {
  ev_ipc_call  id;
  char        *name;
  para         signature;
  const char  *help; } call;



static call calls[]={
  { IPC_NONE,         "win",           P_HELPONLY, "List of window-related commands" },
  { IPC_WIN_LIST,     "win-list",      P_NONE, "win.list\nList all of evidence's windows, giving window-ID and current directory for each." },
  { IPC_WIN_NEW,      "win-new",       P_DIR, "win.new <directory>\nOpen a new window showing <directory>." },
  { IPC_WIN_SEL_GET,  "win-sel-get",   P_SERIAL, "win.sel.get <window-id>\nShow the selection displayed in window <window-id>." },
  { IPC_WIN_SEL_SET,  "win-sel-set",   P_SERIAL|P_DIR, "win.sel.set <window-id> <selection>\nMake the window <window-id> select the given item(s) <selection>." },
  { IPC_WIN_CWD_GET,  "win-cwd-get",   P_SILENT|P_SERIAL, "win.cwd.get <window-id>\nShow the name of the directory displayed in window <window-id>." },
  { IPC_WIN_CWD_GET,  "win-pwd",       P_SERIAL, "win.pwd -- short for win.pwd.get, see there." },
  { IPC_WIN_CWD_SET,  "win-cwd-set",   P_SILENT|P_SERIAL|P_DIR, "win.cwd.set <window-id> <dir>\nMake the window <window-id> show the contents of directory <dir>." },
  { IPC_WIN_CWD_SET,  "win-cwd",       P_SILENT|P_HELPONLY, "List of working-directory related functions." },
  { IPC_WIN_CWD_SET,  "win-cd",        P_SERIAL|P_DIR, "win.cd -- short for win.cwd.set, see there." },
  { IPC_WIN_CWD_FIND, "win-cwd-find",  P_SILENT|P_DIR, "win.cwd.find <dir>\nReturn the ID of the first window showing the contents of directory <dir>." },
  { IPC_WIN_CWD_FIND, "win-find",      P_DIR, "win.find -- short for win.cwd.find, see there." },

  { IPC_NONE,         "prop",          P_HELPONLY, "List of property-related commands" },
  { IPC_PROP_LIST,    "prop-list",     P_SERIAL, "prop.list <window-id>\nList all properties that are currently registered for window <window-id>." },
  { IPC_PROP_DESC,    "prop-describe", P_SERIAL|P_KEY, "prop.desc <window-id> <prop>\nDescribe property <prop> set on window <window-id>." },
  { IPC_PROP_GET,     "prop-get",      P_SERIAL|P_KEY, "prop.get <window-id> <prop>\nShow the value of property <prop> set on window <window-id>." },
  { IPC_PROP_SET,     "prop-set",      P_SERIAL|P_KEY|P_VALUE, "prop.set <window-id> <prop> <new-value>\nSet the value of property <prop> set on window <window-id> to <new-value>." },
  { IPC_PROP_LIST,    "list",          P_SILENT|P_SERIAL, "list -- short for prop.list, see there." },
  { IPC_PROP_DESC,    "describe",      P_SILENT|P_SERIAL|P_KEY, "desc -- short for prop.desc, see there." },
  { IPC_PROP_DESC,    "explain",       P_SILENT|P_SERIAL|P_KEY, "desc -- short for prop.desc, see there." },
  { IPC_PROP_GET,     "print",         P_SILENT|P_SERIAL|P_KEY, "get -- short for prop.get, see there." },
  { IPC_PROP_GET,     "get",           P_SILENT|P_SERIAL|P_KEY, "get -- short for prop.get, see there." },
  { IPC_PROP_SET,     "set",           P_SILENT|P_SERIAL|P_KEY|P_VALUE, "set -- short for prop.get, see there." },
  { IPC_PROP_SET,     "let",           P_SILENT|P_SERIAL|P_KEY|P_VALUE, "let -- short for prop.get, see there." },

  { IPC_NONE,         "cmd",           P_HELPONLY, "List of miscellaneous commands" },
  { IPC_CMD_INFO,     "cmd-info",      P_FILE, "cmd.info <file>\nDisplay info on <file>." },
  { IPC_CMD_INFO,     "info",          P_SILENT|P_FILE, "info -- short for cmd.info, see there." },
  { IPC_CMD_LS,       "cmd-ls",        P_DIR, "ls <dir>\nDisplay contents of directory <dir>." },
  { IPC_CMD_LS,       "ls",            P_SILENT|P_DIR, "ls -- short for cmd.ls, see there." },
  { IPC_CMD_LS,       "dir",           P_SILENT|P_DIR, "dir -- short for cmd.ls, see there." },
};



int debug=1;



/*****************************************************************************/



/**
 * callback: connected
 * @param data connstate variable
 * @param type event-type
 * @param event the event
 */
int ev_ipc_server_con(void *data,int type,void *event) {
  connstate                  *cs=(connstate *)data;
  *cs=ONLINE;
  E(1,"Connected to " ECORE_IPC_PIPE ".\n");
  return 1; }



/**
 * callback: disconnected
 * @param data connstate variable
 * @param type event-type
 * @param event the event
 */
int ev_ipc_server_dis(void *data,int type,void *event) {
  connstate                  *cs=(connstate *)data;
  *cs=OFFLINE;
  ecore_main_loop_quit();
  E(1,"Disconnected.\n");
  return 1; }



/**
 * callback: connected
 * @param data reserved
 * @param type event-type
 * @param event the event
 */
int ev_ipc_server_sent(void *data,int type,void *event) {
  Ecore_Ipc_Event_Server_Data *e=(Ecore_Ipc_Event_Server_Data *)event;
  E(2,"evidence sent: %2d.%02d  #%03d=>#%03d  \"%s\".%d\n",
    e->major,e->minor,0,0,(char *)e->data,e->size);
  if(e->data&&(e->size>0))
    printf(OUT "%s\n",(char *)e->data);
  else if(e->response<0)
    printf(OUT "error #%d\n",e->response);
  else if(!e->response)
    printf(OUT "OK\n");
  else
    printf(OUT "result: %d\n",e->response);
  ecore_main_loop_quit();
  return 1; }



/*****************************************************************************/
/* parser */



/**
 * get serial number from answer
 * @return the serial number, or -1 for none
 * @param m the answer
 */
static long get_serial(char **m) {
  long ret=-1;
  if(m&&*m) {
    char *b;
    ret=strtol(*m,&b,10);
    if(b==*m)
      ret=0;
    else {
      while(isspace(*b))
        b++;
      *m=b; }}
  return ret; }



/**
 * get token from input
 * @return an error-code
 * @param beg start of current token (changed)
 * @param end end of current token (changed)
 * @param tol change token to lower-case where needed?
 */
static int get_token(char **beg,char **end,int tol) {
  int     l;
  char   *b=*beg,*e;

  if(!b||!*b)
    return NOIR_NODATA;

  while(isspace(*b))
    b++;
  e=b;

  if(*e=='\"') {
    do {
      e++;
      if(*e=='\\')
        e++;
    } while(*e&&*e!='\"'); }
  else {
    while(*e&&!isspace(*e)) {
      if(tol&&isupper(*e))
        *e=tolower(*e);
      e++; }}

  if(*e) {
    *(e++)='\0';
    while(*e&&isspace(*e))
      *(e++)='\0';
    *end=e; }
  else
    *end=NULL;
  *beg=b;
  l=e-b;
  return (l<=0)?NOIR_NODATA:l; }



/*****************************************************************************/



/**
 * process a command (entered in shell, parsed in evsh_parse_line()).
 * at this point, the command is known, but its parameters still have to
 * be parsed. the function will do this, then send the request to evidence.
 * @return an error-code
 * @param server connection to evidence
 * @param cp the command to send
 * @param line the input-buffer containing the parameters for the command
 */
static int handle_any(Ecore_Ipc_Server **server,call *cp,char *line) {
  long  serial=-1;
  int   ret,pars=0,l=0;
  char *b=line,*e,*file=NULL,*k=NULL,*v=NULL,*m=NULL;

  if(cp->signature&P_SERIAL)
    serial=get_serial(&b);

  if(cp->signature&(P_DIR|P_FILE)) {
    if((ret=get_token(&b,&e,FALSE))<0)
      return ret;
    file=b;
    pars++;
/*  b=e?e+1:NULL; */
    b=e; }

  if(cp->signature&(P_KEY)) {
    if((ret=get_token(&b,&e,TRUE))<0)
      return ret;
    k=b;
    pars++;
/*  b=e?e:NULL; */
    b=e; }

  if(cp->signature&(P_VALUE)) {
    if((ret=get_token(&b,&e,FALSE))<0)
      return ret;
    v=b;
    pars++;
/*  b=e?e+1:NULL; */
    b=e; }

  if(file)
    ev_send_append(&m,&l,file);
  if(k)
    ev_send_append(&m,&l,k);
  if(v)
    ev_send_append(&m,&l,v);

  E(3,"found \"%s\" |serial:%ld|file:%s|key:%s|value:%s|\n",cp->name,serial,file,k,v);

  ret=ev_ipc_send(server,cp->id,serial,m,l);
  if(m)
    free(m);
  return ret; }






/*****************************************************************************/



/**
 * clean user-input
 * compress space, make sure strings are terminated,
 * turn arbitrary hierarchy notation to lisp-style
 * ("my.fancy.command" -> "my-fancy-command")
 * @param c buffer holding user input (modified)
 */
static char *clean(char *c) {
  char   *p=c;
  size_t  l;

  while(*p) {
    if(*p=='\"') {
      do {
        p++;
        if(*p=='\\')
          p++;
      } while(*p&&*p!='\"');
      if(!*p) {
        E(0,"unterminated string in input?\n");
        *c='\0';
        return c; }
      else
        p++; }
    else if(isspace(*p)) {    /* turn any sort of space into a single ' ' */
      l=strlen(p);
      while(isspace(p[1]))
        memmove(p,p+1,l--);
      *(p++)=' '; }
    else
      p++; }

  if((l=strlen(c))) {
    while(isspace(*c))
      memmove(c,c+1,l--);
    while(isspace(c[l-1]))
      c[l-1]='\0'; }

  p=c;
  while(*p&&!isspace(*p)) {
    if(!isalpha(*p)) {
      while(p[1]&&!isalpha(p[1]))
        memmove(p,p+1,strlen(p));
      *p='-'; }
    p++; }

  return c; }



/**
 * compare two tokens, of one the first may be abbreviated
 * so that "m-f-c" matches "my-fancy-call"
 * @return <0,0,>0 see strcmp()
 * @param a (possibly abbreviated) token
 * @param t token
 */
static int abbrevcmp(char *a,char *t) {
  size_t  l1;
  char   *p,*q;
  int     ret;

  while(1) {
    if(!(p=strchr(a,'-')))
      p=a+strlen(a);
    l1=p-a;
    if((ret=strncasecmp(a,t,l1)))
      return ret;

    a=p;
    if(*a)
      a++;

    if(!(q=strchr(t,'-')))
      t=t+strlen(t);
    else
      t=q+1;

    if(!*a)
      return *t?-1:0;
    else if(!*t)
      return 1; }}



/**
 * find a (possibly abbreviated) token in command-list
 * @return the command-struct, or NULL for not found
 * @param b the token
 */
static call *find_call(char *b) {
  int          nc=sizeof(calls)/sizeof(call);
  call        *cp=calls,*r=NULL;
  while(nc-->0) {
    if(!abbrevcmp(b,cp->name)) {
      if(r) {
        printf(ERR "\"%s\" is not unique. (\"%s\",\"%s\")\n",b,r->name,cp->name);
        return NULL; }
      r=cp; }
    cp++; }
  return r; }



/**
 * find a token consisting of initials ("mfc") in command-list
 * turns "mfc" into "m-f-c" for this.
 * @return the command-struct, or NULL for not found
 * @param beg the start of the potential command-token
 * @param end the end of the potential command-token
 */
static call *find_abbrev(char **beg,char **end) {
  char        *b=*beg,*m,*q;
  size_t       l=strlen(b);
  call        *cp=calls;

  if(!(q=m=malloc(l*2)))
    return NULL;

  while(*b) {
    *(q++)=*(b++);
    *(q++)='-'; }
  *(--q)='\0';

  cp=find_call(m);

  free(m);
  return cp; }



/**
 * find a token ("my-fancy-command") in command-list
 * @return the command-struct, or NULL for not found or not delimited
 * @param beg the start of the potential command-token
 * @param end the end of the potential command-token
 */
static call *find_with_delims(char **beg,char **end) {
  char *b=*beg;
  int   d=0;

  while(*b) {
    if(*b=='-')
      d++;
    b++; }
  return d?find_call(*beg):NULL; }



/**
 * find a token ("my fancy command") in command-list
 * turns "my fancy command" into "my-fancy-command" for this.
 * @return the command-struct, or NULL for not found
 * @param beg the start of the potential command-token
 * @param end the end of the potential command-token
 */
static call *find_with_spaces(char **beg,char **end) {
  call *cp=NULL,*lcp=NULL;
  char *e=*end,*b=*beg,*d=NULL;

  if(!*end)
    return find_call(b);

  while(1) {
    if(!(cp=find_call(b))) {
      *e=' ';
      if(d) {
        *d='\0';
        *end=d; }
      return lcp; }
    lcp=cp;
    d=e;
    *(e++)='-';
    if(!(e=strchr(e,' '))) {
      if((cp=find_call(b))) {
        *end=NULL;
        return cp; }
      *end=d;
      return lcp; }
    *e='\0'; }}



/**
 * parse input from shell.
 * @return an error-code
 * @param server the connection to evidence
 * @param line the input we received from the user
 */
static int evsh_parse_line(Ecore_Ipc_Server **server,char *line) {
  call        *cp=NULL;
  char        *c=NULL,*b=line,*e=NULL,*p;
  int          ret=NOIR_SUCC,h=0;

  if(!line||!*line)
    return NOIR_IGNORED;

  if(!(c=strdup(line)))
    return NOIR_OOM;

  if((p=clean(c))) {
    if(!strcasecmp(p,"help")) {
      int   nc=sizeof(calls)/sizeof(call);
      cp=calls;
      puts(HELP_ALL);
      while(nc-->0) {
        if((cp->signature&P_HELPONLY)&&!(cp->signature&P_SILENT))
          printf("  %s\n",cp->name);
        cp++; }
      goto done; }

    else if(!strncasecmp(p,"help ",5)) {
      h++;
      p+=5; }

    b=p;
    if((e=strchr(b,' ')))
      *e='\0';
    if(!(cp=find_with_delims(&b,&e))) {
      if(!(cp=find_with_spaces(&b,&e))) {
        if(e)
          *e='\0';
        cp=find_abbrev(&b,&e); }}
    if(!cp)
      ret=NOIR_NOTFOUND;
    else {
      if(h)
        puts(cp->help);
      else if(cp->signature&P_HELPONLY) {
        int   nc=sizeof(calls)/sizeof(call),l=strlen(cp->name);
        call *r=calls;

        if(cp->help&&*cp->help)
          puts(cp->help);
        while(nc-->0) {
          if(!strncmp(cp->name,r->name,l)&&(r->name[l]=='-')&&!(r->signature&P_SILENT))
            printf("  %s\n",r->name);
          r++; }}
      else {
        E(3,"parsed: \"%s\" => %s { \"%s\" }\n",b,cp->name,e?e:"");
        if((ret=handle_any(server,cp,e?e+1:""))==NOIR_NODATA)
          E(0,ERR "Insufficient parameters for %s: %s\n",cp->name,cp->help?cp->help:"");
        else if(ret)
          E(0,"ret: %d\n",ret); }}}
  else
    ret=NOIR_FAIL;

 done:
  free(c);
  return ret; }






/*****************************************************************************/



/**
 * evidence textual shell (mainloop).
 * @param argc number of command-line arguments
 * @param argv command-line arguments
 */
int main(int argc,char **argv) {
  Ecore_Ipc_Server *server=NULL;
  int               ret=NOIR_SUCC,cc=0;
  connstate         cs=OFFLINE;
  char             *p,*f=NULL;
#ifndef HAVE_LIBREADLINE
  char              buf[MI];
#elif defined(HAVE_READLINE_HISTORY)
  char             *history=NULL;

  using_history();
  stifle_history(96);
  if((p=getenv("HOME"))) {
    if((history=malloc(strlen(p)+sizeof(HISTORY)))) {
      strcat(strcpy(history,p),HISTORY);
      read_history(history); }}
#endif

  ecore_init();
  ecore_app_args_set(argc,(const char **)argv);
  ecore_event_handler_add(ECORE_EVENT_SIGNAL_EXIT,ev_ipc_sigexit,&cs);

reconnect:
  cc++;
  if((ret=ev_ipc_init(&server,&cs))!=NOIR_SUCC)
    E(0,"%sconnect to " ECORE_IPC_PIPE " failed: %d\n",(cc>1)?"re":"",ret);
  else {
    while(1) {
      p=NULL;
      ecore_main_loop_iterate();
      if(cs==OFFLINE) {
        ev_ipc_exit(&server);
        cs=ONLINE;
        goto reconnect; }

#ifdef HAVE_LIBREADLINE
      if((p=readline(IN))) {
#else
      printf(IN);
      if((f=p=fgets(buf,MI,stdin))) {
#endif
        char *q=strchr(p,'\n');
        if(q)
          *q='\0';
        if(!strcasecmp(p,"exit")||!strcasecmp(p,"quit")) {
          if(f)
            free(f);
          break; }

#ifdef HAVE_READLINE_HISTORY
        add_history(p);
#endif
        ret=evsh_parse_line(&server,p);
        if((ret<NOIR_SUCC)&&(ret!=NOIR_IGNORED))
          printf(ERR "error #%d: \"%s.\"\n",-ret,noir_error(ret));
        if(f)
          free(f); }
      else
        break; }}

  ev_ipc_exit(&server);
  ecore_shutdown();

#ifdef HAVE_READLINE_HISTORY
  if(history) {
    write_history(history);
    free(history); }
#endif

  return ret; }



/*****************************************************************************/
