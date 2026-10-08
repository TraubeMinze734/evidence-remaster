## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS       = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES   = Makefile.in Makefile

LDFLAGS                = -rdynamic -shared
INCLUDES               = -I$(top_srcdir)/src\
			 -I../../../ -I.

pkgdir                 = $(libdir)/evidence/action

pkg_LTLIBRARIES        = sortkey2name.la

sortkey2name_la_SOURCES        = sortkey2name.c
sortkey2name_la_LDFLAGS        = -no-undefined -module -avoid-version
sortkey2name_la_LIBADD         = 
sortkey2name_la_DEPENDENCIES   = $(top_builddir)/config.h 
