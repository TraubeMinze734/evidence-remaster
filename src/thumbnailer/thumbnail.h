#ifndef EVIDENCE_THUMBNAIL_H
#  define EVIDENCE_THUMBNAIL_H

#  include "model.h"
#  include "file.h"  /* for noir_job_type */


int        thumbnail_lib_gc(void);
void       thumbnail_imlib_gc(void);
int        thumbnail_disk_gc(void);  /* go through your thumbnail cache and delete bad references */
int        thumbnail_disk_gc_one(const char *);
int        thumbnail_disk_op(noir_job_type,const char *,const char *);


int        thumbnail_init(const char *setup);  /* create the necessary thumbnail directories  */
int        thumbnail_exit(void);



int        thumbnail_enqueue(file_info *fi);
file_info *thumbnailer_pending(void);

int        thumbnail_find_for(const char *tag,char **path);

#endif
