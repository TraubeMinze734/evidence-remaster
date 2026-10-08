/** @file  ix_textpath.c
 *  @brief common routines for "text-path" plugins
 *
 * written by Azundris
 */



#include "Evas.h"
#include "icon_list.h"
#include "errors.h"
#include "evas_macros.h"
#include "menu.h"

#include <stdio.h>

#include "theme.h"
#include "debug.h"



/*****************************************************************************/
/* public interface of an evidence-extension */
/*********************************************/



/**
 * something happened in the area that is "owned" by this extension.
 * @return NOIR_SUCC if we handled and "swallowed" the event, NOIR_IGNORED otherwise
 * @param il - the icon-list where the event happened
 * @param i - the instance of this extension
 * @param x - mouse/x
 * @param y - mouse/y
 * @param b - mouse/button
 * @param t - event-type (see theme.h)
 * @param m - modifiers  (see theme.h)
 * @param data - user-data (often the session)
 * @param rp - where the plugin can return a pointer
 */
int ext_evt(icon_list *il,icon *i,int x,int y,int b,theme_area_event_type t,theme_area_event_modifiers m,void *data,void **rp) {
  int         ret=NOIR_IGNORED;
  icon_el    *e;
  const char *tag=NULL;


  *rp=NULL;


  if(t==EV_MOTIONED)
    return NOIR_IGNORED;


  /* we already know the event happened in this "icon", but we still have to
     world out which element of it (here: which part of the path) things
     happened in. */
  for(e=i->els;(ret!=NOIR_SUCC)&&e;) {
    if(e->user1&&
       (x>=(i->x+e->xo))&&(x<(i->x+e->xo+e->w))&&
       (y>=(i->y+e->yo))&&(y<(i->y+e->yo+e->h))) {
      E(3,"event %d (%d) {%d,%d,%d} for %s at %p: %s\n",
        t,m,
        x,y,b,
        evas_object_type_get((Evas_Object *)e->data),e,
        file_info_get_tag((file_info *)e->user1));
      ret=NOIR_SUCC; }
    else
      e=e->next; }


  if(!e)
    return NOIR_NOTFOUND;

  tag=file_info_get_tag((file_info *)e->user1);

  *rp=e->user1;

  switch(t) {
    case EV_SINGLE:
      switch(b) {
        case 1:
          if(m==EV_MOD_NEW_WINDOW)
            noir_sess_create(SESSION_COPY,data,tag);
          else
            noir_sess_focus(data,tag);
          break;
        case 2:
          /* E(0,"Yes, a drop-down menu containing the folders in \"%s\" would be nice.  Patches appreciated.\n",tag); */
          menu_subdirs(data,e->user1,2);
          break;
        case 3:
          menu_file_context(data,noir_sess_get_cwd(data),tag);
          break; }
      break;

    case EV_DRAGGING:
      E(1,"dragging over \"%s\"...\n",tag);
/*    ret=NOIR_TYPEMISMATCH; */     /* don't handle this MIME-type */
      break;

    case EV_DROPPED:
      E(1,"dropped onto \"%s\"...\n",tag);
      ret=NOIR_SUCC;
      break;

    default:
      ret=NOIR_IGNORED;
      break; }

  return ret; }
