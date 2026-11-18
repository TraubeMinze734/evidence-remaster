/** @file  live_preview.h
 *  @brief live video preview shown as a separate "tooltip" in the icon-view
 *
 * while the info-tooltip (tt_* engines) only shows the static poster of a
 * video, the actual playback happens in this second tooltip: a framed box
 * with the live emotion object, placed in free space of the visible area
 * (never overlapping the info-tooltip or the icon), connected to the icon
 * by a line -- much like tt_diagram connects its info-boxes to the icon.
 */

#ifndef LIVE_PREVIEW_H
#  define LIVE_PREVIEW_H

#  include "gui/common/icon_list.h"
#  include "model.h"


/** is this file a video we can play live? (MIME video/*) */
int   live_preview_is_video(file_info *fi);

/** bounding box of an open evas-tooltip (head-icon x/y/w/h, or union of the
 *  element geometries for engines that don't set it); may be empty */
void  live_preview_tt_rect(void *tt,int *x,int *y,int *w,int *h);

/** create the live-preview tooltip for fi on il's canvas.
 *  @param il  the icon-list (visible area is il->vis_w x il->vis_h)
 *  @param fi  the file to preview
 *  @param ic  the icon the mouse is on (canvas coords, may be NULL)
 *  @param ax,ay  anchor in canvas coords (mouse pos), used if ic==NULL
 *  @param tt  the open info-tooltip (to avoid overlapping it, may be NULL)
 *  @return a handle for live_preview_hide(), or NULL (not a video, live
 *          thumbnails disabled, no thumbnailer, out of space, ...) */
void *live_preview_show(icon_list *il,file_info *fi,icon *ic,int ax,int ay,void *tt);

/** destroy a live-preview tooltip (stops playback, frees the objects) */
void  live_preview_hide(void **pv);

#endif
