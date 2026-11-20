#!/bin/sh
# Wrapper for tools/web_flasher.py; runs from any directory. Options are passed through.
# Builds the games, serves tools/web/ on http://localhost:8737 and opens it in Chrome/Edge.
exec python3 "$(dirname "$0")/../web_flasher.py" "$@"
