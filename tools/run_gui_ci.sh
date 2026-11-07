#!/usr/bin/env bash
set -euo pipefail
export GDK_BACKEND=x11 GTK_USE_PORTAL=0
unset WAYLAND_DISPLAY
mkdir -p ci-artifacts/gui
openbox > ci-artifacts/gui/window-manager.log 2>&1 &
wm_pid=$!
cleanup() {
  cp -a build-ci/gui-evidence ci-artifacts/gui/native 2>/dev/null || true
  kill "$wm_pid" 2>/dev/null || true
}
trap cleanup EXIT
set +e
ctest --test-dir build-ci -L gui -V --output-junit "$PWD/ci-artifacts/gui/junit.xml" 2>&1 | tee ci-artifacts/gui/tests.log
test_status=${PIPESTATUS[0]}
python3 tools/assert_junit.py ci-artifacts/gui/junit.xml gui_native
junit_status=$?
python3 tests/app_startup.py build-ci/SchematicEditor ci-artifacts/gui/executable

startup_status=$?
if ((test_status || junit_status || startup_status)); then exit 1; fi
