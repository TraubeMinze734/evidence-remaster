#ifndef SESSION_H
#  define SESSION_H
#  include "model.h"
#  include "action.h"



typedef struct _noir_sess  noir_sess;
typedef struct _typebuffer typebuffer;



#  include "gui/common/tooltip.h"



typedef enum _sn_mode {
  SESSION_CREATE,
  SESSION_COPY } sn_mode;



int             noir_sess_client_unregister(noir_sess *,client_funs *);
client_funs    *noir_sess_client_register(noir_sess *,const char *,void *);
client_funs    *noir_sess_client_register_full(noir_sess *,const char *,void *,
                                               int (*evt_view)(file_info *,model_event type,file_flags changed,void *),
                                               int (*dst_data)(void **),
                                               int (*set_selection)(void *));
client_funs    *noir_sess_client_get(noir_sess *,const char *);
void           *noir_sess_client_get_data(noir_sess *,const char *);

client_funs    *noir_sess_client_lock(long serial,const char *tag);
void            noir_sess_client_unlock(client_funs *c);

int             noir_sess_tell_clients(noir_sess *,void *,int,int);


noir_sess      *noir_sess_get_1st(void);
noir_sess      *noir_sess_get_next(noir_sess *);
noir_sess      *noir_sess_get_by_serial(long);
noir_sess      *noir_sess_get_by_cwd(const char *);
long            noir_sess_get_serial(noir_sess *);
char           *noir_sess_get_cwd(noir_sess *);
int             noir_sess_set_cwd(noir_sess *ns,const char *cwd);
typebuffer     *noir_sess_get_typebuffer(noir_sess *);
int             noir_sess_set_typebuffer(noir_sess *,typebuffer *tb);
action_context *noir_sess_get_selection(noir_sess *);
int             noir_sess_set_selection(noir_sess *,action_context *);
void           *noir_sess_get_theme(noir_sess *);
void           *noir_sess_get_theme_props(noir_sess *ns);
int             noir_sess_set_theme(noir_sess *,void *);
void           *noir_sess_get_sortflags(noir_sess *);
int             noir_sess_set_sortflags(noir_sess *,int);
int             noir_sess_get_desktop(noir_sess *);

noir_sess      *noir_sess_copy(noir_sess *dst,noir_sess *src);
long            noir_sess_create(sn_mode sm,noir_sess *template,const char *dir);

noir_sess      *noir_sess_new(void);
int             noir_sess_drop(noir_sess *);

int             noir_sess_redraw_files(noir_sess *);
int             noir_sess_redraw_window(noir_sess *);
int             noir_sess_present(noir_sess *);
int             noir_sess_set_window_title(noir_sess *ns,char *utf8);
noir_sess      *noir_sess_close(noir_sess *);

int             noir_sess_focus_verify(noir_sess *,const char *,char **);
int             noir_sess_focus(noir_sess *,const char *);

void           *noir_sess_get_uidata(noir_sess *);
int             noir_sess_set_uidata(noir_sess *,void *);

tooltip_flags   noir_sess_get_tooltip_flags(noir_sess *ns);
int             noir_sess_set_tooltip_flags(noir_sess *ns,tooltip_flags f);

noir_tooltip   *noir_sess_get_tooltip(noir_sess *ns);
int             noir_sess_set_tooltip(noir_sess *ns,noir_tooltip *tt);

#endif
