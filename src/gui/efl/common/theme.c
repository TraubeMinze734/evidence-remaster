/** @file  theme.c
 *  @brief theme/preferences handlers.
 */


#include "config.h"

#include "gui/common/session_private.h"

#include "theme.h"

#include "setup.h"
#include "util.h"          /* timeout_*() */
#include "mime.h"          /* MIME_* */
#ifdef HAVE_EVAS2
#  include "evas_macros.h"                      /* get_evas_from_iconlist() */
#  include "gui/gtk2/icon_view/gevas_bind.h"    /* ICON_LIST_GET_FROM_WIDGET() */
#  include "gui/gtk2/common/icon.h"             /* icon_init() */
#endif

#include "gui/gtk2/common/shelf.h"         /* CLIENT_SHELF */

#include "file.h"          /* file_exists() */
#include "file_lcl.h"      /* file_compose_path() */

#include "gui/common/icon_path.h"     /* icon_path_get() */

#include <stdlib.h>        /* atof() */
#include <string.h>
#include <dlfcn.h>
#include <ctype.h>
#include <sys/stat.h>      /* S_ISDIR() */



#define LISTENER "theme{evas}"

typedef enum { THEME_ODA, THEME_OFA, THEME_OAA } theme_tag_type;



typedef enum {
  FNT_NONE = 0,    /**< no fonts */
  FNT_TB   = 1,    /**< typebuffer-font */
  FNT_TT   = 2,    /**< tooltip-font */
  FNT_ICN  = 4,    /**< icon-label font */
  FNT_TP   = 8,    /**< text-path font */
  FNT_ALL  = 15    /**< all fonts */
} fonts;

typedef enum {
  PT_SB  =     1,  /**< scrollbar factory */
  PT_TT  =     2,  /**< tooltip factory */
  PT_TB  =     4,  /**< typebuffer engine */
  PT_RB  =     8,  /**< rubber-band factory */
  PT_BG  =  0x10,  /**< background engine */
  PT_IC  =  0x20,  /**< icon-factory */
  PT_SH  =  0x40,  /**< icon-view (background data) */
  PT_IV  =  0x80,  /**< shelf (background data) */
  PT_ID  = 0x100,  /**< idle-handler (optional, hence not part of PT_ALL) */
  PT_IX  = 0x200,  /**< extension */
  PT_ALL =  0xff   /**< "all of the above" */
} parse_theme;



#ifdef HAVE_EVAS2
inline int theme_font_chk(Evas *e,char *font,char *desc);
#endif
int theme_font_fallback(noir_theme *t,char *font,char *key,int got);






/*****************************************************************************/
/* helpers */



/**
 * theme_new - create a new theme struct.
 * @param ns - the session to create the theme for
 */
noir_theme *theme_new(noir_sess *ns) {
  noir_theme *t;
  if((t=malloc(sizeof(noir_theme)))) {
    memset(t,0,sizeof(noir_theme));
    if(!(t->data=prop_bag_new())) {
      free(t);
      return NULL; }
    t->session=ns;
    t->icon_mode=IC_AUTO;
#if 0
    t->icon_width=ICON_SIZE_IN_LIST;
    t->icon_height=(int)((ICON_SIZE_IN_LIST*3)/4);
#endif
#ifdef WITH_DOT_DIR
    t->tf|=TF_DOTDIR;
#endif
  }
  return t; }



#ifdef HAVE_EVAS2

/**
 * broken out of the listener.
 * @return an error-code
 */
static inline int theme_ani_get(noir_theme *t,const char *name,void **l,void (**f)(icon_list *,icon *),int (**d)(noir_theme *)) {
  void *fun=NULL,*lib=NULL;
  char *p=prop_get_string(prop_bag_prop_get(t->data,name)),*x=NULL;
  int   ret=NOIR_IGNORED;

  if((x=theme_ms(t,t->path,"engines","ia_",p,".so"))&&
     (lib=dlopen(x,RTLD_NOW))) {
    if(!(fun=dlsym(lib,"animation"))) {
      ret=NOIR_NOTFOUND;
      dlclose(lib);
      lib=NULL; }
    else
      ret=NOIR_SUCC;
    free(x); }
  else
    ret=NOIR_NOFILE;

  if(l)
    *l=lib;
  if(f)
    *f=fun;

  return ret; }

