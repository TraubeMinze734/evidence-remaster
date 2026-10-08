/** @file  sb_edb.c
 *  @brief load a scrollbar from an edb (deprecated ebits-format)
 *
 * ebits was the widget format for the first prototype of enlightenment DR17
 */

#include "config.h"

#if defined(HAVE_EDB)

#  include <string.h>
#  include <stdlib.h>
#  include <sys/stat.h>
#  include <Evas.h>
#  include <Ebits.h>

#  include "Ebits_private.h"
#  include "evidence.h"
#  include "evas_macros.h"
#  include "icon_list.h"
#  include "file.h"
#  include "theme.h"
#  include "setup.h"
#  include "errors.h"



#  undef NOIR_DEBUG

#  define EBITS_FILE_REDIRECT "%s:/images/%s"

#  define TROUGH_LAYER 0
#  define BAR_LAYER    400

#  define C_UP     "Scrollbar_Arrow1"
#  define C_DOWN   "Scrollbar_Arrow2"
#  define C_MISC   "Decoration"
#  define C_TROUGH "Scrollbar_Trough"
#  define C_BAR    "Scrollbar_Bar"



static inline Ebits_Object_Description desc_new(char *file) {
  Ebits_Object_Description d;

  if(!(d=malloc(sizeof(struct _Ebits_Object_Description))))
    return NULL;

  d=memset(d,0,sizeof(struct _Ebits_Object_Description));
  d->file=malloc(strlen(file)+1);
  strcpy(d->file,file);
  d->references=1;

  d->min.w=0;       d->min.h=0;
  d->max.w=999999;  d->max.h=999999;
  d->preferred.w=0; d->preferred.h=0;
  d->step.x=1;      d->step.y=1;

  return d; }



static void desc_old(Ebits_Object_Description d) {
  if(d) {
    if(d->file)
      free(d->file);
    if(d->bits) {
      int                           c=evas_list_count(d->bits);
      Ebits_Object_Bit_Description  bit;
      Evas_List                    *l;
      while(c) {
        c--;
        if((bit=(Ebits_Object_Bit_Description)evas_list_nth(d->bits,c))) {
          if((l=bit->state_description)) {
            int c2=evas_list_count(l);
            Ebits_State_Description desc;
            while(c2) {
              c2--;
              desc=(Ebits_State_Description)evas_list_nth(l,c2);
              if(desc->name)
                free(desc->name);
              if(desc->image)
                free(desc->image);
              free(desc); }
            evas_list_free(l); }

          if((l=bit->sync)) {
            int c2=evas_list_count(l);
            while(c2) {
              c2--;
              free(evas_list_nth(l,c2)); }
            evas_list_free(l); }

          if(bit->name)
            free(bit->name);
          if(bit->class)
            free(bit->class);
          if(bit->rel1.name)
            free(bit->rel1.name);
          if(bit->rel2.name)
            free(bit->rel2.name);

          if(bit->normal.image)
            free(bit->normal.image);
          if(bit->selected.image)
            free(bit->selected.image);
          if(bit->hilited.image)
            free(bit->hilited.image);
          if(bit->disabled.image)
            free(bit->disabled.image);
          if(bit->clicked.image)
            free(bit->clicked.image);

          free(bit); }}
      evas_list_free(d->bits); }
    free(d); }}



static inline Ebits_Object_Description desc_init(Ebits_Object_Description d,E_DB_File *db) {
  e_db_int_get(db,"/base/min/w",&(d->min.w));
  e_db_int_get(db,"/base/min/h",&(d->min.h));

  e_db_int_get(db,"/base/max/w",&(d->max.w));
  e_db_int_get(db,"/base/max/h",&(d->max.h));

  e_db_int_get(db,"/base/preferred/w",&(d->preferred.w));
  e_db_int_get(db,"/base/preferred/h",&(d->preferred.h));

  e_db_int_get(db,"/base/step/x",&(d->step.x));
  e_db_int_get(db,"/base/step/y",&(d->step.y));

  e_db_int_get(db,"/base/padding/l",&(d->padding.l));
  e_db_int_get(db,"/base/padding/r",&(d->padding.r));
  e_db_int_get(db,"/base/padding/t",&(d->padding.t));
  e_db_int_get(db,"/base/padding/b",&(d->padding.b));

  e_db_int_get(db,"/base/inset/l",&(d->inset.l));
  e_db_int_get(db,"/base/inset/r",&(d->inset.r));
  e_db_int_get(db,"/base/inset/t",&(d->inset.t));
  e_db_int_get(db,"/base/inset/b",&(d->inset.b));

  if(d->step.x<=0)
    d->step.x=1;
  if(d->step.y<=0)
    d->step.y=1;

  return d; }



