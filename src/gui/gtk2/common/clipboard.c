/** @file  clipboard.c
 *  @brief handle the clipboard and the primary selection.
 */

#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>                            /* malloc(), free() */

#include "clipboard.h"
#include "model.h"                             /* struct client_funs */
#include "errors.h"
#include "util.h"                              /* esprintf() */
#include "src/gui/common/session_private.h"
#include "src/gui/common/alert.h"              /* form_alert() */
#include "src/gui/common/typebuffer.h"         /* typebuffer_set(), typebuffer_update() */



/****************************************************************************/



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

  if(debug>1) { if(s) { E(2,"%s\n",s); } else { E(3,"<empty>\n"); }}

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

  return NOIR_SUCC; }



/****************************************************************************/
