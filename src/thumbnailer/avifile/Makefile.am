## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS              = -rdynamic -shared @avi_LIBS@
INCLUDES             = -I$(top_srcdir)/src @avi_CFLAGS@ \
		       -I../../../ -I.

pkgdir               = $(libdir)/evidence/thumbnailer

AVIFILE_L                = avifile.la

pkg_LTLIBRARIES          = $(AVIFILE_L) 

avifile_la_SOURCES       = avifile.cpp
avifile_la_LDFLAGS       = -no-undefined -module -avoid-version
avifile_la_DEPENDENCIES  = $(top_builddir)/config.h 
