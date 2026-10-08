## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. \
		       -I$(top_srcdir)/src

noinst_LTLIBRARIES   = libevidence_gui_gtk2_about.la

libevidence_gui_gtk2_about_la_SOURCES = \
	about.c about.h

libevidence_gui_gtk2_about_la_CFLAGS = \
	 @GTK_CFLAGS@

libevidence_gui_gtk2_about_la_LIBADD = \
	$(LDFLAGS) \
	@GTK_LIBS@

libevidence_gui_gtk2_about_la_DEPENDENCIES = \
	$(top_builddir)/config.h
