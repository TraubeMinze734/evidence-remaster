## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS          = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES      = Makefile.in Makefile

LDFLAGS                   = -rdynamic -shared @extractor_libs@
INCLUDES                  = -I$(top_srcdir)/src\
			    -I../../../ -I.\
			    @extractor_cflags@

pkgdir                    = $(libdir)/evidence/metadata

if BUILD_PLUGIN_EXTRACTOR
EXTRACTOR_L               = extractor.la
endif

pkg_LTLIBRARIES           = $(EXTRACTOR_L) 

extractor_la_SOURCES      = extractor.c
extractor_la_LDFLAGS      = -no-undefined -module -avoid-version
extractor_la_DEPENDENCIES = $(top_builddir)/config.h 
