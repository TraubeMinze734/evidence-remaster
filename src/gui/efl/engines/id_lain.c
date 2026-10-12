/** @file  id_lain.c
 *  @brief garish example idle-handler.
 *
 * an homage to "serial experiments: lain"
 *
 * the handler doesn't install any properties although it really really should.
 */

#include <stdlib.h>
#include <string.h>
#include <Evas.h>

#include "evidence.h"
#include "evas_macros.h"
#include "icon_list.h"
#include "theme.h"

#include <stdio.h>

#define PROP PACKAGE_DATA_DIR "/themes/fonts/vera.ttf"
#define MONO PACKAGE_DATA_DIR "/themes/fonts/veramono.ttf"

#define TOD_CLOCK



#define  RND(lessthan)  (((float)lessthan)*rand()/RAND_MAX)



typedef struct _lainctl {
  icon_list      *il;
  Evas           *evas;
  Evas_Object    *akashi;  int ax,ay;
  Evas_Object    *hanseki; int hx,hy;
  Evas_Object    *kaigan;  int kx,ky;
  Evas_Object    *satori;  int sx,sy;
  Evas_Object    *lain0;
  Evas_Object    *lain1;
  Evas_Object    *lain2;
  Evas_Object    *haveyou; int tx,ty;
  Evas_Object    *timer1;
  Evas_Object    *timer2;
  char           *dir;
  int             state;
} lainctl;



/**
 * adjust image
 * @param o  an evas-image-object
 * @param x  x-ordinate
 * @param y  y-ordinate
 * @param a  transparency
 */
static inline void set_image(Evas_Object *o,int x,int y,int a) {
  if(!o)
    return;
  evas_object_layer_set(o,-10);
  evas_object_move(o,x,y);
  evas_object_color_set(o,255,255,255, a);
  evas_object_show(o); }



#define CYCLE 48
#define MOVE  64



/**
 * system is idle, do silly foo in the file-manager window
 * @return FALSE to destall, TRUE otherwise
 * @param data  pointer to our private data (lainctl)
 */
int idle_func(void **data) {
  lainctl     *ctl=*((lainctl **)data);
  double       c,s=255.0/((float)CYCLE);
  static char  t2[]="00:00:00";
  int          state;
  int          w,h;

  if(!ctl) {
    E(0,"called without paramblock!?  destalling...\n");
    return FALSE; }

  state=ctl->state;
  w=ctl->il->vis_w;
  h=ctl->il->vis_h;

#if 0
  if(!(state%CYCLE))
    E(0,"lain-idle %d\n",state);
#endif

  if(state<1) {
    int hw=(w-ctl->tx)/2;
    ctl->tx=hw;

    set_image(ctl->lain0,w/6,h/3,0);
    set_image(ctl->lain1,w/2,h/2,0);
    set_image(ctl->lain2,w/6,h/5*3,0);

    set_image(ctl->akashi,  ctl->ax=(w*6)/7, ctl->ay=-128,     255);  /* top->bot */
    set_image(ctl->hanseki, ctl->hx=w,       ctl->hy=h/5,      255);  /* right->left */
    set_image(ctl->kaigan,  ctl->kx=w/4,     ctl->ky=h,        255);  /* bot->top */
    set_image(ctl->satori,  ctl->sx=-64,     ctl->sy=(h*5)/8,  255);  /* left->right */
    set_image(ctl->haveyou, hw,              ctl->ty=h/4,      0);

    evas_object_color_set(ctl->haveyou,0,0,0, 0);
    set_image(ctl->timer1,  (w*2)/7,         (h*2)/6,  0);
    evas_object_layer_set(ctl->haveyou,-10);

    evas_object_color_set(ctl->timer1,200,0,64, 0);
    set_image(ctl->timer2,  (w*2)/7,         (h*7)/8,  0);

    evas_object_color_set(ctl->timer2,200,0,64, 31);
    evas_object_layer_set(ctl->timer2,100);

    state++; }
  else if(state<CYCLE) {
    evas_object_color_set(ctl->timer1,200,0,64, (state*(s/2)));
    evas_object_color_set(ctl->lain0,255,255,255, ((float)state)*s);
    state++; }
  else if(state<(CYCLE*2)) {
    c=state-CYCLE;
    if(c>(CYCLE/2.0))
      evas_object_color_set(ctl->haveyou,0,0,0, (c*s)-127);
    evas_object_color_set(ctl->timer1,200,0,64, 127+(c*(s/2)));
    evas_object_color_set(ctl->lain0,255,255,255, 255-(c*s));
    evas_object_color_set(ctl->lain1,255,255,255, c*s);
    state++; }
  else if(state<(CYCLE*3)) {
    c=state-2*CYCLE;
    evas_object_color_set(ctl->haveyou,(c*200)/CYCLE,0,(c*64.0)/CYCLE, 100);
    evas_object_color_set(ctl->timer1,200,0,64, 255-(c*(s/2)));
    evas_object_color_set(ctl->lain1,255,255,255, 255-(c*s));
    evas_object_color_set(ctl->lain2,255,255,255, c*s);
    state++; }
  else if(state<(CYCLE*4)) {
    c=state-3*CYCLE;
    if(c<(CYCLE/2.0))
      evas_object_color_set(ctl->haveyou,200,0,64, 127-(c*s));
    evas_object_color_set(ctl->timer1,200,0,64, 127-(c*(s/2)));
    evas_object_color_set(ctl->lain2,255,255,255, 255-(c*s));
    state++; }

  else if(state<(CYCLE*10))
    state++;

  else if(state<(CYCLE*10+1)) {
    if((ctl->ay+=MOVE)>h) {
      ctl->ay=-128;
      state++; }
    evas_object_move(ctl->akashi,ctl->ax,ctl->ay); }

  else if(state<(CYCLE*10+2)) {
    if((ctl->hx-=MOVE)<-96) {
      ctl->hx=w;
      state++; }
    evas_object_move(ctl->hanseki,ctl->hx,ctl->hy); }

  else if(state<(CYCLE*31))
    state++;

  else if(state<(CYCLE*31+1)) {
    if((ctl->ky-=MOVE)<-96) {
      ctl->ky=h;
      state++; }
    evas_object_move(ctl->kaigan,ctl->kx,ctl->ky); }
  else if(state<(CYCLE*31+2)) {
    if((ctl->sx+=MOVE)>w) {
      ctl->sx=-64;
      state++; }
    evas_object_move(ctl->satori,ctl->sx,ctl->sy); }

  else if(state<(CYCLE*40))
    state++;

  else
    state=1; /* not 0! */

#define TIMER 8
  if(!(state%TIMER)) {
#ifdef TOD_CLOCK
    time_t     t=time(NULL);
    struct tm *l=localtime(&t);
    t2[0]='0'+l->tm_hour/10; t2[1]='0'+l->tm_hour%10;
    t2[3]='0'+l->tm_min/10; t2[4]='0'+l->tm_min%10;
    t2[6]='0'+l->tm_sec/10; t2[7]='0'+l->tm_sec%10;
#else
    int t=(state/TIMER)%1440;
    t2[0]='0'+(t/60)/10; t2[1]='0'+(t/60)%10;
    t2[3]='0'+(t%60)/10; t2[4]='0'+(t%60)%10;
    t2[6]='0'+RND( 6); t2[7]='0'+RND(10);
#endif
    evas_object_text_text_set(ctl->timer2,t2); }

  { Eina_List *ev_up=evas_render_updates(ctl->evas);
    if(ev_up)
      evas_render_updates_free(ev_up); }

  ctl->state=state;

  return TRUE; }  /* TRUE==success.  failure will remove handler. */



