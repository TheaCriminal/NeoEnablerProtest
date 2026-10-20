#!/bin/bash
# Builds build/neo-manager.elf: assembles the position-independent shim, embeds it and the icon,
# compiles the manager.
set -euo pipefail
cd "$(dirname "$0")"
B=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}/bin
NEO_BUILD_DIR=${NEO_BUILD_DIR:-build}
NEO_OUTPUT=${NEO_OUTPUT:-build/neo-manager.elf}
mkdir -p "$NEO_BUILD_DIR" "$(dirname "$NEO_OUTPUT")"
"$B/clang" --target=x86_64-unknown-none -c src/shim.S -o "$NEO_BUILD_DIR/shim.o"
"$B/ld.lld" -T src/shim.ld --no-relax "$NEO_BUILD_DIR/shim.o" -o "$NEO_BUILD_DIR/shim.out"
"$B/llvm-objcopy" -O binary -j .shim "$NEO_BUILD_DIR/shim.out" "$NEO_BUILD_DIR/shim.bin"
python3 tools/gen_shim.py "$B/llvm-nm" "$NEO_BUILD_DIR"
"$B/clang" --target=x86_64-unknown-none -c src/step_shim.S -o "$NEO_BUILD_DIR/step-shim.o"
"$B/ld.lld" -T src/step_shim.ld --no-relax "$NEO_BUILD_DIR/step-shim.o" -o "$NEO_BUILD_DIR/step-shim.out"
"$B/llvm-objcopy" -O binary -j .shim "$NEO_BUILD_DIR/step-shim.out" "$NEO_BUILD_DIR/step-shim.bin"
python3 tools/gen_step_shim.py "$B/llvm-nm" "$NEO_BUILD_DIR"
(cd assets && xxd -i -n icon0_png icon0.png) > "$NEO_BUILD_DIR/icon_data.h"
(cd assets && xxd -i -n panel_js panel.js) > "$NEO_BUILD_DIR/panel_data.h"
"$B/prospero-clang" -O2 -Wall -Wextra -Werror -I"$NEO_BUILD_DIR" -o "$NEO_OUTPUT" src/manager.c -lkernel_sys -lSceNet -lSceSsl -lSceHttp -lSceUserService -lSceSystemService
sha256sum "$NEO_OUTPUT"
