#!/usr/bin/env python3
"""Build the DOOM game (software/doomlike) and write it to the AT32F415.

  flash_doom.py [--no-build] [-y]

Builds with `pio run`, then halts the MCU, stops the WDT, SysTick and
interrupts, writes and verifies firmware.elf, and starts it with
`reset run` (see doc/flashing.md). Power the board with PWR and keep PWR
held until the write finishes: a reset drops the power latch, and a running
custom firmware blocks SWD memory access unless PWR is held.

  --no-build   write the existing .pio/build/at32f415cbt7/firmware.elf
  -y           do not wait for Enter before writing
"""
import argparse, os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
PROJECT = os.path.join(ROOT, 'software', 'doomlike')
ELF = os.path.join(PROJECT, '.pio', 'build', 'at32f415cbt7', 'firmware.elf')
sys.path.insert(0, os.path.join(ROOT, 'software', 'invaders', 'tools'))
import sflash  # noqa: E402

# Same sequence as doc/flashing.md.
COMMANDS = ('init; halt; cortex_m maskisr on; mww 0xE0042004 0x300; mww 0xE000E010 0; '
            'mww 0xE000E180 0xFFFFFFFF; mww 0xE000E280 0xFFFFFFFF; '
            'flash write_image erase {elf}; verify_image {elf}; '
            'cortex_m maskisr auto; reset run; exit')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--no-build', action='store_true', help='skip pio run')
    ap.add_argument('-y', '--yes', action='store_true', help='do not wait for Enter')
    a = ap.parse_args()

    if not a.no_build:
        subprocess.run(['pio', 'run'], cwd=PROJECT, check=True)
    if not os.path.exists(ELF):
        raise SystemExit(f'{ELF} not found; build it with pio run in software/doomlike')

    if not a.yes:
        input('Hold PWR and press Enter (keep holding until the write finishes)... ')
    r = subprocess.run([sflash.openocd_bin(), '-s', os.path.join(sflash.OCD, 'scripts'),
                        '-f', 'interface/cmsis-dap.cfg', '-f', 'target/at32f415xx.cfg',
                        '-c', 'adapter speed 1000', '-c', COMMANDS.format(elf=ELF.replace(os.sep, '/'))])
    if r.returncode != 0:
        raise SystemExit('Write failed. Check the connection with tools/check_probe.py.')
    print('Done. PWR can be released.')


if __name__ == '__main__':
    main()
