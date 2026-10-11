/** @file  info_dial.c
 *  @brief file properties dialog.
 *  shared-object "info_dial.so", stand-alone "effigy"
 *  GPL, part of the evidence project.
 */

#include "userconfig.h"

#include <gtk/gtk.h>
#if GTK_CHECK_VERSION(3,0,0)
/* GtkTooltips was replaced by the widget-owned GtkTooltip API in GTK3;
   keep a void typedef so the (unused under GTK3) plumbing compiles. */
typedef void GtkTooltips;
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>    /* tolower() */
#include <libgen.h>   /* basename()/dirname() */

#include <pwd.h>
#include <grp.h>
#include <sys/types.h>

#include "evidence.h"
#include "gui/common/alert.h"
#include "file.h"
#include "meta.h"
#include "gui/common/icon_path.h"       /* icon_path_get_always() */
#include "gui/gtk2/common/icon.h"       /* pixbuf_get_by_path_and_resize() */
#include "mime.h"       /* MIME_FOLDER */
#include "util.h"
#ifdef STANDALONE
#  include "tree.h"
#endif

#include "acl_tags.h"
#ifdef HAVE_LIBACL
#  include <sys/acl.h>
#else
#  define ACL_UNDEFINED_TAG  (0x00)
#  define ACL_USER_OBJ       (0x01)
#  define ACL_USER           (0x02)
#  define ACL_GROUP_OBJ      (0x04)
#  define ACL_GROUP          (0x08)
#  define ACL_MASK           (0x10)
#  define ACL_OTHER          (0x20)
#endif

#define PAD  8
#define PPAD 4
#define ROWS 64

#define ACL_BUT_ADD   "Add"
#define ACL_BUT_DEL   "Delete"
#define ACL_BUT_MASK  "Recalculate"
#define ACL_BUT_BLIND "Mandatory entry"



typedef struct _ace {
  int   type;
  char *qual;
  char  permset[4];
} ace;



typedef struct _noir_file {
  long        sn;                 /* session serial */
  noir_md    *md;                 /* meta-data */
  GtkWidget  *dial,
             *contents;
  GtkButton  *apply,
             *close;
  gchar      *file; } noir_file;



static void       effigy_dial_populate(noir_file *nf);
static GtkWidget *ace_new(GtkWidget *table,noir_meta *m,int *cptr,noir_file *nf);






/*****************************************************************************/
/* figure out what has changed and needs to be written back to the file */



/**
 * check an entry-field for changes. adjust list of meta-data where needed.
 * @param w     the GTK entry-field
 * @param data  the field's meta-data
 */
static void effigy_check_entry(GtkEntry *w,gpointer data) {
  const char  *key=gtk_widget_get_name(GTK_WIDGET(w)),
              *utf8=gtk_entry_get_text(w);
  char        *val,*locale=NULL;
  gsize        foo;
  noir_meta   *m;
  noir_md     *md=((noir_file *)data)->md;

  val=locale=g_locale_from_utf8(utf8,-1,&foo,&foo,NULL);

  if(!(m=meta_in_group(md,key,NULL))) {
    char *grp=NULL;
    E(0,"  +++ %s => %s\n",key,val);
    /* FIXME: should determine grp */
    meta_add(md,META_DO,META_STR,key,NULL,grp,val,NULL); }
  else if((meta_get_flags(m)==META_RW)||(meta_get_flags(m)==META_DO)) {
    if(meta_get_type(m)==META_UTF8)
      val=(char *)utf8;

    if(strcmp(meta_get_val(m),val)) {
      if(!strlen(val)) {
        E(0,"  --- %s => %s\n",key,val); }
      else {
        E(0,"  *** %s => %s\n",key,val); }
      meta_set_flags(m,META_DO);
      meta_set_string(m,val,META_OLD); }
    else {
      E(0,"  === %s => %s\n",key,val); }}
  g_free(locale); }



/**
 * check a widget (container or entry-field) for changes
 * @param w     the GTK widget
 * @param data  the field's meta-data
 */
static void effigy_check_widget(GtkWidget *w,gpointer data) {
  if(GTK_IS_ENTRY(w))
    effigy_check_entry(GTK_ENTRY(w),data);
  else if(GTK_IS_CONTAINER(w))
    gtk_container_foreach(GTK_CONTAINER(w),effigy_check_widget,data); }






/*****************************************************************************/
/* pop-up menus */