#endif



/*****************************************************************************/
/* idle-handlers  */



int theme_idle_start(noir_sess *ns) {
#ifdef HAVE_EVAS2
  Evas          *e=NULL;
  noir_theme    *t;
  icon_list     *il=NULL;
  noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
  noir_shelf    *sv=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);
  void          *h;

  if(!ns||!(t=((noir_theme *)noir_sess_get_theme(ns)))) {
    E(0,"(%ld): no theme!? (%p)\n",ns?ns->serial:0,ns?((noir_theme *)noir_sess_get_theme(ns)):NULL);
    return NOIR_FAIL; }

  if(sv&&(il=ICON_LIST_GET_FROM_WIDGET(sv->shelf_view))&&il->canvas) {
    e=get_evas_from_iconlist(il);
    if(t->idle_init_shelf&&(t->idle_freq_shelf=t->idle_init_shelf(il,&(t->idle_data_shelf),t)))
      t->idle_hndl_shelf=timeout_add(t->idle_freq_shelf,(int (*)(void *))t->idle_func_shelf,&(t->idle_data_shelf)); }

  if(iv&&(il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view))&&il->canvas) {
    e=get_evas_from_iconlist(il);
    if(t->idle_init_iconview&&(t->idle_freq_iconview=t->idle_init_iconview(il,&(t->idle_data_iconview),t)))
      t->idle_hndl_iconview=timeout_add(t->idle_freq_iconview,(int (*)(void *))t->idle_func_iconview,&(t->idle_data_iconview)); }

  if(il) {
    if(t->rubber_new&&t->rubber_dst) {
      h=t->rubber_new(il);
      t->rubber_dst(&h); }
    if(t->tb_set&&t->tb_dst) {
      h=t->tb_set(il,NULL,"",0);
      t->tb_dst(&h); } }
  else
    E(0,"could not initialize rubber-band and type-buffer listeners!?\n");

  if(e) {
    int    w=0,h=0;
    char  *file,*r,
          *fallback=NULL;
    fonts  got=FNT_NONE;

    /* default icon-size */
    if((file=icon_path_get(NULL,MIME_FOLDER))||
       (file=icon_path_get(NULL,"unknown"))) {
      Evas_Object *o=evas_object_image_add(e);
      evas_object_image_file_set(o,file,NULL);
      evas_object_image_size_get(o,&w,&h);
      evas_object_del(o); }
    t->icon_width=w?w:ICON_SIZE_IN_LIST;
    t->icon_height=h?h:(int)((ICON_SIZE_IN_LIST*3)/4);

    if(theme_font_chk(e,(r=prop_get_string(prop_bag_prop_get(t->data,"typebufferfont"))),"typebufferfont")==NOIR_SUCC) {
      fallback=r; got|=FNT_TB; }
    if(theme_font_chk(e,(r=prop_get_string(prop_bag_prop_get(t->data,"tooltipfont"))),"tooltipfont")==NOIR_SUCC) {
      fallback=r; got|=FNT_TT; }
    if(theme_font_chk(e,(r=prop_get_string(prop_bag_prop_get(t->data,"iconfont"))),"iconfont")==NOIR_SUCC) {
      fallback=r; got|=FNT_ICN; }
    if(theme_font_chk(e,(r=prop_get_string(prop_bag_prop_get(t->data,"textpathfont"))),"textpathfont")==NOIR_SUCC) {
      fallback=r; got|=FNT_TP; }

    if(got!=FNT_ALL) {
      if(fallback) {
        theme_font_fallback(t,fallback,"iconfont",      got&FNT_ICN);
        theme_font_fallback(t,fallback,"tooltipfont",   got&FNT_TT);
        theme_font_fallback(t,fallback,"textpathfont",  got&FNT_TP);
        theme_font_fallback(t,fallback,"typebufferfont",got&FNT_TB); }
      else
        E(0,"no fonts found, icon-list will look very broken...\n"); }}

  return NOIR_SUCC;
#else
  return NOIR_NOTSUPP;
#endif
}



