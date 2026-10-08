#ifndef THUMBNAIL_PRIVATE_H
#  define THUMBNAIL_PRIVATE_H

#  include "config.h"

#  include <time.h>

#  define EXT_JPG  ".jpg"
#  define EXT_PNG  ".png"



#  define EVIDENCE_THUMB_BASE 128



typedef enum {
  TD_FAIL_ME,
  TD_NORMAL,
  TD_LARGE,
  TD_BASE,
  TD_FAIL } thumbnaildir;



typedef enum {
  TT_DK,
  TT_EPEG,
  TT_PNG,
  TT_FOLDER } thumbnailtype;



char *evidence_thumbnail_path(thumbnaildir td,const char *file,const char *ext);
char *evidence_thumb_get_tmpname(const char *file,const char *ext);

#endif
