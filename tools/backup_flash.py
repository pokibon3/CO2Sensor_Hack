#!/usr/bin/env python3
"""Back up the AT32F415 internal flash and the W25Q32 over SWD.

  backup_flash.py mcu      [-o DIR]            internal flash 128 KB + user system data
  backup_flash.py spiflash [-o DIR]            W25Q32 4 MB (sflash firmware must be running)
  backup_flash.py all      [-o DIR] [--no-restore]

`all` backs up the MCU first, then flashes the sflash firmware
(software/invaders, env sflash) to read the W25Q32, and finally writes the
MCU backup back. The MCU flash is overwritten only after two reads of it
have matched. --no-restore leaves the sflash firmware on the MCU.

The default DIR is backups/YYYYmmdd-HHMMSS in the repository root. Files:
factory_firmware.bin, user_system_data.bin, w25q32_factory.bin, SHA256SUMS.
Power the board with S2. Do not power it from the probe.
"""
import argparse, datetime, hashlib, os, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
INVADERS = os.path.join(ROOT, 'software', 'invaders')
sys.path.insert(0, os.path.join(INVADERS, 'tools'))
import sflash  # noqa: E402

MCU_FLASH, MCU_FLASH_SIZE = 0x08000000, 0x20000
USD, USD_SIZE = 0x1FFFF800, 48
MCU_FILE, USD_FILE, SPI_FILE = 'factory_firmware.bin', 'user_system_data.bin', 'w25q32_factory.bin'

# Pause WDT/WWDT while halted (the factory firmware starts the WDT), then halt.
HALT = 'mww 0xE0042004 0x300; halt; mww 0xE0042004 0x300'
# Interrupts must be off while the flash algorithm runs (see software/invaders/README.md).
PREP_FLASH = ('cortex_m maskisr on; mww 0xE000E010 0; '
              'mww 0xE000E180 0xFFFFFFFF; mww 0xE000E280 0xFFFFFFFF')

# Hashes of the dump in analysis/factory_dump/.
KNOWN = {
    MCU_FILE: '0ffef943142116f185524f7dfce2d7e23e30a4551adc9b970cb94a84dec8e230',
    USD_FILE: 'b97158159bbb27e85c5204dde4119b42b273ee2624297c5b00c536ae36c8ce2e',
    SPI_FILE: '5b130150a18e5cd18b215a98c48b8bff9775ed504cff99222cab1966eae7d705',
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


CHUNK, RETRIES = 4096, 5


def dump_once(o, addr, length):
    with tempfile.NamedTemporaryFile(delete=False) as f:
        path = f.name
    try:
        o.cmd(f'dump_image {path} {addr:#x} {length}')
        return open(path, 'rb').read()
    finally:
        os.unlink(path)


def dump(o, addr, length):
    """Read in CHUNK pieces, retrying each one; SWD reads fail now and then."""
    out = bytearray()
    while len(out) < length:
        a, n = addr + len(out), min(CHUNK, length - len(out))
        for _ in range(RETRIES):
            data = dump_once(o, a, n)
            if len(data) == n:
                break
        else:
            raise SystemExit(f'cannot read {n} bytes at {a:#x}; check the SWD wiring')
        out += data
        if length > CHUNK:
            sys.stderr.write(f'\r{addr:#x}: {len(out) // 1024}/{length // 1024} KB ')
    if length > CHUNK:
        sys.stderr.write('\n')
    return bytes(out)


def save(outdir, name, data):
    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, name), 'wb').write(data)
    h = sha256(data)
    with open(os.path.join(outdir, 'SHA256SUMS'), 'a') as f:
        f.write(f'{h}  {name}\n')
    note = '' if name not in KNOWN else ('  (matches analysis/factory_dump)' if h == KNOWN[name]
                                         else '  (differs from analysis/factory_dump)')
    print(f'{name}: {len(data)} bytes, SHA256 {h}{note}')


def backup_mcu(outdir):
    o = sflash.OpenOCD(speed=1000)
    try:
        o.cmd(HALT)
        reads = [dump(o, MCU_FLASH, MCU_FLASH_SIZE) for _ in range(2)]
        usd = [dump(o, USD, USD_SIZE) for _ in range(2)]
        o.cmd('resume')
    finally:
        o.close()
    if reads[0] != reads[1] or usd[0] != usd[1]:
        raise SystemExit('MCU flash reads differ; check the SWD wiring and try again')
    if reads[0] == bytes(MCU_FLASH_SIZE):
        raise SystemExit('MCU flash reads as all zero; flash access protection may be enabled')
    save(outdir, MCU_FILE, reads[0])
    save(outdir, USD_FILE, usd[0])
    return reads[0]


def backup_spiflash(outdir):
    t = sflash.Tool()
    try:
        t.run(sflash.CMD_ID)
        jedec = t.o.rd(t.m + 20)
        print(f'JEDEC ID {jedec:06X}')
        if jedec != 0xEF4016:
            raise SystemExit('unexpected JEDEC ID (W25Q32 is EF4016)')
        out = bytearray()
        t0 = sflash.time.time()
        while len(out) < sflash.FLASH_SIZE:
            n = min(sflash.BUF, sflash.FLASH_SIZE - len(out))
            out += t.read(len(out), n)
            sflash.progress(len(out), sflash.FLASH_SIZE, t0)
        sys.stderr.write('\n')
    finally:
        t.o.close()
    save(outdir, SPI_FILE, bytes(out))


def openocd(commands):
    subprocess.run([sflash.openocd_bin(), '-s', sflash.OCD + '/scripts',
                    '-f', 'interface/cmsis-dap.cfg', '-f', 'target/at32f415xx.cfg',
                    '-c', 'adapter speed 1000', '-c', f'init; {commands}; exit'], check=True)


def write_mcu(image, kind):
    input('Hold S2 and press Enter (the MCU is reset after writing)... ')
    openocd(f'{HALT}; {PREP_FLASH}; flash write_image erase {image} {kind}; '
            f'verify_image {image} {kind}; cortex_m maskisr auto; reset run')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('target', choices=['mcu', 'spiflash', 'all'])
    ap.add_argument('-o', '--outdir',
                    default=os.path.join(ROOT, 'backups', datetime.datetime.now().strftime('%Y%m%d-%H%M%S')))
    ap.add_argument('--no-restore', action='store_true', help='all: keep the sflash firmware on the MCU')
    a = ap.parse_args()

    if os.path.exists(os.path.join(a.outdir, 'SHA256SUMS')):
        raise SystemExit(f'{a.outdir} already contains a backup')

    if a.target in ('mcu', 'all'):
        backup_mcu(a.outdir)
    if a.target == 'all':
        if not os.path.exists(sflash.ELF):
            subprocess.run(['pio', 'run', '-e', 'sflash'], cwd=INVADERS, check=True)
        print('Writing the sflash firmware to the MCU')
        write_mcu(os.path.abspath(sflash.ELF), '')
    if a.target in ('spiflash', 'all'):
        backup_spiflash(a.outdir)
    if a.target == 'all' and not a.no_restore:
        print('Writing the MCU backup back')
        write_mcu(os.path.join(os.path.abspath(a.outdir), MCU_FILE), f'{MCU_FLASH:#x} bin')
    print(f'Saved to {a.outdir}')


if __name__ == '__main__':
    main()
