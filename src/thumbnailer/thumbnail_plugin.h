#ifndef THUMBNAIL_PLUGIN_H
#include <time.h>
#include "thumbnail_private.h"
#  define THUMBNAIL_PLUGIN_H

#include <time.h>

typedef enum {
  TP_NONE =0,

  TP_IMAGE=1,  /**< can create a static thumbnail image */
  TP_VIDEO=2,  /**< can create a live video object (Evas smart object) */
  TP_AUDIO=4,  /**< can create sound (along with image or video, or alone) */

  TP_IMLIB=8,  /**< image-function will create an Imlib-image */
  TP_FILE =16  /**< image-function will create a file */
} thumbnail_plugin_capabilities;



typedef enum {
  TP_ICONS=0,
  TP_MOUSEOVER,
  TP_LIVE,
  TP_COUNT
} thumbnail_plugin_register_for;



typedef enum {
  TP_PRIO_TOP=1,
  TP_PRIO_HIGH=2,
  TP_PRIO_MID=3,
  TP_PRIO_LOW=4,
  TP_PRIO_BOTTOM=5,
  TP_PRIO_FALLBACK=9
} thumbnail_plugin_prio;



typedef struct _Evidence_Thumb {
   void         *theme;          /**< the noir_theme */
   char         *uri;            /**< URI of original file */
   char         *hashedname;     /**< MD5 hash of above URI (no extension) */
   char         *origfile;       /**< path to original image */
   char         *thumbfile;      /**< path to thumbnail of image */
   const char   *mime;           /**< the MIME-type of the original file */

   struct {
     time_t      file,           /**< the original's MTime as per stat(). */
                 thumb;          /**< the thumbnail's MTime. */
   } mtime;

   thumbnaildir  size,           /**< TD_NORMAL or TD_LARGE */
                 found_in;       /**< the dir the thumbnail lives in */
   thumbnailtype type;           /**< EPEG, PNG, folder-Image, ... */

   int           w,h,alpha;      /**< size of the thumbnail, not the original */
   void         *data;           /**< thumbnail data (for saving as PNG) */

   int           attr_used;      /**< number of additional info-items */
   int           attr_allocd;    /**< number of additional info-items */
   char        **attr_key;       /**< keys of additional info-items */
   char        **attr_val;       /**< values of additional info-items */

   struct _thumbnail_plugin *tp; /**< thumbnailer */

   struct _Evidence_Thumb *next; /**< next thumbnail-struct in list, or NULL */
} Evidence_Thumb;



typedef struct _thumbnail_plugin {
  void                           *lib;
  void                           *data;
  char                           *file;
  thumbnail_plugin_capabilities   can;
  int                             refcount;
  int                           (*init)(struct _thumbnail_plugin *);
  int                           (*exit)(struct _thumbnail_plugin *);
  int                           (*make)(struct _thumbnail_plugin *,struct _Evidence_Thumb *,void **);
  int                           (*live)(struct _thumbnail_plugin *,struct _Evidence_Thumb *,thumbnail_plugin_capabilities request);
  time_t                          ts;
} thumbnail_plugin;



thumbnail_plugin *thumbnail_plugin_find_by_mime(const char *mime,thumbnail_plugin_register_for type);
int  thumbnail_plugin_load(thumbnail_plugin *tp);

int  thumbnail_plugins_init(const char *setup);
int  thumbnail_plugins_exit(void);
int  thumbnail_plugins_gc(void);

int  thumbnail_plugin_register_mime(thumbnail_plugin *tp,const char *mime,thumbnail_plugin_prio prio,thumbnail_plugin_register_for type);
int  thumbnail_plugin_register_ext( thumbnail_plugin *tp,const char *ext, thumbnail_plugin_prio prio,thumbnail_plugin_register_for type);

int  thumbnail_plugin_data_set(thumbnail_plugin *tp,void *data);
int  thumbnail_plugin_data_get(thumbnail_plugin *tp,void **data);

int  thumbnail_attribute_add(Evidence_Thumb *e,const char *key,const char *val);



#  ifdef HAVE_IMLIB2
#    define X_DISPLAY_MISSING
#    include <Imlib2.h>
     Imlib_Image evidence_thumb_imlib_standardize(struct _Evidence_Thumb *e);
#  endif
#endif
