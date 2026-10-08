## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS       = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES   = Makefile.in Makefile

LDFLAGS                = -rdynamic -shared
INCLUDES               = -I$(top_srcdir)/src\
			 -I../../../ -I. @TAGLIB_CFLAGS@

pkgdir                    = $(libdir)/evidence/metadata

TAGLIB_L                  = taglib.la

pkg_LTLIBRARIES           = $(TAGLIB_L) 

taglib_la_SOURCES         = taglib.cpp
taglib_la_LDFLAGS         = -no-undefined -module -avoid-version
taglib_la_LIBADD          = @TAGLIB_LIBS@
taglib_la_DEPENDENCIES    = $(top_builddir)/config.h 
