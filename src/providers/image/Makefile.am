## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS      = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES  = Makefile.in Makefile

LDFLAGS               = -rdynamic -shared @imlib2_libs@
INCLUDES              = -I$(top_srcdir)/src\
			-I../../../ -I.

pkgdir                = $(libdir)/evidence/metadata

if BUILD_PLUGIN_IMAGE
IMAGE_L               = image.la
endif

pkg_LTLIBRARIES       = $(IMAGE_L) 

image_la_SOURCES      = image.c
image_la_LDFLAGS      = -no-undefined -module -avoid-version
image_la_DEPENDENCIES = $(top_builddir)/config.h 
