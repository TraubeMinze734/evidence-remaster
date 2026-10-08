/** @file  xine.c
 *  @brief xine movie thumbnailer
 *
 * if libxine is there, it will be used for static thumbnails of movie files.
 * if libemotion is also there, it will be used to generate live thumbnails.
 */

#include "config.h"
#include "debug.h"
#include "errors.h"
#include "util.h"            /* esprintf() */

#include <stdlib.h>          /* malloc(), free() */

#include <stdio.h>
#include <string.h>

#include <xine.h>
#include <xine/xineutils.h>  /* xine_get_homedir() */

#include "../thumbnail_private.h"
#include "../thumbnail_plugin.h"

#ifdef HAVE_EVAS2
#  include <Evas.h>
#endif

#include <dlfcn.h>



#define TILESIZE (EVIDENCE_THUMB_BASE*2)



static int disclaimer=0;



/*****************************************************************************/



#ifdef HAVE_EVAS2

Evas_Object *emotion_object_add(Evas *evas);
double       emotion_object_position_get(Evas_Object *obj);
double       emotion_object_play_length_get(Evas_Object *obj);
void         emotion_object_file_set(Evas_Object *obj,const char *file);
void         emotion_object_size_get(Evas_Object *obj,int *iw,int *ih);
void         emotion_object_smooth_scale_set(Evas_Object *obj,Evas_Bool smooth);
void         emotion_object_play_set(Evas_Object *obj,Evas_Bool play);
Evas_Bool    emotion_object_play_get(Evas_Object *obj);

#endif



typedef struct _thumbnailer_xine_param {
  xine_t              *xine;
  xine_stream_t       *stream;
  xine_video_port_t   *vo_port;
  xine_audio_port_t   *ao_port;
} thumbnailer_xine_param;






/*****************************************************************************/



static inline int clip(int val) {
  if(val<0)
    return 0;
  return (val>255)?255:val; }



/**
 * create RGB data from YV12
 * @return an error-code (NOIR_SUCC: all is well, NOIR_PARTIAL: dubious contrast, anything else: no thumbs for you, you come back one year!)
 * @param name  a label (for debug info only)
 * @param w     width  of the image
 * @param h     height of the image
 * @param buf   YUV data of the image
 * @param rp    a pointer to valid imlib-image or NULL will be store in *rp
 */
static inline int yv12_to_rgb(const char *name,int w,int h,DATA8 **buf,Imlib_Image *rp) {
  DATA8      *_y=*buf,              /* video-data (Y, base) */
             *_u=_y+(w*h),          /* video-data (U, base) */
             *_v=_y+(w*h)+(w*h)/4;  /* video-data (V, base) */
  DATA8      *_rgb,                 /* video-data (RGB, base) */
             *__rgb;                /* video-data (RGB, current row) */

  int         hw=((w+1)/2),
              hh=((h+1)/2);

  int         i,j;                  /* indices */

  int         y,u,v;                /* video-data (YUV) */
  int         r,g,b;                /* video-data (RGB) */

  int         io,jo;                /* offsets */

  long        sx,sy=0;              /* sums */
  int         ymin=255,ymax=0,yt;   /* delta */

  int         ret=NOIR_IGNORED;

  Imlib_Image dst;                  /* destination image (ARGB) */

  if(rp)
    *rp=NULL;

  if(!(dst=imlib_create_image(w,h)))
    return NOIR_OOM;

  imlib_context_set_image(dst);
  imlib_image_set_format("argb");
  imlib_image_set_has_alpha(1);

  if(!(_rgb=(DATA8*)imlib_image_get_data())) {
    imlib_free_image();
    return NOIR_NODATA; }

#define BPP 4

  __rgb=_rgb;

  for(i=0;i<h;++i) {
    io=((i*hh)/h);
    sx=0;

    for(j=0;j<w;++j) {
      jo=((j*hw)/w)+(io*hw);

      y=_y[(i*w)+j]-16;
      u=_u[jo]-128;
      v=_v[jo]-128;

      yt=_y[(i*w)+j];
      if(yt>ymax) ymax=yt;
      if(yt<ymin) ymin=yt;

      sx+=yt;

      r=((1.1644*y)           +(1.5960*v));
      g=((1.1644*y)-(0.3918*u)-(0.8130*v));
      b=((1.1644*y)+(2.0172*u));

      __rgb[(j*BPP)+0]=clip(b);
      __rgb[(j*BPP)+1]=clip(g);
      __rgb[(j*BPP)+2]=clip(r);
      __rgb[(j*BPP)+3]=255; }

    sy+=(sx/w);

    __rgb+=(w*BPP); }

  sy=(sy/h);

  if((sy<32)||(sy>223)) {
    E(2,"wonky contrast in \"%s\"?\n",name);
    ret=NOIR_PARTIAL; }
  else
    ret=NOIR_SUCC;

  E((ret==NOIR_SUCC)?3:2,"%s: %d<%ld<%d, d: %d\n",name,ymin,sy,ymax,ymax-ymin);

  free(*buf);
  *buf=NULL;

  imlib_image_put_back_data((DATA32*)_rgb);

  if(rp)
    *rp=dst;

  return ret; }



