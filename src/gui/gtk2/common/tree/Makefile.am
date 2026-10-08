## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. @GTK_CFLAGS@ \
		       -I$(top_srcdir)/src

noinst_LTLIBRARIES   = libevidence_gui_gtk2_tree.la

libevidence_gui_gtk2_tree_la_SOURCES = \
	tree_model.c tree.h \
	cr_basic.c cr_basic.h \
	cr_arrow.c cr_arrow.h \
	cr_date.c cr_date.h \
	cr_size.c cr_size.h

# libevidence_gui_gtk2_tree_la_CFLAGS =

libevidence_gui_gtk2_tree_la_LIBADD = \
	$(LDFLAGS) \
	@GTK_LIBS@

libevidence_gui_gtk2_tree_la_DEPENDENCIES = \
	$(top_builddir)/config.h
