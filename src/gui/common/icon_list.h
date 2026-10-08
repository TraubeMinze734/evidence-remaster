/** @file icon_list.h
 *  @brief generic parts of icon-lists.
 *  right now only used when compiling with specific parts from evas2.c,
 *  should replace gnome-icon-list in the future, however. */

#ifndef ICON_LIST
#  define ICON_LIST

#  define ICON_LIST_WRAP16             16

#  define ICON_LIST_FIND_MAX           32



#  define    ICON_LIST_CLEAR_FIND(il)      (il)->find[0]='\0'

/* the file the icon stands for: /bin/ls */
#  define    ICON_GET_FILENAME(i)          ((i)?((i)->file):NULL)

/* the label underneat the icon: ls */
#  define    ICON_GET_LABEL(i)             ((i)?((i)->label):NULL)

/* the file containing the actual icon-image: /usr/local/share/evidence/icons/bin.png */
#  define    ICON_GET_NAME(i)              ((i)?((i)->name):NULL)



typedef enum _il_flags {
  ILF_NONE=0,
  ILF_NOSCROLL=1,
  ILF_SPATIAL=2 } il_flags;



typedef enum _sb_state {
  SB_NONE=0x0,
  SB_PGUP=0x1,
  SB_PGDN=0x2,
  SB_TRO =0x40,
  SB_BAR =0x80,
  SB_ABS =0x100,
  SB_ARUP=0x200,
  SB_ARDN=0x400 } sb_state;



typedef enum _sl_state {
  SL_NONE=0,
  SL_MID=1,
  SL_TOP=2,
  SL_RIGHT=3,
  SL_BOTTOM=4,
  SL_LEFT=5,
  SL_GRAD=64,
  SL_ALL=~0, } sl_state;



typedef enum _icon_state {
  IE_ALL=     ~0,   /**< all */
  IE_DK=       0,   /**< none */
  IE_DFL=      1,   /**< default */
  IE_SEL=      2,   /**< selected */
  IE_HI=       4,   /**< hilighted (mouse-over) */
  IE_LO=       8,
  IE_CL=    0x10,   /**< clicked (will turn into selected when button released) */
  IE_KBD=   0x20,   /**< keyboard selection */

  IE_TRO=   0x40,   /**< scrollbars only: trough */
  IE_BAR=   0x80,   /**< scrollbars only: bar */
  IE_ABS=  0x100,   /**< scrollbars only: absolute */

  IE_KEY=  0x200,   /**< tooltips only: key */
  IE_VAL=  0x400,   /**< tooltips only: value */
  IE_ID=  0x2000,   /**< tooltips only: group-title */

  IE_THMB=0x4000    /**< has thumbnail */
} icon_state;



typedef enum _element_tag {
  ET_NONE          =0,

  ET_LABEL_TEXT_HI =1,
  ET_LABEL_TEXT_LO =2,
  ET_LABEL_IMAGE   =4,
  ET_LABEL_SHADOW  =8,
  ET_LABEL_OTHER   =0x10,

  ET_EMBLEM_TEXT_HI=0x20,
  ET_EMBLEM_TEXT_LO=0x40,
  ET_EMBLEM_IMAGE  =0x80,
  ET_EMBLEM_SHADOW =0x100,
  ET_EMBLEM_OTHER  =0x200,

  ET_ICON_TEXT_HI  =0x400,   /* text on icon (# of folders inside etc.) */
  ET_ICON_TEXT_LO  =0x800,
  ET_ICON_IMAGE    =0x1000,
  ET_ICON_SHADOW   =0x2000,
  ET_ICON_OTHER    =0x4000,

  ET_KEYED_TEXT_HI =0x8000,
  ET_KEYED_TEXT_LO =0x10000,
  ET_KEYED_IMAGE   =0x20000,
  ET_KEYED_SHADOW  =0x40000,
  ET_KEYED_OTHER   =0x80000,

  ET_TEXT          =ET_LABEL_TEXT_HI|ET_LABEL_TEXT_LO|ET_EMBLEM_TEXT_HI|ET_EMBLEM_TEXT_LO|ET_ICON_TEXT_HI|ET_ICON_TEXT_LO|ET_KEYED_TEXT_HI|ET_KEYED_TEXT_LO,
  ET_RESIZABLE     =ET_LABEL_IMAGE|ET_LABEL_SHADOW|ET_LABEL_OTHER|ET_EMBLEM_IMAGE|ET_EMBLEM_SHADOW|ET_EMBLEM_OTHER|ET_ICON_IMAGE|ET_ICON_SHADOW|ET_ICON_OTHER|ET_KEYED_IMAGE|ET_KEYED_SHADOW|ET_KEYED_OTHER,

  ET_MISC          =0x100000 } element_tag;



/** an icon-element -- icons may be composed of several of these (image, label, emblems, ...) */
typedef struct _icon_el {
  void            *data;      /**< data (eg Evas_Object) */
  int              xo,yo;     /**< offset in icon */
  int              w,h;       /**< size in pixels */
  icon_state       state;     /**< only visible in these states */
  void            *user1;     /**< not used in evidence-proper; may be used in plugins for private "icons", see ix_rephormed.c for an example */
  element_tag      tag;       /**< what kind of element (image, label, ...) is it? */
  struct _icon_el *next;      /**< next icon-element in this icon */
} icon_el;



