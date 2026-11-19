#!/usr/bin/env python3
"""Restore a backup made by backup_flash.py (factory firmware, W25Q32) over SWD.

  restore_flash.py mcu      [DIR] [-y]      write factory_firmware.bin to the internal flash
  restore_flash.py spiflash [DIR] [-y]      write w25q32_factory.bin (sflash firmware must be running)
  restore_flash.py all      [DIR] [-y]      both: sflash firmware -> W25Q32 -> internal flash

Without DIR, each file is taken from analysis/factory_dump/ or backups/*/,
preferring a file whose SHA256 matches the author's factory dump, otherwise
the newest one. If the directory has SHA256SUMS, the file is checked against
it before anything is written. The user system data is not written.

The W25Q32 is restored per 64 KB block: each block is read and only blocks
that differ are erased, programmed and read back. Reading the whole chip takes
about 7 minutes; keep PWR held throughout. `all` writes the sflash firmware
first (the internal flash is overwritten), then the W25Q32, and the internal
flash last. See doc/flash_backup.md.
"""
import argparse, glob, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backup_flash as bf  # noqa: E402
from backup_flash import sflash  # noqa: E402

BLOCK = 0x10000
RETRIES = 3
SIZES = {bf.MCU_FILE: bf.MCU_FLASH_SIZE, bf.SPI_FILE: sflash.FLASH_SIZE}


def candidates(name):
    dirs = [os.path.join(bf.ROOT, 'analysis', 'factory_dump')]
    dirs += sorted(glob.glob(os.path.join(bf.ROOT, 'backups', '*')), key=os.path.getmtime, reverse=True)
    return [os.path.join(d, name) for d in dirs if os.path.isfile(os.path.join(d, name))]


def find(name, directory):
    """Pick the backup file and check it. Returns (path, data)."""
    if directory:
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            raise SystemExit(f'{path} not found')
    else:
        paths = candidates(name)
        if not paths:
            raise SystemExit(f'no {name} in analysis/factory_dump/ or backups/; give DIR')
        known = [p for p in paths if bf.sha256(open(p, 'rb').read()) == bf.KNOWN[name]]
        path = (known or paths)[0]
    data = open(path, 'rb').read()
    if len(data) != SIZES[name]:
        raise SystemExit(f'{path}: {len(data)} bytes, expected {SIZES[name]}')
    h = bf.sha256(data)
    sums = os.path.join(os.path.dirname(path), 'SHA256SUMS')
    if os.path.isfile(sums):
        listed = {l.split()[1]: l.split()[0] for l in open(sums) if len(l.split()) == 2}
        if name in listed and listed[name] != h:
            raise SystemExit(f'{path} does not match its SHA256SUMS; the file is damaged')
    note = ('matches the factory dump in analysis/factory_dump' if h == bf.KNOWN[name]
            else 'DIFFERS from the factory dump (another firmware or another unit)')
    print(f'{os.path.relpath(path, bf.ROOT)}: SHA256 {h[:16]}... {note}')
    return path, data


def restore_block(t, addr, want):
    """Make one 64 KB block equal to want. Returns (written, tool)."""
    for _ in range(RETRIES):
        try:
            cur = bytearray()
            while len(cur) < BLOCK:
                chunk, t = bf.read_spi_chunk(t, addr + len(cur), sflash.BUF)
                cur += chunk
            if cur == want:
                return False, t
            t.run(sflash.CMD_ERASE64K, addr, timeout=10.0)
            if want != b'\xff' * BLOCK:
                for off in range(0, BLOCK, sflash.BUF):
                    t.program(addr + off, want[off:off + sflash.BUF])
            back = bytearray()
            while len(back) < BLOCK:
                chunk, t = bf.read_spi_chunk(t, addr + len(back), sflash.BUF)
                back += chunk
            if back == want:
                return True, t
        except (SystemExit, ValueError, OSError):
            if t is not None:
                bf.close_tool(t)
            t = None
    raise SystemExit(f'cannot restore the W25Q32 block at {addr:#x}; check the SWD wiring and PWR')


def restore_spiflash(data, ask):
    msg = 'Keep PWR held until the W25Q32 restore finishes (7 minutes or more)'
    if ask:
        input(msg + '. Hold PWR and press Enter... ')
    else:
        print(msg)
    t = bf.open_tool()
    written = 0
    try:
        t.run(sflash.CMD_ID)
        jedec = t.o.rd(t.m + 20)
        print(f'JEDEC ID {jedec:06X}')
        if jedec != 0xEF4016:
            raise SystemExit('unexpected JEDEC ID (W25Q32 is EF4016)')
        t0 = sflash.time.time()
        for addr in range(0, sflash.FLASH_SIZE, BLOCK):
            w, t = restore_block(t, addr, data[addr:addr + BLOCK])
            written += w
            sflash.progress(addr + BLOCK, sflash.FLASH_SIZE, t0)
        sys.stderr.write('\n')
    finally:
        if t is not None:
            bf.close_tool(t)
    print(f'W25Q32 restored: {written} of {sflash.FLASH_SIZE // BLOCK} blocks rewritten, all verified')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('target', choices=['mcu', 'spiflash', 'all'])
    ap.add_argument('dir', nargs='?', help='backup directory (default: see above)')
    ap.add_argument('-y', '--yes', action='store_true', help='do not ask for confirmation')
    a = ap.parse_args()

    mcu = find(bf.MCU_FILE, a.dir) if a.target in ('mcu', 'all') else None
    spi = find(bf.SPI_FILE, a.dir) if a.target in ('spiflash', 'all') else None
    if not a.yes and input('Write these to the board? [y/N] ').strip().lower() != 'y':
        raise SystemExit('Cancelled')

    if a.target == 'all':
        if not os.path.exists(sflash.ELF):
            bf.subprocess.run([bf.sflash.pio_bin(), 'run', '-e', 'sflash'], cwd=bf.INVADERS, check=True)
        print('Writing the sflash firmware to the MCU')
        bf.write_mcu(os.path.abspath(sflash.ELF), '')
    try:
        if spi:
            restore_spiflash(spi[1], ask=a.target == 'spiflash')
    finally:
        # In `all`, put the factory firmware back even if the W25Q32 failed.
        if mcu:
            print('Writing the internal flash')
            bf.write_mcu(os.path.abspath(mcu[0]), f'{bf.MCU_FLASH:#x} bin')
    print('Done. PWR can be released.')


if __name__ == '__main__':
    main()
