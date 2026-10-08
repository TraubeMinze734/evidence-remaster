## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS          = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES      = Makefile.in Makefile

LDFLAGS                   = -rdynamic -shared
INCLUDES                  = -I$(top_srcdir)/src\
			    @GTK_CFLAGS@

pkgdir                    = $(libdir)/evidence/action

pkg_LTLIBRARIES           = info_dial.la

info_dial_la_SOURCES      = info_dial.c
info_dial_la_LDFLAGS      = -no-undefined -module -avoid-version
info_dial_la_LIBADD       = @GTK_LIBS@
info_dial_la_DEPENDENCIES = $(top_builddir)/config.h 
