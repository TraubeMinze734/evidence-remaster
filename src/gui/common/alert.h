#include "evidence.h"
#include "action.h"

typedef enum {
  ALERT_STALE,
  ALERT_CANNOT_START,
  ALERT_CANNOT_OPEN,
  ALERT_EMPTY,
} alert_stock;

typedef enum {
  ALERT_QUESTION,
  ALERT_ERROR
} alert_type;

typedef enum {
  ALERT_BUTTONS_CUSTOM,
  ALERT_BUTTONS_YES_NO,
  ALERT_BUTTONS_OK
} alert_buttons;

int       form_dial(void *,alert_type,alert_buttons,const char *,const char *);
void      form_stock(void *,alert_stock);
int       form_yes_no(void *,const char *);
int       form_yes_no_all(void *,const char *,action_context *);
int       form_alert(void *,const char *,...);
void      form_file_edit(const char *,char **);

void      form_threads_enter(void);
void      form_threads_leave(void);

int       form_modeless_dst(void **);
int       form_modeless_bar_update(void *,const char *,double);
int       form_modeless_bar_new(void *,void **);
int       form_modeless_label_update(void *,const char *);
int       form_modeless_label_new(void *,void **,const char *);
int       form_modeless_new(void **,void **);
int       form_modeless_show(void *);
