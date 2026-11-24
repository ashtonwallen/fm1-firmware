#!/bin/sh
# one or more scenarios in the host simulator: tools/scen.sh NAME... (after ./build.sh --host)
cd "$(dirname "$0")/.."
for s in "$@"; do
    mkdir -p app/build/scenarios/$s
    echo "== $s"
    app/build/host/garden_host tests/scenarios/$s.amb app/build/scenarios/$s 2>&1 | grep -v "^  ok"
done
