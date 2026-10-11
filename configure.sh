#!/bin/sh
# configure.sh for evidence -- lightweight replacement for the 2003 autoconf setup.
# Detects dependencies via pkg-config and writes config.h + config.mk.

set -e

PREFIX="${PREFIX:-/usr/local}"
GTK_MAJOR="${GTK_MAJOR:-2}"
PKG=${PKG_CONFIG:-pkg-config}

say() { printf '%s\n' "$*"; }
die() { say "ERROR: $*"; exit 1; }

command -v "$PKG" >/dev/null 2>&1 || die "pkg-config not found. Install pkg-config first."
for arg in "$@"; do
  case "$arg" in
    --with-gtk3) GTK_MAJOR=3 ;;
    --with-gtk2) GTK_MAJOR=2 ;;
    *) die "unknown option: $arg (supported: --with-gtk2, --with-gtk3)" ;;
  esac
done

have_pc() { "$PKG" --exists "$@" 2>/dev/null; }
pc_cflags() { "$PKG" --cflags "$@" 2>/dev/null; }
pc_libs() { "$PKG" --libs "$@" 2>/dev/null; }

GLIB2=no; if have_pc glib-2.0; then GLIB2=yes; GLIB2_CFLAGS=$(pc_cflags glib-2.0); GLIB2_LIBS=$(pc_libs glib-2.0); fi

# --- optional ------------------------------------------------------------
GTK_PC=gtk+-2.0
if [ "$GTK_MAJOR" = 3 ]; then GTK_PC=gtk+-3.0; fi
GTK2=no
if have_pc $GTK_PC; then GTK2=yes; GTK2_CFLAGS=$(pc_cflags $GTK_PC); GTK2_LIBS=$(pc_libs $GTK_PC); fi
if [ "$GTK_MAJOR" = 3 ] && [ "$GTK2" = no ]; then
  die "GTK3 requested (--with-gtk3) but gtk+-3.0 was not found by pkg-config."
fi
CAIRO=no; if have_pc cairo; then CAIRO=yes; CAIRO_CFLAGS=$(pc_cflags cairo); CAIRO_LIBS=$(pc_libs cairo); fi
EFL=no;   if have_pc evas ecore ecore-evas edje; then EFL=yes; EVAS_CFLAGS=$(pc_cflags evas ecore ecore-evas edje); EVAS_LIBS=$(pc_libs evas ecore ecore-evas edje); fi
IMLIB2=no; if have_pc imlib2; then IMLIB2=yes; IMLIB2_CFLAGS=$(pc_cflags imlib2); IMLIB2_LIBS=$(pc_libs imlib2); fi
ECOREIPC=no; if have_pc ecore-ipc; then ECOREIPC=yes; ECOREIPC_CFLAGS=$(pc_cflags ecore-ipc); ECOREIPC_LIBS=$(pc_libs ecore-ipc); fi
ECORE=no;   if have_pc ecore; then ECORE=yes; ECORE_LIBS=$(pc_libs ecore); fi
PNG=no;   if have_pc libpng; then PNG=yes; PNG_CFLAGS=$(pc_cflags libpng); PNG_LIBS=$(pc_libs libpng); fi
JPEG=no;  if have_pc libjpeg; then JPEG=yes; JPEG_CFLAGS=$(pc_cflags libjpeg); JPEG_LIBS=$(pc_libs libjpeg); fi
PCRE=no;  if have_pc libpcre; then PCRE=yes; PCRE_CFLAGS=$(pc_cflags libpcre); PCRE_LIBS=$(pc_libs libpcre); fi
LIBMAGIC=no; if have_pc libmagic; then LIBMAGIC=yes; LIBMAGIC_CFLAGS=$(pc_cflags libmagic); LIBMAGIC_LIBS=$(pc_libs libmagic); fi
EMOTION=no; if have_pc emotion; then EMOTION=yes; EMOTION_CFLAGS=$(pc_cflags emotion); EMOTION_LIBS=$(pc_libs emotion); fi
EXTRACTOR=no; for ex in extractor libextractor; do if have_pc $ex; then EXTRACTOR=yes; EXTRACTOR_CFLAGS=$(pc_cflags $ex); EXTRACTOR_LIBS=$(pc_libs $ex); break; fi; done
if [ "$EXTRACTOR" = no ] && [ -f /usr/include/extractor.h ]; then EXTRACTOR=yes; EXTRACTOR_CFLAGS=""; EXTRACTOR_LIBS="-lextractor"; fi
LIBCURL=no; if have_pc libcurl; then LIBCURL=yes; LIBCURL_CFLAGS=$(pc_cflags libcurl); LIBCURL_LIBS=$(pc_libs libcurl); fi
X11=no;   if have_pc x11; then X11=yes; X11_CFLAGS=$(pc_cflags x11); X11_LIBS=$(pc_libs x11); fi
# libacl is dlopen()ed at run-time (libacl_init in the posix backend), so we
# only need the headers at compile time -- matching the 2005 configure.in
# logic (--enable-acls, AC_CHECK_HEADER(sys/acl.h)).
LIBACL=no
if have_pc libacl; then
  LIBACL=yes
