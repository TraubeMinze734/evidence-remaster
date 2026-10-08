/** @file  evas_compat.c
 *  @brief implementations for the compat layer in evas_compat.h.
 *
 *  The old Evas gradient objects supported per-stop "distance" weights
 *  and an arbitrary angle.  Modern EFL only has linear/radial gradients
 *  in the VG tree, whose behaviour with legacy geometry/layer usage is
 *  hard to match (the view-darkening fade of the tooltip engines, the
 *  rubber-band gradients etc. did not render).  So instead of VG we
 *  render the gradient ourselves into an ARGB buffer and expose it as
 *  an evas image object -- only rock-solid Evas API, deterministic
 *  result:
 *   - stops are evenly spaced; the legacy "distance" weight is ignored
 *   - the angle selects the gradient axis through the object center
 *   - colours are written premultiplied, as EFL requires
 */

#include "config.h"

#ifdef HAVE_EVAS2

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <Evas.h>

#include "evas_compat.h"

#ifndef EVAS_LEGACY_GRADIENT

#define GRADIENT_MAX_STOPS 128

typedef struct {
  int r, g, b, a;
} compat_stop;

typedef struct {
  Evas_Object *img;                    /* the object the callers work with */
  compat_stop  stops[GRADIENT_MAX_STOPS];
  int          count;
  double       angle;
  unsigned int *pixels;                /* backing-store of the image */
  int          pw, ph;                 /* current size of the backing-store */
} compat_gradient;

#define COMPAT_GRADIENT(o) ((compat_gradient *)evas_object_data_get((Evas_Object *)(o), "compat-gradient"))

static void compat_gradient_render(compat_gradient *g);

static void compat_gradient_geom_changed(void *data, Evas *e, Evas_Object *obj, void *event_info) {
  compat_gradient *g = (compat_gradient *)data;
  (void)e; (void)obj; (void)event_info;
  compat_gradient_render(g); }

Evas_Object *evas_object_gradient_add(Evas *e) {
  compat_gradient *g;
  Evas_Object *img;

  if(!(g = calloc(1, sizeof(compat_gradient))))
    return NULL;

  if(!(img = evas_object_image_add(e))) {
    free(g);
    return NULL; }

  g->img    = img;
  g->angle  = 0.0;
  g->pixels = NULL;
  g->pw = g->ph = 0;

  evas_object_image_alpha_set(img, 1);
  evas_object_image_smooth_scale_set(img, 0);

  evas_object_data_set(img, "compat-gradient", g);
  evas_object_event_callback_add(img, EVAS_CALLBACK_RESIZE, compat_gradient_geom_changed, g);
  evas_object_event_callback_add(img, EVAS_CALLBACK_MOVE, compat_gradient_geom_changed, g);

  return img; }

/* interpolate the stop list for parameter t in [0,1]; premultiplied */
static void compat_gradient_col(compat_gradient *g, double t, int *rr, int *gg, int *bb, int *aa) {
  compat_stop *s = g->stops;
  int i;

  if(g->count < 1) {
    *rr = *gg = *bb = *aa = 0;
    return; }
  if(g->count == 1 || t <= 0.0) {
    *rr = s[0].r; *gg = s[0].g; *bb = s[0].b; *aa = s[0].a;
    return; }
  if(t >= 1.0) {
    s = &s[g->count - 1];
    *rr = s->r; *gg = s->g; *bb = s->b; *aa = s->a;
    return; }

  for(i = 0; i < g->count - 1; i++) {
    double o0 = (double)i / (g->count - 1);
    double o1 = (double)(i + 1) / (g->count - 1);
    if(t >= o0 && t <= o1) {
      double u = (o1 > o0) ? (t - o0) / (o1 - o0) : 0.0;
      *rr = (int)(s[i].r + u * (s[i + 1].r - s[i].r));
      *gg = (int)(s[i].g + u * (s[i + 1].g - s[i].g));
      *bb = (int)(s[i].b + u * (s[i + 1].b - s[i].b));
      *aa = (int)(s[i].a + u * (s[i + 1].a - s[i].a));
      return; } }

  s = &s[g->count - 1];
  *rr = s->r; *gg = s->g; *bb = s->b; *aa = s->a; }

static void compat_gradient_render(compat_gradient *g) {
  int x, y, w, h, i;
  double rad, dx, dy, len, cx, cy;
  unsigned int *row;

  if(g->count < 1)
    return;

  evas_object_geometry_get(g->img, &x, &y, &w, &h);
  if(w < 1 || h < 1)
    return;

  if((w != g->pw) || (h != g->ph)) {
    unsigned int *pixels = realloc(g->pixels, (size_t)w * h * sizeof(unsigned int));
    if(!pixels)
      return;
    g->pixels = pixels;
    g->pw = w;
    g->ph = h; }

  /* gradient axis through the object center, object-local coords */
  rad = (g->angle * M_PI) / 180.0;
  dx = cos(rad);
  dy = -sin(rad);  /* screen y grows downwards */
  cx = w / 2.0;
  cy = h / 2.0;
  len = (fabs(dx) * w + fabs(dy) * h) / 2.0;
  if(len < 1.0)
    len = 1.0;

  for(y = 0; y < h; y++) {
    double py = (double)y - cy;
    row = g->pixels + (size_t)y * w;
    for(x = 0; x < w; x++) {
      double px = (double)x - cx;
      double t = ((px * dx + py * dy) + len) / (2.0 * len);
      int r, gr, b, a;
      compat_gradient_col(g, t, &r, &gr, &b, &a);
      /* premultiplied ARGB (native endian) */
      row[x] = ((unsigned int)(a & 0xff) << 24) |
               ((unsigned int)((r * a / 255) & 0xff) << 16) |
               ((unsigned int)((gr * a / 255) & 0xff) << 8) |
               ((unsigned int)((b * a / 255) & 0xff)); } }

  (void)i;
  evas_object_image_size_set(g->img, w, h);
  evas_object_image_data_set(g->img, g->pixels);
  evas_object_image_data_update_add(g->img, 0, 0, w, h); }

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
  compat_gradient_render(g); }

void evas_object_gradient_colors_clear(Evas_Object *o) {
  compat_gradient *g = COMPAT_GRADIENT(o);
  if(!g)
    return;
  g->count = 0; }

void evas_object_gradient_angle_set(Evas_Object *o, double angle) {
  compat_gradient *g = COMPAT_GRADIENT(o);
  if(!g)
    return;
  g->angle = angle;
  compat_gradient_render(g); }

double evas_object_gradient_angle_get(const Evas_Object *o) {
  compat_gradient *g = COMPAT_GRADIENT((Evas_Object *)o);
  return g ? g->angle : 0.0; }

#endif /* !EVAS_LEGACY_GRADIENT */

#endif /* HAVE_EVAS2 */
