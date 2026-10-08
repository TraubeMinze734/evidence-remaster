## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS     = 1.7 foreign

# A list of all the files in the current directory which can be regenerated
MAINTAINERCLEANFILES = Makefile.in Makefile

EXTRA_DIST           = README

DEPENDENCIES         = $(top_srcdir)/src/gui/efl/common/evas_macros.h
LDFLAGS              = -module -avoid-version -rdynamic -shared
INCLUDES             = -I$(top_srcdir)/src\
                       -I$(top_srcdir)/src/gui/common \
                       -I$(top_srcdir)/src/gui/efl/common \
                       -I. \
                        @evas_cflags@

pkgdir               = $(libdir)/evidence/gui/efl/engines
# pkgdir             =  $(pkgdatadir)/themes/engines

.o.so:
	$(CC) $(INCLUDES) $(LDFLAGS) $< -o $@

clean:
	-rm ix_textpath ix_rephormed ix_LCARS \
	bg_edje bg_ebg bg_ebg_trans bg_trans bg_tile bg_scale \
	rb_edb rb_gradient rb_gradient2 rb_nautilus \
	sb_edb sb_example sb_default \
	tb_default \
	tt_diagram tt_traditional tt_wholewindow tt_centre \
	ic_edje ic_alignbelow ic_alignright ic_nautilus \
	id_lain id_bubbles id_flutterby \
	ia_grow ia_growfade ia_basic ia_throb ia_solid ia_colour ia_grey ia_creepy ia_creepology

if BUILD_WITH_EDJE
EDJE_TGTS            = bg_edje.la ic_edje.la
endif

if BUILD_WITH_EVAS2

pkg_LTLIBRARIES      = ix_rephormed.la ix_LCARS.la \
		       ia_basic.la ia_throb.la ia_grow.la ia_solid.la \
		       ia_colour.la ia_growfade.la ia_grey.la ia_creepy.la ia_creepology.la \
		       sb_edb.la sb_example.la sb_default.la \
		       tt_diagram.la tt_wholewindow.la tt_traditional.la tt_centre.la \
		       tb_default.la \
		       ic_alignbelow.la ic_alignright.la ic_nautilus.la \
		       bg_ebg.la bg_ebg_trans.la bg_tile.la bg_scale.la \
		       $(EDJE_TGTS) bg_trans.la \
		       rb_gradient.la rb_edb.la rb_gradient2.la rb_nautilus.la \
		       id_lain.la id_bubbles.la id_flutterby.la

endif



sb_edb_la_SOURCES  =        sb_edb.c Ebits.h Ebits_private.h
sb_edb_la_CFLAGS =          @edb_cflags@
sb_edb_la_LIBADD =          @edb_libs@

sb_example_la_SOURCES =     sb_example.c
sb_default_la_SOURCES =     sb_default.c



ia_colour_la_SOURCES =      ia_colour.c

ia_grey_la_SOURCES =        ia_common.c ia_common.h ia_grey.c

ia_throb_la_SOURCES =       ia_common.c ia_common.h ia_throb.c

ia_solid_la_SOURCES =       ia_common.c ia_common.h ia_solid.c

ia_basic_la_SOURCES =       ia_common.c ia_common.h ia_basic.c

ia_grow_la_SOURCES =        ia_common.c ia_common.h ia_grow.c

ia_growfade_la_SOURCES =    ia_common.c ia_common.h ia_growfade.c

ia_creepy_la_SOURCES =      ia_common.c ia_common.h ia_creepy.c

ia_creepology_la_SOURCES =  ia_common.c ia_common.h ia_creepology.c



ix_rephormed_la_SOURCES =   ix_textpath.c ix_rephormed.c
ix_rephormed_la_CFLAGS  =   @imlib2_cflags@
ix_rephormed_la_LIBADD  =   @imlib2_libs@

ix_LCARS_la_SOURCES     =   ix_textpath.c ix_LCARS.c

# ix_xmms_la_SOURCES      =   ix_xmms.c
# ix_xmms_la_CFLAGS       =   @XMMS_CFLAGS@
# ix_xmms_la_LIBADD       =   @XMMS_LIBS@



tt_wholewindow_la_SOURCES = tt_wholewindow.c

tt_diagram_la_SOURCES =     tt_diagram.c

tt_traditional_la_SOURCES = tt_traditional.c

tt_centre_la_SOURCES =      tt_centre.c

tb_default_la_SOURCES =     tb_default.c



ic_alignbelow_la_SOURCES =  ic_alignbelow.c

ic_alignright_la_SOURCES =  ic_alignright.c

ic_nautilus_la_SOURCES =    ic_nautilus.c

ic_edje_la_SOURCES =        ic_edje.c
ic_edje_la_CFLAGS =         @edje_cflags@
ic_edje_la_LIBADD =         @edje_libs@



bg_trans_la_SOURCES =       bg_trans.c

bg_tile_la_SOURCES =        bg_tile.c

bg_scale_la_SOURCES =       bg_scale.c

bg_ebg_la_SOURCES =         bg_ebg.c e_bg.c Ebg.h
bg_ebg_la_CFLAGS =          @edb_cflags@   # @ebg_cflags@
bg_ebg_la_LIBADD =          @edb_libs@     # @ebg_libs@

bg_ebg_trans_la_SOURCES =   bg_ebg_trans.c e_bg.c Ebg.h
bg_ebg_trans_la_CFLAGS =    @edb_cflags@   # @ebg_cflags@
bg_ebg_trans_la_LIBADD =    @edb_libs@     # @ebg_libs@

bg_edje_la_SOURCES =        bg_edje.c
bg_edje_la_CFLAGS =         @edje_cflags@
bg_edje_la_LIBADD =         @edje_libs@



rb_gradient_la_SOURCES =    rb_gradient.c

rb_gradient2_la_SOURCES =   rb_gradient2.c

rb_nautilus_la_SOURCES =    rb_nautilus.c

rb_edb_la_SOURCES =         rb_edb.c
rb_edb_la_CFLAGS =          @edb_cflags@
rb_edb_la_LIBADD =          @edb_libs@



id_bubbles_la_SOURCES =     id_bubbles.c
id_lain_la_SOURCES =        id_lain.c
id_flutterby_la_SOURCES =   id_flutterby.c



install-data-local:
	-(mkdir -p $(DESTDIR)$(pkgdatadir)/themes/LCARS; cp -p LCARS_*.png $(DESTDIR)$(pkgdatadir)/themes/LCARS)
	-(cd $(DESTDIR)$(pkgdatadir)/themes/fishtank; ln -s ../../icons/fishtank_128/Fish1.png Fish1.png)
	-(cd $(DESTDIR)$(pkgdatadir)/themes/metallo; ln -s ../../icons/ento_64/Butterfly_green.png Butterfly0.png)
	-(cd $(DESTDIR)$(pkgdatadir)/themes/metallo; ln -s ../../icons/ento_64/Butterfly_pink.png Butterfly1.png)
	-(cd $(DESTDIR)$(pkgdatadir)/themes/metallo; ln -s ../../icons/ento_64/Butterfly_blue.png Butterfly2.png)
