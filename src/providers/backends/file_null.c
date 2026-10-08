/** @file  file_null.c
 *  @brief dummy file-system.
 */

#include <string.h>      /* str...() */
#include <stdio.h>       /* stderr */

#include "mime.h"
#include "file.h"        /* MY_NAME, fs_dir */
#include "file_lcl.h"    /* file_if_local() */
#include "errors.h"
#include "util.h"
#include "monitors.h"    /* file_monitor_do_add() */
#include "model.h"       /* file_...(), file_info */
#include "meta.h"        /* meta_...() */

#include "model_private.h"



/*****************************************************************************/
/* abstraction */
/***************/



/**
 * get item's name (UTF8) from its tag (path, UUID, ...)
 * @return a newly-allocated UTF8-string containing the item's name
 * @param path  the tag/URL/UUID denoting the item
 */
char *backend_tag2name(const char *path) {
  char *name=strrchr(path,DIR_DELIMITER);
  return e2utf8(name?name+1:path); }






/*****************************************************************************/
/* file-ops (mv, cp, mkdir, ...) */
/*********************************/



/**
 * check whether an item exists in the file-system
 * @return 0 for not found, the file's mode as per stat() otherwise
 * @param myfile  the item to check for
 */
int backend_exists(const char *myfile) {
  return 0; }



/**
 * move item to folder, or rename item
 * @return an error-code
 * @param job  the job
 */
int backend_mv(noir_job *job) {
  return NOIR_NOTSUPP; }



/**
 * copy item to folder, or copy item renaming the copy
 * @return an error-code
 * @param job  the job
 */
int backend_cp(noir_job *job) {
  return NOIR_NOTSUPP; }



/**
 * remove an item from the file-system (including any items it may contain)
 * @return an error-code
 * @param job  the job
 */
int backend_rm(noir_job *job) {
  return NOIR_NOTSUPP; }



/**
 * make directory
 * @return an error-code
 * @param dir    the path to create
 * @param perms  the permissions for the path to be created
 * @param hier   FALSE: try to create last element in path only; TRUE: try to create any missing items along the path
 */
int backend_mkdir(char *dir,int perms,int hier) {
  return NOIR_NOTSUPP; }



/**
 * remove an empty directory
 * @return an error-code
 * @param dir  the directory to remove
 */
int backend_rmdir(char *dir) {
  return NOIR_NOTSUPP; }



/**
 * symlink
 * @return an error-code
 * @param xfm  from item...
 * @param xto  ...to item
 */
int backend_symlink(char *xfm,char *xto) {
  return NOIR_NOTSUPP; }



/**
 * hardlink
 * @return an error-code
 * @param xfm  from item...
 * @param xto  ...to item
 */
int backend_hardlink(char *xfm,char *xto) {
  return NOIR_NOTSUPP; }



/**
 * where does this symlink point to?
 * @return a newly-allocated tag describing the target of the symlink, or NULL
 * @param myfile  where to find the symlink in the file-system
 */
char *backend_readlink(char *myfile) {
  return NULL; }






/*****************************************************************************/
/* JOBS */
/********/


/**
 * the job is done, clean-up (join thread or whatever)
 * optional function, may not exist in all backends.
 * @return an error-code
 * @param job
 */
int backend_job_collect(noir_job *job) {
  return NOIR_NOTSUPP; }



/**
 * start a new async job
 * @return a negative error-code, or a positive job-ID
 * @param job  the job to start
 */
int backend_job_start(noir_job *job) {
  return NOIR_NOTSUPP; }






/*****************************************************************************/
/* MIME */
/********/



/**
 * set default (double-click) action for files of a given MIME-type
 * @return an error-code
 * @param mime     the MIME-type we want to register a handler for (may contain *)
 * @param handler  the handler to register (app-name or template, cf file_run())
 */
int backend_mime_handler_set(char *mime,char *handler) {
  return NOIR_NOTSUPP; }



/**
 * get default (double-click) action for files of a given MIME-type
 * @return the handler (app-name or template, cf file_run()), or NULL
 * @param mime  the MIME-type
 * @param match  where to store the MIME-type that was matched (or NULL)
 */
