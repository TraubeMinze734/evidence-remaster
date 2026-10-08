#ifndef __META__
#  define __META__

#if defined(__cplusplus)
extern "C" {
#endif

#  include <time.h>

#  define META_MAX_LEN          4096
#  define TOOLTIP_PROVIDERS    "evidence.providers"

/* deprecated */
#  define meta_meta(a,b)        meta_in_group((a),(b),MY_NAME)
#  define meta_init_str(m,k,d)  meta_dflt((m),META_RW,META_STR,(k),(d),MY_NAME,"",prov)

#  define META_DFLT_GRP        "basic"


typedef enum {
  META_NA,                  /**< meta_set(), meta_chg(): keep old flags */
  META_RO,                  /**< read-only */
  META_RW,                  /**< read/write */
  META_DO,                  /**< read/write */
  META_EF                   /**< entry-field -- has no value, but can generate values */
} meta_flags;

typedef enum {
  META_OLD,                 /**< meta_set(), meta_chg(): keep old type */
  META_STR,                 /**< C-string, in locale-dependent encoding */
  META_UTF8,                /**< C-string, in UTF8-encoding */
  META_ACL,                 /**< Access Control List */
  META_INT,                 /**< integer */
  META_BIN,                 /**< binary data (reserved) */
  META_DBL,                 /**< double-precision floating-point value */
  META_FLT                  /**< floating-point value */
} meta_type;

typedef struct _noir_md       noir_md;
typedef struct _noir_provider noir_provider;
typedef struct _noir_meta     noir_meta;



#if 0
int            meta_lock(noir_md *);
int            meta_trylock(noir_md *);
int            meta_unlock(noir_md *);
#endif


typedef        void (*meta_walker)(noir_meta *m,void *data);
int            meta_walk(noir_md *,meta_walker fun,void *data);

noir_md       *meta_new(void);
int            meta_chg(noir_meta *,meta_flags,meta_type,const char *,const char *);
int            meta_set(noir_md *,meta_flags,meta_type,const char *,const char *,const char *,const char *,noir_provider *p);
int            meta_set_string(noir_meta *m,const char *s,meta_type t);
int            meta_add(noir_md *,meta_flags,meta_type,const char *,const char *,const char *,const char *,noir_provider *p);
int            meta_dflt(noir_md *,meta_flags,meta_type,const char *,const char *,const char *,const char *,noir_provider *p);

noir_meta     *meta_get_1st(noir_md *);
noir_meta     *meta_get_next(noir_meta *);
const char    *meta_get_key(noir_meta *);
const char    *meta_get_group(noir_meta *);
const char    *meta_get_desc(noir_meta *);
void          *meta_get_val(noir_meta *);
void          *meta_get_prvs(noir_meta *);
noir_provider *meta_get_provider(noir_meta *);
void          *meta_get_user1(noir_meta *);
long           meta_get_user2(noir_meta *);
void          *meta_get_user3(noir_meta *);
void          *meta_set_user1(noir_meta *,void *);
long           meta_set_user2(noir_meta *,long);
void          *meta_set_user3(noir_meta *,void *);
meta_flags     meta_get_flags(noir_meta *);
meta_flags     meta_set_flags(noir_meta *,meta_flags);
meta_type      meta_get_type(noir_meta *);

int            meta_dst(noir_md **);
int            meta_del(noir_md *,char *,char *);
int            meta_del_all(noir_md *);
int            meta_del_by_group(noir_md *,const char *);
int            meta_del_by_provider(noir_md *,noir_provider *);
int            meta_set_for_file(noir_md *,char *,noir_provider *);
int            meta_get_for_file(noir_md *,char *,noir_provider *);
int            meta_update(noir_md *,char *);
noir_meta     *meta_in_group(noir_md *,const char *,const char *);
noir_md       *meta_clone(noir_md *);

int            meta_handle_file(noir_provider *,char *,noir_md *);

int            meta_lib_gc(void);
void          *meta_lib_get(noir_provider *);

char          *meta_to_string(noir_md *);

int            meta_init(void);
int            meta_exit(void);

/** meta_internal - must be implemented by the hosting application! */
int            meta_internal(noir_md *,const char *,const char *);

void          *meta_provider_get_data(noir_provider *p);
void          *meta_provider_set_data(noir_provider *p,void *d);

#  ifdef __cplusplus
  }
#  endif

#endif
