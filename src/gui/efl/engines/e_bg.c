/** @file  e_bg.c
 *  @brief routines for the (deprecated) e-bg format
 *
 * ebg was the format used by the first prototype of enlightenment DR17.
 * it lets you compose a background out of individual items such as gradients,
 * images etc.  each item can be placed/resized to fit the current resolution.
 * to compile this plugin, only edb is required, but to create your own
 * ebgs (.bg.db), you'll need ebony, the graphical ebg-editor.
 *
 * part of original e17 libraries, see there for AUTHORS and COPYRIGHT
 */



#include "config.h"
#include "debug.h"

#ifdef HAVE_EDB

#  include <sys/stat.h>

#  include "errors.h"
#  include "file.h"

#  include <Edb.h>

#  include "Ebg.h"

/**
 * allocate a 0'ed out E_Background_Gradient
 * @return a zeroed out E_Background_Gradient
 */
static E_Background_Gradient e_bg_gradient_new(void) {
  E_Background_Gradient g=(E_Background_Gradient)malloc(sizeof(struct _E_Background_Gradient));
  memset(g,0,sizeof(struct _E_Background_Gradient));
  return g; }



/**
 * allocate and zero out a new E_Background_Layer struct
 * @return a zeroed out E_Background_Layer
 */
E_Background_Layer e_bg_layer_new(void) {
  E_Background_Layer bl=(E_Background_Layer)malloc(sizeof(struct _E_Background_Layer));
  memset(bl,0,sizeof(struct _E_Background_Layer));
  return bl; }



/**
 * free an E_Background_Layer
 * @param bg  the E_Background containing the layer to be freed
 * @param bl  the E_Background_Layer to be free
 */
void e_bg_layer_free(E_Background bg, E_Background_Layer bl) {
  Evas_List *l;

  if(!bg||!bl)
    return;

  if(bl->file)
    free(bl->file);
  bl->file=NULL;

  if(bl->color_class)
    free(bl->color_class);
  bl->color_class=NULL;

  if((bg->evas)&&(bl->obj))
    evas_object_del(bl->obj);
   bl->obj=NULL;

   for(l=bl->gradient.colors;l;l=l->next)
     free((E_Background_Gradient)l->data);

   if(bl->gradient.colors)
     bl->gradient.colors=evas_list_free(bl->gradient.colors);

   free(bl); }



/**
 * retrieve the specified layer from an E_Background
 * @return the layer
 * @param bg   the E_Background to request the layer from
 * @param num  the layer number request, 0 being the first(bottom) layer
 */
E_Background_Layer e_bg_get_layer_number(E_Background bg,int num) {
  int        i;
  Evas_List *l;

  if(!bg)
    return NULL;

  for(i=0,l=bg->layers;l;l=l->next,i++) {
    if(i==num)
      return l->data; }
  return NULL; }



/**
 * allocate a new E_Background struct
 * @return a zeroed out E_Background struct, or NULL
 */
E_Background e_bg_new(void) {
  E_Background bg=(E_Background)malloc(sizeof(struct _E_Background));

  if(bg)
    memset(bg,0,sizeof(struct _E_Background));
  return bg; }



/**
 * free the E_Background and all of its layers
 * @param bg  the bg to be freed
 */
void e_bg_free(E_Background bg) {
  Evas_List *l;

  if(!bg)
    return;

  for(l=bg->layers;l;l=l->next) {
    E_Background_Layer bl=l->data;
    e_bg_layer_free(bg,bl); }

  bg->layers=evas_list_free(bg->layers);
  if(bg->file)
    free(bg->file);
  if(bg->base_obj)
    evas_object_del(bg->base_obj);
  bg->evas=NULL;
  free(bg); }



/**
 * Allocate and populate an E_Background from a .bg.db
 * @return E_Background with the data from the bg.db, or NULL
 * @param file  the filename of the .bg.db on disk
 */
