# evidence-remaster

Current version: **10.4.2** (remaster of the original evidence 0.9.8).

This is an AI rewrite of the Evidence file manager for Enlightenment, which was originally written by Azundris in 2005.

The original project for reference: https://sourceforge.net/projects/evidence/

All credits for the original program go to Azundris and contributors!

The latest source tarball was used for this: https://sourceforge.net/projects/evidence/files/evidence%20source/0.9.8/evidence-0.9.8-20050905.src.rpm

I used the Mistral-hosted GLM-5.3. I did this for fun and as a proof of concept for myself. Personally, I love the look of the old program and wanted to use it on my modern CachyOS. So it’s most likely unstable code since I have no idea what I’m doing! 

Some parts of the program could not be ported since they relied on old backends that dont exist anymore like gnomecanvas, epeg and ebg. Some themes are not gonna be installed by the makefile for similiar reasons.

The AI also reworked the clipboard functionality and added GTK3 as a new backend, thats probably unstable tho.

The makefile also now installes a .destkop file for Evidence.

According to the AI, there was a functionality to play Videos inside the file manager using emotion. It looks a bit out of place, so im not sure if it was actually there or if its an hallucination. Its in there now.

## Build

```
./configure.sh              # GTK2 (Default)
./configure.sh --with-gtk3  # GTK3 (probably unstable?)
make
make install
```

## Release notes

### 10.4.2 (2026-11-15)

- Fix headless build (`--without-gtk2 --without-gtk3`): `noir_sess_close()` stub added to `src/gui/headless.c` (was: undefined reference from `monitors.c`)
- Fix segfault on `./evidence -e` when no theme is installed: guard `icon_view_exposed()` call in `noir_sess_create()` against a NULL iconview (efm/default not found → `icon_view_init()` never ran, but `-e` forces `START_IN_ICONVIEW`)

### 10.4.1 (2026-11-15)

- Cold-start thumbnailer fixes: plugin init (emotion/gstreamer) is dispatched to the GUI thread instead of running in a worker (was: `Eo ID not valid` EINA errors, broken decoding, only a few thumbnails on a cold cache); pool mutexes are robust (a GC-cancelled worker can no longer wedge the pool); advisory file lock is non-blocking with cancel cleanup; emotion make-handshake is bounded and shutdown-aware — **quitting no longer hangs** in the worker join; video makes overlap instead of serializing
- Fix garbage MIME-types from a libmagic result race (concurrent worker lookups overwrote the shared result buffer): affected files got a `.bin` icon and no thumbnail
- New command-line options `--with-live-thumbnails` / `--without-live-thumbnails` (aliases: `--live-thumbnails`, `--enable-live-thumbnails`, `--no-live-thumbnails`, `--disable-live-thumbnails`) so `thumbnail-live` can be set persistently across starts (properties are runtime-only by design)

### 10.4 (2026-11-15)

- Thumbnailer worker pool (default: #CPUs, property `thumbnail-workers`): image thumbnails are generated in parallel; video thumbnails stay serialized (libemotion has global engine state)
- Fix UI freeze at 100% CPU: gevas paint/render/expose feedback loop broken (`in_paint` guard)
- Fix crashes (SIGABRT/SIGSEGV) on video folders: libmagic and libextractor calls serialized (not thread-safe), emotion make re-entrancy guarded, imlib_mutex deadlock cycle broken (lock order video_make -> imlib, GUI uses trylock in live-thumbnail path)
- Fix stale plugin load errors at startup (e.g. id3.so without libid3): `make install` prunes stale plugins; missing-dependency loads logged at level 1
- New properties `thumbnail-live` / `thumbnail-live-sound`: turn off (or mute) live video playback in tooltips

### 10.3 (2026-11-15)

- Fix GLib-CRITICAL "Source ID was not found" on notebook page-switch (stale idle handle in `gevas_view_redraw` when the widget is unmapped)
- Icon-view: per-model-event full-canvas damage rects replaced by one coalesced queued redraw per main-loop iteration (large folders)

### 10.2 (2026-11-15)

- Tree-view: right-click on the empty area now opens the root menu (view-switch entries included), like icon and browser views
- View-switch from the right-click menu no longer runs inside the menu's nested main loop (deferred until the plain main loop), fixing intermittent UI freezes
- Thumbnailer plugin init is deferred until the main loop is running — fixes intermittent `Eo ID not valid` errors and a startup SIGSEGV on a cold plugin cache

### 10.1 (2026-11-15)

- Real **Cancel button** in the job progress window: aborts running copy/move/delete jobs and removes partial copies; closing the window still just hides it
- Credits dialog notes that this is a continuation of the original program, further developed with AI assistance
- Icon lookup by file name is O(1) (hash table) instead of a linear scan per file event
- Duplicate thumbnail requests are deduplicated instead of being rendered twice
- `events_pending()` fallback for minimal builds no longer emits a `#warning`

### 10.0 (2026-11-15)

- Version 10.0
- Build fixes: compiles with libpng but without Imlib2, `-Werror=format-security` error in the GTK dialogs fixed, `size_t` format warning fixed
- `configure.sh` options: `--with-<feature>` / `--without-<feature>` for all optional dependencies, `--without-gtk2`/`--without-gtk3` (headless build), `--prefix=DIR`, `--help`
