#!/usr/bin/env python3
"""Build the DOOM game (software/doomlike) and write it to the AT32F415.

  flash_doom.py [--no-build] [-y]

Power the board with PWR and keep PWR held until the script says it can be
released: a reset drops the power latch, and a running custom firmware blocks
SWD memory access unless PWR is held. The steps are in flashfw.py and
doc/flashing.md.

  --no-build   write the existing .pio/build/at32f415cbt7/firmware.elf
  -y           do not wait for Enter before writing
"""
import sys
import flashfw

if __name__ == '__main__':
    sys.exit(flashfw.main('doom', __doc__))