/**
 * callback: something was selected in pop-up menu; update entry-field.
 * @param item  the item that was selected ("data" holds ASCII item to fill into entry-field)
 * @param data  the meta-data item for this entry-field
 */
static void effigy_popup_clicked(GtkWidget *item,gpointer data) {
  noir_meta *m=(noir_meta *)data;
  char      *ascii=(char *)g_object_get_data(G_OBJECT(item),"data");
  gsize      foo;
  gchar     *utf8=g_locale_to_utf8(ascii,-1,&foo,&foo,NULL);
  gtk_entry_set_text(GTK_ENTRY(meta_get_user3(m)),utf8);
  g_free(utf8); }



/**
 * add an item to a pop-up menu
 * @param menu   the GTK menu to add the item to
 * @param title  the label for the item
 * @param data   the meta-data entry for this field
 */
static inline void effigy_popup_add(GtkWidget *menu,char *title,gpointer data) {
  GtkWidget *item=gtk_menu_item_new_with_mnemonic(title);
  g_signal_connect(item,"activate",G_CALLBACK(effigy_popup_clicked),data);
  g_object_set_data(G_OBJECT(item),"data",title);
  gtk_menu_shell_append((GtkMenuShell *)menu,item); }



/**
 * popbutton was clicked, open the pop-up for this entry-field
 * @param data    the meta-data for this entry-field
 * @param button  the button whick was clicked
 */
static void effigy_popbutton_clicked(gpointer data,GtkButton *button) {
  noir_meta  *m=(noir_meta *)data;
  char      **p=meta_get_user1(m);
  GtkWidget  *menu=gtk_menu_new();

  while(p&&*p) {
    effigy_popup_add(menu,*p,m);
    p++; }

  gtk_widget_show_all(menu);
  gtk_menu_popup(GTK_MENU(menu),NULL,NULL,NULL,NULL,3,0L); }






/*****************************************************************************/
/* helpers */



/**
 * make a label for an item. make it clickable if we have data for a pop-up.
 * @return a GtkWidget (the label)
 * @param m  the meta-data
 */
static inline GtkWidget *effigy_make_label(noir_meta *m) {
  gsize      foo;
  gchar     *utf8=g_locale_to_utf8(meta_get_key(m),-1,&foo,&foo,NULL);
  GtkWidget *key=NULL;

  if(meta_get_user1(m)) {  /* make label clickable (for popup) */
    key=gtk_button_new_with_label(utf8);
    g_signal_connect_swapped(G_OBJECT(key),"clicked",G_CALLBACK(effigy_popbutton_clicked),m); }
  else {          /* boring non-clickable label */
    key=gtk_label_new(utf8);
    gtk_misc_set_alignment(GTK_MISC(key),0,1); }
  g_free(utf8);
  return key; }



/**
 * add a row to a notebook
 * @param table  the table to add to
 * @param key    the label
 * @param val    the field
 * @param tt     the tooltips-object
 * @param m      the meta-data item for this row
 * @param count  the index of this row
 */
static inline void effigy_table_add_row(GtkWidget *table,
                                        GtkWidget *key,GtkWidget *val,
                                        GtkTooltips *tt,noir_meta *m,int *count) {
  int cnt=*count;

  if(meta_get_user1(m))
    meta_set_user3(m,val);

  if(!gtk_widget_get_has_window(key)) {
    GtkWidget *w=gtk_event_box_new();
    if(w) {
      gtk_container_add(GTK_CONTAINER(w),key);
      key=w; }}

  /* note original key on value-widget */
  g_object_set_data(G_OBJECT(val),"data",key);

  if(!gtk_widget_get_has_window(val)) {
    GtkWidget *w=gtk_event_box_new();
    if(w) {
      gtk_container_add(GTK_CONTAINER(w),val);
      val=w; }}

  if(tt&&meta_get_desc(m)) {
    const char *mdesc=meta_get_desc(m);
#if GTK_CHECK_VERSION(3,0,0)
    gtk_widget_set_tooltip_text(key,mdesc);
    gtk_widget_set_tooltip_text(val,mdesc);
    (void)tt; }
#else
    gtk_tooltips_set_tip(GTK_TOOLTIPS(tt),key,mdesc,mdesc);
    gtk_tooltips_set_tip(GTK_TOOLTIPS(tt),val,mdesc,mdesc); }
