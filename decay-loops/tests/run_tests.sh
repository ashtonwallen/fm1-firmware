#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal (AMB-1)
# Copyright (C) 2026 ashtonwallen (DECAY LOOPS)
# DECAY LOOPS' tests: the engine (tests/host/dl_test.c), the whole app in fm1-base's simulator (tests/scenarios/*.amb:
# sound from power-on, every preset, the controls, saving, another firmware's data, a long run's peaks, the screen
# reader), and the update path against the package (./build.sh first). Screenshots and audio in app/build/scenarios/.
# fm1-base's own tests: base/tests/run_tests.sh.
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
run engine sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/dl_test tests/host/dl_test.c app/dsp/decay.c -lm && $OUT/dl_test"
if [ -f $B/decayloops.fwsc ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test base/tests/ota_test.c && $OUT/ota_test $B/decayloops.fwsc"
    run update-loader sh -c "head -c 100000 $B/decayloops.bin > $OUT/old_app.bin && \
        $PY base/tools/fm1pkg_make.py $OUT/old_app.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $B/decayloops.fwsc"
else
    echo "  skip update path (no $B/decayloops.fwsc: ./build.sh)"
fi
run host-build sh build.sh --host
for s in tests/scenarios/*.amb; do
    n=$(basename "$s" .amb)
    mkdir -p "$B/scenarios/$n"
    run "scenario-$n" $B/host/decayloops_host "$s" "$B/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
