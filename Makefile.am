## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS       = 1.7 foreign

if BUILD_WITH_EVAS2
themes = evidence.themes
else
themes =
endif

SUBDIRS = src icons $(themes)

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 config.guess \
                       config.h.in config.sub install-sh \
		       ltconfig ltmain.sh missing mkinstalldirs \
		       stamp-h.in depmod \
		       evidence-icons-basic.tar.bz2 \
		       evidence-icons-extra.tar.bz2

EXTRA_DIST=evidence.desktop evidence.lsm.in evidence.spec.in evidence.spec evidence.texinfo evidence.1 evidence.xpm config.h DEPENDENCIES HELP_DEVELOP

# build and install the .info pages
#info_TEXINFOS = evidence.texinfo
#evidence_TEXINFOS = gpl.texinfo

#  install the man pages
man_MANS=evidence.1

# cideb:
#	sudo checkinstall --exclude=/home,*.la,*.a -D --delspec=no --pkgversion=$(VERSION) --pkgrelease=$(RELEASE) --pkglicense=GPL --pkgarch=i386 make install

# cirpm:
#	sudo checkinstall --exclude=/home -R --delspec=no --pkgversion=$(VERSION) --pkgrelease=$(RELEASE) --pkglicense=GPL --pkgarch=i386 make install-strip

gz: dist
	tar -zxf evidence-$(VERSION).tar.gz
	tar -zcf evidence-$(VERSION).tar.gz evidence-$(VERSION)
	rm -rf evidence-$(VERSION)

bz2: gz
	tar -zxf evidence-$(VERSION).tar.gz
	tar -jcf evidence-$(VERSION).tar.bz2 evidence-$(VERSION)
	rm -rf evidence-$(VERSION)

rpm: gz
	rpmbuild -ta evidence-$(VERSION).tar.gz
	-find . -user root -exec chown azundris.users {} \;

deb:
	dpkg-buildpackage -rfakeroot -b -uc
	(cd .. && lintian -i evidence_*.changes || echo "lintian did not like this...")

doc:
	rm -rf ./docs/html ./docs/latex ./docs/man
	doxygen > /dev/null
	cp data/*.png docs/html/

docs:	doc



install-data-local:
	@$(NORMAL_INSTALL)
	echo "******************** INST-DATA ********************"; \
	if test -d $(srcdir)/setup; then \
	  $(mkinstalldirs) $(DESTDIR)$(pkgdatadir); \
	  (cd $(srcdir)/setup; tar -cf - *) | (cd $(DESTDIR)$(pkgdatadir); tar -xf -); \
	  (cp $(srcdir)/data/*.png $(srcdir)/data/README $(DESTDIR)$(pkgdatadir) ) \
	fi
	-find $(DESTDIR)$(libdir)/evidence/backend -name "*.la" -exec rm -f {} \;
	-find $(DESTDIR)$(libdir)/evidence/backend -name "*.a" -exec rm -f {} \;
	-find $(DESTDIR)$(libdir)/evidence/backend -name "*.o" -exec rm -f {} \;
	-(cd $(DESTDIR)$(libdir)/evidence/backend; ln -s ./posix.so fsxs.so)
#	(cd $(distdir)/src; upx $(DESTDIR)$(bindir)/evidence)

dist-hook: doc
	echo "******************** DIST ********************"; \
	mkdir -p $(distdir)/docs && \
	(cd $(srcdir)/docs; tar -cf - *) | (cd $(distdir)/docs; tar -xf -); \
	if test -d setup; then \
	  mkdir -p $(distdir)/data; \
	  (cd $(srcdir)/data; tar -cf - *.png README) | (cd $(distdir)/data; tar -xf -) || echo; \
	  mkdir -p $(distdir)/evidence.themes; \
	  (cd $(srcdir)/evidence.themes; tar -cf - README Makefile.am fonts tosh) | (cd $(distdir)/evidence.themes; tar -xf -) || echo; \
	  mkdir -p $(distdir)/icons; \
	  (cd $(srcdir)/icons; tar -cf - emblems.tar.gz README Makefile.am tosh_128.tar.gz tosh_64.tar.gz indigo_128.tar.gz indigo_64.tar.gz) | (cd $(distdir)/icons; tar -xf -) || echo; \
	  mkdir -p $(distdir)/setup; \
	  (cd $(srcdir)/setup; tar -cf - *) | (cd $(distdir)/setup; tar -xf -); \
	  (find $(distdir) -name "*~" -exec rm -f {} \; || echo ); \
	  (find $(distdir) -name "*.o" -exec rm -f {} \; || echo ); \
	  (find $(distdir) -name "*.l?" -exec rm -f {} \; || echo ); \
	  (find $(distdir) -name ".libs" -type d -exec rm -rf {} \; || echo ); \
	  (find $(distdir) -name ".deps" -type d -exec rm -rf {} \; || echo ) \
	fi
