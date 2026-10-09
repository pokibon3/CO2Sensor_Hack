#!/usr/bin/env python3
"""Read/write the W25Q32 behind the LT7680B, using the sflash firmware
(pio run -e sflash) and OpenOCD's TCL port.

  sflash.py id
  sflash.py read  OUT.bin [ADDR] [LEN]       (default: whole 4 MB)
  sflash.py erase4k ADDR
  sflash.py write IN.bin ADDR                (erases 4 KB sectors first, then verifies)
"""
import os, platform, socket, subprocess, sys, tempfile, time

OCD = os.path.expanduser('~/.platformio/packages/tool-openocd-at32')
EXE = '.exe' if os.name == 'nt' else ''
ELF = os.path.join(os.path.dirname(__file__), '..', '.pio', 'build', 'sflash', 'firmware.elf')
NM = os.path.expanduser('~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm')
FLASH_SIZE = 0x400000
BUF = 16384
CMD_ID, CMD_READ, CMD_ERASE4K, CMD_ERASE64K, CMD_PROGRAM = 1, 2, 3, 4, 5


def openocd_bin():
    """OpenOCD binary for this OS/CPU from the bin-<os>_<arch> directories."""
    system = platform.system().lower()
    machine = platform.machine().lower()
    arch = {'arm64': 'aarch64', 'amd64': 'x86_64', 'x64': 'x86_64'}.get(machine, machine)
    if system == 'darwin':
        name = 'darwin_arm64' if arch == 'aarch64' else 'darwin_x86_64'
    elif system == 'windows':
        name = 'windows_amd64' if arch in ('x86_64', 'aarch64') else 'windows_x86'
    elif system == 'linux' and arch in ('x86_64', 'aarch64'):
        name = 'linux_' + arch
    elif system == 'linux' and arch.startswith('armv7'):
        name = 'linux_armv7l'
    else:
        raise SystemExit(f'no OpenOCD binary for {platform.system()} {platform.machine()}')
    path = os.path.join(OCD, 'bin-' + name, 'openocd' + EXE)
    if not os.path.exists(path):
        raise SystemExit(f'{path} not found; see tools/openocd_setup.md')
    return path


def mbox_addr():
    for line in subprocess.check_output([NM, ELF], text=True).splitlines():
        if line.endswith(' sf_mbox'):
            return int(line.split()[0], 16)
    raise SystemExit('sf_mbox not found in ' + ELF)


class OpenOCD:
    def __init__(self):
        self.proc = subprocess.Popen(
            [openocd_bin(), '-s', OCD + '/scripts', '-f', 'interface/cmsis-dap.cfg',
             '-f', 'target/at32f415xx.cfg', '-c', 'adapter speed 4000', '-c', 'tcl_port 6666',
             '-c', 'gdb_port disabled', '-c', 'telnet_port disabled', '-c', 'init'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                self.sock = socket.create_connection(('127.0.0.1', 6666))
                return
            except OSError:
                time.sleep(0.1)
        raise SystemExit('cannot connect to OpenOCD')

    def cmd(self, c):
        self.sock.sendall(c.encode() + b'\x1a')
        data = b''
        while not data.endswith(b'\x1a'):
            data += self.sock.recv(65536)
        return data[:-1].decode()

    def rd(self, addr):
        out = self.cmd(f'mem2array v 32 {addr} 1; return $v(0)')
        return int(out, 0)

    def close(self):
        self.cmd('shutdown')
        self.proc.wait()


class Tool:
    def __init__(self):
        self.o = OpenOCD()
        self.m = mbox_addr()
        if self.o.rd(self.m) != 0x544C4653:
            raise SystemExit('sflash firmware is not running (magic missing)')

    def run(self, cmd, addr=0, length=0, timeout=10.0):
        m = self.m
        self.o.cmd(f'mww {m + 8} {addr}; mww {m + 12} {length}; mww {m + 16} 0; mww {m + 4} {cmd}')
        t0 = time.time()
        while True:
            st = self.o.rd(m + 16)
            if st == 1:
                return
            if st == 2:
                raise SystemExit(f'command {cmd} failed at {addr:#x}')
            if time.time() - t0 > timeout:
                raise SystemExit(f'command {cmd} timed out at {addr:#x}')

    def read(self, addr, length):
        self.run(CMD_READ, addr, length)
        with tempfile.NamedTemporaryFile(delete=False) as f:
            path = f.name
        self.o.cmd(f'dump_image {path} {self.m + 28} {length}')
        data = open(path, 'rb').read()
        os.unlink(path)
        return data

    def program(self, addr, data):
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(data)
            path = f.name
        self.o.cmd(f'load_image {path} {self.m + 28} bin')
        os.unlink(path)
        self.run(CMD_PROGRAM, addr, len(data))


def progress(done, total, t0):
    rate = done / max(time.time() - t0, 1e-6) / 1024
    sys.stderr.write(f'\r{done * 100 // total:3d}%  {done // 1024} KB  {rate:.1f} KB/s ')
    sys.stderr.flush()


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    t = Tool()
    try:
        op = sys.argv[1]
        if op == 'id':
            t.run(CMD_ID)
            print(f'JEDEC ID {t.o.rd(t.m + 20):06X}')
        elif op == 'read':
            addr = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0
            length = int(sys.argv[4], 0) if len(sys.argv) > 4 else FLASH_SIZE - addr
            t0, out = time.time(), bytearray()
            while len(out) < length:
                n = min(BUF, length - len(out))
                out += t.read(addr + len(out), n)
                progress(len(out), length, t0)
            open(sys.argv[2], 'wb').write(out)
            print(f'\nread {length} bytes in {time.time() - t0:.0f} s')
        elif op == 'erase4k':
            t.run(CMD_ERASE4K, int(sys.argv[2], 0))
            print('erased')
        elif op == 'write':
            data = open(sys.argv[2], 'rb').read()
            addr = int(sys.argv[3], 0)
            if addr % 4096:
                raise SystemExit('ADDR must be 4 KB aligned')
            t0 = time.time()
            for a in range(addr, addr + len(data), 4096):
                t.run(CMD_ERASE4K, a)
            for off in range(0, len(data), BUF):
                t.program(addr + off, data[off:off + BUF])
                progress(off + len(data[off:off + BUF]), len(data), t0)
            back = bytearray()
            for off in range(0, len(data), BUF):
                back += t.read(addr + off, min(BUF, len(data) - off))
            print('\nverify', 'OK' if back == data else 'FAILED', f'({time.time() - t0:.0f} s)')
        else:
            raise SystemExit(__doc__)
    finally:
        t.o.close()


if __name__ == '__main__':
    main()
