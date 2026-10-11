/** @file  clipboard.c
 *  @brief handle the clipboard and the primary selection. */

#include <gtk/gtk.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>                            /* malloc(), free() */

#include "clipboard.h"
#include "model.h"                             /* struct client_funs */
#include "errors.h"
#include "util.h"                              /* esprintf() */
#include "action.h"
#include "file.h"                              /* file_job_create() */
#include "file_lcl.h"                          /* file_to_URL(), file_if_local() */
#include "src/gui/common/session_private.h"
#include "src/gui/common/alert.h"              /* form_alert() */
#include "src/gui/common/typebuffer.h"         /* typebuffer_set(), typebuffer_update() */



/******************************************************************************/

/**
 * set text 'ascii' on clipboard 'cb'.
 * @param cb - the clipboard to modify
 * @param ascii - the text to set
 */
int clipboard_set(GdkAtom cb,const char *ascii) {
  gsize   foo;
  gchar  *utf8;

  E(2,"(\"%s\")\n",ascii);

  if((utf8=g_locale_to_utf8(ascii,-1,&foo,&foo,NULL))) {
    gtk_clipboard_set_text(gtk_clipboard_get(cb),utf8,-1);
    g_free(utf8);
    return NOIR_SUCC; }
  else
    return NOIR_FAIL; }



/**
 * get text from clipboard and insert it into type-buffer.
 * @param cb - the clipboard to read from
 * @param ns - the session (text will be set in ns->typebuffer)
 */
int clipboard_get(GdkAtom cb,noir_sess *ns) {
  gchar *cs=gtk_clipboard_wait_for_text(gtk_clipboard_get(cb));

  if(cs) {
    const char *t=typebuffer_get(ns);
    char       *c=NULL;

    esprintf(&c,"%s%s",t,cs);

    if(c) {
      typebuffer_set(ns,c);
      if(typebuffer_is_open(ns))
        typebuffer_update(ns);
      free(c); }

    g_free(cs);
    return NOIR_SUCC; }

  form_alert(ns,"Could not obtain primary selection -- is it empty?");
  return NOIR_FAIL; }



/* --- file clipboard (text/uri-list, x-special/gnome-copied-files) -------- */

#define CLIPBOARD_URI_TARGETS 3

static GtkTargetEntry clipboard_file_targets[CLIPBOARD_URI_TARGETS]={
  { "x-special/gnome-copied-files", 0, 0 },
  { "text/uri-list",                0, 1 },
  { "text/plain",                   0, 2 } };

/*******************************************************************************
 * percent-decode a URI path component in-place ('%20' -> ' ', etc.)
 */
static void clipboard_uri_decode(char *s) {
  char *w=s;
  if(!s)
    return;
  while(*s) {
    if((*s=='%')&&isxdigit((unsigned char)s[1])&&isxdigit((unsigned char)s[2])) {
      int h,d;
      d=s[1]; h=(d<='9')?d-'0':((d|0x20)-'a'+10);
      d=s[2]; h=h*16+((d<='9')?d-'0':((d|0x20)-'a'+10));
      *w++=(char)h;
      s+=3; }
    else
      *w++=*s++; }
  *w='\0'; }

/* snapshot of the tags currently on the CLIPBOARD selection; kept so the
   clipboard can answer requests even after the selection changed. */
static char  **clipboard_file_tags=NULL;
static int     clipboard_file_count=0;
static int     clipboard_file_cut=0;   /* 0: copy, 1: cut */

static void clipboard_file_clear(void) {
  int n;
  if(clipboard_file_tags) {
    for(n=0;n<clipboard_file_count;n++)
      if(clipboard_file_tags[n])
        free(clipboard_file_tags[n]);
    free(clipboard_file_tags);
    clipboard_file_tags=NULL; }
  clipboard_file_count=0;
  clipboard_file_cut=0; }

/**
 * build the payload for the requested target from our tag snapshot.
 */
