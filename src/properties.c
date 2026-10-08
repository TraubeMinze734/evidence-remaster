/** @file  properties.c
 *  @brief handle properties (theme-elements)
 */

#include "evidence.h"
#include "setup.h"
#include "util.h"          /* esprintf() */
#include "file.h"
#include "properties.h"

#include <stdlib.h>        /* atof() */
#include <string.h>
#include <dlfcn.h>
#include <ctype.h>
#include <sys/stat.h>      /* S_ISDIR() */
#include <stdarg.h>        /* va_*() */



#  define NOIR_THEME_FLOAT_PRECISION 1000



struct _noir_prop_listener {
  property_listener            listener;
  const char                  *name;
  void                        *data;
  int                          tag;
  struct _noir_prop_listener  *next; };



struct _noir_prop {
  char                  *key,*canonized;
  noir_prop_type         type;
  char                  *ptr;
  long                   val,lo,hi,step;
  noir_prop_flags        flags;
  noir_prop_listener    *listeners;
  struct _noir_prop     *next; };



struct _noir_prop_bag {
  struct _noir_prop *anchor; };


static char *el_type[]={ "undefined", "integer", "float", "string", "pointer", "colour" };






/*****************************************************************************/
/* theme-elements ("properties") */



/**
 * split an RGB-value contained in a long into three ints
 * @param rgb  the RGB value combined into a long integer
 * @param r    where to store the red   fraction of the colour
 * @param g    where to store the green fraction of the colour
 * @param b    where to store the blue  fraction of the colour
 */
void long_to_rgb(long rgb,int *r,int *g,int *b) {
  *r=(rgb>>16)&0xff;
  *g=(rgb>>8)&0xff;
  *b=rgb&0xff; }



/**
 * canonize property name -- convert to lower case, weed out _-./
 * @return the canonized name
 * @param pn        a buffer holding the property's name
 * @param modify    if FALSE, new memory will be allocated for the canonized
 *                  name, otherwise, the handed-in buffer will be modified
 * @param compress  TRUE to eliminate separators, FALSE to normalize them
 */
char *prop_canonize_key(char *pn,int modify,int compress) {
  char *v,*r;

  if(modify)
    r=pn;
  else if(!(r=strdup(pn)))
    return NULL;

  v=r;
  while(*v) {
    if(strchr("-_/.",*v)) {
      if(compress)
        memmove(v,&v[1],strlen(v));
      else
        *(v++)='-'; }
    else {
      if(isupper(*v))
	*v=tolower(*v);
      v++; }}
  return r; }






/*****************************************************************************/



/**
 * remove a property from a list (if list is given and element present therein)
 * @param b  the bag of theme-elements
 * @param e  the element to destroy
 */
int prop_bag_prop_remove(noir_prop_bag *b,noir_prop *e) {
  if(b) {
    if(b->anchor==e)
      b->anchor=e->next;
    else {
      noir_prop *p,*c=b->anchor;
      do {
        p=c;
        c=c->next;
      } while(c&&(c!=e));
      if(c)    /* remove from list if even in there */
        p->next=c->next;
      else
        return NOIR_NOTFOUND; }
    return NOIR_SUCC; }
  else
    return NOIR_FAIL; }



/**
 * destruct a theme-element
 * @param e   the element to destroy
 */
static noir_prop *prop_dst(noir_prop *e) {
  noir_prop_listener *l;

  if(!e)
    return NULL;

  while(e->listeners) {
    l=e->listeners;
    e->listeners=e->listeners->next;
    free(l); }

  if(e->key)
    free(e->key);
  if(e->canonized)
    free(e->canonized);
  if(e->ptr&&(e->type==ET_STR))
    free(e->ptr);

  memset(e,0,sizeof(noir_prop));
  free(e);

  return NULL; }



/**
 * destruct a theme-element and remove it from a list (if list is given and element present therein)
 * @param b   the bag of theme-elements, or NULL
 * @param e   the element to destroy
 */
noir_prop *prop_bag_prop_dst(noir_prop_bag *b,noir_prop *e) {
  if(!e)
    return NULL;

  prop_bag_prop_remove(b,e);

  return prop_dst(e); }



/**
 * destroy a bag
 * @return NULL
 * @param bag  the bag to destroy
 */
