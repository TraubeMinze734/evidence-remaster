## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS       = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES   = Makefile.in Makefile

LDFLAGS                = -rdynamic -shared
INCLUDES               = -I$(top_srcdir)/src\
			 -I../../../ -I.

pkgdir                 = $(libdir)/evidence/metadata

FOLDER_L                  = folder.la

pkg_LTLIBRARIES        = $(FOLDER_L) 

folder_la_SOURCES         = folder.c
folder_la_LDFLAGS         = -no-undefined -module -avoid-version
folder_la_DEPENDENCIES    = $(top_builddir)/config.h 
