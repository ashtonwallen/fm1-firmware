#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

cc=${CC:-cc}
python=${PYTHON:-python3}
build=app/build
out=$build/verification
package=${1:-}
mkdir -p "$out"

sh build.sh --host
for scenario in tests/*.sw; do
    name=$(basename "$scenario" .sw)
    mkdir -p "$out/$name"
    "$build/host/shortwave_host" "$scenario" "$out/$name"
done

"$cc" -std=c99 -Wall -Wextra -Werror -o "$out/flash_ok_test" base/tests/flash_ok_test.c
"$out/flash_ok_test"

if [ -n "$package" ]; then
    export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
    "$cc" -w -o "$out/ota_test" base/tests/ota_test.c
    "$out/ota_test" "$package"

    # Build a distinct, synthetic old app for the loader's flash simulation.
    printf 'Old test app\n' > "$out/old_app.bin"
    "$python" base/tools/fm1pkg_make.py "$out/old_app.bin" "$build/loader/ota.bin" "$out/old.fwsc"
    "$cc" -w -o "$out/ldr_test" base/tests/ldr_test.c
    "$out/ldr_test" "$out/old.fwsc" "$package"
    sha256sum "$package"
fi
