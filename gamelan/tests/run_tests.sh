#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal; GAMELAN: Copyright (C) 2026 ashtonwallen
# GAMELAN's tests: the instrument (tests/host/gam_test.c), the whole app in fm1-base's simulator
# (tests/scenarios/*.amb: sound, presets, controls, persistence, the screen reader, long-run peaks) and the update path
# (./build.sh first). Screenshots and audio in app/build/scenarios/. fm1-base's own tests: base/tests/run_tests.sh.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # the package's SDK files, as ./build.sh
B=app/build
OUT=$B/tests
FAIL=0
mkdir -p "$OUT" $B/scenarios
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
run gam sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/gam_test tests/host/gam_test.c app/dsp/gam.c -lm && $OUT/gam_test"
if [ -f $B/gamelan.fwsc ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test base/tests/ota_test.c && $OUT/ota_test $B/gamelan.fwsc"
    run update-loader sh -c "head -c 100000 $B/gamelan.bin > $OUT/old_app.bin && \
        $PY base/tools/fm1pkg_make.py $OUT/old_app.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $B/gamelan.fwsc"
else
    echo "  skip update path (no $B/gamelan.fwsc: ./build.sh)"
fi
run host-build sh build.sh --host
for s in tests/scenarios/*.amb; do
    n=$(basename "$s" .amb)
    mkdir -p "$B/scenarios/$n"
    run "scenario-$n" $B/host/gamelan_host "$s" "$B/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
