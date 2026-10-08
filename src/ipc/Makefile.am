## miracle makefile by Azundris : )

## Process this file with automake to produce Makefile.in

## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in Makefile

LDFLAGS              = -module -avoid-version -rdynamic -shared
INCLUDES             = -I$(top_srcdir)/src\
		       -I../../ -I../ -I.

pkgdir =  $(libdir)/evidence/ipc

if BUILD_IPC_ECORE
IPC_ECORE_L = ipc_ecore.la

ipc_ecore_la_DEPENDENCIES     = $(top_builddir)/config.h 
ipc_ecore_la_CFLAGS           = @ecore_ipc_cflags@
ipc_ecore_la_SOURCES          = ipc_ecore.c
ipc_ecore_la_LIBADD           = @ecore_ipc_libs@
ipc_ecore_la_LDFLAGS          = -no-undefined -module -avoid-version

EVSH_L =
endif

if BUILD_IPC_DCOP
IPC_DCOP_L = ipc_dcop.la

ipc_dcop_la_DEPENDENCIES      = $(top_builddir)/config.h 
ipc_dcop_la_CXXFLAGS          = @qt_cxxflags@ @dcop_cxxflags@
ipc_dcop_la_SOURCES           = ipc_dcop_iface_skel.cpp ipc_dcop_iface.h ipc_dcop.h ipc_dcop.cpp
ipc_dcop_la_LIBADD            = @qt_libadd@ @dcop_libs@ -lkdecore
# $(LIB_KDECORE) $(LIB_KDEUI) $(LIBSOCKET)
ipc_dcop_la_LDFLAGS           = -no-undefined -module -avoid-version @kde_rpath@
endif

if BUILD_IPC_DCOPC
IPC_DCOPC_L = ipc_dcopc.la

ipc_dcopc_la_DEPENDENCIES     = $(top_builddir)/config.h 
ipc_dcopc_la_CFLAGS           = @dcopc_cflags@
ipc_dcopc_la_SOURCES          = ipc_dcopc.c
ipc_dcopc_la_LIBADD           = @dcopc_libs@
ipc_dcopc_la_LDFLAGS          = -no-undefined -module -avoid-version
endif

if BUILD_IPC_DBUS
IPC_DBUS_L = ipc_dbus.la

ipc_dbus_la_DEPENDENCIES      = $(top_builddir)/config.h 
ipc_dbus_la_CFLAGS            = @dbus_cflags@
ipc_dbus_la_SOURCES           = ipc_dbus.c
ipc_dbus_la_LIBADD            = @dbus_libs@
ipc_dbus_la_LDFLAGS           = -no-undefined -module -avoid-version
endif

# /opt/kde3/bin/dcopidl ./dcopdemoiface.h > dcopdemoiface.kidl || ( rm -f dcopdemoiface.kidl ; /bin/false )
# /opt/kde3/bin/dcopidl2cpp --c++-suffix cpp --no-stub dcopdemoiface.kidl

pkg_LTLIBRARIES               = $(IPC_DCOP_L) $(IPC_DCOPC_L) $(IPC_ECORE_L) \
                                $(IPC_DBUS_L) $(EVSH_L)

ipc_dcop.moc: ipc_dcop.h
	@MOC@ ipc_dcop.h -o ipc_dcop.moc
ipc_dcop_iface.kidl: $(DCOPIDL_DEPENDENCIES)
	@DCOPIDL@ ipc_dcop_iface.h > ipc_dcop_iface.kidl
ipc_dcop_iface_skel.cpp: ipc_dcop_iface.kidl ipc_dcop.moc
	@DCOPIDL2CPP@ --c++-suffix cpp --no-stub ipc_dcop_iface.kidl

.o.so:
	$(CC) $(INCLUDES) $(LDFLAGS) $< -o $@

clean:
	-rm ipc_ecore ipc_dcop ipc_dcopc ipc_dbus ipc_dbusglib *.kidl *.moc  ipc_dcop_iface_skel*

dc:
	-rm *.kidl *.moc ipc_dcop_iface_skel*