static void clipboard_file_get(GtkClipboard *cb,GtkSelectionData *sd,guint info,gpointer user_data) {
  int    n;
  char  *data,*w;
  size_t len;

  if(!clipboard_file_tags||!clipboard_file_count)
    return;

  switch(info) {
    case 0: {  /* x-special/gnome-copied-files: "copy\nuri\r\nuri..." */
      len=5;
      for(n=0;n<clipboard_file_count;n++)
        len+=(clipboard_file_tags[n]?strlen(clipboard_file_tags[n]):0)+9;
      if(!(data=malloc(len+1)))
        return;
      w=data;
      w+=sprintf(w,"%s\n",clipboard_file_cut?"cut":"copy");
      for(n=0;n<clipboard_file_count;n++) {
        if(clipboard_file_tags[n]) {
          char *u=file_to_URL(clipboard_file_tags[n]);
          if(u) {
            w+=sprintf(w,"%s\r\n",u);
            free(u); } } }
      gtk_selection_data_set(sd,gdk_atom_intern("x-special/gnome-copied-files",FALSE),8,(guchar *)data,w-data);
      free(data);
      return; }

    case 1: {  /* text/uri-list: "uri\r\nuri..." */
      len=0;
      for(n=0;n<clipboard_file_count;n++)
        len+=(clipboard_file_tags[n]?strlen(clipboard_file_tags[n]):0)+9;
      if(!(data=malloc(len+1)))
        return;
      w=data;
      for(n=0;n<clipboard_file_count;n++) {
        if(clipboard_file_tags[n]) {
          char *u=file_to_URL(clipboard_file_tags[n]);
          if(u) {
            w+=sprintf(w,"%s\r\n",u);
            free(u); } } }
      gtk_selection_data_set(sd,gdk_atom_intern("text/uri-list",FALSE),8,(guchar *)data,w-data);
      free(data);
      return; }

    default: { /* text/plain: newline-separated paths (last resort) */
      len=0;
      for(n=0;n<clipboard_file_count;n++)
        len+=(clipboard_file_tags[n]?strlen(clipboard_file_tags[n]):0)+2;
      if(!(data=malloc(len+1)))
        return;
      w=data;
      for(n=0;n<clipboard_file_count;n++) {
        if(clipboard_file_tags[n])
          w+=sprintf(w,"%s\n",clipboard_file_tags[n]); }
      gtk_selection_data_set(sd,gdk_atom_intern("text/plain",FALSE),8,(guchar *)data,w-data);
      free(data);
      return; } } }

static void clipboard_file_clear_cb(GtkClipboard *cb,gpointer user_data) {
  clipboard_file_clear(); }

/**
 * put the files in 'ac' on the CLIPBOARD selection in a form other
 * file-managers understand (gnome-copied-files, uri-list, plain text).
 * @param ac     - the files to put on the clipboard
 * @param cut    - 0 for copy, 1 for cut
 * @return an error-code
 */
int clipboard_files_set(action_context *ac,int cut) {
  action_file *af;
  char       **tags=NULL;
  int          n=0,count=0;

  clipboard_file_clear();

  if(!ac||!(af=action_context_get_selection(ac)))
    return NOIR_FAIL;

  while(af) {
    count++;
    af=action_file_get_next(af); }

  if(!count)
    return NOIR_FAIL;

  if(!(tags=malloc(sizeof(char *)*count)))
    return NOIR_OOM;

  af=action_context_get_selection(ac);
  while(af&&n<count) {
    const char *tag=action_file_get_tag(af);
    tags[n]=tag?strdup(tag):NULL;
    if(tag&&!tags[n]) {
      while(n>0)
        free(tags[--n]);
      free(tags);
      return NOIR_OOM; }
    n++;
    af=action_file_get_next(af); }

  /* commit the snapshot only AFTER gtk_clipboard_set_with_data():
     taking ownership calls the previous owner's clear-handler, which
     is our own clipboard_file_clear_cb() -- committing earlier, it
     would wipe the fresh snapshot right away, so the second copy in a
     row always left an empty clipboard. */
  if(!gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                                  clipboard_file_targets,CLIPBOARD_URI_TARGETS,
                                  clipboard_file_get,clipboard_file_clear_cb,NULL)) {
    for(n=0;n<count;n++)
      if(tags[n])
        free(tags[n]);
    free(tags);
    return NOIR_FAIL; }

  clipboard_file_tags=tags;
  clipboard_file_count=count;
  clipboard_file_cut=cut;

  gtk_clipboard_set_can_store(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),NULL,0);

  return NOIR_SUCC; }



/**
 * fetch a list of local paths from the clipboard.  understands
 * x-special/gnome-copied-files and text/uri-list.  optionally
 * reports whether the clipboard was cut (move) or copied.
 * @param cb    - the clipboard to read from
 * @param cut   - where to store whether this is a move, or NULL
 * @return a NULL-terminated array of malloc()ed paths, or NULL; caller frees
 */