int theme_idle_destall(noir_sess *ns) {
#ifdef HAVE_EVAS2
  Evas          *e=NULL;
  noir_theme    *t;
  icon_list     *il;
  int            remove,ret=NOIR_SUCC;
  noir_iconview *iv=(noir_iconview *)noir_sess_client_get_data(ns,CLIENT_ICONVIEW);
  noir_shelf    *sv=(noir_shelf *)noir_sess_client_get_data(ns,CLIENT_SHELF);

  if(!ns||!(t=((noir_theme *)noir_sess_get_theme(ns))))
    return NOIR_NODATA;

  /* destall shelf's idle-handler */
  if(sv&&sv->shelf_view&&(il=ICON_LIST_GET_FROM_WIDGET(sv->shelf_view))&&il->canvas) {
    e=get_evas_from_iconlist(il);
    if(t->idle_hndl_shelf) {
      timeout_remove(&t->idle_hndl_shelf);  /* remove idler even if cleanup doesn't work */
      remove=0;
      if(t->idle_exit_shelf) {
        if(t->idle_exit_shelf(&(t->idle_data_shelf)))
          remove=1; }
      else
        remove=1;
      if(remove) {
        E(2,"destalling idle-handler for shelf.\n");
        dlclose(t->idle_shelf_lib);
        t->idle_shelf_lib=NULL; }
      else {
        ret=NOIR_FAIL;
        E(0,"idle-handler for shelf would not destall!?\n"); }}}

#  if 0
  il=iv?ICON_LIST_GET_FROM_WIDGET(iv->icon_view):NULL;
  E(0,"B  %p, %p, %p, %p\n",iv,iv?iv->icon_view:NULL,il,il?il->canvas:NULL);
#  endif

  /* destall icon-view's idle-handler */
  if(iv&&iv->icon_view&&(il=ICON_LIST_GET_FROM_WIDGET(iv->icon_view))&&il->canvas) {
    e=get_evas_from_iconlist(il);
    if(t->idle_hndl_iconview) {
      timeout_remove(&t->idle_hndl_iconview);  /* remove idler even if cleanup doesn't work */
      remove=0;
      if(t->idle_exit_iconview) {
        if(t->idle_exit_iconview(&(t->idle_data_iconview))) {
          remove=1; }}
      else
        remove=1;
      if(remove) {
        E(2,"destalling idle-handler for icon-view.\n");
        dlclose(t->idle_iconview_lib);
        t->idle_iconview_lib=NULL; }
      else {
        ret=NOIR_FAIL;
        E(0,"idle-handler for icon-view would not destall!?\n"); }}}

  return ret;
#else
  return NOIR_NOTSUPP;
#endif
}



#ifdef HAVE_EVAS2

static int theme_idle_install(noir_sess *ns,char *k,char *v,size_t l) {
  noir_theme  *t=((noir_theme *)noir_sess_get_theme(ns));
  int          ret=NOIR_FAIL;
  char        *x;
  void        *lib;

  int        (*idle_init)(void *,void **,struct _noir_theme *);
  int        (*idle_exit)(void **);
  int        (*idle_func)(void **);

  if(((ret=dlmulti(v,x=theme_ms(t,t->path,"engines","id_",v,".so"),RTLD_NOW,
                   &lib,"!idle_func !idle_init ?idle_exit",
                   &idle_func,&idle_init,&idle_exit))==NOIR_SUCC)||
     (ret==NOIR_PARTIAL)) {

    if(theme_idle_destall(ns)==NOIR_FAIL)
      goto done;

    ret=NOIR_SUCC;

    if(!strcmp(k+l-5,"shelf")) {
      t->idle_shelf_lib=lib;
      t->idle_init_shelf=idle_init;
      t->idle_exit_shelf=idle_exit;
      t->idle_func_shelf=idle_func; }
    else if(!strcmp(k+l-8,"iconview")||!strcmp(k+l-9,"icon-view")||!strcmp(k+l-4,"icon")) {
      t->idle_iconview_lib=lib;
      t->idle_init_iconview=idle_init;
      t->idle_exit_iconview=idle_exit;
      t->idle_func_iconview=idle_func; }
    else {
      ret=NOIR_IGNORED;
      E(0,"unknown configuration key %s ignored...\n",k); }}

 done:
  if(x)
    free(x);

  return ret; }



