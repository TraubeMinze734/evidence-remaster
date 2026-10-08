if BUILD_PLUGIN_TAGLIB
taglibdir=taglib
else
taglibdir=
endif

if BUILD_PLUGIN_LIBVORBIS
vorbisdir=vorbis
else
vorbisdir=
endif

if BUILD_PLUGIN_MP3
id3dir=mp3
else
id3dir=
endif

if BUILD_PLUGIN_IMAGE
imagedir=image
else
imagedir=
endif

if BUILD_PLUGIN_TTF
ttfdir=ttf
else
ttfdir=
endif

if BUILD_PLUGIN_EXTRACTOR
extractordir=extractor
else
extractordir=
endif

SUBDIRS = $(taglibdir) folder ape exif \
          $(vorbisdir) $(id3dir) $(imagedir) $(ttfdir) $(extractordir) \
          backends
