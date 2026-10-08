/** @file  thumbnailer/thumbnail.c
 *  @brief threaded modular thumbnailer.
 *
 * original imlib2 thumbnailer by Azundris
 * original truetype thumbnailer by raster
 * original freedesktop.org-compatible thumbnailer by atmos
 * threaded thumbnailer by Azundris
 * modular  thumbnailer by Azundris
 */

#define LISTENER      "thumbnailer"

#define THUMBNAIL_IN_PROTECTED_MODE

#include "config.h"
#include "userconfig.h"

/* how many seconds a thumbnailer gets before it is consider "stuck" and killed */
#define THUMBNAILER_TIMEOUT 6

#include "errors.h"
#include "thumbnail.h"
#include "thumbnail_private.h"
#include "util.h"
#include "mime.h"                     /* MIME_DELIMITER */
#include "monitors.h"                 /* file_monitor_add() */
#include "gui/common/alert.h"         /* form_modeless_*() */

#include "gui/efl/common/theme.h"     /* theme_get_iconsize(),
                                         theme_get_iconmode() ### FIXME */
#include "file_lcl.h"

#include "thumbnail_plugin.h"



#include <errno.h>

#include <fcntl.h>    /* O_RDONLY */
#include <ctype.h>    /* tolower() */

#ifdef THUMBNAIL_IN_PROTECTED_MODE
#  include <sys/types.h>
#  include <sys/wait.h>
#  ifdef HAVE_ECORE
#    include <Ecore.h>
#  endif
#endif

#ifdef HAVE_EPEG
#  include "epeg/Epeg.h"
#endif

#ifdef HAVE_LIBPNG
#  include "evidence.h"
#  include "md5.h"
#  include "file.h"

#  ifdef HAVE_IMLIB2
#    define X_DISPLAY_MISSING
#    include "imlib.h"
#    include <Imlib2.h>
#  else
#    define DATA32 unsigned int
#  endif
#  include <limits.h>
#  include <dirent.h>
#  include <unistd.h>
#  include <png.h>

#  include <sys/stat.h>
#  include <sys/types.h>
#  include <stdlib.h>






/*****************************************************************************/



typedef enum {
  TN_MUTE,
  TN_ICONS_SKIP,
  TN_MOUSEOVER_SKIP,
  TN_LIVE_SKIP,
  TN_PROTECTED
} tn_tag_type;



struct _tn_param {
  int   mute,protected;
  char *icons_skip,*mouseover_skip,*live_skip;
} tn_param;



typedef struct _Evidence_Thumbnailer {
  pthread_mutex_t               qchg;   /**< frontend and backend shouldn't
                                             modify the Q at the same time */
  queue_flags                   flags;  /**< engaged? */
  pthread_t                     thumbnailer;
  time_t                        ts;     /**< when did thumbnailing the current file start? */
#ifdef THUMBNAIL_IN_PROTECTED_MODE
  Evidence_Thumb               *task_thumbnail; /**< the thumbnail process by the thumbnailer (child-) process.  info is held for debug-info only and refs item in main-queue; item must not be released, only set NULL when done. */
  int                           task_ret;       /**< code returned by the thumbnailer (child-) process */
  pid_t                         task_pid;       /**< PID of the thumbnailer (child-) process */
  pthread_mutex_t               task_wait;      /**< pause thread (thumbnailing in progress in child) */
  pthread_cond_t                task_waiting;   /**< signal wakeup to thumbnailing thread */
  void                         *sigchld_handler_handle;
#endif
  Evidence_Thumb               *anchor; /**< thumbnails to be processed */
  Evidence_Thumb               *current; /**< thumbnails currently processed */
  struct _Evidence_Thumbnailer *next;
} Evidence_Thumbnailer;



/** frontend and backend shouldn't use imlib2 at the same time, it's not
 *  threadsafe.  we will always have the mutex, even if evidence proper
 *  is not compiled with imlib2 support. */
pthread_mutex_t       imlib_mutex;
Evidence_Thumbnailer *thumbnailers=NULL,         /**< list of thumbnailers (one for now) */
                     *thumbnails_complete=NULL;  /**< list of complete thumbnails */




/* forward */
static int thumbnail_failinfo_save(const char *fail,const char *software,char **path);
static int thumbnailer_start(Evidence_Thumbnailer *t);






/*****************************************************************************/



/**
 * listener for numeric properties
 * @return an error-code
 * @param b     the bag
 * @param e     the property
 * @param tag   the numeric tag of the property
 * @param data  the icon-list
 */
static int thumbnail_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  struct _tn_param *tnp=(struct _tn_param *)data;
  long              val=prop_get_int(e);

  if(!tnp)
    return NOIR_FAIL;

  switch(tag) {
    case TN_MUTE:
      tnp->mute=val;
      break;
    case TN_PROTECTED:
      tnp->protected=val;
      break;
    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,prop_get_key(e)?prop_get_key(e):"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld, %p)\n",prop_get_key(e),val,data);

  return NOIR_SUCC; }



/**
 * listener for string properties
 * @return an error-code
 * @param b     the bag
 * @param e     the property
 * @param tag   the numeric tag of the property
 * @param data  the icon-list
 */
static int thumbnail_str_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  struct _tn_param *tnp=(struct _tn_param *)data;
  char             *val=prop_get_string(e);

  switch(tag) {
    case TN_ICONS_SKIP:
      if(tnp->icons_skip)
        free(tnp->icons_skip);
      tnp->icons_skip=val?strdup(val):NULL;
      break;
    case TN_MOUSEOVER_SKIP:
      if(tnp->mouseover_skip)
        free(tnp->mouseover_skip);
      tnp->mouseover_skip=val?strdup(val):NULL;
      break;
    case TN_LIVE_SKIP:
      if(tnp->live_skip)
        free(tnp->live_skip);
      tnp->live_skip=val?strdup(val):NULL;
      break;
    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,prop_get_key(e)?prop_get_key(e):"<none>");
      return NOIR_FAIL; }

  return NOIR_SUCC; }






/*****************************************************************************/
/* HELPERS */
/***********/

/**
 * make a temporary file-name.
 * @return a newly-allocated buffer
 * @param file - the file to make a temporary name for
 * @param ext  - the extension to give the temporary file
 */
char *evidence_thumb_get_tmpname(const char *file,const char *ext) {
  char   *p=file_dirname(file),*q=NULL,*r=NULL;

  if(!p)
    goto fail;
  esprintf(&q,"evidence-%06d%s",(int)getpid(),ext?ext:"");
  if(!q)
    goto fail;
  if(!(r=file_compose_path(p,q,NULL)))
    goto fail;
  free(p);
  free(q);
  return r;

 fail:
  if(p) free(p);
  if(q) free(q);
  if(r) free(r);
  return NULL; }



/**
 * create path for a thumbnail.
 * @return the desired path, or NULL for failure
 * @param td - the directory the thumbnail will reside in
 * @param file - the name for the thumbnail (a hash), or NULL
 * @param ext - the extension for the thumbnail, or NULL
 */
char *evidence_thumbnail_path(thumbnaildir td,const char *file,const char *ext) {
  static char *thumbdirs[]={
    ".thumbnails/fail/evidence-" VERSION, /* evidence's load failures */
    ".thumbnails/normal",        /* 128x128 pixels */
    ".thumbnails/large",         /* 256x256 pixels */
    ".thumbnails",               /* dir we keep our thumbs in */
    ".thumbnails/fail" };        /* app specific dir we keep failures in */
  char *p=NULL;

  if(file_if_absolute(file))
    return strdup(file_if_local(file));

  if(file)
    esprintf(&p,"%s/%s/%s%s",home,thumbdirs[td],file,ext?ext:"");
  else
    esprintf(&p,"%s/%s",home,thumbdirs[td]);
  return p; }



/**
 * get the thumbnail name (hash) on disk.
 *
 * @return an error-code
 * @param e - the evidence thumb we're hashing
 */
static inline int evidence_thumb_hash_name(Evidence_Thumb *e) {
  int               n;
  MD5_CTX           ctx;
  char              md5out[32];
  unsigned char     hash[MD5_HASHBYTES];
  static const char hex[]="0123456789abcdef";

  /* use normal as the fallback */

  if(!e||!e->uri)
    return NOIR_FAIL;

  if(e->hashedname) {
    free(e->hashedname);
    e->hashedname=NULL; }

  MD5Init(&ctx);
  MD5Update(&ctx,e->uri,strlen(e->uri));
  MD5Final(hash,&ctx);

  for(n=0;n<MD5_HASHBYTES;n++) {
    md5out[2*n]=hex[hash[n]>>4];
    md5out[2*n+1]=hex[hash[n]&0x0f]; }
  md5out[2*n]='\0';

  return (e->hashedname=strdup(md5out))?NOIR_SUCC:NOIR_OOM; }



/**
 * free the attributes of an Evidence_Thumb.
 * @param e the Evidence_Thumb we want to free
 */
static void evidence_thumb_free_attributes(Evidence_Thumb *e) {
  if(e&&e->attr_allocd) {
    int i;
    for(i=0;i<e->attr_used;i++) {
      if(e->attr_key[i])
        free(e->attr_key[i]);
      if(e->attr_val[i])
        free(e->attr_val[i]); }
    free(e->attr_key);
    free(e->attr_val);
    e->attr_key=e->attr_val=NULL;
    e->attr_used=e->attr_allocd=0; }}



/**
 * free an Evidence_Thumb.
 * @param e the Evidence_Thumb we want to free
 */
static void evidence_thumb_free(Evidence_Thumb *e) {
  if(!e)
    return;
  if(e->uri)
    free(e->uri);
  if(e->hashedname)
    free(e->hashedname);
  if(e->thumbfile)
    free(e->thumbfile);
  if(e->origfile)
    free(e->origfile);
  evidence_thumb_free_attributes(e);
  free(e); }



