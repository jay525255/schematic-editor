#!/usr/bin/env bash
set -euo pipefail
export GDK_BACKEND=x11 GTK_USE_PORTAL=0
unset WAYLAND_DISPLAY
mkdir -p ci-artifacts/kicad
openbox > ci-artifacts/kicad/window-manager.log 2>&1 &
wm_pid=$!
trap 'kill "$wm_pid" 2>/dev/null || true' EXIT
/usr/bin/python3 tests/kicad_import.py --cli build-ci/schematic-cli --design examples/dynamic-and.ses --output ci-artifacts/kicad 2>&1 | tee ci-artifacts/kicad/test.log