/*****************************************************************************/



/**
 * listener for string properties
 * @return an error-code
 * @param b     the bag
 * @param e     the property
 * @param tag   the numeric tag of the property
 * @param data  the icon-list
 */
static int theme_string_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  char          *val=prop_get_string(e);
  noir_sess     *ns=(noir_sess *)data;
  noir_theme    *t=(noir_theme *)((noir_theme *)noir_sess_get_theme(ns));
  const char    *key=prop_get_key(e);

  if(val) {
    void (**ani)(icon_list *,icon *)=NULL;
    int  (**die)(struct _noir_theme *)=NULL;
    void  **lib=NULL;

    E(1,"%s <- %s\n",key,val);

    switch(tag) {
      case THEME_ODA: ani=&t->ani_open_folder; die=&t->ani_open_folder_die; lib=&t->ani_open_folder_lib; break;
      case THEME_OFA: ani=&t->ani_open_file;   die=&t->ani_open_file_die;   lib=&t->ani_open_file_lib;   break;
      case THEME_OAA: ani=&t->ani_open_app;    die=&t->ani_open_app_die;    lib=&t->ani_open_app_lib;    break;
      default: break; }

    if(!ani||!lib) {
      E(0,"unknown tag type \"%s\" (%d)!?\n",key,tag);
      return NOIR_NOTSUPP; }

    *ani=NULL;

    if(*die) {
      (*die)(t);
      *die=NULL; }

    if(*lib) {
      dlclose(*lib);
      *lib=NULL; }

    if(val&&*val&&strcmp(val,"none")) {
      int r=theme_ani_get(t,key,lib,ani,die);
      if(r!=NOIR_SUCC) {
        E(0,"could not get animation from \"%s\"!?  (%d)\n",val,r);
        return NOIR_FAIL; }}

    return NOIR_SUCC; }

  return NOIR_IGNORED; }



/*****************************************************************************/
/* setup theme */



