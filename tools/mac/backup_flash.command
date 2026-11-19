#!/bin/sh
# Wrapper for tools/backup_flash.py; runs from any directory. Options are passed through.
# Started without arguments (e.g. double-clicked in Finder), it asks what to back up.
if [ $# -eq 0 ]; then
  echo "1) mcu      internal flash 128 KB + user system data"
  echo "2) spiflash W25Q32 4 MB (sflash firmware must be running)"
  echo "3) all      both, then write the MCU backup back"
  printf "Select [1-3]: "
  read ans
  case "$ans" in
    1) set -- mcu ;;
    2) set -- spiflash ;;
    3) set -- all ;;
    *) echo "Cancelled"; exit 1 ;;
  esac
fi
exec python3 "$(dirname "$0")/../backup_flash.py" "$@"
