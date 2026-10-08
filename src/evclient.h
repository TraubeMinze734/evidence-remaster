typedef enum { OFFLINE,ONLINE } connstate;

/* in client */
int ev_ipc_server_con(void *data,int type,void *event);
int ev_ipc_server_dis(void *data,int type,void *event);
int ev_ipc_server_sent(void *data,int type,void *event);

/* in client lib */
const char *noir_error(int no);
int ev_ipc_init(Ecore_Ipc_Server **srv,connstate *cs);
int ev_ipc_exit(Ecore_Ipc_Server **srv);
int ev_ipc_sigexit(void *data, int type, void *event);
int ev_ipc_send(Ecore_Ipc_Server **srv,int major,int minor,void *data,int size);
int ev_send_append(char **mp,int *lp,char *dp);

extern int debug;

#ifndef E
#  define E(lvl,fmt,args...) do { if(debug>=(lvl)) fprintf(stderr,fmt,## args); } while(0)
#endif