#endif


  gtk_table_attach(GTK_TABLE(table),key,0,1,cnt,cnt+1,
                   (GtkAttachOptions) (GTK_FILL),
                   (GtkAttachOptions) (0),0,0);
  gtk_table_attach(GTK_TABLE(table),val,1,3,cnt,cnt+1,
                   (GtkAttachOptions) (GTK_FILL | GTK_EXPAND),
                   (GtkAttachOptions) (0),0,0);

  *count=cnt+1; }






/*****************************************************************************/
/* ACL fu */




/**
 * recalc ACL-mask -- if perm is set in any ACE, set it in mask
 * @param w     the GTK widget
 * @param data  the ACL struct to return the data in
 */
static void effigy_acl_check_mask(GtkWidget *w,gpointer data) {
  const char *n=gtk_widget_get_name(w),   /* widget's name (R, W, X, ...) */
             *m;                          /* label-text (User, Group, ...) */
  ace        *buf=(ace *)data;            /* the mask we're composing */

  /* if it's a label, determine ACL's type */
  if(GTK_IS_LABEL(w)) {
    m=gtk_label_get_text(GTK_LABEL(w));
    if(!strcmp(m,ACL_TAG_GROUP))
      buf->type=ACL_GROUP_OBJ;
    else if(!strcmp(m,ACL_TAG_NGROUP))
      buf->type=ACL_GROUP;
    else if(!strcmp(m,ACL_TAG_NUSER))
      buf->type=ACL_USER;
    else
      buf->type=ACL_UNDEFINED_TAG; }

  /* if it's a button, set bit in ACE if button is selected */
  else if(GTK_IS_BUTTON(w)) {
    int  p=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    char s='-';

    if(p)
      switch((s=tolower(n[0]))) {
        case 'r': buf->permset[0]=s; break;
        case 'w': buf->permset[1]=s; break;
        case 'x': buf->permset[2]=s; E(1,"MASK: (%d)  %s\n",buf->type,buf->permset); break; }}

  /* if they gave us a container, recurse (fallback) */
  else if(GTK_IS_CONTAINER(w))
    gtk_container_foreach(GTK_CONTAINER(w),effigy_acl_check_mask,data); }



/**
 * rebuild ACL table after an ACE was added or removed
 * @param nf     the file'n'dial struct
 * @param r      the meta-data of the ACE that was modified
 * @param table  the GTK table that holds the ACL
 */
static void effigy_acl_table_rebuild(noir_file *nf,noir_meta *r,GtkWidget *table) {
  noir_meta   *f,*a;
  int          cnt=-1;
  GtkWidget   *key,
              *val,
              *parent=table->parent;
#if GTK_CHECK_VERSION(3,0,0)
  GtkTooltips *tt=NULL;
#else
  GtkTooltips *tt=gtk_tooltips_new();
#endif

  gtk_container_remove(GTK_CONTAINER(parent),GTK_WIDGET(table));

  for(f=NULL,a=meta_get_1st(nf->md);a;a=meta_get_next(a)) {
    if((meta_get_type(a)==META_ACL)&&
       !strcmp(meta_get_group(a),meta_get_group(r))) {
      if(!f)
        f=a;
      cnt++; }}

  table=gtk_table_new(3,cnt,FALSE);
  gtk_table_set_row_spacings(GTK_TABLE(table),4);
  gtk_table_set_col_spacings(GTK_TABLE(table),4);

  for(a=f;a;a=meta_get_next(a)) {
    if((meta_get_type(a)==META_ACL)&&(meta_get_flags(a)!=META_EF)&&
       meta_get_val(a)&&!strcmp(meta_get_group(a),meta_get_group(r))) {
      key=effigy_make_label(a);
      val=ace_new(table,a,&cnt,nf);
      effigy_table_add_row(table,key,val,tt,a,&cnt); }}

  for(a=f;a;a=meta_get_next(a)) {
    if((meta_get_type(a)==META_ACL)&&(meta_get_flags(a)==META_EF)&&
       meta_get_val(a)&&!strcmp(meta_get_group(a),meta_get_group(r))) {
      key=effigy_make_label(a);
      val=ace_new(table,a,&cnt,nf);
      effigy_table_add_row(table,key,val,tt,a,&cnt); }}

  gtk_container_add(GTK_CONTAINER(parent),GTK_WIDGET(table));
  gtk_widget_show_all(table); }



/**
 * get new ACE's perms and qualifier from dialog and put in ACE struct
 * ("add ACE"-button was clicked)
 * @param w     the widget to look at
 * @param data  the ACE-struct
 */
