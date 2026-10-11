#include "gui/common/session.h"
#include <gdk/gdk.h>

int selection_update(noir_sess *,client_funs *);
int selection_contains(noir_sess *,const char *);
int selection_remove(noir_sess *,char *);
int clipboard_set(GdkAtom,const char *);
int clipboard_get(GdkAtom,noir_sess *);
int clipboard_files_set(action_context *,int);
char **clipboard_files_get(GdkAtom,int *);
int clipboard_paste(noir_sess *,GdkAtom);
