#include "config.h"

#ifdef HAVE_X
#  include <X11/Xlib.h>
Display *get_display(void);
Window   get_root_window(void);
Window   get_widget_window(void *w);
#endif