/**
 * Allocate a new Evas_Thumb, setting up everything
 * @return an error-code
 * @param thumb - where the address of the newly-allocated buffer is stored
 * @param t - the noir theme, used in icon size calculation and more!
 * @param file - the file on disk we want to make a thumbnail of
 * @param mime - the MIME-type of 'file'
 *
 * called from thumbnail_enqueue_do() only.
 */
static inline int evidence_thumb_new(Evidence_Thumb **thumb,void *t,const char *file,const char *mime) {
  char             buf[PATH_MAX];
  Evidence_Thumb  *e=NULL;
  int              ret=NOIR_FAIL;

  if(!thumb)
    return NOIR_FAIL;

  *thumb=NULL;

  if(!(e=(Evidence_Thumb *)malloc(sizeof(Evidence_Thumb))))
    return NOIR_OOM;

  memset(e,0,sizeof(Evidence_Thumb));

  if(t&&file&&*file) {
    struct stat filestatus;
    int         iw,ih;

    if(theme_get_iconsize(t,&iw,&ih)!=NOIR_SUCC)
      iw=ih=EVIDENCE_THUMB_BASE;


    e->size=((iw<EVIDENCE_THUMB_BASE)&&
             (ih<EVIDENCE_THUMB_BASE))?TD_NORMAL:TD_LARGE;

    e->found_in=TD_FAIL;
    e->type    =TT_DK;
    e->theme   =t;
    e->mime    =mime;

    if(stat(file,&filestatus))
      goto fail;

    if(S_ISDIR(filestatus.st_mode)) {          /* folder */
      char  *imgs[]={ "folder.jpg",
                      ".folder.png",
                      NULL };
      char **img=imgs;
      char  *p=NULL;

      while(*img&&!e->origfile) {
        esprintf(&p,"%s/%s",file,*img);
        if(p) {
          if(!stat(p,&filestatus)&&S_ISREG(filestatus.st_mode)) {
            e->type=TT_FOLDER;
            file=e->origfile=p; }
          else {
            free(p);
            p=NULL; }}
        img++; }

      if(!e->origfile) {
        ret=NOIR_NODATA;
        goto fail; }}

    else if(!(e->origfile=strdup(file))) {
      ret=NOIR_OOM;
      goto fail; }

    snprintf(buf,PATH_MAX,PROTOCOL_FILE "%s",file);
    if(!(e->uri=strdup(buf))) {
      ret=NOIR_OOM;
      goto fail; }

    if(!filestatus.st_size) {
      E(2,"\"%s\" is empty!?\n",e->origfile);
      thumbnail_disk_gc_one(e->origfile); /* remove stale thumbfile, if any */
      ret=NOIR_NODATA;
      goto fail; }
    else {
      char *v=NULL;
      esprintf(&v,"%ld",filestatus.st_size);
      if(v) {
        thumbnail_attribute_add(e,"Thumb::Size",v);
        free(v); }}

    evidence_thumb_hash_name(e);
    e->mtime.file=filestatus.st_mtime;

    ret=NOIR_SUCC; }

  else  /* no data handed in, return zerod out struct */
    ret=NOIR_PARTIAL;

  *thumb=e;
  return ret;


fail:
  evidence_thumb_free(e);
  return ret; }






/*****************************************************************************/
/* PNG */
/*******/

/**
 * find thumbnail in any of the cache-dirs.
 *
 * only used from evidence_thumb_read_file_attributes()
 * @param e - struct describing the thumbnail to look for
 */
static inline char *evidence_thumb_find_cached(Evidence_Thumb *e) {
  char          *p;
  thumbnaildir   dirs[]={ TD_LARGE, TD_NORMAL, TD_FAIL_ME };
  int            n=0;
  struct stat    thumbstatus;

  e->found_in=TD_FAIL;

  /* we could always read the JPEG, of course, but let's only when we
     can also read the attributes et al. */
#ifdef HAVE_EPEG
  for(n=0;n<2;n++) {   /* TD_LARGE..TD_NORMAL */
    if((p=evidence_thumbnail_path(dirs[n],e->hashedname,EXT_JPG))) {
      if(!stat(p,&thumbstatus)) {
        e->type=TT_EPEG;
        goto success; }
      free(p); }}
#endif

  for(n=0;n<3;n++) {  /* TD_LARGE..TD_FAIL_ME */
    if((p=evidence_thumbnail_path(dirs[n],e->hashedname,EXT_PNG))) {
      if(!stat(p,&thumbstatus)) {
        e->type=TT_PNG;
        goto success; }
      free(p); }}

  if(e->thumbfile) {
    free(e->thumbfile);
    e->thumbfile=NULL; }

  return NULL;   /* not found */

 success:
  e->found_in=dirs[n];
  return e->thumbfile=p; }



/**
 * Open a PNG-image.
 * @return file-handle only if file exists and is PNG
 * @param filename - the file to open
 *
 * used by evidence_thumb_read_file_attributes() only */
static inline FILE *evidence_thumb_open_png(char *filename) {
  FILE *fp=NULL;

  if((fp=fopen(filename,"rb"))) {
    char buf[4];
    int  bytes=sizeof(buf);
    int  ret;

    ret=fread(buf,sizeof(char),bytes,fp);
    if(ret!=bytes) {
      fclose(fp);
      fp=NULL; }
    else {
      if((ret=png_check_sig(buf,bytes)))
        rewind(fp);
      else {
        fclose(fp);
        fp=NULL; }}}
  return fp; }



/**
 * read the attributes of an ePEG or PNG thumbnail.
 * @return if NOIR_SUCC, the attributes were set on the thumbnail-descriptor
 * @param e - the thumbnail desciptor
 * @param f - force reading even on fail-infos
 *
 * used in thumbnail_enqueue_do() and thumbnail_disk_gc().
 */
static int evidence_thumb_read_file_attributes(Evidence_Thumb *e,int f) {
  FILE         *fp=NULL;
  char         *n=evidence_thumb_find_cached(e);
  int           ret;

  if(!n)
    return NOIR_NOTFOUND;

  evidence_thumb_free_attributes(e);

  if(!f&&(e->found_in==TD_FAIL_ME)) /* we already know we can't thumbnail */
    return NOIR_IGNORED;            /*  this; don't waste time trying again */

#ifdef HAVE_EPEG
  else if(e->type==TT_EPEG) {
    Epeg_Image          *im;
    Epeg_Thumbnail_Info  info;
    int                  w,h;

    ret=NOIR_NODATA;

    if(!(im=epeg_file_open(e->thumbfile))) {
      E(0,"(\"%s\"): cannot open EPEG\n",e->thumbfile);
      return NOIR_FAIL; }

    epeg_thumbnail_comments_get(im,&info);
    if(!info.mimetype||!info.uri)
      goto epeg_done;

    if(e->uri)
      free(e->uri);
    e->uri=strdup(info.uri);
    e->mtime.thumb=info.mtime;

    epeg_size_get(im,&w,&h);

    if(debug>2) {
      const char *q;
      if((q=file_name(e->thumbfile))) {
        do { q--; } while((q>e->thumbfile)&&(*q!='/')); q++; }
      E(0,"\n  Image \"%s\"\n  thumb \"%s\" (%d, %dx%d)\n\n",
        e->origfile, q?q:e->thumbfile, e->size, w,h); }

    ret=NOIR_SUCC;

  epeg_done:
    epeg_close(im);   /* also release Epeg_Thumbnail_Info */
    return ret; }
#endif

  else if((fp=evidence_thumb_open_png(n))) {
    png_structp png_ptr=NULL;
    png_infop   info_ptr=NULL;
    png_textp   text_ptr;
    int         num_text=0,i;

    if(!(png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL))) {
      fclose(fp);
      return NOIR_OOM; }

    if(!(info_ptr=png_create_info_struct(png_ptr))) {
      png_destroy_read_struct(&png_ptr,(png_infopp) NULL,
                              (png_infopp) NULL);
      fclose(fp);
      return NOIR_OOM; }

    png_init_io(png_ptr,fp);
    png_read_info(png_ptr,info_ptr);

 /* png_get_IHDR(png_ptr,info_ptr,NULL,NULL,NULL,NULL,NULL,NULL,NULL); */

    num_text=png_get_text(png_ptr,info_ptr,&text_ptr,&num_text);

    for(i=0;i<num_text;i++) {
      png_text text=text_ptr[i];

      if(!strcmp(text.key,"Thumb::MTime"))
        e->mtime.thumb=atoi(text.text);
      if(!strcmp(text.key,"Thumb::URI")) {
        if(e->uri)
          free(e->uri);
        e->uri=strdup(text.text); }
      else
       thumbnail_attribute_add(e,text.key,text.text); }

    /* png_read_end(png_ptr,info_ptr); */
    png_destroy_read_struct(&png_ptr,&info_ptr,(png_infopp) NULL);
    fclose(fp);
    return NOIR_SUCC; }

  return NOIR_FAIL; }






/*****************************************************************************/
/* SAVING */
/**********/



/**
 * write thumbnail (from e->data) to a PNG image.
 * @return an error-code
 * @param file - the name for the image-file
 * @param e - the thumbnail descriptor; e->data must point to the thumbnail's image-data
 * @param software - who generated the thumbnail
 *
 * used in evidence_thumb_save_png() only.
 */
