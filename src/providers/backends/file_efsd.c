/* access to file-system thru enlightened file-system daemon (efsd) */

#include "evidence.h"
#include "file.h"
#include "file_fun.h"
#include "file_lcl.h"
#include "tree.h"
#include "icon.h"
#include "setup.h"
#include "tree_view.h"
#include "info.h"

char *backend_get_mime(char *myfile) {
  return NULL; }



void backend_monitor_event(noir_sess *ns) {
}



int backend_monitor_add(noir_monitor *m) {
  return NOIR_FAIL; }



int backend_monitor_remove(noir_monitor *m) {
  return NOIR_SUCC; }



int backend_exists(char *myfile) {
  return 0; }



int backend_run(char *p,char *args) {
  return NOIR_FAIL; }



int backend_activate(char *myfile,char *mime,char *app) {
  return NOIR_FAIL; }



int backend_get_info(noir_sess *ns,char *myfile,int *is_dir) {
  return 0; }



int backend_mv(char *xfm,char *xto) {
  return NOIR_FAIL; }



int backend_cp(char *xfm,char *xto) {
 return NOIR_FAIL; }



int backend_mkdir(char *dir,int perms,int hier) {
  return NOIR_FAIL; }



int backend_rmdir(char *dir) {
  return NOIR_FAIL; }



int backend_rm(char *xfile) {
  return NOIR_FAIL; }



int backend_symlink(char *xfm,char *xto) {
  return NOIR_FAIL; }



int backend_hardlink(char *xfm,char *xto) {
  return NOIR_FAIL; }



int backend_get_dir(noir_sess *ns,char *xpath) {
  return 0; }



int backend_exit(noir_sess *ns) {
  return NOIR_SUCC; }



int backend_init(noir_sess *ns) {
  return NOIR_SUCC; }
