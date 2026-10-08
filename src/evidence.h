#ifndef EVIDENCE_H
#define EVIDENCE_H

#include <stdio.h>
#include <pthread.h>

#include "debug.h"
#include "errors.h"
#include "meta.h"
#include "model.h"

#include "config.h"
#include "userconfig.h"

#include <time.h>

#define DIR_THEME          "themes"
#define DIR_ICONS          "icons"
#define DIR_CACHE          "cache"
#define DIR_USER           ".evidence"

typedef void session;

extern q_head *queue;

extern char   *hostname;
extern char   *home;
extern char   *temp;
extern char   *appName;  /* FAM wants this spelling */

extern pthread_t main_thread;


#undef  QUIT_BUTTON

#define FORCE_FULLSCREEN

#define MAX_PARS           512

#define PROTOCOL_HANDLERS  "evidence.handlers"



#define ICON_SIZE_IN_LIST     64

/* used in menu_common */
#define GC_UPDATE_TIMESTAMP(a)   (time(NULL)+(a))
#define GC_INTERVAL_MENU   24000
#define GC_INTERVAL_META   60000
#define GC_INTERVAL_MAIN    1000
#endif
