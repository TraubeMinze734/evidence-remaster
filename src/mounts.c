/** @file  mounts.c
 *  @brief file-systems (dealing with "mounts" only!)
 */

#define MY_NAME "Mount"




// #include "config.h"      /* config */

#include <stdio.h>       /* stderr */
#include <stdlib.h>      /* free() */
#include <string.h>      /* strrchr() */

#include "meta.h"        /* meta_*() */
#include "mounts.h"      /* magics */
#include "errors.h"      /* NOIR_* */
#include "file_lcl.h"    /* file_if_local() */
#include "util.h"        /* esprintf() */
#include "debug.h"       /* E() */
#include "file.h"        /* file_exists() */
#include "monitors.h"    /* file_monitor_add_with_handler() */



/*****************************************************************************/



#ifdef HAVE_STATVFS
#  define __USE_GNU
#  include <sys/statvfs.h>
#endif

#ifdef HAVE_STATFS
#  include <sys/statfs.h>

typedef struct _fs_type {
  long        magic;
  const char *name;
} fstype;

static fstype fstypes[]={
  { ADFS_SUPER_MAGIC,     "ADFS" },
  { AFFS_SUPER_MAGIC,     "AFFS" },
  { BEFS_SUPER_MAGIC,     "BeFS" },
  { BFS_MAGIC,            "BFS" },
  { CIFS_MAGIC_NUMBER,    "CIFS (Samba)" },
  { CODA_SUPER_MAGIC,     "Coda" },
  { COH_SUPER_MAGIC,      "COH" },
  { CRAMFS_MAGIC,         "CramFS" },
  { DEVFS_SUPER_MAGIC,    "DevFS" },
  { EFS_SUPER_MAGIC,      "EFS" },
  { EXT_SUPER_MAGIC,      "Ext" },
  { EXT2_OLD_SUPER_MAGIC, "Ext2 (old)" },
  { EXT2_SUPER_MAGIC,     "Ext2" },
  { EXT3_SUPER_MAGIC,     "Ext3" },
  { HFS_SUPER_MAGIC,      "HFS" },
  { HPFS_SUPER_MAGIC,     "HPFS" },
  { HUGETLBFS_MAGIC,      "HugeTLB-FS" },
  { ISOFS_SUPER_MAGIC,    "ISO-FS" },
  { JFFS2_SUPER_MAGIC,    "JFFS2" },
  { JFS_SUPER_MAGIC,      "JFS" },
  { MINIX_SUPER_MAGIC,    "Minix" },
  { MINIX_SUPER_MAGIC2,   "Minix (30 character names)" },
  { MINIX2_SUPER_MAGIC,   "Minix2" },
  { MINIX2_SUPER_MAGIC2,  "Minix2 (30 character names)" },
  { MSDOS_SUPER_MAGIC,    "MS-DOS" },
  { NCP_SUPER_MAGIC,      "NCP" },
  { NFS_SUPER_MAGIC,      "NFS" },
  { NTFS_SB_MAGIC,        "NTFS" },
  { OPENPROM_SUPER_MAGIC, "OpenPROM" },
  { PROC_SUPER_MAGIC,     "proc" },
  { QNX4_SUPER_MAGIC,     "QNX4" },
  { REISERFS_SUPER_MAGIC, "ReiserFS" },
  { ROMFS_MAGIC,          "ROM-FS" },
  { SMB_SUPER_MAGIC,      "SMB (Samba)" },
  { SYSV2_SUPER_MAGIC,    "SysV2" },
  { SYSV4_SUPER_MAGIC,    "SysV4" },
  { TMPFS_MAGIC,          "TmpFS" },
  { UDF_SUPER_MAGIC,      "UDF" },
  { UFS_MAGIC,            "UFS" },
  { USBDEVICE_SUPER_MAGIC,"USB" },
  { VXFS_SUPER_MAGIC,     "VxFS" },
  { XENIX_SUPER_MAGIC,    "Xenix" },
  { XFS_SUPER_MAGIC,      "XFS" },
  { _XIAFS_SUPER_MAGIC,   "XIA-FS" },

  { AUTOFS_MAGIC,         "AutoFS" }
};
#endif


typedef enum {
  FSH_NONE=0,
  FSH_INODES=1,
  FSH_BLOCKS=2,
  FSH_ID=4,
  FSH_NAMELEN=8,
  FSH_TYPE=16,
  FSH_MB=32
} fsh_have;



/*****************************************************************************/



