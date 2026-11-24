#!/bin/sh
# run some scenarios (names without .amb), printing only what is not "ok": a quick loop while working
#   sh tests/run_some.sh first storm
cd "$(dirname "$0")/.."
for s in "$@"; do
    mkdir -p "app/build/scenarios/$s"
    echo "== $s"
    app/build/host/tideclock_host "tests/scenarios/$s.amb" "app/build/scenarios/$s" 2>&1 | grep -v "^  ok" | tail -15
done
