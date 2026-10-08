## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS       = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES   = Makefile.in Makefile

LDFLAGS                = -rdynamic -shared
INCLUDES               = -I$(top_srcdir)/src\
			 -I../../../ -I.

pkgdir                 = $(libdir)/evidence/action

pkg_LTLIBRARIES        = backdrop.la

backdrop_la_SOURCES        = backdrop.c
backdrop_la_LDFLAGS        = -no-undefined -module -avoid-version
backdrop_la_LIBADD         = 
backdrop_la_DEPENDENCIES   = $(top_builddir)/config.h 
