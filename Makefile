# evidence -- plain Makefile replacing the 2003 autotools build
# run ./configure.sh first (writes config.h + config.mk)

TOP := $(CURDIR)
include config.mk

CC      ?= cc
CSTD    ?= gnu89
CFLAGS  ?= -O2 -g
BASE_CFLAGS := $(CFLAGS) -std=$(CSTD) -I$(TOP) -I$(TOP)/src -I$(TOP)/src/gui/common \
               -Wno-implicit-function-declaration -Wno-int-conversion -Wno-sign-compare \
               -rdynamic -fPIC
LDLIBS  += -lpthread -ldl -lm

# --- core objects -----------------------------------------------------------
CORE_SRC := src/action.c src/cache.c src/evidence.c src/events.c src/file.c \
            src/md5c.c src/meta.c src/model.c src/monitors.c src/mounts.c \
            src/properties.c src/search.c src/setup.c src/util.c src/ipc_main.c
COMMON_SRC := $(wildcard src/gui/common/*.c)
GUI_COMMON_OBJ := $(COMMON_SRC:.c=.o)

CORE_OBJ := $(CORE_SRC:.c=.o)
CORE_LIBS :=
CORE_CFLAGS :=

# optional core deps
ifeq ($(GLIB2),yes)
CORE_CFLAGS += $(GLIB2_CFLAGS) -DHAVE_GLIB2
CORE_LIBS   += $(GLIB2_LIBS)
endif
ifeq ($(ECOREIPC),yes)
CORE_CFLAGS += $(ECOREIPC_CFLAGS) -DHAVE_ECORE_IPC
CORE_LIBS   += $(ECOREIPC_LIBS)
endif
ifeq ($(IMLIB2),yes)
CORE_CFLAGS += $(IMLIB2_CFLAGS)
CORE_LIBS   += $(IMLIB2_LIBS)
endif
ifeq ($(PNG),yes)
CORE_CFLAGS += $(PNG_CFLAGS)
CORE_LIBS   += $(PNG_LIBS)
endif
ifeq ($(PCRE),yes)
CORE_CFLAGS += $(PCRE_CFLAGS)
CORE_LIBS   += $(PCRE_LIBS)
endif

# --- thumbnailer --------------------------------------------------------------
THUMBNAILER_SRC := src/thumbnailer/thumbnail.c src/thumbnailer/thumbnail_plugin.c
THUMBNAILER_OBJ := $(THUMBNAILER_SRC:.c=.o)
THUMBNAILER_CFLAGS := $(CORE_CFLAGS)
THUMBNAILER_LIBS := $(CORE_LIBS)

# --- GUI backends ---------------------------------------------------------------
# gtk2 backend
ifeq ($(GTK2),yes)
# any source may #include <gtk/gtk.h> behind #ifdef HAVE_GTK2, so make the
# flags global (the headers/impl guard themselves)
BASE_CFLAGS += $(GTK2_CFLAGS)
GUI_SRC += $(wildcard src/gui/gtk2/*.c)
GTK2_SUBDIRS := src/gui/gtk2/common src/gui/gtk2/common/tree \
                src/gui/gtk2/browser_view src/gui/gtk2/tree_view
# icon_view/shelf need Evas2 (gevas) or GnomeCanvas; gui_gtk2.c only calls
# them behind #if HAVE_EVAS2 || HAVE_GNOMECANVAS, so leave them out of a
# canvas-less build
ifneq ($(EFL),yes)
GUI_SRC += $(foreach d,$(GTK2_SUBDIRS),$(filter-out %shelf.c,$(wildcard $(d)/*.c)))
else
GUI_SRC += $(foreach d,$(GTK2_SUBDIRS),$(wildcard $(d)/*.c)) \
           src/gui/gtk2/icon_view/icon_view.c
endif
endif

# efl backend
ifeq ($(EFL),yes)
EFL_COMMON_SRC := $(wildcard src/gui/efl/common/*.c)
EFL_ICONVIEW_SRC := $(filter-out %gevas_light.c,$(wildcard src/gui/efl/icon_view/*.c))
endif

OBJS := $(CORE_OBJ) $(GUI_COMMON_OBJ) $(THUMBNAILER_OBJ) $(GUI_SRC:.c=.o) \
        $(EFL_COMMON_SRC:.c=.o) $(EFL_ICONVIEW_SRC:.c=.o)

ifeq ($(GTK2)$(EFL),nono)
OBJS += src/gui/headless.o
endif

# --- rules ---------------------------------------------------------------------
all: evidence

evidence: $(OBJS)
	$(CC) $(BASE_CFLAGS) -o $@ $(OBJS) $(CORE_LIBS) $(GTK2_LIBS) $(EVAS_LIBS) $(X11_LIBS) $(LDLIBS)

%.o: %.c config.h
	$(CC) $(BASE_CFLAGS) $(CORE_CFLAGS) \
	  $(if $(filter src/gui/gtk2/%,$<),$(GTK2_CFLAGS),) \
	  $(if $(filter src/gui/efl/%,$<),$(EVAS_CFLAGS),) \
	  -c -o $@ $<

config.h config.mk: configure.sh
	./configure.sh

clean:
	rm -f $(OBJS) evidence

distclean: clean
	rm -f config.h config.mk

.PHONY: all clean distclean
