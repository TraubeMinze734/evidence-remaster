/** @file  emotion.c
 *  @brief Emotion movie thumbnailer (live + static), using modern EFL's
 *         system libemotion (which delegates to gstreamer/xine backends).
 *
 *  replaces the old xine.c thumbnailer, which required the 2005 libxine API.
 *
 *  static thumbnails: seek into the stream, grab the frame via
 *                     emotion_object_image_get(), hand a COPY to Imlib2.
 *  live thumbnails:   the emotion object itself is an Evas smart object;
 *                     we just hand it to the icon-view and play it.
 *
 *  threading: the thumbnailer calls plugins from a worker thread that runs
 *  its own ecore loop (eina_main_loop_is() is TRUE there, but the domain
 *  differs!).  Emotion objects must live in the domain of the thread that
 *  later renders them (the GUI main thread).  therefore we remember the
 *  main thread's identity at init() time and defer all Evas/Emotion work
 *  to it via ecore_main_loop_thread_safe_call_async() + handshake.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "errors.h"
#include "util.h"            /* esprintf() */
#include "evidence.h"        /* main_thread (the GUI thread) */

#include "../thumbnail_private.h"
#include "../thumbnail_plugin.h"

#ifdef HAVE_EVAS2
#  include <Evas.h>
#  include <Ecore.h>
#  include <Emotion.h>
#  include <pthread.h>
#  ifdef HAVE_IMLIB2
#    include <Imlib2.h>
#  endif
#endif

#define SEEK_NUMERATOR   8   /* take a frame from ~ 8% into the movie */
#define SEEK_DENOMINATOR 10
#define MAX_TRIES         8  /* how often to poll while emotion opens the file */

/******************************************************************************/

#ifdef HAVE_EVAS2

typedef struct _emotion_param {
  int          dummy;
} emotion_param;

/* the GUI thread: evidence sets main_thread in main() before the
   thumbnailer workers are ever started; the plugin's init() may run in
   a worker, so we cannot remember the identity ourselves */



typedef struct {
  void           (*cb)(void *);
  void            *data;
  pthread_mutex_t  mtx;
  pthread_cond_t   cond;
  int              done;
} emotion_handshake;

/* runs in the main thread; calls the real callback, then signals */
static void _emotion_hs_dispatch(void *data) {
  emotion_handshake *hs=(emotion_handshake *)data;
  hs->cb(hs->data);
  pthread_mutex_lock(&hs->mtx);
  hs->done=1;
  pthread_cond_signal(&hs->cond);
  pthread_mutex_unlock(&hs->mtx); }

/* run cb(data) in the GUI main thread and block until it finished */
static void emotion_in_main(void (*cb)(void *),void *data) {
  emotion_handshake hs;

  if(pthread_equal(pthread_self(),main_thread)) {
    cb(data);
    return; }

  memset(&hs,0,sizeof(hs));
  hs.cb=cb;
  hs.data=data;
  pthread_mutex_init(&hs.mtx,NULL);
  pthread_cond_init(&hs.cond,NULL);

  ecore_main_loop_thread_safe_call_async(_emotion_hs_dispatch,&hs);

  pthread_mutex_lock(&hs.mtx);
  while(!hs.done)
    pthread_cond_wait(&hs.cond,&hs.mtx);
  pthread_mutex_unlock(&hs.mtx);
  pthread_mutex_destroy(&hs.mtx);
  pthread_cond_destroy(&hs.cond); }

#endif /* HAVE_EVAS2 */




#if defined(HAVE_EVAS2) && defined(HAVE_IMLIB2)

/* context for the static thumbnail, filled in the main thread */
typedef struct {
  const char  *file;
  Imlib_Image  im;
} emotion_make_ctx;

