/** @file  desktop.c
 *  @brief desktop-related functions
 */

#include "config.h"

/*****************************************************************************/

#ifdef HAVE_X
#  include "x11.h"          /* get_display() */

#  include <stdlib.h>       /* free() */
#  include <X11/Xlib.h>     /* Display, Window, ... */
#  include <X11/Xatom.h>    /* XAtom */
#  include <X11/Xutil.h>    /* XSetWMNormalHints() */
#  include <string.h>       /* memset() */
#  include <stdio.h>        /* fprintf(), stderr, ... */
#  include "debug.h"



/**
 * set an X property on a window.
 * @param disp - X display
 * @param win - X window
 * @param prop - the property
 * @param type - the type of the property (ignored by server, application-layer only)
 * @param bits - number of bits in one element (8/16/32)
 * @param data - the actual data to set
 * @param elements - number of elements in data
 */
static int window_prop_set(Display *disp,Window win,Atom prop,Atom type,int bits,void *data,int elements) {
  int ret=0;

  if(!win)
    win=DefaultRootWindow(disp);

  if(bits!=32)
    ret=XChangeProperty(disp,win,prop,type,bits,PropModeReplace,(unsigned char *)data,elements);
  else {
      long *dat;
      int  *ptr;
      int   i;

      if((dat=malloc(sizeof(long)*elements))) {
        for(ptr=(int *)data,i=0;i<elements;i++)
          dat[i]=ptr[i];
        ret=XChangeProperty(disp,win,prop,type,bits,PropModeReplace,(unsigned char *)dat,elements);
        free(dat); }}
  return ret; }



/**
 * set all the necessary X props to mark the specified window as the desktop,
 * according to the NetWM specs.
 * @param disp  the X-display
 * @param win   the X-window
 * @param w     width
 * @param h     height
 */
void window_as_desk(Display *disp,Window win,int w,int h) {
  int      ret,f;
  Atom     atom;

  /* _NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DESKTOP */
  ret=99;
  if((atom=XInternAtom(disp,"_NET_WM_WINDOW_TYPE_DESKTOP",False))!=None)
    ret=window_prop_set(disp,win,
                        XInternAtom(disp,"_NET_WM_WINDOW_TYPE",False),
                        XA_ATOM,32,&atom,1);
  if(ret>1)
    E(0,"could not set NetWM-Window-Type... (%d, %ld)\n",ret,atom);

  /* _WIN_LAYER */
  ret=99;
  if((atom=XInternAtom(disp,"_WIN_LAYER",False))!=None) {
    long layer=0;
    ret=window_prop_set(disp,win,atom,XA_CARDINAL,32,&layer,1); }
  if(ret>1)
    E(0,"could not set window layer... (%d, %ld)\n",ret,atom);

  if(win!=None) {
    if((ret=XLowerWindow(disp,win))>1)
      E(0,"XLowerWindow returned %d\n",ret); }
  else
    E(0,"view has no window?\n");

  if((atom=XInternAtom(disp,"_WIN_STATE",False))!=None) {
    f=((1<<0)|(1<<8)|(1<<9));
    window_prop_set(disp,win,atom,XA_CARDINAL,32,&f,1); }

  if((atom=XInternAtom(disp,"_WIN_HINTS",False))!=None) {
    f=((1<<0)|(1 <<1)|(1<<2));
    window_prop_set(disp,win,atom,XA_CARDINAL,32,&f,1); }

  { XSizeHints hints;
    long       dummy;

    memset(&hints,0,sizeof(XSizeHints));
    XGetWMNormalHints(disp,win,&hints,&dummy);
    hints.flags|=USSize|PMinSize|PSize;
    hints.min_width=w;
    hints.min_height=h;
    XSetWMNormalHints(disp,win,&hints); }}



/**
 * forward mouse-click to desktop
 * when evidence handles a desktop, it can forward clicks to the "original"
 * background (to call up window-manager menus etc.)
 * @param mx         mouse x
 * @param my         mouse y
 * @param button     mouse buttons (X format)
 * @param pressed    pressed (1) or released (0)?
 * @param state      modifiers (shift etc., X format)
 * @param serial     last X event seen
 * @param timestamp  when did the event happen?
 */
void desktop_click(int mx,int my,int button,int pressed,int state,unsigned long serial,unsigned long timestamp) {
  XEvent   ev;
  Display *display=get_display();

  memset(&ev,0,sizeof(XEvent));

  ev.type=pressed?ButtonPress:ButtonRelease;
  ev.xany.serial=0;
  ev.xany.send_event=FALSE;
  ev.xany.display=display;
  ev.xany.window=DefaultRootWindow(display);
  ev.xbutton.root=DefaultRootWindow(display);
  ev.xbutton.subwindow=None;
  ev.xbutton.time=timestamp;
  ev.xbutton.x=ev.xbutton.x_root=mx;
  ev.xbutton.y=ev.xbutton.y_root=my;
  ev.xbutton.state=state;
  ev.xbutton.button=button;
  ev.xbutton.same_screen=TRUE;

  XSendEvent(display,DefaultRootWindow(display),True,
             pressed?ButtonPressMask:ButtonReleaseMask,&ev); }


#else
void desktop_click(int mx,int my,int button,int pressed,int state,unsigned long serial) { }
#endif
