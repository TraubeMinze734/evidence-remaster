/** @file  headless.c
 *  @brief stub GUI backend -- used when neither GTK2 nor EFL are available.
 *
 * provides no-op implementations of the GUI entry points the core links
 * against, so the core (model, file, thumbnailer, ...) can be built and
 * tested without a graphical toolkit.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "config.h"
#include "errors.h"
#include "gui/common/session.h"
#include "gui/gui.h"
#include "gui/common/alert.h"
#include "gui/common/menu.h"
#include "gui/common/session.h"
#include "gui/common/session_private.h"
#include "gui/common/tooltip.h"
#include "gui/common/icon_list.h"
#include "gui/common/menu_private.h"
#include "gui/efl/common/theme.h"

noir_sess *gui_init(int argc,char *argv[]) {
  (void)argc; (void)argv;
  fprintf(stderr,"evidence: built without GUI backend (headless).\n");
  return NULL; }

int gui_start(noir_sess *ns) { (void)ns; return NOIR_FAIL; }
int gui_exit(void)            { return NOIR_SUCC; }
void gui_quit(void)           { }

int form_dial(void *ns,alert_type t,alert_buttons b,const char *m1,const char *m2) {
  (void)ns; (void)t; (void)b;
  fprintf(stderr,"evidence: %s (%s)\n",m1?m1:"",m2?m2:"");
  return 0; }

void form_stock(void *ns,alert_stock s) { (void)ns; (void)s; }

int form_yes_no(void *ns,const char *q) {
  (void)ns;
  fprintf(stderr,"evidence: (auto-no) %s\n",q?q:"");
  return 0; }

int form_yes_no_all(void *ns,const char *q,action_context *ac) {
  (void)ns; (void)ac;
  return form_yes_no(ns,q); }

int form_alert(void *ns,const char *fmt,...) {
  va_list ap;
  (void)ns;
  va_start(ap,fmt);
  vfprintf(stderr,fmt,ap);
  fputc('\n',stderr);
  va_end(ap);
  return NOIR_SUCC; }

void form_file_edit(const char *file,char **err) {
  (void)file;
  if(err) *err=NULL; }

int form_modeless_new(void **window,void **data) { (void)window; (void)data; return NOIR_NOTSUPP; }
int form_modeless_dst(void **window)             { (void)window; return NOIR_NOTSUPP; }
int form_modeless_show(void *window)             { (void)window; return NOIR_NOTSUPP; }
int form_modeless_bar_new(void *window,void **bar) { (void)window; (void)bar; return NOIR_NOTSUPP; }
int form_modeless_bar_update(void *bar,const char *label,double done) { (void)bar; (void)label; (void)done; return NOIR_NOTSUPP; }
int form_modeless_label_new(void *window,void **label,const char *text) { (void)window; (void)label; (void)text; return NOIR_NOTSUPP; }
int form_modeless_label_update(void *label,const char *text) { (void)label; (void)text; return NOIR_NOTSUPP; }

int menu_run(action_context *ac,void *menu) { (void)ac; (void)menu; return NOIR_NOTSUPP; }

void *menu_selection_create(action_context *ac) { (void)ac; return NULL; }

long noir_sess_create(sn_mode sm,noir_sess *tmpl,const char *dir) { (void)sm; (void)tmpl; (void)dir; return -1; }
int noir_sess_present(noir_sess *ns) { (void)ns; return NOIR_NOTSUPP; }
int noir_sess_set_window_title(noir_sess *ns,char *utf8) { (void)ns; (void)utf8; return NOIR_NOTSUPP; }
int noir_sess_uidata_new(struct _noir_sess *ns) { (void)ns; return NOIR_SUCC; }
int noir_sess_uidata_free(struct _noir_sess *ns) { (void)ns; return NOIR_SUCC; }

int tooltip_init(noir_sess *ns) { (void)ns; return NOIR_SUCC; }
int tooltip_exit(noir_sess *ns) { (void)ns; return NOIR_SUCC; }
int tooltip_cancel(noir_sess *ns) { (void)ns; return NOIR_SUCC; }

noir_theme *theme_new(noir_sess *session) { (void)session; return NULL; }
int theme_exit(noir_sess *ns) { (void)ns; return NOIR_SUCC; }
int theme_idle_destall(noir_sess *ns) { (void)ns; return NOIR_SUCC; }

void icon_el_data_dst(void *data) { (void)data; }
void icon_move(icon *i,int x,int y) { (void)i; (void)x; (void)y; }
int pointer_on_icon_p(icon_list *il,icon *i,int x,int y) { (void)il; (void)i; (void)x; (void)y; return 0; }
void sb_update(icon_list *il,int sb) { (void)il; (void)sb; }

void *gtk_idle_add_priority(int prio,int (*fun)(void *),void *data) { (void)prio; (void)fun; (void)data; return NULL; }
