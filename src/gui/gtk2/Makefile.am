## Process this file with automake to produce Makefile.in

SUBDIRS              = common about icon_view browser_view tree_view

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. \
		       -I$(top_srcdir)/src

noinst_LTLIBRARIES   = libevidence_gui_gtk2.la

libevidence_gui_gtk2_la_SOURCES = gui_gtk2.c

libevidence_gui_gtk2_la_CFLAGS = \
	@GTK_CFLAGS@ \
	@gnomecanvas_cflags@ \
	@edje_cflags@ \
	@ecore_cflags@

libevidence_gui_gtk2_la_LIBADD = \
	$(LDFLAGS) \
	common/libevidence_gui_gtk2_common.la \
	about/libevidence_gui_gtk2_about.la \
	tree_view/libevidence_gui_gtk2_treeview.la \
	browser_view/libevidence_gui_gtk2_browser.la \
	icon_view/libevidence_gui_gtk2_iconview.la \
	@GTK_LIBS@ @gnomecanvas_libs@ \
	@edje_libs@ @ecore_libs@

libevidence_gui_gtk2_la_DEPENDENCIES = \
	$(top_builddir)/config.h \
	common/libevidence_gui_gtk2_common.la \
	about/libevidence_gui_gtk2_about.la \
	tree_view/libevidence_gui_gtk2_treeview.la \
	browser_view/libevidence_gui_gtk2_browser.la \
	icon_view/libevidence_gui_gtk2_iconview.la
