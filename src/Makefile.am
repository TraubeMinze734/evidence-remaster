## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS       = 1.7 foreign

SUBDIRS = thumbnailer ipc gui providers actions

###############################################################################

if BUILD_IPC_ECORE
if BUILD_ECORE_DEPENDENT
EVSH_B      = evsh
EVSH_L =
# EVSH_L      = evsh.la

# EVCFG_B     = evcfg
endif
endif

if BUILD_THUMBNAILER_EPEG
JPEG_LIBS = @libjpeg_libs@
endif

###############################################################################

bin_PROGRAMS = evidence $(EVCFG_B) $(EVSH_B)

# noinst_PROGRAMS= fstst
#		 session.c session.h session_private.h

evidence_SOURCES = \
                                 action.h action.c acl_tags.h \
                                 cache.c cache.h \
                                 debug.h \
                                 evidence.c evidence.h evidence_private.h \
                                 errors.h \
                                 events.c events.h \
                                 file.c file_fun.h file.h file_lcl.h \
                                 imlib.h \
                                 ipc_main.h ipc_main.c ipc.h \
                                 mounts.c mounts.h \
                                 md5c.c md5.h \
                                 meta.c meta.h \
                                 monitors.c monitors.h \
                                 mime.h \
                                 model.c model.h model_private.h \
                                 properties.c properties.h \
                                 re.h \
                                 search.c search.h \
                                 setup.c setup.h \
                                 userconfig.h \
                                 util.c util.h

evidence_INCLUDES              = @VALGRIND_CFLAGS@

evidence_DEPENDENCIES          = $(top_builddir)/config.h gui/libevidence_gui.la thumbnailer/libevidence_thumbnailer.la
evidence_CC                    = @PTHREAD_CC@
evidence_CFLAGS                = @PTHREAD_CFLAGS@ \
                                 @btrace_cflags@ @doodle_cflags@ \
                                 $(evidence_INCLUDES) \
                                 @iconv_cflags@ @GTK_CFLAGS@ @ecore_cflags@

evidence_LDFLAGS               = -rdynamic @PTHREAD_CFLAGS@
evidence_LDADD                 = $(INTLLIBS) \
                                 @PTHREAD_LIBS@ \
                                 @btrace_libs@ \
                                 $(JPEG_LIBS) \
                                 thumbnailer/libevidence_thumbnailer.la \
                                 gui/libevidence_gui.la \
                                 @iconv_libs@ @GTK_LIBS@ @ecore_libs@
# @doodle_libs@

evsh_DEPENDENCIES              = $(top_builddir)/config.h
evsh_CFLAGS                    = @ecore_ipc_cflags@ @libreadline_cflags@
evsh_SOURCES                   = evsh.c evclient.c evclient.h
evsh_LDFLAGS                   =
evsh_LDADD                     = @ecore_ipc_libs@ @libreadline_libs@

# evcfg_DEPENDENCIES             = $(top_builddir)/config.h
# evcfg_CFLAGS                   = @ecore_ipc_cflags@ @GTK_CFLAGS@
# evcfg_SOURCES                  = evcfg.c evclient.c evclient.h
# evcfg_LDFLAGS                  =
# evcfg_LDADD                    = @ecore_ipc_libs@ @GTK_LIBS@

# fstst_CC                       = @PTHREAD_CC@
# fstst_CFLAGS                   = @PTHREAD_CFLAGS@ @ecore_cflags@ @GTK_CFLAGS@ @iconv_cflags@
# fstst_DEPENDENCIES             = $(top_builddir)/config.h
# fstst_SOURCES                  = fstst.c \
#                                  file.c md5c.c cache.c util.c model.c \
#                                  meta.c setup.c
# fstst_LDFLAGS                  = -rdynamic @PTHREAD_CFLAGS@
# fstst_LDADD                    = @ecore_libs@ @GTK_LIBS@ @PTHREAD_LIBS@ @iconv_libs@

test: fstst
	fstst $(DESTDIR)$(libdir)/evidence/backend/posix.so
