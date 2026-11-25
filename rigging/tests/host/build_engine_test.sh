#!/bin/sh
# builds the engine test (tests/host/rig_test.c) into $1 (default /tmp/rigging/rig_test)
set -e
cd "$(dirname "$0")/../.."
OUT="${1:-/tmp/rigging/rig_test}"
mkdir -p "$(dirname "$OUT")"
CC="${CC:-cc}"
F="-O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -DEN_HOST -Iapp/dsp -Ibase/include"
$CC $F -Wdouble-promotion -c -o "$OUT.o" app/dsp/rig.c
$CC $F -o "$OUT" tests/host/rig_test.c "$OUT.o" -lm
