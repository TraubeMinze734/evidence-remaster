/** @file  id_flutterby.c
 *  @brief subtle example idle-handler.
 *
 * less in-your-face idle-animation.
 * needs butterfly-icons to be visible. ("evidence -T metallo")
 */

#include <stdlib.h>
#include <string.h>
#include <Evas.h>

#include "evidence.h"
#include "evas_macros.h"
#include "icon_list.h"
#include "theme.h"

#include <stdio.h>



#define  BF_NAME      "Butterfly_"
#define  BF_FILE      "Butterfly0.png"
/* for a less busy display, a BF_CHANCE of 125 seems good */
#define  BF_CHANCE    12
#define  BF_STEP      32
#define  MAX_ANI      2

#define  RND(lessthan)  (((float)lessthan)*rand()/RAND_MAX)

typedef struct _bf {
  icon           *i;
  icon_el        *el_fm,*el_to;
  int             frame;
  Evas_Object    *fm,*to;
  struct _bf     *next; } bf;

typedef struct _bfctl {
  icon_list      *il;
  Evas           *evas;
  char           *dir;
  bf             *bfs;
  long            serial;
  int             no,animated; } bfctl;



/**
 * is icon a butterfly?
 * @return TRUE/FALSE
 * @param i  the icon to test
 */
static inline int is_bf(icon *i) {
  const char *a,*p;
  if((a=i->name)) {
    if((p=file_name(a)))   /* not too happy with this one */
      a=p;
    if(!strncmp(a,BF_NAME,sizeof(BF_NAME)-1)) {
      return TRUE; }}
  return FALSE; }



/**
 * release butterfly-data
 * @param die  the struct to release
 */
static inline void unmake_bf(bf **die) {
  if(die&&*die) {
    D("killing the butterfly with bad bad vibes...\n")
    /* since we now add the evas object we cross-fade to the actual icon, it
       will be destroyed together with that icon, so don't destroy it here! */
    free(*die);
    *die=NULL; }}



/**
 * kill all butterflies : (
 * @param li  linked list of butterflies
 */
static void unmake_bf_list(bf **li) {
  if(li&&*li) {
    bf *bf=*li,*t;
    D("killing all butterflies...\n")
    while(bf) {
      t=bf->next;
      unmake_bf(&bf);
      bf=t; }
    *li=NULL; }}



/**
 * make colourful butterfly and hide it underneath a dull one.
 * we'll then crossfade from the dull to the pretty one.
 * @return an evas-object
 * @param e     the canvas to create the butterfly on
 * @param x     x-position
 * @param y     y-position
 * @param file  the file holding the butterfly-image
 */
static inline Evas_Object *make_bf_obj(Evas *e,int x,int y,char *file) {
  int          w,h,err;
  Evas_Object *o=evas_object_image_add(e);

  evas_object_image_file_set(o,file,NULL);
  if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
    E(0,"error #%d loading image %s...\n",err,file);
    evas_object_del(o);
    return NULL; }

  evas_object_image_size_get(o,&w,&h);
  evas_object_resize(o,w,h);
  evas_object_image_fill_set(o,0,0,w,h);
  evas_object_color_set(o,255,255,255,0);
  evas_object_layer_set(o,400);
  evas_object_move(o,x,y);
  evas_object_show(o);
  return o; }



/**
 * create objects for butterfly and fill in administrative struct
 * static inline struct _bf *fill_bf
 * @param e     the canvas the butterfly should live in
 * @param il    the icon-list
 * @param i     the icon we want to crossfade
 * @param mybf  the butterfly struct to fill in
 * @param tail  previous butterfly (so we can create a linked list)
 * @param file  the file holding the image of a butterfly. Butterfly?.png, 0..2
 */
static inline struct _bf *fill_bf(Evas *e,icon_list *il,icon *i,bf *mybf,bf *tail,char *file) {
  icon_el *el=i->els,*el_to;
  while(el) {
    if(!strcmp(evas_object_type_get(mybf->fm=((Evas_Object *)el->data)),"image")) {
      /* in theory, we have a race re the filename from here to make_bf_obj(),
         but since it doesn't really matter what colour the new butterfly
         comes out as, we do not catch that */
      file[strlen(file)-5]='0'+((int)RND(3));
      mybf->frame=0;
      mybf->el_fm=el;
      if(el->next&&(el->next->state&IE_ID)) {
        D("reusing icon_el...\n")
        el_to=el->next; }
      else if(!(el_to=malloc(sizeof(struct _icon_el)))) {
        D("cannot create new icon_el...\n")
        return tail; }
      if(!(mybf->to=make_bf_obj(e,i->x+el->xo-il->sx,i->y+el->yo-il->sy,file))) {
        free(mybf);
        free(el_to);
        D("make_bf_obj() somehow failed, abort creation...\n")
        return tail; }
      if(el->next!=el_to) {
        memcpy(mybf->el_to=el_to,mybf->el_fm,sizeof(struct _icon_el));
        mybf->el_fm->next=el_to; }     /* put in list after src el */
      el_to->state|=IE_ID;             /* mark as ours */
      mybf->el_to->data=(void *)mybf->to;
      el=NULL; }
    else
      el=el->next; }
  mybf->i=i;
  mybf->next=tail;
  return mybf; }



/**
 * create a list of all butterfly-icons in this session. initialize animation.
 * @param ctl  our param-blk
 */
