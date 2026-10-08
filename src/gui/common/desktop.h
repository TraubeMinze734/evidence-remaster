#include "config.h"
#ifdef HAVE_X
#  include <X11/Xlib.h>
void window_as_desk(Display *disp,Window win,int w,int h);
void desktop_click(int mx,int my,int button,int pressed,int state,unsigned long serial,unsigned long timestamp);
#endif