static inline float pti(int ti) {
  if(ti<0) return 0;
  return (ti/60000.0); }





/*****************************************************************************/

/**
 * thumbnail a movie (using libxine).
 *
 * called from thumbnail_generate() only.
 * @return an error-code
 * @param e     - the thumbnail struct
 * @param param - TLS for this xine-thumbnailer
 * @param r     - store error-code here (or NULL)
 */
static inline Imlib_Image make(struct _Evidence_Thumb *e,thumbnailer_xine_param *param,int *r) {
  int            ret=NOIR_IGNORED;
  char          *p;
  Imlib_Image    img=NULL;
  DATA8         *buf=NULL;
  int            cnt,attempts=0,
                 length=-1,
                 pos_perc,pos_time,
                 new_perc,new_time,
                 req_perc=24000,req_time=1000*300;
  static int     old_length=-1;

  if(r)
    *r=NOIR_FAIL;

  /* something went wrong in init, we shouldn't even be here */
  if(!param||!param->xine) {
    if(r)
      *r=NOIR_NODATA;
    return NULL; }

  /* opening xine output ports */
  if(!(param->vo_port=xine_open_video_driver(param->xine,"none",XINE_VISUAL_TYPE_NONE,(void *)NULL))) {
    E(0,"failed to open video driver...\n");
    ret=NOIR_FAIL;
    goto done; }

  param->ao_port=xine_open_audio_driver(param->xine,"none",NULL);

  /* open a xine stream connected to these ports */
  if(!(param->stream=xine_stream_new(param->xine,param->ao_port,param->vo_port))) {
    E(0,"failed to create stream...\n");
    ret=NOIR_FAIL;
    goto done; }

  if(!(p=evidence_thumbnail_path(e->size,e->hashedname,EXT_PNG))) {
    ret=NOIR_OOM;
    goto done; }

  if(e->thumbfile)
    free(e->thumbfile);
  e->thumbfile=p;

  if(!xine_open(param->stream,e->origfile)) {
    E(0,"Unable to open MRL \"%s\"...\n",e->origfile);
    ret=NOIR_FAIL;
    goto done; }

 try_get_chance:
  new_perc=-1;
  new_time=req_time;
  if(!xine_play(param->stream,0,new_time)) {
    new_time=-1;
    new_perc=req_perc; /* 0..65535 */
    if(!xine_play(param->stream,new_perc,0)) {
      E(0,"Unable to play \"%s\"...\n",e->origfile);
      ret=NOIR_FAIL;
      goto close_stream; }}

  /* get position */
  for(cnt=0;((cnt<10)&&
             (!xine_get_pos_length(param->stream,&pos_perc,&pos_time,&length)||
              (length==old_length)));cnt++)
    thread_sleep(0.05);

  if(length<0) {
    E(1,"couldn't determine length of \"%s\"!?\n",e->origfile);
    ret=NOIR_FAIL;
    goto close_stream; }

  /* wait till position changes */
  for(cnt=0;
      (cnt<10)&&
        (!xine_get_pos_length(param->stream,&new_perc,&new_time,&length)||
         ((new_perc==pos_perc)&&(new_time==pos_time)));
      cnt++) {
    thread_sleep(0.2); }

  /* debug info */
  if(debug>1) {
    E(0,"%d ms  (%d)  %-40.40s\n",length,cnt,(p=strrchr(e->origfile,'/'))?p+1:e->origfile);
    E(0,"len %d/%d, %% %d/%d, time %5.2f/%5.2f\n\n",
      length,old_length,
      pos_perc,new_perc,
      pti(pos_time),pti(new_time)); }

  /* get snapshot */

  { int w,h,ratio,format;

    old_length=length;

    /* ask what size a snapshot would be (if we can make one) */
    if(!xine_get_current_frame(param->stream,&w,&h,&ratio,&format,NULL)) {
      E(1,"snapshot failed for \"%s\"...\n",e->origfile);
      ret=NOIR_FAIL;
      goto close_stream; }
    else if((w<=0)||(h<=0)) {
      E(1,"snapshot ignored: %dx%d\n",w,h);
      ret=NOIR_NODATA;
      goto close_stream; }
    else {
      const char *fmt;
      char       *v;

      v=NULL;
      esprintf(&v,"%d",w);
      if(v) {
        thumbnail_attribute_add(e,"Thumb::Image::Width",v);
        free(v);
        v=NULL; }

      esprintf(&v,"%d",h);
      if(v) {
        thumbnail_attribute_add(e,"Thumb::Image::Height",v);
        free(v);
        v=NULL; }

      esprintf(&v,"%d",length);
      if(v) {
        thumbnail_attribute_add(e,"Thumb::Movie::Length",v);
        free(v);
        v=NULL; }

      /* know we know the size, get a buffer for the snapshot */
      if(!(buf=malloc(w*h*2))) {
        ret=NOIR_OOM;
        goto close_stream; }

      /* make a snapshot! */
      if(!xine_get_current_frame(param->stream,&w,&h,&ratio,&format,buf)) {
        E(1,"snapshot failed, no data!?\n");
        ret=NOIR_NODATA;
        goto close_stream; }

      else if((w<=0)||(h<=0)) {
        E(1,"snapshot failed, zero geometry!?  (%dx%d)\n",w,h);
        ret=NOIR_NODATA;
        goto close_stream; }

      switch(format) {
        case XINE_IMGFMT_YV12: fmt="YV12"; break;
        case XINE_IMGFMT_YUY2: fmt="YUY2"; break;
        case XINE_IMGFMT_XVMC: fmt="XVMC"; break;
        default: fmt="???"; }

      E(1,"snapshot: %04dx%04d %d %s \"%s\"\n",w,h,ratio,fmt,file_name(e->origfile));

      if(format==XINE_IMGFMT_YUY2) {
/*      if(!yuy2_to_yv12(buf)) { */
        if(1) {
          E(0,"YUY2 => YV12 failed\n");
          ret=NOIR_FAIL;
          goto close_stream; }
        else
          format=XINE_IMGFMT_YV12; }

      if(format==XINE_IMGFMT_YV12) {
        ret=yv12_to_rgb(e->origfile,w,h,&buf,&img);

        if(ret!=NOIR_SUCC) {
          if(img) {
            if(++attempts>4)
              goto close_stream;
            E(4,"not overly happy with this thumbnail, trying again at a different index into the movie (\"%s\")...\n",e->origfile);
            imlib_free_image();
            img=NULL;
            if(ret==NOIR_PARTIAL) {
              if(buf) {
                free(buf);
                buf=NULL;
                E(1,"buf was set where it really shouldn't be!?\n"); }
              if((req_perc+=(attempts*1000))>65535)
                req_perc=1000;
              if((req_time+=(attempts*2500))>length)
                req_time=3000;
              goto try_get_chance; }
            E(0,"wonky error-code %d!?\n",ret); }
          ret=NOIR_FAIL;
          goto close_stream; }}

      else {
        E(0,"unknown snapshot format, ignored\n");
        ret=NOIR_NOTSUPP;
        goto close_stream; }}}

  ret=NOIR_SUCC;

 close_stream:
  if(buf) {
    free(buf);
    buf=NULL; }
  xine_close(param->stream);

 done:
  if(ret==NOIR_PARTIAL)
    E(2,"still not overly happy with the thumbnail for \"%s\", gave up after %d attempts...\n",e->origfile,attempts);
  else if(ret!=NOIR_SUCC) {
    E(0,"failed to thumbnail \"%s\": #%d\n",e->origfile,ret);
    if(img) {
      E(0,"had to release broken img!?\n");
      imlib_free_image();
      img=NULL; }}
  else
    img=evidence_thumb_imlib_standardize(e);

  if(r)
    *r=ret;

  if(param->stream) {
    xine_dispose(param->stream);
    param->stream=NULL; }
  if(param->ao_port) {
    xine_close_audio_driver(param->xine,param->ao_port);
    param->ao_port=NULL; }
  if(param->vo_port) {
    xine_close_video_driver(param->xine,param->vo_port);
    param->vo_port=NULL; }

  return img; }





