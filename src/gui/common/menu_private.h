#include <time.h>
#include "action.h"
#include "mime.h"

#define MENU_HANDLERS     "evidence.menu"

#define INFO_MAX          4096



typedef struct _selection_action {
  menu_action              *menu;
  action_context           *ac;
  mime_match                section;
  struct _selection_action *next; } selection_action;



action_context *menu_action_context_new(long serial,char *cwd);
void            menu_action_context_dst(action_context *ac);

menu_action    *menu_action_get_anchor(void);
menu_action    *menu_action_by_mime_first(menu_action **ml,char *mime);
menu_action    *menu_action_by_mime_next(menu_action **ml,char *mime);

int             menu_run(action_context *ac,void *menu);      /* extern */

void           *menu_selection_create(action_context *ac); /* extern */
int             menu_selection_file_add(action_context *ac,const char *file,selection_action **e);