E_Background e_bg_load(char *file) {
   E_Background bg;
   E_DB_File *db;
   int i, num = 0;

   if (!file || !*file)
      return NULL;

   db = e_db_open_read(file);

   /* not a db, return NULL */
   if (!db)
      return (NULL);
   /* ok it is a db file, make sure it's a bg db or return NULLL */
   e_db_int_get(db, "/type/bg", &num);
   if (num != 1)
   {
      e_db_close(db);
      e_db_flush();
      return NULL;
   }
   e_db_int_get(db, "/layers/count", &num);

   bg = e_bg_new();
   bg->file = strdup(file);
   for (i = 0; i < num; i++)
   {
      E_Background_Layer bl;
      char buf[PATH_MAX];

      bl = e_bg_layer_new();
      bg->layers = evas_list_append(bg->layers, bl);

      snprintf(buf, PATH_MAX, "/layers/%i/type", i);
      e_db_int_get(db, buf, (int *) &(bl->type));
      snprintf(buf, PATH_MAX, "/layers/%i/inlined", i);
      e_db_int_get(db, buf, &(bl->inlined));
      snprintf(buf, PATH_MAX, "/layers/%i/color_class", i);
      bl->color_class = e_db_str_get(db, buf);
      if (bl->type == E_BACKGROUND_TYPE_IMAGE)
      {
         if (bl->inlined)
         {
            snprintf(buf, PATH_MAX, "/layers/%i/image", i);
            bl->file = strdup(buf);
         }
         else
         {
            snprintf(buf, PATH_MAX, "/layers/%i/file", i);
            bl->file = e_db_str_get(db, buf);
         }
      }
      else
      {

         if (bl->type == E_BACKGROUND_TYPE_GRADIENT)
         {
            int gradient_count, n;

            snprintf(buf, PATH_MAX, "/layers/%i/gradient/count", i);
            e_db_int_get(db, buf, &gradient_count);
            snprintf(buf, PATH_MAX, "/layers/%i/gradient/angle", i);
            e_db_float_get(db, buf, &(bl->gradient.angle));
            for (n = 0; n < gradient_count; n++)
            {
               E_Background_Gradient g;

               g = e_bg_gradient_new();
               snprintf(buf, PATH_MAX, "/layers/%i/gradient/%i/r", i, n);
               e_db_int_get(db, buf, &(g->r));
               snprintf(buf, PATH_MAX, "/layers/%i/gradient/%i/g", i, n);
               e_db_int_get(db, buf, &(g->g));
               snprintf(buf, PATH_MAX, "/layers/%i/gradient/%i/b", i, n);
               e_db_int_get(db, buf, &(g->b));
               snprintf(buf, PATH_MAX, "/layers/%i/gradient/%i/a", i, n);
               e_db_int_get(db, buf, &(g->a));
               snprintf(buf, PATH_MAX, "/layers/%i/gradient/%i/distance", i,
                        n);
               e_db_int_get(db, buf, &(g->dist));
               bl->gradient.colors = evas_list_append(bl->gradient.colors, g);
            }
         }
      }
      snprintf(buf, PATH_MAX, "/layers/%i/fg.r", i);
      e_db_int_get(db, buf, &(bl->fg.r));
      snprintf(buf, PATH_MAX, "/layers/%i/fg.g", i);
      e_db_int_get(db, buf, &(bl->fg.g));
      snprintf(buf, PATH_MAX, "/layers/%i/fg.b", i);
      e_db_int_get(db, buf, &(bl->fg.b));
      snprintf(buf, PATH_MAX, "/layers/%i/fg.a", i);
      e_db_int_get(db, buf, &(bl->fg.a));
      snprintf(buf, PATH_MAX, "/layers/%i/bg.r", i);
      e_db_int_get(db, buf, &(bl->bg.r));
      snprintf(buf, PATH_MAX, "/layers/%i/bg.g", i);
      e_db_int_get(db, buf, &(bl->bg.g));
      snprintf(buf, PATH_MAX, "/layers/%i/bg.b", i);
      e_db_int_get(db, buf, &(bl->bg.b));
      snprintf(buf, PATH_MAX, "/layers/%i/bg.a", i);
      e_db_int_get(db, buf, &(bl->bg.a));

      snprintf(buf, PATH_MAX, "/layers/%i/scroll.x", i);
      e_db_float_get(db, buf, &(bl->scroll.x));
      snprintf(buf, PATH_MAX, "/layers/%i/scroll.y", i);
      e_db_float_get(db, buf, &(bl->scroll.y));

      snprintf(buf, PATH_MAX, "/layers/%i/pos.x", i);
      e_db_float_get(db, buf, &(bl->pos.x));
      snprintf(buf, PATH_MAX, "/layers/%i/pos.y", i);
      e_db_float_get(db, buf, &(bl->pos.y));

      snprintf(buf, PATH_MAX, "/layers/%i/size.w", i);
      e_db_float_get(db, buf, &(bl->size.w));
      snprintf(buf, PATH_MAX, "/layers/%i/size.h", i);
      e_db_float_get(db, buf, &(bl->size.h));
      snprintf(buf, PATH_MAX, "/layers/%i/size.orig.w", i);
      e_db_int_get(db, buf, &(bl->size.orig.w));
      snprintf(buf, PATH_MAX, "/layers/%i/size.orig.h", i);
      e_db_int_get(db, buf, &(bl->size.orig.h));
      snprintf(buf, PATH_MAX, "/layers/%i/size.absolute.w", i);
      e_db_int_get(db, buf, &(bl->size.absolute.w));
      snprintf(buf, PATH_MAX, "/layers/%i/size.absolute.h", i);
      e_db_int_get(db, buf, &(bl->size.absolute.h));

      snprintf(buf, PATH_MAX, "/layers/%i/fill.w", i);
      e_db_float_get(db, buf, &(bl->fill.w));
      snprintf(buf, PATH_MAX, "/layers/%i/fill.h", i);
      e_db_float_get(db, buf, &(bl->fill.h));
      snprintf(buf, PATH_MAX, "/layers/%i/fill.orig.w", i);
      e_db_int_get(db, buf, &(bl->fill.orig.w));
      snprintf(buf, PATH_MAX, "/layers/%i/fill.orig.h", i);
      e_db_int_get(db, buf, &(bl->fill.orig.h));

      snprintf(buf, PATH_MAX, "/layers/%i/abs.y", i);
      e_db_int_get(db, buf, &(bl->abs.y));
      snprintf(buf, PATH_MAX, "/layers/%i/abs.x", i);
      e_db_int_get(db, buf, &(bl->abs.x));
   }
   e_db_close(db);

   return bg;
}


