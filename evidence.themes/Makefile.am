if INSTALL_ALL_THEMES
extra_themes= essence scorpio azundris.bw green transwarp lain
else
extra_themes=
endif

themes= tosh efm fishtank metallo nautilus rephormed LCARS OtE

SUBDIRS =

EXTRA_DIST = $(themes) $(extra_themes)

install-data-local:
	@echo
	@echo ---------------------------------------------------------------------------
	@echo Installing themes...
	@echo ---------------------------------------------------------------------------
	@echo
	-mkdir -p $(DESTDIR)$(pkgdatadir)/themes
	-ls -1
	cp -rpv $(themes) $(extra_themes) $(DESTDIR)$(pkgdatadir)/themes
	$(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/themes/fonts;
	(cd fonts; tar -cf - *) | (cd $(DESTDIR)$(pkgdatadir)/themes/fonts; tar -xf -)
	-(ln -s tosh $(DESTDIR)$(pkgdatadir)/themes/default)
#	-rm $(DESTDIR)$(pkgdatadir)/themes/engines/*.a
#	-rm $(DESTDIR)$(pkgdatadir)/themes/engines/*.la
	@echo
