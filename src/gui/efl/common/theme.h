#ifndef NOIR_THEME
#  define NOIR_THEME
#  include "config.h"
#  include "model.h"
#  include "properties.h"
#  include "gui/common/session.h"
#  include "gui/common/icon_list.h"

#  ifdef HAVE_EVAS2
#    include <Evas.h>
#  endif


#  define THEME_CFG "evidence.theme"



typedef enum _noir_theme_flags {
  TF_NONE=0,
  TF_DOTDIR=1 } noir_theme_flags;



typedef enum _ic_mode {
  IC_NONE=0,
  IC_W=1,
  IC_H=2,
  IC_WH_W=4,
  IC_WH_H=8,
  IC_AUTO=16,
  IC_FILL=32 } ic_mode;



#ifdef HAVE_EVAS2
typedef enum {
  EV_NONE=0,
  EV_SINGLE,
  EV_DOUBLE,
  EV_TREBLE,
  EV_RELEASED,
  EV_MOTIONED,
  EV_DRAGGING,
  EV_DROPPED } theme_area_event_type;

typedef enum {
  EV_MOD_NONE=0,
  EV_MOD_SELECT_RANGE,
  EV_MOD_SELECT_MULTI,
  EV_MOD_NEW_WINDOW } theme_area_event_modifiers;




typedef struct _noir_theme noir_theme;



typedef struct _theme_area {
  void *user1,*user2,*user3,*user4;
  void *lib;
  int  (*ext_upd)(icon_list *il,icon *i,void *data);
  int  (*ext_dst)(icon *i);
  int  (*ext_die)(struct _noir_theme *t,icon *i);
  int  (*ext_evt)(icon_list *il,icon *i,int x,int y,int b,theme_area_event_type t,theme_area_event_modifiers m,void *data,void **rp);
} theme_area;
#else

typedef struct _noir_theme noir_theme;

#endif



struct _noir_theme {
  void    *session;

  char    *name,*path;

  ic_mode  icon_mode;

  /* this basically exports WITH_DOT_DIR and others.
     that way, plugins don't have to be re-compiled to conform to the
     local setup, they just get the config of the main program from
     this indicator. */
  noir_theme_flags tf;

  char    *shelf_bg,*iconview_bg;

  int      icon_width,icon_height;

  /* vvv   used only by evas icon_view */

  void    *tb_lib;
  void    *tb_data;
  void     (*tb_redraw)(icon_list *);
  icon_el *(*tb_set)(icon_list *il,void *,char *,int);
  void     (*tb_dst)(void **);
  int      (*tb_die)(noir_theme *);

  void    *sb_lib;
  void    *sb_data;
  void    *(*sb_new)(noir_theme *,void *);
  void     (*sb_set)(void *,icon *,int,int,int,int);
  void     (*sb_dst)(void **);
  int      (*sb_die)(noir_theme *);

  void    *tt_lib;
  void    *tt_data;
  void    *(*tt_new)(void *,int,int,file_info *);
  void     (*tt_dst)(void **);
  int      (*tt_die)(noir_theme *);

  void    *rubber_lib;
  void    *rubber_data;
  void    *(*rubber_new)(icon_list *);
  int      (*rubber_set)(icon_list *,int,int,int,int);
  void     (*rubber_dst)(void **);
  int      (*rubber_die)(noir_theme *);

  void    *icon_lib;
  void    *icon_data;

  icon_el *(*icon_els_create)(icon_list *il,icon *icon,const char *thumb);
  int      (*icon_set_state) (icon_list *il,icon *icon,icon_state);
  int      (*icon_die)(noir_theme *);

  void    *bg_lib;
  void    *bg_data;
  void    *(*bg_new)(noir_theme *,icon_list *,char *);
  void     (*bg_dst)(void **);
  void     (*bg_upd)(void *,int x,int y,int w,int h);
  void     (*bg_scr)(void *);
  int      (*bg_die)(noir_theme *);

  void    *idle_iconview_lib;
  int      (*idle_init_iconview)(void *,void **,noir_theme *);
  int      (*idle_exit_iconview)(void **);
  int      (*idle_func_iconview)(void **);
  void    *idle_hndl_iconview;
  int      idle_freq_iconview;
  void    *idle_data_iconview;

  void    *idle_shelf_lib;
  int      (*idle_init_shelf)(void *,void **,noir_theme *);
  int      (*idle_exit_shelf)(void **);
  int      (*idle_func_shelf)(void **);
  void    *idle_hndl_shelf;
  int      idle_freq_shelf;
  void    *idle_data_shelf;

  void    *ani_open_folder_lib;
  void    *ani_open_folder_data;
  void     (*ani_open_folder)(icon_list *,icon *);
  int      (*ani_open_folder_die)(noir_theme *);

  void    *ani_open_app_lib;
  void    *ani_open_app_data;
  void     (*ani_open_app)(icon_list *,icon *);
  int      (*ani_open_app_die)(noir_theme *);

  void    *ani_open_file_lib;
  void    *ani_open_file_data;
  void     (*ani_open_file)(icon_list *,icon *);
  int      (*ani_open_file_die)(noir_theme *);

  icon    *areas;

  /* ^^^ used only by evas icon_view */

  noir_prop_bag *data;
};



char          *theme_font_find(const noir_theme *,const char *);

int            theme_dump(const void *);

int            theme_parse_line(void *data,char *key,char *val);

#  ifdef HAVE_EVAS2
char          *theme_ms(const noir_theme *,const char *,const char *,const char *,const char *,const char *);
#  endif

int            theme_init(noir_sess *,char *);
int            theme_exit(noir_sess *);
noir_theme    *theme_new(noir_sess *session);
int            theme_canvas_destall(icon_list *);
int            theme_idle_start(noir_sess *);
int            theme_idle_destall(noir_sess *ns);

int            theme_get_iconsize(const noir_theme *t,int *w,int *h);
ic_mode        theme_get_iconmode(const noir_theme *t);

#endif
