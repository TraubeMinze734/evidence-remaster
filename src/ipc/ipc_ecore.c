/** @file  ipc_ecore.c
 *  @brief eCore IPC module for evidence
 *
 * by Azundris, with thanks to Corey Donohoe <atmos@atmos.org>
 */


#include "config.h"
#include "errors.h"
#include "debug.h"
#include "ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#include <Ecore.h>
#include <Ecore_Ipc.h>



/*****************************************************************************/



/**
 * get a data-element from a message we received
 * [type.b][len.w][contents.b]
 * @return an error-code
 * @param m  current position in buffer, will be updated to next item
 * @param r  where to store pointer to resulting data
 */
static int get_string(char **m,char **r) {
  char *q;
  int   l=0;

  if(!m||!*m)
    return NOIR_NODATA;
  if(!r)
    return NOIR_FAIL;
  q=*m;
  if(*q!='s')
    return NOIR_TYPEMISMATCH;
  q++;
  l=(*(q++))<<8;
  l+=*(q++);
  *r=q;
  q+=l;
  *m=q;
  E(1,"got string-%d \"%s\"\n",l,*r);
  return NOIR_SUCC; }






/*****************************************************************************/



/**
 * send message
 * @param e      the connection
 * @param code   an error-code
 * @param reply  the string to send, or NULL
 */
static int send(Ecore_Ipc_Event_Client_Data *e,int code,char *reply) {
  static int our_ref=0;
  int        len=reply?strlen(reply)+1:0;
  our_ref++;
  E(1,"replying [0,0] %d IRT %d => %d {\"%s\":%d}\n",our_ref,e->ref,code,reply?reply:"",len);
  return ecore_ipc_client_send(e->client,0,0,our_ref,e->ref,code,reply,len); }






/*****************************************************************************/



/**
 * handle a client's request
 * @return an error-code
 * @param server  the server-object (ignored)
 * @param e       the request
 */
static int handle_request(Ecore_Ipc_Server *server,Ecore_Ipc_Event_Client_Data *e) {
  long  serial=e->minor;
  int   ret=NOIR_FAIL;
  char *r=NULL,*k,*v,*m=(char *)e->data;

  E(1,"client sent: [%d,%d] #%d (%d) @ %p\n",e->major,e->minor,e->ref,e->size,server);

  switch(e->major) {
    case IPC_CMD_INFO:
      if(get_string(&m,&k)==NOIR_SUCC)
        r=ipc_cmd_info(k);
      break;
    case IPC_CMD_LS:
      if(get_string(&m,&k)==NOIR_SUCC)
        r=ipc_cmd_ls(k);
      break;

    case IPC_PROP_LIST:
      r=ipc_prop_list(serial);
      break;
    case IPC_PROP_DESC:
      if(get_string(&m,&k)==NOIR_SUCC)
        r=ipc_prop_desc(serial,k);
      break;
    case IPC_PROP_GET:
      if(get_string(&m,&k)==NOIR_SUCC)
        r=ipc_prop_get(serial,k);
      break;
    case IPC_PROP_SET:
      if(get_string(&m,&k)==NOIR_SUCC) {
        if(get_string(&m,&v)==NOIR_SUCC)
          return send(e,ipc_prop_set(serial,k,v),NULL); }
      break;

    case IPC_WIN_LIST:
      r=ipc_win_list();
      break;
    case IPC_WIN_NEW:
      if(get_string(&m,&k)==NOIR_SUCC)
        return send(e,k?ipc_win_new(k):NOIR_FAIL,NULL);
      break;
    case IPC_WIN_CWD_FIND:
      if(get_string(&m,&k)==NOIR_SUCC)
        return send(e,ipc_win_cwd_find(k),NULL);
      break;
    case IPC_WIN_CWD_SET:
      if(get_string(&m,&k)==NOIR_SUCC)
        return send(e,k?ipc_win_cwd_set(serial,k):NOIR_FAIL,NULL);
      break;
    case IPC_WIN_CWD_GET:
      r=ipc_win_cwd_get(serial);
      break;
    case IPC_WIN_SEL_SET:
      if(get_string(&m,&k)==NOIR_SUCC)
        return send(e,k?ipc_win_sel_set(serial,k):NOIR_FAIL,NULL);
      break;
    case IPC_WIN_SEL_GET:
      r=ipc_win_sel_get(serial);
      break; }

  ret=send(e,r?NOIR_SUCC:NOIR_FAIL,r);
  if(r) {
    free(r);
    return ret; }
  return NOIR_NOTFOUND; }






/*****************************************************************************/



/**
 * callback: client has connected.  mostly informational.
 */
static int ipc_client_add(void *data,int type,void *event) {
  Ecore_Ipc_Server           **server=(Ecore_Ipc_Server **)data;
  E(1,"Client connected. @ %p\n",server);
  return 1; }



/**
 * callback: client has disconnected.  mostly informational.
 */
static int ipc_client_del(void *data, int type, void *event) {
  Ecore_Ipc_Server           **server=(Ecore_Ipc_Server **)data;
  E(1,"Client disconnected. @ %p\n",server);
  return 1; }



/**
 * callback: client sent us a request.  handle it.
 */
static int ipc_client_sent(void *data,int type,void *event) {
  Ecore_Ipc_Server            **server=(Ecore_Ipc_Server **)data;
  Ecore_Ipc_Event_Client_Data  *e=(Ecore_Ipc_Event_Client_Data *)event;
  handle_request(*server,e);
  return 1; }






/*****************************************************************************/



/**
 * initialize eCore IPC module
 * @return an error-code
 * @param data  where to store our connection handle
 */
int ipc_module_init(void **data) {
  Ecore_Ipc_Server **server=(Ecore_Ipc_Server **)data;
  int                port=0;

  if(!server)
    return NOIR_FAIL;

  if(*server)
    return NOIR_IGNORED;

  ecore_init();
  if(ecore_ipc_init()<1)
    return NOIR_FAIL;

  *server=ecore_ipc_server_add(ECORE_IPC_LOCAL_USER,ECORE_IPC_PIPE,port,NULL);

  ecore_event_handler_add(ECORE_IPC_EVENT_CLIENT_ADD, ipc_client_add, server);
  ecore_event_handler_add(ECORE_IPC_EVENT_CLIENT_DEL, ipc_client_del, server);
  ecore_event_handler_add(ECORE_IPC_EVENT_CLIENT_DATA,ipc_client_sent,server);

  E(1,"Server is listening on " ECORE_IPC_PIPE ".\n");

  return NOIR_SUCC; }



/**
 * deinitialize eCore IPC (destall handlers, drop connection, release resources)
 * @return an error-code
 * @param data  the connection to throw out
 */
int ipc_module_exit(void **data) {
  int                ret=NOIR_SUCC;
  Ecore_Ipc_Server **server=(Ecore_Ipc_Server **)data;

  if(!server)
    return NOIR_FAIL;

  if(*server) {
    E(1,"Shutting down server listening on " ECORE_IPC_PIPE " (%p).\n",*server);
    ecore_ipc_server_del(*server);
    *server=NULL; }

  ecore_ipc_shutdown();

  return ret; }






/*****************************************************************************/



/**
 * check for messages from eCore IPC
 * @return an error-code
 * @param data  the connection
 */
int ipc_module_poll(void **data) {
  Ecore_Ipc_Server **server=(Ecore_Ipc_Server **)data;

  if(!server)
    return NOIR_FAIL;

  ecore_main_loop_iterate();

  return NOIR_SUCC; }



/*****************************************************************************/
