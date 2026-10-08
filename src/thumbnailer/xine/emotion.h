#include <stdio.h>

#include "config.h"
#include "errors.h"
#include "debug.h"
#include "util.h"

#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <Evas.h>

#if 0
#  include <Ecore.h>
#  include <Ecore_Job.h>
#endif



enum _Emotion_Event
{
   EMOTION_EVENT_MENU1, // Escape Menu
   EMOTION_EVENT_MENU2, // Title Menu
   EMOTION_EVENT_MENU3, // Root Menu
   EMOTION_EVENT_MENU4, // Subpicture Menu
   EMOTION_EVENT_MENU5, // Audio Menu
   EMOTION_EVENT_MENU6, // Angle Menu
   EMOTION_EVENT_MENU7, // Part Menu
   EMOTION_EVENT_UP,
   EMOTION_EVENT_DOWN,
   EMOTION_EVENT_LEFT,
   EMOTION_EVENT_RIGHT,
   EMOTION_EVENT_SELECT,
   EMOTION_EVENT_NEXT,
   EMOTION_EVENT_PREV,
   EMOTION_EVENT_ANGLE_NEXT,
   EMOTION_EVENT_ANGLE_PREV,
   EMOTION_EVENT_FORCE,
   EMOTION_EVENT_0,
   EMOTION_EVENT_1,
   EMOTION_EVENT_2,
   EMOTION_EVENT_3,
   EMOTION_EVENT_4,
   EMOTION_EVENT_5,
   EMOTION_EVENT_6,
   EMOTION_EVENT_7,
   EMOTION_EVENT_8,
   EMOTION_EVENT_9,
   EMOTION_EVENT_10
};



enum _Emotion_Meta_Info
{
   EMOTION_META_INFO_TRACK_TITLE,
   EMOTION_META_INFO_TRACK_ARTIST,
   EMOTION_META_INFO_TRACK_ALBUM,
   EMOTION_META_INFO_TRACK_YEAR,
   EMOTION_META_INFO_TRACK_GENRE,
   EMOTION_META_INFO_TRACK_COMMENT,
   EMOTION_META_INFO_TRACK_DISC_ID
};



typedef enum _Emotion_Event     Emotion_Event;
typedef enum _Emotion_Meta_Info Emotion_Meta_Info;



#define EMOTION_CHANNEL_AUTO -1
#define EMOTION_CHANNEL_DEFAULT 0



#define META_TRACK_TITLE 1
#define META_TRACK_ARTIST 2
#define META_TRACK_GENRE 3
#define META_TRACK_COMMENT 4
#define META_TRACK_ALBUM 5
#define META_TRACK_YEAR 6
#define META_TRACK_DISCID 7



typedef struct _Emotion_Video_Module Emotion_Video_Module;



struct _Emotion_Video_Module
{
   int          (*init) (void);
   int          (*shutdown) (void);
   void *       (*file_open) (const char *file, Evas_Object *obj);
   void         (*file_close) (void *ef);
   void         (*play) (void *ef, double pos);
   void         (*stop) (void *ef);
   void         (*size_get) (void *ef, int *w, int *h);
   void         (*pos_set) (void *ef, double pos);
   double       (*len_get) (void *ef);
   double       (*fps_get) (void *ef);
   double       (*pos_get) (void *ef);
   double       (*ratio_get) (void *ef);
   int          (*seekable) (void *ef);
   void         (*frame_done) (void *ef);
   void         (*yuv_size_get) (void *ef, int *w, int *h);
   int          (*yuv_rows_get) (void *ef, int w, int h, unsigned char **yrows, unsigned char **urows, unsigned char **vrows);
   void         (*event_feed) (void *ef, int event);
   void         (*event_mouse_button_feed) (void *ef, int button, int x, int y);
   void         (*event_mouse_move_feed) (void *ef, int x, int y);
   int          (*video_channel_count) (void *ef);
   void         (*video_channel_set) (void *ef, int channel);
   int          (*video_channel_get) (void *ef);
   const char * (*video_channel_name_get) (void *ef, int channel);
   void         (*video_channel_mute_set) (void *ef, int mute);
   int          (*video_channel_mute_get) (void *ef);
   int          (*audio_channel_count) (void *ef);
   void         (*audio_channel_set) (void *ef, int channel);
   int          (*audio_channel_get) (void *ef);
   const char * (*audio_channel_name_get) (void *ef, int channel);
   void         (*audio_channel_mute_set) (void *ef, int mute);
   int          (*audio_channel_mute_get) (void *ef);
   void         (*audio_channel_volume_set) (void *ef, double vol);
   double       (*audio_channel_volume_get) (void *ef);
   int          (*spu_channel_count) (void *ef);
   void         (*spu_channel_set) (void *ef, int channel);
   int          (*spu_channel_get) (void *ef);
   const char * (*spu_channel_name_get) (void *ef, int channel);
   void         (*spu_channel_mute_set) (void *ef, int mute);
   int          (*spu_channel_mute_get) (void *ef);
   int          (*chapter_count) (void *ef);
   void         (*chapter_set) (void *ef, int chapter);
   int          (*chapter_get) (void *ef);
   const char * (*chapter_name_get) (void *ef, int chapter);
   void         (*speed_set) (void *ef, double speed);
   double       (*speed_get) (void *ef);
   int          (*eject) (void *ef);
   const char * (*meta_get) (void *ef, int meta);
   
