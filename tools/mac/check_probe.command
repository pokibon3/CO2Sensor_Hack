#!/bin/sh
# Wrapper for tools/check_probe.py; runs from any directory. Options are passed through.
exec python3 "$(dirname "$0")/../check_probe.py" "$@"