static int theme_factory(noir_theme *t,char *k,char *x) {
  int   ret=0;
  char *v=NULL;

  switch(*k) {
    case 's':
      v=theme_ms(t,t->path,"engines","sb_",x,".so");
      if(v&&(((ret=dlmulti(x,v,RTLD_NOW,&t->sb_lib,
                           "!sb_new !sb_set !sb_dst ?sb_die",
                           &t->sb_new,&t->sb_set,&t->sb_dst,
                           &t->sb_die))==NOIR_SUCC)||(ret==NOIR_PARTIAL))) {
        prop_bag_prop_set(t->data,"scrollbar-factory",x);
        ret=PT_SB; }
      break;
    case 'b':
      v=theme_ms(t,t->path,"engines","bg_",x,".so");
      if(v&&(((ret=dlmulti(x,v,RTLD_NOW,&t->bg_lib,
                           "!bg_new !bg_scroll !bg_dst ?bg_upd ?bg_die",
                           &t->bg_new,&t->bg_scr,&t->bg_dst,&t->bg_upd,
                           &t->bg_die))==NOIR_SUCC)||(ret==NOIR_PARTIAL))) {
        prop_bag_prop_set(t->data,"background-factory",x);
        ret=PT_BG; }
      break;
    case 'r':
      v=theme_ms(t,t->path,"engines","rb_",x,".so");
      if(v&&(((ret=dlmulti(x,v,RTLD_NOW,&t->rubber_lib,
                           "!rubber_new !rubber_set !rubber_dst ?rubber_die",
                           &t->rubber_new,&t->rubber_set,&t->rubber_dst,
                           &t->rubber_die))==NOIR_SUCC)||(ret==NOIR_PARTIAL))) {
        prop_bag_prop_set(t->data,"rubberband-factory",x);
        ret=PT_RB; }
      break;
    case 'i':
      v=theme_ms(t,t->path,"engines","ic_",x,".so");
      if(v&&(((ret=dlmulti(x,v,RTLD_NOW,&t->icon_lib,
                           "!icon_els_create ?icon_set_state ?icon_die",
                           &t->icon_els_create,
                           &t->icon_set_state,
                           &t->icon_die))==NOIR_SUCC)||(ret==NOIR_PARTIAL))) {
        prop_bag_prop_set(t->data,"icon-factory",x);
        ret=PT_IC; }
      break;
    case 'e': {
      theme_area *a=malloc(sizeof(struct _theme_area));
      icon       *i=NULL;

      if(a) {
        memset(a,0,sizeof(struct _theme_area));
        if((i=malloc(sizeof(struct _icon)))) {
          memset(i,0,sizeof(struct _icon));
          v=theme_ms(t,t->path,"engines","ix_",x,".so");
          if(v&&(((ret=dlmulti(x,v,RTLD_NOW,&a->lib,
                               "!ext_upd !ext_dst !ext_evt ?ext_die",
                               &a->ext_upd,&a->ext_dst,&a->ext_evt,
                               &a->ext_die))==NOIR_SUCC)||(ret==NOIR_PARTIAL))) {
            ret=PT_IX;
            i->state=IE_DFL;
            i->name=strdup(x);
            i->label=strdup("");
            i->user1=(void *)a;
            i->next=t->areas;
            t->areas=i;
            prop_bag_prop_set(t->data,"extensions",x); }}

        if(!ret) {
          free(a);
          if(i)
            icon_dst(i); }}}
      break;
    case 't':
      switch(k[1]) {
        case 't':
        case 'o':
          v=theme_ms(t,t->path,"engines","tt_",x,".so");
          if(v&&(((ret=dlmulti(x,v,RTLD_NOW,&t->tt_lib,
                               "!tt_new !tt_dst ?tt_die",
                               &t->tt_new,&t->tt_dst,
                               &t->tt_die))==NOIR_SUCC)||(ret==NOIR_PARTIAL))) {
            prop_bag_prop_set(t->data,"tooltip-factory",x);
            ret=PT_TT; }
          break;
        case 'y':
        case 'b':
          v=theme_ms(t,t->path,"engines","tb_",x,".so");
          if(v&&((ret=dlmulti(x,v,RTLD_NOW,&t->tb_lib,
                              "!tb_redraw !tb_set !tb_dst ?tb_die",
                              &t->tb_redraw,&t->tb_set,&t->tb_dst,
                              &t->tb_die))==NOIR_SUCC)) {
            prop_bag_prop_set(t->data,"typebuffer-factory",x);
            ret=PT_TB; }
          break;
        default:
          E(0,"unknown engine-type \"%s\" ignored.\n",k); }
      break;
    default:
      E(0,"unknown engine-type \"%s\" ignored.\n",k); }

  if(v) {
    if(ret<=0) {
      if(ret!=NOIR_PARTIAL)
        E(0,"dlopen() failed: %s\n",dlerror());
      else
        E(0,"engine %s did not provide all required symbols, ignored...\n",v); }
    free(v); }

  return ret; }
#endif



static inline ic_mode theme_suss_iconmode(char *v) {
  ic_mode  ret=IC_AUTO;
  char    *p=&v[strlen(v)-1];

  if(!strcmp(v,"auto"))
    ret=IC_AUTO;
  else if(!strcmp(v,"fill"))
    ret=IC_FILL;
  else if(!strcmp(v,"w"))
    ret=IC_W;
  else if(!strcmp(v,"h"))
    ret=IC_H;
  else if(*p=='w')
    ret=IC_WH_W;
  else if(*p=='h')
    ret=IC_WH_H;
  else
    E(0,"unknown icon-mode \"%s\", falling back on \"auto\"...\n",v);
  return ret; }






/*****************************************************************************/



