/** @file  emotion.c
 *  @brief Emotion movie thumbnailer (live + static), using modern EFL's
 *         system libemotion (which delegates to gstreamer/xine backends).
 *
 *  replaces the old xine.c thumbnailer, which required the 2005 libxine API
 *  and bundled a private copy of the Emotion smart object (emotion.h here
 *  is a left-over of that and is no longer used).
 *
 *  static thumbnails: seek into the stream, grab the frame via
 *                     emotion_object_image_get(), hand it to Imlib2.
 *  live thumbnails:   the emotion object itself is an Evas smart object;
 *                     we just hand it to the icon-view and play it.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "errors.h"
#include "util.h"            /* esprintf() */

#include "../thumbnail_private.h"
#include "../thumbnail_plugin.h"

#ifdef HAVE_EVAS2
#  include <Evas.h>
#  include <Emotion.h>
#  ifdef HAVE_IMLIB2
#    include <Imlib2.h>
#  endif
#endif

#define SEEK_NUMERATOR   8   /* take a frame from ~ 8% into the movie */
#define SEEK_DENOMINATOR 10
#define MAX_TRIES         8  /* how often to retry after an open error */

/******************************************************************************/

#ifdef HAVE_EVAS2

typedef struct _emotion_param {
  int inited;
} emotion_param;



static void _live_frame(void *data,Evas_Object *o,void *einfo) {
  Evas *evas=(Evas *)data;
  (void)o; (void)einfo;
  if(evas)
    evas_render(evas); }

/******************************************************************************/

/**
 * initialize the thumbnailer-plugin and register what we can handle
 * @return a bitmask of capabilities (TP_*)
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_init(thumbnail_plugin *tp) {
  emotion_param *param;
  static int     disclaimer=0;

  if(!(param=malloc(sizeof(emotion_param))))
    return NOIR_FAIL;
  memset(param,0,sizeof(emotion_param));

  /* emotion_init() is deprecated since EFL 1.21 -- the first
     emotion_object_add() initializes the library implicitly. */
  param->inited=1;

  thumbnail_plugin_data_set(tp,(void *)param);

  /* what we handle.  emotion delegates to whatever backend is installed
     (gstreamer by default, xine optionally), so we register the common
     video containers. */
  thumbnail_plugin_register_mime(tp,"video/*",TP_PRIO_MID,TP_ICONS);
  thumbnail_plugin_register_mime(tp,"video/*",TP_PRIO_MID,TP_LIVE);
  thumbnail_plugin_register_ext(tp,"avi",TP_PRIO_MID,TP_ICONS);
  thumbnail_plugin_register_ext(tp,"mpg",TP_PRIO_MID,TP_ICONS);
  thumbnail_plugin_register_ext(tp,"mpeg",TP_PRIO_MID,TP_ICONS);
  thumbnail_plugin_register_ext(tp,"mp4",TP_PRIO_MID,TP_ICONS);
  thumbnail_plugin_register_ext(tp,"mkv",TP_PRIO_MID,TP_ICONS);
  thumbnail_plugin_register_ext(tp,"mov",TP_PRIO_MID,TP_ICONS);
  thumbnail_plugin_register_ext(tp,"webm",TP_PRIO_MID,TP_ICONS);
  thumbnail_plugin_register_ext(tp,"ogv",TP_PRIO_MID,TP_ICONS);

  if(!disclaimer) {
    disclaimer++;
    E(1,"\n*** engaged Emotion movie thumbnailer. this may use a lot of RAM and CPU;\n"
         "*** if you prefer a 'light' file-manager, you may want to disable this...\n\n"); }

  return (TP_IMAGE|TP_VIDEO|(TP_IMLIB)
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
  emotion_param *param=NULL;
  void          *v;

  if((thumbnail_plugin_data_get(tp,&v)==NOIR_SUCC)&&v) {
    param=(emotion_param *)v;
    free(param);
    thumbnail_plugin_data_set(tp,NULL); }

  return NOIR_SUCC; }



#ifndef HAVE_IMLIB2
/* no Imlib2: we cannot produce static thumbnails, but live video still
   works.  the plugin API requires a make() function, so provide a stub. */
int thumbnail_plugin_make(thumbnail_plugin *tp,struct _Evidence_Thumb *e,void **data) {
  (void)tp; (void)e; (void)data;
  return NOIR_NOTSUPP; }
#else
/**
 * create a static thumbnail image of a movie
 * @return an error-code
 * @param tp   an opaque thumbnailer-plugin struct
 * @param e    the thumbnail struct describing what to create
 * @param data returns an Imlib-image (for TP_IMLIB)
 */
