#define ECORE_IPC_PIPE "evidence"



typedef enum {
  IPC_NONE,
  IPC_PROP_LIST,
  IPC_PROP_DESC,
  IPC_PROP_GET,
  IPC_PROP_SET,  /* end of the codes shared by evidence and econf */

  IPC_CMD_INFO,
  IPC_CMD_LS,

  IPC_WIN_LIST,
  IPC_WIN_NEW,
  IPC_WIN_CWD_GET,
  IPC_WIN_CWD_SET,
  IPC_WIN_CWD_FIND,
  IPC_WIN_SEL_GET,
  IPC_WIN_SEL_SET,

  IPC_LAST
} ev_ipc_call;



char *ipc_prop_list(const long serial);
char *ipc_prop_desc(const long serial,const char *key);
char *ipc_prop_get(const long serial,const char *key);
int   ipc_prop_set(const long serial,const char *key,const char *val);

char *ipc_cmd_info(const char *file);
char *ipc_cmd_ls(const char *file);

char *ipc_win_list(void);
int   ipc_win_new(const char *);
char *ipc_win_sel_get(const long);
int   ipc_win_sel_set(const long,const char *);
char *ipc_win_cwd_get(const long);
int   ipc_win_cwd_set(const long,const char *);
long  ipc_win_cwd_find(const char *);
