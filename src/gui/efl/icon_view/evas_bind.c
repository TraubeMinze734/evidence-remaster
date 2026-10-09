/** @file  evas_bind.c
 *  @brief the parts of an icon-list specific to the evas canvas.
 *  generic parts are in icon_list.  gevas-specific parts are in gevas_bind.
 */

#include "config.h"

#ifdef HAVE_EVAS2

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>           /* chdir() */

#include <Evas.h>

#include <stdio.h>

#include "debug.h"

#include "gui/common/menu.h"             /* menu_action_by_mime() */
#include "gui/common/icon_list.h"
#include "gui/common/typebuffer.h"       /* re_*() */
#include "gui/common/icon_path.h"

#include "gui/gtk2/icon_view/gevas_bind.h"  /* ICON_LIST_GET_FROM_WIDGET() */

#include "thumbnailer/thumbnail.h"          /* thumbnail_load() */

#include "gui/efl/common/theme.h"
#include "gui/efl/common/evas_macros.h"

#include "evas_bind.h"        /* FUZZ */

#include "action.h"
#include "mime.h"             /* MIME_INTL_REN */
#include "errors.h"
#include "file.h"             /* file_activate() */
#include "setup.h"            /* prefix */



int selection_update(noir_sess *ns,client_funs *skip);






/*****************************************************************************/
/* gevas_bind helpers */
/**********************/

/* the following functions are called mostly (and in some cases exclusively)
   by functions in gevas_bind.  they remain here all the same because they
   are independent from GTK, while the functions in gevas_bind are not. */


/**
 * get file-tag from an icon.
 * called from gevas_bind::gevas_keyed(), gevas_bind::gevas_clicked() and
 * evas_open_folder().
 * @param data - the session
 * @param i - the icon to get the file from
 */
char *evas_get_file_from_icon(void *data,icon *i) {
  char      *file=NULL;

#if 1
  if(i)
    file=strdup(ICON_GET_FILENAME(i));
#else
  if(i) {
    if(!strcmp(ICON_GET_LABEL(i),DIR_PARENT)) {
      noir_sess *ns=(noir_sess *)data;
      file_info *pa=NULL;
      char      *p;
      size_t     l=strlen(noir_sess_get_cwd(ns))-1;

      if(i->user2&&(pa=((file_info *)i->user2))&&pa->tag)
        file=strdup(pa->tag);
      else {   /* fallback */
        E(0,"needed fallback. interesting.\n");

        file=strdup(noir_sess_get_cwd(ns));

        while(file[l]==DIR_DELIMITER)
          file[l--]='\0';  /* trailing slash */

        if((p=strrchr(file,DIR_DELIMITER))) {  /* slash left (should always be true!) */
          p[(p>file)?0:1]='\0'; }}}
    else {
      file=strdup(ICON_GET_FILENAME(i)); }}
#endif
  return file; }



/**
 * check whether mouse-pointer is over a given icon's label.
 * called from gevas_bind::gevas_clicked().
 * @return  TRUE or FALSE.
 * @param i - the icon
 * @param x - mouse-x, window-relative
 * @param y - mouse-y, window-relative
 */
int pointer_on_iconlabel_p(icon *i,int x,int y) {
  icon_el *el;

  if(!i)
    return FALSE;

  el=i->els;

  while(el) {
    if((x>=(i->x+el->xo))&&
       (y>=(i->y+el->yo))&&
       (x<(i->x+el->xo+el->w))&&
       (y<(i->y+el->yo+el->h))&&
       ((el->tag>=ET_LABEL_TEXT_HI)&&
        (el->tag<=ET_LABEL_OTHER)))
      return TRUE;
    el=el->next; }
  return FALSE; }



/**
 * check whether mouse-pointer is over a given icon.
 * this means "within the icon's bounding box, within an element's bounding
 * box, on a non-transparent pixel".
 * called from icon_list::icon_list_get_icon_at().
 * @return  TRUE or FALSE.
 * @param il - the icon-list
 * @param i - the icon
 * @param x - mouse-x, window-relative
 * @param y - mouse-y, window-relative
 */
