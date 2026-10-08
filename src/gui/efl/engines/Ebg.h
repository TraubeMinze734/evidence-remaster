/* part of original e17 libraries, see there for AUTHORS and COPYRIGHT */

#ifndef _E_BG_H_
#define _E_BG_H_

#include <Edb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <X11/Xlib.h>
#include <Evas.h>

typedef struct _E_Background *E_Background;
typedef struct _E_Background_Layer *E_Background_Layer;
typedef struct _E_Background_Gradient *E_Background_Gradient;

typedef enum _E_Background_Type
{
   E_BACKGROUND_TYPE_IMAGE = 0,
   E_BACKGROUND_TYPE_GRADIENT = 1,
   E_BACKGROUND_TYPE_SOLID = 2
}
E_Background_Type;

/* Gradient segments need a data representation during db extraction */
struct _E_Background_Gradient
{
   int r, g, b, a;              /* rgba for this section */
   int dist;                    /* its distance from the next section */
};

struct _E_Background_Layer
{
   E_Background_Type type;      /* image, gradient, or color */
   char *file;                  /* the image filename on/in disk/db */
   int inlined;                 /* 1 if image is loaded directly to/from db */
   char *color_class;           /* color class name for the layer */

   /* pos.x - 0.0 <= pos.x <= 1.0, 0 being left most 1 being right most */
   /* pos.y - 0.0 <= pos.y <= 1.0, 0 being top 1 being bottom most */
   /* scroll.x - 0.0 <= scroll.x <= 1.0 0 being no scroll 1 being max */
   /* scroll.y - 0.0 <= scroll.y <= 1.0 0 being no scroll 1 beingmax */
   struct
   {
      float x, y;
   }
   pos, scroll;

   /* Absolute horizontal and vertical positions. */
   /* x - the distance in pixels off of the location determined by pos.x */
   /* y - the distance in pixels off of the location determined by pos.y */
   struct
   {
      int x, y;
   }
   abs;

   /* 
    * size.w - scaled object values are 0.0 <= size.w <= 1.0, absolutely
    *   sized objects size.w is the width of the object
    * size.h - scaled object values are 0.0 <= size.h <= 1.0, absolutely
    *   sized objects size.h is the height of the object 
    * size.orig.w - boolean value - true if the image should be scaled relative
    *  to the width of the image on disk. (w = iw * size.w)
    * size.orig.h - boolean value - true if the image should be scaled relative 
    *  to the height of the image on disk (h = ih * size.h)
    * size.absolute.w - boolean value - true if the object width is staticly
    *  defined(w = size.w), ignored otherwise 
    * size.absolute.h - boolean value - true if the object height is staticly
    *  defined(h = size.h), ignored otherwise 
    * fill.w - float 0.0 <= fill.w <= 1.0
    * fill.h - float 0.0 <= fill.h <= 1.0
    * fill.orig.w - boolean value - true if the image should fill the width
    * relative to its image size, false if it should fill relative to the bg
    * size
    * fill.orig.h - boolean value - true if the image should fill the height
    * relative to its image size, false if it should fill relative to the bg
    * size
    * fill.absolute.w - Not used
    * fill.absolute.h - Not used
    */
   struct
   {
      float w, h;
      struct
      {
         int w, h;
      }
      orig, absolute;
   }
   size, fill;

   /* foreground and background r,g,b,a */
   struct
   {
      int r, g, b, a;
   }
   bg, fg;

   /* Gradient specific info */
   struct
   {
      Evas_List *colors;        /* list of E_Background_Gradient structs */
      float angle;              /* angle the gradient is set to */
   }
   gradient;

   /* 
    * x - x location in the bg 
    * y - y location in the bg 
    * w - the width of the Evas_Object obj in the evas 
    * h - the height of the Evas_Object obj in the evas 
    * fw - the fill width(only applicable to images) 
    * fh - the fill height(only applicable to images) 
    */
   double x, y, w, h, fw, fh;

   /* The object in the layer */
   Evas_Object *obj;
};

struct _E_Background
{
   Evas *evas;                  /* evas for this background */
   char *file;                  /* bg.db file on disk */
   int layer;                   /* layer in the evas */
   int x, y;                    /* upper left corner of the bg in the evas */

   struct
   {
      int sx, sy;               /* the current scroll x,y values */
      int w, h;                 /* width/height of the bg */
   }
   geom;

   Evas_List *layers;

   /* All events pass through to base_obj for callbacks etc */
   Evas_Object *base_obj;
};

#ifdef __cplusplus
extern "C"
{
#endif

   E_Background_Layer e_bg_layer_new(void);
   void e_bg_layer_free(E_Background bg, E_Background_Layer layer);
   E_Background_Layer e_bg_get_layer_number(E_Background bg, int num);

   E_Background e_bg_new(void);
   E_Background e_bg_load(char *file);
   E_Background e_bg_copy(E_Background);

   void e_bg_save(E_Background bg, char *file);
   void e_bg_free(E_Background bg);
   int  e_bg_add_to_evas(E_Background bg, Evas * evas);
   void e_bg_set_scroll(E_Background bg, int sx, int sy);
   void e_bg_move(E_Background bg, int x, int y);
   void e_bg_resize(E_Background bg, int w, int h);
   void e_bg_set_color_class(E_Background bg, char *cc, int r, int g, int b,
                             int a);
   E_Background e_bg_copy(E_Background bg);

   /* show/hide */
   void e_bg_show(E_Background bg);
   void e_bg_hide(E_Background bg);

   /* layers and stacking */
   void e_bg_set_layer(E_Background bg, int layer);
   void e_bg_stack_object_above(E_Background bg, Evas_Object * above);
   void e_bg_stack_object_below(E_Background bg, Evas_Object * below);

#ifdef __cplusplus
}
#endif

#endif