int theme_parse_line(void *data,char *k,char *v) {
  noir_sess  *ns=(noir_sess *)data;
  noir_theme *t=((noir_theme *)noir_sess_get_theme(ns));
  int         ret=0;
  char       *p,*f,*c=NULL;
  size_t      l;

  if(!t||!k||!v||!*k||!*v)
    return 0;

  if((p=strchr(k,':')))
    *p='\0';

  if(!(c=prop_canonize_key(k,FALSE,FALSE)))
    return 0;

  prop_canonize_key(k,TRUE,TRUE);

  l=strlen(k);
  if(!l) {
    if(c)
      free(c);
    return 0; }

  E(3,"  %s=%s\n",c,v);

#ifdef HAVE_EVAS2
  if((l>7)&&!strncmp(k,"idle",4))
    theme_idle_install(ns,c,v,strlen(c));

  else if(((l>6)&&(!strcmp(k+l-7,"factory")||!strcmp(k+l-6,"engine")))||
          !strcmp(k,"extensions"))
    ret=theme_factory(t,c,v);

  else if(!strcmp(k,"shelf")) {
    if((!strcmp(v,"NONE")&&(t->shelf_bg=strdup("")))||
       ((v[0]=='/')&&(t->shelf_bg=strdup(v)))||
       (t->shelf_bg=theme_ms(NULL,t->path,t->name,"",v,".bg.db"))||
       (t->shelf_bg=theme_ms(NULL,t->path,t->name,"",v,".jpg"))||
       (t->shelf_bg=theme_ms(NULL,t->path,t->name,"",v,".png"))||
       (t->shelf_bg=theme_ms(NULL,t->path,t->name,"",v,".eet"))||
       (t->shelf_bg=theme_ms(NULL,t->path,t->name,"",v,""))) {
      prop_bag_prop_set(t->data,c,t->shelf_bg);
      ret=PT_SH; }}

  else if(!strcmp(k,"iconview")) {
    if((!strcmp(v,"NONE")&&(t->iconview_bg=strdup("")))||
       ((v[0]=='/')&&(t->iconview_bg=strdup(v)))||
       (t->iconview_bg=theme_ms(NULL,t->path,t->name,"",v,".bg.db"))||
       (t->iconview_bg=theme_ms(NULL,t->path,t->name,"",v,".jpg"))||
       (t->iconview_bg=theme_ms(NULL,t->path,t->name,"",v,".png"))||
       (t->iconview_bg=theme_ms(NULL,t->path,t->name,"",v,".eet"))||
       (t->iconview_bg=theme_ms(NULL,t->path,t->name,"",v,""))) {
    prop_bag_prop_set(t->data,c,t->iconview_bg);
    ret=PT_IV; }}

  else if(!strcmp(k,"typebufferfont")||
          !strcmp(k,"tooltipfont")||
          !strcmp(k,"textpathfont")||
          !strcmp(k,"iconfont")) {
    f=theme_font_find(t,v);
    prop_bag_prop_set(t->data,c,f);
    free(f); }

  else if(!strcmp(k,"iconmode"))
    t->icon_mode=theme_suss_iconmode(v);

  else
#endif
  if(!strcmp(k,"iconset")&&!ns->iconset)
    ns->iconset=strdup(v);
  else {
    E(3,"adding unknown configuration tuple \"%s\",\"%s\" (\"%s\") to bag, hoping some plugin can make sense of it.\n",c,v,k);
    prop_bag_prop_set(t->data,c,v); }

  if(c)
    free(c);

  return ret; }






/*****************************************************************************/
/* read a theme-file and set the theme-properties accordingly */