/*****************************************************************************/



static thumbnailer_xine_param *param_new(void) {
  thumbnailer_xine_param *param;
  char                   *cfg=NULL;

  if(!(param=malloc(sizeof(thumbnailer_xine_param))))
    return NULL;

  memset(param,0,(sizeof(thumbnailer_xine_param)));

  /* load xine config file and init xine */
  if(!(param->xine=xine_new())) {
    E(0,"xine_new() failed\n");
    free(param);
    return NULL; }

  esprintf(&cfg,"%s%s",xine_get_homedir(),"/.xine/config");
  if(cfg) {
    xine_config_load(param->xine,cfg);
    free(cfg); }
  else
    E(0,"OOM?\n");

  xine_init(param->xine);

  return param; }



/**
 * deinitialize the thumbnailer-plugin and free any resources it may hold
 * @return an error-code
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_exit(thumbnail_plugin *tp) {
  thumbnailer_xine_param *param=NULL;
  void                   *v;
  int                     ret;

  if((ret=thumbnail_plugin_data_get(tp,&v))!=NOIR_SUCC)
    return ret;

  if(!(param=v))
    return NOIR_IGNORED;

  if(param->xine) {
    xine_exit(param->xine);
    param->xine=NULL; }

  thumbnail_plugin_data_set(tp,NULL);

  free(param);

  return NOIR_SUCC; }



/**
 * initialize the thumbnailer-plugin and register it for the MIME-types and extensions it knows.
 * @return an error-code <0, or a set of features supported by the plugin
 * @param tp  an opaque thumbnailer-plugin struct
 */
