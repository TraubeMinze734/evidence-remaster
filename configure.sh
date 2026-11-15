#!/bin/sh
# configure.sh for evidence -- lightweight replacement for the 2003 autoconf setup.
# Detects dependencies via pkg-config and writes config.h + config.mk.

set -e

PREFIX="${PREFIX:-/usr/local}"
GTK_MAJOR="${GTK_MAJOR:-2}"
PKG=${PKG_CONFIG:-pkg-config}

say() { printf '%s\n' "$*"; }
die() { say "ERROR: $*"; exit 1; }

usage() {
  cat <<'USAGE'
usage: ./configure.sh [options]

GUI backend:
  --with-gtk2              build the GTK2 GUI backend (default)
  --with-gtk3              build the GTK3 GUI backend
  --without-gtk            disable the GTK GUI backend
  --without-gtk2           disable the GTK2 GUI backend
  --without-gtk3           disable the GTK3 GUI backend

optional features (auto-detected by default; override per feature):
  --with-<feature>         require <feature> (error if not found)
  --without-<feature>      disable <feature>
  features: glib2 cairo efl imlib2 ecore-ipc ecore png jpeg pcre magic
            emotion extractor curl x11 acl vorbis taglib id3 freetype2
            readline gtk

misc:
  --prefix=DIR             installation prefix (default: /usr/local or $PREFIX)
  --help                   show this help and exit

environment:
  PREFIX=DIR               same as --prefix=DIR
  PKG_CONFIG=path          pkg-config binary to use
USAGE
}

WITH_FEATURES=""
WITHOUT_FEATURES=""
CONFIGURE_ARGS="$*"

for arg in "$@"; do
  case "$arg" in
    --help|-h)      usage; exit 0 ;;
    --prefix=*)     PREFIX="${arg#--prefix=}" ;;
    --with-gtk2)    GTK_MAJOR=2; WITH_FEATURES="$WITH_FEATURES gtk2" ;;
    --with-gtk3)    GTK_MAJOR=3; WITH_FEATURES="$WITH_FEATURES gtk3" ;;
    --without-gtk)  WITHOUT_FEATURES="$WITHOUT_FEATURES gtk" ;;
    --without-gtk2) WITHOUT_FEATURES="$WITHOUT_FEATURES gtk2" ;;
    --without-gtk3) WITHOUT_FEATURES="$WITHOUT_FEATURES gtk3" ;;
    --with-*)       WITH_FEATURES="$WITH_FEATURES ${arg#--with-}" ;;
    --without-*)    WITHOUT_FEATURES="$WITHOUT_FEATURES ${arg#--without-}" ;;
    *) die "unknown option: $arg (try --help)" ;;
  esac
done

# known optional features (anything else in --with-/--without- is a typo)
KNOWN_FEATURES="gtk gtk2 gtk3 glib2 cairo efl imlib2 ecore-ipc ecore png jpeg pcre magic emotion extractor curl x11 acl vorbis taglib id3 freetype2 readline"
for _f in $WITH_FEATURES $WITHOUT_FEATURES; do
  case " $KNOWN_FEATURES " in
    *" $_f "*) ;;
    *) die "unknown feature: --$_f (known: $KNOWN_FEATURES)" ;;
  esac
done

# feat <name> -> on (--with-<name>), off (--without-<name>), auto (default)
feat() {
  case " $WITHOUT_FEATURES " in *" $1 "*) echo off; return ;; esac
  case " $WITH_FEATURES " in *" $1 "*) echo on; return ;; esac
  echo auto
}

command -v "$PKG" >/dev/null 2>&1 || die "pkg-config not found. Install pkg-config first."

have_pc() { "$PKG" --exists "$@" 2>/dev/null; }
pc_cflags() { "$PKG" --cflags "$@" 2>/dev/null; }
pc_libs() { "$PKG" --libs "$@" 2>/dev/null; }

# detect <feature> <VAR> <pkg-config module...>
#   auto: enable <VAR> if pkg-config finds all modules
#   on:   require them (die if missing); off: skip detection entirely
detect() {
  _f=$1
  _v=$2
  shift 2
  eval "${_v}=no; ${_v}_CFLAGS=; ${_v}_LIBS="
  _st=$(feat "$_f")
  if [ "$_st" = off ]; then
    return 0
  fi
  if have_pc "$@"; then
    _c=$(pc_cflags "$@")
    _l=$(pc_libs "$@")
    eval "${_v}=yes; ${_v}_CFLAGS=\$_c; ${_v}_LIBS=\$_l"
  elif [ "$_st" = on ]; then
    die "--with-$_f was requested, but pkg-config could not find: $*"
  fi
}

# --- optional dependencies (each may be forced via --with-/--without-<feature>)
detect glib2 GLIB2 glib-2.0

# GTK GUI backend: --with-gtk2/--with-gtk3 select the major version,
# --without-gtk/--without-gtk2/--without-gtk3 disable it; an explicit
# --with-gtkN (or --with-gtk) requires the library and fails if missing
GTK_PC=gtk+-2.0
if [ "$GTK_MAJOR" = 3 ]; then GTK_PC=gtk+-3.0; fi
GTK2=no
if [ "$(feat gtk)" != off ] && [ "$(feat "gtk$GTK_MAJOR")" != off ]; then
  if have_pc $GTK_PC; then GTK2=yes; GTK2_CFLAGS=$(pc_cflags $GTK_PC); GTK2_LIBS=$(pc_libs $GTK_PC); fi