elif [ -f /usr/include/sys/acl.h ] || [ -f /usr/local/include/sys/acl.h ]; then
  LIBACL=yes
fi
# --- meta-data provider libs (2005 configure.in: plugin-vorbis, plugin-id3, ---
# --- plugin-taglib, plugin-ttf) ----------------------------------------------
VORBIS=no; if have_pc vorbis vorbisfile; then VORBIS=yes; VORBIS_CFLAGS=$(pc_cflags vorbis vorbisfile); VORBIS_LIBS=$(pc_libs vorbis vorbisfile ogg); fi
TAGLIB=no; if have_pc taglib; then TAGLIB=yes; TAGLIB_CFLAGS=$(pc_cflags taglib); TAGLIB_LIBS=$(pc_libs taglib); fi
# id3lib ships no pkg-config file (never did); match the 2005
# AC_SEARCH_LIBS(ID3Tag_CreateIterator, "id3 -lz -lstdc++") by trying an
# actual test-link against -lid3 -lz -lstdc++ with the id3/tag.h we found.
ID3=no
if have_pc id3; then
  ID3=yes; ID3_CFLAGS=$(pc_cflags id3); ID3_LIBS=$(pc_libs id3)
else
  for inc in /usr/include /usr/local/include; do
    if [ -f "$inc/id3/tag.h" ]; then
      cat > /tmp/evidence_id3test.$$.cpp <<'TESTEOF'
#include <id3.h>
int main() { ID3Tag *t = ID3Tag_New(); (void)t; return 0; }
TESTEOF
      if c++ -I"$inc" /tmp/evidence_id3test.$$.cpp -lid3 -lz -o /tmp/evidence_id3test.$$.out 2>/dev/null; then
        ID3=yes; ID3_CFLAGS="-I$inc"; ID3_LIBS="-lid3 -lz -lstdc++"
      fi
      rm -f /tmp/evidence_id3test.$$.cpp /tmp/evidence_id3test.$$.out
      [ "$ID3" = yes ] && break
    fi
  done
fi
FT2=no;    if have_pc freetype2; then FT2=yes; FT2_CFLAGS=$(pc_cflags freetype2); FT2_LIBS=$(pc_libs freetype2); fi
# readline for evsh (2005 configure.in: VL_LIB_READLINE)
READLINE=no
if [ -f /usr/include/readline/readline.h ] || [ -f /usr/local/include/readline/readline.h ]; then
  READLINE=yes; READLINE_CFLAGS=""; READLINE_LIBS="-lreadline"
fi

if [ "$GTK2" = no ] && [ "$EFL" = no ]; then
  say "WARNING: neither GTK2 nor EFL found -- no GUI backend will be built."
fi

HDRNAME() { echo "$1" | sed 's|/|_|; s|\.|_|' | tr 'a-z' 'A-Z'; }