/** an icon */
typedef struct _icon {
  char            *label;     /**< the text underneath the icon */
  char            *file;      /**< the file it represents */
  char            *name;      /**< the file that holds the icon-image */
  int              x,y;       /**< position */
  int              w,h;       /**< size */
  int              index;     /**< index in icon-list (0..n-1) */
  icon_state       state;     /**< state (selected, ...) */
  icon_el         *els;       /**< linked list of icon-elements */
  void            *user1,     /**< evidence stores fi->intrinsic (posixmd) here */
                  *user2;     /**< evidence stores fi (file_info) here */
  struct _icon    *next;      /**< next icon in this icon-list */
} icon;



/** an icon-list */
typedef struct _icon_list {
  char            *name;          /**< a new for this list (will not strdup() value!) */

  void            *canvas;        /**< canvas */
  void            *bg;            /**< background-object */
  void            *sb_h,*sb_v;    /**< scrollbar objects (horizontal, vertical) */
  void            *rubber;        /**< rubberband object (or NULL) */

  int              xpad,ypad;     /**< pad visible area (offsets for icon #0) */
  int              wpad,hpad;     /**< pad visible area (between icons) */
  int              width,height;  /**< virtual size */
  int              vis_w,vis_h;   /**< visible size */
  int              sx,sy,ss;      /**< scroll offsets, state */
  int              px,py;         /**< prvs   offsets (scroll) */
  int              mxb,myb;       /**< motion offsets (scrolling) - begin pos. */
  int              mxe,mye;       /**< motion offsets (scrolling) - end   pos */
  int              mxs,mys;       /**< motion offsets (scrolling) - scroll-bar pos */
  int              ki;            /**< keybdb icon */
  int              rx1,ry1;       /**< rubber-band coordinates */
  int              rx2,ry2;       /**< guarantee: x1<=x2 ^ y1<=y2 */
  int              mb;            /**< mouse-button */
  int              num_icons;     /**< number of icons in view */
  il_flags         flags;         /**< flags -- inhibit visual updates etc.*/
  int             *sortflags;     /**< how to sort (dirs first, by ext, ...) */
  int              dragflag;      /**< dragging in process? */
  void            *dc;            /**< drag-context */

  char            *sep;           /**< string w/ wrap-chars in icon-labels */
  char            *find;          /**< go to icon starting with... */
  long             find_last;     /**< when was last keystroke for find entrd? */

  void            *theme;         /**< theme for this icon-list */
  void            *extensions;    /**< plugins for this icon-list */

  long             serial;        /**< serial number */

  void            *sess;          /**< session this icon-list belongs to */
  void            *user1;         /**< not used in evidence */
  void            *user2;         /**< not used in evidence */

  icon            *icons;         /**< linked list of icons in this icon-list */
} icon_list;



void       icon_el_dst(icon_el *);
void       icon_el_data_dst(void *);
icon_el   *icon_el_new(icon_state,int,int,int,int,void *,void *);
icon_el   *icon_el_dst_all(icon_el *);
void       icon_el_dbg(icon *,icon_el *);
icon_el   *icon_el_get_by_tag(icon_el *,element_tag,void **);
icon      *icon_el_dst_by_tag(icon *,element_tag);

void       icon_dst_all(icon *);
void       icon_dst(icon *);
void       icon_move(icon *,int,int);

inline int pointer_on_icon_p(icon_list *,icon *,int,int);                /* defined elsewhere */

int        icon_list_icon_remove(icon_list *,icon *,int);
icon      *icon_list_icon_insert(icon_list *,int,const char *,const char *,const char *,const char *,void *,void *,int);
icon      *icon_list_icon_insert_sorted(icon_list *,const char *,const char *,const char *,const char *,void *,void *,int);
#  if 0
icon      *icon_list_icon_append(icon_list *,const char *,const char *,const char *,const char *,void *,void *,int);
#  else
#    define icon_list_icon_append(il,name,file,label,thumb,user1,user2,redraw) \
           icon_list_icon_insert((il),(il)->num_icons,(name),(file),(label),(thumb),(user1),(user2),(redraw))
#  endif

inline int icon_list_icon_in_rubber(icon_list *,icon *);
inline int icon_list_icon_visible(icon_list *il,icon *i);
int        icon_list_get_index_at(icon_list *,int,int);
icon      *icon_list_get_icon_at(icon_list *,int,int);
icon      *icon_list_get_icon_by_index(icon_list *,int);
icon      *icon_list_get_icon_by_file(icon_list *,const char *);

void       icon_list_resized(icon_list *il,long w,long h);
int        icon_list_scroll_abs(icon_list *,int,int);
int        icon_list_scroll_rel(icon_list *,int,int);
int        icon_list_scroll_pg(icon_list *,int,int);

icon_list *icon_list_new(const char *,void *,void *,int,int,int);
void       icon_list_destroy(void *);
icon_list *icon_list_rst(icon_list *);
int        icon_list_layout_all(icon_list *);
int        icon_list_resort(icon_list *);

int        icon_list_get_selection(void *,icon_list *);
int        icon_list_set_selection(icon_list *);

void       sb_update(icon_list *il,int); /* used in icon_list.c and
                                            gevas_bind.c::gevas_icon_list_resized() */

#endif