static inline int evidence_thumb_write_png(char *file,Evidence_Thumb *e,const char *software) {
  FILE        *fp=NULL;
  char        *tmp=NULL,*v=NULL;
  int          i,j,k,ret=NOIR_FAIL;
  int          w=e->w,h=e->h;

  DATA32      *ptr=NULL;
  png_infop    info_ptr;
  png_color_8  sig_bit;
  png_structp  png_ptr;
  png_text    *text_ptr=NULL;
  png_bytep    row_ptr,
               row_data=NULL;

  if(!(tmp=evidence_thumb_get_tmpname(file,EXT_PNG))) {
    ret=NOIR_OOM;
    goto done; }

  E(2,"Writing PNG thumbnail \"%s\" to \"%s\"...\n",file,tmp);

  if((fp=fopen(tmp,"wb"))) {
    if(!(png_ptr=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL))) {
      ret=NOIR_OOM;
      goto done; }

    if(!(info_ptr=png_create_info_struct(png_ptr))) {
      png_destroy_write_struct(&png_ptr,(png_infopp)NULL);
      ret=NOIR_OOM;
      goto done; }

    if(setjmp(png_ptr->jmpbuf)) {
      png_destroy_write_struct(&png_ptr,&info_ptr);
      ret=NOIR_FAIL;
      goto done; }



    png_init_io(png_ptr,fp);

#    ifdef PNG_TEXT_SUPPORTED


    /* setup tags here */
    v=NULL;
    esprintf(&v,"%d",(int)e->mtime.file);
    if(v) {
      thumbnail_attribute_add(e,"Thumb::MTime",v);
      free(v);
      v=NULL; }

    if(software)
      esprintf(&v,"evidence-%s: %s",VERSION,software);
    else
      esprintf(&v,"evidence-%s",VERSION);
    if(v) {
      thumbnail_attribute_add(e,"Software",v);
      free(v);
      v=NULL; }

    thumbnail_attribute_add(e,"Thumb::URI",e->uri);

    if(e->mime)
      thumbnail_attribute_add(e,"Thumb::Mimetype",e->mime);

    if(!(text_ptr=malloc(sizeof(png_text)*e->attr_used)))
      return NOIR_OOM;

    for(i=0;i<e->attr_used;i++) {
      text_ptr[i].key =e->attr_key[i];
      text_ptr[i].text=e->attr_val[i];
      text_ptr[i].compression=PNG_TEXT_COMPRESSION_NONE; }

    png_set_text(png_ptr,info_ptr,text_ptr,e->attr_used);

#    else
#      warning text tags in PNG files not supported by library, this will violate the freedesktop.org thumbnailing standard!
#    endif
    if(e->alpha) {
      png_set_IHDR(png_ptr,info_ptr,w,h,8,PNG_COLOR_TYPE_RGB_ALPHA,
                   PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_BASE,
                   PNG_FILTER_TYPE_BASE);
#    ifdef WORDS_BIGENDIAN
      png_set_swap_alpha(png_ptr);
#    else
      png_set_bgr(png_ptr);
#    endif
    }
    else {
      png_set_IHDR(png_ptr,info_ptr,w,h,8,PNG_COLOR_TYPE_RGB,
                   PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_BASE,
                   PNG_FILTER_TYPE_BASE);
      row_data=malloc(w*3*sizeof(char)); }

    sig_bit.red=8;
    sig_bit.green=8;
    sig_bit.blue=8;
    sig_bit.alpha=8;
    png_set_sBIT(png_ptr,info_ptr,&sig_bit);

    png_set_compression_level(png_ptr,PNG_COMPRESS);
    png_write_info(png_ptr,info_ptr);
    png_set_shift(png_ptr,&sig_bit);
    png_set_packing(png_ptr);

    ptr=e->data;
    for(i=0;i<h;i++) {
      if(e->alpha)
        row_ptr=(png_bytep) ptr;
      else {
        for(j=0,k=0;j<w;k++) {
          row_data[j++]=(ptr[k]>>16)&0xff;
          row_data[j++]=(ptr[k]>> 8)&0xff;
          row_data[j++]=(ptr[k])    &0xff; }
        row_ptr=(png_bytep) row_data; }
      png_write_row(png_ptr,row_ptr);
      ptr += w; }

    if(row_data)
      free(row_data);

    png_write_end(png_ptr,info_ptr);
    png_destroy_write_struct(&png_ptr,&info_ptr);
    png_destroy_info_struct(png_ptr,&info_ptr);

    if(!rename(tmp,file)) {
      if(chmod(file,S_IWUSR|S_IRUSR))
        E(0,"could not set permissions on \"%s\"!?\n",file);
      else
        ret=NOIR_SUCC; }}
  else
    E(0,"Unable to open \"%s\" for writing\n",tmp);

done:
  if(text_ptr)
    free(text_ptr);
  if(fp)
    fclose(fp);
  if(ret!=NOIR_SUCC)
    unlink(tmp);
  if(row_data)
    free(row_data);
  if(tmp)
    free(tmp);

  e->data=NULL;

  return ret; }



/**
 * save thumbnail(s) for a file.
 * @param e - the thumbnail descriptor
 *
 * wrapper for evidence_thumb_write_png().
 */
static inline int evidence_thumb_save_png(Evidence_Thumb *e) {
  int ret=NOIR_IGNORED;

  if(!e)
    return NOIR_FAIL;

  if(e->mtime.thumb!=e->mtime.file) {
    char       *p;
    const char *q;

    if(!(p=evidence_thumbnail_path(e->size,e->hashedname,EXT_PNG)))
      return NOIR_OOM;

    if((q=file_name(p))) {
      do { q--; } while((q>p)&&(*q!='/')); q++; }
    E(1,"updating thumbnail...\n  Image \"%s\"\n  thumb \"%s\" (%d, %dx%d)\n\n",
        e->origfile,q?q:p,e->size,e->w,e->h);

    if((e->w>0)&&(e->h>0))
      ret=evidence_thumb_write_png(p,e,NULL);

    if(e->thumbfile)
      free(e->thumbfile);

    e->thumbfile=p; }

  return ret; }



/*****************************************************************************/
/* LOADING */
/***********/



#  ifdef HAVE_IMLIB2



/**
 * flush unused fonts and images
 */
void thumbnail_imlib_gc(void) {
  int cache;

  cache=imlib_get_cache_size();
  imlib_set_cache_size(0);
  imlib_set_cache_size(cache);

  cache=imlib_get_font_cache_size();
  imlib_set_font_cache_size(0);
  imlib_set_font_cache_size(cache); }



/**
 * resize thumbnail to standard-size (using Imlib2).
 * @param e - the thumbnail descriptor
 */
Imlib_Image evidence_thumb_imlib_standardize(Evidence_Thumb *e) {
  Imlib_Image dst=NULL;
  int         dw,dh;
  int         sw=imlib_image_get_width(),sh=imlib_image_get_height();
  int         s=EVIDENCE_THUMB_BASE*e->size;

  if(sw>sh) {
    dw=s;
    dh=(s*sh)/sw; }
  else {
    dh=s;
    dw=(s*sw)/sh; }

  imlib_context_set_cliprect(0,0,dw,dh);

  if((dst=imlib_create_cropped_scaled_image(0,0,sw,sh,dw,dh))) {
    thumbnail_imlib_gc();  /* free tmp */

    E(2,"Source(%d,%d) New(%d,%d)\n",sw,sh,dw,dh);

    imlib_context_set_image(dst);
    imlib_context_set_anti_alias(1);
    imlib_image_set_has_alpha(1); }
  else
    thumbnail_imlib_gc();

  return dst; }


#  endif



/*****************************************************************************/



/**
 * create a new thumbnailer-struct
 * @return a new initialized struct
 */
Evidence_Thumbnailer *thumbnailer_new(void) {
  Evidence_Thumbnailer *t;

  if(!(t=malloc(sizeof(Evidence_Thumbnailer))))
    return NULL;
  memset(t,0,sizeof(Evidence_Thumbnailer));

  if(pthread_mutex_init(&t->qchg,NULL)) {
    E(0,"failed to initialize mutex: #%d - %s\n",errno,strerror(errno));
    free(t);
    return NULL; }

  t->ts=time(NULL);

#ifdef THUMBNAIL_IN_PROTECTED_MODE
  pthread_mutex_init(&t->task_wait,NULL);
  pthread_cond_init(&t->task_waiting,NULL);

  t->task_pid=0;
#endif

  return t; }



/**
 * stop a thumbnailer
 * @return an error-code
 * @param t  the thumbnailer to stop
 */
static int thumbnailer_cxl(Evidence_Thumbnailer *t) {
  int ret=NOIR_IGNORED;

  if(!t)
    return ret;

  pthread_mutex_lock(&t->qchg);

  if(t->flags==QUEUE_NONE)
    goto done;

  if(t->flags&QUEUE_SCANNING) {
#ifdef THUMBNAIL_IN_PROTECTED_MODE
    if(tn_param.protected&&t->task_pid) {
      int kr=kill(t->task_pid,SIGKILL);
      if(kr) {
        E(0,"error %d \"%s\" killing thumbnailer task\n",errno,strerror(errno));
        ret=NOIR_FAIL; }
      else {
        E(0,"sent SIGKILL to thumbnailer task for \"%s\"...\n",t->current?t->current->origfile:"???");
        ret=NOIR_PARTIAL; }
      goto done; }
#endif

    if(pthread_cancel(t->thumbnailer)) {
      E(0,"failed to send cancel to thumbnailer!?\n");
      ret=NOIR_FAIL;
      goto done; }
    else
      t->flags=QUEUE_ZOMBIE; }

  if(t->flags&QUEUE_ZOMBIE) {
    E(0,"sent cxl to thumbnailer @ %p/%lu/$%x...\n",t,(long)t->thumbnailer,t->flags);  /* ,t->current?t->current->origfile:"" */
    if((ret=pthread_join(t->thumbnailer,NULL))) {
      E(0,"failed to join thread (%ld:%d): #%d - %s\n",(long)t->thumbnailer,t->flags,ret,strerror(ret));
      ret=NOIR_PARTIAL; }
    else {
      E(3,"finished.\n");
      ret=NOIR_SUCC; }
    t->flags=QUEUE_NONE; }

 done:
  pthread_mutex_unlock(&t->qchg);
  return ret; }



/**
 * check whether a given thumbnailer has timed out.  if so, cancel it.
 * @return an error-code
 * @param t  the thumbnailer to check
 */