noir_prop_bag *prop_bag_dst(noir_prop_bag *bag) {
  if(bag) {
    noir_prop *e;
    while((e=bag->anchor))
      prop_bag_prop_dst(bag,e);
    free(bag); }
  return NULL; }



/**
 * create a bag
 * @return the new bag, or NULL
 */
noir_prop_bag *prop_bag_new(void) {
  noir_prop_bag *bag=malloc(sizeof(noir_prop_bag));
  if(bag)
    memset(bag,0,sizeof(noir_prop_bag));
  return bag; }



/**
 * find theme-element with property-name "key"
 * @return a theme-element
 * @param b    the bag
 * @param key  the name of the property to look for
 */
noir_prop *prop_bag_prop_get(noir_prop_bag *b,const char *key) {
  noir_prop *e;
  char      *k;

  if(!b||!key||!(k=prop_canonize_key((char *)key,FALSE,TRUE)))
    return NULL;

  e=b->anchor;
  while(e) {
    if(!strcmp(k,e->canonized)) {
      free(k);
      return e; }
    e=e->next; }
  free(k);
  return NULL; }



/**
 * iterate over each element in a property bag
 * @return an error-code
 * @param b     the bag
 * @param fun   the function to apply to each property
 * @param data  user-supplied data for the function
 */