int thumbnail_plugin_init(thumbnail_plugin *tp) {
  char                   *mime;
  int                     have_ecore=1;
  thumbnailer_xine_param *param;

  if(!(param=param_new()))
    return NOIR_FAIL;

  thumbnail_plugin_data_set(tp,(void *)param);

  if((mime=strdup(xine_get_mime_types(param->xine)))) {
    char *type=mime,*colon,*next;

    do {
      if((next=strchr(type,';')))
        next++;
      else
        next=type+strlen(type);
      if((colon=strchr(type,':'))&&(colon<next))
        *colon='\0';
      if(!strncmp(type,"video/",6)) {
        E(2,"registering \"%s\"\n",type);
        thumbnail_plugin_register_mime(tp,type,TP_PRIO_MID,TP_ICONS);
        thumbnail_plugin_register_mime(tp,type,TP_PRIO_MID,TP_LIVE); }
      else if(!strncmp(type,"audio/",6)) {
        E(2,"registering \"%s\"\n",type);
        thumbnail_plugin_register_mime(tp,type,TP_PRIO_LOW,TP_LIVE); }
      else
        E(2,"ignoring    \"%s\"\n",type);
      type=next;
    } while(*type);
    free(mime); }
  else {
    thumbnail_plugin_register_mime(tp,"video/*",TP_PRIO_MID,TP_ICONS);
    thumbnail_plugin_register_mime(tp,"video/*",TP_PRIO_MID,TP_LIVE); }

  if(!disclaimer) {
    disclaimer++;
    E(1,"\n*** engaged xine move thumbnailer. this uses a lot of RAM and CPU;\n*** if you prefer a 'light' file-manager, you may want to disable this...\n\n"); }

#ifdef HAVE_EVAS2
  return have_ecore?(TP_IMAGE|TP_IMLIB|TP_VIDEO|TP_AUDIO):(TP_IMAGE|TP_IMLIB);
#else
  return (TP_IMAGE|TP_IMLIB);
#endif
}



