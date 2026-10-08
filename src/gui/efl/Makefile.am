## Process this file with automake to produce Makefile.in

SUBDIRS              = common icon_view engines

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. \
		       -I$(top_srcdir)/src

noinst_LTLIBRARIES   = libevidence_gui_efl.la

libevidence_gui_efl_la_SOURCES =

libevidence_gui_efl_la_LIBADD = \
	common/libevidence_gui_efl_common.la \
	icon_view/libevidence_gui_efl_iconview.la \
	$(LDFLAGS)

libevidence_gui_efl_la_DEPENDENCIES = \
	$(top_builddir)/config.h \
	common/libevidence_gui_efl_common.la \
	icon_view/libevidence_gui_efl_iconview.la
