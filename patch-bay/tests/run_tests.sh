#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 ashtonwallen; built on AMB-1 by Charles Vestal
# PATCH BAY's tests: the instrument (tests/host/patch_test.c: presets, the auto-patcher's rules, levels over long
# runs), the whole app in fm1-base's simulator (tests/scenarios/*.amb: sound, presets, controls, saving, the screen
# reader), the browser build, and the update path against the package (./build.sh first). Screenshots and audio in
# app/build/scenarios/. fm1-base's own tests: base/tests/run_tests.sh.
#   tests/run_tests.sh            everything
#   tests/run_tests.sh bench      the render's cost per second of sound, each preset
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
B=app/build
OUT=$B/tests
FAIL=0
mkdir -p "$OUT" $B/scenarios
PT="$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/patch_test tests/host/patch_test.c app/dsp/patch.c -lm"
if [ "${1:-}" = bench ]; then
    $PT && exec $OUT/patch_test bench
fi
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run patch sh -c "$PT && $OUT/patch_test"
if [ -f $B/patchbay.fwsc ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test base/tests/ota_test.c && $OUT/ota_test $B/patchbay.fwsc"
    run update-loader sh -c "head -c 100000 $B/patchbay.bin > $OUT/old_app.bin && \
        $PY base/tools/fm1pkg_make.py $OUT/old_app.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $B/patchbay.fwsc"
else
    echo "  skip update path (no $B/patchbay.fwsc: ./build.sh)"
fi
run host-build sh build.sh --host
if command -v emcc >/dev/null 2>&1; then
    run emu sh -c "sh build.sh --web && node tests/host/emu_test.mjs $B/emu/app.wasm"
else
    echo "  skip emu (no emcc)"
fi
for s in tests/scenarios/*.amb; do
    n=$(basename "$s" .amb)
    mkdir -p "$B/scenarios/$n"
    run "scenario-$n" $B/host/patchbay_host "$s" "$B/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