static inline void bit_init(Ebits_Object_Bit_Description bit,Ebits_Object_Description d,int i,E_DB_File *db) {
       char                key[4096];
       int                 j,num_sync;
       float               f;

       snprintf(key,sizeof(key),"/bits/bit/%i/border/l",i);
       e_db_int_get(db,key,&(bit->border.l));

       snprintf(key,sizeof(key),"/bits/bit/%i/border/r",i);
       e_db_int_get(db,key,&(bit->border.r));

       snprintf(key,sizeof(key),"/bits/bit/%i/border/t",i);
       e_db_int_get(db,key,&(bit->border.t));

       snprintf(key,sizeof(key),"/bits/bit/%i/border/b",i);
       e_db_int_get(db,key,&(bit->border.b));



       snprintf(key,sizeof(key),"/bits/bit/%i/tile/w",i);
       e_db_int_get(db,key,(int *)&(bit->tile.w));

       snprintf(key,sizeof(key),"/bits/bit/%i/tile/h",i);
       e_db_int_get(db,key,(int *)&(bit->tile.h));



       snprintf(key,sizeof(key),"/bits/bit/%i/rel1/name",i);
       bit->rel1.name=e_db_str_get(db,key);

       snprintf(key,sizeof(key),"/bits/bit/%i/rel1/x",i);
       e_db_int_get(db,key,&(bit->rel1.x));

       snprintf(key,sizeof(key),"/bits/bit/%i/rel1/y",i);
       e_db_int_get(db,key,&(bit->rel1.y));

       snprintf(key,sizeof(key),"/bits/bit/%i/rel1/rx",i);
       e_db_float_get(db,key,&(f));
       bit->rel1.rx=f;

       snprintf(key,sizeof(key),"/bits/bit/%i/rel1/ry",i);
       e_db_float_get(db,key,&(f));
       bit->rel1.ry=f;

       snprintf(key,sizeof(key),"/bits/bit/%i/rel1/ax",i);
       e_db_int_get(db,key,&(bit->rel1.ax));

       snprintf(key,sizeof(key),"/bits/bit/%i/rel1/ay",i);
       e_db_int_get(db,key,&(bit->rel1.ay));



       snprintf(key,sizeof(key),"/bits/bit/%i/rel2/name",i);
       bit->rel2.name=e_db_str_get(db,key);

       snprintf(key,sizeof(key),"/bits/bit/%i/rel2/x",i);
       e_db_int_get(db,key,&(bit->rel2.x));

       snprintf(key,sizeof(key),"/bits/bit/%i/rel2/y",i);
       e_db_int_get(db,key,&(bit->rel2.y));

       snprintf(key,sizeof(key),"/bits/bit/%i/rel2/rx",i);
       e_db_float_get(db,key,&(f));
       bit->rel2.rx=f;

       snprintf(key,sizeof(key),"/bits/bit/%i/rel2/ry",i);
       e_db_float_get(db,key,&(f));
       bit->rel2.ry=f;

       snprintf(key,sizeof(key),"/bits/bit/%i/rel2/ax",i);
       e_db_int_get(db,key,&(bit->rel2.ax));

       snprintf(key,sizeof(key),"/bits/bit/%i/rel2/ay",i);
       e_db_int_get(db,key,&(bit->rel2.ay));



       snprintf(key,sizeof(key),"/bits/bit/%i/align/w",i);
       e_db_float_get(db,key,&(f));
       bit->align.w=f;

       snprintf(key,sizeof(key),"/bits/bit/%i/align/h",i);
       e_db_float_get(db,key,&(f));
       bit->align.h=f;

       snprintf(key,sizeof(key),"/bits/bit/%i/aspect/x",i);
       e_db_int_get(db,key,&(bit->aspect.x));

       snprintf(key,sizeof(key),"/bits/bit/%i/aspect/y",i);
       e_db_int_get(db,key,&(bit->aspect.y));

       snprintf(key,sizeof(key),"/bits/bit/%i/step/x",i);
       e_db_int_get(db,key,&(bit->step.x));

       snprintf(key,sizeof(key),"/bits/bit/%i/step/y",i);
       e_db_int_get(db,key,&(bit->step.y));

       snprintf(key,sizeof(key),"/bits/bit/%i/min/w",i);
       e_db_int_get(db,key,&(bit->min.w));

       snprintf(key,sizeof(key),"/bits/bit/%i/min/h",i);
       e_db_int_get(db,key,&(bit->min.h));

       snprintf(key,sizeof(key),"/bits/bit/%i/max/w",i);
       e_db_int_get(db,key,&(bit->max.w));

       snprintf(key,sizeof(key),"/bits/bit/%i/max/h",i);
       e_db_int_get(db,key,&(bit->max.h));

       num_sync=0;

       snprintf(key,sizeof(key),"/bits/bit/%i/sync/count",i);
       e_db_int_get(db,key,&(num_sync));

       if(bit->step.x<=0)
         bit->step.x=1;
       if(bit->step.y<=0)
         bit->step.y=1;

       for(j=0;j<num_sync;j++) {
         char               *s;

         snprintf(key,sizeof(key),"/bits/bit/%i/sync/%i",i,j);
         s=e_db_str_get(db,key);

         if(s)
           bit->sync=evas_list_append(bit->sync,s); }

       { Ebits_State_Description  state_d;
         Evas_List               *l;

         for(l=d->state_names;l;l=l->next) {
           state_d=malloc(sizeof(struct _Ebits_State_Description));
           state_d=memset(state_d,0,sizeof(struct _Ebits_State_Description));

	   fprintf(stderr,"  Accessing %s...\n",state_d->name);

           state_d->name=strdup(l->data);
           snprintf(key,sizeof(key),"/bits/bit/%i/%s/image",i,state_d->name);

           state_d->image=e_db_str_get(db,key);

           if(!state_d->image) {
             free(state_d->name);
             free(state_d);
             continue; }

           bit->state_description=evas_list_append(bit->state_description,state_d); }}

       d->bits=evas_list_append(d->bits,bit); }


