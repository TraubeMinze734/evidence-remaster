## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS       = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES   = Makefile.in Makefile

LDFLAGS                = -rdynamic -shared
INCLUDES               = -I$(top_srcdir)/src\
			 -I../../../ -I.

pkgdir                 = $(libdir)/evidence/action

pkg_LTLIBRARIES        = tile.la

tile_la_SOURCES        = tile.c
tile_la_LDFLAGS        = -no-undefined -module -avoid-version
tile_la_LIBADD         = 
tile_la_DEPENDENCIES   = $(top_builddir)/config.h 