static int thumbnailer_gc(Evidence_Thumbnailer *t) {
  int ret=NOIR_IGNORED,cxl;

  if(!t)
    return NOIR_FAIL;

  /* have we been trying to thumbnail for a long time? */
  pthread_mutex_lock(&t->qchg);
  cxl=((t->flags&QUEUE_SCANNING)&&((t->ts+THUMBNAILER_TIMEOUT)<time(NULL)))
      ?TRUE
      :FALSE;
  pthread_mutex_unlock(&t->qchg);

  /* ...yes. cancel thumbnailer. */
  if(cxl) {
    E(2,"thumbnailer %p stuck, sending cancel (at %ld)...\n",t,time(NULL));
    if((ret=thumbnailer_cxl(t))==NOIR_SUCC) {
      E(2,"restarting thumbnailer...\n");
      /* restart here. if we don't, the thread will be down until the next
         time an item is enqueue to be thumbnailed (that also restarts the
         thread as needed). */
      thumbnailer_start(t); }}

  return ret; }



/**
 * destroy a thumbnailer (thread, struct, Q)
 * @param t  the thumbnailer
 */
Evidence_Thumbnailer *thumbnailer_dst(Evidence_Thumbnailer *t) {
  if(!t)
    return NULL;

/* thumbnailer_gc(t); */  /* check if thumbnailer is stuck; if so, kill it */
  thumbnailer_cxl(t);  /* check if thumbnailer is stuck; if so, kill it */

  if(t->flags&(QUEUE_ZOMBIE|QUEUE_SCANNING)) {
    int ret;
    E(1,"waiting for thumbnailer @ %p/%lu/$%x to finish...\n",t,(long)t->thumbnailer,t->flags);
    if((ret=pthread_join(t->thumbnailer,NULL)))
      E(0,"failed to join thread (%ld:%d): #%d - %s\n",(long)t->thumbnailer,t->flags,ret,strerror(ret));
    else
      t->flags=QUEUE_NONE; }

  pthread_mutex_destroy(&t->qchg);

#ifdef THUMBNAIL_IN_PROTECTED_MODE
  pthread_mutex_destroy(&t->task_wait);
  pthread_cond_destroy(&t->task_waiting);
#endif

#if 0
  if(t->current) {  /* put current item back into Q so it is free()'d properly */
    t->current->next=t->anchor;
    t->anchor=t->current;
    t->current=NULL; }
#endif

  if(t->anchor) {
    Evidence_Thumb *o;
    E(0,"thumbnailer still has items on-Q!?\n");
    while((o=t->anchor)) {
      t->anchor=o->next;
      evidence_thumb_free(o); }}
  free(t);

  return NULL; }



/**
 * enqueue an item to be thumbnailed
 * @return an error-code
 * @param t      the thumbnailer to use
 * @param thumb  the item to thumbnail
 */
int thumbnailer_enqueue(Evidence_Thumbnailer *t,Evidence_Thumb *thumb) {
  Evidence_Thumb *p;

  pthread_mutex_lock(&t->qchg);

  if(!(p=t->anchor))   /* append */
    t->anchor=thumb;
  else {
    while(p->next)
      p=p->next;
    p->next=thumb; }

  thumb->next=NULL;    /* leave this in. might be anything if item was
                          sent here from another queue! */

  pthread_mutex_unlock(&t->qchg);

  return NOIR_SUCC; }



/**
 * pop an item from the Q for a given thumbnailer
 * @return the tail item from the Q
 * @param the thumbnailer
 */
Evidence_Thumb *thumbnailer_dequeue(Evidence_Thumbnailer *t) {
  Evidence_Thumb *p=NULL,*e;

  pthread_mutex_lock(&t->qchg);

  if((e=t->anchor)) {
    while(e->next) {
      p=e;
      e=e->next; }

    if(p)
      p->next=NULL;
    else
      t->anchor=NULL; }

  pthread_mutex_unlock(&t->qchg);
  return e; }



/**
 * creates a new thumbnail (using a plugin, ePEG or Imlib2 or whatver).
 * @param thumb - the thumbnail descriptor
 * @return NOIR_SUCC for success, or NOIR_NOTSUPP if the format can not be thumbnailed.
 */
static int thumbnailer_generate(Evidence_Thumb *thumb) {
  char           *file=thumb->origfile;
  int             lock,ret=NOIR_NOTSUPP;
#  ifdef HAVE_IMLIB2
  Imlib_Image     im=NULL;
#  else
  void           *im=NULL;
#  endif

  if(!thumb)
    return NOIR_NODATA;

  if(!thumb->tp)
    return ret;

  if((lock=file_lock(file,O_RDONLY,0))<0)
    E(0,"failed to acquire advisory lock on \"%s\"...\n",file);

 try_thumbnail:

  thumbnail_plugin_load(thumb->tp);

  if(!thumb->tp->lib)
    goto done;
  if(!thumb->tp->make)
    goto done;



  /* we don't need no stinking imlib-wrapper!! */
  if(!(thumb->tp->can&TP_IMLIB)) {
    ret=thumb->tp->make(thumb->tp,thumb,&im);
    goto fallback; }

#  ifdef HAVE_IMLIB2
  /* try to use imlib2... */

  thumb->type=TT_PNG;

  IMLIB_ACQUIRE;

  ret=thumb->tp->make(thumb->tp,thumb,&im);

  if((ret==NOIR_SUCC)&&im) {   /* we got a valid image, save it as PNG */
    imlib_image_set_format("argb");
    imlib_context_set_image(im);
    thumb->w=imlib_image_get_width();
    thumb->h=imlib_image_get_height();
    thumb->alpha=imlib_image_has_alpha();
    thumb->data=imlib_image_get_data_for_reading_only();

    thumbnail_attribute_add(thumb,"Description","A preview for a file (\"thumbnail\").");

    evidence_thumb_save_png(thumb);  /* save as PNG */

    thumbnail_imlib_gc(); }

  IMLIB_RELEASE;

 fallback:
  if((ret!=NOIR_SUCC)&&(ret!=NOIR_NODATA)) {
    file_info        *fi=file_info_fetch(NULL,thumb->origfile);
    thumbnail_plugin *tp;
    const char       *tag,*mime,*q;

    if(!fi||!(mime=file_info_get_mime(fi))||
       (strncmp(mime,"image/",6)&&
        !((tag=file_info_get_tag(fi))&&(q=tag+strlen(tag))&&
          (!strcasecmp(q-7,".xcf.gz")||!strcasecmp(q-8,".xcf.bz2")))))
      E(3,"\"%s\" not an image, won't fall back on imlib2...\n",thumb->origfile);
    else if((tp=thumbnail_plugin_find_by_mime("image/",TP_ICONS))&&(tp!=thumb->tp)) {
      const char *n1,*n2;
      if(!(n1=file_name(thumb->tp->file))) n1=thumb->tp->file;
      if(!(n2=file_name(       tp->file))) n2=       tp->file;
      E(0,"thumbnailer %s failed (%d), falling back on %s...\n",n1,ret,n2);
      thumb->tp=tp;
      thumbnail_plugin_load(thumb->tp);
      goto try_thumbnail; }}

#  else
  fallback:
#  endif

 done:
  if(lock>=0)
    file_unlock(lock);

  E((ret==NOIR_SUCC)?2:1,"%s => %d\n",thumb->origfile,ret);

  return ret; }



#ifdef THUMBNAIL_IN_PROTECTED_MODE

/**
 * thumbnailing task exited; release associated thread
 */
int thumbnailer_protected_sigchld(void *data,int type,void *event) {
  Ecore_Event_Exe_Exit *e=(Ecore_Event_Exe_Exit *)event;
  Evidence_Thumbnailer *t=(Evidence_Thumbnailer *)data;

  t->task_ret=NOIR_FAIL;

  if(e->exited) {
    t->task_ret=e->exit_code;
    E(3,"thumbnailer %d for \"%s\" exited (%d).\n",(int)e->pid,t->task_thumbnail->origfile,t->task_ret); }
  else if(e->signalled) {
    if(e->exit_signal!=SIGKILL) {
      E(0,"**********\n");
      E(0,"thumbnailer %d for \"%s\" crashed: terminated by signal %d.\n",(int)e->pid,t->task_thumbnail->origfile,e->exit_signal);
      E(0,"**********\n\n"); }
    else
      E(5,"thumbnailer %d for \"%s\" killed (sig%d).\n",(int)e->pid,t->task_thumbnail->origfile,e->exit_signal); }
  else
    E(0,"thumbnailer %d for \"%s\" exited in some new and exotic way!?\n",(int)e->pid,t->task_thumbnail->origfile);

  t->task_pid=0;

  E(6,"signalling wakeup to thumbnailer-thread after processing \"%s\"...\n",t->task_thumbnail->origfile);
  pthread_cond_signal(&t->task_waiting);

  t->task_thumbnail=NULL;

  return 0; }



/**
 * execute thumbnailer in separate task so it won't take down evidence if it crashes.
 * @return an error-code
 * @param t      the thumbnailer
 * @param thumb  the thumbnail struct to create a thumbnail file for
 */
