# Building evidence (remaster)

The original 2003 autoconf/automake build is kept for reference but no longer
works with modern toolchains. Use the new lightweight build instead:

    ./configure.sh
    make
    ./evidence

`configure.sh` uses pkg-config to detect optional dependencies and writes
`config.h` and `config.mk`. Run `make install` to install the binary, setup files (mime.types,
menus, providers), themes, icon-sets, plugins and theme engines below
`PREFIX` (default `/usr/local`, override with `PREFIX=/usr make install`).
The program finds its data via `PACKAGE_DATA_DIR`, which configure.sh
derives from the same `PREFIX`.

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
