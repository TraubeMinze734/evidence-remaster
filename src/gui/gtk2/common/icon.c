/** @file  icon.c
 *  @brief load/resize icon-image data; convert to GtkWidget or GdkPixbuf. */

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "evidence.h"
#include "dnd.h"        /* target, targets */
#include "setup.h"      /* get_setup_file() */
#include "file.h"
#include "util.h"       /* hashes */

#include "gui/common/icon_path.h"



static char       *userpic=NULL;



/*****************************************************************/



/**
 * create a GTK-"icon" from an image.
 * most basic procedure -- no labels, conversions, searching, or resizing.
 * called from about::about_init().
 * @param ip - icon-path
 */
GtkWidget *icon_get_by_path_no_resize(const char *ip) {
  GtkWidget *icon=NULL;
  if(ip) {
    icon=gtk_image_new_from_file(ip);
    gtk_widget_set_name(icon,"icon"); }
  return icon; }



/**
 * create a GDK pixbuf from an image.
 * most basic procedure -- no labels, conversions, searching, or resizing.
 * called by various in icon-view.
 * @param ip - icon-path
 */
GdkPixbuf *pixbuf_get_by_path_no_resize(const char *ip) {
  return ip?gdk_pixbuf_new_from_file(ip,NULL):NULL; }



/**
 * create a GDK pixbuf from an image and resize it if necessary.
 *
 * called by various in icon-view, tooltip, shelf, actions.
 * @param ip - icon-path
 * @param size - if image exceeds box of size*size, scale it to fit (retain aspect)
 * @param interpolate - quality: GDK_INTERP_BILINEAR or GDK_INTERP_HYPER
 */
GdkPixbuf *pixbuf_get_by_path_and_resize(const char *ip,int size,int interpolate) {
  int        w,h;
  GdkPixbuf *pb;

  if(!ip||!(pb=gdk_pixbuf_new_from_file(ip,NULL)))
    return NULL;
  w=gdk_pixbuf_get_width(pb);
  h=gdk_pixbuf_get_height(pb);
  if((w>size)||(h>size)) {
    GdkPixbuf *p2;
    if(w>h) {
      h=(int)((h*size)/w);
      w=size; }
    else {
      w=(int)((w*size)/h);
      h=size; }
    p2=gdk_pixbuf_scale_simple(pb,w,h,(GdkInterpType)interpolate);
    g_object_unref(pb);
    pb=p2; }
  return pb; }



/**
 * create a GTK-"icon" for a file.
 *
 * called from browser_view::browser_view_dir() to create column-icons.
 * @param text - the text for the icon's label 
 * @param ip - the file containing the image-data for the icon
 */
static GtkWidget *icon_get_by_path(const char *text,const char *ip) {
  GtkWidget *icon;
  GtkWidget *label;
  GdkPixbuf *pb;

  if(ip&&(icon=gtk_image_new_from_pixbuf(pb=pixbuf_get_by_path_and_resize(ip,ICON_SIZE_IN_LIST,GDK_INTERP_BILINEAR)))) {
    GtkWidget *evbox=gtk_event_box_new(),*vbox=gtk_vbox_new(FALSE,0);

    if(pb)
      g_object_unref(pb);

    gtk_box_pack_start(GTK_BOX(vbox),icon,FALSE,FALSE,0);

    if(text) {
      label=gtk_label_new(text);
      gtk_widget_set_name(label,"label");
      gtk_box_pack_start(GTK_BOX(vbox),label,FALSE,FALSE,0); }

    gtk_container_add(GTK_CONTAINER(evbox),vbox);

    gtk_drag_source_set(GTK_WIDGET(evbox),GDK_BUTTON1_MASK|GDK_SHIFT_MASK|GDK_CONTROL_MASK,target,targets,GDK_ACTION_MOVE|GDK_ACTION_LINK|GDK_ACTION_COPY);
    gtk_drag_dest_set(GTK_WIDGET(evbox),GTK_DEST_DEFAULT_ALL,target,targets,GDK_ACTION_LINK|GDK_ACTION_COPY|GDK_ACTION_MOVE);

    gtk_widget_set_name(vbox,"box");
    gtk_widget_set_name(evbox,"eventbox");
    gtk_widget_set_name(icon,"icon");

    return evbox; }
  return NULL; }



/**
 * create a GTK-"icon" for a file, by thumbnailing it, or by finding an image
 * for its MIME-type.
 *
 * called from browser_view::browser_view_dir() to create column-icons.
 * @param text - the text for the icon's label 
 * @param file - the file the icon stands for
 * @param mime - the file's MIME-type
 */
GtkWidget *icon_get_by_file(const char *text,const char *file,const char *mime) {
  GtkWidget *ret=NULL;

  if(!strncmp(mime,"image/",6))  /* try thumbnailing */
    ret=icon_get_by_path(text,file);

  return ret?ret:icon_get_by_path(text,icon_path_get_always(file,mime)); }



/**
 * deinitialize icon sub-system.
 */
int icon_exit(void) {
  if(userpic) {
    free(userpic);
    userpic=NULL; }
  return NOIR_SUCC; }



/**
 * initialize icon sub-system.
 * tries to find a picture of the user for her home-directory.
 */
int icon_init(void) {
#ifndef STANDALONE
  if(!icon_path_get(home,NULL)) {
    size_t l=strlen(home)+20;
    userpic=malloc(l);
    if(userpic) {
      snprintf(userpic,l,"%s/.gnome2/photo",home);
      if(file_exists(userpic)) {
        icon_path_add(home,userpic); }
      else {
        snprintf(userpic,l,"%s/.gnome/photo",home);
        if(file_exists(userpic))
          icon_path_add(home,userpic);
        else {
          /* get_setup_file() justified; this will probably not resolve anyway,
             so let's not create a dead entry if we can avoid it */
          char *gwp=get_setup_file("gnome-who.png");
          if(gwp) {
            icon_path_add(home,gwp);
            free(gwp); }
          else if((gwp=icon_path_get("/home",NULL)))
            icon_path_add(home,gwp);
          else
            E(1,"could not find any suitable icon for your home-dir, sorry...\n"); }}}}
#endif
  return NOIR_SUCC; }



/*****************************************************************/