static void effigy_ace_get(GtkWidget *w,gpointer data) {
  const char *n=gtk_widget_get_name(w);
  ace        *buf=(ace *)data;

  /* entry-field (qualifier) */
  if(GTK_IS_ENTRY(w)) {
    const gchar *utf8=gtk_entry_get_text(GTK_ENTRY(w));
    gsize        foo;

    buf->qual=g_locale_from_utf8(utf8,-1,&foo,&foo,NULL); }

  /* buttons (r/w/x/...) */
  else if(GTK_IS_BUTTON(w)) {
    int p=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    char s='-';

    if(p)
      switch((s=tolower(n[0]))) {
        case 'r': buf->permset[0]=s; break;
        case 'w': buf->permset[1]=s; break;
        case 'x': buf->permset[2]=s; break; }}}



/**
 * find ACE (so we can avoid duplicates)
 * duplicates are only possibly for named user/named group entries,
 * so 'qual' will always be set.
 * @return an error-code
 * @param md    the list of the meta-data
 * @param r     the meta-data of the potential new ACE
 * @param key   the key/label (user, group, named user, ...)
 * @param qual  the qualifier (user/group for named user/named group)j
 */
static int effigy_ace_find(noir_md *md,noir_meta *r,char *key,char *qual) {
  noir_meta *a;
  char      *q=NULL;
  size_t     l;

  esprintf(&q,"%s:",qual);
  if(!q)
    return NOIR_OOM;
  l=strlen(q);

  for(a=meta_get_1st(md);a;a=meta_get_next(a)) {
    if((meta_get_type(a)==META_ACL)&&(meta_get_flags(a)!=META_EF)&&
       meta_get_val(a)&&!strcmp(meta_get_group(a),meta_get_group(r))) {
      if(!strncmp(meta_get_val(a),q,l)) {
        free(q);
        return NOIR_SUCC; }}}

  free(q);
  return NOIR_NOTFOUND; }



/**
 * callback: one of the ACE buttons (add, delete, recalc, ...) was clicked
 * @return TRUE for handled
 * @param ebox   the event-box containing the icon that was clicked
 * @param event  the GDK button-event
 * @param data   the row's meta-data
 */
static gboolean effigy_ace_button_clicked(GtkWidget *ebox,GdkEventButton *event,gpointer data) {
  noir_meta     *m=(noir_meta *)data;
  const char    *n=gtk_widget_get_name(ebox);
  GtkWidget     *parent=ebox->parent,*top=parent->parent,*table=top->parent;
  noir_provider *prv=NULL;
  noir_file     *nf=(noir_file *)g_object_get_data(G_OBJECT(ebox),"data");


  if(!strcmp(n,ACL_BUT_BLIND))
    form_alert(NULL,"\"%s\" is a mandatory entry.\nIt can not be deleted.",meta_get_key(m));


  else if(!strcmp(n,ACL_BUT_ADD)) {                             /* Add ACE! */
    int ret=FALSE;
    ace buf;

    buf.qual=NULL;
    strcpy(buf.permset,"---");

    gtk_container_foreach(GTK_CONTAINER(parent),effigy_ace_get,&buf);

    if(buf.qual) {
      if(strlen(buf.qual)) {
        char *val=NULL;

        esprintf(&val,"%s:%s",buf.qual,buf.permset);
        ret=TRUE;
        if(!val)
          form_alert(NULL,"Out of memory, cannot add " ACL_TAG_ACEDSC "!?");
        else if(!strcmp(meta_get_key(m),ACL_TAG_NEWUSR)) {
          struct passwd *pwd;
          if(!(pwd=getpwnam(buf.qual)))
            form_alert(NULL,"I cannot create ACEs for unknown users.");
          else if(effigy_ace_find(nf->md,m,ACL_TAG_NUSER,buf.qual)==NOIR_SUCC)
            form_alert(NULL,"There already is an ACE for %s.",buf.qual);
          else {
            meta_add(nf->md,META_DO,META_ACL,ACL_TAG_NUSER,ACL_TAG_ACEDSC,meta_get_group(m),val,prv);
            m=meta_in_group(nf->md,ACL_TAG_NUSER,meta_get_group(m));
            E(0,"=> %s %s with %s\n",meta_get_key(m),buf.qual,buf.permset);
            effigy_acl_table_rebuild(nf,m,table); }
          free(val); }

        else {
          struct group *grp;
          if(!(grp=getgrnam(buf.qual)))
            form_alert(NULL,"I cannot create ACEs for unknown groups.");
          else if(effigy_ace_find(nf->md,m,ACL_TAG_NGROUP,buf.qual)==NOIR_SUCC)
            form_alert(NULL,"There already is an ACE for %s.",buf.qual);
          else {
            meta_add(nf->md,META_DO,META_ACL,ACL_TAG_NGROUP,ACL_TAG_ACEDSC,meta_get_group(m),val,prv);
            m=meta_in_group(nf->md,ACL_TAG_NGROUP,meta_get_group(m));
            E(0,"=> %s %s with %s\n",meta_get_key(m),buf.qual,buf.permset);
            effigy_acl_table_rebuild(nf,m,table); }
          free(val); }}
      g_free(buf.qual); }


    if(!ret) {
      char *txt=!strcmp(meta_get_key(m),ACL_TAG_NEWUSR)?
        "Sorry, you must provide a name\nfor the new user.":
        "Sorry, you must provide a name\nfor the new group.";
      form_alert(NULL,txt); }}


  else if(!strcmp(n,ACL_BUT_DEL)) {                             /* Del ACL! */
    meta_set_string(m,NULL,META_OLD);
    effigy_acl_table_rebuild(nf,m,table);
    meta_del(nf->md,(char *)meta_get_key(m),(char *)meta_get_group(m)); }


  else if(!strcmp(n,ACL_BUT_MASK)) {                         /* Recalc Mask */
    char *n=NULL;
    ace   buf;

    buf.qual=NULL;
    strcpy(buf.permset,"---");
    gtk_container_foreach(GTK_CONTAINER(top->parent),effigy_acl_check_mask,&buf);
    esprintf(&n,":%s",buf.permset);
    if(n) {
      meta_set_string(m,(char *)meta_get_prvs(m),META_OLD);
      meta_set_string(m,n,META_OLD); }
    effigy_acl_table_rebuild(nf,m,table); }


  else
    E(0,"Unknown ACL-button type \"%s\"...\n",n);

  return TRUE; }