inline int pointer_on_icon_p(icon_list *il,icon *i,int x,int y) {
  Evas_Object   *o;
  unsigned char *d;
  Evas_Coord     ox,oy,ow,oh,rw,rh;
  int            vx,vy;

  if((x>=i->x)&&(x<(i->x+i->w))&&
     (y>=i->y)&&(y<(i->y+i->h))) {  /* if in bounding box... */
    icon_el *e=i->els;
    while(e) {
      if((x>=i->x+e->xo)&&(x<(i->x+e->xo+e->w))&&
         (y>=i->y+e->yo)&&(y<(i->y+e->yo+e->h))) {  /* if in element... */
        if(!(o=(Evas_Object *)e->data)) {
          E(0,"icon for \"%s\" very broken!??  this is a nasty bug, please report and a description of the file that caused this. add a backtrace if you know how. thankyou! <evidence@azundris.com>\n",i->file);
          return FALSE; }
        else if(!strcmp(evas_object_type_get(o),"image")) {  /* if it's an image */
          if((d=evas_object_image_data_get(o,FALSE))) { /* if on non-transparent pixel... */
            evas_object_image_size_get(o,&rw,&rh);      /* real in-memory size */
            evas_object_geometry_get(o,&ox,&oy,&ow,&oh);  /* scaled on-screen size */
            vx=x-ox-il->sx;
            vy=y-oy-il->sy;
            if((vx<ow)&&(vy<oh)) {
              if((rw!=ow)||(rh!=oh)) {   /* adjust for scaled images */
                float sw=((float)ow)/rw,sh=((float)oh)/rh;
             /* E(2,"image (%d,%d) => (%d, %d) scaled by %f, %f\n",rw,rh,ow,oh,sw,sh); */
                vx=((float)vx)/sw;
                vy=((float)vy)/sh; }
              d+=(((vy*((int)rw))+vx)*4);
           /* E(4,"%03d,%03d | %03d,%03d | %03d,%03d | %02x,%02x,%02x, %02x, %ld\n",
                x,y, ox,oy, vx,vy,
                d[0],d[1],d[2],d[3],time(NULL)); */
              if(d[3]>31)               /* if above arbitrary transparency threshold */
                return TRUE; }}       /* return success */
          else
            E(0,"could not get image-data!?\n"); }
        else if(e->tag!=ET_MISC)      /* have no pixel-data for non-images; */
          return TRUE; }              /* within bounding-box => success */
      e=e->next; }}
  return FALSE; }



/**
 * rename the file represented by a given icon.
 * normally called when the label is double-clicked.
 * called from gevas_bind::gevas_clicked().
 * @param ns - the session
 * @param i - the icon
 */
int evas_rename_icon(noir_sess *ns,icon *i) {
  char *file=evas_get_file_from_icon(ns,i);
  int   ret=NOIR_FAIL;

  if(file) {
    if((ret=menu_action_by_mime(noir_sess_get_serial(ns),MIME_INTL_REN,file,noir_sess_get_selection(ns)))!=NOIR_SUCC)
      E(0,"could not open rename dialog (type \""
          MIME_INTL_REN "\") in session {%p|%ld}: %d\n",
          ns,noir_sess_get_serial(ns),ret);
    free(file); }
  else if(!i)
    E(0,"No icon selected for rename.\n");
  else
    E(0,"couldn't get file-name from icon!?\n");
  return ret; }



/**
 * focus new icon by keyboard.
 * called from evas_find_icon_by_string() and by gevas_bind::gevas_keyed().
 * @param il - the icon-list
 * @param new - the icon that should now be selected, or -1 for none
 */
void evas_keyed_icon(icon_list *il,int new) {
  icon *i;
  Evas *evas=get_evas_from_iconlist(il);

  ICON_LIST_CLEAR_FIND(il);

  if(il->ki>=0) {
    for(i=il->icons;i;i=i->next)  /* =icon_list_get_icon_by_index(il,il->ki))) */
      if(i->state&IE_KBD)
        evas_icon_set_state(il,i,i->state&(~IE_KBD)); }
  if(new>=0) {
    if((i=icon_list_get_icon_by_index(il,new))) {
      evas_icon_set_state(il,i,i->state|IE_KBD);
      if(((i->y+i->h)>(il->sy+il->vis_h))||((i->y-i->h)<il->sy))
        icon_list_scroll_abs(il,0,i->y); }
    else
      new=-1; }
  evas_render(evas);
  il->ki=new; }



