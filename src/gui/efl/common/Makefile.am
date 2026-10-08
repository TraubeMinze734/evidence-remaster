## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. @GTK_CFLAGS@ \
		       -I$(top_srcdir)/src

noinst_LTLIBRARIES   = libevidence_gui_efl_common.la

libevidence_gui_efl_common_la_SOURCES = \
	theme.c theme.h theme_bind.c evas_macros.h evas_thumbnail.c

# libevidence_gui_efl_common_la_CFLAGS =

libevidence_gui_efl_common_la_LIBADD = \
	$(LDFLAGS)

libevidence_gui_efl_common_la_DEPENDENCIES = \
	$(top_builddir)/config.h
