# Debian Trixie test container

Smoke-test evidence-remaster on Debian Trixie (the base of Elive) without
touching the host system. Requires Docker on the host:

```sh
sudo pacman -S docker            # CachyOS/Arch
sudo systemctl enable --now docker
sudo usermod -aG docker $USER    # re-login afterwards
```

Then:

```sh
./docker/test-trixie.sh              # build image + run smoke tests
./docker/test-trixie.sh --no-cache   # fresh build (after pulling new commits)
./docker/test-trixie.sh --shell      # interactive shell inside the container
```

What the smoke test (`smoke-test.sh`) does inside the container:

1. Headless build: `./configure.sh --without-gtk3 --without-gtk2 && make`
   (this is the configuration from bug3.md that failed to link)
2. GTK2 build: `./configure.sh --without-gtk3 --with-gtk2 && make`
3. `make install` to `/usr/local`
4. `evidence --version`
5. 15 s GUI run under Xvfb — exit 124 (timeout) means it stayed alive,
   139/134 would mean SIGSEGV/SIGABRT

Notes:

- id3lib no longer exists in Trixie, so the mp3 metadata provider plugin
  is skipped (configure.sh treats it as optional).
- metallo/OtE themes are not installed by design (2005 binary formats).