static inline int thumbnailer_protected_generate(Evidence_Thumbnailer *t,Evidence_Thumb *thumb) {
  pthread_mutex_lock(&t->task_wait);
  t->task_ret=NOIR_NOERRORCODE;
  t->task_thumbnail=thumb;

  t->sigchld_handler_handle=util_sigchld_handler_add(thumbnailer_protected_sigchld,t);

  if(!(t->task_pid=fork())) {          /* child */
    E(5,"Hi, I'm thumbnailer %d for \"%s\".\n",(int)getpid(),thumb->origfile);

    if(debug<0) {
      close(1);

      if(open("/dev/null",O_RDWR)!=1) {
        E(0,"could not open /dev/null!?\n");
        _exit(NOIR_FAIL); }
      else {
        dup2(1,2);
        setsid();

        signal(SIGCHLD,SIG_DFL);
        signal(SIGILL, SIG_DFL);
        signal(SIGABRT,SIG_DFL);
        signal(SIGFPE, SIG_DFL);
        signal(SIGSEGV,SIG_DFL);

        _exit(thumbnailer_generate(thumb)); }}

    else
      _exit(thumbnailer_generate(thumb)); }

  else if(t->task_pid<0) {             /* fail */
    E(0,"fork failed!?\n");
    t->task_ret=NOIR_IGNORED; }        /* do NOT write fail-info for this! */

  else {                               /* parent */
    while(t->task_ret==NOIR_NOERRORCODE) {
      E(6,"thumbnailing \"%s\" in separate task %d, calling thread going to sleep...\n",thumb->origfile,(int)t->task_pid);
      pthread_cond_wait(&t->task_waiting,&t->task_wait); }
    E(6,"caller waking up...\n"); }

  if(t->sigchld_handler_handle)
    util_sigchld_handler_del(t->sigchld_handler_handle);

  pthread_mutex_unlock(&t->task_wait);

  return t->task_ret; }

#endif



/**
 * the thumbnailing thread.
 * @param q - the queue of items to create thumbnails for
 */
static void *thumbnailer_generator(void *q) {
  Evidence_Thumbnailer *queue=(Evidence_Thumbnailer *)q;
  Evidence_Thumb       *thumb;
  int                   ret;

  E(3,"starts ------------------------\n");

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
  if(pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL))
    E(0,"couldn't set cancel-type, will not be able to cancel stuck thumbnailers the hard way...\n");

  while((thumb=thumbnailer_dequeue(queue))) {
    E(3,"thumbnailing \"%s\"...\n",thumb->origfile);

    /* if the thumbnailer-plugin hasn't been loaded yet, do it now --
       the generator might run in another task, if we load it there,
       it will get thrown away with the task. */
    thumbnail_plugin_load(thumb->tp);

    pthread_mutex_lock(&queue->qchg);
    queue->ts=time(NULL);
    queue->current=thumb;
    pthread_mutex_unlock(&queue->qchg);

#ifdef THUMBNAIL_IN_PROTECTED_MODE
    if(!tn_param.protected)
      ret=thumbnailer_generate(thumb);
    else if((ret=thumbnailer_protected_generate(queue,thumb))==NOIR_SUCC) {
      if(thumb->thumbfile) {
        free(thumb->thumbfile);
        thumb->thumbfile=NULL; }
      thumbnail_find_for(thumb->origfile,&thumb->thumbfile); }
#else
    ret=thumbnailer_generate(thumb);
#endif

    pthread_mutex_lock(&queue->qchg);
    queue->ts=0;
    pthread_mutex_unlock(&queue->qchg);

    if(ret==NOIR_SUCC) {
      E(3,"thumbnailed \"%s\"\n",thumb->origfile);
      thumbnailer_enqueue(thumbnails_complete,thumb); }
    else {
      if(ret!=NOIR_IGNORED)
        thumbnail_failinfo_save(thumb->origfile,thumb->tp?file_name(thumb->tp->file):NULL,NULL);
      evidence_thumb_free(thumb); }}

  queue->flags=(queue->flags|QUEUE_ZOMBIE)&(~QUEUE_SCANNING);
  E(3,"ends --------------------------\n");

  return NULL; }



/**
 * if any new thumbnails have arrived, dequeue the top one from the ready-Q,
 * add the thumbfile to the file-info instance, and return fi to model.
 *
 * called by the model to poll for new thumbnails.
 * @return the file-info struct that now has a thumbnail
 */
file_info *thumbnailer_pending(void) {
  Evidence_Thumb *thumb;
  file_info      *fi=NULL;

  if((thumb=thumbnailer_dequeue(thumbnails_complete))) {
    if(!(fi=file_info_fetch(NULL,thumb->origfile)))
      E(0,"cannot find file-info for \"%s\"!?\n",thumb->origfile);
    else {
      if(!(file_info_set_thumbfile(fi,thumb->thumbfile)))
        E(0,"no thumb for \"%s\".\n",thumb->origfile); }
    evidence_thumb_free(thumb); }

  return fi; }



/**
 * start thumbnailing thread (unless already running).
 * @param t - instance to start
 */
static int thumbnailer_start(Evidence_Thumbnailer *t) {
  int ret=NOIR_SUCC;

  pthread_mutex_lock(&t->qchg);

  if(t->flags&QUEUE_ZOMBIE) {
    pthread_join(t->thumbnailer,NULL);
    t->flags&=(~(QUEUE_ZOMBIE|QUEUE_SCANNING)); }

  if(t->flags&QUEUE_SCANNING) {
    E(4,"thumbnailer already active (top: \"%s\")...\n",t->anchor?t->anchor->origfile:"<empty>");
    ret=NOIR_IGNORED; }
  else if(pthread_create(&t->thumbnailer,NULL,thumbnailer_generator,t)) {
    E(0,"couldn't start thumbnailer!?\n");
    ret=NOIR_FAIL; }
  else {
    E(2,"starting thumbnailer (top: \"%s\")...\n",t->anchor?t->anchor->origfile:"<empty>");
    t->flags|=QUEUE_SCANNING; }

  t->ts=time(NULL);

  pthread_mutex_unlock(&t->qchg);

  return ret; }






/*****************************************************************************/



/**
 * fetches or creates a new thumbnail.
 * @return If an up-to-date thumbnail is found, NOIR_SUCC is returned and
 *         fi->thumbfile holds the name of that thumbnail.  Otherwise,
 *         NOIR_PARTIAL is returned and the thumbnail is enqueued for
 *         generation. If an error occurs, fi->thumbfile is NULL and an
 *         error-code is returned.
 * @param t - current theme
 * @param fi - current file
 * @param tp - what plugin to use to thumbnail this item
 */
static inline int thumbnail_enqueue_do(void *t,file_info *fi,thumbnail_plugin *tp) {
  Evidence_Thumb *thumb;
  int             ret=NOIR_SUCC;
  const char     *tag;

  if(!t) {
    noir_sess *ns=noir_sess_get_1st();
    if(!ns||!(t=noir_sess_get_theme(ns)))  /* if there are no more sessions, go home */
      return NOIR_IGNORED; }

  /* virtual item with no corresponding item in file-system? */
  if(file_info_is_pure_virtual(fi))
    return NOIR_IGNORED;

  if(!(tag=file_info_get_tag(fi)))
    return NOIR_FAIL;

  /* clear "have thumbnail", but keep old path into thumbnail cache, if any */
  file_info_clr_infobits(fi,FF_THUMB);

  /* create thumb-struct */
  if((ret=evidence_thumb_new(&thumb,t,tag,file_info_get_mime(fi)))!=NOIR_SUCC) {
    if(ret!=NOIR_NODATA)
      E(0,"evidence_thumb_new(%p,\"%s\") failed: %d\n\n",t,tag,ret);
    return ret; }

  if(thumb->type==TT_FOLDER)
    goto thumb_load;

  /* check for existing thumb */
  if((ret=evidence_thumb_read_file_attributes(thumb,FALSE))==NOIR_IGNORED) { /* thumb is in FAIL_ME */
    goto thumb_done; }
  else if(ret!=NOIR_SUCC) {  /* no thumb, or broken thumb (w/o info) */
    ret=NOIR_PARTIAL;
    goto thumb_do; }

  E(3,"thumb: %s (%d)\n\t%s\n\n",
    thumb->origfile?thumb->origfile:"",
    thumb->found_in,
    thumb->thumbfile?thumb->thumbfile:"<create thumbnail>");

  if(thumb->thumbfile) {
#  ifndef HAVE_IMLIB2
    /* don't have imlib.  nevermind whether existing thumb is up-to-date,
       we can't make a better one, anyway.  just load it. */
    if(1)
#  else
    /* check whether existing thumb is up-to-date */
    if(thumb->mtime.thumb==thumb->mtime.file)
#  endif
    /* try to load existing thumb */ {
    thumb_load:
      if(thumb->found_in==TD_FAIL_ME)
        ret=NOIR_FAIL;
      else if(!file_info_set_thumbfile(fi,thumb->thumbfile?thumb->thumbfile:thumb->origfile))
        ret=NOIR_OOM;
      goto thumb_done; }}

  E(3,"thumbnail \"%s\" for \"%s\" out of date! (%ld-%ld=%ld)\n",
    thumb->thumbfile,
    thumb->origfile,
    (long)thumb->mtime.file,
    (long)thumb->mtime.thumb,
    (long)(thumb->mtime.file-thumb->mtime.thumb));

  ret=NOIR_PARTIAL;

thumb_do:
  thumb->tp=tp;

  E(2,"(re-)creating thumbnail for \"%s\"...\n",thumb->origfile);

  if(thumbnailer_enqueue(thumbnailers,thumb)==NOIR_SUCC) {
    thumbnailer_start(thumbnailers); /* start thread if not already running */
    return ret; }  /* do not free thumb, the thread has it! */

thumb_done:
  evidence_thumb_free(thumb);
  return ret; }






/*****************************************************************************/
/* PUBLIC */
/**********/

/**
 * creates the directories we need for thumbnailing.
 * @return an error-code
 * @param setup  the directory containing the setup
 */
