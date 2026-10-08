## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. @GTK_CFLAGS@ \
		       -I$(top_srcdir)/src

noinst_LTLIBRARIES   = libevidence_gui_efl_iconview.la

libevidence_gui_efl_iconview_la_SOURCES = \
	evas_bind.c evas_bind.h

# libevidence_gui_efl_iconview_la_CFLAGS =

libevidence_gui_efl_iconview_la_LIBADD = \
	$(LDFLAGS)

libevidence_gui_efl_iconview_la_DEPENDENCIES = \
	$(top_builddir)/config.h
