#ifndef NOIR_PROPS
#  define NOIR_PROPS



typedef enum _noir_prop_type {
  ET_NIL=0,
  ET_INT=1,
  ET_FLT=2,
  ET_STR=3,
  ET_PTR=4,
  ET_RGB=5
} noir_prop_type;



typedef enum _noir_prop_flags {
  EF_NONE=0,
  EF_BOUNDS=1,
  EF_MODIFIED=2,
  EF_READONLY=4
} noir_prop_flags;



typedef struct _noir_prop                 noir_prop;
typedef struct _noir_prop_bag             noir_prop_bag;
typedef struct _noir_prop_listener        noir_prop_listener;

typedef int (*property_listener)(noir_prop_bag *b,noir_prop *e,
                                 const int tag,void *data);

typedef void (*prop_bag_walker)(noir_prop *m,void *data);



extern noir_prop_bag *props_global;



void             long_to_rgb(long rgb,int *r,int *g,int *b); /* legacy */

noir_prop       *prop_bags_prop_get(int cnt,noir_prop_bag **bag_ret,const char *key,...);

noir_prop_bag   *prop_bag_dst(noir_prop_bag *bag);
noir_prop_bag   *prop_bag_new(void);
noir_prop       *prop_bag_prop_get_1st(noir_prop_bag *b);
int              prop_bag_prop_walk(noir_prop_bag *b,prop_bag_walker fun,void *data);
noir_prop       *prop_bag_prop_get(noir_prop_bag *b,const char *key);
noir_prop       *prop_bag_prop_dst(noir_prop_bag *b,noir_prop *e);
int              prop_bag_prop_remove(noir_prop_bag *b,noir_prop *e);
int              prop_bag_prop_const(noir_prop_bag *b,const char *k,const char *v);
int              prop_bag_prop_set(noir_prop_bag *b,const char *k,const char *val);
int              prop_bag_prop_default(noir_prop_bag *b,const char *key,const char *val,float lo,float hi,float step);
int              prop_bag_prop_listen(noir_prop_bag *b,const char *name,const char *key,property_listener listener,int tag,void *data);
int              prop_bag_prop_deaf(noir_prop_bag *b,const char *name,const char *key,property_listener listener);

noir_prop       *prop_get_next(noir_prop *e);
const char      *prop_get_key(noir_prop *e);
noir_prop_type   prop_get_type(noir_prop *e);
noir_prop_flags  prop_get_flags(noir_prop *e);
const char      *prop_get_type_as_string(noir_prop *e);
void            *prop_get_data(noir_prop *e);
char            *prop_get_string(noir_prop *e);
int              prop_get_rgb(noir_prop *e,int *r,int *g,int *b);
char            *prop_desc(noir_prop *e);
long             prop_get_int(noir_prop *e);
float            prop_get_float(noir_prop *e);
int              prop_get_float_int(noir_prop *e);
int              prop_get_float_decimals(noir_prop *e);
char            *prop_get_as_string(noir_prop *e);

const char      *prop_type_to_string(noir_prop_type t);
char            *prop_canonize_key(char *pn,int modify,int compress);

#endif