int thumbnail_init(const char *setup) {
  int          ret=NOIR_SUCC;
#  ifdef HAVE_IMLIB2
  int          i=0;
  struct stat  status;
  char        *p;
  thumbnaildir dirs[]={ TD_BASE, TD_FAIL, TD_NORMAL, TD_LARGE, TD_FAIL_ME };

  if(!(thumbnailers=thumbnailer_new()))
    return NOIR_FAIL;

  if(!(thumbnails_complete=thumbnailer_new()))
    return NOIR_FAIL;

  if(pthread_mutex_init(&imlib_mutex,NULL)) {
    E(0,"failed to initialize mutex: #%d - %s\n",errno,strerror(errno));
    return NOIR_FAIL; }

  do {
    if((p=evidence_thumbnail_path(dirs[i],NULL,NULL))) {
      if(stat(p,&status)) {
        if(mkdir(p,S_IRUSR|S_IWUSR|S_IXUSR)) {
          ret=NOIR_FAIL;
          E(0,"Unable to create \"%s\"...",p); }
        else /* FIXME ### */
          file_monitor_add(NULL,NOIR_MONITOR_DIR,p); }
      free(p); }
  } while(dirs[i++]!=TD_FAIL_ME);

  thumbnail_plugins_init(setup);

#ifdef THUMBNAIL_IN_PROTECTED_MODE
  prop_bag_prop_default(props_global,"thumbnail-protected-mode",   "1",0,1,1);
#endif
  prop_bag_prop_default(props_global,"thumbnail-mute",             "0",0,1,1);
  prop_bag_prop_default(props_global,"thumbnail-icons-skip",       "audio/*",0,0,0);
  prop_bag_prop_default(props_global,"thumbnail-mouseover-skip",   "",0,0,0);
  prop_bag_prop_default(props_global,"thumbnail-live-skip",        "image/*,application/x-font-ttf",0,0,0);

#ifdef THUMBNAIL_IN_PROTECTED_MODE
  prop_bag_prop_listen(props_global,LISTENER,"thumbnail-protected-mode",thumbnail_num_listener,TN_PROTECTED,     &tn_param);
#endif
  prop_bag_prop_listen(props_global,LISTENER,"thumbnail-mute",          thumbnail_num_listener,TN_MUTE,          &tn_param);
  prop_bag_prop_listen(props_global,LISTENER,"thumbnail-icons-skip",    thumbnail_str_listener,TN_ICONS_SKIP,    &tn_param);
  prop_bag_prop_listen(props_global,LISTENER,"thumbnail-mouseover-skip",thumbnail_str_listener,TN_MOUSEOVER_SKIP,&tn_param);
  prop_bag_prop_listen(props_global,LISTENER,"thumbnail-live-skip",     thumbnail_str_listener,TN_LIVE_SKIP,     &tn_param);

#  endif

  return ret; }



/**
 * find a thumbnail for a given file, if one exists
 * @return NOIR_SUCC for thumbnail found,
 *         NOIR_PARTIAL for "can't thumbnail this item",
 *         or another error-code for "this should never happen"
 * @param tag   the item for which we want a thumbnail
 * @param path  where to store the path to the thumbnail (if any), NULL for don't
 */
int thumbnail_find_for(const char *tag,char **path) {
  char           *p=NULL,*uri=NULL;
  int             i,ret=NOIR_IGNORED;
  Evidence_Thumb  e;
  thumbnaildir    dirs[]={ TD_LARGE, TD_NORMAL, TD_FAIL_ME };

  if(path)
    *path=NULL;

  if(!tag)
    return NOIR_FAIL;

  memset(&e,0,sizeof(Evidence_Thumb));

  if(!strncmp(tag,PROTOCOL_FILE,strlen(PROTOCOL_FILE)))
    uri=strdup(tag);
  else
    esprintf(&uri,PROTOCOL_FILE "%s",tag);

  if(!uri)
    return NOIR_OOM;

  e.uri=uri;
  if((ret=evidence_thumb_hash_name(&e))!=NOIR_SUCC)
    goto done;

  ret=NOIR_IGNORED;

  for(i=0;i<3;i++) {
    if(!(p=evidence_thumbnail_path(dirs[i],e.hashedname,EXT_JPG))) {
      ret=NOIR_OOM;
      goto done; }
    if(file_exists(p)) {
      ret=(dirs[i]==TD_FAIL_ME)?NOIR_PARTIAL:NOIR_SUCC;
      goto done; }
    free(p);

    if(!(p=evidence_thumbnail_path(dirs[i],e.hashedname,EXT_PNG))) {
      ret=NOIR_OOM;
      goto done; }
    if(file_exists(p)) {
      ret=(dirs[i]==TD_FAIL_ME)?NOIR_PARTIAL:NOIR_SUCC;
      goto done; }
    free(p);

    p=NULL; }

 done:
  if(e.hashedname) free(e.hashedname);
  if(uri)          free(uri);

  if(path)
    *path=p;
  else
    free(p);

  return ret; }



/**
 * deinitialize thumbnail sub-system.
 */
static int thumbnailers_gc(void) {
  Evidence_Thumbnailer *t,*t2;

  for(t=thumbnailers;t;t=t2) {
    t2=thumbnailers->next;
    thumbnailer_gc(t); }

  return NOIR_SUCC; }



/**
 * garbage-collect plugins
 * @return an error-code
 */
int thumbnail_lib_gc(void) {
  thumbnailers_gc();
  thumbnail_plugins_gc();
  return NOIR_SUCC; }



/**
 * delete all the thumbnails for the given source file.
 * @return an error-code
 * @param tag  the source-file
 */
int thumbnail_disk_gc_one(const char *tag) {
  char *p;
  int   ret=NOIR_IGNORED,tr;

  while(((tr=thumbnail_find_for(tag,&p))==NOIR_SUCC)||(tr==NOIR_PARTIAL)) {
    if(!p) {
      E(0,"\"%s\" not found!?  (%d)\n",tag,tr);
      ret=NOIR_NODATA; }
    else {
/*    ret=NOIR_SUCC; */
      E(1,"deleting \"%s\"...\n",p);
      if((ret=file_rm(p))!=NOIR_SUCC)
        E(0,"failed to delete \"%s\"!? (%d)",p,ret);
      free(p); }}

  return ret; }



/**
 * Using the URI tag stored in the png, check all the thumbnails to see if
 * their source file has been deleted; if so, delete the thumbnail as well.
 */
int thumbnail_disk_gc(void) {
  int            i=0,ret=NOIR_FAIL,s;
  thumbnaildir   dirs[]={ TD_LARGE, TD_NORMAL, TD_FAIL_ME };
  static char   *fprot=PROTOCOL_FILE;
  size_t         fplen=strlen(fprot);
  struct stat    st;
  char          *p,*fn,*ext;
  dir_entry     *d=NULL;
  void          *win=NULL,*box=NULL,*label=NULL,*bar=NULL;

  form_modeless_new(&win,&box);
  form_modeless_label_new(box,&label,"Expiring old thumbnails, please hold on...");
  form_modeless_bar_new(box,&bar);
  form_modeless_show(win);

  for(i=0;i<3;i++) {
    if(!(p=evidence_thumbnail_path(dirs[i],NULL,NULL)))
      continue;

    E(2,"garbage-collecting in \"%s\"...\n",p);

    { char       *msg=NULL;
      const char *q;

      q=file_name(p);
      esprintf(&msg,"Checking %s thumbnails in\n%s",q,p);
      if(msg) {
        form_modeless_label_update(label,msg);
        free(msg); }}

    if((d=file_getdir(p,FGD_NONE,NULL,NULL))) {
      Evidence_Thumb *e=NULL;
      int             thumb=0,thumbs=0;
      dir_entry      *c=d;

      while(c) {
        thumbs++;
        c=c->next; }

      if(!(e=(Evidence_Thumb *)malloc(sizeof(Evidence_Thumb)))) {
        ret=NOIR_OOM;
        goto tdgc_cd; }

      memset(e,0,sizeof(Evidence_Thumb));

      if(ret==NOIR_FAIL)
        ret=NOIR_SUCC;

      for(c=d;c;c=c->next) {
        thumb++;

        mainloop_once();

        fn=c->name;
        if(!strcmp(fn,".")||!strcmp(fn,".."))
          continue;

        if(!(ext=strrchr(fn,'.')))
          continue;

        if(!strcasecmp(ext,EXT_PNG))  /* PNG thumb */
          e->type=TT_PNG;
        else if(!strcasecmp(ext,EXT_JPG)) /* EPEG */
          e->type=TT_EPEG;
        else
          continue;  /* unknown thumb-type */

        if(e->hashedname)
          free(e->hashedname);
        e->hashedname=strdup(fn);
        e->hashedname[ext-fn]='\0';

        if(!(e->thumbfile=evidence_thumbnail_path(dirs[i],fn,NULL))) {
          ret=NOIR_OOM;
          continue; }

        if((s=evidence_thumb_read_file_attributes(e,TRUE))!=NOIR_SUCC)
          E(1,"could not get attributes for \"%s\" => %d\n",e->thumbfile,s);

        if(e->uri) {
          if(!(thumb%32)) {
            char *m=NULL;
            /* fn=(char *)file_name(e->uri); */
            esprintf(&m,"%d/%d",thumb,thumbs);
            form_modeless_bar_update(bar,m,((double)thumb)/thumbs);
            if(m)
              free(m); }

          if(!strncmp(e->uri,fprot,fplen)) {
            if(stat(&e->uri[fplen],&st)) {
              E(3,"  \"%s\" no longer exists...\n",&e->uri[fplen]);
              unlink(e->thumbfile); }}
          free(e->uri);
          e->uri=NULL; }

        else {
          E(0,"  \"%s\" is broken...\n",e->thumbfile);
          unlink(e->thumbfile); }

        if(e->thumbfile) {
          free(e->thumbfile);
          e->thumbfile=NULL; }}

      if(e)
        evidence_thumb_free(e);

    tdgc_cd:
      dir_entry_dst_all(d); }
    else
      E(2,"could not read an entries from \"%s\"...\n",p);
    free(p); }

  form_modeless_dst(&win);

  return ret; }



/**
 * react to a disk-op (copy, move (re-tag), delete thumbnail)
 * @return an error-code
 * @param t    the action-type
 * @param src  the source-file, or file to be deleted
 * @param dst  the destination file, or NULL
 */
