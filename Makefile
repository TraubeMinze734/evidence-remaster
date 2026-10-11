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
# theme + theme_bind are Evas-guarded internally; every backend needs them.
# with EFL they are already covered by EFL_COMMON_SRC below.
ifneq ($(EFL),yes)
GUI_SRC += src/gui/efl/common/theme.c src/gui/efl/common/theme_bind.c
endif
# without a canvas there is no icon-list backend; provide no-op stubs
ifneq ($(EFL),yes)
GUI_SRC += src/gui/iconlist_stub.c
endif
endif

# efl backend
ifeq ($(EFL),yes)
# core files may #include <Evas.h> behind #ifdef HAVE_EVAS2 as well
# (ipc_main.c -> theme.h), so make the flags global like the GTK2 ones
BASE_CFLAGS += $(EVAS_CFLAGS)
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
           lib/evidence/metadata/exif.so \
           lib/evidence/actions/null.so \
           lib/evidence/action/delete.so \
           lib/evidence/action/rename.so \
           lib/evidence/action/mkdir.so \
           lib/evidence/action/info_dial.so \
           lib/evidence/action/newwindow.so \
           lib/evidence/action/backdrop.so \
           lib/evidence/action/tile.so \
           lib/evidence/action/tag2name.so \
           lib/evidence/action/name2tag.so \
           lib/evidence/action/sortkey2name.so \
           lib/evidence/action/playlist.so \
           lib/evidence/action/searchable.so \
           lib/evidence/action/searchable_r.so

PLUG_CFLAGS := $(BASE_CFLAGS) $(CORE_CFLAGS)
PLUG_LINK    := -shared -fPIC $(CORE_LIBS)

lib/evidence/backend/posix.so: src/providers/backends/file_posix.c src/providers/backends/file_direct.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(LIBMAGIC_CFLAGS) $(LIBCURL_CFLAGS) $(PLUG_LINK) -o $@ src/providers/backends/file_posix.c src/providers/backends/file_direct.c $(LIBMAGIC_LIBS) $(LIBCURL_LIBS)

lib/evidence/backend/null.so: src/providers/backends/file_null.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<

ifeq ($(EXTRACTOR),yes)
lib/evidence/metadata/extractor.so: src/providers/extractor/extractor.c config.h
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(EXTRACTOR_CFLAGS) $(PLUG_LINK) -o $@ $< $(EXTRACTOR_LIBS)
PLUGINS += lib/evidence/metadata/extractor.so
endif
lib/evidence/metadata/folder.so: src/providers/folder/folder.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/metadata/exif.so: src/providers/exif/exif.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<

lib/evidence/actions/null.so: src/actions/null/null.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/delete.so: src/actions/delete/delete.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/rename.so: src/actions/rename/rename.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/mkdir.so: src/actions/mkdir/mkdir.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/newwindow.so: src/actions/newwindow/newwindow.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/backdrop.so: src/actions/backdrop/backdrop.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/tile.so: src/actions/tile/tile.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/tag2name.so: src/actions/tag2name/tag2name.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(GTK2_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/name2tag.so: src/actions/name2tag/name2tag.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/sortkey2name.so: src/actions/sortkey2name/sortkey2name.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/playlist.so: src/actions/playlist/playlist.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/searchable.so: src/actions/searchable/searchable.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
lib/evidence/action/searchable_r.so: src/actions/searchable_r/searchable_r.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LINK) -o $@ $<
# properties dialog is GTK2-only; keep the plugin under action/ where
# evidence.menu's internal handlers expect it
lib/evidence/action/info_dial.so: src/actions/properties/info_dial.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(GTK2_CFLAGS) $(PLUG_LINK) -o $@ $<

