#!/bin/sh
# Assemble flash_stub.S and print it as 32-bit little-endian words for at32.js.
set -e
cd "$(dirname "$0")"
BIN=~/.platformio/packages/toolchain-gccarmnoneeabi/bin
$BIN/arm-none-eabi-as -mcpu=cortex-m4 -mthumb -o "${TMPDIR:-/tmp}/flash_stub.o" flash_stub.S
$BIN/arm-none-eabi-objcopy -O binary "${TMPDIR:-/tmp}/flash_stub.o" "${TMPDIR:-/tmp}/flash_stub.bin"
python3 - "${TMPDIR:-/tmp}/flash_stub.bin" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read()
b += b'\0' * (-len(b) % 4)
w = struct.unpack(f'<{len(b) // 4}I', b)
print(',\n'.join(', '.join(f'0x{x:08x}' for x in w[i:i + 6]) for i in range(0, len(w), 6)))
PY
