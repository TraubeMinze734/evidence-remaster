## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS      = 1.7 foreign

if BUILD_WITH_EVAS2
XINE_EMOTION = emotion.c emotion.h
endif

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES  = Makefile.in

LDFLAGS               = -rdynamic -shared @XINE_LIBS@
INCLUDES              = -I$(top_srcdir)/src\
		        -I../../../ -I.\
		        @XINE_CFLAGS@

pkgdir                = $(libdir)/evidence/thumbnailer

XINE_L                = xine.la

pkg_LTLIBRARIES       = $(XINE_L) 

xine_la_SOURCES       = xine.c $(XINE_EMOTION)
xine_la_LDFLAGS       = -no-undefined -module -avoid-version
xine_la_DEPENDENCIES  = $(top_builddir)/config.h 
