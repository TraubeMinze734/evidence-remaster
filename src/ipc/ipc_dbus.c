/** @file  ipc_dbus.c
 *  @brief DBUS IPC module for evidence
 *
 * this implements a service (with defined messages), but can be changed
 * into a generic filter with little effort.
 *
 * tested with DBUS 0.12 -- deprecated
 *
 * by Azundris
 */

#define DBUS_API_SUBJECT_TO_CHANGE

#include "config.h"
#include "errors.h"        /* NOIR_* error codes */
#include "debug.h"         /* debug-macros E(), D() */
#include "ipc.h"           /* defines for evidence-specific part */

#include <errno.h>
#include <sys/select.h>
#include <dbus/dbus.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>



/* some evidence-specific defines */
#define DBUS_PREFIX "com.azundris.Evidence"
#define PROP ".Prop"
#define WIN  ".Win"
#define CMD  ".Cmd"



int ipc_module_exit(void **);



DBusMessageHandler *handler=NULL;






/*****************************************************************************/
/* watches */
/***********/


typedef enum { WS_NEW, WS_ON, WS_OFF } ipc_watch_state;

typedef struct _ipc_watch {
  struct _ipc_watch *next;
  DBusWatch         *watch;
  ipc_watch_state    status;
} ipc_watch;

ipc_watch *watches=NULL;



/**
 * helper function: find DBUS-watch in our list -- do we already have this one?
 * @return an ipc_watch struct, or NULL
 * @param watch  the DBUS-watch to look for
 */
static ipc_watch *watch_find(DBusWatch *watch) {
  ipc_watch *w=watches;
  while(w) {
    if(w->watch==watch)
      return w;
    w=w->next; }
  return NULL; }



/**
 * callback: tell main-loop that there's a new file-descriptor to watch
 * dbus_watch_get_fd() gets the fd from the watch, dbus_watch_get_flags()
 * gets the "flags" (DBUS_WATCH_READABLE/DBUS_WATCH_WRITABLE).
 * cf http://freedesktop.org/Software/dbus/doc/api/html/group__DBusConnection.html#a36
 * @return TRUE
 * @param watch  the DBUS-watch (from which we can get the fd)
 * @param data   reserved
 */
static dbus_bool_t watch_add(DBusWatch *watch,void *data) {
  ipc_watch *w;

  E(3,"watch_add %d\n",dbus_watch_get_fd(watch));

  if((w=malloc(sizeof(ipc_watch)))) {
    memset(w,0,sizeof(ipc_watch));
    w->watch=watch;
    w->status=WS_ON;
    w->next=watches;
    watches=w; }

  return TRUE; }



/**
 * callback: tell main-loop to "pause" watched file-descriptor
 * @param watch  the DBUS-watch (from which we can get the fd)
 * @param data   reserved
 */
static void watch_toggle(DBusWatch *watch,void *data) {
  ipc_watch *w=watch_find(watch);
  if(w) {
    w->status=dbus_watch_get_enabled(watch)?WS_ON:WS_OFF;
    E(3,"watch_toggle %d: %d\n",dbus_watch_get_fd(watch),w->status); }}



/**
 * callback: tell main-loop to forget about a file-descriptor
 * @param watch  the DBUS-watch (from which we can get the fd)
 * @param data   reserved
 */
static void watch_del(DBusWatch *watch,void *data) {
  ipc_watch *w=watch_find(watch);
  E(3,"watch_del %d\n",dbus_watch_get_fd(watch));
  if(w) {
    if(w==watches)
      watches=watches->next;
    else {
      ipc_watch *a=watches;
      while(a&&a->next) {
        if(a->next==w) {
          a->next=w->next;
          a=NULL; }
        else
          a=a->next; }}
    free(w); }}






/*****************************************************************************/



/**
 *
 * @param connection  the connection on which the dispatch-status changed
 * @param status      the new status
 * @param data        unused
 * If the dispatch status is DBUS_DISPATCH_DATA_REMAINS, then
 * dbus_connection_dispatch() needs to be called to process incoming
 * messages, BUT NOT from inside this function!  It should simply set
 * flag indicating that messages should be dispatched in the next
 * iteration of the main-loop.
 * http://freedesktop.org/Software/dbus/doc/api/html/group__DBusConnection.html#a39
 */
