## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS            = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES        = Makefile.in Makefile

LDFLAGS                     = -rdynamic -shared
INCLUDES                    = -I$(top_srcdir)/src\
                              -I../../../ -I.

pkgdir                      = $(libdir)/evidence/action

pkg_LTLIBRARIES             = thumbnail.la

thumbnail_la_SOURCES        = thumbnail.c
thumbnail_la_LDFLAGS        = -no-undefined -module -avoid-version
thumbnail_la_LIBADD         = 
thumbnail_la_DEPENDENCIES   = $(top_builddir)/config.h 
