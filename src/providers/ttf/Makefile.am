## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in Makefile

LDFLAGS              = -rdynamic -shared @ttf_libs@
INCLUDES             = -I$(top_srcdir)/src\
		       -I../../../ -I.\
		       @GTK_CFLAGS@ \
		       @ttf_cflags@

pkgdir               = $(libdir)/evidence/metadata

if BUILD_PLUGIN_TTF
TTF_L                = ttf.la
endif

pkg_LTLIBRARIES      = $(TTF_L) 

ttf_la_SOURCES       = ttf.c
ttf_la_LDFLAGS       = -no-undefined -module -avoid-version
ttf_la_LIBADD        = @GTK_LIBS@
ttf_la_DEPENDENCIES  = $(top_builddir)/config.h 