fi
if [ "$GTK2" = no ] && { [ "$(feat gtk)" = on ] || [ "$(feat "gtk$GTK_MAJOR")" = on ]; }; then
  die "$GTK_PC was requested but was not found by pkg-config."
fi

detect cairo     CAIRO     cairo
# EFL's flags live in EVAS_CFLAGS/EVAS_LIBS (not EFL_*), see config.mk
detect efl       EFL       evas ecore ecore-evas edje
if [ "$EFL" = yes ]; then
  EVAS_CFLAGS=$(pc_cflags evas ecore ecore-evas edje)
  EVAS_LIBS=$(pc_libs evas ecore ecore-evas edje)
fi
# EFL is only used as the canvas inside the GTK frontend (gevas); without a
# GTK window system there is no GUI, so the build falls back to headless
if [ "$GTK2" = no ] && [ "$EFL" = yes ]; then
  say "note: no GTK frontend -- EFL canvas not used, building headless"
  EFL=no
  EVAS_CFLAGS=
  EVAS_LIBS=
fi
detect imlib2    IMLIB2    imlib2
detect ecore-ipc ECOREIPC  ecore-ipc
detect ecore     ECORE     ecore
detect png       PNG       libpng
detect jpeg      JPEG      libjpeg
detect pcre      PCRE      libpcre
detect magic     LIBMAGIC  libmagic
detect emotion   EMOTION   emotion
detect curl      LIBCURL   libcurl
detect x11       X11       x11
detect freetype2 FT2       freetype2

# libextractor: pkg-config module may be named extractor or libextractor,
# plus the plain-header fallback
EXTRACTOR=no
if [ "$(feat extractor)" != off ]; then
  if have_pc extractor; then
    EXTRACTOR=yes; EXTRACTOR_CFLAGS=$(pc_cflags extractor); EXTRACTOR_LIBS=$(pc_libs extractor)
  elif have_pc libextractor; then
    EXTRACTOR=yes; EXTRACTOR_CFLAGS=$(pc_cflags libextractor); EXTRACTOR_LIBS=$(pc_libs libextractor)
  elif [ -f /usr/include/extractor.h ]; then
    EXTRACTOR=yes; EXTRACTOR_CFLAGS=""; EXTRACTOR_LIBS="-lextractor"
  elif [ "$(feat extractor)" = on ]; then
    die "--with-extractor was requested, but libextractor was not found."
  fi
fi
# libacl is dlopen()ed at run-time (libacl_init in the posix backend), so we
# only need the headers at compile time -- matching the 2005 configure.in
# logic (--enable-acls, AC_CHECK_HEADER(sys/acl.h)).
LIBACL=no
if [ "$(feat acl)" != off ]; then
  if have_pc libacl; then
    LIBACL=yes
  elif [ -f /usr/include/sys/acl.h ] || [ -f /usr/local/include/sys/acl.h ]; then
    LIBACL=yes
  elif [ "$(feat acl)" = on ]; then
    die "--with-acl was requested, but libacl was not found."
  fi
fi
# --- meta-data provider libs (2005 configure.in: plugin-vorbis, plugin-id3, ---
# --- plugin-taglib, plugin-ttf) ----------------------------------------------
VORBIS=no
if [ "$(feat vorbis)" != off ]; then
  if have_pc vorbis vorbisfile; then
    VORBIS=yes; VORBIS_CFLAGS=$(pc_cflags vorbis vorbisfile); VORBIS_LIBS=$(pc_libs vorbis vorbisfile ogg)
  elif [ "$(feat vorbis)" = on ]; then
    die "--with-vorbis was requested, but pkg-config could not find vorbis/vorbisfile."
  fi
fi
detect taglib TAGLIB taglib
# id3lib ships no pkg-config file (never did); match the 2005
# AC_SEARCH_LIBS(ID3Tag_CreateIterator, "id3 -lz -lstdc++") by trying an
# actual test-link against -lid3 -lz -lstdc++ with the id3/tag.h we found.
ID3=no
if [ "$(feat id3)" != off ]; then
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
        if [ "$ID3" = yes ]; then break; fi
      fi
    done
  fi
  if [ "$ID3" = no ] && [ "$(feat id3)" = on ]; then
    die "--with-id3 was requested, but id3lib was not found."
  fi
fi
# readline for evsh (2005 configure.in: VL_LIB_READLINE)
READLINE=no
if [ "$(feat readline)" != off ]; then
  if [ -f /usr/include/readline/readline.h ] || [ -f /usr/local/include/readline/readline.h ]; then
    READLINE=yes; READLINE_CFLAGS=""; READLINE_LIBS="-lreadline"
  elif [ "$(feat readline)" = on ]; then
    die "--with-readline was requested, but readline headers were not found."
  fi
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
  echo "#define PACKAGE_VERSION \"${VERSION:-10.3}\""
  echo "#define VERSION \"${VERSION:-10.3}\""
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
  echo "CONFIGURE_ARGS ?= $CONFIGURE_ARGS"
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
if [ -n "$WITH_FEATURES" ]; then
  say "  forced on:   $WITH_FEATURES"
fi
if [ -n "$WITHOUT_FEATURES" ]; then
  say "  forced off:  $WITHOUT_FEATURES"
fi
say "Now run: make"
