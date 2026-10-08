## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

SUBDIRS              = common efl gtk2

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. \
		       -I$(top_srcdir)/src \
		       @edje_cflags@

noinst_LTLIBRARIES   = libevidence_gui.la

libevidence_gui_la_SOURCES = gui.h

libevidence_gui_la_LIBADD = \
	$(LDFLAGS) @edje_libs@ \
	common/libevidence_gui_common.la \
	gtk2/libevidence_gui_gtk2.la \
	efl/libevidence_gui_efl.la

libevidence_gui_la_DEPENDENCIES = \
	$(top_builddir)/config.h \
	common/libevidence_gui_common.la \
	gtk2/libevidence_gui_gtk2.la \
	efl/libevidence_gui_efl.la