int thumbnail_plugin_make(thumbnail_plugin *tp,struct _Evidence_Thumb *e,void **data) {
  Evas        *evas=NULL;
  Evas_Object *em=NULL;
  const char  *file=e->origfile;
  Imlib_Image  im=NULL;
  double       len;
  int          w=0,h=0,tries=0;

  (void)tp;

  if(!file||!data)
    return NOIR_NODATA;

  /* we need a canvas to host the emotion object.  the thumbnailer is
     threaded, so we create a private software-buffer canvas. */
  if(!(evas=evas_new()))
    return NOIR_FAIL;
  if(evas_render_method_lookup("buffer"))
    evas_output_method_set(evas,evas_render_method_lookup("buffer"));
  evas_output_size_set(evas,16,16);
  evas_output_viewport_set(evas,0,0,16,16);

  if(!(em=emotion_object_add(evas))) {
    E(0,"couldn't create emotion object for \"%s\"...\n",file);
    evas_free(evas);
    return NOIR_FAIL; }

  /* NULL module: use whatever backend is installed (gstreamer/xine) */
  if(!emotion_object_init(em,NULL)) {
    E(0,"couldn't init emotion (no gstreamer/xine backend?) for \"%s\"...\n",file);
    evas_object_del(em);
    evas_free(evas);
    return NOIR_FAIL; }

  emotion_object_file_set(em,file);

  while(tries++<MAX_TRIES) {
    if(emotion_object_ready(em)||(emotion_object_file_get(em)&&emotion_object_play_length_get(em)>0.0))
      break;
    /* not ready yet; emotion needs the main-loop.  we can't spin one
       here, so rely on the synchronous open.  sleep a little. */
    usleep(100000); }

  len=emotion_object_play_length_get(em);
  if(len<=0.0) {
    E(1,"no video in \"%s\"?\n",file);
    evas_object_del(em);
    evas_free(evas);
    return NOIR_FAIL; }

  /* seek into the movie: trailers/logos are usually in the first frames */
  emotion_object_position_set(em,len*SEEK_NUMERATOR/SEEK_DENOMINATOR);

  emotion_object_size_get(em,&w,&h);
  if((w<1)||(h<1)) {
    E(1,"couldn't determine size of \"%s\"...\n",file);
    evas_object_del(em);
    evas_free(evas);
    return NOIR_FAIL; }

  {
    /* the emotion smart object hosts an internal image that holds the
       current frame (emotion_object_image_get() returns it) */
    Evas_Object   *frame=emotion_object_image_get(em);
    unsigned char *frame_data;
    int            iw=0,ih=0;

    if(!frame) {
      E(1,"couldn't get frame image from \"%s\"...\n",file);
      evas_object_del(em);
      evas_free(evas);
      return NOIR_FAIL; }

    evas_object_image_size_get(frame,&iw,&ih);
    frame_data=(unsigned char *)evas_object_image_data_get(frame,0);
    if(frame_data&&iw>0&&ih>0) {
      /* evas gives premultiplied ARGB in native endianness; imlib
         wants ARGB in host byte order as well. */
      im=imlib_create_image_using_data(iw,ih,(DATA32 *)frame_data);
      if(im) {
        imlib_context_set_image(im);
        imlib_image_set_has_alpha(0); } }
    else
      E(1,"couldn't get frame data from \"%s\" (%dx%d)...\n",file,iw,ih); }

  evas_object_del(em);
  evas_free(evas);

  if(!im) {
    E(1,"couldn't create thumbnail of \"%s\"...\n",file);
    return NOIR_FAIL; }

  *data=im;
  return NOIR_SUCC; }
#endif /* HAVE_IMLIB2 */



/**
 * create a live Evas object (video playing "inside" the icon)
 * @return NOIR_SUCC for success
 * @param tp    an opaque thumbnailer-plugin struct
 * @param e     the thumbnail struct describing what to create
 * @param what  TP_VIDEO and/or TP_AUDIO
 */
int thumbnail_plugin_live(thumbnail_plugin *tp,struct _Evidence_Thumb *e,thumbnail_plugin_capabilities what) {
  Evas_Object *o;
  (void)tp;

  if(!(what&TP_VIDEO))
    return NOIR_NOTSUPP;

  if((o=emotion_object_add((Evas *)e->data))&&emotion_object_init(o,NULL)) {
    int w,h;

    evas_object_smart_callback_add(o,"frame_decode",_live_frame,e->data);
    evas_object_move(o,0,0);
    evas_object_resize(o,320,240);
    emotion_object_smooth_scale_set(o,1);
    evas_object_show(o);
    emotion_object_file_set(o,e->origfile);
    emotion_object_play_set(o,1);

    emotion_object_size_get(o,&w,&h);
    evas_object_resize(o,e->w,e->h);  /* fill the icon */
    E(1,"now playing \"%s\" (%dx%d)\n",e->origfile,w,h); }
  else {
    if(o)
      evas_object_del(o);   /* created, but init failed */
    E(0,"couldn't create live object for movie \"%s\"...\n",e->origfile); }

  return (e->data=o)?NOIR_SUCC:NOIR_FAIL; }

#endif /* HAVE_EVAS2 */
