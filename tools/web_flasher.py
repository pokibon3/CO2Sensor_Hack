#!/usr/bin/env python3
"""Start the web flasher (tools/web/) on http://localhost and open it.

  web_flasher.py [--no-build] [--port N] [--no-open]

Builds DOOM (software/doomlike) and ALIEN RAID (software/invaders) with
`pio run`, exports them to tools/web/firmware/ as .bin with manifest.json,
and serves the repository on 127.0.0.1. The page talks to the debugger
over WebUSB, so open it in Chrome or Edge. Stop with Ctrl+C.

The page can also check the connection and write the factory firmware back
(analysis/factory_dump/factory_firmware.bin or a backup you choose).

  --no-build   export the existing firmware.elf files (or keep the .bin
               files already in tools/web/firmware/ when there is none)
  --port N     port to serve on (default 8737)
  --no-open    do not open a browser
"""
import argparse, functools, hashlib, http.server, json, os, platform, subprocess, sys, webbrowser

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flashfw  # noqa: E402
from flashfw import sflash  # noqa: E402

ROOT = flashfw.ROOT
WEB = os.path.join(ROOT, 'tools', 'web')
OUT = os.path.join(WEB, 'firmware')
# Windows finds the .exe for a path without an extension, as for sflash.NM.
NM = sflash.NM
OBJCOPY = os.path.join(os.path.dirname(NM), 'arm-none-eabi-objcopy')
FACTORY = 'analysis/factory_dump/factory_firmware.bin'
FACTORY_SHA256 = '0ffef943142116f185524f7dfce2d7e23e30a4551adc9b970cb94a84dec8e230'


def sha256(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def g_diag(elf):
    for line in subprocess.check_output([NM, elf], text=True).splitlines():
        if line.endswith(' g_diag'):
            return int(line.split()[0], 16)
    return None


def export(build):
    """Build and export each game. Returns the manifest entries."""
    os.makedirs(OUT, exist_ok=True)
    old = {}
    path = os.path.join(OUT, 'manifest.json')
    if os.path.exists(path):
        old = {e['id']: e for e in json.load(open(path))['firmware']}
    entries = []
    for game, (name, subdir, fields) in flashfw.GAMES.items():
        elf = flashfw.elf_path(game)
        if build:
            try:
                flashfw.build(game)
            except (SystemExit, subprocess.CalledProcessError) as e:
                print(f'{name}: build failed ({e}); using what is already there')
        binfile = os.path.join(OUT, game + '.bin')
        if os.path.exists(elf):
            subprocess.run([OBJCOPY, '-O', 'binary', elf, binfile], check=True)
            diag = g_diag(elf)
            built = os.path.getmtime(elf)
        elif game in old and os.path.exists(binfile):
            diag, built = old[game]['g_diag'], old[game]['built']
        else:
            print(f'{name}: no firmware.elf in software/{subdir}; skipped')
            continue
        entries.append({
            'id': game, 'name': name, 'file': f'firmware/{game}.bin', 'source': f'software/{subdir}',
            'size': os.path.getsize(binfile), 'sha256': sha256(binfile), 'built': built,
            'g_diag': diag, 'fields': fields,
        })
        print(f'{name}: {os.path.getsize(binfile)} bytes -> tools/web/firmware/{game}.bin')
    manifest = {'firmware': entries,
                'factory': {'file': '../../' + FACTORY, 'sha256': FACTORY_SHA256}}
    json.dump(manifest, open(path, 'w'), indent=1)
    return entries


class Handler(http.server.SimpleHTTPRequestHandler):
    # Windows can map .js to text/plain through the registry; modules need JS.
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      '.js': 'text/javascript', '.json': 'application/json',
                      '.bin': 'application/octet-stream'}

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def log_message(self, fmt, *args):
        pass


def open_browser(url):
    # Safari and Firefox have no WebUSB: prefer Chrome, then Edge.
    if platform.system() == 'Darwin':
        for app in ('Google Chrome', 'Microsoft Edge'):
            if subprocess.run(['open', '-a', app, url], stderr=subprocess.DEVNULL).returncode == 0:
                return
    webbrowser.open(url)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--no-build', action='store_true', help='skip pio run')
    ap.add_argument('--port', type=int, default=8737)
    ap.add_argument('--no-open', action='store_true', help='do not open a browser')
    a = ap.parse_args()

    export(not a.no_build)
    httpd = http.server.ThreadingHTTPServer(
        ('127.0.0.1', a.port), functools.partial(Handler, directory=ROOT))
    url = f'http://localhost:{a.port}/tools/web/'
    print(f'\nWeb flasher: {url}  (Chrome or Edge)\nStop with Ctrl+C.')
    if not a.no_open:
        open_browser(url)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
