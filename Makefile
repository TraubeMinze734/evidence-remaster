# evidence -- plain Makefile replacing the 2003 autotools build
# run ./configure.sh first (writes config.h + config.mk)

TOP := $(CURDIR)
include config.mk

.DEFAULT_GOAL := all

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
GUI_SRC += $(foreach d,$(GTK2_SUBDIRS),$(filter-out %shelf.c,$(wildcard $(d)/*.c))) \
           src/gui/gtk2/about/about.c
else
GUI_SRC += $(foreach d,$(GTK2_SUBDIRS),$(wildcard $(d)/*.c)) \
           src/gui/gtk2/about/about.c \
           src/gui/gtk2/icon_view/icon_view.c \
           src/gui/gtk2/icon_view/gevas_light.c \
           src/gui/gtk2/icon_view/gevas_bind.c
endif
# theme + theme_bind are Evas-guarded internally; every backend needs them
GUI_SRC += src/gui/efl/common/theme.c src/gui/efl/common/theme_bind.c
# without a canvas there is no icon-list backend; provide no-op stubs
ifneq ($(EFL),yes)
GUI_SRC += src/gui/iconlist_stub.c
endif
endif

# efl backend
ifeq ($(EFL),yes)
EFL_COMMON_SRC := $(wildcard src/gui/efl/common/*.c)
EFL_ICONVIEW_SRC := $(wildcard src/gui/efl/icon_view/*.c)
endif

OBJS := $(CORE_OBJ) $(GUI_COMMON_OBJ) $(THUMBNAILER_OBJ) $(GUI_SRC:.c=.o) \
        $(EFL_COMMON_SRC:.c=.o) $(EFL_ICONVIEW_SRC:.c=.o)

ifeq ($(GTK2)$(EFL),nono)
OBJS += src/gui/headless.o
endif

# --- plugins -----------------------------------------------------------------
# built into ./lib/evidence/... so they are found relative to the binary
# NOTE: 'all' must stay the first target in this file
PLUGINS := lib/evidence/backend/posix.so lib/evidence/backend/null.so \
           lib/evidence/metadata/folder.so \
           lib/evidence/actions/null.so

PLUG_CFLAGS := $(BASE_CFLAGS) $(CORE_CFLAGS)
PLUG_LINK    := -shared -fPIC $(CORE_LIBS)

lib/evidence/backend/posix.so: src/providers/backends/file_posix.c src/providers/backends/file_direct.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ src/providers/backends/file_posix.c src/providers/backends/file_direct.c

lib/evidence/backend/null.so: src/providers/backends/file_null.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<

lib/evidence/metadata/folder.so: src/providers/folder/folder.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<

lib/evidence/actions/null.so: src/actions/null/null.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<

# --- EFL theme engines -------------------------------------------------------
# engine plugins (scrollbars, tooltips, icon frames, rubber-bands, ...) are
# dlopen()ed by theme.c/theme_bind.c from lib/evidence/gui/efl/engines/.
# engines depending on long-removed EFL libs (Edb, Ebits, Ebg) are not built.
ifeq ($(EFL),yes)
ENGINE_SRC := $(filter-out src/gui/efl/engines/e_bg.c src/gui/efl/engines/sb_edb.c \
                      src/gui/efl/engines/rb_edb.c src/gui/efl/engines/bg_ebg.c \
                      src/gui/efl/engines/bg_ebg_trans.c, \
                      $(wildcard src/gui/efl/engines/*.c))
ENGINES := $(patsubst src/gui/efl/engines/%.c,lib/evidence/gui/efl/engines/%.so,$(ENGINE_SRC))
ENGINE_CFLAGS := $(BASE_CFLAGS) $(CORE_CFLAGS) $(EVAS_CFLAGS) -I$(TOP)/src -I$(TOP)/src/gui/common \
                 -I$(TOP)/src/gui/efl/common -I$(TOP)/src/gui/efl/engines -fPIC
ENGINE_LINK := -shared
lib/evidence/gui/efl/engines/%.so: src/gui/efl/engines/%.c config.h
	@mkdir -p $(dir $@)
	$(CC) $(ENGINE_CFLAGS) $(ENGINE_LINK) -o $@ $<
endif

plugins: $(PLUGINS) $(ENGINES)

# --- rules ---------------------------------------------------------------------
all: evidence plugins

evidence: $(OBJS)
	$(CC) $(BASE_CFLAGS) -o $@ $(OBJS) $(CORE_LIBS) $(GTK2_LIBS) $(EVAS_LIBS) $(CAIRO_LIBS) $(X11_LIBS) $(LDLIBS)

%.o: %.c config.h
	$(CC) $(BASE_CFLAGS) $(CORE_CFLAGS) \
	  $(if $(filter src/gui/gtk2/%,$<),$(GTK2_CFLAGS),) \
	  $(if $(filter src/gui/efl/%,$<),$(EVAS_CFLAGS),) \
	  -c -o $@ $<

config.h config.mk: configure.sh
	./configure.sh

clean:
	rm -f $(OBJS) evidence
	rm -rf lib

distclean: clean
	rm -f config.h config.mk

.PHONY: all clean distclean plugins
