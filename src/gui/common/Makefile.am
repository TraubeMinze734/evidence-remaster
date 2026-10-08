AUTOMAKE_OPTIONS                       = 1.7 foreign
MAINTAINERCLEANFILES                   = Makefile.in

if BUILD_WITH_EVAS2
ICONLIST_SRC=icon_list.c icon_list.h
endif

INCLUDES                               = -I. \
                                         -I$(top_srcdir)/src \
                                         @GTK_CFLAGS@ @pcre_cflags@

noinst_LTLIBRARIES                     = libevidence_gui_common.la

libevidence_gui_common_la_LIBADD       = @pcre_libs@
libevidence_gui_common_la_LDFLAGS      = $(LDFLAGS) -static
libevidence_gui_common_la_DEPENDENCIES = $(top_builddir)/config.h
libevidence_gui_common_la_SOURCES = \
          x11.h \
          tooltip.h alert.h \
          desktop.c desktop.h \
          icon_path.c icon_path.h \
          session.c session.h session_private.h \
          menu_common.c menu.h menu_private.h \
          typebuffer.c typebuffer.h \
          focus.c \
          $(ICONLIST_SRC) \
          options.c options.h
