#!/usr/bin/env python3
"""Build the DOOM game (software/doomlike) and write it to the AT32F415.

  flash_doom.py [--no-build] [-y]

Builds with `pio run`, then resets and halts the MCU (PWR held keeps the
power on), stops the WDT, SysTick and interrupts, writes and verifies
firmware.elf, and starts it with `reset run` (see doc/flashing.md).
Power the board with PWR and keep PWR held until the write finishes: a reset
drops the power latch, and a running custom firmware blocks SWD memory access
unless PWR is held.

After the reset, DOOM latches the power only once PWR has been held for 1 s,
so this script keeps asking for PWR for STARTUP_S seconds, then reads g_diag
over SWD (without halting) to check that the game is running.
A failed write is retried up to WRITE_TRIES times in all.

  --no-build   write the existing .pio/build/at32f415cbt7/firmware.elf
  -y           do not wait for Enter before writing
"""
import argparse, os, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
PROJECT = os.path.join(ROOT, 'software', 'doomlike')
ELF = os.path.join(PROJECT, '.pio', 'build', 'at32f415cbt7', 'firmware.elf')
sys.path.insert(0, os.path.join(ROOT, 'software', 'invaders', 'tools'))
import sflash  # noqa: E402

# Writing over the running DOOM (PLL clock, peripherals on) ends with "timeout
# waiting for algorithm" after the erase; writing right after a reset works
# (2026-11-19). So reset and halt first. PWR is held, so the reset does not
# cut the power. The rest is the sequence in doc/flashing.md.
COMMANDS = ('init; reset halt; cortex_m maskisr on; mww 0xE0042004 0x300; mww 0xE000E010 0; '
            'mww 0xE000E180 0xFFFFFFFF; mww 0xE000E280 0xFFFFFFFF; '
            'flash write_image erase {elf}; verify_image {elf}; '
            'cortex_m maskisr auto; reset run; exit')


WRITE_TRIES = 3
STARTUP_S = 2.0
STAGES = {0: 'did not reach main (power-on wait)', 1: 'stuck in LT7680B init',
          2: 'stuck after LT7680B init', 3: 'running', 0xE1: 'LT7680B init failed'}


def openocd(commands):
    return subprocess.run([sflash.openocd_bin(), '-s', os.path.join(sflash.OCD, 'scripts'),
                           '-f', 'interface/cmsis-dap.cfg', '-f', 'target/at32f415xx.cfg',
                           '-c', 'adapter speed 1000', '-c', commands])


def diag_addr():
    for line in subprocess.check_output([sflash.NM, ELF], text=True).splitlines():
        if line.endswith(' g_diag'):
            return int(line.split()[0], 16)
    return None


def check_startup():
    """Read g_diag.stage, frames and ticks. Returns True when the game runs."""
    addr = diag_addr()
    if addr is None:
        print('g_diag not found in the ELF; startup not checked')
        return True
    try:
        o = sflash.OpenOCD(speed=1000)
    except SystemExit as e:
        print(f'Could not read g_diag: {e}')
        return False
    try:
        stage, frames, ticks = (o.rd(addr + 4 * i) for i in (0, 1, 5))
    except ValueError:
        print('Could not read g_diag (was PWR held?)')
        return False
    finally:
        o.close()
    print(f'g_diag: stage {stage:#x} ({STAGES.get(stage, "unknown")}), frames {frames}, ticks {ticks}')
    return stage == 3


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--no-build', action='store_true', help='skip pio run')
    ap.add_argument('-y', '--yes', action='store_true', help='do not wait for Enter')
    a = ap.parse_args()

    if not a.no_build:
        subprocess.run([sflash.pio_bin(), 'run'], cwd=PROJECT, check=True)
    if not os.path.exists(ELF):
        raise SystemExit(f'{ELF} not found; build it with pio run in software/doomlike')

    if not a.yes:
        input('Hold PWR and press Enter (keep holding until the write finishes)... ')
    # Retry in case a write still fails; keep PWR held.
    for attempt in range(1, WRITE_TRIES + 1):
        r = openocd(COMMANDS.format(elf=ELF.replace(os.sep, '/')))
        if r.returncode == 0:
            break
        if attempt < WRITE_TRIES:
            print(f'Write failed (attempt {attempt}). Retrying; keep holding PWR...')
    else:
        raise SystemExit('Write failed. Check the connection with tools/check_probe.py.')
    print(f'Starting DOOM. Keep holding PWR for {STARTUP_S:.0f} more seconds...')
    time.sleep(STARTUP_S)
    ok = check_startup()
    print('Done. PWR can be released.' if ok else
          'DOOM did not start. Release PWR, then send the g_diag line above.')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