int thumbnail_disk_op(noir_job_type t,const char *src,const char *dst) {
  int ret=NOIR_FAIL;
  char *s,*d,*su,*du;

  s=d=su=du=NULL;

  switch(t) {
    case NOIR_JOB_CP:
      break;
    case NOIR_JOB_MV:
      break;
    case NOIR_JOB_RM:
      break;
    default:
      E(0,"unknown op %d!?\n",t);
      ret=NOIR_NOTSUPP;
      break; }

/* done: */
  if(s)  free(s);
  if(d)  free(d);
  if(su) free(su);
  if(du) free(du);

  return ret; }



/**
 * add an attribute to a thumbnail
 * @return an error-code
 * @param e     the thumbnail
 * @param key_  the name of the attribute
 * @param val_  the contents of the attribute
 */
int thumbnail_attribute_add(Evidence_Thumb *e,const char *key_,const char *val_) {
  char **k=NULL,**v=NULL;
  char  *key=NULL,*val=NULL;

  if(!key_||!val_)
    return NOIR_FAIL;

  if(e->attr_allocd<(e->attr_used+1)) {
    int s=e->attr_used+16;

    if(!(k=realloc(e->attr_key,s*sizeof(char *))))
      return NOIR_OOM;
    e->attr_key=k;

    if(!(v=realloc(e->attr_val,s*sizeof(char *))))
      return NOIR_OOM;

    e->attr_val=v;
    e->attr_allocd=s; }

  if(!(key=strdup(key_)))
    return NOIR_OOM;
  if(val_&&!(val=strdup(val_))) {
    free(key);
    return NOIR_OOM; }

  e->attr_key[e->attr_used]=key;
  e->attr_val[e->attr_used]=val;
  e->attr_used++;

  return NOIR_SUCC; }



/**
 * create an application-specific thumbnail-failure entry in thumbnail cache
 * @return an error-code
 * @param fail      the item we failed on
 * @param software  a comment to save in the failure entry (e.g. the plugin that failed), or NULL
 * @param path      where to store the path to the newly-written item, or NULL
 */
static int thumbnail_failinfo_save(const char *fail,const char *software,char **path) {
  char           *uri=NULL;
  Evidence_Thumb  e;
  int             ret=NOIR_FAIL;
  char            pixel[4]={ 0,0,0,0 };
  time_t          mtime=0;
  file_info      *fi;

  if(path)
    *path=NULL;
  if(!fail)
    return NOIR_FAIL;

  if((fi=file_info_fetch(NULL,fail)))
    mtime=file_info_get_mtime(fi);
  if(mtime<=0) {
    E(0,"no mtime for \"%s\"!?\n",fail);
    mtime=0; }

  memset(&e,0,sizeof(e));

  esprintf(&uri,PROTOCOL_FILE "%s",fail);
  if(!uri)
    return NOIR_OOM;

  e.data=&pixel;
  e.uri=uri;
  e.origfile=(char *)fail;
  e.mtime.file=e.mtime.thumb=mtime;
  e.size=e.found_in=TD_FAIL_ME;
  e.type=TT_PNG;
  e.w=e.h=1;
  e.alpha=1;

  if((ret=evidence_thumb_hash_name(&e))!=NOIR_SUCC)
    goto done;
  if(!(e.thumbfile=evidence_thumbnail_path(TD_FAIL_ME,e.hashedname,EXT_PNG))) {
    ret=NOIR_OOM;
    goto done; }

  E(3,"writing fail-info \"%s\" for \"%s\" to \"%s\"...\n",software?software:"",fail,e.thumbfile);

  thumbnail_attribute_add(&e,"Description","evidence failed to create a preview for this item.");

  ret=evidence_thumb_write_png(e.thumbfile,&e,software);

 done:
  if(ret==NOIR_SUCC) {
    if(path)
      *path=e.thumbfile;
    else
      free(e.thumbfile); }

  if(e.uri)        free(e.uri);
  if(e.hashedname) free(e.hashedname);

  evidence_thumb_free_attributes(&e);

  return ret; }



/**
 * remove the failure-info for a thumbnail
 * @return an error-code
 * @param fail  path to fail-info. this is guaranteed to be free()d even in case of failure.
 */
#if 0
static int thumbnail_failinfo_remove(char *fail) {
  if(!fail)
    return NOIR_IGNORED;
  E(3,"deleting fail-info \"%s\"...\n",fail);
  unlink(fail);
  free(fail);
  return NOIR_SUCC; }
#endif



/**
 * deinitialize thumbnail sub-system.
 */
int thumbnail_exit(void) {
  Evidence_Thumbnailer *t;

  while((t=thumbnailers)) {
    thumbnailers=thumbnailers->next;
    thumbnailer_dst(t); }

  thumbnail_plugins_exit();

  return NOIR_SUCC; }



/**
 * enqueue a file for thumbnail-creation, if its format is supported.
 * @param fi - file_info for the item we want a thumbnail for
 * @return If we don't know how to thumbnail a file of the given type,
 * NOIR_TYPEMISMATCH is returned.
 * If THUMBNAIL_IMAGES_SMALLER_THAN is set to a non-zero value and the
 * original file's size exceeds that value, NOIR_IGNORED is returned.
 * If an error occurs, NOIR_FAIL is returned.
 * Otherwise, the response of thumbnail_enqueue_do() is returned.
 */
int thumbnail_enqueue(file_info *fi) {
  int               thumb=NOIR_SUCC;
  const char       *p,*q,*tag=file_info_get_tag(fi),*mime;
  thumbnail_plugin *tp=NULL;

  /* is it a folder? */
  if(((file_info_get_mode(fi)>=0)&&S_ISDIR(file_info_get_mode(fi)))||
     (file_info_get_mime(fi)&&!strcmp(file_info_get_mime(fi),MIME_FOLDER))) {
    file_annotation *fa;
    if(file_info_is_dotfolder(fi))
      return NOIR_IGNORED;
    if((fa=file_annotation_get(tag))&&
       (fa->lvl==FA_MEDIA)) { /* allow otherwise, may have "folder.jpg" */
      E(2,"\"%s\" ignored: removable media.\n",tag);
      return NOIR_IGNORED; }}



  /* do we have a MIME-type yet? */
  else {
    if(!(mime=file_info_get_mime(fi))) {
      if((p=file_get_mime(tag))) {
        E(0,"MIME(\"%s\") => %s\n",tag,p);
        if(!(mime=file_info_set_mime(fi,p)))
          thumb=NOIR_OOM; }
      else {
        E(0,"failed to get MIME-type for \"%s\"...\n",tag);
        thumb=NOIR_FAIL; }}

    if(thumb!=NOIR_SUCC)
      ;

    /* if it's a special type ("device", ...), don't try to thumbnail it */
    else if(!(p=strchr(mime,MIME_DELIMITER)))
      thumb=NOIR_TYPEMISMATCH;

    else if((tp=thumbnail_plugin_find_by_mime(mime,TP_ICONS))) {
      E(4,"plugin: %s\n",tp->file);
      thumb=NOIR_SUCC; }

    /* special cases: .xcf.gz, .xcf.bz2, application/x-font-ttf */
    else if((q=tag+strlen(tag))&&
            (!strcasecmp(q-7,".xcf.gz")||!strcasecmp(q-8,".xcf.bz2"))) {
      if((tp=thumbnail_plugin_find_by_mime("image/xcf",TP_ICONS))) {
        E(4,"plugin: %s\n",tp->file);
        thumb=NOIR_SUCC; }
      else {
        E(0,"ignoring compressed \"%s\"\n",tag);
        thumb=NOIR_IGNORED; }}

    else
      thumb=NOIR_NOTSUPP; }



  /* experimental threshold: don't try to thumbnail files larger than
     THUMBNAIL_IMAGES_SMALLER_THAN.  the idea is to not have imlib2
     try to look for compressed images in large gz/bz2 archives. */
  if(THUMBNAIL_IMAGES_SMALLER_THAN&&
     file_info_get_size(fi)>(THUMBNAIL_IMAGES_SMALLER_THAN)) {
    thumb=NOIR_IGNORED;
    E(2,"file \"%s\" not thumbnailed, too large...\n",tag); }

/* E(0,"thumb \"%s\"?  => %d\n",tag,thumb); */

  /* right, we've decided we actually want to try and thumbnail this. */
  if(thumb==NOIR_SUCC) {
    if(((thumb=thumbnail_enqueue_do(NULL,fi,tp))!=NOIR_SUCC)&&(thumb!=NOIR_PARTIAL))
      E(5,"thumbnail_enqueue_do(\"%s\") failed: #%d\n",tag,thumb); }

  return thumb; }



/*****************************************************************************/


#else
int thumbnail_lib_gc(void)           { return NOIR_SUCC; }

int thumbnail_attribute_add(Evidence_Thumb *e,const char *key,const char *val) { return NOIR_NOTSUPP; }
int thumbnail_find_for(const char *tag,char **path) { if(path) *path=NULL; (void)tag; return NOIR_NOTSUPP; }
int thumbnail_enqueue(file_info *fi) { (void)fi; return NOIR_NOTSUPP; }
int thumbnail_disk_gc(void)          { return NOIR_NOTSUPP; }
int thumbnail_disk_gc_one(const char *tag) { (void)tag; return NOIR_NOTSUPP; }
int thumbnail_disk_op(noir_job_type t,const char *src,const char *dst) { return NOIR_NOTSUPP; }
void thumbnail_imlib_gc(void)        { }
int thumbnail_init(const char *setup) { (void)setup; return NOIR_IGNORED; }
int thumbnail_exit(void)             { return NOIR_IGNORED; }
file_info *thumbnailer_pending(void) { return NULL; }

#  warning *************************************
#  warning ****** BUILDING WITHOUT LIBPNG ******
#  warning *** THERE WILL BE NO "THUMBNAILS" ***
#  warning *************************************
#endif



