#ifndef MIME_H
#  define MIME_H

typedef enum {
  MIME_NONE=0,
  MIME_FULL,
  MIME_PARTIAL,
  MIME_ANY
} mime_match;



#  define MIME_FILE          "mime.types"
#  define MIME_ALII          "mime.alii"
#  define MIME_ICONS         "mime.icons"
#  define MIME_HANDLERS      "mime.handlers"
#  define MIME_MAX_LEN       512
#  define MIME_DK_FILE       "???"
#  define MIME_DK_TYPE       "unknown"
#  define MIME_DELIMITER     '/'

#  define MIME_INTL          "internal/"
#  define MIME_INTL_MB1      "internal/root1"
#  define MIME_INTL_MB2      "internal/root2"
#  define MIME_INTL_MB3      "internal/root3"
#  define MIME_INTL_REN      "internal/rename"
#  define MIME_INTL_DEL      "internal/delete"
#  define MIME_INTL_MKD      "internal/mkdir"
#  define MIME_INTL_FND      "internal/search"
#  define MIME_INTL_INF      "internal/properties"

#  define MIME_FOLDER        "folder"
#  define MIME_VFOLDER       "vfolder"
#  define MIME_EXECUTABLE    "application/x-executable"
#  define MIME_SCRIPT        "application/x-script"
#  define MIME_BACKUP        "application/x-backup"
#  define MIME_CHAR          "character_device"
#  define MIME_BLK           "block_device"
#  define MIME_FIFO          "FIFO"
#  define MIME_SOCKET        "socket"

#endif