static int theme_parse(noir_sess *ns) {
  noir_theme *t=((noir_theme *)noir_sess_get_theme(ns));
  int         ret=NOIR_FAIL;
  char       *cfg=NULL;
  int         done=0;
  static int  icon_mapping=0;

  size_t      l=strlen(home)+sizeof(DIR_USER)+sizeof(THEME_CFG)+1;

  if(!strcmp(t->name,"default")&&(cfg=malloc(l))) {
    snprintf(cfg,l,"%s/" DIR_USER "/" THEME_CFG,home);
    E(1,"looking for user-config in \"%s\"...\n",cfg);
    if(!file_exists(cfg)) {
      free(cfg);
      cfg=NULL; }}

  if(!cfg)
    cfg=theme_ms(NULL,t->path,t->name,"",THEME_CFG,"");

  if(cfg) {
    FILE *fh;
    char l[MIME_MAX_LEN];
    char *p,*p3;

    if((fh=fopen(cfg,"r"))) {
      while(fgets(l,MIME_MAX_LEN,fh)) {
        if(util_parse_line(l,&p,&p3,NULL,NULL)==2)
          done|=theme_parse_line(ns,p,p3); }

      fclose(fh);
#ifdef HAVE_EVAS2
      if((PT_ALL&done)!=PT_ALL) {
        char *fact[]={ "scrollbar", "tooltip", "typebuffer", "rubberband", "background", "icon", "shelf", "iconview" };
        int need=PT_ALL-(PT_ALL&done),facts=sizeof(fact)/sizeof(char *),i;
        /* E(0,"setup incomplete, need to set all engines! (got $%x, still need $%x)\n",done,need); */
        for(i=0;i<facts;i++) {
          if(need&(1<<i))
            E(0,"factory MISSING: %s\n",fact[i]);
          else
            E(1,"factory present: %s\n",fact[i]); }}
      else
#endif
      {
        char *is=NULL,*pr=NULL,*v=ns->iconset;

        if(v) {
          if(*v=='/')
            is=strdup(v);
          else if((pr=get_setup_file(DIR_ICONS))) {
            is=file_compose_path(pr,v,NULL);
            free(pr); }}

        if(is&&S_ISDIR(file_exists(is))) {
          ns->iconset=is;
          E(1,"adding path for icon-set \"%s\" {%s}...\n",v,is);
          paths_add(ns->iconset); }
        else {
          ns->iconset=NULL;
          E(3,"broken icon-set \"%s\" -- \"%s\" not found or not a directory; trying to fall back to default...\n",v,is?is:"<none>");
          if(is)
            free(is); }

        icon_path_init(ns->iconset);

        if(v)
          free(v);

        ret=NOIR_SUCC; }}

    /* this is uncouth. mappings should be per-theme. ### FIXME */
    if(!icon_mapping) {
      if(icon_path_load()!=NOIR_SUCC)
        E(0,"unable to load icons...\n");
      else {
        icon_init();
        icon_mapping++; }}

    free(cfg); }
  else
    E(0,"configuration for theme not found...\n");

  return ret; }






/*****************************************************************************/



/**
 * theme_init - initialize theme sub-system for a session
 * @param ns - the session
 * @param cwd - current working-directory
 */
int theme_init(noir_sess *ns,char *cwd) {
  char      *s,*n,*p;
  size_t     l;
  int        ret;

  if(!((noir_theme *)noir_sess_get_theme(ns)))
    return NOIR_FAIL;
  if(!((noir_theme *)noir_sess_get_theme(ns))->name)
    return NOIR_NODATA;

  n=((noir_theme *)noir_sess_get_theme(ns))->name;

  if(n[0]=='/') {
    l=strlen(n)-1;

    if(n[l]=='/')
      n[l--]='\0';
    p=strrchr(n,'/');
    ((noir_theme *)noir_sess_get_theme(ns))->path=n;
    if(p) {
      *(p++)='\0';
      ((noir_theme *)noir_sess_get_theme(ns))->name=p; }}

  else if((p=strrchr(n,'/'))) {
    if(!p[1]) {
      *p='\0';
      p=strrchr(n,'/'); }
    if(!p)
      ((noir_theme *)noir_sess_get_theme(ns))->path=cwd;
    else {
      l=strlen(cwd)+strlen(n)+3;
      *(p++)='\0';
      ((noir_theme *)noir_sess_get_theme(ns))->name=p;
      if((s=malloc(l))) {
        int ret;
        ret=snprintf(s,l,"%s/%s",cwd,n);
        if((ret>=0)&&(ret<=(signed int)l))
          ((noir_theme *)noir_sess_get_theme(ns))->path=s; }}}

  else if(!(((noir_theme *)noir_sess_get_theme(ns))->path=get_setup_file(DIR_THEME)))
    return NOIR_FAIL;

  E(1,"adding path \"%s\" {%s}...\n",((noir_theme *)noir_sess_get_theme(ns))->path,((noir_theme *)noir_sess_get_theme(ns))->name);
  paths_add(((noir_theme *)noir_sess_get_theme(ns))->path);

  E(1,"reading theme from \"%s\" in \"%s\"...\n",((noir_theme *)noir_sess_get_theme(ns))->name,((noir_theme *)noir_sess_get_theme(ns))->path);

#ifdef HAVE_EVAS2
  prop_bag_prop_listen(((noir_theme *)noir_sess_get_theme(ns))->data,LISTENER,"open-folder-animation",theme_string_listener,THEME_ODA,ns);
  prop_bag_prop_listen(((noir_theme *)noir_sess_get_theme(ns))->data,LISTENER,"open-file-animation",theme_string_listener,THEME_OFA,ns);
  prop_bag_prop_listen(((noir_theme *)noir_sess_get_theme(ns))->data,LISTENER,"open-app-animation",theme_string_listener,THEME_OAA,ns);
#endif

  ret=theme_parse(ns);

  return ret; }






