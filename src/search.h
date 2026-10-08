/**
 *  @file  search.h
 *  @brief search files by meta-data and create a "live" folder
 */

#define EVIDENCE_DEFAULT_SEARCH_DB "evidoodle"

typedef enum {
  SEARCH_NONE=0,
  SEARCH_DEBUG=1,
  SEARCH_CASEFOLD=2,
  SEARCH_APPROX=4
} search_opts;

int search_scan_dir(const char *cdb,const char *dir,int recoursive_indexing);
int search_file_by_md(const char *db,const char *prefix,const char *keyword,search_opts opt);
int search_init(void);
int search_exit(void);
