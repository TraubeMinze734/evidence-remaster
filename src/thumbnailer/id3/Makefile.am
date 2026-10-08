## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS              = -rdynamic -shared @id3_libs@
INCLUDES             = -I$(top_srcdir)/src\
		       -I../../../ -I.

pkgdir               = $(libdir)/evidence/thumbnailer

ID3_L                = id3.la

pkg_LTLIBRARIES      = $(ID3_L) 

id3_la_SOURCES       = id3.cpp
id3_la_LDFLAGS       = -no-undefined -module -avoid-version
id3_la_DEPENDENCIES  = $(top_builddir)/config.h 
