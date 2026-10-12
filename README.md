# evidence-remaster

Remaster von **evidence**, dem Enlightenment-Dateimanager von Azundris aus
dem Jahr 2005 (evidence.sf.net, GPL). Ziel: lauffähig auf modernem Linux
(getestet auf CachyOS, GCC 14, aktuelles EFL aus den Distro-Repositories)
— die EFL-GUI sieht dabei so nah wie möglich am Original aus.

Detaillierter Projektstatus und Fallstricke für Entwickler: siehe
`PROJECT_STATUS.md`, Build-Anleitung: `BUILDING.md`.

## Entfernt

- **Autotools-Build (2003)**: `configure`, `configure.in`, `aclocal.m4`,
  `acinclude.m4`, `Makefile.am`, `Makefile.in` (auch in `src/`),
  `config.h.in`, `ltmain.sh`, `install-sh`, `depcomp`, `missing`, `compile`,
  `config.guess`, `config.sub`, `texinfo.tex`, `INSTALL`, alte `README`.
  Der Build läuft jetzt über `./configure.sh` (pkg-config) + plain `Makefile`.
- **Paketier-/Distributionsreste**: `evidence.spec(.in)` (RPM),
  `evidence.lsm.in`, `evidence.texinfo`.
- **Generierter Doxygen-Output von 2005** (`docs/html`, `docs/latex`,
  `docs/man`, ~11 MB): erwähnt noch den GnomeCanvas-Backend-Code, der
  inzwischen entfernt wurde. Die handgeschriebene Doku in `docs/` bleibt.
- **CVS-Verwaltungsmetadaten** (`CVS/`-Ordner im kompletten Baum).
- **GnomeCanvas-Backend** (~4.400 Zeilen, `gnome-icon-list/item`): war
  schon im Original nie erreichbar (`HAVE_GNOMECANVAS` wurde nie definiert,
  `libgnomecanvas` existiert auf modernen Systemen nicht mehr).
- **Tote Plugins, absichtlich nicht reaktiviert**: `epeg.so` (Lib aus EFL
  entfernt), `xine.so`/`avifile.so` (durch Emotion ersetzt),
  `ipc_dcop.so` (KDE3-DCOP), `ipc_dbus.so` (Pre-1.0-D-Bus-API),
  `file_efsd`/`file_gnomevfs`/`file_kio` (Backends; POSIX-Backend aktiv).

## Neu

- `configure.sh` (pkg-config-Erkennung) + plain `Makefile` ersetzen den
  autotools-Build; `make install` nach `/usr/local` (oder `PREFIX=`).
- **GTK3-Backend**: GTK2 bleibt der getestete Default-Pfad,
  `./configure.sh --with-gtk3` baut gegen GTK3 (`HAVE_GTK3`) inkl.
  Wayland-Support (`ecore_main_loop_glib_integrate()` mit Fallback-Pumpe,
  Compat-Header für Keysyms/Selection/DnD/Cell-Renderer).
- **Evas-Gradient-Kompat-Schicht** `evas_compat.c`: ersetzt die aus
  modernem EFL entfernte `evas_object_gradient`-API — Rubberband,
  Scrollbars, Tooltips und Icon-Frames rendern originalgetreu.
- **Buffer-Engine-Port**: der Icon-View rendert über die Evas-Buffer-Engine
  mit Cairo-Blit ins GDK-Fenster (die alten X11-Engine-Header sind aus EFL
  entfernt). Gevas-Widget auf GType portiert.
- **Emotion-Video-Thumbnails** (Frame bei ~8 %) und **Live-Video-Tooltips**
  mit Ton, **Imlib2-Bild-Thumbnails** (`imlib.so`), freedesktop-Thumb-Cache
  in `~/.thumbnails/`.
- **Metadata-Provider & Thumbnailer komplett**: exif (eigener JHead-Parser,
  kein libexif), image (Imlib2), vorbis, ape, mp3 (id3lib), taglib, ttf
  (freetype2), ID3-Cover-Art-Thumbnailer, TTF-Font-Vorschau, ACL-Editor.
- **Action-Plugins komplett**: tag2name, name2tag, sortkey2name, playlist,
  searchable, searchable_r.
- **evsh + `ipc_ecore.so`**: IPC-Client und Backend (lokaler User-Socket),
  evsh mit readline-History.
- **inotify-Monitor** (statt FAM), **GTK-Clipboard-Integration**
  (Strg+C/X/V mit Dolphin/Nautilus/Thunar), Drag&Drop, Kontextmenü
  (Delete/Rename/New folder/Properties), Doppelklick via `xdg-open`,
  Kopier-Jobs mit Fortschrittsanzeige (nahe Platten-Geschwindigkeit).
- SIGPIPE-Handling, Exit-Synchronisation mit laufenden Jobs, jede Menge
  Crash-Fixes für modernes EFL/GCC (EFL-Threading, `#endif`-Fallstricke,
  Double-Free in Jobs).

## Aus dem Original übernommen

- Kompletter Kern: Modell, Scanner, Datei-Actions, Job-Maschinerie,
  Typebuffer, Tag-/Metadaten-System, Plugin-Loader (Backend/Action/
  Metadata/Thumbnailer/Theme-Engine), IPC.
- EFL-GUI mit Evas-Icon-View, gevas-Widget, Shelf, Browser-/Tree-Views,
  Tooltips — Look & Feel wie 2005, alle installierbaren Themes (tosh, efm,
  fishtank, nautilus, rephormed (default), LCARS). `metallo` und `OtE` sind
  nicht installierbar (2005er Binärformate, die modernes Edje nicht lädt)
  und fallen auf `default` zurück.
- Icon-Sets (indigo_128, tosh_128, efm, rephormed, fishtank_128, LCARS,
  gorilla_36/48/72, ento_64), MIME-Setup (`setup/`), Themes
  (`evidence.themes/`), Manpage, Desktop-Datei, `AUTHORS`, `ChangeLog`,
  `COPYING`, `NEWS`, `DEPENDENCIES`, `HELP_DEVELOP`.
- Handgeschriebene Original-Doku in `docs/` (FAQ, enlightened.txt,
  typebuffer.txt, ...).

## Build

```
./configure.sh            # GTK2 (Default, getestet)
./configure.sh --with-gtk3
make
make install              # PREFIX=/usr für systemweit
```

Details und erkannte Abhängigkeiten: `BUILDING.md`.
