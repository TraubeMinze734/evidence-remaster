## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS         = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES     = Makefile.in Makefile

LDFLAGS                  = -rdynamic -shared
INCLUDES                 = -I$(top_srcdir)/src\
			   -I../../../ -I.\
			   @GTK_CFLAGS@

pkgdir                   = $(libdir)/evidence/action

pkg_LTLIBRARIES          = tag2name.la

tag2name_la_SOURCES      = tag2name.c
tag2name_la_LDFLAGS      = -no-undefined -module -avoid-version
tag2name_la_LIBADD       = @GTK_LIBS@
tag2name_la_DEPENDENCIES = $(top_builddir)/config.h 