char *backend_mime_handler_get(const char *mime,char **match) {
  return NULL; }



/**
 * figure out a file's MIME-type
 * @return a MIME-type that must not be free()d
 * @param myfile  the file to analyze
 */
const char *backend_get_mime(const char *myfile) {
  return MIME_DK_TYPE; }






/*****************************************************************************/
/* monitors */
/************/



/**
 * add a monitor -- tell us if the file/directory changes
 * @return an error-code
 * @param m  the monitor to add (prepared in file_monitor_add())
 */
int backend_monitor_add(noir_monitor *m) {
  return NOIR_NOTSUPP; }



/**
 * remove a monitor -- we're no long interested in that file/directory
 * @return an error-code
 * @param m  the monitor to add (prepared in file_monitor_add())
 */
int backend_monitor_remove(noir_monitor *m) {
  return NOIR_NOTSUPP; }






/*****************************************************************************/
/* activation */
/**************/



/**
 * start a file (double-click etc.)
 * if there is a default action for files of this MIME-type, use that.
 * otherwise, if file is executable, start the file itself.
 * @return an error-code
 * @param myfile  a local file
 * @param mime    the file's MIME-type, or NULL for auto-determine
 */
int backend_activate(char *myfile,const char *mime) {
  return NOIR_NOTSUPP; }






/*****************************************************************************/
/* meta-data */
/*************/

/**
 * set group of meta-data on file
 * @return an error-code
 * @param ns        the session
 * @param filename  the file
 * @param md        the list of meta-data
 * @param r         first meta-data item in group to set
 */

int backend_set_meta(void *ns,char *filename,noir_md *md,noir_meta *r) {
  return NOIR_NOTSUPP; }



/**
 * get backend-specific meta-data (ACL, EA)
 * @return an error-code
 * @param ns      the session
 * @param myfile  the file
 * @param md      the meta-data list to add to
 */
int backend_get_meta(void *ns,char *myfile,noir_md *md) {
  return NOIR_NOTSUPP; }



/**
 * get stat-info and put into posixmd struct ("intrinsic" of file_info)
 * @return an error-code
 * @param ns      the session
 * @param myfile  the file
 * @param info    where the pointer to a file_info is store (*info may be NULL, but info mustn't be). allocate file-info and/or posixmd as applicable.
 */
int backend_get_info(void *ns,const char *myfile,file_info **info) {
  return NOIR_NOTSUPP; }






/*****************************************************************************/
/* directories */



/**
 * open a directory (for use in directory scanner only)
 * @return a handle for this directory
 * @param path  the directory to open
 */
void *backend_opendir(char *path) {
  return NULL; }



/**
 * read an entry from a directory (for use in directory scanner only)
 * @return an error-code
 * @param data      a handle for this directory (from backend_opendir())
 * @param dir_path  path to the directory
 * @param fp        a buffer (char[PATH_MAX+1]) to return the entry's full path/tag in
 */
int backend_readdir(void **data,char *dir_path,char *fp) {
  return NOIR_NOTSUPP; }



/**
 * close a directory (for use in directory scanner only)
 * @return an error-code
 * @param data   a handle for this directory
 */
int backend_closedir(void **data) {
  return NOIR_NOTSUPP; }



/**
 * read an entire directory (for use in directory scanner only)
 * used in file_monitor_reload_maybe()
 * @return a linked list of dir_entry structs, or NULL
 * @param path  the directory to read
 */
dir_entry *backend_getdir(char *path) {
  return NULL; }



/*****************************************************************************/



/**
 * create and start a directory-scanner.
 * @return an error-code
 * @param qh  the queue to the model this scanner is associated with
 */
int backend_scanner_activate(q_head *qh) {
  return NOIR_NOTSUPP; }






/*****************************************************************************/



/**
 * initialize this backend
 * load libraries etc.
 * @return an error-code
 * @param qh  the queue to use to speak with the model
 */
int backend_init(q_head *qh) {
  return NOIR_SUCC; }



/**
 * deinitialize this backend
 * free resources, unload libraries, stop threads
 * @return an error-code
 */
int backend_exit(void) {
  return NOIR_SUCC; }
