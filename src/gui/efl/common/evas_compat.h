/** @file  evas_compat.h
 *  @brief compatibility layer for APIs removed from modern EFL.
 *
 *  evidence (2003/2005) was written against Evas 1.0-beta.  Modern EFL
 *  removed the gradient object API (evas_object_gradient_*), which the
 *  original theme engines (scrollbars, tooltips, rubber-bands, icon
 *  frames) use heavily.  This header provides drop-in replacements so
 *  the original engines can be used unmodified with modern EFL:
 *
 *   - evas_object_gradient_add()          -> EvasVG object (rect path, linear gradient fill)
 *   - evas_object_gradient_color_add()    -> Efl.Gfx.Gradient stop list
 *   - evas_object_gradient_colors_clear() -> Efl.Gfx.Gradient stop list
 *   - evas_object_gradient_angle_set()     -> linear gradient start/end points
 *
 *  Text metrics (evas_object_text_ascent_get etc.) still exist in
 *  modern EFL and need no shim.
 */
#ifndef EVAS_COMPAT_H
#define EVAS_COMPAT_H

#include "config.h"
#include <Evas.h>

#ifdef HAVE_EVAS2

#ifndef EVAS_LEGACY_GRADIENT

/* Old Evas accepted non-premultiplied colours (e.g. 255,255,255,159 for
   semi-transparent symlinks); modern EFL requires 0 <= R,G,B <= A and
   logs a costly backtrace-dump for every violation.  Clamp at call-site
   for all code including this header (the theme engines). */
#define evas_object_color_set(obj,r,g,b,a) \
  evas_object_color_set((obj), \
    ((r)>(a))?(a):(r), ((g)>(a))?(a):(g), ((b)>(a))?(a):(b), (a))

Evas_Object *evas_object_gradient_add(Evas *e);
void evas_object_gradient_color_add(Evas_Object *o, int r, int g, int b, int a, int distance);
void evas_object_gradient_colors_clear(Evas_Object *o);
void evas_object_gradient_angle_set(Evas_Object *o, double angle);
double evas_object_gradient_angle_get(const Evas_Object *o);

#endif /* !EVAS_LEGACY_GRADIENT */

#endif /* HAVE_EVAS2 */
#endif /* EVAS_COMPAT_H */