char **clipboard_files_get(GdkAtom cb,int *cut) {
  GtkClipboard     *clip=gtk_clipboard_get(cb);
  GtkSelectionData *sd;

  if(cut)
    *cut=0;

  sd=gtk_clipboard_wait_for_contents(clip,gdk_atom_intern("x-special/gnome-copied-files",FALSE));
  if(sd) {
    const guchar *d=gtk_selection_data_get_data(sd);
    gint          l=gtk_selection_data_get_length(sd);
    gchar        *buf,**lines,**ret=NULL;
    int           mv=0;

    if(d&&l>0&&(buf=g_strndup((const gchar *)d,l))) {
      lines=g_strsplit(buf,"\n",-1);
      if(lines&&lines[0]) {
        if(!strncasecmp(lines[0],"cut",3))
          mv=1;
        { gchar **line; int n=0; char **w;
          for(line=&lines[1];*line;line++)
            if(**line)
              n++;
          if(n&&(ret=malloc(sizeof(char *)*(n+1)))) {
            for(line=&lines[1],w=ret;*line;line++) {
              if(**line) {
                gsize ln=strlen(*line);
                const char *p;
                if(ln&&(*line)[ln-1]=='\r')
                  (*line)[ln-1]='\0';
                if((p=file_if_local(*line))) {
                  if((*w=strdup(p)))
                    clipboard_uri_decode(*w);
                  w++; } } }
            *w=NULL; } } }
      g_strfreev(lines);
      g_free(buf); }
    gtk_selection_data_free(sd);
    if(ret) {
      if(cut)
        *cut=mv;
      return ret; } }

  sd=gtk_clipboard_wait_for_contents(clip,gdk_atom_intern("text/uri-list",FALSE));
  if(sd) {
    gchar **uris=gtk_selection_data_get_uris(sd);

    gtk_selection_data_free(sd);
    if(uris) {
      char **ret=NULL,**u,**w;
      int    n=0;

      for(u=uris;*u;u++)
        if(file_if_local(*u))
          n++;
      if(n&&(ret=malloc(sizeof(char *)*(n+1)))) {
        for(u=uris,w=ret;*u;u++) {
          const char *p;
          if((p=file_if_local(*u))) {
            if((*w=strdup(p)))
              clipboard_uri_decode(*w);
            w++; } }
        *w=NULL; }
      g_strfreev(uris);
      if(ret)
        return ret; } }

  return NULL; }



/**
 * paste files from the clipboard into the session's current directory,
 * using the same job machinery as drag&drop (file_job_create()).
 * @param ns - the session to paste into
 * @param cb - the clipboard to read from
 * @return an error-code
 */
int clipboard_paste(noir_sess *ns,GdkAtom cb) {
  char          **tags=NULL,**t;
  int             cut=0,ret;
  action_context *ac;
  action_file    *af;
  const char     *cwd=noir_sess_get_cwd(ns);

  if(!cwd)
    return NOIR_FAIL;

  if(!(tags=clipboard_files_get(cb,&cut))) {
    form_alert(ns,"Clipboard contains no files to paste...");
    return NOIR_FAIL; }

  if(!(ac=action_context_new(noir_sess_get_serial(ns),cwd))) {
    ret=NOIR_OOM;
    goto done; }

  for(t=tags;*t;t++) {
    if(!(af=action_file_new(ac,*t))) {
      ret=NOIR_OOM;
      goto done; } }

  /* the job takes ownership of the action_context (file_job_dst()
     destroys it when the job is done) -- we must NOT dst() it here.
     like file_drop_do() in dnd.c, the target directory is attached
     as the context's 'single', which becomes the job's dst_path. */
  if((af=action_file_new(NULL,cwd)))
    action_context_set_single(ac,af);
  else {
    ret=NOIR_OOM;
    goto done; }

  ret=file_job_create(ns,cut?NOIR_JOB_MV:NOIR_JOB_CP,ac,NULL);
  ac=NULL;   /* owned by the job now */

 done:
  if(ac)
    action_context_dst(ac);
  if(tags) {
    for(t=tags;*t;t++)
      free(*t);
    free(tags); }
  return ret; }



/**
 * copy selection to all views in a session.
 * @param ns - the session
 * @param skip - the calling client (don't call setter on that, it already knows)
 */
int selection_update(noir_sess *ns,client_funs *skip) {
  action_context *ac=noir_sess_get_selection(ns);
  char           *s=NULL;

  if(action_context_get_selection(ac))
    s=action_context_files_to_quoted_string(ac);

  action_context_set_cwd(ac,noir_sess_get_cwd(ns));

#if 0
  client_funs *cf=ns->clients;
#endif

  if(debug>1) { if(s) { E(2,"%s\n",s); } else { E(3,"<empty>\n"); } }

#if 0
  /* deferred, will update when switching pages */
  while(cf) {
    if((cf!=skip)&&(cf->set_selection))
      cf->set_selection((void *)ns);
    cf=cf->next; }
#endif

  if(s) {
    clipboard_set(GDK_SELECTION_PRIMARY,s);
    free(s); }
  else
    gtk_clipboard_clear(gtk_clipboard_get(GDK_SELECTION_PRIMARY));

  /* the CLIPBOARD selection (file list) is NONE of selection_update's
     business: a clipboard must outlive selection changes.  it is only
     set by the explicit copy (Ctrl+C) and cut (Ctrl+X) handlers, and
     only ever replaced or cleared by a new copy/cut -- otherwise any
     focus change or deselect would wipe what the user just copied */

  return NOIR_SUCC; }

/****************************************************************************/