static void dispatch_status(DBusConnection *connection,DBusDispatchStatus status,void *data) {
  return; }






/*****************************************************************************/
/* timeouts */
/************/



/**
 * callback: tell the main-loop to add a timeout
 * http://freedesktop.org/Software/dbus/doc/api/html/group__DBusConnection.html#a37
 * @param timeout  the timeout to remove
 * @param data     unused
 */
static dbus_bool_t timeout_add(DBusTimeout *timeout,void *data) {
  E(0,"timeout_add\n");
  return TRUE; }



/**
 * callback: tell the main-loop to delete a timeout
 * @param timeout  the timeout to remove
 * @param data     unused
 */
static void timeout_del(DBusTimeout *timeout,void *data) {
  E(0,"timeout_del\n"); }



/**
 * callback: enable/disable a timeout
 * main-loop can do this "bookkeeping", or just query
 * dbus_timeout_get_enabled() every time the timeout fires
 * @param timeout  the timeout to toggle
 * @param data     reserved
 */
static void timeout_toggle(DBusTimeout *timeout,void *data) {
  E(0,"timeout_toggle\n"); }







/*****************************************************************************/
/* send reply */
/**************/



/**
 * reply with an error-code
 * @return NOIR_SUCC if we could send our reply, NOIR_FAIL otherwise
 * @param connection  the connection to use to send the reply
 * @param message     the message to reply to
 * @param error       a DBUS-error to send in reply
 */
int reply_err(DBusConnection *connection,DBusMessage *message,DBusError *error) {
  DBusMessage     *r;
  if((r=dbus_message_new_error_reply(message,error->name,error->message))&&
     dbus_connection_send(connection,r,NULL)) {
    dbus_message_unref(r);
    return NOIR_SUCC; }
  else
    return NOIR_FAIL; }



/**
 * reply with a string
 * @return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS
 * @param connection  the connection to use to send the reply
 * @param message     the message to reply to
 * @param plaintext   the string to send in reply
 */
static DBusHandlerResult reply_str(DBusConnection *connection,DBusMessage *message,
                                 char *plaintext) {
  DBusError        error;
  DBusMessage     *reply;
  DBusMessageIter  iter;
  int              ret=NOIR_OOM;

  dbus_error_init(&error);

  if((reply=dbus_message_new_reply(message))) {
    dbus_message_append_iter_init(message,&iter);
    if(dbus_message_iter_append_string(&iter,plaintext)&&
       dbus_connection_send(connection,reply,NULL))
      ret=NOIR_SUCC;
    dbus_message_unref(reply); }

  if(ret==NOIR_OOM)
    E(0,"out of memory in reply_str()\n");

  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS; }



/**
 * reply with an integer
 * @return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS
 * @param connection  the connection to use to send the reply
 * @param message     the message to reply to
 * @param rep         the integer to send in reply
 */
static DBusHandlerResult reply_int(DBusConnection *connection,DBusMessage *message,
                                   int rep) {
  DBusError        error;
  DBusMessage     *reply;
  DBusMessageIter  iter;
  int              ret=NOIR_OOM;

  dbus_error_init(&error);

  if((reply=dbus_message_new_reply(message))) {
    dbus_message_append_iter_init(message,&iter);
    if(dbus_message_iter_append_int32(&iter,rep)&&
       dbus_connection_send(connection,reply,NULL))
      ret=NOIR_SUCC;
    dbus_message_unref(reply); }

  if(ret==NOIR_OOM)
    E(0,"out of memory in reply_int()\n");

  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS; }






/*****************************************************************************/
/* generic handlers */
/********************/



/**
 * print DBUS message (debug)
 * @param message  the DBUS message to print
 */
