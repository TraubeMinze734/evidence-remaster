## Process this file with automake to produce Makefile.in

if BUILD_THUMBNAILER_MPEG3
MPEG3_DIR=mpeg3
endif
if BUILD_THUMBNAILER_AVIFILE
AVIFILE_DIR=avifile
endif
if BUILD_THUMBNAILER_ID3
ID3_DIR=id3
endif
if BUILD_THUMBNAILER_XINE
XINE_DIR=xine
endif
if BUILD_THUMBNAILER_IMLIB2
IMLIB2_DIR=imlib_loaders
endif
if BUILD_THUMBNAILER_EPEG
EPEG_DIR=epeg
EPEG_LA=epeg/libevidence_thumbnailer_epeg.la
endif

if BUILD_THUMBNAILER_IMLIB2
SUBDIRS              = ttf $(EPEG_DIR) $(IMLIB2_DIR) $(AVIFILE_DIR) $(ID3_DIR) $(MPEG3_DIR) $(XINE_DIR)
endif


AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in

LDFLAGS		     =
INCLUDES	     = -I. \
		       -I$(top_srcdir)/src \
		       @evas_cflags@ \
		       @libpng_cflags@ \
		       @imlib2_cflags@
#		       @libjpeg_cflags@

noinst_LTLIBRARIES   = libevidence_thumbnailer.la

libevidence_thumbnailer_la_SOURCES = \
	thumbnail.c thumbnail.h thumbnail_plugin.c thumbnail_plugin.h thumbnail_private.h

libevidence_thumbnailer_la_LIBADD = \
	@evas_libs@ \
	@libpng_libs@ \
	@imlib2_libs@ \
	$(EPEG_LA) \
	$(LDFLAGS)
#	@libjpeg_libs@

libevidence_thumbnailer_la_DEPENDENCIES = \
	$(top_builddir)/config.h