/**
 * load an image
 * @return an evas image-object
 * @param ctl   our param-blk
 * @param name  the file holding the image
 */
static inline Evas_Object *make_image(lainctl *ctl,char *name) {
  Evas_Object *o=evas_object_image_add(ctl->evas);
  size_t       l=strlen(ctl->dir)+strlen(name)+2;
  char        *m=malloc(l);
  int          err;

  if(!m)
    return NULL;
  snprintf(m,l,"%s/%s",ctl->dir,name);
  evas_object_image_file_set(o,m,NULL);
  if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
    E(0,"error #%d loading image \"%s\"...\n",err,m);
    evas_object_del(o);
    o=NULL; }
  else {
    int w,h;
    evas_object_image_size_get(o,&w,&h);
    E(1,"\"%s\" %dx%d\n",m,w,h);
    evas_object_resize(o,w,h);
    evas_object_image_fill_set(o,0,0,w,h); }
  free(m);
  return o; }



/**
 * create a text-object
 * @return an evas text-object
 * @param ctl   our param-blk
 * @param text  what we want to say (C-string)
 * @param font  the font to use
 * @param size  the font-size to use
 * @param rw    where to return the width of the newly-created object
 * @param rh    where to return the height of the newly-created object
 */
static inline Evas_Object *make_text(lainctl *ctl,char *text,char *font,double size,int *rw,int *rh) {
  Evas_Object *o=evas_object_image_add(ctl->evas);
  if((o=evas_object_text_add(ctl->evas))) {
    Evas_Coord x,y,w,h;
    evas_object_text_font_set(o,font,size);
    evas_object_text_text_set(o,text);
    evas_object_geometry_get(o,&x,&y,&w,&h);
    if(rw)
      *rw=(int)w;
    if(rh)
      *rh=(int)h;
    if(!w||!h) {
      E(0,"failed to create object using font \"%s\"...\n",font);
      evas_object_del(o);
      o=NULL; }}
  else
    E(0,"failed to create object for \"%s\"...\n",text);
  return o; }



/**
 * set up silly idle animations
 * @return call idler every n ms, 0 for never
 * @param ilp   the icon-list to display the animation in
 * @param data  where to store a pointer to the param-block we'll create
 * @param t     the current theme
 */
int idle_init(void *ilp,void **data,noir_theme *t) {
  lainctl *ctl;
  size_t   l=strlen(t->path)+strlen(t->name)+2;

  if(!(ctl=malloc(sizeof(struct _lainctl))))
    return 0;
  memset(ctl,0,sizeof(struct _lainctl));

  ctl->il=(icon_list *)ilp;
  ctl->evas=get_evas_from_iconlist(ctl->il);
  if((ctl->dir=malloc(l)))
    snprintf(ctl->dir,l,"%s/%s",t->path,t->name);

  ctl->akashi=make_image(ctl,"akashi.png");
  ctl->hanseki=make_image(ctl,"hanseki.png");
  ctl->kaigan=make_image(ctl,"kaigan.png");
  ctl->satori=make_image(ctl,"satori.png");
  ctl->lain0=make_image(ctl,"lain0.png");
  ctl->lain1=make_image(ctl,"lain1.png");
  ctl->lain2=make_image(ctl,"lain2.png");
  ctl->haveyou=make_text(ctl,"Have you ever seen the lain?",PROP,29,&ctl->tx,&ctl->ty);
  ctl->timer1 =make_text(ctl,"214.09",PROP,13,NULL,NULL);
  ctl->timer2 =make_text(ctl,"00:00:00",PROP,36,NULL,NULL);

  *data=(void *)ctl;

  return 125; }   /* return desired update frequency in ms */



/**
 * clean-up on destall
 * @return TRUE for could destall, FALSE for do not deactivate handler
 * @param data  this idler's param-block (lainctl)
 */
int idle_exit(void **data) {
  return TRUE; }
