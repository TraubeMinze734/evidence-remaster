/** @file  iconlist_stub.c
 *  @brief no-op icon-list backend for builds without a canvas.
 *
 * icon_list.c (GUI-common) calls into a canvas backend (icon_move,
 * sb_update, pointer_on_icon_p, icon_el_data_dst) that only exists in the
 * Evas icon-view (evas_bind.c). Without Evas/GnomeCanvas no icon-list is
 * ever created, so these stubs only satisfy the linker.
 */
#include "gui/common/icon_list.h"

void icon_el_data_dst(void *data) { (void)data; }
void icon_move(icon *i,int x,int y) { (void)i; (void)x; (void)y; }
int pointer_on_icon_p(icon_list *il,icon *i,int x,int y) { (void)il; (void)i; (void)x; (void)y; return 0; }
void sb_update(icon_list *il,int sb) { (void)il; (void)sb; }
