#!/bin/sh
# Smoke tests for evidence inside the Debian Trixie container.
# Runs the two build configurations from bug3.md plus install, version
# output and a 15-second GUI run under Xvfb.
set -e
cd /src/evidence

echo "== 1/5 headless build (--without-gtk3 --without-gtk2) =="
./configure.sh --without-gtk3 --without-gtk2 >/tmp/cfg-headless.log 2>&1
tail -n 3 /tmp/cfg-headless.log
make -j"$(nproc)" >/tmp/make-headless.log 2>&1 || { tail -n 20 /tmp/make-headless.log; exit 1; }
test -x ./evidence
echo "OK: headless binary linked"

echo "== 2/5 GTK2 build (--without-gtk3 --with-gtk2) =="
./configure.sh --without-gtk3 --with-gtk2 >/tmp/cfg-gtk2.log 2>&1
tail -n 3 /tmp/cfg-gtk2.log
make -j"$(nproc)" >/tmp/make-gtk2.log 2>&1 || { tail -n 20 /tmp/make-gtk2.log; exit 1; }
test -x ./evidence
echo "OK: GTK2 binary linked"

echo "== 3/5 make install =="
make install >/tmp/install.log 2>&1 || { tail -n 20 /tmp/install.log; exit 1; }
test -x /usr/local/bin/evidence
echo "OK: installed to /usr/local"

echo "== 4/5 version =="
/usr/local/bin/evidence --version 2>&1 | head -n 6 || true

echo "== 5/5 GUI smoke test under Xvfb (15s) =="
set +e
timeout 15 xvfb-run -a /usr/local/bin/evidence >/tmp/evidence-gui.log 2>&1
rc=$?
set -e
head -n 30 /tmp/evidence-gui.log
if [ "$rc" -eq 124 ]; then
  echo "OK: evidence ran the full 15s under Xvfb without crashing"
elif [ "$rc" -eq 0 ]; then
  echo "OK: evidence exited cleanly (rc=0)"
else
  echo "FAIL: evidence exited rc=$rc (139=SIGSEGV, 134=SIGABRT)"
  tail -n 30 /tmp/evidence-gui.log
  exit 1
fi

echo "ALL SMOKE TESTS PASSED"