static inline icon *my_ebits_load(Evas *evas,int what,char *file) {
  icon                     *icon;
  Ebits_Object_Description  d=NULL;
  E_DB_File                *db;
  int                       version=0;

  icon_el     *el=NULL,*up=NULL,*dn=NULL;
  Evas_Object *o;
  Evas_Coord   _x,_y,_w,_h;  /* return buffers for object_geo_get() */
  int          w_,h_,wt=0,ht=0;

  if(!(icon=malloc(sizeof(struct _icon)))) {
    E(0,"out of memory??\n");
    return NULL; }

  if(!(db=e_db_open_read(file))) {
    free(icon);
    E(0,"couldn't load scrollbar \"%s\"?  (e_db_open_read() failed)\n",file);
    return NULL; }

  memset(icon,0,sizeof(struct _icon));

  icon->label=strdup((what==IE_BAR)?"scrollbar":"trough");
  icon->file=NULL;
  icon->name=strdup(file);
  icon->index=-1;
  icon->state=IE_DFL|what;

  /* check version of file (and magic key/number) */
  e_db_int_get(db,"/type/bits",&(version));
  if((version!=1)||!(d=desc_new(file))) {
    if(d) desc_old(d);
    e_db_close(db);
    return NULL; }
  desc_init(d,db);

  { int                 i,num_states=0;
    char                key[4096];
    char               *name;

    e_db_int_get(db,"/base/states/count",&(num_states));
    for(i=0;i<num_states;i++) {
      snprintf(key,4096,"/base/states/%i/name",i);
      name=e_db_str_get(db,key);
#ifdef NOIR_DEBUG
      fprintf(stderr,"state: %s\n",name);
#endif
      d->state_names=evas_list_append(d->state_names,name); }}

  /* all the bits */
  {  int                 num_bits=0,i,layer=BAR_LAYER;

     e_db_int_get(db,"/bits/count",&(num_bits));
     for(i=0;i<num_bits;i++) {
       Ebits_Object_Bit_Description bit;
       char                key[4096];
       int                 j, subbit=0;

       bit=malloc(sizeof(struct _Ebits_Object_Bit_Description));
       bit=memset(bit,0,sizeof(struct _Ebits_Object_Bit_Description));

       snprintf(key,sizeof(key),"/bits/bit/%i/name",i);
       bit->name=e_db_str_get(db,key);

       snprintf(key,sizeof(key),"/bits/bit/%i/class",i);
       bit->class=e_db_str_get(db,key);

#ifdef NOIR_DEBUG
       fprintf(stderr," accessing %s [%s]\n",bit->name,bit->class);
#endif

       snprintf(key,sizeof(key),"/bits/bit/%i/color_class",i);
       bit->color_class=e_db_str_get(db,key);

       bit_init(bit,d,i,db);

       if(strcasecmp(bit->class,"Scrollbar_Trough")) {
#define MAX_STATES 5
#define PREFIX "/images/"
         char *fn[MAX_STATES],*f2[MAX_STATES],*buf;
         int num_states=0,k,s2[MAX_STATES],err;
         size_t l=0,l2;

         snprintf(key,sizeof(key),"/bits/bit/%i/normal/image",i);
         fn[0]=bit->normal.image=e_db_str_get(db,key);

         snprintf(key,sizeof(key),"/bits/bit/%i/selected/image",i);
         fn[1]=bit->selected.image=e_db_str_get(db,key);

         snprintf(key,sizeof(key),"/bits/bit/%i/hilited/image",i);
         fn[2]=bit->hilited.image=e_db_str_get(db,key);

         snprintf(key,sizeof(key),"/bits/bit/%i/disabled/image",i);
         fn[3]=bit->disabled.image=e_db_str_get(db,key);

         snprintf(key,sizeof(key),"/bits/bit/%i/clicked/image",i);
         fn[4]=bit->clicked.image=e_db_str_get(db,key);

	 for(j=0;j<MAX_STATES;j++)
	   f2[j]=NULL;

	 num_states=0;
	 for(j=0;j<MAX_STATES;j++) {
	   if(fn[j]) {
	     int found=0;
	     for(k=0;k<num_states;k++)
	       if(!strcmp(f2[k],fn[j])) {
		 found=1;
#ifdef NOIR_DEBUG
		 fprintf(stderr,"  added to  %s    state %3x\n",f2[k],1<<j);
#endif
		 s2[k]|=(1<<j); }
	     if(!found) {
	       f2[num_states]=fn[j];
	       s2[num_states]=1<<j;
#ifdef NOIR_DEBUG
	       fprintf(stderr,"  created %d-%s w/ state %3x\n",num_states+1,f2[num_states],s2[num_states]);
#endif
	       if((l2=strlen(f2[num_states]))>l)
		 l=l2;
	       num_states++; }}}

	 if(!l) {
	   desc_old(d);
	   return NULL; }

	 if(!(buf=malloc(l+=(strlen(PREFIX)+2)))) {
	   desc_old(d);
	   return NULL; }

	 for(k=0;k<num_states;k++) {
	   snprintf(buf,l,PREFIX "%s",f2[k]);

	   o=evas_object_image_add(evas);     /* image, if any */
	   evas_object_image_file_set(o,file,buf);
           if((err=evas_object_image_load_error_get(o))!=EVAS_LOAD_ERROR_NONE) {
             int ex=file_exists(file);
             ex=S_ISREG(ex)||S_ISLNK(ex);
             fprintf(stderr,"\nsb_edb: error #%d loading %s{%s}...%s\n\n",err,file,f2[k],
                     ex?"\n\tThe file was found, but could not be loaded...\n\tare you sure your evas-library was built to support edb-files?":""); }

	   evas_object_image_size_get(o,&w_,&h_);

           if(!strcasecmp(bit->class,"Decoration")) {
             layer=TROUGH_LAYER;
             subbit=SB_TRO; }
           else if(!strcasecmp(bit->class,"Scrollbar_Arrow1")) {
             subbit=SB_ARUP; }
           else if(!strcasecmp(bit->class,"Scrollbar_Arrow2")) {
	     subbit=SB_ARDN; }
           else if(!strcasecmp(bit->class,"Scrollbar_Bar"))
             subbit=SB_BAR;

#ifdef NOIR_DEBUG
	   fprintf(stderr,"  creating object for %s, state %3x, %dx%d\n",buf,s2[k],w_,h_);
#endif

	   evas_object_resize(o,w_,h_);
	   ICON_EL_PRE((subbit|s2[k]),layer,0,0);
	   if((subbit==SB_ARUP)||(subbit==SB_ARDN)) {
	     if(subbit==SB_ARUP) {
	       if(up)
		 up->next=el;
	       else
		 up=el; }
	     else {
	       if(dn)
		 dn->next=el;
	       else
		 dn=el; }
	     el=el->next; }}

	 free(buf); }}}

  e_db_close(db);

  icon->w=wt;
  icon->h=ht;

  if(up||dn) {
    if(up&&dn) {
      up->next=dn;
      dn->next=el;
      el=up; }
    else
      fprintf(stderr,"up=%p, dn=%p...\n",up,dn); }

  icon->els=el;

#ifdef NOIR_DEBUG
  fputs("\n",stderr);
#endif

  desc_old(d);

  return icon; }



