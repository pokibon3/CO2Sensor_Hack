"""Shared build-and-write steps for flash_doom.py and flash_invaders.py.

Builds with `pio run`, then resets and halts the MCU (PWR held keeps the
power on), stops the WDT, SysTick and interrupts, writes and verifies
firmware.elf, and starts it with `reset run` (see doc/flashing.md).

Both games latch the power only once PWR has been held for 1 s after the
reset, so PWR is kept held for STARTUP_S seconds, then g_diag is read over
SWD (without halting) to check that the game is running.
A failed write is retried up to WRITE_TRIES times in all.
"""
import argparse, os, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(0, os.path.join(ROOT, 'software', 'invaders', 'tools'))
import sflash  # noqa: E402

# Writing over a running game (PLL clock, peripherals on) ends with "timeout
# waiting for algorithm" after the erase; writing right after a reset works
# (2026-11-19). So reset and halt first. PWR is held, so the reset does not
# cut the power. The rest is the sequence in doc/flashing.md.
COMMANDS = ('init; reset halt; cortex_m maskisr on; mww 0xE0042004 0x300; mww 0xE000E010 0; '
            'mww 0xE000E180 0xFFFFFFFF; mww 0xE000E280 0xFFFFFFFF; '
            'flash write_image erase {elf}; verify_image {elf}; '
            'cortex_m maskisr auto; reset run; exit')

WRITE_TRIES = 3
STARTUP_S = 2.0
# Games the tools can build and write: id -> (name, directory under software/,
# g_diag fields printed besides the stage as (label, word index)).
GAMES = {
    'doom': ('DOOM', 'doomlike', [('frames', 1), ('ticks', 5)]),
    'invaders': ('ALIEN RAID', 'invaders', [('frames', 1), ('late frames', 5)]),
}
STAGES = {0: 'did not reach main (power-on wait)', 1: 'stuck in LT7680B init',
          2: 'stuck after LT7680B init', 3: 'running', 0xE1: 'LT7680B init failed'}


def openocd(commands):
    return subprocess.run([sflash.openocd_bin(), '-s', os.path.join(sflash.OCD, 'scripts'),
                           '-f', 'interface/cmsis-dap.cfg', '-f', 'target/at32f415xx.cfg',
                           '-c', 'adapter speed 1000', '-c', commands])


def diag_addr(elf):
    for line in subprocess.check_output([sflash.NM, elf], text=True).splitlines():
        if line.endswith(' g_diag'):
            return int(line.split()[0], 16)
    return None


def check_startup(elf, fields):
    """Read g_diag.stage and the given (name, word index) fields.
    Returns True when the game runs."""
    addr = diag_addr(elf)
    if addr is None:
        print('g_diag not found in the ELF; startup not checked')
        return True
    try:
        o = sflash.OpenOCD(speed=1000)
    except SystemExit as e:
        print(f'Could not read g_diag: {e}')
        return False
    try:
        stage = o.rd(addr)
        values = [(name, o.rd(addr + 4 * i)) for name, i in fields]
    except ValueError:
        print('Could not read g_diag (was PWR held?)')
        return False
    finally:
        o.close()
    extra = ''.join(f', {name} {v}' for name, v in values)
    print(f'g_diag: stage {stage:#x} ({STAGES.get(stage, "unknown")}){extra}')
    return stage == 3


def project_dir(game):
    return os.path.join(ROOT, 'software', GAMES[game][1])


def elf_path(game):
    return os.path.join(project_dir(game), '.pio', 'build', 'at32f415cbt7', 'firmware.elf')


def build(game):
    subprocess.run([sflash.pio_bin(), 'run', '-e', 'at32f415cbt7'], cwd=project_dir(game), check=True)


def main(game, doc):
    """Build and write GAMES[game]; doc is the calling script's usage text."""
    name, subdir, fields = GAMES[game]
    elf = elf_path(game)

    ap = argparse.ArgumentParser(description=doc, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--no-build', action='store_true', help='skip pio run')
    ap.add_argument('-y', '--yes', action='store_true', help='do not wait for Enter')
    a = ap.parse_args()

    if not a.no_build:
        build(game)
    if not os.path.exists(elf):
        raise SystemExit(f'{elf} not found; build it with pio run in software/{subdir}')

    if not a.yes:
        input('Hold PWR and press Enter (keep holding until the write finishes)... ')
    # Retry in case a write still fails; keep PWR held.
    for attempt in range(1, WRITE_TRIES + 1):
        r = openocd(COMMANDS.format(elf=elf.replace(os.sep, '/')))
        if r.returncode == 0:
            break
        if attempt < WRITE_TRIES:
            print(f'Write failed (attempt {attempt}). Retrying; keep holding PWR...')
    else:
        raise SystemExit('Write failed. Check the connection with tools/check_probe.py.')
    print(f'Starting {name}. Keep holding PWR for {STARTUP_S:.0f} more seconds...')
    time.sleep(STARTUP_S)
    ok = check_startup(elf, fields)
    print('Done. PWR can be released.' if ok else
          f'{name} did not start. Release PWR, then send the g_diag line above.')
    return 0 if ok else 1