static void handle_generic(DBusMessage *message) {
  DBusMessageIter  iter;
  const char      *sender=dbus_message_get_sender(message);
  char            *str;
  dbus_int32_t     int32;
  int              type;

  E(0,"received \"%s\" from \"%s\":\n",
    dbus_message_get_name(message),
    sender?sender:"(someone)");
  
  dbus_message_iter_init(message,&iter);

  do {
    switch((type=dbus_message_iter_get_arg_type(&iter))) {
    case DBUS_TYPE_INVALID:
      break;

    case DBUS_TYPE_STRING:
      str=dbus_message_iter_get_string(&iter);
      E(0,"  \"string:%s\"\n",str);
      break;

    case DBUS_TYPE_INT32:
      int32=dbus_message_iter_get_int32(&iter);
      E(0,"  int32:%d\n",int32);
      break;
  
    default:
      E(0,"  type-%d\n",type);
      break; }} while(dbus_message_iter_next(&iter));
  E(0,"\n"); }



/**
 * echo -- send back a string we were sent
 * @return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS
 * @param connection  the connection this came in on
 * @param message     the message we were sent
 */
static DBusHandlerResult handle_echo(DBusConnection *connection,DBusMessage *message) {
  DBusError        error;
  DBusMessage     *r;
  char            *s;

  dbus_error_init(&error);

  if(!dbus_message_get_args(message,&error,DBUS_TYPE_STRING,&s,DBUS_TYPE_INVALID)) {
    if((r=dbus_message_new_error_reply(message,error.name,error.message))&&
       dbus_connection_send(connection,r,NULL))
      dbus_message_unref(r); }
  else
    return reply_str(connection,message,s);
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS; }






/*****************************************************************************/
/* evidence-specific handlers */
/******************************/



/**
 * get/set properties in evidence
 * @return the result of any of the reply_*() functions (DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS)
 * @param connection  the connection the request was sent on
 * @param message     the request
 * @param m           the sub-command ("minor")
 */
static DBusHandlerResult handle_prop(DBusConnection *connection,DBusMessage *message,char *m) {
  DBusError          error;
  char              *k,*v,*r=NULL;
  dbus_int32_t       serial;
  DBusHandlerResult  ret=-1;

  dbus_error_init(&error);

  E(0,"got \"%s\"\n",m);

  if(!strcmp(m,"Set")) {
    if(!dbus_message_get_args(message,&error,DBUS_TYPE_INT32,&serial,DBUS_TYPE_STRING,&k,DBUS_TYPE_STRING,&v,DBUS_TYPE_INVALID))
      reply_err(connection,message,&error);
    else {
      ret=reply_int(connection,message,ipc_prop_set(serial,k,v)); }}

  else if(!strcmp(m,"Get")) {
    if(!dbus_message_get_args(message,&error,DBUS_TYPE_STRING,&k,DBUS_TYPE_INVALID))
      reply_err(connection,message,&error);
    else {
      r=ipc_prop_get(serial,k); }}

  else if(!strcmp(m,"Desc")) {
    if(!dbus_message_get_args(message,&error,DBUS_TYPE_STRING,&k,DBUS_TYPE_INVALID))
      reply_err(connection,message,&error);
    else {
      r=ipc_prop_desc(serial,k); }}

  else if(!strcmp(m,"List")) {
    if(!dbus_message_get_args(message,&error,DBUS_TYPE_STRING,&k,DBUS_TYPE_INVALID))
      reply_err(connection,message,&error);
    else {
      r=ipc_prop_list(serial); }}

  else
    E(0,"internal error in handle_prop(): someone registered for \"%s\", but we don't handle it...\n",m);

  if(ret<0)
    r=strdup("What?");
  if(r) {
    ret=reply_str(connection,message,r);
    free(r); }

  return ret; }



/**
 * call window functions in evidence
 * @return the result of any of the reply_*() functions (DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS)
 * @param connection  the connection the request was sent on
 * @param message     the request
 * @param m           the sub-command ("minor")
 */