/* scrollbar factory */



/**
 * create a scroll-bar
 * @return a handle (icon) for the newly-created scroll-bar, or NULL
 * @param t       the current theme of this window
 * @param canvas  the canvas to create the scroll-bar on
 */
void *sb_new(noir_theme *t,void *canvas) {
  icon        *bar=NULL,*trough=NULL;
  char        *v=NULL;

  if((v=theme_ms(t,t->path,"scrollbars","scroll_bar_","v",".bits.db"))) {
    bar=my_ebits_load(canvas,IE_BAR,v);
    free(v); }
  else {
    E(0,"couldn't find scrollbar in \"%s/%s/scroll_bar_v.bits.db\"...\n",t->path,t->name);
    return NULL; }

  if(!bar) {
    E(0,"failed to load scrollbar...\n");
    return NULL; }

  if((v=theme_ms(t,t->path,"scrollbars","scroll_base_","v",".bits.db"))) {
    trough=my_ebits_load(canvas,IE_TRO,v);
    free(v); }
  else {
    E(0,"couldn't find trough in \"%s/%s/scroll_base_v.bits.db\"...\n",t->path,t->name);
    return NULL; }

  if(!trough) {
    E(0,"failed to load trough...\n");
    return NULL; }

  if(trough&&trough->els) {
    icon_el *g=bar->els,*h;
    if((h=bar->els=trough->els)) {
      while(h->next)
	h=h->next;
      h->next=g; }
    trough->els=NULL;
    icon_dst(trough); }

  return (void *)bar; }



