#!/bin/sh
# A few scenarios quickly, by name (the host build first): sh tests/run_some.sh first presets
cd "$(dirname "$0")/.."
B=app/build
for n in "$@"; do
    mkdir -p "$B/scenarios/$n"
    if $B/host/nightdrive_host "tests/scenarios/$n.amb" "$B/scenarios/$n" > "$B/scenarios/$n/log.txt" 2>&1; then
        echo "  ok   $n"
    else
        echo "  FAIL $n"
        grep -E "FAIL|line" "$B/scenarios/$n/log.txt" | head -20 | sed 's/^/       /'
    fi
done
