# evidence-remaster

Current version: **10.1** (remaster of the original evidence 0.9.8).

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