/**
 * update scroll-bar position and/or size
 * @param evas  the canvas the scrollbar is on
 * @param i     the scroll-bar object
 * @param cw    width (total)
 * @param ch    height (total)
 * @param vis   height (visible)
 * @param pos   y-offset (in pixels)
 */
void sb_set(void *evas,icon *i,int cw,int ch,int vis,int pos) {
/* Evas        *evas=get_evas_from_iconlist(il); */
  icon_el     *e;
  Evas_Object *o;
  int          x,y,w,h,z,q,vis2,ch2,up=0,dn=0;
  double       f;

#ifdef NOIR_DEBUG
  puts("\n\nsetting ----------------------------------------");
  printf("%03dx%03d, %03d:%03d\n",cw,ch,pos,vis);
#endif

  if(!i||!ch)
    return;

  if(vis>ch)
    ch=vis;

  { icon_el *g=i->els;
    if(g&&(g->state&SB_ARUP)) {
      up=g->h;
      if((g=g->next)&&(g->state&SB_ARDN))
        dn=g->h; }}

  z=up+dn;
  vis2=vis-z;
  if(vis2<1)
    vis2=1;
  f=((double)vis)/vis2;
  ch2=((double)ch*f);
  if(ch2<1)
    ch2=1;

#ifdef NOIR_DEBUG
  printf("%s (%s) %d<->%d, %d<->%d, %lf\n",i->label,i->name,vis,vis2,ch,ch2,f);
#endif

#define MAGIC 0
  i->x=q=cw-i->w-MAGIC;
  i->h=vis;
  e=i->els;

  while(e) {
    if(e->state&IE_DFL) {
      x=q+e->xo;   /* align at right border */
      w=e->w;

      if(e->state&SB_BAR) {
#ifdef NOIR_DEBUG
	printf("BA:");
#endif
	e->yo=y=(vis2*pos)/ch2+up+1;
	e->h=h=(int)((vis*vis)/ch2)+up; }
      else if(e->state&SB_TRO) {
#ifdef NOIR_DEBUG
	printf("BG:");
#endif
	e->yo=y=up+1;
	e->h=h=vis2; }
      else if(e->state&SB_ARUP) {
#ifdef NOIR_DEBUG
	printf("UP:");
#endif
	y=0;
	h=e->h; }
      else if(e->state&SB_ARDN) {
#ifdef NOIR_DEBUG
	printf("DN:");
#endif
	e->yo=y=vis-up-MAGIC;
	h=e->h; }
      else {
	x=y=w=h=0;
	printf("??: ($%x)",e->state); }

#ifdef NOIR_DEBUG
      printf(" \t %03d,%03d %02dx%02d\n",x,y,w,h);
#endif
#if 0
      printf("%d-%d-%d=%d=%d\n",vis,up,dn,vis-up-dn,vis2);
#endif

      o=(Evas_Object *)e->data;

      if(!(e->state&IE_ABS)) {
	evas_object_resize(o,w,h);
	evas_object_image_fill_set(o,0,0,w,h); }

      evas_object_move(o,x,y);
      evas_object_show(o); }

    e=e->next; }}



/**
 * destruct a scroll-bar
 * @param data  the handle of the scroll-bar to destroy
 */
void sb_dst(void **data) {
  if(data&&*data) {
    icon_dst(*data);
    *data=NULL; }}



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 */
int sb_die(noir_theme *theme) {
  return NOIR_IGNORED; }



#else
#  warning no libedb or no X11, not building the ebg-type scrollbar-plugin
#endif
