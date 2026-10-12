#!/bin/bash
# Builds build/neo-enabler-manager.elf: assembles the position-independent shim, embeds it and the icon,
# compiles the manager.
set -e; cd "$(dirname "$0")"
B=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}/bin
mkdir -p build
$B/clang --target=x86_64-unknown-none -c src/shim.S -o build/shim.o
$B/ld.lld -T src/shim.ld --no-relax build/shim.o -o build/shim.out
$B/llvm-objcopy -O binary -j .shim build/shim.out build/shim.bin
python3 tools/gen_shim.py "$B/llvm-nm"
(cd assets && xxd -i -n icon0_png icon0.png) > build/icon_data.h
$B/prospero-clang -O2 -Wall -Wextra -Ibuild -o build/neo-enabler-manager.elf src/manager.c -lkernel_sys
sha256sum build/neo-enabler-manager.elf
