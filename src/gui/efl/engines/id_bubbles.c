/** @file  id_bubbles.c
 *  @brief campy example idle-handler.
 *
 * bubbles rise in icon-view. works best with fishtank-theme
 * (evidence -T fishtank).  extra fish by tillsan.
 *
 * the handler doesn't install any properties although it really really should.
 */

#include "evidence.h"
#include <stdlib.h>
#include <string.h>
#include <Evas.h>

#include "evas_macros.h"
#include "icon_list.h"
#include "theme.h"


#ifndef FALSE
#  define FALSE (0)
#  define TRUE (!(FALSE))
#endif

#define  FILENAME     "bubble1.png"
#define  FISH         "Fish1.png"
#define  MAX_BUBBLES  4
#define  BUBSIZE     16
#define  XSTEP        1
#define  YSTEP        3

#define  RND(lessthan)  (((float)lessthan)*rand()/RAND_MAX)



typedef struct _bubble {
  Evas_Object    *o;
  int             x,y;
  struct _bubble *next; } bubble;

typedef struct _fish {
  Evas_Object    *o;
  int             x,y;
} fish;
  
typedef struct _bubctl {
  icon_list      *il;
  Evas           *evas;
  bubble         *anchor;
  char           *dir;
  fish           *fish;
  char           *fishfile;
  int             no; } bubctl;



/**
 * make evas-object for a bubble
 * @return an evas image-object
 * @param e     the canvas for the bubble to live on
 * @param x     horizontal position of the bubble
 * @param y     vertical position
 * @param file  file containing a bubble "bubble?.png", 1..5
 */
static inline Evas_Object *make_bubble_obj(Evas *e,int x,int y,char *file) {
#ifdef DEBUG_BUBBLES
  Evas_Object *o=evas_object_line_add(e);
  evas_object_line_xy_set(o,x,y,x+8,y+8);
#else
  int w,h,err;
  Evas_Object *o=evas_object_image_add(e);

  file[strlen(file)-5]='1'+RND(4);

  evas_object_image_file_set(o,file,NULL);
  if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
    E(0,"error #%d loading image %s...\n",err,file);
    evas_object_del(o);
    return NULL; }

  evas_object_image_size_get(o,&w,&h);
  evas_object_resize(o,w,h);
  evas_object_image_fill_set(o,0,0,w,h);
#endif

  evas_object_layer_set(o,-100+RND(200));
  evas_object_move(o,x,y);
  evas_object_show(o);
  return o; }



/**
 * create a bubble
 * @return a new bubble object
 * @param c  our param-blk
 */
static inline bubble *make_bubble(bubctl *c) {
  bubble *b;
  if((b=malloc(sizeof(struct _bubble)))) {
    if(!(b->o=make_bubble_obj(c->evas,
                              b->x=RND(c->il->vis_w),b->y=RND(c->il->vis_h),
                              c->dir))) {
      free(b);
      return NULL; }
    b->next=c->anchor;
    c->anchor=b; }
  c->no++;
  return b; }



/**
 * destroy a bubble
 * @param c  our param-blk
 * @param b  the bubble to burst
 */
static inline void unmake_bubble(bubctl *c,bubble *b) {
  bubble *t=c->anchor;
  if(b==t)
    c->anchor=b->next;
  else {
    while(t->next&&(t->next!=b))
      t=t->next;
    if(!t) {
      E(0,"request to remove a bubble that's not in our list!? : (\n");
      return; }
    t->next=b->next; }
  evas_object_del(b->o);
  free(b); }



/**
 * animate bubbles
 * @param c  our param-blk
 */
static inline void move_bubbles(bubctl *c) {
  if(c->no) {
    bubble *a,*b=c->anchor;
    while(b) {
      a=b->next; /* current bubble may go away, better get all we need NOW */
      switch((int)RND(4)) {
      case 0: b->x-=XSTEP; break;
      case 1: b->x+=XSTEP; break; }
      if(((b->y-=YSTEP)<-BUBSIZE)||(b->x<-BUBSIZE)||(b->x>c->il->vis_w+BUBSIZE)) {
        unmake_bubble(c,b);
        c->no--; }
      else {
        evas_object_move(b->o,b->x,b->y); }
      b=a; }}}



