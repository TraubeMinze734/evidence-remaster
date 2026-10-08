#ifndef TOOLTIP_H
#  define TOOLTIP_H

#  include "gui/common/session.h"

#  define TOOLTIP_ICON_SIZE  384
#  define TOOLTIP_DELAY     1000



typedef enum {
  TOOLTIP_DISABLE=0,
  TOOLTIP_ENABLE=1,
  TOOLTIP_MOUSE =2
} tooltip_flags;



typedef enum {
  TTO_NONE=0,
  TTO_GTK2=1,
  TTO_EVAS=2
} tooltip_open;



typedef struct _noir_tooltip noir_tooltip;



int           tooltip_init(noir_sess *ns);
int           tooltip_exit(noir_sess *ns);
int           tooltip_maybe(noir_sess *,char *,int,int);
int           tooltip_show_for(noir_sess *,char *,int,int);
int           tooltip_cancel(noir_sess *);
const char   *tooltip_get_file(noir_sess *);
tooltip_open  tooltip_is_open(noir_sess *ns);

#endif
