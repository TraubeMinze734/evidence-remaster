## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS     = 1.7 foreign

#SUBDIRS = kio

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in Makefile

LDFLAGS              = -rdynamic -shared
INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/src

pkgdir =  $(libdir)/evidence/backend

NULL_L = null.la

if BUILD_BACKEND_POSIX
POSIX_L = posix.la
endif

if BUILD_BACKEND_GNOMEVFS2
GNOME_VFS_L = gnomevfs.la
endif

if BUILD_BACKEND_EFSD
EFSD_L = efsd.la
endif

if BUILD_BACKEND_KIO
KIO_L = kio.la
endif

pkg_LTLIBRARIES          = $(NULL_L) $(POSIX_L) $(GNOME_VFS_L) $(KIO_L) $(EFSD_L)

null_la_SOURCES          = file_null.c
null_la_CFLAGS           =
null_la_LDFLAGS          =  -no-undefined -module -avoid-version
null_la_DEPENDENCIES     = $(top_builddir)/config.h

posix_la_SOURCES         = file_direct.c file_posix.c file_direct.h
posix_la_CFLAGS          = @attr_cflags@ @acl_cflags@ @libmagic_cflags@ @fam_cflags@ @libsharedmime_CFLAGS@ @curl_cflags@
posix_la_LDFLAGS         =  -no-undefined -module -avoid-version
posix_la_DEPENDENCIES    = $(top_builddir)/config.h
# posix_la_LIBADD        = @acl_libs@ @libmagic_libs@ @fam_libs@ @attr_libs@ @libsharedmime_LIBS@ @curl_libs@

gnomevfs_la_SOURCES      = file_gnomevfs.c 
gnomevfs_la_CFLAGS       = @BACKEND_GNOMEVFS2_CFLAGS@ @EVIDENCE_CFLAGS@
gnomevfs_la_LDFLAGS      = -no-undefined -module -avoid-version @BACKEND_GNOMEVFS2_LIBS@
gnomevfs_la_DEPENDENCIES = $(top_builddir)/config.h 

efsd_la_SOURCES          = file_efsd.c 
efsd_la_LDFLAGS          =  -no-undefined -module -avoid-version
efsd_la_DEPENDENCIES     = $(top_builddir)/config.h 

kio_la_CXXFLAGS          = @qt_cxxflags@
kio_la_SOURCES           = file_kio.cpp
kio_la_DEPENDENCIES      = $(top_builddir)/config.h 
kio_la_LDFLAGS           = -no-undefined -module -avoid-version @kde_rpath@
kio_la_LIBADD            = @qt_libadd@ -lkdecore
# $(LIB_KDECORE) $(LIB_KDEUI) $(LIBSOCKET)