# --- config.h --------------------------------------------------------------
{
  echo "/* generated by configure.sh -- do not edit */"
  echo "#define CONFIGURED_WITH \"configure.sh (options: $CONFIGURE_ARGS)\""
  echo "#define PACKAGE \"evidence\""
  echo "#define PACKAGE_VERSION \"${VERSION:-0.9.5}\""
  echo "#define VERSION \"${VERSION:-0.9.5}\""
  echo "#define PACKAGE_PREFIX \"$PREFIX\""
  echo "#define PACKAGE_DATA_DIR \"$PREFIX/share/evidence\""
  echo "#define PACKAGE_LIB_DIR \"$PREFIX/lib/evidence\""
  echo "#define LOCALEDIR \"$PREFIX/share/locale\""
  for h in dirent.h dlfcn.h inttypes.h langinfo.h limits.h memory.h stdint.h stdlib.h strings.h string.h unistd.h sys/stat.h sys/types.h time.h; do
    echo "#define HAVE_$(HDRNAME "$h") 1"
  done
  echo "#define HAVE_ALARM 1"
  echo "#define HAVE_FORK 1"
  echo "#define HAVE_FSEEKO 1"
  echo "#define HAVE_FTELLO 1"
  echo "#define HAVE_LANGINFO_CODESET 1"
  echo "#define HAVE_MEMMOVE 1"
  echo "#define HAVE_MEMSET 1"
  echo "#define HAVE_STATFS 1"
  echo "#define HAVE_STATVFS 1"
  echo "#define HAVE_ST_BLKSIZE 1"
  echo "#define HAVE_ST_BLOCKS 1"
  echo "#define HAVE_STRTOL 1"
  echo "#define HAVE_STRTOLL 1"
  echo "#define HAVE_STRTOK_R 1"
  echo "#define HAVE_WORDEXP 1"
  [ "$GLIB2" = yes ] && echo "#define HAVE_GLIB2 1"
  [ "$GTK2" = yes ] && echo "#define HAVE_GTK2 1"
  if [ "$GTK2" = yes ] && [ "$GTK_MAJOR" = 3 ]; then echo "#define HAVE_GTK3 1"; fi
  [ "$EFL" = yes ] && echo "#define HAVE_EVAS2 1"
  [ "$EFL" = yes ] && echo "#define HAVE_ECORE 1"
  [ "$EFL" = yes ] && echo "#define HAVE_EDJE 1"
  [ "$ECOREIPC" = yes ] && echo "#define HAVE_ECORE_IPC 1"
  [ "$IMLIB2" = yes ] && echo "#define HAVE_IMLIB2 1"
  [ "$PNG" = yes ] && echo "#define HAVE_LIBPNG 1"
  [ "$JPEG" = yes ] && echo "#define HAVE_LIBJPEG 1"
  [ "$PCRE" = yes ] && echo "#define HAVE_PCRE 1"
  [ "$LIBMAGIC" = yes ] && echo "#define HAVE_LIBMAGIC 1"
  [ "$EMOTION" = yes ] && echo "#define HAVE_EMOTION 1"
  [ "$EXTRACTOR" = yes ] && echo "#define HAVE_EXTRACTOR 1"
  [ "$LIBCURL" = yes ] && echo "#define HAVE_LIBCURL 1"
  [ "$LIBACL" = yes ] && echo "#define HAVE_LIBACL 1"
  [ "$VORBIS" = yes ] && echo "#define HAVE_VORBIS 1"
  [ "$TAGLIB" = yes ] && echo "#define HAVE_TAGLIB 1"
  [ "$ID3" = yes ] && echo "#define HAVE_ID3 1"
  [ "$FT2" = yes ] && echo "#define HAVE_FT2 1"
  [ "$READLINE" = yes ] && echo "#define HAVE_LIBREADLINE 1"
  [ "$READLINE" = yes ] && echo "#define HAVE_READLINE_READLINE_H 1"
case "$(uname -s)" in
  Linux) echo "#define I_NOTIFY 1" ;;
esac
  [ "$X11" = yes ] && echo "#define HAVE_X 1"
  # views (old configure.in logic): tree-view and browser-view are always
  # built; the icon-view/shelf require a canvas (Evas or GnomeCanvas)
  echo "#define WITH_TREEVIEW 1"
  echo "#define WITH_BROWSERVIEW 1"
  if [ "$EFL" = yes ]; then
    echo "#define WITH_ICONVIEW 1"
  fi
} > config.h