int mounts_get_meta(file_info *fi) {
  struct statfs    fs;
  struct statvfs   vfs;
  const char      *path;
  int              ret=NOIR_SUCC;
  noir_md         *md;
  noir_provider   *l=NULL;
  char            *item=NULL;
  fsh_have         have=FSH_NONE;
  file_info       *pa;
  unsigned long    pa_id=0;

  if(!fi)
    return NOIR_FAIL;
  if(!(path=file_if_local(file_info_get_tag(fi))))
    return NOIR_NOTSUPP;

  if((pa=file_info_get_parent(fi))) {
    noir_meta *m;
    if((md=file_info_get_meta(pa))&&
       (m=meta_in_group(md,"FSID",MY_NAME))&&
       meta_get_val(m))
      pa_id=strtoul(meta_get_val(m),NULL,10); }
  else if((debug>0)&&strcmp(path,"/"))
    E(0,"%s has no parent!?\n",path);

  md=file_info_get_meta(fi);

#ifdef HAVE_STATVFS
  if(!statvfs(path,&vfs)) {
    if(pa_id&&(pa_id==vfs.f_fsid)) {  /* same as parent */
      ret=NOIR_IGNORED;
      goto done; }

    if(!(have&FSH_ID)&&vfs.f_fsid) {
      esprintf(&item,"$%x",vfs.f_fsid);
      if(item) {
        have|=FSH_ID;
        meta_add(md,META_RO,META_STR,"FSID","File-system ID",MY_NAME,item,l); }}

    if(!(have&FSH_NAMELEN)&&vfs.f_namemax) {
      esprintf(&item,"%ld",vfs.f_namemax);
      if(item) {
        have|=FSH_NAMELEN;
        meta_add(md,META_RO,META_STR,"Name-length","Maximum length of filenames in this file-system",MY_NAME,item,l); }}

    if(!(have&FSH_INODES)&&vfs.f_files) {
      esprintf(&item,"%'lld used, %'lld reserved, %'lld total (%.2f %% used)",
               (long long)vfs.f_files-vfs.f_ffree,
               (long long)vfs.f_ffree-vfs.f_favail,
               (long long)vfs.f_files,
               (vfs.f_files-vfs.f_ffree)/((float)vfs.f_files));
      if(item) {
        have|=FSH_INODES;
        meta_add(md,META_RO,META_STR,"Inodes","Number of files in file-system",MY_NAME,item,l); }}


    if(!(have&FSH_BLOCKS)&&vfs.f_blocks) {
      esprintf(&item,"%'lld used, %'lld reserved, %'lld total (%.2f %% used, %'ld bytes/block, %'ld bytes/fragment)",
               (long long)vfs.f_blocks-vfs.f_bfree,
               (long long)vfs.f_bfree-vfs.f_bavail,
               (long long)vfs.f_blocks,
               (vfs.f_blocks-vfs.f_bfree)/((float)vfs.f_blocks),
               vfs.f_bsize,vfs.f_frsize);
      if(item) {
        have|=FSH_BLOCKS;
        meta_add(md,META_RO,META_STR,"Blocks","Number of blocks in file-system",MY_NAME,item,l); }

      if(!(have&FSH_MB)&&(vfs.f_bsize||vfs.f_frsize)) {
        unsigned long mu=vfs.f_frsize?vfs.f_frsize:vfs.f_bsize;
        esprintf(&item,"%'lld used, %'lld reserved, %'lld total (%.2f %% used)",
                 (long long)mu*(vfs.f_blocks-vfs.f_bfree)/(1024*1024),
                 (long long)mu*(vfs.f_bfree-vfs.f_bavail)/(1024*1024),
                 (long long)mu*vfs.f_blocks/(1024*1024),
                 (long long)(vfs.f_blocks-vfs.f_bfree)/((float)vfs.f_blocks));
        if(item) {
          have|=FSH_MB;
          meta_add(md,META_RO,META_STR,"Megabytes","Number of mega-bytes in file-system",MY_NAME,item,l); }}}

    if(vfs.f_flag) {
      unsigned long  fl=vfs.f_flag;
      char          *p,*ld=NULL;

      esprintf(&item,"$%x ",fl);
      esprintf(&ld,"This tree is mounted ");

      if(fl&ST_RDONLY) { fl&=(~ST_RDONLY); eappendf(&item,"ro,");     eappendf(&ld,"read-only, "); }
      else             {                   eappendf(&item,"rw,");     eappendf(&ld,"read/write, "); }
      if(fl&ST_NOSUID) { fl&=(~ST_NOSUID); eappendf(&item,"nosuid,"); eappendf(&ld,"ignore SUID and SGID bits, "); }
#ifdef ST_NODEV
      if(fl&ST_NODEV)  { fl&=(~ST_NODEV);  eappendf(&item,"nodev,"); eappendf(&ld,"ignore device files, "); }
#endif
#ifdef ST_NOEXEC
      if(fl&ST_NOEXEC) { fl&=(~ST_NOEXEC); eappendf(&item,"noexec,"); eappendf(&ld,"disallow program execution, "); }
#endif
#ifdef ST_SYNCHRONOUS
      if(fl&ST_SYNCHRONOUS) { fl&=(~ST_SYNCHRONOUS); eappendf(&item,"synchronous,"); eappendf(&ld,"writes are synced at once, "); }
#endif
#ifdef ST_MANDLOCK
      if(fl&ST_MANDLOCK) { fl&=(~ST_MANDLOCK); eappendf(&item,"mandlock,"); eappendf(&ld,"allow mandatory locks, "); }
#endif
#ifdef ST_WRITE
      if(fl&ST_WRITE) { fl&=(~ST_WRITE); eappendf(&item,"write,"); eappendf(&ld,"write on file/directory/symlink, "); }
#endif
#ifdef ST_APPEND
      if(fl&ST_APPEND) { fl&=(~ST_APPEND); eappendf(&item,"append,"); eappendf(&ld,"append-only files, "); }
#endif
#ifdef ST_IMMUTABLE
      if(fl&ST_IMMUTABLE) { fl&=(~ST_IMMUTABLE); eappendf(&item,"immutable,"); eappendf(&ld,"immutable files, "); }
#endif
#ifdef ST_NOATIME
      if(fl&ST_NOATIME) { fl&=(~ST_NOATIME); eappendf(&item,"noatime,"); eappendf(&ld,"do not update access times, "); }
#endif
#ifdef ST_NODIRATIME
      if(fl&ST_NODIRATIME) { fl&=(~ST_NODIRATIME); eappendf(&item,"nodiratime,"); eappendf(&ld,"do not update directory access times, "); }
#endif

      if(fl) {
        eappendf(&item,"???,");
        eappendf(&ld,"and there are some unknown mount-flags set."); }

      if((p=strrchr(item,',')))   *p='\0';
      if((p=strrchr(ld,',')))   { *(p++)='.'; *p='\0'; }

      meta_add(md,META_RO,META_STR,"Flags",ld,MY_NAME,item,l);
      if(ld)
        free(ld); }
    else
      meta_add(md,META_RO,META_STR,"Flags","This tree is mounted with default flags.",MY_NAME,"$0",l); }
  else
    ret=NOIR_FAIL;

#endif

#ifdef HAVE_STATFS
  if(!statfs(path,&fs)) {
    if(pa_id&&(pa_id==fs.f_type)) {  /* same as parent */
      ret=NOIR_IGNORED;
      goto done; }

    if(!(have&FSH_ID)&&fs.f_type) {
      esprintf(&item,"$%x",fs.f_type);
      if(item) {
        have|=FSH_ID;
        meta_add(md,META_RO,META_STR,"FSID","File-system ID",MY_NAME,item,l); }}

    if(fs.f_type) {
      int n=sizeof(fstypes)/sizeof(fstype),i;
      for(i=0;i<n;i++) {
        if(fs.f_type==fstypes[i].magic) {
          meta_add(md,META_RO,META_STR,"FSType","File-system type",MY_NAME,fstypes[i].name,l);
          i=n; }}
      if(i<=n) {
        esprintf(&item,"$%x",fs.f_type);
        have|=FSH_TYPE;
        meta_add(md,META_RO,META_STR,"FSType","File-system type",MY_NAME,item,l); }}

    if(!(have&FSH_NAMELEN)&&fs.f_namelen) {
      esprintf(&item,"%ld",fs.f_namelen);
      if(item) {
        have|=FSH_NAMELEN;
        meta_add(md,META_RO,META_STR,"Name-length","Maximum length of filenames in this file-system",MY_NAME,item,l); }}

    if(!(have&FSH_INODES)&&fs.f_files) {
      esprintf(&item,"%'lld/%'lld (%.2f %% used)",
               (long long)fs.f_files-fs.f_ffree,
               (long long)fs.f_files,
               (fs.f_files-fs.f_ffree)/((float)fs.f_files));
      if(item) {
        have|=FSH_INODES;
        meta_add(md,META_RO,META_STR,"Inodes","Number of files in file-system",MY_NAME,item,l); }}

    if(!(have&FSH_BLOCKS)&&fs.f_blocks) {
      esprintf(&item,"%'lld used, %'lld reserved, %'lld total (%.2f %% used, %'ld bytes/block)",
               (long long)fs.f_blocks-fs.f_bfree,
               (long long)fs.f_bfree-fs.f_bavail,
               (long long)fs.f_blocks,
               (fs.f_blocks-fs.f_bfree)/((float)fs.f_blocks),
               fs.f_bsize);
      if(item) {
        have|=FSH_BLOCKS;
        meta_add(md,META_RO,META_STR,"Blocks","Number of blocks in file-system",MY_NAME,item,l); }

      if(!(have&FSH_MB)&&fs.f_bsize) {
        esprintf(&item,"%'lld used, %'lld reserved, %'lld total (%.2f %% used)",
                 (long long)fs.f_bsize*(fs.f_blocks-fs.f_bfree)/(1024*1024),
                 (long long)fs.f_bsize*(fs.f_bfree-fs.f_bavail)/(1024*1024),
                 (long long)fs.f_bsize*fs.f_blocks/(1024*1024),
                 (long long)(fs.f_blocks-fs.f_bfree)/((float)fs.f_blocks));
        if(item) {
          have|=FSH_MB;
          meta_add(md,META_RO,META_STR,"Megabytes","Number of mega-bytes in file-system",MY_NAME,item,l); }}}

    if(ret!=NOIR_FAIL)
      ret=NOIR_SUCC;
    else
      ret=NOIR_PARTIAL; }
  else if(ret==NOIR_SUCC)
    ret=NOIR_PARTIAL;
  else
    ret=NOIR_FAIL;
#endif

 done:
  if(item)
    free(item);

  return ret; }






