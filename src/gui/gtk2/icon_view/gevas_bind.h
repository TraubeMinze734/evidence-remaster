#include <gtk/gtk.h>

#include "src/gui/common/icon_list.h"
#include "icon_view.h"

void       gevas_icon_list_setup(noir_sess *);
void       gevas_icon_list_setup_bg(GtkWidget *,char *);
GtkWidget *gevas_icon_list_new(noir_sess *,const char *,int,int,int);
void       gevas_icon_list_resized(GtkWidget *,GtkAllocation *,gpointer);
void       gevas_icon_list_force(icon_list *il);
int        gevas_bg_update(icon_list *,int,int);