/*****************************************************************************/



/**
 * resize thumb-object to fit theme (after saving in standardized size).
 * @return theme-dependent on-screen size of thumbnail in w,h
 * @param t - the theme (needed for icon-dimensions and resize-mode)
 * @param w - source-width  (of newly-created or read-from-cache thumbnail)
 * @param h - source-height (of newly-created or read-from-cache thumbnail)
 *
 * useful values for icon-mode are {w,h}/h and {w,h}/w. {w,h} means
 * that both width and height of the thumbnail are scaled.  h as a
 * divisor indicates that the thumbnail should be as high as a normal
 * icon, no matter what width that will result in -- this is adequate
 * for smaller icon-sets, as it tends to prevent the thumbnails from
 * getting too small.  w as a divisor indicates that the thumbnail
 * should be as wide as a canonical icon, no matter what the height
 * will be.  the special value "auto" will scale the thumbnail to its
 * maximum within the bounding-box of the icon while retaining the
 * thumbnails aspect ratio, which in real terms means, "make thumbnail
 * as wide as an icon, unless that would mean it's higher than an
 * icon; in this case, make the thumbnail as high as an icon instead."
 *
 *     icon: 32x48
 *
 *       icon-mode    640x480 picture    320x960 picture
 *
 *       {w,h}/w      32x24 thumbnail    32x96 thumbnail
 *
 *       {w,h}/h      64x48 thumbnail    16x48 thumbnail
 *
 *       auto         32x24 thumbnail    16x48 thumbnail
 */
static int evidence_thumb_calc_size(void *t,int *w,int *h) {
  int         w_,h_,iw,ih;
  double      wf=-1,hf=-1;

  if(!t||!w||!h)
    return NOIR_FAIL;
  if(theme_get_iconsize(t,&iw,&ih)!=NOIR_SUCC)
    iw=ih=EVIDENCE_THUMB_BASE;

  w_=*w;
  h_=*h;

  switch(theme_get_iconmode(t)) {
    case IC_NONE:
      wf=hf=1;
      break;
    case IC_W:
      wf=(((float)w_)/iw);
      w_/=wf;
      break;
    case IC_H:
      hf=(((float)h_)/ih);
      h_/=hf;
      break;
    case IC_WH_W:
      wf=(((float)w_)/iw);
      w_/=wf; h_/=wf;
      break;
    case IC_WH_H:
      hf=(((float)h_)/ih);
      w_/=hf; h_/=hf;
      break;
    case IC_FILL:
      if((wf=(((float)w_)/iw))<(hf=(((float)h_)/ih))) {
        w_/=wf; h_/=wf; }
      else {
        w_/=hf; h_/=hf; }
      break;
    case IC_AUTO:
    default:   /* if in doubt, do the sensible thing */
      if((wf=(((float)w_)/iw))>(hf=(((float)h_)/ih))) {
        w_/=wf; h_/=wf; }
      else {
        w_/=hf; h_/=hf; }
      break; }

  if(w_<1) {
    w_=1;
    return NOIR_FAIL; }
  if(h_<1) {
    h_=1;
    return NOIR_FAIL; }

  if((*w==w_)&&(*h==h_))
    return NOIR_IGNORED;

  *w=w_;
  *h=h_;

  return NOIR_SUCC; }



/*****************************************************************************/



#ifdef HAVE_EVAS2



/**
 * try to load the image (using Evas).
 * @return an Evas_Image_Object, or NULL for failure
 * @param evas - the evas to add the object to if the load succeeds
 * @param file - the name of the image file on disk ("/home/me/foo.png")
 */
static Evas_Object *evidence_thumb_evas_image_load(Evas *evas,const char *file) {
  int          err;
  Evas_Object *result=evas_object_image_add(evas);

  if(result) {
    evas_object_image_file_set(result,file,NULL);
    if((err=evas_object_image_load_error_get(result))!=EVAS_LOAD_ERROR_NONE) {
      E(0,"error #%d loading \"%s\"...\n",err,file);
      evas_object_del(result);
      result=NULL; }}
  return result; }






/*****************************************************************************/



/**
 * load an existing thumbnail
 *
 * this convenience function is intended to be called from icon-factories
 * wishing to load an existing thumbnail.
 * @return an Evas_Image_Object containing the properly sized thumbnail, or NULL
 * @param t - the current theme (needed for sizing info)
 * @param evas - the Evas canvas to create the image-object on
 * @param fi - the file we want a thumbnail for
 */
Evas_Object *evas_thumbnail_dead(void *t,Evas *evas,file_info *fi) {
  Evas_Object    *result=NULL;

  if(!(file_info_get_infobits(fi)&FF_THUMB)||!file_info_get_thumbfile(fi))
    return NULL;

  if((result=evidence_thumb_evas_image_load(evas,file_info_get_thumbfile(fi)))) {
    int w_,h_;
    evas_object_image_size_get(result,&w_,&h_);
    evidence_thumb_calc_size(t,&w_,&h_);
    evas_object_resize(result,w_,h_);
    evas_object_image_fill_set(result,0,0,w_,h_); }

  return result; }



/**
 * create a "live" thumbnail
 *
 * this convenience function is intended to be called from icon-factories
 * wishing to create a "live" thumbnail.
 * @return an Evas_Image_Object containing the properly sized thumbnail, or NULL
 * @param t - the current theme (needed for sizing info)
 * @param evas - the Evas canvas to create the image-object on
 * @param fi - the file we want a thumbnail for
 * @param w - the suggested width
 * @param h - the suggested height
 */
Evas_Object *evas_thumbnail_live(void *t,Evas *evas,file_info *fi,int w,int h) {
  Evidence_Thumb    et;
  Evas_Object      *result=NULL;
  const char       *mime=NULL,*tag;
  thumbnail_plugin *tp;
  int               ret;
  thumbnail_plugin_capabilities what_we_want=(TP_VIDEO|TP_AUDIO); /* create with sound. this should be a global setting. ### FIXME */

E(0,"A\n");

  if(!fi)
    return NULL;

E(0,"B\n");

  tag=file_info_get_tag(fi);

E(0,"C\n");

  if(!(mime=file_info_get_mime(fi)))
    mime=file_get_mime(tag);

  if(!mime) {
    E(0,"couldn't determine MIME-type for \"%s\", no live thumbnail!\n",tag);
    return NULL; }
  if(!(tp=thumbnail_plugin_find_by_mime(mime,TP_LIVE))) {
    E(1,"couldn't find thumbnailer for MIME-type %s, no live thumbnail for \"%s\"...\n",mime,tag);
    return NULL; }

  thumbnail_plugin_load(tp);
  if(!tp->lib) {
    E(0,"couldn't load thumbnailer for MIME-type %s, no live thumbnail for \"%s\"...\n",mime,tag);
    return NULL; }

  if(!(tp->live)) {
    E(2,"thumbnailer for MIME-type %s doesn't know how to create live thumbnails (\"%s\")...\n",mime,tag);
    return NULL; }

E(0,"D  %s\n",tp->file);

  memset(&et,0,sizeof(Evidence_Thumb));

  et.theme=t;
  et.origfile=(char *)tag;
  et.type=TT_DK;
  et.size=TD_FAIL;
  et.found_in=TD_FAIL;
  et.tp=tp;
  et.data=evas;
  et.w=w;
  et.h=h;

  IMLIB_ACQUIRE;

  if(!(what_we_want&=(tp->can&(TP_VIDEO|TP_AUDIO))))
    E(0,"won't create empty live %s thumbnail for \"%s\"...\n",mime,tag);
  else if((ret=tp->live(tp,&et,what_we_want))==NOIR_SUCC) {
E(0,"E\n");
    result=et.data; }
  else
    E(0,"couldn't create live %s thumbnail for \"%s\": %d...\n",mime,tag,ret);

  IMLIB_RELEASE;

  return result; }



/**
 * create a "live" thumbnail if possible, otherwise return a static one
 *
 * this convenience function is intended to be called from icon-factories
 * wishing to create a "live" thumb, but settling for a static one if need be.
 * @return an Evas_Image_Object containing the properly sized thumbnail, or NULL
 * @param t - the current theme (needed for sizing info)
 * @param evas - the Evas canvas to create the image-object on
 * @param fi - the file we want a thumbnail for
 * @param w - the suggested width
 * @param h - the suggested height
 */
Evas_Object *evas_thumbnail_dead_or_alive(void *t,Evas *evas,file_info *fi,int w,int h) {
  Evas_Object *result=evas_thumbnail_live(t,evas,fi,w,h);
  if(result) {
    E(0,"have live thumb...\n");
    return result; }
  E(0,"don't have live thumb, falling back on static thumbnail...\n");
  return evas_thumbnail_dead(t,evas,fi); }



/**
 * create a new evas image-object from a given Imlib2-image
 * @return the new evas image-object, or NULL for failure
 * @param evas  the evas to create the image-object on
 * @param im    the Imlib2-image, or NULL for image currently in context
 */
Evas_Object *evas_object_from_imlib_image(Evas *evas,void *im) {
  Evas_Object *ret=NULL;
  int          w,h;

  if(im)
    imlib_context_set_image((Imlib_Image)im);

  w=imlib_image_get_width();
  h=imlib_image_get_height();

  if((w<=0)||(h<=0))
    return NULL;

  imlib_image_set_format("argb");

  if((ret=evas_object_image_add(evas))) {
    evas_object_image_alpha_set(ret,1);
    evas_object_image_size_set(ret,w,h);
    evas_object_image_data_copy_set(ret,imlib_image_get_data_for_reading_only());
    evas_object_resize(ret,w,h);
    evas_object_image_fill_set(ret,0,0,w,h);
    evas_object_image_data_update_add(ret,0,0,w,h);
    evas_object_color_set(ret,255,255,255, 255); }

  imlib_free_image();
  return ret; }



#endif



/*****************************************************************************/
