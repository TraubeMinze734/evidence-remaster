#ifndef EVIDENCE_GTK_COMPAT_H
#define EVIDENCE_GTK_COMPAT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* GTK3 made GdkDragContext and GtkSelectionData opaque. The 2005 code
   dereferences them directly; these helpers keep both build paths
   readable without littering call-sites with #ifs. They are tiny and
   static-inline so unused ones cost nothing. */

static inline int evis_seldata_length(const GtkSelectionData *sd) {
#if GTK_CHECK_VERSION(3,0,0)
  return gtk_selection_data_get_length((GtkSelectionData *)sd);
#else
  return sd?sd->length:-1234;
#endif }

static inline int evis_seldata_format(const GtkSelectionData *sd) {
#if GTK_CHECK_VERSION(3,0,0)
  return gtk_selection_data_get_format((GtkSelectionData *)sd);
#else
  return sd->format;
#endif }

static inline const guchar *evis_seldata_data(const GtkSelectionData *sd) {
#if GTK_CHECK_VERSION(3,0,0)
  return gtk_selection_data_get_data((GtkSelectionData *)sd);
#else
  return sd->data;
#endif }

static inline GdkAtom evis_seldata_target(const GtkSelectionData *sd) {
#if GTK_CHECK_VERSION(3,0,0)
  return gtk_selection_data_get_target((GtkSelectionData *)sd);
#else
  return sd->target;
#endif }

static inline GdkDragAction evis_drag_action(const GdkDragContext *dc) {
#if GTK_CHECK_VERSION(3,0,0)
  return gdk_drag_context_get_selected_action((GdkDragContext *)dc);
#else
  return dc->action;
#endif }

G_END_DECLS

#endif /* EVIDENCE_GTK_COMPAT_H */
