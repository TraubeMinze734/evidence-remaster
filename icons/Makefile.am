MAINTAINERCLEANFILES = Makefile.in
# EXTRA_DIST = emblems.tar.gz README tosh_128.tar.gz tosh_64.tar.gz
EXTRA_DIST = README *.tar.gz

if INSTALL_ALL_THEMES
extra_iconsets= lain_64 fishtank_48 ento_64 gfilerunner gorilla_36 gorilla_48 gorilla_72 icosx indigo_64 tosh_64
else
extra_iconsets=
endif

iconsets=indigo_128 indigo_96 tosh_128 tosh_96 efm rephormed fishtank_128 LCARS

all:

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/icons;
	for iconset in $(iconsets); do \
	  tar -zxvf $$iconset.tar.gz -C $(DESTDIR)$(pkgdatadir)/icons || echo "iconset $$iconset skipped..."; \
	done;
	-(cd $(DESTDIR)$(pkgdatadir)/icons/tosh_96; ./create)
	-(cd $(DESTDIR)$(pkgdatadir)/icons/tosh_64; ./create)
	-(cd $(DESTDIR)$(pkgdatadir)/icons/indigo_64; ./create)
	-(cd $(DESTDIR)$(pkgdatadir)/icons/indigo_96; ./create)
	for iconset in $(extra_iconsets); do \
	  tar -zxvf $$iconset.tar.gz -C $(DESTDIR)$(pkgdatadir)/icons || echo "iconset $$iconset skipped..."; \
	done;
	-(ln -s rephormed $(DESTDIR)$(pkgdatadir)/icons/default)
	$(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/icons/emblems;
	-(tar -cf - README tosh_128.tar.gz tosh_96.tar.gz indigo_128.tar.gz indigo_96.tar.gz) | (cd $(DESTDIR)$(pkgdatadir)/icons; tar -xf -) || echo; \
	tar -zxvf emblems.tar.gz -C $(DESTDIR)$(pkgdatadir)/icons
	cp README Makefile $(DESTDIR)$(pkgdatadir)/icons

install: install-data-local
