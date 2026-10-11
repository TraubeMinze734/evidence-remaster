/** @file  playlist.c
 *  @brief context-menu action "make playlist"
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>      /* creat() */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gui/common/session.h"
#include "gui/common/alert.h"      /* form_*() */
#include "meta.h"       /* struct _meta */
#include "util.h"       /* estring_*() */
#include "action.h"     /* action_file_dst() */
#include "file.h"       /* file_exists() */
#include "file_lcl.h"   /* file_dirname() */
#include "mime.h"       /* MIME_DELIMITER */



/******************************************************************************/



/**
 * context-menu action: make playlist
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  noir_sess   *ns=noir_sess_get_by_serial(serial);
  int          ret=NOIR_FAIL;
  action_file *sf;
  estring     *msg=estring_new(2048);
  char        *data,*path;
  const char  *mime;

  if(!filename)
    return NOIR_FAIL;

  if(!(path=file_dirname(filename)))
    return NOIR_OOM;

  /* no selection (right-clicked a single file): act on that file */
  if(!action_context_get_selection(ac)&&
     (sf=action_context_get_single(ac)))
    action_file_new(ac,action_file_get_tag(sf));

  if(!msg) {
    free(path);
    return NOIR_OOM; }



  /* create playlist */
  while((sf=action_context_get_selection(ac))) { /* get next item in selection */
    action_context_remove_file(ac,sf);   /* remove item from selection */

    mime=NULL;                /* get its MIME-type */
    if(action_file_get_meta(sf)) {
      noir_meta *m=meta_in_group(action_file_get_meta(sf),"MIME",NULL);
      if(m)
        mime=(char *)meta_get_val(m); }
    if(!mime)
      mime=file_get_mime(action_file_get_tag(sf));

    if(mime) {
      const char *p=strchr(mime,MIME_DELIMITER);
      size_t      cl=strlen(mime);

      if(p)
        cl=(p-mime+1);

      if(!strcasecmp(mime,"audio/x-mpegurl")) /* if it's a playlist, ignore it */
        E(1,"ignoring play-list \"%s\" (%s,%d)\n",action_file_get_tag(sf),mime,cl);
      else if(strncasecmp(mime,"audio/",cl))  /* if it's not audio/..., ignore it */
        E(3,"ignoring           \"%s\" (%s,%d)\n",action_file_get_tag(sf),mime,cl);
      else {                                  /* otherwise, add it */
        E(2,"adding             \"%s\" (%s)\n",action_file_get_tag(sf),mime);

        if((p=file_name(action_file_get_tag(sf))))  /* make path relative */
          estring_appendf(msg,"%s\n",p); }}       /* add relative path */

    action_file_dst(sf); }



  if(estring_length(msg)&&(data=ESTRING_GET_CSTRING(msg))) {  /* do we have a playlist? */
    char       *fn=NULL;
    const char *p=file_name(path);
    if(path&&p) {
      mode_t     mode=S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;
      file_info *pa=file_info_fetch(NULL,path);
      posixmd   *intrinsic=file_info_get_intrinsic(pa);

      if(intrinsic)   /* inherit permissions from dir if possible */
        mode=posixmd_get_mode(intrinsic)&mode;

      esprintf(&fn,"%s/%s.m3u",path,p);

      if(fn) {
        E(1,"playlist \"%s\"\n",fn);

        if(file_exists(fn)&&(form_yes_no(ns,"Playlist already exists, overwrite?")!=NOIR_SUCC))
          ;
        else if(file_save(fn,mode,estring_length(msg),data)!=NOIR_SUCC)
          form_alert(ns,"Couldn't save %s...",fn);

        free(fn); }}}
  else
    ret=NOIR_IGNORED;

  estring_free(msg,TRUE);
  free(path);

  return ret; }



/******************************************************************************/
