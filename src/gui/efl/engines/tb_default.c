/** @file  tb_default.c
 *  @brief handles a "type-buffer" micro-shell
 *
 * properties:
 * - typebuffer-shadow-colour
 * - typebuffer-shadow-alpha
 * - typebuffer-text-alpha
 * - typebuffer-text-colour
 * - typebuffer-font-size
 * - typebuffer-font
 */

#include <string.h>
#include <stdlib.h>
#include <Evas.h>
#include <stdio.h>

#include "debug.h"
#include "theme.h"
#include "evas_macros.h"
#include "errors.h"
#include "properties.h"

#define LISTENER "typebuffer{default}"

typedef enum {
  TB_FONT,
  TB_SIZE,
  TB_TEXTCOL,
  TB_TEXTALPHA,
  TB_SHADOWCOL,
  TB_SHADOWALPHA } tb_tag;

#define SHADOW 2



typedef struct _tb_param {
  char *font;
  long  size;
  long  textcolour;
  int   textalpha;
  long  shadowcolour;
  int   shadowalpha;
} tb_param;



/* typebuffer factory */



/**
 * destall factory -- unregister listeners, free any resources etc.
 * @return an error-code
 * @param theme  the theme
 */
int tb_die(noir_theme *theme) {
  tb_param    *param;
  int          c=0;

  if(!theme)
    return NOIR_FAIL;

  if(prop_bag_prop_deaf(theme->data,LISTENER,NULL,NULL)==NOIR_SUCC)
    c++;
  if((param=theme->tb_data)) {
    if(param->font)
      free(param->font);
    memset(param,0,sizeof(param));
    free(param);
    theme->tb_data=NULL;
    c++; }

  return c?NOIR_SUCC:NOIR_IGNORED; }



/**
 * create an evas text-object
 * @return an evas-object, or NULL
 * @param evas  the canvas to create the object on
 * @param font  the font to use
 * @param fs    the font-size to use
 */
static inline Evas_Object *tb_to(Evas *evas,char *font,long fs) {
  Evas_Object *o=evas_object_text_add(evas);
  evas_object_text_font_set(o,font,fs);
  return o; }



/**
 * redraw typebuffer contents
 * @param il  the icon-list the typebuffer is associated with
 */
void tb_redraw(icon_list *il) {
  Evas *evas=get_evas_from_iconlist(il);
/* evas_damage_rectangle_add(evas,0,0,il->vis_w,il->vis_h); */
  evas_render(evas); }



/**
 * destruct a typebuffer
 * @param data  the handle of the typebuffer to destroy
 */
void tb_dst(void **data) {
  if(data&&*data) {
    icon_el_dst_all(*data);
    *data=NULL; }}



/**
 * listen for changes in our string-properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int tb_str_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  char          *val=prop_get_string(e);
  const char    *key=prop_get_key(e);
  tb_param      *param=(tb_param *)data;

  if(val) {
    E(1,"(%s::=\"%s\", %p)\n",key,val?val:"<null>",data);
    if(param->font)
      free(param->font);
    param->font=strdup(val);
    return NOIR_SUCC; }

  return NOIR_IGNORED; }



/**
 * listen for changes in our numeric properties.
 * @return NOIR_SUCC, or an error-code.
 * @param b   - the bag
 * @param e   - the property
 * @param tag - our index/tag for the property that has changed
 * @param data - our parameter-struct for this plugin, for faster access to the properties' values
 */
static int tb_num_listener(noir_prop_bag *b,noir_prop *e,int tag,void *data) {
  long           val=prop_get_int(e);
  const char    *key=prop_get_key(e);
  tb_param      *param=(tb_param *)data;

  switch(tag) {
    case TB_SIZE:
      param->size=val; break;
    case TB_TEXTCOL:
      param->textcolour=val; break;
    case TB_TEXTALPHA:
      param->textalpha=val; break;
    case TB_SHADOWCOL:
      param->shadowcolour=val; break;
    case TB_SHADOWALPHA:
      param->shadowalpha=val; break;
    default:
      E(0,"unknown tag %d (\"%s\")!?\n",tag,key?key:"<none>");
      return NOIR_FAIL; }

  E(1,"(%s::=%ld, %p)\n",key,val,data);

  return NOIR_SUCC; }



/**
 * set typebuffer contents
 * @return a handle for this typebuffer (here: chains of icon-elements)
 * @param il      the icon-list the typebuffer is associated with
 * @param iel     handle of the previous typebuffer
 * @param msg     the text to display
 * @param cursor  cursor-index (on character 0..n)
 */
