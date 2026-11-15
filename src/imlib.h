#include "config.h"

#ifdef HAVE_IMLIB2
#  include <pthread.h>
   extern pthread_mutex_t imlib_mutex;    /* frontend and backend shouldn't use imlib2 at the same time, it's not threadsafe */
   /* robust lock: a worker thread cancelled (GC watchdog) while holding the
      mutex must not leave it locked forever -- EOWNERDEAD is recovered with
      pthread_mutex_consistent() inside */
   int  imlib_mutex_lock(void);
#  define IMLIB_ACQUIRE imlib_mutex_lock()
#  define IMLIB_RELEASE pthread_mutex_unlock(&imlib_mutex)
#else
#  define IMLIB_ACQUIRE
#  define IMLIB_RELEASE
#endif
