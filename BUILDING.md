# Building evidence (remaster)

The original 2003 autoconf/automake build is kept for reference but no longer
works with modern toolchains. Use the new lightweight build instead:

    ./configure.sh
    make
    ./evidence

`configure.sh` uses pkg-config to detect optional dependencies and writes
`config.h` and `config.mk`. Nothing is installed yet (`make install` follows
in a later step).

## Detected dependencies

| feature    | pkg-config modules            | notes                                  |
|------------|-------------------------------|----------------------------------------|
| GLib2      | glib-2.0                      | optional; util.c falls back to POSIX   |
| GTK2 GUI   | gtk+-2.0                      | GTK2 backend                           |
| EFL GUI    | evas, ecore, ecore-evas, edje | EFL backend (buffer-engine port)       |
| cairo      | cairo                         | blits the Evas buffer into the GdkWindow |
| Ecore-IPC  | ecore-ipc                     | IPC backend / evsh                     |
| Imlib2     | imlib2                        | thumbnailer                            |
| libpng     | libpng                        | thumbnailer (strongly recommended)    |
| libjpeg    | libjpeg                       | JPEG thumbnails                        |
| PCRE       | libpcre                       | regex provider                         |
| X11        | x11                           | desktop/window helpers                 |

If neither GTK2 nor EFL is found, a **headless** stub backend
(`src/gui/headless.c`) is linked instead: the core (model, filesystem,
metadata, thumbnailer, IPC) runs, all GUI calls become no-ops. This is
useful for CI and core testing.

## Status / roadmap

1. [x] New build system (`configure.sh` + `Makefile`), core builds headless
2. [ ] GTK2 backend build (needs gtk+-2.0 dev packages; code unchanged)
3. [~] EFL backend: GTK2 icon-view/shelf embed an Evas canvas again.
       The raw X11 engine setup was replaced by Evas' buffer engine, which
       renders into a backing-store blitted to the GdkWindow via cairo
       (modern EFL no longer installs the X11 engine headers).  The legacy
       gradient API (`evas_object_gradient_*`), removed from modern EFL, is
       reimplemented on top of EvasVG (see src/gui/efl/common/evas_compat.*);
       the original theme engines build against it unmodified.  Engines
       depending on dead libs (Edb, Ebits, Ebg) are not built.
4. [ ] Replace/remove dead deps: Ebits, Edb (removed from EFL ~2004)
5. [ ] Re-enable plugins (providers/actions/thumbnailer plugins)
