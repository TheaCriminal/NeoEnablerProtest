#!/usr/bin/env bash
# Native host tests only: never accesses a console or the network.
set -euo pipefail
cd "$(dirname "$0")/.."
shim_test_build="${1:-build/budget-shim-tests}"
mkdir -p "$shim_test_build"
sdk_bin="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}/bin"
if [ -x "$sdk_bin/clang" ]; then
  shim_assembler=("$sdk_bin/clang" --target=x86_64-unknown-none)
  shim_linker="$sdk_bin/ld.lld"
  shim_objcopy="$sdk_bin/llvm-objcopy"
  shim_nm="$sdk_bin/llvm-nm"
else
  shim_assembler=("${CC:-cc}")
  shim_linker=ld
  shim_objcopy=objcopy
  shim_nm=nm
fi
"${shim_assembler[@]}" -c src/step_shim.S -o "$shim_test_build/step-shim.o"
"$shim_linker" -T src/step_shim.ld --no-relax "$shim_test_build/step-shim.o" -o "$shim_test_build/step-shim.out"
"$shim_objcopy" -O binary -j .shim "$shim_test_build/step-shim.out" "$shim_test_build/step-shim.bin"
python3 tools/gen_step_shim.py "$shim_nm" "$shim_test_build"
python3 - "$shim_nm" "$shim_test_build" <<'PY'
import json, pathlib, re, subprocess, sys
nm, directory = sys.argv[1:]
p = pathlib.Path(directory)
symbols = {}
for line in subprocess.check_output([nm, str(p / 'step-shim.out')], text=True).splitlines():
    match = re.fullmatch(r'([0-9a-f]+)\s+\w\s+(\w+)', line.strip())
    if match:
        symbols[match[2]] = int(match[1], 16)
(p / 'step-symbols.json').write_text(json.dumps(symbols))
PY
cc -shared -fPIC -Wall -Wextra -Werror tests/budget_shim_abi.S -o "$shim_test_build/step-tests.so"
timeout --signal=KILL 25s python3 tests/test_budget_shim.py "$shim_test_build"