static DBusHandlerResult handle_win(DBusConnection *connection,DBusMessage *message,char *m) {
  char              *sub[]={ "List", "Ls", "New", "Cd", "Pwd", "Find" };
  char              *p,*r=NULL;
  int                c=0,nc=sizeof(sub)/sizeof(char *);
  DBusError          error;
  DBusHandlerResult  ret=-1;
  dbus_int32_t       serial;

  if(!strncmp(m,"Cwd.",4)) {
    m+=4;
    if(!strcmp(m,"Set"))
      c=3;
    else if(!strcmp(m,"Get"))
      c=4;
    else if(!strcmp(m,"Find"))
      c=5;
    else
      c=-1; }
  else if(!strncmp(m,"Sel.",4)) {
    m+=4;
    if(!strcmp(m,"Set"))
      c=7;
    else if(!strcmp(m,"Get"))
      c=6;
    else
      c=-1; }
  else {
    while((c<nc)&&strcmp(m,sub[c]))
      c++;
    if(c>=nc)
      c=-1; }

  E(0,"got \"%s\"\n",(c>=0)?m:"???");

  switch(c) {
    case 0:  /* List */
    case 1:
      r=ipc_win_list();
      break;

    case 2:  /* New */
      if(!dbus_message_get_args(message,&error,DBUS_TYPE_STRING,&p,DBUS_TYPE_INVALID))
        reply_err(connection,message,&error);
      else
        ret=reply_int(connection,message,ipc_win_new(p));
      break;

    case 3:  /* Cd */
      if(!dbus_message_get_args(message,&error,DBUS_TYPE_INT32,&serial,DBUS_TYPE_STRING,&p,DBUS_TYPE_INVALID))
        reply_err(connection,message,&error);
      else
        ret=reply_int(connection,message,ipc_win_cwd_set(serial,p));
      break;

    case 4: /* Pwd */
      if(!dbus_message_get_args(message,&error,DBUS_TYPE_INT32,&serial,DBUS_TYPE_INVALID))
        reply_err(connection,message,&error);
      else
        ret=reply_int(connection,message,ipc_win_cwd_get(serial));
      break;

    case 5: /* Find */
      if(!dbus_message_get_args(message,&error,DBUS_TYPE_STRING,&p,DBUS_TYPE_INVALID))
        reply_err(connection,message,&error);
      else
        ret=reply_int(connection,message,ipc_win_cwd_find(p));
      break;

    case 6: /* GetSel */
      if(!dbus_message_get_args(message,&error,DBUS_TYPE_INT32,&serial,DBUS_TYPE_INVALID))
        reply_err(connection,message,&error);
      else
        ret=reply_int(connection,message,ipc_win_cwd_get(serial));
      break;

    case 7: /* SetSel */
      if(!dbus_message_get_args(message,&error,DBUS_TYPE_INT32,&serial,DBUS_TYPE_STRING,&p,DBUS_TYPE_INVALID))
        reply_err(connection,message,&error);
      else
        ret=reply_int(connection,message,ipc_win_cwd_set(serial,p));
      break;

    default:
      break; }

  if(ret<0)
    r=strdup("What?");

  if(r) {
    ret=reply_str(connection,message,r);
    free(r); }

  return ret; }



/**
 * call a command (ls, file-info, ...) within evidence
 * @return the result of any of the reply_*() functions (DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS)
 * @param connection  the connection the request was sent on
 * @param message     the request
 * @param m           the sub-command ("minor")
 */
static DBusHandlerResult handle_cmd(DBusConnection *connection,DBusMessage *message,char *m) {
  DBusError          error;
  char              *r=NULL,*p;
  DBusHandlerResult  ret=-1;

  E(0,"got \"%s\"\n",m);

  if(!dbus_message_get_args(message,&error,DBUS_TYPE_STRING,&p,DBUS_TYPE_INVALID))
    reply_err(connection,message,&error);
  else if(!strcmp(m,"Ls")||!strcmp(m,"List")||!strcmp(m,"Dir"))
    r=ipc_cmd_ls(p);
  else if(!strcmp(m,"Info"))
    r=ipc_cmd_info(p);

  if(!r)
    r=strdup("What?");

  ret=reply_str(connection,message,r);
  free(r);

  return ret; }