/**
 * a permission toggle-button (R/W/X/...) in an ACE was toggled. update ACE.
 * @param widget  the toggle-button
 * @param m       the row's meta-data
 */
static void effigy_ace_perm_toggled(GtkToggleButton *widget,noir_meta *m) {
  const char *n=gtk_widget_get_name(GTK_WIDGET(widget)),
             *s="RWX";
  char       *p=NULL,
             *q;

  if((meta_get_flags(m)==META_RW)&&!meta_get_prvs(m)) {
    meta_set_string(m,(char *)meta_get_val(m),META_OLD);
    meta_set_flags(m,META_DO); }

  if((p=strchr(s,n[0]))) {
    if((q=strrchr(meta_get_val(m),':')))
      q++;
    else
      q=meta_get_val(m);
    p=p-s+q;
    *p=gtk_toggle_button_get_active(widget)?tolower(n[0]):'-'; }

  E(0,"\"%s::%s\" \"%s\": %s\n",
    meta_get_group(m),meta_get_key(m),(char *)meta_get_prvs(m),(char *)meta_get_val(m)); }



/**
 * create an ACE-row
 * @return a GTK HBox holding the new row, or NULL
 * @param table  the GTK table to add the row to
 * @param m      the meta-data for this row
 * @param cptr   row-count
 * @param nf     the file'n'dial struct
 */
