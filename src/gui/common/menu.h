#ifndef MENU
#  define MENU

#  include "session.h"
#  include "action.h"

typedef struct _menu_action {
  char                *title;
  char                *mime;
  char                *file;
  void                *lib;
  time_t               ts;
  long                 used;
  struct _menu_action *next;
} menu_action;

int             menu_init(noir_sess *);
int             menu_exit(noir_sess *);

int             menu_path(noir_sess *,int,long);

int             menu_file_context(noir_sess *,const char *,const char *);
int             menu_root_context(noir_sess *,const char *,int);

int             menu_action_by_entry(long serial,menu_action *m,noir_md *r,action_context *ac);
int             menu_action_by_mime(long serial,char *mime,char *file,action_context *ac);

char           *menu_submenu_title_from_mime_type(const char *);

int             menu_lib_gc(void);

int             menu_path(noir_sess *ns,int button,long when);

int             menu_subdirs(noir_sess *ns,file_info *pa,int button);

#endif
