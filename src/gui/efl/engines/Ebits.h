#ifndef EBITS_H
#define EBITS_H 1
#include <Evas.h>

#ifndef _EBITS_INTERNAL
typedef void       *Ebits_Object;
#else
typedef struct _Ebits_Object *Ebits_Object;
#endif

/* functions */
#ifdef __cplusplus
extern              "C"
{
#endif

   Ebits_Object        ebits_load(char *file);
   void                ebits_free(Ebits_Object o);
   void                ebits_set_cache(int count);
   int                 ebits_get_cache(void);
   void                ebits_flush(void);
   void                ebits_add_to_evas(Ebits_Object o, Evas * e);
   void                ebits_show(Ebits_Object o);
   void                ebits_hide(Ebits_Object o);
   void                ebits_set_layer(Ebits_Object o, int l);
   void                ebits_raise(Ebits_Object o);
   void                ebits_lower(Ebits_Object o);
   void                ebits_move(Ebits_Object o, double x, double y);
   void                ebits_resize(Ebits_Object o, double w, double h);
   void                ebits_set_clip(Ebits_Object o, Evas_Object * clip);
   void                ebits_unset_clip(Ebits_Object o);
   void                ebits_get_padding(Ebits_Object o, int *l, int *r,
					 int *t, int *b);
   void                ebits_get_insets(Ebits_Object o, int *l, int *r, int *t,
					int *b);
   void                ebits_get_min_size(Ebits_Object o, int *w, int *h);
   void                ebits_get_max_size(Ebits_Object o, int *w, int *h);
   void                ebits_get_real_min_size(Ebits_Object o, int *w, int *h);
   void                ebits_get_size_step(Ebits_Object o, int *x, int *y);
   void                ebits_set_color_class(Ebits_Object o, char *cc,
					     int r, int g, int b, int a);
   void                ebits_get_load(Ebits_Object o, long *load);
   void                ebits_set_load(Ebits_Object o, char *cc, long load);
   void                ebits_set_classed_bit_callback(Ebits_Object o, char *c,
						      Evas_Callback_Type type,
						      void (*func) (void
								    *_data,
								    Ebits_Object
								    _o,
								    char *_c,
								    int _b,
								    int _x,
								    int _y,
								    int _ox,
								    int _oy,
								    int _ow,
								    int _oh),
						      void *data);
   void                ebits_get_named_bit_geometry(Ebits_Object o, char *c,
						    double *x, double *y,
						    double *w, double *h);
   void                ebits_set_named_bit_replace(Ebits_Object o, char *c,
						   void (*func_show) (void
								      *_data),
						   void (*func_hide) (void
								      *_data),
						   void (*func_move) (void
								      *_data,
								      double x,
								      double
								      y),
						   void (*func_resize) (void
									*_data,
									double
									w,
									double
									h),
						   void (*func_raise) (void
								       *_data),
						   void (*func_lower) (void
								       *_data),
						   void (*func_set_layer) (void
									   *_data,
									   int
									   l),
						   void (*func_set_clip) (void
									  *_data,
									  Evas_Object *
									  clip),
						   void (*func_set_color_class)
						   (void *_data, char *cc,
						    int r, int g, int b,
						    int a),
						   void (*func_get_min_size)
						   (void *_data, double *w,
						    double *h),
						   void (*func_get_max_size)
						   (void *_data, double *w,
						    double *h), void *data);
   void                ebits_set_named_bit_state(Ebits_Object o, char *c,
						 char *state);

   void                ebits_set_named_bit_selected(Ebits_Object o, char *c,
						    unsigned int s);

   Evas_List *           ebits_get_bit_names(Ebits_Object o);
   Evas_List *           ebits_get_state_names(Ebits_Object o);
   void                ebits_get_preferred_size(Ebits_Object o, int *w, int *h);
   int                 ebits_is_shaped(Ebits_Object o);

#ifdef __cplusplus
}
#endif
#endif
