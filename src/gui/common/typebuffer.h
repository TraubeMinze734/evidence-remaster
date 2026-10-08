#ifndef TYPEBUFFER_H
#  define TYPEBUFFER_H
#  include "session.h"

typedef enum {
  REM_RE=0,
  REM_GLOB=1
} re_mode;

typedef enum {
  TB_NONE=0,
  TB_OPEN=1,
  TB_RSEARCH=2,
  TB_RRSEARCH=4
} tb_flags;

int   typebuffer_init(void);                    /* setup typebuffer subsys */
int   typebuffer_exit(void);                    /* shutdown typebuffer subsys */
int   typebuffer_new(noir_sess *);              /* create typebuffer */
int   typebuffer_dst(noir_sess *);              /* destroy typebuffer */
int   typebuffer_tab(noir_sess *);              /* "tab" -> complete input */
int   typebuffer_do(noir_sess *);               /* "return" -> do stuff */
int   typebuffer_open(noir_sess *);             /* "esc" -> open typebuffer */
int   typebuffer_update(noir_sess *);           /* show typebuffer */
int   typebuffer_close(noir_sess *);            /* "esc" -> cancel */
int   typebuffer_tab_reset(noir_sess *ns,size_t l); /* not tabbed, reset tab */

int   typebuffer_history_traverse(typebuffer *tb,int count);
int   typebuffer_history_search(typebuffer *tb,const char *substr);

int   typebuffer_is_open(noir_sess *ns);        /* is it open now? */

const char *typebuffer_get(noir_sess *ns);      /* get contents */
int   typebuffer_set(noir_sess *,const char *); /* change contents */

const char *typebuffer_get_tab(noir_sess *ns);      /* get tab */
int   typebuffer_set_tab(noir_sess *,const char *); /* change tab */

int   typebuffer_set_flags(noir_sess *ns,tb_flags fl);
int   typebuffer_add_flags(noir_sess *ns,tb_flags fl);
int   typebuffer_clr_flags(noir_sess *ns,tb_flags fl);
tb_flags typebuffer_get_flags(noir_sess *ns);

int   re_add(noir_sess *,const char *,re_mode);
int   re_match(noir_sess *,const char *);

#endif