/**
 * add a bg to an evas
 * After loading or manually allocating an E_Background it can be added to
 * an evas.  All layers pass event thru to a white base object in the
 * E_Background.  It does not show the bg, use e_bg_show(bg)
 * @param bg    the bg to add and show in the evas
 * @param evas  the evas to add the E_Background to
 */
int e_bg_add_to_evas(E_Background bg,Evas *evas) {
   Evas_List *l;
   int ww, hh, count, ret=NOIR_SUCC, err;

   if (!bg || !evas)
      return NOIR_FAIL;

   if (bg->evas)
      return NOIR_IGNORED;
   bg->evas = evas;
   if (!bg->evas)
      return NOIR_NODATA;

   /* white base bg */
   bg->base_obj = evas_object_rectangle_add(bg->evas);
   evas_object_move(bg->base_obj, 0, 0);
   evas_object_resize(bg->base_obj, 999999999, 999999999);
   evas_object_color_set(bg->base_obj, 255, 255, 255, 255);

   for (count = 0, l = bg->layers; l; l = l->next, count++)
   {
      E_Background_Layer bl;

      bl = l->data;

      switch (bl->type)
      {
        case E_BACKGROUND_TYPE_IMAGE:
           bl->obj = evas_object_image_add(bg->evas);
           if (bl->inlined)
              evas_object_image_file_set(bl->obj, bg->file, bl->file);
           else
              evas_object_image_file_set(bl->obj, bl->file, NULL);
           if((err=evas_object_image_load_error_get(bl->obj))!=EVAS_LOAD_ERROR_NONE) {
             char *mf=bl->inlined?bg->file:bl->file;
             int   ex=file_exists(mf);

             ex=S_ISREG(ex)||S_ISLNK(ex);

             ret=NOIR_PARTIAL;

             if (bl->inlined)
               E(0,"error #%d loading \"%s{%s}\"...%s\n\n",err,bg->file,bl->file,
                 ex?"\n\tThe file was found, but could not be loaded...\n\tare you sure your evas-library was built to support edb-files?":"");
             else
               E(0,"error #%d loading \"%s\"...%s\n\n",err,bl->file,
                 ex?"\n\tThe file was found, but could not be loaded.":"");

             evas_object_del(bl->obj);
             bl->obj=NULL; }

           break;
        case E_BACKGROUND_TYPE_GRADIENT:
        {
           Evas_List *l;
           E_Background_Gradient g;

           bl->obj = evas_object_gradient_add(bg->evas);

           for (l = bl->gradient.colors; l; l = l->next)
           {
              g = (E_Background_Gradient) l->data;
              evas_object_gradient_color_add(bl->obj, g->r, g->g, g->b, g->a,
                                             g->dist);
           }
           evas_object_gradient_angle_set(bl->obj, bl->gradient.angle);
           break;
        }
        default:
           /* case E_BACKGROUND_TYPE_SOLID: */
        {
           bl->obj = evas_object_rectangle_add(bg->evas);
           evas_object_color_set(bl->obj, bl->fg.r, bl->fg.g, bl->fg.b,
                                 bl->fg.a);
           break;
        }
      }
      if(bl->obj)
        evas_object_pass_events_set(bl->obj, 1);
   }
   evas_output_size_get(bg->evas, &ww, &hh);
   bg->geom.w = bg->geom.h = 0;
   e_bg_resize(bg, ww, hh);

   return ret; }

