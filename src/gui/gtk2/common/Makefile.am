## Process this file with automake to produce Makefile.in

SUBDIRS              = tree

AUTOMAKE_OPTIONS     = 1.7 foreign

if BUILD_WITH_ICONLIST
SHELF_SRC=shelf.c shelf.h
endif


# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. \
		       -I$(top_srcdir)/src

noinst_LTLIBRARIES   = libevidence_gui_gtk2_common.la

libevidence_gui_gtk2_common_la_SOURCES = \
	session_gtk2.c session_gtk2.h \
	tooltip.c \
	keysyms.h alert.c \
	clipboard.c clipboard.h \
	dnd.c dnd.h \
	icon.c icon.h \
	menu_gtk.c \
	notebook.c notebook.h \
	$(SHELF_SRC)

libevidence_gui_gtk2_common_la_CFLAGS = \
	@GTK_CFLAGS@

libevidence_gui_gtk2_common_la_LIBADD = \
	$(LDFLAGS) \
	@GTK_LIBS@ \
	tree/libevidence_gui_gtk2_tree.la

libevidence_gui_gtk2_common_la_DEPENDENCIES = \
	$(top_builddir)/config.h \
	tree/libevidence_gui_gtk2_tree.la
