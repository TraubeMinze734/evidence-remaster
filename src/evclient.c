/** @file  evclient.c
 *  @brief library for evidence-clients (such as evsh)
 */

#include "config.h"

#include <Ecore.h>
#include <Ecore_Ipc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



#include "errors.h"
#include "ipc.h"
#include "evclient.h"



/*****************************************************************************/



/**
 * get string-representation for evidence error-code
 * @return a verbal description of the error
 * @param no an error-code
 */
const char *noir_error(int no) {
  static const char *noir_errstr[]={
    "Success",
    "General failure",
    "No data",
    "Action was not necessary",
    "Out of memory",
    "Not found",
    "Resource unavailable",
    "Parameters were of the wrong type",
    "Maximum length for path exceeded",
    "Partial success",
    "Parameter is not an executable file",
    "The type of monitor you are trying to set does not match the type of the object you are trying to set the monitor on",
    "File handed to meta-data provider has invalid contents",
    "File-I/O error in meta-data provider",
    "Could not activate meta-data provider",
    "Action requires a file",
    "Action not supported" };

  if(no<0)
    no=-no;
  if(no<=-NOIR_NOTSUPP)
    return noir_errstr[no];
  return "No such error"; }



static int print_data(char *d,size_t l) {
  char *e=d+l,*f;
  while(d<e) {
    l=(d[1]<<8)|d[2];
    if(*d=='s') {
      f=d+3;
      printf("  STR-%d: \"%s\"\n",l,f); }
    else
      printf("  skipping chunk type '%c' {%d bytes}...\n",*d,l);
    d+=3+l; }
  return NOIR_SUCC; }



/*****************************************************************************/



/**
 * initialize eCore-IPC and connect to evidence
 * @return an error-code
 * @param srv **server-struct (must point to valid location containing NULL)
 * @param cs pointer to a connection-state marker (online/offline)
 */
int ev_ipc_init(Ecore_Ipc_Server **srv,connstate *cs) {
  if(ecore_ipc_init()<1)
    return NOIR_FAIL;
  if(!srv)
    return NOIR_FAIL;
  if(*srv)
    return NOIR_IGNORED;
  if((*srv=ecore_ipc_server_connect(ECORE_IPC_LOCAL_USER,ECORE_IPC_PIPE,0,NULL))) {
    ecore_event_handler_add(ECORE_IPC_EVENT_SERVER_ADD, ev_ipc_server_con,cs);
    ecore_event_handler_add(ECORE_IPC_EVENT_SERVER_DEL, ev_ipc_server_dis,cs);
    ecore_event_handler_add(ECORE_IPC_EVENT_SERVER_DATA,ev_ipc_server_sent,NULL);
    return NOIR_SUCC; }

  *srv=NULL;
  return NOIR_NOTFOUND; }



/**
 * disconnect from evidence and shutdown eCore-IPC
 * @return an error-code
 * @param srv **server-struct
 */
int ev_ipc_exit(Ecore_Ipc_Server **srv) {
  if(!srv)
    return NOIR_FAIL;
  if(*srv)
    ecore_ipc_server_del(*srv);
  *srv=NULL;
  ecore_ipc_shutdown();
  return NOIR_SUCC; }



/**
 * dummy signal-handler for eCore (to ignore ^C)
 */
Eina_Bool ev_ipc_sigexit(void *data, int type, void *event) {
  return 0; }



/*****************************************************************************/



/**
 * send a request to evidence
 * @return an error-code
 * @param srv **server struct
 * @param major number of request
 * @param minor serial number of session to affect
 * @param data payload
 * @param size length of payload, of -1 for auto (strlen(payload)+1)
 */
int ev_ipc_send(Ecore_Ipc_Server **srv,int major,int minor,void *data,int size) {
  static int ref=0;
  int        ret;

  if(!srv)
    return NOIR_FAIL;
  if(!*srv)
    return NOIR_NODATA;

  if(size<0)
    size=data?strlen(data)+1:0;

  E(3,"sending request to evidence...\n");
  ret=ecore_ipc_server_send(*srv,major,minor,++ref,0,0,data,size);
  E(2,"we sent: %2d.%02d  #%03d  \"%s\".%d  =>  %d\n",
    major,minor,ref,(char *)data,size,ret);

  if((debug>=2)&&(size>0))
    print_data(data,size);

  ecore_main_loop_begin();

  return NOIR_SUCC; }



/*****************************************************************************/


/**
 * append string to that in send-buffer.
 * each string thus appended will have a 3-byte header (1 byte type ('s'),
 * 2 bytes length (network order)).
 * @return an error-code
 * @param mp the send buffer (address may change due to realloc())
 * @param lp current number of bytes in send-buffer (will be updated)
 * @param dp data to append
*/
int ev_send_append(char **mp,int *lp,char *dp) {
  size_t l=strlen(dp)+1+1+2+*lp,l2;
  char *m;

  if(!(m=realloc(*mp,l)))
    return NOIR_OOM;

  l2=*lp;
  *mp=m;
  *lp=l;
  m+=l2;
  l2=strlen(dp)+1;

  *(m++)='s';
  *(m++)=l2<<8;
  *(m++)=l2%256;
  strcpy(m,dp);

  return NOIR_SUCC; }



/*****************************************************************************/
