## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS       = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES   = Makefile.in Makefile

LDFLAGS                = -rdynamic -shared
INCLUDES               = -I$(top_srcdir)/src\
			 -I../../../ -I.\
			 @GTK_CFLAGS@

pkgdir                 = $(libdir)/evidence/action

pkg_LTLIBRARIES        = rename.la

rename_la_SOURCES      = rename.c
rename_la_LDFLAGS      = -no-undefined -module -avoid-version
rename_la_LIBADD       = @GTK_LIBS@
rename_la_DEPENDENCIES = $(top_builddir)/config.h 