   void          *handle;
};



void *_emotion_video_get(Evas_Object *obj);
void  _emotion_frame_new(Evas_Object *obj);
void  _emotion_video_pos_update(Evas_Object *obj, double pos, double len);
void  _emotion_frame_resize(Evas_Object *obj, int w, int h, double ratio);
void  _emotion_decode_stop(Evas_Object *obj);
void  _emotion_channels_change(Evas_Object *obj);
void  _emotion_title_set(Evas_Object *obj, char *title);
void  _emotion_progress_set(Evas_Object *obj, char *info, double stat);
void  _emotion_file_ref_set(Evas_Object *obj, char *file, int num);
void  _emotion_spu_button_num_set(Evas_Object *obj, int num);
void  _emotion_spu_button_set(Evas_Object *obj, int button);
    


#define E_SMART_OBJ_GET(smart, o, type) \
     { \
	char *_e_smart_str; \
	\
	if (!o) return; \
	smart = evas_object_smart_data_get(o); \
	if (!smart) return; \
	_e_smart_str = (char *)evas_object_type_get(o); \
	if (!_e_smart_str) return; \
	if (strcmp(_e_smart_str, type)) return; \
     }

#define E_SMART_OBJ_GET_RETURN(smart, o, type, ret) \
   { \
      char *_e_smart_str; \
      \
      if (!o) return ret; \
      smart = evas_object_smart_data_get(o); \
      if (!smart) return ret; \
      _e_smart_str = (char *)evas_object_type_get(o); \
      if (!_e_smart_str) return ret; \
      if (strcmp(_e_smart_str, type)) return ret; \
   }



#define E_OBJ_NAME "emotion_object"



typedef struct _Smart_Data Smart_Data;



struct _Smart_Data
{
   Emotion_Video_Module  *module;
   void                  *video;
   
   char          *file;
   Evas_Object   *obj;
   double         ratio;
   double         pos;
   double         seek_pos;
   double         len;
   
#ifdef _ECORE_JOB_H
   Ecore_Job     *job;
#endif
   
   unsigned char  play : 1;
   unsigned char  seek : 1;
   
   char *title;
   struct {
      char *info;
      double stat;
   } progress;
   struct {
      char *file;
      int num;
   } ref;
   struct {
      int button_num;
      int button;
   } spu;
};

static void _mouse_move(void *data, Evas *ev, Evas_Object *obj, void *event_info);
static void _mouse_down(void *data, Evas *ev, Evas_Object *obj, void *event_info);
#ifdef _ECORE_JOB_H
static void _pos_set_job(void *data);
#endif
static void _pixels_get(void *data, Evas_Object *obj);

static void _smart_init(void);
static void _smart_add(Evas_Object * obj);
static void _smart_del(Evas_Object * obj);
static void _smart_layer_set(Evas_Object * obj, int layer);
static void _smart_raise(Evas_Object * obj);
static void _smart_lower(Evas_Object * obj);
static void _smart_stack_above(Evas_Object * obj, Evas_Object * above);
static void _smart_stack_below(Evas_Object * obj, Evas_Object * below);
static void _smart_move(Evas_Object * obj, Evas_Coord x, Evas_Coord y);
static void _smart_resize(Evas_Object * obj, Evas_Coord w, Evas_Coord h);
static void _smart_show(Evas_Object * obj);
static void _smart_hide(Evas_Object * obj);
static void _smart_color_set(Evas_Object * obj, int r, int g, int b, int a);
static void _smart_clip_set(Evas_Object * obj, Evas_Object * clip);
static void _smart_clip_unset(Evas_Object * obj);