/**
 * got key in evas. check whether find-string has timed out.
 * if so, start a new one, else add to existing one.
 * called from gevas_bind::gevas_keyed().
 * @param il - the icon-list
 * @param l - where length of the find-string (il->find) lives
 */
int evas_find_icon_by_string_chk(icon_list *il,size_t *l) {
  int    ret=NOIR_IGNORED;
  time_t now=time(NULL);

  if(*l&&((now-ICONLIST_FIND_ICON_BY_STRING_RESETS_AFTER)>=il->find_last)) {
    E(3,"resetting icon search-string (was \"%s\") after %lds.\n",il->find,now-ICONLIST_FIND_ICON_BY_STRING_RESETS_AFTER-il->find_last);
    il->find[0]='\0';
    *l=0;
    ret=NOIR_SUCC; }
  il->find_last=(long)now;
  return ret; }



/**
 * find first icon whose name matches il->find.
 * called from gevas_bind::gevas_keyed().
 * @param il - the icon-list
 */
int evas_find_icon_by_string(icon_list *il) {
  icon   *i=il->icons,*n=NULL;
  char   *f=il->find;
  size_t  l=strlen(f);
  Evas   *evas=get_evas_from_iconlist(il);
  int     ret;

  if(!l)
    return FALSE;

#ifdef HAVE_PCRE
  if((ret=re_add(il->sess,f,REM_GLOB))!=NOIR_SUCC)
    return ret;
#endif

  while(i) {
#ifdef HAVE_PCRE
    E(3,"|%s| |%s|\n",f,i->label);
    if(re_match(il->sess,i->label)==NOIR_SUCC)
#else
    if(!strncasecmp(i->label,f,l))
#endif
    {
      if(!n)
        n=i;
      evas_icon_set_state(il,i,i->state|IE_KBD); }
    else
      evas_icon_set_state(il,i,i->state&(~IE_KBD));
    i=i->next; }

  if(n) {
    il->ki=n->index;
    if(((n->y+n->h)>(il->sy+il->vis_h))||((n->y-n->h)<il->sy))
      icon_list_scroll_abs(il,0,n->y); }
  else
    il->ki=-1;

  evas_render(evas);

  return n?TRUE:FALSE; }



/**
 * find first selected icon.
 * called from gevas_bind::gevas_keyed().
 * used as a fallback when an icon should be selected via keyboard, but isn't.
 * @param il - the icon-list
 */
icon *evas_find_first_selected(icon_list *il) {
  icon *i=il->icons;
  while(i) {
    if(i->state&IE_SEL)
      return i;
    i=i->next; }
  return NULL; }



/**
 * check whether mouse is over scrollbar.
 * @return SB_BAR for "over bar", SB_PGUP for "above", SB_PGDN for "below bar".
 * @param il - the icon-list
 * @param i - the scroll-bar
 * @param x - mouse-x (icon-list relative)
 * @param y - mouse-y (icon-list relative)
 */
int sb_chk(icon_list *il,icon *i,int x,int y) {
  if(i&&(x>=i->x)) {
    icon_el *e=i->els;
    int      ix=i->x,iy=i->y;

    while(e) {
      if((e->state&IE_BAR)&&
         (x>=(ix+e->xo))&&
         (y>=(iy+e->yo))&&
         ((!(e->state&IE_ABS)&&
           (x<(ix+e->xo+e->w))&&
           (y<(iy+e->yo+e->h)))||
         (((e->state&IE_ABS)&&
          (x<(ix+e->w))&&
          (y<(iy+e->h)))))) {
        return SB_BAR; }
      e=e->next; }
    if(y<(iy+(i->h/2)))
      return SB_PGDN;
    else
      return SB_PGUP; }
  return SB_NONE; }






/******************************************************************************/
/* icon_list helpers */
/*********************/

/* the following functions are called mostly (and in some cases exclusively)
   by functions in icon_list.  they remain here all the same because they
   are dependent on evas, while the functions in icon_list are not. */



/**
 * update scroll-bar and icon-list.
 * called from icon-list and gevas_bind::gevas_icon_list_resized().
 * @param il - the icon-list
 * @param scrolled - was the region damaged only (FALSE), or scrolled as well (TRUE)?
 */
