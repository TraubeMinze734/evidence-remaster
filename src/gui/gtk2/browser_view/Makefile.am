## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. \
		       -I$(top_srcdir)/src

noinst_LTLIBRARIES   = libevidence_gui_gtk2_browser.la

libevidence_gui_gtk2_browser_la_SOURCES = \
	browser_view.c browser_view.h

libevidence_gui_gtk2_browser_la_CFLAGS = \
	 @GTK_CFLAGS@

libevidence_gui_gtk2_browser_la_LIBADD = \
	$(LDFLAGS) \
	@GTK_LIBS@

libevidence_gui_gtk2_browser_la_DEPENDENCIES = \
	$(top_builddir)/config.h
