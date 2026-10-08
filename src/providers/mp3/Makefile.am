## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in Makefile

LDFLAGS              = -rdynamic -shared @id3_libs@
INCLUDES             = -I$(top_srcdir)/src\
		       -I../../../ -I.

pkgdir               = $(libdir)/evidence/metadata

if BUILD_PLUGIN_MP3
MP3_L                = mp3.la
endif

pkg_LTLIBRARIES      = $(MP3_L) 

mp3_la_SOURCES       = mp3.cpp genre.h
mp3_la_LDFLAGS       = -no-undefined -module -avoid-version
mp3_la_DEPENDENCIES  = $(top_builddir)/config.h 