static void make_bf_list(bfctl *ctl) {
  icon_list *il=ctl->il;
  icon      *i=il->icons;
  bf        *bf=NULL,*bn;
  int        c=0;

  D("Scanning icon-list for butterflies...\n")
  while(i) {
    if(is_bf(i)) {
      c++;
      D("-- Found %d: \"%s\"!\n",c,i->label)
      if((bn=malloc(sizeof(struct _bf)))) {
        if((bn=fill_bf(ctl->evas,ctl->il,i,bn,bf,ctl->dir)))
          bf=bn; }}
    i=i->next; }
  ctl->no=c;
  ctl->serial=il->serial;
  ctl->bfs=bf; }



/**
 * animate a given butterfly
 * @return TRUE when more animation is required, FALSE otherwise
 * @param il    icon-list
 * @param mybf  the butterfly to crossfade
 */
static inline int animate_bf(icon_list *il,bf *mybf) {
/*  D("Animating butterfly \"%s\"...\n",bf->i->label) */
  mybf->frame+=16;
  if(mybf->frame>=255) {
    D("Animation for butterfly \"%s\" completed, dequeuing...\n",mybf->i->label)
    mybf->frame=0;
    evas_object_color_set(mybf->to,255,255,255,255);
    evas_object_del((Evas_Object *)mybf->el_fm->data);
    mybf->fm=mybf->to;
    mybf->to=NULL;
    mybf->el_to->state=IE_ID;
    mybf->el_fm->data=(void *)mybf->fm;
    evas_object_layer_set(mybf->fm,0);
    return FALSE; }
  evas_object_color_set(mybf->to,255,255,255,mybf->frame);
  evas_object_color_set(mybf->fm,255,255,255,255-mybf->frame);
  return TRUE; }



/**
 * system is idle, do silly foo in the file-manager window
 * @return FALSE to destall, TRUE otherwise
 * @param data  pointer to our private data (bfctl)
 */
int idle_func(void **data) {
  bfctl    *c=*((bfctl **)data);
  bf       *a,*b;
  int       n;

  if(!c) {
    E(0,"called without paramblock!?  destalling...\n");
    return FALSE; }

  if(c->no) {              /* have butterflies... */
    int fail=(c->serial!=c->il->serial);  /* we have serials now telling us when the view has been superseded */
    if(fail) {             /* if not, rebuild list... */
      D("Serial has changed, rebuilding list...\n")
      unmake_bf_list(&c->bfs);
      make_bf_list(c);
      if(!c->no) {         /* if new list is empty, go home */
        D("New list is empty, going home (theme from local hero).\n")
        return TRUE; }}

    if((c->animated<MAX_ANI)&&(RND(BF_CHANCE)<3)) {  /* start new animation? */
      int which=RND(c->no);
      D("Start new animation #%d.\n",which);
      a=c->bfs;
      while(a&&which) {
        which--;
        a=a->next; }
      if(a&&!a->frame) {
        c->animated++;
        a->frame++; }}

    D("Any ongoing animations? (%d/%d)\n",c->animated,c->no)
    n=0;
    a=c->bfs;              /* progress in all current animations */
    while(a) {
      if(a->frame) {
        D("Yes, #%d (%d)\n",n,a->frame)
        if(!animate_bf(c->il,a)) {
          D("DQ #%d ***\n",n)
          if((b=fill_bf(c->evas,c->il,a->i,a,a->next,c->dir)))
            a=b;
          else {
            E(0,"fill_bf() broke!? dequeuing idle-handler...\n");
            return FALSE; }
          c->animated--; }}
      n++;
      a=a->next; }

    { Eina_List *ev_up=evas_render_updates(c->evas);
      if(ev_up)
        evas_render_updates_free(ev_up); } }   /* render updates */

  else {
    D("Have no butterflies, re-scanning list...\n")
      make_bf_list(c); }

  return TRUE; }  /* TRUE==success.  failure will remove handler. */



/**
 * set up silly idle animations
 * @return call idler every n ms, 0 for never
 * @param ilp   the icon-list to display the animation in
 * @param data  where to store a pointer to the param-block we'll create
 * @param t     the current theme
 */
int idle_init(void *ilp,void **data,noir_theme *t) {
  bfctl *ctl;
  size_t  l=strlen(t->path)+strlen(t->name)+3+sizeof(BF_FILE);

  D("Idle init...\n")

  if(!(ctl=malloc(sizeof(struct _bfctl))))
    return 0;

  ctl->animated=ctl->no=0;
  if(!(ctl->il=(icon_list *)ilp)||
     !(ctl->evas=get_evas_from_iconlist(ctl->il))) {
    E(0,"error getting icon-list or canvas...\n");
    free(ctl);
    return 0; }

  if((ctl->dir=malloc(l+4)))
    snprintf(ctl->dir,l,"%s/%s/" BF_FILE,t->path,t->name);

  make_bf_list(ctl);

  *data=(void *)ctl;

  return 175; }   /* return desired update frequency in ms */



/**
 * clean-up on destall
 * @return TRUE for could destall, FALSE for do not deactivate handler
 * @param data  this idler's param-block (bfctl)
 */
int idle_exit(void **data) {
  bfctl    *ctl=*((bfctl **)data);

  D("Idle exit...\n")

  if(ctl) {
    if(ctl->bfs)
      unmake_bf_list(&ctl->bfs);
    ctl->no=0;
    if(ctl->dir)
      free(ctl->dir);
    ctl->dir=NULL;
    free(ctl); }
  *data=NULL;

  return TRUE; }
