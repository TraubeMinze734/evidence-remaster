/** @file  evas_compat.c
 *  @brief implementations for the compat layer in evas_compat.h.
 *
 *  The old Evas gradient objects supported per-stop "distance" weights
 *  and an arbitrary angle.  Modern EFL only has linear/radial gradients
 *  with normalized offsets.  We approximate the original behaviour:
 *   - stops are evenly spaced; the legacy "distance" weight is ignored
 *   - the angle selects the gradient axis through the object center;
 *     the fill covers the object rectangle in object-local coordinates
 */

#include "config.h"

#ifdef HAVE_EVAS2

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <Evas.h>
#include <Evas_Eo.h>

#include "evas_compat.h"

#ifndef EVAS_LEGACY_GRADIENT

#define GRADIENT_MAX_STOPS 128

typedef struct {
  Evas_Object             *vg;      /* the EvasVG object the callers work with */
  Evas_Vg_Container       *cont;
  Evas_Vg_Shape           *shape;
  Evas_Vg_Gradient_Linear *grad;
  Efl_Gfx_Gradient_Stop    stops[GRADIENT_MAX_STOPS];
  int                      count;
  double                   angle;
} compat_gradient;

#define COMPAT_GRADIENT(o) ((compat_gradient *)evas_object_data_get((Evas_Object *)(o), "compat-gradient"))

static void compat_gradient_rebuild(compat_gradient *g) {
  int x, y, w, h, i;
  double rad, cx, cy, dx, dy, len;

  if(g->count < 1)
    return;

  evas_object_geometry_get(g->vg, &x, &y, &w, &h);

  /* gradient axis through the object center, object-local coords */
  cx = w / 2.0;
  cy = h / 2.0;
  rad = (g->angle * M_PI) / 180.0;
  dx = cos(rad);
  dy = -sin(rad);  /* screen y grows downwards */
  len = (fabs(dx) * w + fabs(dy) * h) / 2.0;
  if(len < 1.0)
    len = 1.0;

  evas_vg_gradient_linear_start_set(g->grad, cx - dx * len, cy - dy * len);
  evas_vg_gradient_linear_end_set(g->grad, cx + dx * len, cy + dy * len);

  for(i = 0; i < g->count; i++) {
    /* EFL requires premultiplied stops (0 <= r,g,b <= a) */
    g->stops[i].offset = (double)i / (double)(g->count - 1);
    g->stops[i].r = (g->stops[i].r > g->stops[i].a) ? g->stops[i].a : g->stops[i].r;
    g->stops[i].g = (g->stops[i].g > g->stops[i].a) ? g->stops[i].a : g->stops[i].g;
    g->stops[i].b = (g->stops[i].b > g->stops[i].a) ? g->stops[i].a : g->stops[i].b; }

  evas_vg_gradient_stop_set((Evas_Vg_Gradient *)g->grad, g->stops, g->count);

  /* cover the whole object with the path (object-local coords) */
  efl_gfx_path_reset(g->shape);
  efl_gfx_path_append_rect(g->shape, 0, 0, w, h, 0, 0);

  evas_vg_shape_fill_set(g->shape, (Evas_Vg_Node *)g->grad); }

static void compat_gradient_geom_changed(void *data, Evas *e, Evas_Object *obj, void *event_info) {
  compat_gradient *g = (compat_gradient *)data;
  (void)e; (void)obj; (void)event_info;
  compat_gradient_rebuild(g); }

Evas_Object *evas_object_gradient_add(Evas *e) {
  compat_gradient *g;
  Evas_Object *vg;

  if(!(g = calloc(1, sizeof(compat_gradient))))
    return NULL;

  if(!(vg = evas_object_vg_add(e)) ||
     !(g->cont = evas_vg_container_add(vg)) ||
     !(g->shape = evas_vg_shape_add(g->cont)) ||
     !(g->grad = evas_vg_gradient_linear_add(g->cont))) {
    if(vg)
      evas_object_del(vg);
    free(g);
    return NULL; }

  g->vg = vg;
  g->angle = 0.0;

  evas_object_data_set(vg, "compat-gradient", g);
  evas_object_event_callback_add(vg, EVAS_CALLBACK_RESIZE, compat_gradient_geom_changed, g);
  evas_object_event_callback_add(vg, EVAS_CALLBACK_MOVE, compat_gradient_geom_changed, g);

  return vg; }

void evas_object_gradient_color_add(Evas_Object *o, int r, int gr, int b, int a, int distance) {
  compat_gradient *g = COMPAT_GRADIENT(o);
  (void)distance; /* evenly-spaced approximation, see file comment */
  if(!g || g->count >= GRADIENT_MAX_STOPS)
    return;
  g->stops[g->count].r = r;
  g->stops[g->count].g = gr;
  g->stops[g->count].b = b;
  g->stops[g->count].a = a;
  g->count++;
  compat_gradient_rebuild(g); }

void evas_object_gradient_colors_clear(Evas_Object *o) {
  compat_gradient *g = COMPAT_GRADIENT(o);
  if(!g)
    return;
  g->count = 0;
  compat_gradient_rebuild(g); }

void evas_object_gradient_angle_set(Evas_Object *o, double angle) {
  compat_gradient *g = COMPAT_GRADIENT(o);
  if(!g)
    return;
  g->angle = angle;
  compat_gradient_rebuild(g); }

double evas_object_gradient_angle_get(const Evas_Object *o) {
  compat_gradient *g = COMPAT_GRADIENT((Evas_Object *)o);
  return g ? g->angle : 0.0; }

#endif /* !EVAS_LEGACY_GRADIENT */

#endif /* HAVE_EVAS2 */
