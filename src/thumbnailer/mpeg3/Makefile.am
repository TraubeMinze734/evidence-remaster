## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS              = -rdynamic -shared @mpeg3_libs@
INCLUDES             = -I$(top_srcdir)/src\
		       -I../../../ -I.\
		       @mpeg3_cflags@

pkgdir               = $(libdir)/evidence/thumbnailer

MPEG3_L                = mpeg3.la

pkg_LTLIBRARIES      = $(MPEG3_L) 

mpeg3_la_SOURCES       = mpeg3.c
mpeg3_la_LDFLAGS       = -no-undefined -module -avoid-version
mpeg3_la_DEPENDENCIES  = $(top_builddir)/config.h 
