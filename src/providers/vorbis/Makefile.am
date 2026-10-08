## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS       = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES   = Makefile.in Makefile

LDFLAGS                = -rdynamic -shared @vorbis_libs@
INCLUDES               = -I$(top_srcdir)/src\
			 -I../../../ -I.

pkgdir                 = $(libdir)/evidence/metadata

if BUILD_PLUGIN_LIBVORBIS
VORBIS_L               = vorbis.la
endif

pkg_LTLIBRARIES        = $(VORBIS_L) 

vorbis_la_SOURCES      = vorbis.c vcedit.c vcedit.h 
vorbis_la_LDFLAGS      = -no-undefined -module -avoid-version -lvorbis -lvorbisfile
vorbis_la_DEPENDENCIES = $(top_builddir)/config.h 
