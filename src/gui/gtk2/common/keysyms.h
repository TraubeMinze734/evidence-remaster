#include <gdk/gdkkeysyms.h>
#if GTK_CHECK_VERSION(3,0,0)
/* GTK3 renamed the keysyms to GDK_KEY_*; the compat header keeps the
   2005-era GDK_* names used throughout evidence. */
#include <gdk/gdkkeysyms-compat.h>
#endif
