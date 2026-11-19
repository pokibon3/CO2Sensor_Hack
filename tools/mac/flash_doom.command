#!/bin/sh
# Wrapper for tools/flash_doom.py; runs from any directory. Options are passed through.
exec python3 "$(dirname "$0")/../flash_doom.py" "$@"
