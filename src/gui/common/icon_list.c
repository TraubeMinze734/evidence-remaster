/** @file icon_list.c
 *  @brief generic parts of icon-lists.
 *  right now only used when compiling with specific parts from evas2.c,
 *  should replace gnome-icon-list in the future, however. */

#include "icon_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"     /* qsrt() */
#include "errors.h"

#ifndef FALSE
#  define FALSE (0)
#  define TRUE (!FALSE)
#endif

#include "gui/efl/common/theme.h"    /* FIXME ### */



/*****************************************************************************/
/* icon elements */



/**
 * create new icon-element.
 * @param state - this element should be visible when the icon is in 'state'
 * @param xo - x-offset to icon's upper-left corner (to "icon-x")
 * @param yo - y-offset to icon's upper-left corner (to "icon-y")
 * @param w - width of element
 * @param h - height of element
 * @param data - payload (e.g. an Evas_Object)
 * @param next - next element, or NULL for EOL
 */
icon_el *icon_el_new(icon_state state,int xo,int yo,int w,int h,void *data,void *next) {
  icon_el *e;

  if((e=malloc(sizeof(icon_el)))) {
    memset(e,0,sizeof(icon_el));
    e->data=data;
    e->xo=xo;
    e->yo=yo;
    e->w=w;
    e->h=h;
    e->state=state;
    e->next=next;
    return e; }

  return next; }



/**
 * destruct an icon-element.
 *
 * if e->data is set, will call icon_el_data_dst().  since the nature of the
 * data is GUI-specific (Evas_Object or such), icon_el_data_dst() is as well.
 * it must be defined in the GUI-specific module.
 * @param e - the element to destruct
 */
void icon_el_dst(icon_el *e) {
  if(e) {
    e->next=NULL;   /* paranoia */
    e->state=IE_DK; /* paranoia */
    if(e->data) {
      icon_el_data_dst(e->data);
      e->data=NULL; }
    free(e); }}



/**
 * destruct an icon-element and all its successors.
 * @param e - the first element to destruct (normally the first element of the icon)
 */
icon_el *icon_el_dst_all(icon_el *e) {
  icon_el *n;
  while(e) {
    n=e;
    e=e->next;
    icon_el_dst(n); }
  return e; }



/**
 * find icon-element by tag.
 *
 * convenience function.
 * @param el - first element in list
 * @param t - tag to look for (first match)
 * @param o - where to copy the data-element of the found icon_el, or NULL
 */
icon_el *icon_el_get_by_tag(icon_el *el,element_tag t,void **o) {
  while(el&&(el->tag!=t))
    el=el->next;
  if(o)
    *o=el?el->data:NULL;
  return el; }



/**
 * destruct icon-element by tag.
 *
 * convenience function.
 * @param i - the icon to remove elements from
 * @param t - tag to look for (all matches)
 */
icon *icon_el_dst_by_tag(icon *i,element_tag t) {
  if(i) {
    icon_el *el=i->els,*n;

    while(el) {
      if(el->next&&(el->next->tag==t)) {
        n=el->next;
        el->next=n->next;
        icon_el_dst(n); }
      el=el->next; }

    if(i->els&&(i->els->tag==t)) {
      el=i->els;
      i->els=el->next;
      icon_el_dst(el); }}
  return i; }



/*****************************************************************************/
/* icons */



/**
 * destruct an icon and its elements.
 * @param i - the icon to release
 */
void icon_dst(icon *i) {
  if(i) {
    if(i->label)
      free(i->label);
    if(i->file)
      free(i->file);
    if(i->name)
      free(i->name);
    if(i->els)
      i->els=icon_el_dst_all(i->els);
    free(i); }}



/**
 * destruct an icon and all its successors.
 * @param i - the first icon to release
 */
void icon_dst_all(icon *i) {
  icon *j;
  while(i) {
    j=i->next;
    if(i->label)
      free(i->label);
    if(i->file)
      free(i->file);
    if(i->name)
      free(i->name);
    if(i->els)
      i->els=icon_el_dst_all(i->els);
    i->next=NULL;
    free(i);
    i=j; }}



/*****************************************************************************/
/* icon-lists */



/**
 * destruct an icon-list and all its icons.
 * @param il - the icon-list to release
 */
