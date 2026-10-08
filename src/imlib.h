#include "config.h"

#ifdef HAVE_IMLIB2
#  include <pthread.h>
   extern pthread_mutex_t imlib_mutex;    /* frontend and backend shouldn't use imlib2 at the same time, it's not threadsafe */
#  define IMLIB_ACQUIRE pthread_mutex_lock(&imlib_mutex)
#  define IMLIB_RELEASE pthread_mutex_unlock(&imlib_mutex)
#else
#  define IMLIB_ACQUIRE
#  define IMLIB_RELEASE
#endif
