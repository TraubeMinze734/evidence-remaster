# evidence-remaster

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
