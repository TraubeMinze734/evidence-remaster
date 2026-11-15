# Building evidence (remaster)

The original 2003 autoconf/automake build is kept for reference but no longer
works with modern toolchains. Use the new lightweight build instead:

    ./configure.sh
    make
    ./evidence

`configure.sh` uses pkg-config to detect optional dependencies and writes
`config.h` and `config.mk`. Run `make install` to install the binary, setup files (mime.types,
menus, providers), themes, icon-sets, plugins and theme engines below
`PREFIX` (default `/usr/local`, override with `--prefix=DIR`, `PREFIX=/usr make install`,
or the `PREFIX` environment variable).
The program finds its data via `PACKAGE_DATA_DIR`, which configure.sh
derives from the same `PREFIX`.

## Configure options

Run `./configure.sh --help` for the full list. Every optional dependency is
auto-detected by default and can be forced on or off:

    ./configure.sh --with-gtk3                 # GTK3 instead of GTK2 GUI
    ./configure.sh --without-imlib2            # build without Imlib2
    ./configure.sh --without-gtk2 --without-gtk3   # no GTK frontend (headless build)
    ./configure.sh --with-emotion --without-x11   # force one, disable another

`--with-<feature>` fails with an error if the library is not found;
`--without-<feature>` skips it even if present. Known features:
`glib2 cairo efl imlib2 ecore-ipc ecore png jpeg pcre magic emotion extractor
curl x11 acl vorbis taglib id3 freetype2 readline gtk`.

Notes:

- GTK is the window frontend; EFL is only used as the canvas inside it
  (gevas). Without GTK there is no GUI, so `--without-gtk*` builds the
  headless core (plus `evsh` if ecore-ipc is available).
- The chosen options are stored in `config.mk` (`CONFIGURE_ARGS`), so a
  plain `make` re-runs configure with the same options if needed.

## Detected dependencies

| feature    | pkg-config modules            | notes                                  |
|------------|-------------------------------|----------------------------------------|
| GLib2      | glib-2.0                      | optional; util.c falls back to POSIX   |
| GTK GUI    | gtk+-2.0 or gtk+-3.0           | GTK backend (--with-gtk3 for GTK3)    |
| EFL GUI    | evas, ecore, ecore-evas, edje | EFL backend (buffer-engine port)       |
| cairo      | cairo                         | blits the Evas buffer into the GdkWindow |
| Emotion    | emotion                       | live/static video thumbnails (needs a backend: gstreamer or xine) |
| inotify    | (kernel, Linux only)          | auto-refresh directories on change |
| libextractor | extractor                  | mass meta-data extraction for the tooltip/info dialog |
| libcurl    | libcurl                       | remote URLs (download for file_run %f etc.) |
| Ecore-IPC  | ecore-ipc                     | IPC backend / evsh                     |
| Imlib2     | imlib2                        | thumbnailer                            |
| libpng     | libpng                        | thumbnailer (strongly recommended)    |
| libjpeg    | libjpeg                       | JPEG thumbnails                        |
| PCRE       | libpcre                       | regex provider                         |
| X11        | x11                           | desktop/window helpers                 |