icon_el *tb_set(icon_list *il,void *iel,char *msg,int cursor) {
  noir_theme  *t=(noir_theme  *)il->theme;
  Evas        *evas=get_evas_from_iconlist(il);
  icon_el     *el=(icon_el *)iel,*e2;

  tb_param    *param;

  icon        *icon=NULL;
  Evas_Coord   _w,_h,_x,_y;
  int          w_,h_,wt=0,ht=0;

  char        *m,*m2;

  if(!msg)
    return NULL;
  if(!(m2=strdup(msg)))
    return NULL;



  /***************************************************************************/
  /* get theme-settings */

  if(!(param=t->tb_data)) {
    noir_theme     *t=(noir_theme *)il->theme;
    noir_prop_bag  *te=t->data;

    if(!(t->tb_data=param=malloc(sizeof(tb_param))))
      return NULL;

    memset(param,0,sizeof(tb_param));

    param->font=strdup("arial");
    param->size=32;
    param->textcolour=0xffffff;
    param->textalpha=119;
    param->shadowcolour=0L;
    param->shadowalpha=119;

    prop_bag_prop_default(te,"typebuffer-shadow-alpha","119",0,255,1);
    prop_bag_prop_default(te,"typebuffer-text-alpha","119",0,255,1);
    prop_bag_prop_default(te,"typebuffer-font-size", "32", 6,128,1);

    prop_bag_prop_listen(te,LISTENER,"typebuffer-shadow-colour",tb_num_listener, TB_SHADOWCOL,   param);
    prop_bag_prop_listen(te,LISTENER,"typebuffer-shadow-alpha", tb_num_listener, TB_SHADOWALPHA, param);

    prop_bag_prop_listen(te,LISTENER,"typebuffer-text-alpha",   tb_num_listener, TB_TEXTALPHA,   param);
    prop_bag_prop_listen(te,LISTENER,"typebuffer-text-colour",  tb_num_listener, TB_TEXTCOL,     param);

    prop_bag_prop_listen(te,LISTENER,"typebuffer-font-size",    tb_num_listener, TB_SIZE, param);
    prop_bag_prop_listen(te,LISTENER,"typebuffer-font",         tb_str_listener,TB_FONT, param); }



  /***************************************************************************/
  /* clear previous tb */

  if(el) {
    void *v=el;
    tb_dst(&v); }
  e2=el=NULL;



  /***************************************************************************/
  /* create new tb */

  if(il&&il->vis_w&&t) {
    Evas_Object *o,*o2;
    double       f,th=0;
    size_t       l,l2;
    long         col=0;

    m=m2;

    while((l=strlen(m))) {
      o=tb_to(evas,param->font,param->size);

      evas_object_text_text_set(o,m);
      evas_object_geometry_get(o,&_x,&_y,&_w,&_h);

      /* wrapping needed?  find best point! */
      if((f=(_w/((double)il->vis_w)))>1.0) {
        char s;

        l=((float)l)/f;

        do {
          l2=l-1;
          do {
            l--;
          } while(l&&(!strchr(il->sep,m[l])));
          if(!l)
            l=l2;
          s=m[l];
          m[l]='\0';
          evas_object_text_text_set(o,m);
          evas_object_geometry_get(o,&_x,&_y,&_w,&_h);
          m[l]=s;
        } while((f=(_w/((double)il->vis_w)))>1.0); }

      m+=l;

      col=param->textcolour;
      evas_object_color_set(o, (col>>16)%256,(col>>8)%256,col%256, param->textalpha);
      ICON_EL_APP(IE_DFL,101,_x,_y);

      if(!e2)
        e2=el;

      el->xo=(il->vis_w-_w)/2;
      el->yo=(il->vis_h-_h)/2;

      th+=_h;

      evas_object_move(o,el->xo,el->yo);

      o2=o;
      o=tb_to(evas,param->font,param->size);
      evas_object_text_text_set(o,evas_object_text_text_get(o2));
      col=param->shadowcolour;
      evas_object_color_set(o, (col>>16)%256,(col>>8)%256,col%256, param->shadowalpha);
      ICON_EL_APP(IE_DFL,100,_x+SHADOW,_y+SHADOW);

      el->xo=(il->vis_w-_w)/2+SHADOW;
      el->yo=(il->vis_h-_h)/2+SHADOW;

      evas_object_move(o,el->xo,el->yo); }

    /*************************************************************************/


    _y=(il->vis_h>th)?(il->vis_h-th)/2:il->vis_h-th;

    el=e2;
    while(el) {
      el->yo=_y;
      E(5,"Set \"%s\" at %d,%d  (%d)\n",evas_object_text_text_get((Evas_Object *)el->data),el->xo,el->yo,el->h);
      evas_object_move((Evas_Object *)el->data,el->xo,el->yo);

      el=el->next;
      el->yo=_y;
      evas_object_move((Evas_Object *)el->data,el->xo,el->yo);

      _y+=el->h;
      el=el->next; }

    free(m2); }

  return e2; }