void sb_update(icon_list *il,int scrolled) {
  Evas       *evas=get_evas_from_iconlist(il);
  noir_theme *t=((noir_theme *)il->theme);

  if(il&&evas) {
    t->sb_set(evas,(icon *)il->sb_v,il->width,il->height,il->vis_h,il->sy);
    evas_damage_rectangle_add(evas,0,0,il->vis_w,il->vis_h);
    evas_queue_redraw(il); }}



/**
 * destruct data (Evas-Object) of an icon-element.
 * @param data - the Evas-Object
 */
void icon_el_data_dst(void *data) {
  evas_object_del((Evas_Object *)data); }



/**
 * move an icon.
 * @param i - the icon to be moved
 * @param x - new x-position
 * @param y - new y-position
 */
void icon_move(icon *i,int x,int y) {
  if(i&&i->els) {
    icon_el *e=i->els;
    while(e) {
      evas_object_move(e->data,x+e->xo,y+e->yo);
      e=e->next; }}}



/**
 * set state (selected, normal, ...) of an icon.  if the icon-factory
 * provides a function icon_set_state(), that function will be called.
 * if it returns TRUE (or there is no such function), all elements
 * suitable for the desired state will be shown; all others will be
 * hidden.
 * @param il - the icon-list
 * @param i - the icon
 * @param s - the desired state
 */
void evas_icon_set_state(icon_list *il,icon *i,icon_state s) {
  int redraw=TRUE;

  if(i) {
    icon_el    *e;
    icon_state  t=s;

    if(t&IE_SEL)
      t&=~IE_DFL;

    /* theme may define its own function for icon-change.
       if it does, call it.  it returns whether it wants
       a redraw from us (TRUE). */
    if(il&&il->theme&&((noir_theme *)il->theme)->icon_set_state)
      redraw=((noir_theme *)il->theme)->icon_set_state(il,i,s);

    if(redraw) {
      e=i->els;
      while(e) {
        if(e->state&t)
          evas_object_show(e->data);
        else
          evas_object_hide(e->data);
        e=e->next; }}

    i->state=s; }}






/*****************************************************************************/
/* icon-view helpers */
/*********************/



/**
 * update the pictogram/thumbnail of an icon.
 * called from icon_view::icon_view_evt().
 * @return an error-code. anything but NOIR_FAIL should be regarded as a
 * suggestion to redraw the icon-view.
 * @param il - the icon-list to update
 * @param i - the icon to update
 * @param fi - the file to update
 */
int evas_icon_chg_event(icon_list *il,icon *i,file_info *fi) {
  int ret=NOIR_FAIL;

  if(i) {
    icon_el        *els=i->els,*old=NULL,*el;
    char           *ifile;
    const char     *mime=file_info_get_mime(fi),*thumb;
    noir_theme     *theme=(noir_theme *)il->theme;
    posixmd        *intrinsic=file_info_get_intrinsic(fi);

    if(!mime&&intrinsic)
      mime=S_ISDIR(file_info_get_mode(fi))?MIME_FOLDER:NULL;

    if((ifile=icon_path_get_always(file_info_get_tag(fi),mime))) {
      int   w=i->w,h=i->h;

      if(i->name)
        free(i->name);
      i->name=strdup(ifile);
      i->user1=intrinsic;

      old=i->els;
      i->els=NULL;

      /* file has changed in some way, update thumb in image-cache... */
      if((thumb=file_info_get_thumbfile(fi))) {
        const char *file,*key;
        for(el=old;el;el=el->next) {
          if(!strcmp(evas_object_type_get(el->data),"image")) {
            evas_object_image_file_get(el->data,&file,&key);
            if(file&&!strcmp(file,thumb)) {
              E(3,"invalidating image-cache for \"%s\"...\n",file);
              evas_object_image_reload(el->data); }}}
        if(!(file_info_get_infobits(fi)&FF_THUMB))
          thumb=NULL; }

      if((els=(void *)theme->icon_els_create(il,i,thumb))) {
        icon_el_dst_all(old);
        i->els=els;
        evas_icon_set_state(il,i,i->state);
        icon_move(i,i->x-il->sx,i->y-il->sy);
        if((w!=i->w)||(h!=i->h))
          ret=NOIR_SUCC;
        else
          ret=NOIR_PARTIAL; }
      else
        i->els=old; }}
  return ret; }