static GtkWidget *ace_new(GtkWidget *table,noir_meta *m,int *cptr,noir_file *nf) {
  char      *q,*r;
  GtkWidget *p,*e,*val;
  int        cnt=cptr?*cptr:0;

  val=gtk_hbox_new(FALSE,0);

  r=strdup(meta_get_val(m));
  if((q=strchr(r,':')))
    *(q++)='\0';

  /* actual permissions (rwx...) */
  p=gtk_toggle_button_new_with_label("R");
  gtk_widget_set_name(p,"R");
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(p),FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p),strchr(q,'r')?TRUE:FALSE);
  gtk_box_pack_start(GTK_BOX(val),p,FALSE,FALSE,PPAD);
  g_signal_connect(G_OBJECT(p),"toggled",G_CALLBACK(effigy_ace_perm_toggled),m);

  p=gtk_toggle_button_new_with_label("W");
  gtk_widget_set_name(p,"W");
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(p),FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p),strchr(q,'w')?TRUE:FALSE);
  gtk_box_pack_start(GTK_BOX(val),p,FALSE,FALSE,0);
  g_signal_connect(G_OBJECT(p),"toggled",G_CALLBACK(effigy_ace_perm_toggled),m);

  p=gtk_toggle_button_new_with_label("X");
  gtk_widget_set_name(p,"X");
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(p),TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p),strchr(q,'x')?TRUE:FALSE);
  gtk_box_pack_start(GTK_BOX(val),p,FALSE,FALSE,PPAD);
  g_signal_connect(G_OBJECT(p),"toggled",G_CALLBACK(effigy_ace_perm_toggled),m);

  if(!strcmp(meta_get_key(m),ACL_TAG_NEWUSR)||
     !strcmp(meta_get_key(m),ACL_TAG_NEWGRP)) {
    /* qualifier box */
    if(!strcmp(meta_get_key(m),ACL_TAG_NEWUSR)) {
      p=gtk_hbox_new(FALSE,0);
      gtk_table_attach(GTK_TABLE(table),p,0,3,cnt,cnt+1,
                       (GtkAttachOptions) (GTK_FILL | GTK_EXPAND),
                       (GtkAttachOptions) (GTK_FILL | GTK_EXPAND),0,0);
      cnt++; }

    p=gtk_image_new_from_stock(GTK_STOCK_ADD,GTK_ICON_SIZE_SMALL_TOOLBAR);
    e=gtk_event_box_new();
    gtk_widget_set_name(e,ACL_BUT_ADD);
    gtk_container_add(GTK_CONTAINER(e),p);
    g_signal_connect(G_OBJECT(e),"button_press_event",G_CALLBACK(effigy_ace_button_clicked),m);
    gtk_box_pack_start(GTK_BOX(val),e,FALSE,FALSE,PAD);

    p=gtk_entry_new();
    gtk_widget_set_name(p,meta_get_key(m));
    gtk_box_pack_start(GTK_BOX(val),p,FALSE,FALSE,PAD); }

  else {
    char *i;
    char *n;
    if(!strcmp(meta_get_key(m),ACL_TAG_NUSER)||
       !strcmp(meta_get_key(m),ACL_TAG_NGROUP)) {
      i=GTK_STOCK_DELETE;
      n=ACL_BUT_DEL; }
    else if(!strcmp(meta_get_key(m),ACL_TAG_MASK)) {
      i=GTK_STOCK_REFRESH; /* GTK_STOCK_EXECUTE; */
      n=ACL_BUT_MASK; }
    else {
      i=GTK_STOCK_YES;
      n=ACL_BUT_BLIND; }

    /* delete icon */
    p=gtk_image_new_from_stock(i,GTK_ICON_SIZE_SMALL_TOOLBAR);
    e=gtk_event_box_new();
    gtk_widget_set_name(e,n);
    gtk_container_add(GTK_CONTAINER(e),p);
    g_signal_connect(G_OBJECT(e),"button_press_event",G_CALLBACK(effigy_ace_button_clicked),m);
    gtk_box_pack_start(GTK_BOX(val),e,FALSE,FALSE,PAD); }

  /* save top-level struct so we can modify meta-data list */
  g_object_set_data(G_OBJECT(e),"data",nf);

  /* qualifier, if any (user-name, group-name, ...) */
  p=gtk_label_new(r);
  gtk_box_pack_start(GTK_BOX(val),p,FALSE,FALSE,PAD);

  free(r);

  if(cptr)
    *cptr=cnt;

  return val; }



/*****************************************************************************/
/* main dial fu */



/**
 * callback: one of "Apply" and "Close" was clicked
 * @param data    the file'n'dial struct
 * @param button  the button that was clicked
 */
static void effigy_dial_clicked(gpointer data,GtkButton *button) {
  noir_file *nf=(noir_file *)data;

  if(!nf)
    return;

  if(button==nf->apply) {
    effigy_check_widget((GtkWidget *)nf->dial,nf);
    meta_update(nf->md,NULL);
    gtk_widget_destroy(GTK_WIDGET(nf->dial));  /* hotfix ### */
#if 0
    effigy_dial_populate(nf);
#endif
  }
  else {
    gtk_widget_destroy(GTK_WIDGET(nf->dial)); }}



/**
 * callback: inspector window was destroyed, release admin structs
 * @param data  file'n'dial struct
 * @param w     the window that was destroyed
 */
static void effigy_dial_destroyed(gpointer data,GtkWidget *w) {
  noir_file *nf=(noir_file *)data;
  meta_dst(&(nf->md));
  if(nf->file) {
    g_free(nf->file);
    nf->file=NULL;
#ifdef SELECTION_PARALLEL
    free(nf);
#endif
  }}



