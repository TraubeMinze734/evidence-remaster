## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS       = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES   = Makefile.in Makefile

LDFLAGS                = -rdynamic -shared
INCLUDES               = -I$(top_srcdir)/src\
			 -I../../../ -I.

pkgdir                 = $(libdir)/evidence/metadata

EXIF_L                 = exif.la

pkg_LTLIBRARIES        = $(EXIF_L) 

exif_la_SOURCES        = exif.c
exif_la_LDFLAGS        = -no-undefined -module -avoid-version
exif_la_DEPENDENCIES   = $(top_builddir)/config.h 
