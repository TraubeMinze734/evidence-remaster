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
#include "imlib.h"           /* IMLIB_ACQUIRE/RELEASE (imlib_mutex) */

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
#define EMOTION_OPEN_TIMEOUT 10.0 /* seconds to wait for emotion to open a file */

/******************************************************************************/

#ifdef HAVE_EVAS2

typedef struct _emotion_param {
  int          dummy;
} emotion_param;

/* the GUI thread: evidence sets main_thread in main() before the
   thumbnailer workers are ever started; the plugin's init() may run in
   a worker, so we cannot remember the identity ourselves.
   evidence's thumbnailer 'protected mode' fork()s a task process; in
   that child there is no mainloop to dispatch to, so we run inline
   there (the child has its private copy of the world and only produces
   an Imlib image; the live path is never requested from the child). */
static pid_t emotion_parent_pid;



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

  if(pthread_equal(pthread_self(),main_thread)||
     (getpid()!=emotion_parent_pid)) {   /* fork()ed thumbnailer task */
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

/* context for the static thumbnail.  the object is created in the main
   thread, but the frame is not grabbed synchronously:  emotion needs the
   ecore mainloop to iterate to make progress (it reports the stream length
   via smart callbacks), so a blocking poll inside the dispatched callback can
   never observe the result -- which is why the old code always failed with
   "no video in ...".  we now drive the whole thing from an ecore_timer in the
   main loop; the thumbnailer worker just waits on a condvar until the
   machine is done (success or timeout). */
typedef struct {
  const char    *file;
  unsigned char *frame;      /* plain copy of the frame, or NULL */
  int            iw,ih;
  Ecore_Timer   *timer;
  Evas          *evas;
  Evas_Object   *em;
  double         deadline;   /* absolute time when we give up */
  int            seek_set;
  pthread_mutex_t mtx;
  pthread_cond_t  cond;
  int             done;
} emotion_make_ctx;

static void _emotion_make_finish(emotion_make_ctx *c) {
  if(c->timer) {
    ecore_timer_del(c->timer);
    c->timer=NULL; }
  if(c->em) {
    /* stop the pipeline before tearing down, or GStreamer keeps pushing
       frames into an object that is about to go away (EO errors) */
    emotion_object_play_set(c->em,0);
    emotion_object_file_set(c->em,NULL);
    evas_object_del(c->em);
    c->em=NULL; }
  if(c->evas) {
    evas_free(c->evas);
    c->evas=NULL; }
  pthread_mutex_lock(&c->mtx);
  c->done=1;
  pthread_cond_signal(&c->cond);
  pthread_mutex_unlock(&c->mtx); }

static Eina_Bool _emotion_make_timer(void *data) {
  emotion_make_ctx *c=(emotion_make_ctx *)data;
  double len=emotion_object_play_length_get(c->em);
  if(len<=0.0) {
    /* still opening: give the mainloop time to breathe */
    if(ecore_time_get()<c->deadline)
      return ECORE_CALLBACK_RENEW;
    E(1,"no video in \"%s\"? (open timed out)\n",c->file);
    goto done; }
  if(!c->seek_set) {
    /* seek into the movie: trailers/logos are usually in the first frames */
    emotion_object_position_set(c->em,len*SEEK_NUMERATOR/SEEK_DENOMINATOR);
    c->seek_set=1;
    if(ecore_time_get()<c->deadline)
      return ECORE_CALLBACK_RENEW;
    /* no time left to wait for the seeked frame; fall through and grab
       whatever the object currently holds */ }
  {
    /* the emotion smart object hosts an internal image with the frame */
    Evas_Object   *frame=emotion_object_image_get(c->em);
    unsigned char *frame_data;
    int            iw=0,ih=0;
    if(!frame) {
      E(1,"couldn't get frame image from \"%s\"...\n",c->file);
      goto done; }
    evas_object_image_size_get(frame,&iw,&ih);
    frame_data=(unsigned char *)evas_object_image_data_get(frame,0);
    if(frame_data&&iw>0&&ih>0) {
      /* copy the pixels out -- do NOT touch imlib here:  this runs in the
         GUI thread, while the thumbnailer worker holds the imlib mutex
         (IMLIB_ACQUIRE around tp->make()); imlib use from here corrupts
         imlib's context (NULL-image warnings, double free).  the worker
         wraps the copy in an Imlib image after we hand it over. */
      size_t sz=(size_t)iw*ih*4;
      if((c->frame=malloc(sz)))
        memcpy(c->frame,frame_data,sz);
      c->iw=iw; c->ih=ih; }
    else {
      /* frame not decoded yet (e.g. right after the seek): keep waiting
         until the deadline rather than failing immediately */
      if(ecore_time_get()<c->deadline)
        return ECORE_CALLBACK_RENEW;
      E(1,"couldn't get frame data from \"%s\" (%dx%d)...\n",c->file,iw,ih);
      goto done; } }
 done:
  _emotion_make_finish(c);
  return ECORE_CALLBACK_CANCEL; }

static void _emotion_make_in_main(void *data) {
  emotion_make_ctx *c=(emotion_make_ctx *)data;
  /* a private buffer canvas hosts the emotion object */
  if(!(c->evas=evas_new()))
    goto done;
  if(evas_render_method_lookup("buffer"))
    evas_output_method_set(c->evas,evas_render_method_lookup("buffer"));
  evas_output_size_set(c->evas,16,16);
  evas_output_viewport_set(c->evas,0,0,16,16);
  if(!(c->em=emotion_object_add(c->evas)))
    goto done;
  /* NULL module: use whatever backend is installed (gstreamer/xine) */
  if(!emotion_object_init(c->em,NULL)) {
    E(0,"couldn't init emotion (no gstreamer/xine backend?) for \"%s\"...\n",c->file);
    goto done; }
  /* grace period: slow disks and big files need far more than the old 0.8s
     of usleep()ing; the timer keeps the GUI responsive throughout */
  c->deadline=ecore_time_get()+EMOTION_OPEN_TIMEOUT;
  emotion_object_file_set(c->em,c->file);
  if((c->timer=ecore_timer_add(0.1,_emotion_make_timer,c)))
    return;
  /* no timers available!?  fall back to giving up */
 done:
  _emotion_make_finish(c); }

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

/* the tooltip engine deletes its elements when the tooltip closes; stop
   the pipeline before the object is freed, or GStreamer keeps pushing
   frames to a sink whose object is gone ('LOCKED') and trashes state */
static void _emotion_live_del_cb(void *data,Evas *e,Evas_Object *o,void *einfo) {
  (void)data; (void)e; (void)einfo;
  /* synchronous teardown: pause, then unload (closes the engine
     instance and drops the image data).  emotion_object_file_set(NULL)
     only *starts* an asynchronous close -- racing the imminent free */
  emotion_object_play_set(o,0);
  efl_file_unload(o); }

static void _emotion_live_in_main(void *data) {
  emotion_live_ctx *c=(emotion_live_ctx *)data;
  Evas_Object *o;

  if((o=emotion_object_add(c->evas))&&emotion_object_init(o,NULL)) {
    int w,h;

    evas_object_smart_callback_add(o,"frame_decode",_live_frame,c->evas);
    evas_object_event_callback_add(o,EVAS_CALLBACK_DEL,_emotion_live_del_cb,NULL);
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

#ifdef HAVE_EVAS2
  emotion_parent_pid=getpid();
  /* hold a reference for the life of the plugin: every video object
     deletion calls emotion_shutdown() (refcount--); without our +1
     the count reaches zero with the first deleted object and GStreamer
     is deinitialized while the app keeps running -- 'playbin' creation
     fails from then on, and the final gst_deinit() asserts in glib */
  emotion_init();
#endif

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
#ifdef HAVE_EVAS2
  emotion_shutdown();   /* drop the reference taken in init() */
#endif
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

  pthread_mutex_init(&c.mtx,NULL);
  pthread_cond_init(&c.cond,NULL);
  /* _emotion_make_in_main only starts the machine; the worker waits here
     until the main-loop timer finishes (frame grabbed or timed out).  the
     mainloop stays responsive the whole time, and the timeout bounds the
     wait so a broken file can never wedge the thumbnailer thread. */

  /* our caller (thumbnailer_generate) holds the imlib mutex while we
     run.  the GUI thread needs that very mutex for its own imlib work
     (theme engines like ix_rephormed, loading thumbnails), so waiting
     for the GUI thread WITH the mutex held is a guaranteed ABBA
     deadlock:  GUI waits for the mutex, we wait for the GUI.  drop the
     mutex for the duration of the handshake and re-acquire it after;
     the frame data is a plain malloc() copy, no imlib state is touched
     while we don't hold the lock. */
  IMLIB_RELEASE;
  emotion_in_main(_emotion_make_in_main,&c);
  pthread_mutex_lock(&c.mtx);
  while(!c.done)
    pthread_cond_wait(&c.cond,&c.mtx);
  pthread_mutex_unlock(&c.mtx);
  IMLIB_ACQUIRE;
  pthread_mutex_destroy(&c.mtx);
  pthread_cond_destroy(&c.cond);


  if(!c.frame) {
    E(1,"couldn't create thumbnail of \"%s\"...\n",e->origfile);
    return NOIR_FAIL; }
  /* build an imlib image from the copied pixels.  we run in the worker
     thread, which already holds the imlib mutex, so imlib use is safe
     here.  we do NOT use imlib_create_image_using_data() + free():  the
     wrapper does not own the pixels, and the crop/free/decache dance
     around it confused imlib's ownership bookkeeping and kept crashing
     with double free.  imlib_create_image() allocates an image imlib
     owns outright; we copy our frame in and are done.  (Evas delivers
     premultiplied BGRA, which is exactly the byte order imlib uses
     internally, so a plain memcpy is also correct.) */
  {
    Imlib_Image im=imlib_create_image(c.iw,c.ih);
    if(im) {
      imlib_context_set_image(im);
      memcpy(imlib_image_get_data(),c.frame,(size_t)c.iw*c.ih*4);
      imlib_image_set_has_alpha(1); }
    free(c.frame);
    if(!im) {
      E(1,"couldn't create thumbnail of \"%s\"...\n",e->origfile);
      return NOIR_FAIL; }
    *data=im; }
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
