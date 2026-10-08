#ifndef EVAS_BIND
#  define EVAS_BIND

#  include "model.h"
#  include "src/gui/common/session.h"
#  include "src/gui/common/icon_list.h"
#  include "src/gui/efl/common/theme.h"

/* mouse threshhold -- how far we need to move the mouse before a drag is
   recognized.  6 is good. */
#  define FUZZ 12



void         evas_queue_redraw(icon_list *il);

int          evas_icon_chg_event(icon_list *,icon *,file_info *);

int          evas_file_activate(icon_list *il,icon *i,Evas *evas);
void         evas_open_folder(noir_sess *ns,icon_list *il,icon *i,Evas *evas);
int          evas_rename_icon(noir_sess *ns,icon *i);
int          evas_find_icon_by_string_chk(icon_list *il,size_t *l);
int          evas_find_icon_by_string(icon_list *il);
icon        *evas_find_first_selected(icon_list *il);
void         evas_keyed_icon(icon_list *il,int new);
char        *evas_get_file_from_icon(void *data,icon *i);
void         evas_icon_set_state(icon_list *il,icon *i,icon_state s);

int          pointer_on_iconlabel_p(icon *i,int x,int y);
int          sb_chk(icon_list *il,icon *i,int x,int y);

#endif