/**
 * make the fish (there can be only one!)
 * @param c  our param-blk
 */
static void make_fish(bubctl *c) {
  fish *f;
  int w, h, err;

  if((f=malloc(sizeof(fish)))) {
    f->o=evas_object_image_add(c->evas);
    evas_object_image_file_set(f->o, c->fishfile, NULL);

    if((err=evas_object_image_load_error_get(f->o))!=EVAS_LOAD_ERROR_NONE) {
      E(0,"error #%d loading image %s...\n",err,c->fishfile);
      evas_object_del(f->o);
      return; }

    c->fish = f;
    evas_object_image_size_get(f->o,&w,&h);
    c->fish->x=-w;
    c->fish->y=RND(c->il->vis_h); 
    evas_object_layer_set(f->o,-100+RND(200));
    evas_object_move(f->o, c->fish->x, c->fish->y);
    evas_object_resize(f->o,w,h);
    evas_object_image_fill_set(f->o,0,0,w,h);

    evas_object_show(f->o); }}



/**
 * destroy the fish
 * @param c  our param-blk
 */
static void unmake_fish(bubctl *c) {
  evas_object_del(c->fish->o);
  free(c->fish);
  c->fish=NULL; }



/**
 * system is idle, do silly foo in the file-manager window
 * @return FALSE to destall, TRUE otherwise
 * @param data  pointer to our private data (bubctl)
 */
int idle_func(void **data) {
  bubctl    *ctl=*((bubctl **)data);

  if(!ctl) {
    E(0,"called without paramblock!?  destalling...\n");
    return FALSE; }

  move_bubbles(ctl);

  if((ctl->no<MAX_BUBBLES)&&(RND(10)<3))
    make_bubble(ctl);

  if (RND(1000) < 3 && !ctl->fish)
	  make_fish(ctl);

  if(ctl->fish) {
    ctl->fish->x += 2;
    evas_object_move(ctl->fish->o, ctl->fish->x ,ctl->fish->y);
    if(ctl->fish->x>ctl->il->vis_w)
      unmake_fish(ctl); }

  (void)evas_render_updates(ctl->evas);

  return TRUE; }  /* TRUE==success.  failure will remove handler. */



/**
 * set up silly idle animations
 * @return call idler every n ms, 0 for never
 * @param ilp   the icon-list to display the animation in
 * @param data  where to store a pointer to the param-block we'll create
 * @param t     the current theme
 */
int idle_init(void *ilp,void **data,noir_theme *t) {
  bubctl *ctl;
  size_t  l=strlen(t->path)+strlen(t->name)+3+sizeof(FILENAME);

  if(!(ctl=malloc(sizeof(struct _bubctl))))
    return 0;

  ctl->il=(icon_list *)ilp;
  ctl->evas=get_evas_from_iconlist(ctl->il);
  ctl->anchor=NULL;
  if((ctl->dir=malloc(l+4)))
    snprintf(ctl->dir,l,"%s/%s/" FILENAME,t->path,t->name);

  if((ctl->fishfile=malloc(l+4)))
    snprintf(ctl->fishfile,l,"%s/%s/" FISH,t->path,t->name);

  ctl->no=0;
  ctl->fish = NULL;

  *data=(void *)ctl;

  return 125; }   /* return desired update frequency in ms */



/**
 * clean-up on destall
 * @return TRUE for could destall, FALSE for do not deactivate handler
 * @param data  this idler's param-block (bubctl)
 */
int idle_exit(void **data) {
  bubctl    *ctl=*((bubctl **)data);

  if(ctl) {
    bubble *a,*b=ctl->anchor;
    while(b) {
      a=b->next;
      unmake_bubble(ctl,b);
      b=a; }
    ctl->no=0;
    if(ctl->dir)
      free(ctl->dir);
    ctl->dir=NULL;
    if(ctl->fishfile)
      free(ctl->fishfile);
    ctl->fishfile=NULL;
    if(ctl->fish) {
      evas_object_del(ctl->fish->o);
      free(ctl->fish); }

    free(ctl); }
  *data=NULL;

  return TRUE; }