/*****************************************************************************/
/* request dispatch */
/********************/



/**
 * callback: DBUS sent us stuff
 * @return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS
 * @param handler     the handle of this DBUS-handler
 * @param connection  the connection
 * @param message     the message we were sent
 * @param user_data   &connection (in case we need to call ipc_module_exit())
 */
static DBusHandlerResult client_sent(DBusMessageHandler *handler,
                                     DBusConnection     *connection,
                                     DBusMessage        *message,
                                     void               *user_data) {
  char *msg=(char *)dbus_message_get_name(message),*m;

  handle_generic(message);

  if(!strncmp(DBUS_PREFIX ".",msg,sizeof(DBUS_PREFIX))) {
    m=msg+sizeof(DBUS_PREFIX)-1;

    if(!strncmp(m,PROP,sizeof(PROP)-1))
      return handle_prop(connection,message,m+sizeof(PROP));

    else if(!strncmp(m,WIN,sizeof(WIN)-1))
      return handle_win(connection,message,m+sizeof(WIN));

    else if(!strncmp(m,CMD,sizeof(CMD)-1))
      return handle_cmd(connection,message,m+sizeof(CMD));

    else if(!strcmp(m,".Version"))
      return reply_str(connection,message,"evidence " VERSION " a/o " __DATE__);

    else if(!strcmp(m,".Echo"))
      return handle_echo(connection,message);

    else
      E(0,"internal error in client_sent(): registered for \"%s\", but no handler feels responsible!?\n",m); }

  else if(dbus_message_has_name(message,DBUS_MESSAGE_LOCAL_DISCONNECT)) {
    dbus_connection_disconnect(connection);
    E(0,"QUIT!\n");
    ipc_module_exit(user_data); }

  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS; }






/*****************************************************************************/
/* PUBLIC */
/**********/



/**
 * initialize DBUS IPC module
 * @return an error-code
 * @param data  where to store our connection handle
 */
int ipc_module_init(void **data) {
  DBusMessage         *message;
  DBusError            error;
  int                  ret=NOIR_SUCC,result;
  DBusConnection     **connection=(DBusConnection **)data;
  const char          *to_handle[]={ DBUS_PREFIX ".Version",
                                     DBUS_PREFIX ".Echo",
                                     DBUS_PREFIX ".Prop.List",
                                     DBUS_PREFIX ".Prop.Set",
                                     DBUS_PREFIX ".Prop.Get",
                                     DBUS_PREFIX ".Prop.Desc",
                                     DBUS_PREFIX ".Cmd.Ls",
                                     DBUS_PREFIX ".Cmd.List",
                                     DBUS_PREFIX ".Cmd.Dir",
                                     DBUS_PREFIX ".Cmd.Info",
                                     DBUS_PREFIX ".Win.List",
                                     DBUS_PREFIX ".Win.New",
                                     DBUS_PREFIX ".Win.Cwd.Get",
                                     DBUS_PREFIX ".Win.Cwd.Set",
                                     DBUS_PREFIX ".Win.Cwd.Find",
                                     DBUS_PREFIX ".Win.Sel.Get",
                                     DBUS_PREFIX ".Win.Sel.Set",
                                     DBUS_PREFIX ".Win.Pwd",
                                     DBUS_PREFIX ".Win.Cd",
                                     DBUS_PREFIX ".Win.Find",
                                     DBUS_MESSAGE_LOCAL_DISCONNECT };

  if(!connection)
    return NOIR_FAIL;

  if(*connection)
    return NOIR_IGNORED;

  dbus_error_init(&error);
  if(!(*connection=dbus_bus_get(DBUS_BUS_SESSION,&error))) {
    E(0,"failed to open connection: %s\n",error.message);
    ret=NOIR_NOTFOUND;
    goto done; }

  if(!(message=dbus_message_new(DBUS_MESSAGE_HELLO,DBUS_SERVICE_DBUS))) {
    ret=NOIR_OOM;
    goto done; }

 done:
  dbus_error_free(&error);

  if(ret==NOIR_SUCC) {
    dbus_connection_set_dispatch_status_function(*connection,dispatch_status,NULL,NULL);
    dbus_connection_set_watch_functions(*connection,watch_add,watch_del,watch_toggle,NULL,NULL);
    dbus_connection_set_timeout_functions(*connection,timeout_add,timeout_del,NULL,NULL,NULL);

    if(!(handler=dbus_message_handler_new(client_sent,connection,NULL)))
      return NOIR_OOM;
    if(!dbus_connection_register_handler(*connection,handler,to_handle,
                                         sizeof(to_handle)/sizeof(const char *)))
      return NOIR_OOM;

    result=dbus_bus_acquire_service(*connection,DBUS_PREFIX,0,&error);
    if(dbus_error_is_set(&error)) {
      E(0,"failed to acquire service \"%s\"...\n",error.message);
      dbus_error_free(&error);
      return NOIR_FAIL; }}

  return ret; }