/*****************************************************************************/



/**
 * de-initialize theme sub-system for a single canvas
 * @param il  the icon-list
 */
int theme_canvas_destall(icon_list *il) {
  noir_theme *t=(noir_theme *)il->theme;

  /* have destalled idlers before we removed the views */

  if(t->bg_dst&&il->bg)   { t->bg_dst(&(il->bg));   il->bg=NULL; }
  if(t->sb_dst&&il->sb_v) { t->sb_dst(&(il->sb_v)); il->sb_v=NULL; }
  if(t->sb_dst&&il->sb_h) { t->sb_dst(&(il->sb_h)); il->sb_h=NULL; }
  if(t->sb_die)           { t->sb_die(il->theme); }

  return NOIR_SUCC; }



/**
 * de-initialize theme sub-system for a session
 * @param ns - the session
 */
int theme_exit(noir_sess *ns) {
  noir_theme    *t=((noir_theme *)noir_sess_get_theme(ns));
  noir_prop     *e;
#ifdef HAVE_EVAS2
  icon          *i;
  theme_area    *a;

  prop_bag_prop_deaf(t->data,LISTENER,NULL,NULL);

  if(t) {
    /* have destalled idlers before we removed the views */
#else
  if(t) {
#endif

    if(t->rubber_die)       t->rubber_die(t);
    if(t->rubber_data)    { free(t->rubber_data); E(0,"rubberband-factory set up data, but has no cleanup routine. this is EVIL, and deprecated.\n"); }

    /* typebuffer */
    if(ns->tb)              typebuffer_dst(ns);  /* now in noir_sess_free() */
    if(t->tb_die)           t->tb_die(t);
    else if(t->tb_data)   { free(t->tb_data); E(0,"typebuffer-factory set up data, but has no cleanup routine. this is EVIL, and deprecated.\n"); }

    if(t->icon_die)         t->icon_die(t);
    else if(t->icon_data) { free(t->icon_data); E(0,"icon-factory set up data, but has no cleanup routine. this is EVIL, and deprecated.\n"); }

#if 0
/* destroyed in tooltip_exit() */
    if(ns->tt_evas)         t->tt_dst(&(ns->tt_evas));   /* tooltip */
#endif
    if(t->tt_die)           t->tt_die(t);
    else if(t->tt_data)   { free(t->tt_data); E(0,"tooltip-factory set up data, but has no cleanup routine. this is EVIL, and deprecated.\n"); }

    if(t->shelf_bg)         free(t->shelf_bg);
    if(t->iconview_bg)      free(t->iconview_bg);
    if(t->name)           { free(t->name); t->name=NULL; }

    /* do not free the following for now; paths_exit() will */
/*  if(t->path)             free(t->path); */

    if(ns->iconset) {
/*    free(ns->iconset); */   /* valgrind alert */
      ns->iconset=NULL; }

    while((e=prop_bag_prop_get_1st(t->data)))
      prop_bag_prop_dst(t->data,e);

#ifdef HAVE_EVAS2
    for(i=t->areas;i;i=i->next) {        /* dest extensions */
      a=i->user1;
      if(a) {
        if(a->ext_die)
          a->ext_die(t,i);
        else if(a->ext_dst)
          a->ext_dst(i);
        free(a); }
      i->user1=NULL; }
    icon_dst(t->areas);
#endif

    if(t->data)
      prop_bag_dst(t->data);

    memset(t,0,sizeof(noir_theme));
    free(t);
    noir_sess_set_theme(ns,NULL); }
  return NOIR_SUCC; }






/*****************************************************************************/