/**
 ******************************************************************************
 * extensions  (toolbars et al.)
 *
 * (icon *i)il->theme->areas anchors a list of all extensions for icon-list il.
 * each of these "icon" entries describes a single extension.
 * user1 of each of these "icon" entries contains a pointer to a theme_area
 * struct that contains function pointers, a shared object-handle, and space
 * for extra user-data.
 * each of these "icon" entries is likely to have one or more sub-entries of
 * the "icon_el" type that describe the rectangle-list of the extension.
 * the user1 of each of these "icon_el" entries is not used by evidence and
 * may be used by the extensions to store pointers particular to the given
 * rectangle.  the nature of the data pointed to may vary from extension to
 * extension.
 *
 * icon
 *  +- user1{theme_area *}                   theme_area object
 *  |   +- user1,user2,user3,user4           extension-specific pointers
 *  |   +- lib                               library-handle
 *  |   +- ext_upd, ext_dst, ext_evt         functions in extension
 *  +- user2{void *}                         extension-specific pointer
 *  +- els{icon_el *}                        rectangle list of extension
 *  |   +- user1{file_info *}                extension-specific pointer
 *  |   +- xo,yo,w,h{int}                    position and area of rectangle
 *  |   +- ...
 *  |   +- next{icon_el *}                   next rectangle
 *  +- next{icon *}                          next extension
 *
 */



/**
 * propagate mouse-clicks to concerned extension plug-ins
 * @return what the extension said (or NOIR_NOTFOUND
 * @param il - the icon-list
 * @param x - window-relative x-position of click
 * @param y - window-relative y-position of click
 * @param b - mouse-buttons
 * @param t - the event-type (press, release, double-click, ...)
 * @param m - modifiers
 * @param data - the session
 * @param ext - where to return the extension that consumed the event, or NULL
 * @param rp - where the extension can store a return value (pointer)
 */
int evas_ext_evt(icon_list *il,int x,int y,int b,theme_area_event_type t,theme_area_event_modifiers m,void *data,icon **ext,void **rp) {
  int   ret;
  icon *i=((noir_theme *)il->theme)->areas;
  void *r=NULL;

  while(i) {
    if((x>=i->x)&&(x<(i->x+i->w))&&
       (y>=i->y)&&(y<(i->y+i->h))) {
      E(3,"{%d,%d} b %d, t %d, m %d  on %s \"%s\"\n",x,y,b,t,m,i->label,i->name);
      if((ret=((theme_area *)i->user1)->ext_evt(il,i,x,y,b,t,m,data,&r))!=NOIR_NOTFOUND) {
        if(ext)
          *ext=i;
        if(rp)
          *rp=r;
        return ret; }}
    i=i->next; }

  if(ext)
    *ext=NULL;
  if(rp)
    *rp=NULL;
  return NOIR_NOTFOUND; }



/**
 * find extension plugin by name.
 * @param areas - the linked list of extensions
 * @param name - the name of the extension we're looking for
 */
icon *evas_ext_find(icon **areas,const char *name) {
  icon *a=*areas;
  while(a) {
    if(!strcmp(name,a->name))
      return a;
    a=a->next; }
  return NULL; }



/**
 * redraw all extensions in icon-list
 * @return an error-code < 0, or a count of queried extensions
 * @param il    icon-list
 * @param fi    the file-info of the current directory
 */
int evas_ext_upd(icon_list *il,file_info *fi) {
  icon *i=((noir_theme *)il->theme)->areas;
  int   c=0;
  while(i) {
    c++;
    ((theme_area *)i->user1)->ext_upd(il,i,(void *)fi);
    i=i->next; }
  return c; }






/*****************************************************************************/
/* selections */
/**************/



/**
 * return current selection of icon-list
 * @param data - the session
 * @param il - the icon-list
 */
int icon_list_get_selection(void *data,icon_list *il) {
  noir_sess      *ns=(noir_sess *)data;
  icon           *i=il->icons;
  noir_iconview  *ivs=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
  icon_list      *iv=ICON_LIST_GET_FROM_WIDGET(ivs->icon_view);
  action_context *ac=NULL;

  if(il!=iv) {
    E(2,"request from shelf ignored...\n");
    return NOIR_IGNORED; }

  if(!(ac=action_context_new(noir_sess_get_serial(ns),noir_sess_get_cwd(ns))))
    return NOIR_OOM;

  while(i) {
    if(i->state&IE_SEL)
      action_file_new(ac,i->file);
    i=i->next; }

  /* update selection in session */
  noir_sess_set_selection(ns,ac);

  /* propagate new selection to other views in session */
  selection_update(ns,noir_sess_client_get(ns,CLIENT_ICONVIEW));

  return NOIR_SUCC; }