/**
 * Set the scroll effect for each layer.
 * This *might* be broken with types other than images 
 * @param bg  the E_Background to set the scroll value for
 * @param sx  the scroll x offset
 * @param sy  the scroll y offset
 */
void e_bg_set_scroll(E_Background bg,int sx,int sy) {
   Evas_List *l;

   if (!bg)
      return;

   if ((bg->geom.sx == sx) && (bg->geom.sy == sy))
      return;

   bg->geom.sx = sx;
   bg->geom.sy = sy;
   if (!bg->evas)
      return;

   for (l = bg->layers; l; l = l->next)
   {
      E_Background_Layer bl;

      bl = l->data;

      if (bl->type == E_BACKGROUND_TYPE_IMAGE)
      {
         evas_object_image_fill_set(bl->obj,
                                    (double) bg->geom.sx * bl->scroll.x,
                                    (double) bg->geom.sy * bl->scroll.y,
                                    bl->fw, bl->fh);
      }
   }
}



/**
 * Put the E_Background in the specified layer
 * @param bg     the E_Background to be moved to the new layer
 * @param layer  the layer number requested to put the E_Background in
 */
void e_bg_set_layer(E_Background bg,int layer) {
   if(!bg)
      return;

   if(bg->evas) {
      Evas_List *l;
      Evas_Object *o;
      E_Background_Layer bl;

      /* base bg color on the bottom */
      evas_object_layer_set(bg->base_obj, layer);
      o = bg->base_obj;
      for (l = bg->layers; l; l = l->next)
      {
         bl = l->data;
         evas_object_layer_set(bl->obj, layer);
         evas_object_stack_above(bl->obj, o);
         o = bl->obj;
      }
   }
}



/**
 * move the E_Background to the specified x/y in the evas
 * @param bg  the E_Background to be moved
 * @param _x  the x coordinate in the evas for the upper left corner of the bg
 * @param _y  the y coordinate in the evas for the upper left corner of the bg
 */
void e_bg_move(E_Background bg,int _x,int _y) {
   Evas_List *l;
   int i;

   if (!bg)
      return;
   if (!bg->evas)
      return;

   bg->x = _x;
   bg->y = _y;
   for (i = 0, l = bg->layers; l; l = l->next, i++)
   {
      E_Background_Layer bl;
      Evas_Coord x, y, w, h;

      bl = (E_Background_Layer) l->data;

      evas_object_geometry_get(bl->obj, NULL, NULL, &w, &h);
      /* base is the bg's (x,y) */
      x = bg->x;
      y = bg->y;

      /* this layers set position */
      x += ((double) bg->geom.w - w) * bl->pos.x;
      y += ((double) bg->geom.h - h) * bl->pos.y;

      /* adjust for absolute positioning */
      x += bl->abs.x;
      y += bl->abs.y;

      bl->x = x;
      bl->y = y;
      evas_object_move(bl->obj, bl->x, bl->y);
   }
}