void icon_list_destroy(void *il) {
  volatile icon_list *vil=il;

  icon_dst_all(vil->icons);
  if(vil->name)
    free(vil->name);
  if(vil->sep)
    free(vil->sep);
  if(vil->find)
    free(vil->find);
  memset(il,0,sizeof(icon_list));
  free(il); }



/**
 * recalc layout of all icons in list.
 * unless the FR_NOSCROLL-bit is set on the frozen-property, a redraw will
 * be effected calling the efun sb_update().
 * @param il - the icon-list
 */
int icon_list_layout_all(icon_list *il) {
  icon *j;
  int _m=0,         /* highest icon in row */
       c=0,         /* icon-count */
      _w=0, _h=0;   /* w/h position */

  if(!il)
    return NOIR_FAIL;

  if(!il->serial)
    return NOIR_IGNORED;

  _w=il->xpad;
  _h=il->ypad;

  for(j=il->icons;j;j=j->next) {
    if((j->x!=_w)||(j->y!=_h)) {  /* position has changed */
      j->x=_w;
      j->y=_h;
      icon_move(j,_w-il->sx,_h-il->sy); }

    j->index=c++;
    if(j->h>_m)         /* adjust height-max if needed */
      _m=j->h;

    _w+=j->w+il->wpad;
    if((j->next)&&((_w+((j->next->w*ICON_LIST_WRAP16)/16))>il->width)) {   /* obscured too much? => wrap */
      _h+=_m+il->hpad;  /* add height of highest icon in prvs line */
      _m=0;             /* reset highest-in-line memory */
      _w=il->xpad; }}   /* beginning of row */

  il->num_icons=c;
  il->height=_h+_m;

  if(!(il->flags&ILF_NOSCROLL)) {
    sb_update(il,FALSE); }

  return NOIR_SUCC; }



/**
 * compare two icons (for sort-order)
 * @return -1,0,+1
 * @param flags  sort-flags
 * @param a      the first  icon
 * @param b      the second icon
 */
static int icon_compare(const void *flags,const void *a,const void *b) {
  return model_sort_fun(*((int *)flags),
                        ((icon *)a)->label,((icon *)a)->user2,
                        ((icon *)b)->label,((icon *)b)->user2); }



/**
 * sort icons in icon-list.
 * @param il - the icon-list
 */