static void _emotion_make_in_main(void *data) {
  emotion_make_ctx *c=(emotion_make_ctx *)data;
  Evas             *evas=NULL;
  Evas_Object      *em=NULL;
  const char       *file=c->file;
  double            len=0.0;
  int               tries=0;

  /* a private buffer canvas hosts the emotion object */
  if(!(evas=evas_new()))
    return;
  if(evas_render_method_lookup("buffer"))
    evas_output_method_set(evas,evas_render_method_lookup("buffer"));
  evas_output_size_set(evas,16,16);
  evas_output_viewport_set(evas,0,0,16,16);

  if(!(em=emotion_object_add(evas)))
    goto done;
  /* NULL module: use whatever backend is installed (gstreamer/xine) */
  if(!emotion_object_init(em,NULL)) {
    E(0,"couldn't init emotion (no gstreamer/xine backend?) for \"%s\"...\n",file);
    goto done; }

  emotion_object_file_set(em,file);

  /* emotion loads asynchronously; poll the length until it is known */
  while(tries++<MAX_TRIES) {
    len=emotion_object_play_length_get(em);
    if(len>0.0)
      break;
    usleep(100000); }

  if(len<=0.0) {
    E(1,"no video in \"%s\"?\n",file);
    goto done; }

  /* seek into the movie: trailers/logos are usually in the first frames */
  emotion_object_position_set(em,len*SEEK_NUMERATOR/SEEK_DENOMINATOR);

  {
    /* the emotion smart object hosts an internal image with the frame */
    Evas_Object   *frame=emotion_object_image_get(em);
    unsigned char *frame_data;
    int            iw=0,ih=0;

    if(!frame) {
      E(1,"couldn't get frame image from \"%s\"...\n",file);
      goto done; }

    evas_object_image_size_get(frame,&iw,&ih);
    frame_data=(unsigned char *)evas_object_image_data_get(frame,0);
    if(frame_data&&iw>0&&ih>0) {
      Imlib_Image wrap=imlib_create_image_using_data(iw,ih,(DATA32 *)frame_data);
      if(wrap) {
        imlib_context_set_image(wrap);
        /* copy: the canvas (and with it frame_data) goes away below */
        c->im=imlib_create_cropped_image(0,0,iw,ih);
        imlib_free_image_and_decache(); } }
    else
      E(1,"couldn't get frame data from \"%s\" (%dx%d)...\n",file,iw,ih); }

 done:
  if(em)
    evas_object_del(em);
  if(evas)
    evas_free(evas); }

#endif /* HAVE_EVAS2 && HAVE_IMLIB2 */



/******************************************************************************/

#ifdef HAVE_EVAS2

static void _live_frame(void *data,Evas_Object *o,void *einfo) {
  Evas *evas=(Evas *)data;
  (void)o; (void)einfo;
  if(evas)
    evas_render(evas); }

/* context for the live object, filled in the main thread */
typedef struct {
  Evas        *evas;
  const char  *file;
  int          w,h;
  Evas_Object *result;
} emotion_live_ctx;

static void _emotion_live_in_main(void *data) {
  emotion_live_ctx *c=(emotion_live_ctx *)data;
  Evas_Object *o;

  if((o=emotion_object_add(c->evas))&&emotion_object_init(o,NULL)) {
    int w,h;

    evas_object_smart_callback_add(o,"frame_decode",_live_frame,c->evas);
    evas_object_move(o,0,0);
    evas_object_resize(o,320,240);
    emotion_object_smooth_scale_set(o,1);
    evas_object_show(o);
    emotion_object_file_set(o,c->file);
    emotion_object_play_set(o,1);

    emotion_object_size_get(o,&w,&h);
    evas_object_resize(o,c->w,c->h);  /* fill the icon */
    E(1,"now playing \"%s\" (%dx%d)\n",c->file,w,h);
    c->result=o; }
  else {
    if(o)
      evas_object_del(o);   /* created, but init failed */
    E(0,"couldn't create live object for movie \"%s\"...\n",c->file); } }

#endif /* HAVE_EVAS2 */

/******************************************************************************/