/**
 * resize the E_Background to the given height/width
 * @param bg  the E_Background to be resized
 * @param w   the new requested width
 * @param h   the new requested height
 */
void e_bg_resize(E_Background bg,int w,int h) {
   Evas_List *l;
   int i;

   if (!bg)
      return;
   if (!bg->evas)
      return;
   if ((bg->geom.w == w) && (bg->geom.h == h))
      return;

   bg->geom.w = w;
   bg->geom.h = h;

   evas_object_resize(bg->base_obj, w, h);
   for (i = 0, l = bg->layers; l; l = l->next, i++)
   {
      E_Background_Layer bl;
      Evas_Coord x, y, w, h;
      double fw, fh;
      int iw, ih;

      bl = (E_Background_Layer) l->data;
      iw = ih = 0;
      fw = fh = 0.0;

      x = bg->x;
      y = bg->y;

      w = bl->size.w * (double) bg->geom.w;
      h = bl->size.h * (double) bg->geom.h;

      /* object is an image, resize and calculate fill */
      if (bl->type == E_BACKGROUND_TYPE_IMAGE)
      {
         evas_object_image_size_get(bl->obj, &iw, &ih);
         if (bl->size.orig.w)
            w = (double) iw *bl->size.w;

         if (bl->size.orig.h)
            h = (double) ih *bl->size.h;

         fw = bl->fill.w * w;
         fh = bl->fill.h * h;
         if (bl->fill.orig.w)
            fw = (double) iw *bl->fill.w;

         if (bl->fill.orig.h)
            fh = (double) ih *bl->fill.h;

      }

      /* absolutely sized object */
      if (bl->size.absolute.w)
         w = bl->size.w;
      if (bl->size.absolute.h)
         h = bl->size.h;

      /* set position */
      x += ((double) bg->geom.w - w) * bl->pos.x;
      y += ((double) bg->geom.h - h) * bl->pos.y;

      /* adjust for absolute positioning */
      x += bl->abs.x;
      y += bl->abs.y;

      bl->x = x;
      bl->y = y;
      bl->w = w;
      bl->h = h;
      bl->fw = fw;
      bl->fh = fh;

      evas_object_move(bl->obj, bl->x, bl->y);
      evas_object_resize(bl->obj, bl->w, bl->h);

      switch (bl->type)
      {
        case E_BACKGROUND_TYPE_IMAGE:
           evas_object_image_fill_set(bl->obj,
                                      (double) bg->geom.sx * bl->scroll.x,
                                      (double) bg->geom.sy * bl->scroll.y,
                                      bl->fw, bl->fh);
           break;
        case E_BACKGROUND_TYPE_GRADIENT:
           /* FIXME Necessary to call again ? */
           evas_object_gradient_angle_set(bl->obj, bl->gradient.angle);
           break;
        case E_BACKGROUND_TYPE_SOLID:
           break;
        default:
	   break;
      }
   }
}



/**
 * show all the layers in the E_Background
 * @param bg  The background to show
 */
void e_bg_show(E_Background bg) {
   Evas_List *l;
   E_Background_Layer bl;

   if (!bg)
      return;
   if (!bg->evas)
      return;

   evas_object_show(bg->base_obj);
   for (l = bg->layers; l; l = l->next)
   {
      bl = l->data;
      evas_object_show(bl->obj);
   }
}



/**
 * hide all the layers in the E_Background
 * @param bg  the background to hide 
 */
void e_bg_hide(E_Background bg) {
   Evas_List *l;
   E_Background_Layer bl;

   if (!bg)
      return;
   if (!bg->evas)
      return;

   evas_object_hide(bg->base_obj);
   for (l = bg->layers; l; l = l->next)
   {
      bl = l->data;
      evas_object_hide(bl->obj);
   }
}

#endif
