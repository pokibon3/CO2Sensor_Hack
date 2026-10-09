#!/usr/bin/env python3
"""Run the reference Python model with the same input script as host_test.c.
   ref_run.py EMU8080_DIR ROM FRAMES TRACE_LINES trace.txt ram.bin"""
import sys
sys.path.insert(0, sys.argv[1])
import emu8080

def input_script(f):
    v = 0x08
    if 10 <= f <= 12: v |= 0x04
    if (f // 7) % 3 == 0: v |= 0x10
    v |= 0x20 if (f // 90) % 2 == 0 else 0x40
    if (f // 1500) % 2 == 1 and f % 600 < 20: v |= 0x04
    return v

rom, frames, limit = sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
m = emu8080.Machine(open(rom, 'rb').read())

class Limited:
    def __init__(self, f, n): self.f, self.n = f, n
    def write(self, s):
        if self.n > 0:
            self.n -= 1
            self.f.write(s)
m.trace = Limited(open(sys.argv[5], 'w'), limit)
for f in range(1, frames + 1):
    m.in1 = input_script(f)
    m.run_frame()
m.trace.f.close()
open(sys.argv[6], 'wb').write(bytes(m.mem[0x2000:0x4000]))
print('pc=%04X' % m.cpu.pc)
