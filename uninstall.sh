#!/bin/sh
# uninstall evidence -- removes exactly what `make install` put there.
#
#   ./uninstall.sh                          # uninstall from /usr/local
#   ./uninstall.sh --prefix=/usr             # uninstall from /usr
#   PREFIX=/opt/evidence ./uninstall.sh      # same, via environment
#   DESTDIR=/tmp/pkg ./uninstall.sh          # pseudo-install (packaging)
#   ./uninstall.sh --dry-run                 # show what would be removed
#
# Only the files/dirs listed below are touched; nothing else under
# $PREFIX is ever removed.

set -e

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"
DRY_RUN=0

for arg in "$@"; do
  case "$arg" in
    --prefix=*)  PREFIX="${arg#--prefix=}" ;;
    --destdir=*) DESTDIR="${arg#--destdir=}" ;;
    -n|--dry-run) DRY_RUN=1 ;;
    --help|-h)
      sed -n '2,10p' "$0"
      exit 0 ;;
    *) echo "unknown option: $arg (try --help)" >&2; exit 1 ;;
  esac
done

ROOT="${DESTDIR}${PREFIX}"

# everything `make install` creates (see do-install in Makefile)
ITEMS="
bin/evidence
bin/evsh
share/applications/evidence.desktop
share/pixmaps/evidence.xpm
share/evidence
lib/evidence
"

# dirs that become empty after removal and are safe to drop
EMPTY_DIRS="
share/evidence/themes
share/evidence/icons
share/applications
share/pixmaps
share/evidence
lib/evidence
lib
share
bin
"

rm_item() {
  if [ "$DRY_RUN" = 1 ]; then
    [ -e "$ROOT/$1" ] && echo "would remove: $ROOT/$1"
    return 0
  fi
  if [ -e "$ROOT/$1" ] || [ -L "$ROOT/$1" ]; then
    rm -rf "$ROOT/$1"
    echo "removed: $ROOT/$1"
  fi
}

for item in $ITEMS; do
  rm_item "$item"
done

# drop now-empty directories bottom-up, but never $PREFIX itself
if [ "$DRY_RUN" = 0 ]; then
  for d in $EMPTY_DIRS; do
    if [ -d "$ROOT/$d" ] && [ -z "$(ls -A "$ROOT/$d" 2>/dev/null)" ]; then
      rmdir "$ROOT/$d" 2>/dev/null && echo "removed empty dir: $ROOT/$d"
    fi
  done
  # last resort: $PREFIX itself if it is now completely empty
  if [ -d "$ROOT" ] && [ -z "$(ls -A "$ROOT" 2>/dev/null)" ]; then
    rmdir "$ROOT" && echo "removed empty dir: $ROOT"
  fi
fi

if [ "$DRY_RUN" = 1 ]; then
  echo "dry run: nothing was removed."
fi