/**
 * create a thumbnail
 * @return an error-code
 * @param tp    an opaque thumbnailer-plugin struct
 * @param e     the struct describing the thumbnail
 * @param data  the imlib-image (if TP_IMLIB)
 */
int thumbnail_plugin_make(thumbnail_plugin *tp,struct _Evidence_Thumb *e,void **data) {
  int                     ret=NOIR_IGNORED;
  Imlib_Image             im=NULL;
  thumbnailer_xine_param *param=NULL;
  void                   *v;

  if((ret=thumbnail_plugin_data_get(tp,&v))!=NOIR_SUCC)
    return ret;

  param=v;

#if 0
  /* pessimistic approach: write failinfo in advance and delete it if we
     succeed, so if a rotten xine plugin crashes us, we won't try again. */
  { char                 *fail;

    thumbnail_failinfo_save(e->origfile,file_name(tp->file),&fail);

    im=make(e,param,&ret);
    if((*((Imlib_Image *)data)=im))
      thumbnail_failinfo_remove(fail);
    else
      free(fail); }

#else

  im=make(e,param,&ret);
  *((Imlib_Image *)data)=im;
#endif

  return im?NOIR_SUCC:NOIR_FAIL; }






/*****************************************************************************/



#ifdef HAVE_EVAS2

static void _live_done(void *data,Evas_Object *obj,void *event_info) {
  E(0,"throwing out paramblk\n");
  if(data)
    free(data); }



static void _live_frame(void *data,Evas_Object *obj,void *event_info) {
/* int w,h;
   emotion_object_size_get(obj,&w,&h);
   evas_damage_rectangle_add(data,0,0,w,h);
   evas_obscured_clear(data); */
  evas_render(data); }



/**
 * create a live thumbnail
 * @return an error-code
 * @param tp    an opaque thumbnailer-plugin struct
 * @param e     the struct describing the thumbnail
 * @param what  what is to be created (if the thumbnailer has more than one capability)
 */
int thumbnail_plugin_live(thumbnail_plugin *tp,struct _Evidence_Thumb *e,thumbnail_plugin_capabilities what) {
  Evas_Object            *o;
  thumbnailer_xine_param *param;

  if(!(param=param_new()))
    return NOIR_FAIL;

  if((o=emotion_object_add((Evas *)e->data))) {
    int w,h;

    evas_object_smart_callback_add(o,"frame_decode",_live_frame,e->data);

    evas_object_smart_callback_add(o,"decode_stop",_live_done,param);

    evas_object_move(o,0,0);
    evas_object_resize(o,320,240);

    emotion_object_smooth_scale_set(o,1);

    evas_object_show(o);

    emotion_object_file_set(o,e->origfile);
    emotion_object_play_set(o,1);

    E(1,"now playing \"%s\" %.2fs\n",e->origfile,emotion_object_play_length_get(o));

    emotion_object_size_get(o,&w,&h);
    evas_object_resize(o,e->w,e->h);  /* fill the window */
    E(1,"original size %dx%d, zoom to %dx%d...\n",w,h,e->w,e->h); }
  else
    E(0,"couldn't create live object for movie \"%s\"...\n",e->origfile);

  return (e->data=o)?NOIR_SUCC:NOIR_FAIL; }

#endif



/*****************************************************************************/