/**
 * create and populate file'n'dial struct
 * @return file'n'dial struct
 * @param serial    session's serial number
 * @param filename  the file we're inspecting
 * @param md        the file's meta-data
 */
static noir_file *effigy_dial_create(long serial,char *filename,noir_md *md) {
  noir_file *nf;
  if((nf=malloc(sizeof(noir_file)))) {
    memset(nf,0,sizeof(noir_file));
    nf->sn=serial;
    if(!(nf->md=meta_clone(md))) {
      free(nf);
      return NULL; }
    nf->dial=(GtkWidget *)gtk_window_new(GTK_WINDOW_TOPLEVEL);
    nf->file=g_strdup(filename); }
  return nf; }



/*****************************************************************************/
/* assemble main dial */



/**
 * populate dialog window
 * @param nf  file'n'dial info
 */
static void effigy_dial_populate(noir_file *nf) {
  GtkWindow   *window=(GtkWindow *)nf->dial;
  GtkWidget   *notebook=gtk_notebook_new();
  GtkWidget   *key,
              *val;
  GtkWidget   *table=NULL,
              *outer_table,
              *bbox,
              *button,
              *scroll,
              *icon=NULL;
  GdkPixbuf   *pb;
  GtkTooltips *tt=NULL;

  noir_meta   *m=meta_get_1st(nf->md),
              *mime=meta_in_group(nf->md,"MIME",META_DFLT_GRP);
  char        *grp=NULL,
              *utf8;
  int          cnt=-1;
  gsize        foo;



  /* throw out current contents (if any) when rebuilding */
  if(nf->contents) {
    gtk_container_remove(GTK_CONTAINER(nf->dial),GTK_WIDGET(nf->contents));
    nf->contents=NULL; }

  gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook),TRUE);

  /* icon */
  if(!(pb=pixbuf_get_by_path_and_resize(nf->file,ICON_SIZE_IN_LIST,GDK_INTERP_BILINEAR)))
    pb=pixbuf_get_by_path_and_resize(icon_path_get_always(nf->file,meta_get_val(mime)),ICON_SIZE_IN_LIST,GDK_INTERP_BILINEAR);
  if(pb)
    icon=gtk_image_new_from_pixbuf(pb);



  /***************************************************************************/

  /* meta-data */
  while(m) {

    /* make new page in notebook when group changes */
    if(!grp||!meta_get_group(m)||strcmp(grp,meta_get_group(m))) {
      grp=(char *)meta_get_group(m);

#if !GTK_CHECK_VERSION(3,0,0)
      if(tt)
        gtk_tooltips_enable(tt);
      tt=gtk_tooltips_new();
#endif

      /* (inner) table */
      if(mime&&!strcmp(meta_get_val(mime),MIME_FOLDER))
        table=gtk_table_new(3,8,FALSE);
      else
        table=gtk_table_new(3,7,FALSE);

      if(cnt<0) {
        gtk_table_attach(GTK_TABLE(table),icon,0,3,0,1,
                         (GtkAttachOptions)(GTK_FILL),
                         (GtkAttachOptions)(0),0,8);
        cnt=1; }
      else
        cnt=0;

      gtk_table_set_row_spacings(GTK_TABLE(table),4);
      gtk_table_set_col_spacings(GTK_TABLE(table),4);

      /* page in notebook */

      scroll=gtk_scrolled_window_new(NULL,NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
      gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll),table);

      gtk_notebook_append_page(GTK_NOTEBOOK(notebook),scroll,
                               gtk_label_new(grp)); }

    /*************************************************************************/


    key=effigy_make_label(m);

    utf8=g_locale_to_utf8(meta_get_val(m),-1,&foo,&foo,NULL);
    if((meta_get_flags(m)==META_RO)||
       ((meta_get_type(m)!=META_ACL)&&
        (meta_get_type(m)!=META_STR)&&
        (meta_get_type(m)!=META_UTF8)&&
        (meta_get_type(m)!=META_INT))) {
#if 0
      val=gtk_label_new((meta_get_type(m)!=META_UTF8)?utf8:meta_get_val(m));
      gtk_label_set_line_wrap(GTK_LABEL(val),TRUE);
      gtk_widget_set_sensitive(val,FALSE);
      gtk_misc_set_alignment(GTK_MISC(val),0,1);
#else
      GtkTextBuffer *buffer;

      val=gtk_text_view_new();
      buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(val));
      gtk_text_buffer_set_text(buffer,(meta_get_type(m)!=META_UTF8)?utf8:meta_get_val(m),-1);
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(val),GTK_WRAP_WORD);
      gtk_text_view_set_editable(GTK_TEXT_VIEW(val),FALSE);
      gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(val),FALSE);
      gtk_widget_set_sensitive(val,TRUE);
