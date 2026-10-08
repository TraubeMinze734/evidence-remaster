## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS              = -rdynamic -shared @imlib2_libs@
INCLUDES             = -I$(top_srcdir)/src\
		       -I../../../ -I.\
		       @imlib2_cflags@

pkgdir               = $(libdir)/evidence/thumbnailer

# if BUILD_THUMBNAILER_TTF
TTF_L                = ttf.la
# endif

pkg_LTLIBRARIES      = $(TTF_L) 

ttf_la_SOURCES       = ttf.c
ttf_la_LDFLAGS       = -no-undefined -module -avoid-version
ttf_la_DEPENDENCIES  = $(top_builddir)/config.h 
