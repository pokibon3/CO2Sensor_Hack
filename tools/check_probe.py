#!/usr/bin/env python3
"""Check the debugger (CMSIS-DAP) and the SWD connection to the AT32F415.

  check_probe.py [--speed KHZ]

Runs `init; targets; exit` in OpenOCD and reports whether the probe, the
SWD DP (DPIDR 0x2ba01477) and the Cortex-M4 were found. Nothing is halted
or reset. Power the board with PWR first. While a custom firmware is
running (LT7680B and panel on), keep PWR held. See doc/debugger_connection.md.
"""
import argparse, os, re, subprocess, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'software', 'invaders', 'tools'))
import sflash  # noqa: E402

DPIDR = 0x2BA01477

# (pattern in the OpenOCD output, explanation), checked in order.
DIAGNOSES = [
    ('unable to find a matching CMSIS-DAP device',
     'The debugger is not found on USB. Replug it and try again.'),
    ('cannot read IDR',
     'The debugger is found, but the MCU does not answer. Turn the board on with PWR; '
     'if a custom firmware is running, keep PWR held. Otherwise check that GP2 (SWCLK) '
     'and GP3 (SWDIO) are not swapped and that GND is connected.'),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--speed', type=int, default=1000, help='adapter speed in kHz (default 1000)')
    a = ap.parse_args()

    r = subprocess.run([sflash.openocd_bin(), '-s', os.path.join(sflash.OCD, 'scripts'),
                        '-f', 'interface/cmsis-dap.cfg', '-f', 'target/at32f415xx.cfg',
                        '-c', f'adapter speed {a.speed}', '-c', 'init; targets; exit'],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    out = r.stdout

    probe = 'unable to find a matching CMSIS-DAP device' not in out
    dp = re.search(r'SWD DPIDR (0x[0-9a-fA-F]+)', out)
    cpu = re.search(r'Cortex-M4 r\d+p\d+ processor detected', out)

    print(f'Debugger (CMSIS-DAP) : {"found" if probe else "NOT found"}')
    print(f'SWD DPIDR            : {dp.group(1) if dp else "not read"}')
    print(f'CPU                  : {cpu.group(0) if cpu else "not detected"}')

    if dp and int(dp.group(1), 16) == DPIDR and cpu and r.returncode == 0:
        print('OK: the debugger is connected to the AT32F415.')
        return 0

    print('\nNG: connection failed.')
    for pat, why in DIAGNOSES:
        if pat in out:
            print(why)
            break
    else:
        if dp and int(dp.group(1), 16) != DPIDR:
            print(f'Unexpected DPIDR (the AT32F415 is {DPIDR:#010x}).')
    print('\n--- OpenOCD output ---')
    print(out.rstrip())
    return 1


if __name__ == '__main__':
    sys.exit(main())
