## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS              = -rdynamic -shared @imlib2_libs@
INCLUDES             = -I$(top_srcdir)/src\
		       -I../../../ -I.\
		       @imlib2_cflags@

pkgdir                 = $(libdir)/evidence/thumbnailer

# if BUILD_THUMBNAILER_TTF
IMLIB_L                = imlib.la
# endif

pkg_LTLIBRARIES        = $(IMLIB_L) 

imlib_la_SOURCES       = imlib.c
imlib_la_LDFLAGS       = -no-undefined -module -avoid-version
imlib_la_DEPENDENCIES  = $(top_builddir)/config.h 