int prop_bag_prop_walk(noir_prop_bag *b,prop_bag_walker fun,void *data) {
  noir_prop *m=b?b->anchor:NULL,*n;
  if(m) {
    do {
      n=m->next;
      fun(m,data);
    } while((m=n));
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * get 1st property in bag
 * @return a property, or NULL
 * @param b  the bag
 */
noir_prop *prop_bag_prop_get_1st(noir_prop_bag *b) {
  return b?b->anchor:NULL; }



/**
 * get prop from either of a list of bags
 * @return the property, if found anywhere
 * @param cnt      the number of bags
 * @param bag_ret  where to store the (first) bag the property is found in, or NULL for don't
 * @param key      the key of the property
 * @param ...      'cnt' pointers to bags (which may be NULL)
 */
noir_prop *prop_bags_prop_get(int cnt,noir_prop_bag **bag_ret,const char *key,...) {
  va_list         ap;
  noir_prop_bag  *bag=NULL;
  noir_prop      *e=NULL;

  if((cnt<1)||!key||!strlen(key))
    goto fail;

  va_start(ap,key);
  while(cnt>0) {
    if(!(bag=va_arg(ap,noir_prop_bag *)))
      goto succ;
    if((e=prop_bag_prop_get(bag,key)))
      goto succ;
    cnt--; }

 succ:
  va_end(ap);

 fail:
  if(bag_ret)
    *bag_ret=bag;
  return e; }



/**
 * get the type of a theme-element
 * @return type
 * @param e  the theme-element we want to know the type of
 */
noir_prop_type prop_get_type(noir_prop *e) {
  return e?e->type:ET_NIL; }



/**
 * get the flags of a theme-element
 * @return flags
 * @param e  the theme-element we want the flags of
 */
noir_prop_flags prop_get_flags(noir_prop *e) {
  return e?e->flags:EF_NONE; }



/**
 * get the type of a theme-element as string
 * @return description for the type, this must not be free()d
 * @param e  the theme-element we want to know the type of
 */
const char *prop_get_type_as_string(noir_prop *e) {
  return e?el_type[e->type]:"no_property"; }



/**
 * describe a given theme-element type
 * @return textual description of the type (static memory caller mustn't free())
 * @param t  the type we want explained
 */
const char *prop_type_to_string(noir_prop_type t) {
  return el_type[t]; }



/**
 * return next prop in list
 * @return next prop, or NULL
 * @param e  the current prop
 */
noir_prop *prop_get_next(noir_prop *e) {
  return e?e->next:NULL; }



/**
 * return key of current prop
 * @return next prop, or NULL
 * @param e  the current prop
 */
const char *prop_get_key(noir_prop *e) {
  return e?e->key:NULL; }



/**
 * get pointer at the value of a theme-element
 * @return a pointer to either a pointer (types string, pointer) or a value
 * @param e  the prop
 */
void *prop_get_data(noir_prop *e) {
  return (e?(((e->type==ET_STR)||(e->type==ET_PTR))?((void *)&e->ptr)
                                                   :((void *)&e->val))
           :NULL); }



/**
 * get character-string value from theme-element
 * @return a pointer to a C-string
 * @param e  the prop
 */
char *prop_get_string(noir_prop *e) {
  return (e&&(e->type==ET_STR))?e->ptr:NULL; }



/**
 * get a description of a given prop
 * @return a buffer the caller must free()
 * @param e  the propr to describe
 */
char *prop_desc(noir_prop *e) {
  if(e) {
    estring *s=estring_new(512);
    estring_appendf(s,"%s: %s",e->key,prop_get_type_as_string(e));
    if(e->flags&EF_BOUNDS) {
      if(e->type==ET_INT)
        estring_appendf(s,", range %d..%d, step %d",e->lo,e->hi,e->step);
      else if(e->type==ET_FLT)
        estring_appendf(s,", range %d..%d, step %d",e->lo/NOIR_THEME_FLOAT_PRECISION,e->hi/NOIR_THEME_FLOAT_PRECISION,e->step/NOIR_THEME_FLOAT_PRECISION);
      else
        E(0,"bounds not expect on type %d prop \"%s\"...\n",e->type,e->key); }
    return estring_disown(s); }
  return NULL; }



/**
 * get long integer value from theme-element
 * @return a long integer value
 * @param e  the prop
 */
long prop_get_int(noir_prop *e) {
  return (e&&((e->type==ET_INT)||(e->type==ET_RGB)))?e->val:0L; }



/**
 * get floating-point value from theme-element
 * @return a floating-point value
 * @param e  the prop
 */
float prop_get_float(noir_prop *e) {
  return (e&&(e->type==ET_FLT))?((float)e->val/NOIR_THEME_FLOAT_PRECISION):0.0; }



/**
 * get integer-part of a floating-point value from theme-element
 * @return an integer value
 * @param e  the prop
 */
int prop_get_float_int(noir_prop *e) {
  return (e&&(e->type==ET_FLT))?((int)e->val/NOIR_THEME_FLOAT_PRECISION):0.0; }



/**
 * get decimal places of a floating-point value from theme-element
 * @return an integer value
 * @param e  the theme-element
 */
int prop_get_float_decimals(noir_prop *e) {
  return (e&&(e->type==ET_FLT))?((int)e->val%NOIR_THEME_FLOAT_PRECISION):0.0; }



/**
 * get individual R, G and B values from theme-element
 * @return an error-code
 * @param e    the prop
 * @param r    where to store the red   fraction of the colour
 * @param g    where to store the green fraction of the colour
 * @param b    where to store the blue  fraction of the colour
 */
int prop_get_rgb(noir_prop *e,int *r,int *g,int *b) {
  if(e&&((e->type==ET_RGB))) {
    long_to_rgb(e->val,r,g,b);
    return NOIR_SUCC; }
  return NOIR_FAIL; }



/**
 * get textual representation of the value of a theme-element
 * @return a newly allocated buffer containing the textual represenation of
 *         the property's value, or NULL
 * @param e    the prop
 */
char *prop_get_as_string(noir_prop *e) {
  char          *r=NULL;

  if(!e)
    return NULL;

  switch(e->type) {
    case ET_NIL:
      break;
    case ET_INT:
      esprintf(&r,"%ld",e->val); break;
    case ET_FLT:
/*    esprintf(&r,"%lf",prop_get_float(te,k)); break; */
      esprintf(&r,"%ld.%ld",prop_get_float_int(e),abs(prop_get_float_decimals(e))); break;
    case ET_STR:
      esprintf(&r,"%s",e->ptr); break;
    case ET_PTR:
      esprintf(&r,"%p",e->ptr); break;
    case ET_RGB:
      esprintf(&r,"#%06lx",e->val); break;
    default:
      E(0,"%s:unknown_type",e->key); break; }

  return r; }



/**
 * ensure that if a property has a range and/or a step-setting, the value adheres to this
 * called from prop_val() and prop_default()
 * @return an error-code
 * @param e  the theme-element
 */
static int prop_bound(noir_prop *e) {
  int  ret=NOIR_SUCC;
  long v;

  if(!e)
    return NOIR_FAIL;
  if(e->flags&EF_BOUNDS) {
    if((e->val<e->lo)) {
      E(0,"(\"%s\",%ld): value out of range; adjusted to %ld...\n",e->key,e->val,e->lo);
      e->val=e->lo; }
    else if((e->val>e->hi)) {
      E(0,"(\"%s\",%ld): value out of range; adjusted to %ld...\n",e->key,e->val,e->hi);
      e->val=e->hi; }
    else
      ret=NOIR_IGNORED; }
  else
    ret=NOIR_IGNORED;

  if(e->step) {
    v=((int)(e->val/e->step))*e->step;
    if(v!=e->val) {
      if(e->type==ET_FLT)
        E(1,"(\"%s\"): float value %f not a multiple of %f, adjusted to %f...\n",
          e->key,((double)e->val)/NOIR_THEME_FLOAT_PRECISION,((double)e->step)/NOIR_THEME_FLOAT_PRECISION,((double)v)/NOIR_THEME_FLOAT_PRECISION);
      else
        E(1,"(\"%s\"): integer value %ld not a multiple of %ld, adjusted to %ld...\n",
          e->key,e->val,e->step,v);
      ret=NOIR_SUCC;
      e->val=v; }}

  return ret; }



/**
 * make a property read-only (const).  if key and value are not NULL, create
 * new prop, then make it read-only.  otherwise, find key in bag, then make
 * the prop read-only.
 * @return an error-code
 * @param b  the bag in which property to set read-only
 * @param k  the key
 * @param v  the value
 */
int prop_bag_prop_const(noir_prop_bag *b,const char *k,const char *v) {
  noir_prop *e=NULL;

  if(!b||!k)
    return NOIR_FAIL;

  if(v) {
    int ret;
    if((ret=prop_bag_prop_set(b,k,v))!=NOIR_SUCC)
      return ret; }

  if(!(e=prop_bag_prop_get(b,k)))
    return NOIR_NOTFOUND;

  if(e->flags&EF_READONLY)
    return NOIR_IGNORED;

  e->flags|=EF_READONLY;
  return NOIR_SUCC; }



/**
 * set a property from a string. will guess type from value.
 * called from prop_bag_prop_add() and prop_bag_prop_set_set().
 * @return an error-code
 * @param e    the theme-element to set
 * @param val  the value (as C-string) to set the element to
 */
static int prop_val(noir_prop *e,const char *val) {
  const char         *l=NULL;
  long                v=0;
  noir_prop_type      ot=e->type;

  e->type=ET_NIL;

  if(e->flags&EF_READONLY) {
    E(0,"request to change read-only property \"%s\" ignored.\n",e->key);
    return NOIR_IGNORED; }

  if(!val)
    e->ptr=NULL;

  else if(!*val) {
    if(!(e->ptr=strdup(val)))
      return NOIR_OOM;
    e->type=ET_STR; }

  else if(val[0]=='#') {   /* rgb */
    if((v=strtol(&val[1],(char **)&l,16))<0) {
      v=0;
      E(0,"(\"%s\"): hexadecimal value less than zero, bound to zero...\n",val);
      l=val; }
    if(*l)
      E(0,"key \"%s\" -- value \"%s\" not a valid hexadecimal RGB value?\n",e->key,val);
    else {
      e->val=v;
      e->type=ET_RGB; }}

  else {
    const char *p=val;
    int   dp=0;
    int   nd=0;
    int   di=0;
    int   mi=0;

    while(*p) {    /* must ignore locale on this */
      if(!isdigit(*p)) {
        if(*p=='.')
          dp++;
        else if(*p=='-')
          mi++;
        else
          nd++; }
      else
        di++;
      p++; }

    if(di&&!nd&&(dp<2)&&(!mi||((mi==1)&&(val[0]=='-')))) {
      if(dp) {
        p=&val[mi];
        float f=(float)strtol((char *)p,(char **)&p,10),f2;

        if(p&&*p) {
          int t=1;
          di=strlen(++p);
          f2=strtol(p,NULL,10);
          while(di--)
            t*=10;
          f+=(f2/t); }

        e->val=(long)(f*(float)NOIR_THEME_FLOAT_PRECISION);
        if(mi)
          e->val=-e->val;
/*      E(0,"val: %ld, f: %f, s: %s|%s\n",e->val,f,val,p); */
        e->type=ET_FLT; }
      else {
        e->val=strtol(val,(char **)&l,10);
        e->type=ET_INT; }}

    else if(!(e->ptr=strdup(val)))
      return NOIR_OOM;

    else
      e->type=ET_STR; }

  if((ot==ET_FLT)&&(e->type==ET_INT)) {  /* automatic conversion int->flt */
    E(2,"automatic type conversion for \"%s\" int->flt: %ld -> %ld.0\n",e->key,e->val,e->val);
    e->type=ET_FLT;
    e->val*=NOIR_THEME_FLOAT_PRECISION; }

  prop_bound(e);

  e->flags|=EF_MODIFIED;

  return NOIR_SUCC; }



/**
 * add a property to a list. will not check if property already present.
 * called from prop_bag_prop_set(), prop_bag_prop_default() and
 * prop_bag_prop_listen().
 * @return an error-code
 * @param b    the bag
 * @param key  the name of the property to add
 * @param val  the value of the property to set using prop_val() magic
 */
static int prop_bag_prop_add(noir_prop_bag *b,const char *key,const char *val) {
  noir_prop *e;

  if(!key)
    return NOIR_NODATA;

  if(!(e=malloc(sizeof(noir_prop))))
    goto ret;
  memset(e,0,sizeof(noir_prop));

  if(!(e->key=prop_canonize_key((char *)key,FALSE,FALSE)))
    goto ret_free_nte;
  if(!(e->canonized=prop_canonize_key((char *)key,FALSE,TRUE))) {
    free(e->key);
    goto ret_free_nte; }

  if(!val)
    e->type=ET_NIL;
  else if(prop_val(e,val)==NOIR_OOM)
    goto ret_free_key;

  e->next=b->anchor;
  b->anchor=e;

  return NOIR_SUCC;

 ret_free_key:
  free(e->key);
 ret_free_nte:
  free(e);
 ret:
  return NOIR_OOM; }



/**
 * add a property to a list, or modify it if it already exists.
 * @return an error-code
 * @param b    the bag
 * @param k    the name of the property to add
 * @param val  the value of the property to set using prop_val() magic
 */
int prop_bag_prop_set(noir_prop_bag *b,const char *k,const char *val) {
  noir_prop           *e,f;
  noir_prop_listener  *l;
  int                  ret=NOIR_SUCC;

  if(!b||!k)
    return NOIR_NODATA;

  if(!(e=prop_bag_prop_get(b,k)))
    return prop_bag_prop_add(b,k,val);

  if(e->flags&EF_READONLY)
    return NOIR_IGNORED;

  if((e->ptr)&&(e->type==ET_STR)) {
    e->type=ET_NIL;
    free(e->ptr);
    e->ptr=NULL; }

  memcpy(&f,e,sizeof(noir_prop));
  if((ret=prop_val(&f,val))==NOIR_SUCC) {
    if((f.type==e->type)||(f.type==ET_NIL)||(e->type==ET_NIL)) {
/*    value=((e->type==ET_STR)||(e->type==ET_PTR))?((void *)&e->ptr):((void *)&e->val); */
      memcpy(e,&f,sizeof(noir_prop));
      for(l=e->listeners;l;l=l->next)
        if(e->key)
          l->listener(b,e,l->tag,l->data); }
    else
      E(0,"(\"%s\"): attempt to change variable type from %s to %s denied!\n",k,el_type[e->type],el_type[f.type]); }
  else
    E(0,"(\"%s\"): prop_val() failed: %d\n",k,ret);

  return ret; }



/**
 * set a default value for a property.
 * @return an error-code
 * @param b     the bag of theme-elements
 * @param key   the name of the property to add
 * @param val   the value of the property to set using prop_val() magic
 * @param lo    if the type warrants this: the minimum value
 * @param hi    if the type warrants this: the maximum value
 * @param step  if the type warrants this: a step-value, 0 for default (1)
 */
int prop_bag_prop_default(noir_prop_bag *b,const char *key,const char *val,float lo,float hi,float step) {
  int            ret=NOIR_SUCC;
  noir_prop     *e;

  if(!(e=prop_bag_prop_get(b,key))) {              /* prop doesn't exist yet */
    if((ret=prop_bag_prop_add(b,key,val))!=NOIR_SUCC)  /* try to add it */
      return ret;                                  /* ...failed */
    if(!(e=prop_bag_prop_get(b,key)))              /* get handle */
      return NOIR_FAIL; }

  if(e->type==ET_INT) {
    e->step=(int)step?step:1;
    e->flags|=EF_BOUNDS;
    e->lo=(int)lo;
    e->hi=(int)hi; }
  else if(e->type==ET_FLT) {
    e->step=(int)((step?step:1)*NOIR_THEME_FLOAT_PRECISION);
    e->flags|=EF_BOUNDS;
    e->lo=(int)(lo*NOIR_THEME_FLOAT_PRECISION);
    e->hi=(int)(hi*NOIR_THEME_FLOAT_PRECISION); }

  prop_bound(e);

  e->flags=e->flags&~EF_MODIFIED;

  return ret; }



/**
 * install a listener
 * @return an error-code
 * @param b         the bag
 * @param name      the name of the listener (usually that of the plugin the listener lives in)
 * @param key       the name of the property the listener is for
 * @param listener  the function to call back
 * @param tag       an arbitrary tag -- when one listener listens for changes
 *                  on several properties, this helps distinguishing them
 *                  (without having to resort to strcmp())
 * @param data      some user-data (often the session) to give the listener
 *                  when called back
 */
int prop_bag_prop_listen(noir_prop_bag *b,const char *name,const char *key,
                         property_listener listener,int tag,void *data) {
  noir_prop          *e;
  noir_prop_listener *l;

  if(!b||!key)
    return NOIR_NODATA;

  if(!(e=prop_bag_prop_get(b,key))) {
    int ret=prop_bag_prop_add(b,key,NULL);
    if(ret!=NOIR_SUCC) {
      E(0,"prop_bag_prop_add(\"%s\") failed: %d\n",key,ret);
      return ret; }
    if(!(e=prop_bag_prop_get(b,key))) {
      E(0,"list of theme-elements corrupted!?\n");
      return NOIR_FAIL; }}

  for(l=e->listeners;l;l=l->next)
    if(!strcmp(l->name,name)||(l->listener==listener)) {
      E(1,"%s is already listening for changes of %s...\n",
        name,key);
      return NOIR_IGNORED; }

  if(!(l=malloc(sizeof(noir_prop_listener))))
    return NOIR_OOM;

  E(2,"registering listener \"%s\" for \"%s\" (%d)...\n",name,key,e->type);

  memset(l,0,sizeof(noir_prop_listener));

  l->listener =listener;
  l->name     =name;
  l->data     =data;
  l->tag      =tag;
  l->next     =e->listeners;
  e->listeners=l;

  if(e->key&&(e->type!=ET_NIL))  /* call right on creation if el exists and has val */
    listener(b,e,tag,data);

  return NOIR_SUCC; }



/**
 * destall a listener (by name, or by listener-struct)
 * @return an error-code
 * @param b         the bag
 * @param name      name of the listener, or NULL if listener-function is given
 * @param key       the property the listener was for, or NULL for any
 * @param listener  the listener-function, or NULL if name is given
 */
int prop_bag_prop_deaf(noir_prop_bag *b,const char *name,const char *key,
                       property_listener listener) {
  noir_prop           *e;
  noir_prop_listener  *l,*p,*q;
  int                  ret=NOIR_NOTFOUND;

  if(!b)
    return NOIR_NODATA;

  if(key) {
    if(!(e=prop_bag_prop_get(b,key)))
      return NOIR_NOTFOUND; }
  else
    e=b->anchor;

  while(e) {
    for(p=NULL,l=e->listeners;l;) {
      if((name&&!strcmp(l->name,name))||(l->listener==listener)) {
        ret=NOIR_SUCC;
        if(!p)
          e->listeners=e->listeners->next;
        else
          p->next=l->next;
        q=l->next;
        memset(l,0,sizeof(noir_prop_listener));
        free(l);
        l=q; }
      else {
        p=l;
        l=l->next; }}

    if(key)
      e=NULL;
    else
      e=e->next; }

  return ret; }