# --- EFL theme engines -------------------------------------------------------
# engine plugins (scrollbars, tooltips, icon frames, rubber-bands, ...) are
# dlopen()ed by theme.c/theme_bind.c from lib/evidence/gui/efl/engines/.
# engines depending on long-removed EFL libs (Edb, Ebits, Ebg) are not built.
ifeq ($(EFL),yes)
ENGINE_SRC := $(filter-out src/gui/efl/engines/e_bg.c src/gui/efl/engines/sb_edb.c \
                      src/gui/efl/engines/rb_edb.c src/gui/efl/engines/bg_ebg.c \
                      src/gui/efl/engines/bg_ebg_trans.c \
                      src/gui/efl/engines/ia_common.c src/gui/efl/engines/ix_textpath.c, \
                      $(wildcard src/gui/efl/engines/*.c))
# some engines consist of more than one source file (cf. engines/Makefile.am)
ENGINE_EXTRA := ia_grey:ia_common ia_throb:ia_common ia_solid:ia_common \
                ia_basic:ia_common ia_grow:ia_common ia_growfade:ia_common \
                ia_creepy:ia_common ia_creepology:ia_common \
                ix_rephormed:ix_textpath ix_LCARS:ix_textpath
ENGINE_GET_EXTRA = $(strip $(foreach e,$(ENGINE_EXTRA),$(if $(filter $(1),$(firstword $(subst :, ,$(e)))), $(word 2,$(subst :, ,$(e))))))
ENGINES := $(patsubst src/gui/efl/engines/%.c,lib/evidence/gui/efl/engines/%.so,$(ENGINE_SRC))
ENGINE_CFLAGS := $(BASE_CFLAGS) $(CORE_CFLAGS) $(EVAS_CFLAGS) -I$(TOP)/src -I$(TOP)/src/gui/common \
                 -I$(TOP)/src/gui/efl/common -I$(TOP)/src/gui/efl/engines -fPIC
ENGINE_LINK := -shared
lib/evidence/gui/efl/engines/%.so: src/gui/efl/engines/%.c \
    src/gui/efl/engines/ia_common.c src/gui/efl/engines/ix_textpath.c config.h
	@mkdir -p $(dir $@)
	$(CC) $(ENGINE_CFLAGS) $(ENGINE_LINK) -o $@ $< $(addsuffix .c,$(addprefix src/gui/efl/engines/,$(call ENGINE_GET_EXTRA,$*)))
endif

# --- Imlib2 image thumbnailer (static image thumbs) ---------------------------
ifeq ($(IMLIB2),yes)
lib/evidence/thumbnailer/imlib.so: src/thumbnailer/imlib_loaders/imlib.c config.h
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(IMLIB2_CFLAGS) -shared -o $@ $< $(IMLIB2_LIBS) $(CORE_LIBS)
PLUGINS += lib/evidence/thumbnailer/imlib.so
endif

# --- Emotion movie thumbnailer (live + static video thumbs) -------------------
ifeq ($(EMOTION),yes)
lib/evidence/thumbnailer/emotion.so: src/thumbnailer/emotion/emotion.c config.h
	@mkdir -p $(dir $@)
	$(CC) $(PLUG_CFLAGS) $(EMOTION_CFLAGS) $(IMLIB2_CFLAGS) -shared -o $@ $< $(EMOTION_LIBS) $(IMLIB2_LIBS) $(CORE_LIBS)
PLUGINS += lib/evidence/thumbnailer/emotion.so
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

# --- install -------------------------------------------------------------------
# mimics the original autotools install-data-local (Makefile.am): binary,
# setup files, data images, themes, icon-sets (from their tarballs), plugins,
# theme engines.  set PREFIX=/usr for a system-wide install, the default is
# /usr/local.  use DESTDIR for packaging/pseudo installs.
PREFIX ?= /usr/local

install: all
	$(MAKE) do-install

do-install:
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 evidence $(DESTDIR)$(PREFIX)/bin/evidence
	install -d $(DESTDIR)$(PREFIX)/share/evidence
	install -m 0644 setup/mime.types setup/mime.alii setup/mime.icons \
	                setup/mime.handlers setup/evidence.menu \
	                setup/evidence.providers setup/evidence.handlers \
	                $(DESTDIR)$(PREFIX)/share/evidence/
	install -m 0644 data/*.png data/README $(DESTDIR)$(PREFIX)/share/evidence/
	install -d $(DESTDIR)$(PREFIX)/share/applications
	install -m 0644 evidence.desktop $(DESTDIR)$(PREFIX)/share/applications/evidence.desktop
	install -d $(DESTDIR)$(PREFIX)/share/pixmaps
	install -m 0644 evidence.xpm $(DESTDIR)$(PREFIX)/share/pixmaps/evidence.xpm
	install -d $(DESTDIR)$(PREFIX)/share/evidence/themes
	# OtE and metallo are not installable: their look depends on 2005-era
	# binary formats (edje groups / Ebg) that no longer load; shipping them
	# only produces crash reports.  kept in the repo for reference.
	for t in tosh efm fishtank nautilus rephormed LCARS; do \
	  cp -r evidence.themes/$$t $(DESTDIR)$(PREFIX)/share/evidence/themes/ || echo "theme $$t skipped"; \
	done
	# fonts is a data directory (referenced as themes/fonts/...), not a
	# theme; keep it out of --theme's namespace
	cp -r evidence.themes/fonts $(DESTDIR)$(PREFIX)/share/evidence/themes/ || true
	ln -sfn rephormed $(DESTDIR)$(PREFIX)/share/evidence/themes/default
	install -d $(DESTDIR)$(PREFIX)/share/evidence/icons
	for i in indigo_128 indigo_96 tosh_128 tosh_96 efm rephormed fishtank_128 LCARS ento_64 gorilla_36 gorilla_48 gorilla_72; do \
	  tar -xzf icons/$$i.tar.gz -C $(DESTDIR)$(PREFIX)/share/evidence/icons/ || echo "iconset $$i skipped"; \
	done
	# some 2005 tarballs shipped without the actual images (tosh_96, indigo_96);
	# drop such empty sets again so no theme silently binds to a broken icon-set
	-for d in $(DESTDIR)$(PREFIX)/share/evidence/icons/*; do \
	  if [ -d "$$d" ] && [ -z "$$(find "$$d" \( -name '*.png' -o -name '*.jpg' \) -print -quit)" ]; then \
	    echo "iconset $$(basename $$d) contains no images, removing"; rm -rf "$$d"; fi; \
	done
	cd $(DESTDIR)$(PREFIX)/share/evidence/icons && ln -sfn rephormed default
	tar -xzf icons/emblems.tar.gz -C $(DESTDIR)$(PREFIX)/share/evidence/icons/ || true
	install -d $(DESTDIR)$(PREFIX)/lib/evidence
	cp -r lib/evidence/* $(DESTDIR)$(PREFIX)/lib/evidence/
	cd $(DESTDIR)$(PREFIX)/lib/evidence/backend && ln -sfn posix.so fsxs.so

.PHONY: all clean distclean plugins install do-install
