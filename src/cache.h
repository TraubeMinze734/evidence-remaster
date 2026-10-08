
#include "util.h"
#include "model.h"
#include <time.h>

#define TAG_CACHE_FOR    "EvCa"
#define TAG_CACHE_HDR    "CHdr"
#define TAG_CACHE_FILE   "CFil"
#define TAG_CACHE_THUMB  "CThm"
#define TAG_CACHE_WINXY  "CWXY"
#define TAG_CACHE_ICNXY  "CIXY"
#define TAG_CACHE_SHELF  "CShl"
#define TAG_CACHE_BDROP  "CBgr"



typedef struct _cache_el {
  char     magic[4];
  ulong    len; } cache_el;

typedef struct _cache_hdr {
  char     version[4];
  time_t   ctime;
  ulong    nfiles; } cache_hdr;



int           file_save_cache(file_info *);
int           file_load_cache(q_element *);
