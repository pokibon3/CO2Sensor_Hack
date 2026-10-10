#!/usr/bin/env python3
"""Run the ArteryTek OpenOCD for this OS/CPU with its scripts directory.

  openocd.py [OPENOCD ARGS...]

Same as `openocd -s <scripts> ARGS...`. The binary is chosen by
sflash.openocd_bin(); see doc/openocd_setup.md.
"""
import os, subprocess, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'software', 'invaders', 'tools'))
import sflash  # noqa: E402

if __name__ == '__main__':
    sys.exit(subprocess.call([sflash.openocd_bin(), '-s', os.path.join(sflash.OCD, 'scripts')] + sys.argv[1:]))
