## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. \
		       -I$(top_srcdir)/src

pkgdir               = $(libdir)/evidence/thumbnailer

noinst_LTLIBRARIES   = libevidence_thumbnailer_epeg.la

pkg_LTLIBRARIES      = epeg.la

epeg_la_SOURCES       = epeg.c
epeg_la_LDFLAGS       = -no-undefined -module -avoid-version
epeg_la_DEPENDENCIES  = $(top_builddir)/config.h 

libevidence_thumbnailer_epeg_la_SOURCES = \
	Epeg.h epeg_main.c epeg_memfile.c epeg_private.h

libevidence_thumbnailer_epeg_la_LIBADD = \
	@libjpeg_libs@ \
	$(LDFLAGS)

libevidence_thumbnailer_epeg_la_DEPENDENCIES = \
	$(top_builddir)/config.h
