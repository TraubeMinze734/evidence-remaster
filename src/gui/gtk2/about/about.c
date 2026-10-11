/** @file  about.c
 *  @brief the "about" (or credits-) tab.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <stdlib.h>

#include <stdio.h>

#include "debug.h"
#include "setup.h"
#include "file.h"

#include "src/gui/common/menu.h"
#include "src/gui/common/session.h"

#include "src/gui/gtk2/common/icon.h"



/**
 * fallback callback for mouse-clicks that no better handler exists for.
 * @param widget - the widget that received the event
 * @param ev - the actual event
 * @param data - the session
 */
static gboolean win_clicked(GtkWidget *widget,GdkEventButton *ev,gpointer data) {
  noir_sess     *ns=(noir_sess *)data;

  if((ev->type!=GDK_BUTTON_RELEASE)&&(ev->type!=GDK_BUTTON_PRESS))
    return FALSE;

  if(ns&&ev) {
    if((ev->type==GDK_BUTTON_RELEASE)&&(ev->button==3))
      menu_root_context(ns,NULL,3); }

  return TRUE; }



/**
 * create "about" page in GTK note-book
 * @param ns - the session
 * @param about - path and name of an image representing the current theme, or NULL
 */
GtkWidget *about_init(noir_sess *ns,char *about) {
  GtkWidget *box,*label,*image=NULL;
#ifdef QUIT_BUTTON
  GtkWidget *button;
#endif
  GtkWidget *e;
  char      *ip=NULL;

  box=gtk_hbox_new(FALSE,0);

  if((e=gtk_event_box_new()))
    gtk_container_add(GTK_CONTAINER(e),box);

  label=gtk_label_new(NULL);

  gtk_label_set_markup(GTK_LABEL(label),
    "\n<b><big>Evidence.</big></b>\n<small>by Azundris.</small>\n\n"
    "with additional code by atmos, tillsan, omni, underDOG.\n"
    "concepts by Azundris, Isotopp, tillsan.\n"
    "debugged by catalyst and creepy.\n"
    "special greetings to chimou and #e.\n\n"

    "may contain parts of efsd by cK.\n"
#ifdef HAVE_EVAS2
    "may contain traces of gevas2 by monkeyIQ.\n"
    "uses evas and epeg by the rasterman.\n"
#endif

    "\nReleased under the GNU Public License\n"
    "as v" VERSION " on " __DATE__ ".\n\n"
    "<u><span foreground=\"blue\">http://evidence.sourceforge.net/</span></u>\n");

  if(about) {
    if(!(image=icon_get_by_path_no_resize(about)))
      E(0,"image \"%s\" not found, trying to fall back on default...\n",about); }
  if(!image) {
    if((ip=get_setup_file("evidence.gif")))
      about=ip; }

  if(image||(image=icon_get_by_path_no_resize(about)))
    gtk_box_pack_start(GTK_BOX(box),image,TRUE,TRUE,0);
  else
    E(0,"no image \"%s\" for credits page...\n",about?about:"");

  E(3,"image \"%s\" -> %p\n",about?about:"<none>",image);

  gtk_box_pack_start(GTK_BOX(box),label,TRUE,TRUE,0);

#ifdef QUIT_BUTTON
  button=gtk_button_new_with_mnemonic("_Quit");
  g_signal_connect(button,"clicked",gtk_main_quit,NULL);
  gtk_box_pack_start(GTK_BOX(box),button,FALSE,TRUE,0);
#endif

  gtk_widget_set_name(box,"about_dial");

  if(e)
    g_signal_connect(G_OBJECT(e),"button_release_event",G_CALLBACK(win_clicked),(gpointer)ns);

  if(ip)
    free(ip);

  return e?e:box; }
