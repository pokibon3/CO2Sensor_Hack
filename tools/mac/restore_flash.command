#!/bin/sh
# Wrapper for tools/restore_flash.py; runs from any directory. Options are passed through.
# Started without arguments (e.g. double-clicked in Finder), it asks what to restore.
if [ $# -eq 0 ]; then
  echo "1) mcu      internal flash (factory_firmware.bin)"
  echo "2) spiflash W25Q32 (w25q32_factory.bin; sflash firmware must be running)"
  echo "3) all      both: sflash firmware -> W25Q32 -> internal flash"
  printf "Select [1-3]: "
  read ans
  case "$ans" in
    1) set -- mcu ;;
    2) set -- spiflash ;;
    3) set -- all ;;
    *) echo "Cancelled"; exit 1 ;;
  esac
fi
exec python3 "$(dirname "$0")/../restore_flash.py" "$@"