/**
 * initialize the thumbnailer-plugin and register what we can handle
 * @return a bitmask of capabilities (TP_*)
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_init(thumbnail_plugin *tp) {
  static int disclaimer=0;

  /* what we handle.  emotion delegates to whatever backend is installed
     (gstreamer by default, xine optionally), so register the common
     video containers. */
  thumbnail_plugin_register_mime(tp,"video/*",TP_PRIO_MID,TP_ICONS);
  thumbnail_plugin_register_mime(tp,"video/*",TP_PRIO_MID,TP_LIVE);
  thumbnail_plugin_register_mime(tp,"video/mp4",TP_PRIO_MID,TP_LIVE);
  thumbnail_plugin_register_mime(tp,"video/mpeg",TP_PRIO_MID,TP_LIVE);
  thumbnail_plugin_register_mime(tp,"video/x-matroska",TP_PRIO_MID,TP_LIVE);
  thumbnail_plugin_register_mime(tp,"video/quicktime",TP_PRIO_MID,TP_LIVE);
  thumbnail_plugin_register_mime(tp,"video/webm",TP_PRIO_MID,TP_LIVE);
  thumbnail_plugin_register_mime(tp,"video/ogg",TP_PRIO_MID,TP_LIVE);
  thumbnail_plugin_register_mime(tp,"video/x-msvideo",TP_PRIO_MID,TP_LIVE);

  if(!disclaimer) {
    disclaimer++;
    E(1,"\n*** engaged Emotion movie thumbnailer. this may use a lot of RAM and CPU;\n"
         "*** if you prefer a 'light' file-manager, you may want to disable this...\n\n"); }

  return (TP_IMAGE|TP_VIDEO
#ifdef HAVE_IMLIB2
          |TP_IMLIB
#endif
          ); }



/**
 * deinitialize the thumbnailer-plugin and free any resources it may hold
 * @return an error-code
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_exit(thumbnail_plugin *tp) {
  (void)tp;
  return NOIR_SUCC; }



#ifdef HAVE_IMLIB2
/**
 * create a static thumbnail image of a movie
 * @return an error-code
 * @param tp   an opaque thumbnailer-plugin struct
 * @param e    the thumbnail struct describing what to create
 * @param data returns an Imlib-image (for TP_IMLIB)
 */
int thumbnail_plugin_make(thumbnail_plugin *tp,struct _Evidence_Thumb *e,void **data) {
#  ifdef HAVE_EVAS2
  emotion_make_ctx c;

  (void)tp;

  if(!e->origfile||!data)
    return NOIR_NODATA;

  memset(&c,0,sizeof(c));
  c.file=e->origfile;

  emotion_in_main(_emotion_make_in_main,&c);

  if(!c.im) {
    E(1,"couldn't create thumbnail of \"%s\"...\n",e->origfile);
    return NOIR_FAIL; }

  *data=c.im;
  return NOIR_SUCC;
#  else
  /* no Evas: no live canvas to grab a frame from */
  (void)tp; (void)e; (void)data;
  return NOIR_NOTSUPP;
#  endif
  }
#endif /* HAVE_IMLIB2 */



#ifdef HAVE_EVAS2
/**
 * create a live Evas object (video playing "inside" the icon)
 * @return NOIR_SUCC for success
 * @param tp    an opaque thumbnailer-plugin struct
 * @param e     the thumbnail struct describing what to create
 * @param what  TP_VIDEO and/or TP_AUDIO
 */
int thumbnail_plugin_live(thumbnail_plugin *tp,struct _Evidence_Thumb *e,thumbnail_plugin_capabilities what) {
  emotion_live_ctx c;

  (void)tp;

  if(!(what&TP_VIDEO))
    return NOIR_NOTSUPP;

  memset(&c,0,sizeof(c));
  c.evas=(Evas *)e->data;
  c.file=e->origfile;
  c.w=e->w;
  c.h=e->h;

  emotion_in_main(_emotion_live_in_main,&c);

  e->data=c.result;
  return c.result?NOIR_SUCC:NOIR_FAIL; }
#endif /* HAVE_EVAS2 */
