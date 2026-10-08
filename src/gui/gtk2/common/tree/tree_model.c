/** @file  tree_model.c
 *  @brief some functions common to GTK tree-view and GTK browser-view
 */

#include "config.h"

#if defined(WITH_TREEVIEW)||defined(WITH_BROWSERVIEW)
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tree.h"
#include "file_lcl.h"    /* file_to_URL() */
#include "gui/gtk2/common/dnd.h"



/**
 * DND: foreach selected row, create an URL and add it to the list
 * @param model  the GTK tree-model the item lives in
 * @param path   the path to the element to add (or NULL)
 * @param iter   the iter of the element to add
 * @param data   the list (none/one/many \n-separated items)
 */
void tree_send_URL(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
  gchar *new=NULL,*file,*list=*((gchar **)data);
  gtk_tree_model_get(model,iter,FILE_FILE,&file,-1);
  if(file) {
    char *myfile=file_to_URL(file);
    if(myfile) {
      if(list) {
        if((new=malloc(strlen(file)+strlen(list)+2))) {
          strcat(strcat(strcpy(new,list),DELIMITER),file);
          g_free(list); }}
      else
        new=strdup(file);
      *((gchar **)data)=new;
      free(myfile); }
    g_free(file); }}



/**
 * DND: foreach selected row, create a string and add it to the list
 * @param model  the GTK tree-model the item lives in
 * @param path   the path to the element to add (or NULL)
 * @param iter   the iter of the element to add
 * @param data   the list (none/one/many \n-separated items)
 */
void tree_send_string(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
  gchar *new=NULL,*file,*list=*((gchar **)data);
  gtk_tree_model_get(model,iter,FILE_FILE,&file,-1);
  if(file) {
    if(list) {
      if((new=malloc(strlen(file)+strlen(list)+2))) {
        strcat(strcat(strcpy(new,list),DELIMITER),file);
        g_free(list); }}
    else
      new=strdup(file);
    *((gchar **)data)=new;
    g_free(file); }}



/****************************************************************************/
/* sorting */
/***********/



/**
 * comparator for sorting a GTK tree-view
 * @return -1,0,1
 * @param model  the GTK tree-model
 * @param a      the 1st GTK tree-iterator
 * @param b      the 2nd GTK tree-iterator
 * @param data   sort-flags
 */
gint tree_sort(GtkTreeModel *model,GtkTreeIter *a,GtkTreeIter *b,gpointer data) {
  gchar     *a_ptr,*b_ptr;
  file_info *a_fi, *b_fi;
  gint       ret=-1;

  gtk_tree_model_get(GTK_TREE_MODEL(model),a,FILE_NAME,&a_ptr,FILE_INFO,&a_fi,-1);
  gtk_tree_model_get(GTK_TREE_MODEL(model),b,FILE_NAME,&b_ptr,FILE_INFO,&b_fi,-1);

  /* don't want a getter for sortflags, speed is crucial here */
  ret=model_sort_fun(*((int *)data),a_ptr,a_fi,b_ptr,b_fi);

  g_free(a_ptr);
  g_free(b_ptr);

  return ret; }



/****************************************************************************/

#endif
