## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

noinst_LTLIBRARIES   = libevidence_gui_gtk2_iconview.la

if BUILD_WITH_EVAS2
libevidence_gui_gtk2_iconview_la_SOURCES = \
	gevas_light.c gevas_light.h \
	gevas_bind.c  gevas_bind.h \
	icon_view.c   icon_view.h
endif

LDFLAGS		     =
INCLUDES	     = -I. \
		       -I$(top_srcdir)/src \
		       @pcre_cflags@

libevidence_gui_gtk2_iconview_la_CFLAGS = \
	 @GTK_CFLAGS@

libevidence_gui_gtk2_iconview_la_LIBADD = \
	$(LDFLAGS) \
	@GTK_LIBS@ 

libevidence_gui_gtk2_iconview_la_DEPENDENCIES = \
	$(top_builddir)/config.h