# --- config.mk --------------------------------------------------------------
{
  echo "# generated by configure.sh -- do not edit"
  echo "PREFIX ?= $PREFIX"
  echo "GLIB2_CFLAGS ?= $GLIB2_CFLAGS"
  echo "GLIB2_LIBS ?= $GLIB2_LIBS"
  echo "GTK2 ?= $GTK2"
  echo "GTK2_CFLAGS ?= $GTK2_CFLAGS"
  echo "GTK2_LIBS ?= $GTK2_LIBS"
  echo "CAIRO_CFLAGS ?= $CAIRO_CFLAGS"
  echo "CAIRO_LIBS ?= $CAIRO_LIBS"
  echo "EFL ?= $EFL"
  echo "EVAS_CFLAGS ?= $EVAS_CFLAGS"
  echo "EVAS_LIBS ?= $EVAS_LIBS"
  echo "IMLIB2 ?= $IMLIB2"
  echo "IMLIB2_CFLAGS ?= $IMLIB2_CFLAGS"
  echo "IMLIB2_LIBS ?= $IMLIB2_LIBS"
  echo "ECOREIPC ?= $ECOREIPC"
  echo "ECOREIPC_CFLAGS ?= $ECOREIPC_CFLAGS"
  echo "ECOREIPC_LIBS ?= $ECOREIPC_LIBS"
  echo "ECORE_LIBS ?= $ECORE_LIBS"
  echo "PNG ?= $PNG"
  echo "PNG_CFLAGS ?= $PNG_CFLAGS"
  echo "PNG_LIBS ?= $PNG_LIBS"
  echo "JPEG ?= $JPEG"
  echo "JPEG_CFLAGS ?= $JPEG_CFLAGS"
  echo "JPEG_LIBS ?= $JPEG_LIBS"
  echo "PCRE ?= $PCRE"
  echo "PCRE_CFLAGS ?= $PCRE_CFLAGS"
  echo "PCRE_LIBS ?= $PCRE_LIBS"
  echo "LIBMAGIC_CFLAGS ?= $LIBMAGIC_CFLAGS"
  echo "LIBMAGIC_LIBS ?= $LIBMAGIC_LIBS"
  echo "EMOTION ?= $EMOTION"
  echo "EMOTION_CFLAGS ?= $EMOTION_CFLAGS"
  echo "EMOTION_LIBS ?= $EMOTION_LIBS"
  echo "EXTRACTOR ?= $EXTRACTOR"
  echo "EXTRACTOR_CFLAGS ?= $EXTRACTOR_CFLAGS"
  echo "EXTRACTOR_LIBS ?= $EXTRACTOR_LIBS"
  echo "LIBCURL ?= $LIBCURL"
  echo "LIBCURL_CFLAGS ?= $LIBCURL_CFLAGS"
  echo "LIBCURL_LIBS ?= $LIBCURL_LIBS"
  echo "GLIB2 ?= $GLIB2"
  echo "GLIB2_CFLAGS ?= $GLIB2_CFLAGS"
  echo "GLIB2_LIBS ?= $GLIB2_LIBS"
  echo "X11 ?= $X11"
  echo "X11_CFLAGS ?= $X11_CFLAGS"
  echo "X11_LIBS ?= $X11_LIBS"
  echo "VORBIS ?= $VORBIS"
  echo "VORBIS_CFLAGS ?= $VORBIS_CFLAGS"
  echo "VORBIS_LIBS ?= $VORBIS_LIBS"
  echo "TAGLIB ?= $TAGLIB"
  echo "TAGLIB_CFLAGS ?= $TAGLIB_CFLAGS"
  echo "TAGLIB_LIBS ?= $TAGLIB_LIBS"
  echo "ID3 ?= $ID3"
  echo "ID3_CFLAGS ?= $ID3_CFLAGS"
  echo "ID3_LIBS ?= $ID3_LIBS"
  echo "FT2 ?= $FT2"
  echo "FT2_CFLAGS ?= $FT2_CFLAGS"
  echo "FT2_LIBS ?= $FT2_LIBS"
  echo "READLINE ?= $READLINE"
  echo "READLINE_CFLAGS ?= $READLINE_CFLAGS"
  echo "READLINE_LIBS ?= $READLINE_LIBS"
} > config.mk

say "evidence configured:"
say "  prefix        $PREFIX"
say "  GLib2         $GLIB2"
say "  GTK backend   $GTK2 (GTK$GTK_MAJOR)"
say "  EFL backend   $EFL"
say "  Ecore-IPC     $ECOREIPC"
say "  libmagic      $LIBMAGIC"
say "  Emotion       $EMOTION"
say "  libextractor  $EXTRACTOR"
say "  libcurl       $LIBCURL"
  say "  ACLs          $LIBACL"
say "  Vorbis        $VORBIS"
say "  taglib        $TAGLIB"
say "  id3lib        $ID3"
say "  freetype2     $FT2"
say "  readline      $READLINE"
say "  Imlib2        $IMLIB2"
say "  libpng        $PNG"
say "  libjpeg       $JPEG"
say "  PCRE          $PCRE"
say "  X11           $X11"
say "Now run: make"