/**
 * set selection in icon-view.
 * @param il - icon-list
 */
int icon_list_set_selection(icon_list *il) {
  if(il) {
    action_file *af;
    icon        *i=il->icons;
    Evas        *ev=get_evas_from_iconlist(il);

    if(!i) {
      E(2,"cannot set selection on empty icon-view...\n");
      return NOIR_IGNORED; }

    while(i) {
      if(i->state&IE_SEL)
        evas_icon_set_state(il,i,i->state&~IE_SEL);
      i=i->next; }

    if((af=action_context_get_selection(noir_sess_get_selection(il->sess)))) {
      while(af) {
        if((i=icon_list_get_icon_by_file(il,action_file_get_tag(af)))) {
          E(2,"selecting icon for \"%s\".\n",action_file_get_tag(af));
          evas_icon_set_state(il,i,i->state|IE_SEL); }
        else
          E(1,"no icon for \"%s\" found in current view!? (%p,%p)\n",action_file_get_tag(af),il,il?il->icons:NULL);
        af=action_file_get_next(af); }
      return NOIR_SUCC; }
    else {
      evas_damage_rectangle_add(ev,0,0,il->vis_w,il->vis_h);
      evas_queue_redraw(il);
      return NOIR_SUCC; }}
  else
    E(0,"no icon-list!?\n");

  return NOIR_FAIL; }






/*****************************************************************************/
/* icon-activation animations */
/******************************/



/**
 * open-folder animation.
 * will run theme->ani_open_folder() for the given icon.
 * @param ns - session
 * @param il - icon-list
 * @param i - icon
 * @param evas - evas, or NULL for auto
 */
void evas_open_folder(noir_sess *ns,icon_list *il,icon *i,Evas *evas) {
  if(il&&i) {
    char       *file=evas_get_file_from_icon(ns,i);
    noir_theme *t=(noir_theme *)il->theme;
    int         ret=NOIR_SUCC;

    if(!file)
      return;

    if(!evas)
      evas=get_evas_from_iconlist(il);

    if((!ns||!noir_sess_get_desktop(ns))&&t&&t->ani_open_folder) {
      if((ret=noir_sess_focus_verify(ns,file,NULL))==NOIR_SUCC) {
        t->ani_open_folder(il,i);
        noir_sess_focus(ns,file); }}
    else
      ret=noir_sess_focus(ns,file);

    if(ret!=NOIR_SUCC)
      E(0,"cannot change directory to \"%s\": %d\n",file,ret);

    free(file); }}



/**
 * open-file animation.
 * will determine whether to run t->ani_open_file() or t->ani_open_app().
 * will return result of file_activate().
 * @param il - icon-list
 * @param i - icon
 * @param evas - evas, or NULL for auto
 */
int evas_file_activate(icon_list *il,icon *i,Evas *evas) {
  int   ret=NOIR_NODATA;

  if(i&&il) {
    if(((ret=file_activate(il->sess,ICON_GET_FILENAME(i),NULL,NULL))==NOIR_SUCC)
       &&(!il->sess||!noir_sess_get_desktop(il->sess))
       &&evas) {
      noir_theme *t=(noir_theme *)il->theme;
      int         what=-1;
      file_info  *fi=i->user2;
      posixmd    *intrinsic=i->user1;
      const char *mime=file_info_get_mime(fi);

      if(!evas)
        evas=get_evas_from_iconlist(il);

      if(mime)
        what=(!strcmp(MIME_EXECUTABLE,mime))||
             (!strcmp(MIME_SCRIPT,mime))
             ?1:0;
      else if(intrinsic)
        what=posixmd_get_mode(intrinsic)&(S_IXUSR|S_IXGRP|S_IXOTH)?1:0;

      if(t&&(what>=0)) {
        if((what==0)&&t->ani_open_file)
          t->ani_open_file(il,i);
        else if((what==1)&&t->ani_open_app)
          t->ani_open_app(il,i); }}}
  return ret; }



#endif