/*****************************************************************************/



static int mounts_file_event_handler(void *session,file_info *fi,model_event etype,file_flags fc,void *data) {
  static char *mon[]={ "???", "UPD", "DEL", "CWD", "MSC" };
  E(0,"%s/$%-4x   \"%s\"  \"%s\"\n",mon[etype],fc,fi?file_info_get_tag(fi):"<NULL>",fi?file_info_get_name(fi):"<NULL>");
  return NOIR_SUCC; }






/*****************************************************************************/



static int mounts_add_mount(q_head *queue,const char *tag) {
  file_info *fi;
  int        ret=NOIR_FAIL;

  file_monitor_add_with_handler(NULL,NOIR_MONITOR_DIR,(char *)tag,mounts_file_event_handler,NULL);

  if(!(fi=file_info_fetch(NULL,tag))) {
    model_path_get(queue,NULL,(char *)tag,PG_ENQUEUE_PATH); /* force reading */
    if(!(fi=file_info_fetch(NULL,tag))) {
      E(2,"couldn't get file-info for  \"%s\"...\n",tag);
      return NOIR_NOTFOUND; }}

  if(!(file_info_get_infobits(fi)&FF_META))  /* JIT dir */
    file_get_meta_with_lock(fi,NULL);

  if((ret=mounts_get_meta(fi))==NOIR_SUCC) {
    if(debug>2) {
      char *m=meta_to_string(file_info_get_meta(fi));
      if(m) {
        E(0,"meta: |%s|\n\n",m);
        free(m); }
      else
        E(1,"no meta-data on \"%s\"?  0x%x  %p\n",file_info_get_tag(fi),file_info_get_infobits(fi),file_info_get_meta(fi)); }
    else
      E(2,"read meta-data for mount-point \"%s\"...\n",tag); }
  else if(ret!=NOIR_IGNORED)
    E(0,"couldn't get info for mount-point \"%s\"...\n",tag);

  return ret; }






/*****************************************************************************/



int mounts_init(q_head *queue) {
  const char *stds[]={ "/", "/home", "/misc", "/mnt", "/boot", "/dvd", "/cdrom", "/proc", "/tmp", "/var/tmp" };
  int         i,n=sizeof(stds)/sizeof(const char *);

  return NOIR_IGNORED;

  for(i=0;i<n;i++) {
    if(file_exists(stds[i]))
      mounts_add_mount(queue,stds[i]); }

  return NOIR_SUCC; }



int mounts_exit(void) {
  return NOIR_IGNORED; }



/*****************************************************************************/