int icon_list_resort(icon_list *il) {
  int   n;
  int   flags=il->sortflags?*il->sortflags:0;

  if(il&&(n=il->num_icons)) {
    void **ib=(void **)malloc(sizeof(void *)*n),
         **ic=ib;
    icon *i=il->icons;

    while(n--) {
      *(ic++)=i;
      i=i->next; }

    qsrt(ib,&flags,0,il->num_icons-1,icon_compare);

    n=il->num_icons-1;
    ic=ib;
    i=il->icons=*(ic++);
    while(n--) {
      i->next=*(ic++);
      i=i->next; }
    i->next=NULL;

    free(ib);

    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * make icon-label with suggested wrap-points.
 * @param il - the icon-list (containing a list of separator characters)
 * @param in - the text for the label
 */
static inline char *make_label(icon_list *il,const char *in) {
#define F 4
#define P 14
  size_t l;
  if((l=strlen(in))<=(P+F))
    return strdup(in);
  else {
    int         pb=(l/P),c;
    const char *s=il->sep;
    char       *p,*r,*o;

    if((o=malloc(l+=pb+1))) {
      strcpy(p=o,in);

      while(pb-->0) {
        p+=P;                            /* to next potential breakpoint */
        r=p;
        c=F;
        while(--c&&!strchr(s,*r))        /* scan backwards */
          r--;

        if(!c) {                         /* no separator, so... */
          r=p+1;
          c=F;
          while(--c&&*r&&!strchr(s,*r))  /* ...scan forward */
            r++;

          if(!*r||!c)                    /* still no separator, hard cut */
            r=p; }

        l=strlen(++r);                   /* insert line-break */
        memmove(r+1,r,l+1);
        *(r++)='\n';

        if(l>P)
          p=r;
        else
          pb=0; }}

    return o; }}



/**
 * insert icon into icon-list.
 * @param il - icon-list
 * @param index - insert after index'th icon (0 for "make first icon in list")
 * @param name - the name of the icon-image ("/foo/bar/binary.png")
 * @param file - the file represented by the icon ("/bin/ls")
 * @param label - the text-label to go underneath the icon-image ("ls")
 * @param thumb - the "thumbnail" for the file, or NULL ("/home/azundris/.thumbnails/large/ffa4b11d64a9d082c4b8d4e795d9b3e8.png")
 * @param user1 - user-data.  in evidence, the stat-info for the icon.
 * @param user2 - user-data.  in evidence, the file-info for the icon.
 * @param redraw - redraw icon-list after icon is inserted?
 */
icon *icon_list_icon_insert(icon_list *il,
                            int index,
                            const char *name,const char *file,const char *label,const char *thumb,
                            void *user1,void *user2,
                            int redraw) {
  icon *i,*j;

  if(!il||!file||!*file)
    return NULL;

  if((i=malloc(sizeof(icon)))) {
    if(!(i->file=strdup(file)))
      goto free_i;
    if(!(i->label=label?make_label(il,label):strdup("")))
      goto free_file;
    if(!(i->name=strdup(name?name:"")))
      goto free_label;

    i->x=i->y=i->w=i->h=0;
    i->state=IE_DFL;
    i->next=NULL;
    i->user1=user1;
    i->user2=user2;
    i->els=NULL;

    if(!(i->els=(void *)((noir_theme *)il->theme)->icon_els_create(il,i,thumb)))
      goto free_name;

    if((index<1)||!il->icons) {
      i->next=il->icons;
      il->icons=i; }
    else {
      j=il->icons;
      while(--index&&j->next)
        j=j->next;
      i->next=j->next;
      j->next=i; }

    if(!i->els) {
      i->w=((noir_theme *)il->theme)->icon_width;
      i->h=((noir_theme *)il->theme)->icon_height; }

    if(!redraw) {
      il_flags of=il->flags;
      il->flags|=ILF_NOSCROLL;
      icon_list_layout_all(il);
      il->flags=of; }
    else
      icon_list_layout_all(il);

    return i;

   free_name:
    free(i->name);
   free_label:
    free(i->label);
   free_file:
    free(i->file);
   free_i:
    free(i); }
  return NULL; }



/**
 * determine index, insert icon into icon-list.
 * see icon_list_icon_insert() re the parameters.
 */
icon *icon_list_icon_insert_sorted(icon_list *il,
                                   const char *name,const char *file,const char *label,const char *thumb,
                                   void *user1,void *user2,
                                   int redraw) {

  int   n=0,sf=il->sortflags?*(il->sortflags):0;
  icon *i=il->icons;

  while(i&&(model_sort_fun(sf,label,user2,i->label,i->user2)>0)) {
    n++;
    i=i->next; }
  return icon_list_icon_insert(il,n,name,file,label,thumb,user1,user2,redraw); }



#if 0

/**
 * append an icon.
 * see icon_list_icon_insert() re the parameters.
 * implemented as a macro calling icon_list_icon_insert() with il->num_icons
 * as position.
 */
icon *icon_list_icon_append(icon_list *il,const char *name,const char *file,const char *label,const char *thumb,void *user1,void *user2,int redraw) {
  if(!il)
    return NULL;
  return icon_list_icon_insert(il,il->num_icons,name,file,label,thumb,user1,user2,redraw); }

#endif



/**
 * remove an icon from the icon-list.
 * @param il - the icon-list
 * @param i - the icon to remove
 * @param redraw - redraw the icon-list after removing the icon?
 */
int icon_list_icon_remove(icon_list *il,icon *i,int redraw) {
  if(il&&i) {
    if(il->icons==i)
      il->icons=i->next;
    else {
      icon *j=il->icons;
      while(j&&(j->next!=i))
        j=j->next;
      if(!j)
        return NOIR_NOTFOUND;
      j->next=i->next; }
    icon_dst(i);
    if(redraw)
      icon_list_layout_all(il);
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * find out whether icon is in rubber-band selection.
 * @return TRUE if icon is at least partially in the rectangular rubber-band selection.
 * @param il -the icon-list
 * @param i - the icon
 */
inline int icon_list_icon_in_rubber(icon_list *il,icon *i) {
  int ix,iy;

  ix=i->x;
  iy=i->y;

  return ((il->rx1<(ix+i->w))&&
          (il->ry1<(iy+i->h))&&
          (il->rx2>ix)&&
          (il->ry2>iy)); }



/**
 * find out whether icon is exposed.
 * @return TRUE if icon is at least partially in the exposed part of the scroll-area.
 * @param il -the icon-list
 * @param i - the icon
 */
inline int icon_list_icon_visible(icon_list *il,icon *i) {
/* int x1=i->x,x2=i->x+i->w,y1=i->y,y2=i->y+i->h; */ /* OLD_SCROLL */
  int x1=i->x-il->sx,x2=i->x+i->w-il->sx,y1=i->y-il->sy,y2=i->y+i->h-il->sy;
  return ((x2>=0)&&(y2>=0)&&(x1<=il->vis_w)&&(y1<=il->vis_h)); }



/**
 * get icon number 'in'.
 * @param il - the icon-list
 * @param in - the index
 */
icon *icon_list_get_icon_by_index(icon_list *il,int in) {
  if(il||(in>=0)) {
    icon *i=il->icons;
    while(i&&in--)
      i=i->next;
    return i; }
  return NULL; }



/**
 * get the icon that represents a given file.
 * @param il - the icon-list
 * @param file - the file to find the icon for
 */
icon *icon_list_get_icon_by_file(icon_list *il,const char *file) {
  icon *i=NULL;
  if(il&&file) {
    i=il->icons;
    while(i) {
      if(!strcmp(file,i->file))
	return i;
      i=i->next; }}
  return i; }



/**
 * get the icon at a given coordinate
 * @param il - the icon-list
 * @param x - icon-list relative x (0..)
 * @param y - icon-list relative y (0..)
 */
icon *icon_list_get_icon_at(icon_list *il,int x,int y) {
  icon *i;
  if(il&&(x>=0)&&(y>=0)) {
    x+=il->sx;    /* factor in scrolling */
    y+=il->sy;
    i =il->icons;
    while(i) {
      if(pointer_on_icon_p(il,i,x,y))
        return i;
      i=i->next; }}
  return NULL; }



/**
 * find out the index of the icon at a given coordinate
 * @param il - the icon-list
 * @param x - icon-list relative x (0..)
 * @param y - icon-list relative y (0..)
 */
int icon_list_get_index_at(icon_list *il,int x,int y) {
  icon *i=icon_list_get_icon_at(il,x,y);
  return i?i->index:-1; }



/**
 * set separators for word-wrap.
 * @param il - the icon-list to change the separators on
 * @param sep - a C-string containing the desired separators
 */
int icon_list_set_separators(icon_list *il,const char *sep) {
  if(!il||!sep)
    return NOIR_FAIL;

  if(!strcmp(sep,il->sep))
    return NOIR_IGNORED;

  if(il->sep)
    free(il->sep);
  il->sep=strdup(sep);

  icon_list_layout_all(il);

  return NOIR_SUCC; }



/**
 * do the actual footwork for the scrolling routines.
 * @param il - the icon-list
 */
static int icon_list_scroll_do(icon_list *il) {
  int dx,dy;

  if(il->sx>(il->width-il->vis_w))
    il->sx=(il->width-il->vis_w)-1;
  if(il->sx<0)
    il->sx=0;

  if(il->sy>(il->height-il->vis_h))
    il->sy=(il->height-il->vis_h)-1;
  if(il->sy<0)
    il->sy=0;

  dx=il->sx-il->px;
  dy=il->sy-il->py;

  if(dx||dy) {
    icon *i=il->icons;

    while(i) {
      icon_move(i,i->x-il->sx,i->y-il->sy);
      i=i->next; }
    sb_update(il,TRUE);

    il->px=il->sx;
    il->py=il->sy;

    return NOIR_SUCC; }

  return NOIR_IGNORED; }



/**
 * scroll icon-list to absolute position.
 * @param il - the icon-list
 * @param sx - the desired horizontal position (0..)
 * @param sy - the desired vertical position (0..)
 */
int icon_list_scroll_abs(icon_list *il,int sx,int sy) {
  if(il) {
    il->sx=sx;
    il->sy=sy;
    return icon_list_scroll_do(il); }
  return NOIR_FAIL; }



/**
 * scroll relative to current position.
 * @param il - the icon-list
 * @param sx - the horizontal offset
 * @param sy - the vertical offset
 */
int icon_list_scroll_rel(icon_list *il,int sx,int sy) {
  if(!il)
    return NOIR_FAIL;
  if(sx||sy) {
    il->sx+=sx;
    il->sy+=sy;
    return icon_list_scroll_do(il); }
  return NOIR_IGNORED; }



/**
 * scroll icon-list page-wise.
 * @param il - the icon-list
 * @param sx - scroll horizontally: <0, page left; 0, don't scroll; >0, page right
 * @param sy - scroll vertically:   <0, page up;   0, don't scroll; >0, page down
 */
int icon_list_scroll_pg(icon_list *il,int sx,int sy) {
  int         y2;
  icon       *i,*j,*k;


  /* scroll up */
  if(sy<0) {
    /* find first visible icon (top left) */

    k=il->icons;
    y2=il->sy-il->vis_h;

    for(i=il->icons;i&&!icon_list_icon_visible(il,i);i=i->next) {
      if(i->y<y2)
        k=i; }

    if(!(j=i))
      return NOIR_NOTFOUND;


    /* if icon is only partially visible, find first fully visible */
    if(i->y<il->sy) {
      for(y2=i->y;i&&(i->y==y2);i=i->next)
        ;
      if(!i)
        i=j; }

    y2=i->y-il->vis_h;
    for(i=k;i&&i->y<y2;i=i->next)
      ;

    il->sy=i->y; }


  /* scroll down */
  else if(sy>0) {
    /* find first visible icon (top left) */
    y2=il->sy;
    for(i=il->icons;i&&(i->y<y2);i=i->next)
      ;
    if(!i)
      return NOIR_NOTFOUND;

    /* find last only partially visible. that's our new scroll-base. */
    j=i;
    y2=il->sy+il->vis_h;
    while(i&&(i->y<y2)) {
      j=i;
      i=i->next; }
    il->sy=j->y; }

  il->sy-=il->ypad;

  return icon_list_scroll_do(il); }



/**
 * reset icon-list.
 * this throws out all icons and resets settings to "factory defaults."
 * in other words, it should be similar to destructing an icon-list and
 * making a new one.
 * @param il - the icon-list
 */
icon_list *icon_list_rst(icon_list *il) {
  static long serial=0;

  if(!il)
    return NULL;

  il->num_icons=
  il->mxb      =
  il->myb      =
  il->mxe      =
  il->mye      =
  il->sx       =
  il->sy       =
  il->px       =
  il->py       =
  il->flags    =ILF_NONE;
  il->ss       =SB_NONE;
  il->ki       =-1;
  il->dragflag =0;
  il->serial   =++serial;
  ICON_LIST_CLEAR_FIND(il);

  if(il->rubber)
    ((noir_theme *)il->theme)->rubber_dst(&(il->rubber));

  if(il->icons) {
    icon_dst_all(il->icons);
    il->icons=NULL; }

  return il; }



/**
 * create new icon-list
 * @param name - name for the icon-list (evidence: "iconview" or "shelf")
 * @param theme - the theme for this icon-list
 * @param canvas - the canvas for this icon-list (GtkGEvas, Evas, ...)
 * @param wpad - horizontal padding between icons
 * @param hpad - vertical padding between icons
 * @param flags - reserved
 */
icon_list *icon_list_new(const char *name,void *theme,void *canvas,int wpad,int hpad,int flags) {
  icon_list *il;

  if(!(il=malloc(sizeof(icon_list))))
    return NULL;

  memset(il,0,sizeof(icon_list));

  il->name      =strdup(name);
  il->theme     =theme;
  il->wpad      =wpad;
  il->hpad      =hpad;
  il->xpad      =wpad/2;
  il->ypad      =hpad/2;
  il->canvas    =canvas;
  il->sb_h      =
  il->sb_v      =NULL;
  il->bg        =NULL;
  il->width     =
  il->height    =
  il->vis_w     =
  il->vis_h     =0;
  il->rubber    =
  il->icons     =
  il->dc        =NULL;
  il->sep       =strdup("- \"/\\ \t\n\r_.");
  il->extensions=NULL;

  if(!(il->find=malloc(ICON_LIST_FIND_MAX))) {
    free(il);
    return NULL; }

  return icon_list_rst(il); }



/**
 * adjust size of icon-list.
 *
 * the GUI-part of the icon-list will likely have a callback for when the
 * window's size changes.  that callback should in turn call icon_list_resized
 * to let the internal representation know about the change as well.
 * icon_list_resized() will also re-layout the window's contents.
 * @param il - the icon-list
 * @param w - the new width of the icon-list
 * @param h - the new height of the icon-list
 */
void icon_list_resized(icon_list *il,long w,long h) {
  il->width=w;
  icon_list_layout_all(il); }