/**
 * deinitialize DBUS IPC (destall handlers, drop connection, release resources)
 * @return an error-code
 * @param data  the connection to throw out
 */
int ipc_module_exit(void **data) {
  DBusConnection **connection=(DBusConnection **)data;

  if(!connection)
    return NOIR_FAIL;

  if(!*connection)
    return NOIR_IGNORED;

  dbus_connection_set_watch_functions(*connection,NULL,NULL,NULL,NULL,NULL);
  dbus_connection_set_timeout_functions(*connection,NULL,NULL,NULL,NULL,NULL);
  dbus_connection_set_dispatch_status_function(*connection,NULL,NULL,NULL);

  dbus_connection_unref(*connection);
  dbus_message_handler_unref(handler);
  dbus_shutdown();

  return NOIR_SUCC; }



/**
 * check for messages from DBUS
 * this should be integrated with eCore at some point (don't do our own
 * select() etc.)
 * @return an error-code
 * @param data  the connection
 */
int ipc_module_poll(void **data) {
  ipc_watch       *a=watches;
  DBusConnection **connection=(DBusConnection **)data;

  fd_set           r,w;
  int              fd,fd_max,n;
  unsigned int     flags;
  struct timeval   t;

  if(!connection)
    return NOIR_FAIL;
  if(!*connection)
    return NOIR_IGNORED;

restart:
  t.tv_sec=0;
  t.tv_usec=0;

  FD_ZERO(&r);
  FD_ZERO(&w);

  fd_max=-1;
  while(a) {
    if(a->status==WS_ON) {
      flags=dbus_watch_get_flags(a->watch);

      if((fd=dbus_watch_get_fd(a->watch))>fd_max)
        fd_max=fd;

      if(flags&DBUS_WATCH_READABLE)
        FD_SET(fd,&r);
      else if(flags&DBUS_WATCH_WRITABLE)
        FD_SET(fd,&w); }
    a=a->next; }

  if(fd_max>=0) {
    if((n=select(fd_max+1,&r,&w,NULL,&t))<0) {
      if(errno==EINTR)             /* interrupted, restart */
        goto restart;
      else
        return NOIR_FAIL; }
    else if(n>0) {
      for(a=watches;a;a=a->next) {
        fd=dbus_watch_get_fd(a->watch);
        if(FD_ISSET(fd,&r)) {
          E(3,"fd %d became readable...\n",fd);
          n=dbus_watch_handle(a->watch,DBUS_WATCH_READABLE); }
        else if(FD_ISSET(fd,&w)) {
          E(3,"fd %d became writable...\n",fd);
          n=dbus_watch_handle(a->watch,DBUS_WATCH_WRITABLE); }
        else {
          E(0,"fd %d assumed unknown state!?...\n",fd);
          n=TRUE; }
        if(!n) {
          E(0,"out of memory in dbus_watch_handle()!?\n");
          return NOIR_OOM; }}}}

  dbus_connection_dispatch(*connection);
  dbus_connection_flush(*connection);
  return NOIR_SUCC; }



/*****************************************************************************/