#endif
    }
    else if(meta_get_type(m)==META_ACL)
      val=ace_new(table,m,&cnt,nf);

    else if(meta_get_type(m)==META_INT) {
      val=gtk_spin_button_new_with_range(-1,100000,1);
      gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(val),TRUE);
      gtk_spin_button_set_digits(GTK_SPIN_BUTTON(val),0);
      gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(val),FALSE);
      gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(val),TRUE);
      gtk_widget_set_name(GTK_WIDGET(val),meta_get_key(m));
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(val),(gdouble)atof(utf8)); }

    else {
      val=gtk_entry_new();
      gtk_widget_set_name(val,meta_get_key(m));
      gtk_entry_set_text(GTK_ENTRY(val),(meta_get_type(m)!=META_UTF8)?utf8:meta_get_val(m)); }
    g_free(utf8);

    effigy_table_add_row(table,key,val,tt,m,&cnt);

    m=meta_get_next(m); }

  /***************************************************************************/

#if !GTK_CHECK_VERSION(3,0,0)
  if(tt)
    gtk_tooltips_enable(tt);
#endif

  outer_table=gtk_table_new(3,3,FALSE);
  gtk_table_set_row_spacings(GTK_TABLE(outer_table),2);

  /* ButtonBox Section */
  bbox=gtk_hbutton_box_new();
  gtk_container_set_border_width(GTK_CONTAINER(bbox),5);
  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox),GTK_BUTTONBOX_END);
  gtk_box_set_spacing(GTK_BOX(bbox),10);

  /* button "Apply" */
  button=gtk_button_new_from_stock(GTK_STOCK_APPLY); /* Apply Button */
  nf->apply=(GtkButton *)button;
  g_signal_connect_swapped(G_OBJECT(button),"clicked",G_CALLBACK(effigy_dial_clicked),nf);
  gtk_container_add(GTK_CONTAINER(bbox),button);

  /* button "Close" */
  button=gtk_button_new_from_stock(GTK_STOCK_CLOSE); /* Close Button */
  nf->close=(GtkButton *)button;
  g_signal_connect_swapped(G_OBJECT(button),"clicked",G_CALLBACK(effigy_dial_clicked),nf);
  gtk_container_add(GTK_CONTAINER(bbox),button);

  /* Add widgets to their parents */
  gtk_container_add(GTK_CONTAINER(window),nf->contents=outer_table);
  gtk_table_attach(GTK_TABLE(outer_table),notebook,0,3,0,1,
                   (GtkAttachOptions)(GTK_EXPAND|GTK_FILL),
                   (GtkAttachOptions)(GTK_EXPAND|GTK_FILL),0,0);
  gtk_table_attach(GTK_TABLE(outer_table),bbox,0,3,2,3,
                   (GtkAttachOptions)(GTK_SHRINK|GTK_FILL),
                   (GtkAttachOptions)(GTK_SHRINK|GTK_FILL),0,0);

  gtk_window_resize(GTK_WINDOW(window),302,552);
  gtk_window_set_title(GTK_WINDOW(window),nf->file);
  gtk_container_set_border_width(GTK_CONTAINER(window),5);
  g_signal_connect_swapped(G_OBJECT(window),"destroy",
                           G_CALLBACK(effigy_dial_destroyed),nf);

  gtk_widget_show_all(GTK_WIDGET(window)); }



/*****************************************************************************/
/* plugin API */



/**
 * context-menu action: info-dial
 * @return an error-code
 * @param serial    the serial number of the session that called us
 * @param filename  the file to act on
 * @param md        the file's meta-data
 * @param ac        the context of this action (selection et al.)
 */
int efun(long serial,char *filename,noir_md *md,action_context *ac) {
  noir_file *nf;
  if(!md)
    return NOIR_NODATA;
  else if((nf=effigy_dial_create(serial,filename,md))) {
    effigy_dial_populate(nf);
#ifndef SELECTION_PARALLEL
    while(nf->file)
      mainloop_once();
    free(nf);
#endif
    return NOIR_SUCC; }
  else {
    E(0,"out of memory...\n");
    return NOIR_OOM; }
  return NOIR_FAIL; }   /* unreachable for now */



/*****************************************************************************/
