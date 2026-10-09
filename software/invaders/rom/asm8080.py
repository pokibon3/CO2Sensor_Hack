#!/usr/bin/env python3
"""Minimal two-pass Intel 8080 assembler.

Syntax (Intel style, case-insensitive mnemonics):
    label:  MVI  A, 12H        ; comment
    NAME    EQU  expr
            ORG  expr
            DB   1, 2, 'TEXT', expr
            DW   label, 1234H
            DS   count
Numbers: 123, 0FFH / 0xFF, 1010B, 'A'. '$' = current address.
Expressions are Python expressions over symbols (+ - * // & | ^ << >> ~, LOW(x), HIGH(x)).

Usage: asm8080.py src.asm -o out.bin [--hex out.hex] [--lst out.lst] [--size 8192]
"""
import argparse, re, sys

REG8 = {'B': 0, 'C': 1, 'D': 2, 'E': 3, 'H': 4, 'L': 5, 'M': 6, 'A': 7}
RP   = {'B': 0, 'D': 1, 'H': 2, 'SP': 3}
RPS  = {'B': 0, 'D': 1, 'H': 2, 'PSW': 3}

IMPLIED = {'NOP': 0x00, 'RLC': 0x07, 'RRC': 0x0F, 'RAL': 0x17, 'RAR': 0x1F, 'DAA': 0x27,
           'CMA': 0x2F, 'STC': 0x37, 'CMC': 0x3F, 'HLT': 0x76, 'RET': 0xC9, 'XCHG': 0xEB,
           'XTHL': 0xE3, 'PCHL': 0xE9, 'SPHL': 0xF9, 'DI': 0xF3, 'EI': 0xFB,
           'RNZ': 0xC0, 'RZ': 0xC8, 'RNC': 0xD0, 'RC': 0xD8, 'RPO': 0xE0, 'RPE': 0xE8,
           'RP': 0xF0, 'RM': 0xF8}
ADDR16 = {'JMP': 0xC3, 'JNZ': 0xC2, 'JZ': 0xCA, 'JNC': 0xD2, 'JC': 0xDA, 'JPO': 0xE2,
          'JPE': 0xEA, 'JP': 0xF2, 'JM': 0xFA, 'CALL': 0xCD, 'CNZ': 0xC4, 'CZ': 0xCC,
          'CNC': 0xD4, 'CC': 0xDC, 'CPO': 0xE4, 'CPE': 0xEC, 'CP': 0xF4, 'CM': 0xFC,
          'SHLD': 0x22, 'LHLD': 0x2A, 'STA': 0x32, 'LDA': 0x3A}
ALU_R = {'ADD': 0x80, 'ADC': 0x88, 'SUB': 0x90, 'SBB': 0x98, 'ANA': 0xA0, 'XRA': 0xA8,
         'ORA': 0xB0, 'CMP': 0xB8}
ALU_I = {'ADI': 0xC6, 'ACI': 0xCE, 'SUI': 0xD6, 'SBI': 0xDE, 'ANI': 0xE6, 'XRI': 0xEE,
         'ORI': 0xF6, 'CPI': 0xFE, 'IN': 0xDB, 'OUT': 0xD3}


class AsmError(Exception):
    pass


def split_operands(s):
    """Split on commas not inside quotes."""
    out, cur, q = [], '', None
    for ch in s:
        if q:
            cur += ch
            if ch == q:
                q = None
        elif ch in "'\"":
            q = ch
            cur += ch
        elif ch == ',':
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def strip_comment(line):
    q = None
    for i, ch in enumerate(line):
        if q:
            if ch == q:
                q = None
        elif ch in "'\"":
            q = ch
        elif ch == ';':
            return line[:i]
    return line


class Assembler:
    def __init__(self):
        self.sym = {}
        self.pc = 0
        self.out = {}
        self.final = False
        self.lst = []

    def num(self, tok):
        t = tok.upper()
        if re.fullmatch(r'[0-9][0-9A-F]*H', t):
            return str(int(t[:-1], 16))
        if re.fullmatch(r'[01]+B', t):
            return str(int(t[:-1], 2))
        return None

    def eval(self, expr):
        e = expr.strip()
        # character literals
        e = re.sub(r"'(.)'", lambda m: str(ord(m.group(1))), e)

        def tok(m):
            t = m.group(0)
            n = self.num(t)
            if n is not None:
                return n
            if re.fullmatch(r'0[xX][0-9a-fA-F]+|[0-9]+', t):
                return str(int(t, 0) if t.lower().startswith('0x') else int(t))
            u = t.upper()
            if u in ('LOW', 'HIGH'):
                return u
            if u in self.sym:
                return str(self.sym[u])
            if self.final:
                raise AsmError(f'undefined symbol {t}')
            return '0'
        e = re.sub(r'\$', str(self.pc), e)
        e = re.sub(r'\b[0-9][0-9A-Fa-fxX]*[HhBb]?\b|\b[A-Za-z_.][A-Za-z0-9_.]*\b', tok, e)
        try:
            v = eval(e, {'__builtins__': {}}, {'LOW': lambda x: x & 0xFF, 'HIGH': lambda x: (x >> 8) & 0xFF})
        except Exception as ex:
            raise AsmError(f'bad expression "{expr}": {ex}')
        return int(v)

    def emit(self, *bs):
        for b in bs:
            self.out[self.pc] = b & 0xFF
            self.pc += 1

    def reg8(self, s):
        s = s.upper()
        if s not in REG8:
            raise AsmError(f'bad register {s}')
        return REG8[s]

    def line(self, raw):
        text = strip_comment(raw).rstrip()
        if not text.strip():
            return
        m = re.match(r'^\s*([A-Za-z_.][A-Za-z0-9_.]*):', text)
        if m:
            self.define(m.group(1), self.pc)
            text = text[m.end():]
        parts = text.strip().split(None, 1)
        if not parts:
            return
        # NAME EQU expr  /  NAME SET expr
        if len(parts) == 2 and re.match(r'^(EQU|SET|=)\b', parts[1].upper()):
            name = parts[0]
            expr = parts[1].split(None, 1)[1] if parts[1][0] != '=' else parts[1][1:]
            self.define(name, self.eval(expr), redefine=True)
            return
        op = parts[0].upper()
        args = split_operands(parts[1]) if len(parts) > 1 else []
        start = self.pc
        self.instr(op, args)
        if self.final:
            data = [self.out[a] for a in range(start, self.pc) if a in self.out]
            self.lst.append((start, data, raw.rstrip('\n')))

    def define(self, name, val, redefine=False):
        n = name.upper()
        if not self.final and n in self.sym and not redefine and self.sym[n] != val:
            raise AsmError(f'duplicate symbol {name}')
        self.sym[n] = val

    def instr(self, op, a):
        if op == 'ORG':
            self.pc = self.eval(a[0])
        elif op == 'DB':
            for x in a:
                if len(x) >= 2 and x[0] == x[-1] and x[0] in "'\"" and len(x) > 3:
                    self.emit(*[ord(c) for c in x[1:-1]])
                else:
                    self.emit(self.eval(x))
        elif op == 'DW':
            for x in a:
                v = self.eval(x)
                self.emit(v & 0xFF, v >> 8)
        elif op == 'DS':
            self.pc += self.eval(a[0])
        elif op == 'END':
            pass
        elif op in IMPLIED:
            self.emit(IMPLIED[op])
        elif op in ADDR16:
            v = self.eval(a[0])
            self.emit(ADDR16[op], v & 0xFF, v >> 8)
        elif op in ALU_R:
            self.emit(ALU_R[op] | self.reg8(a[0]))
        elif op in ALU_I:
            self.emit(ALU_I[op], self.eval(a[0]))
        elif op == 'MOV':
            d, s = self.reg8(a[0]), self.reg8(a[1])
            if d == 6 and s == 6:
                raise AsmError('MOV M,M is HLT')
            self.emit(0x40 | d << 3 | s)
        elif op == 'MVI':
            self.emit(0x06 | self.reg8(a[0]) << 3, self.eval(a[1]))
        elif op == 'INR':
            self.emit(0x04 | self.reg8(a[0]) << 3)
        elif op == 'DCR':
            self.emit(0x05 | self.reg8(a[0]) << 3)
        elif op in ('LXI', 'DAD', 'INX', 'DCX'):
            rp = RP[a[0].upper()]
            base = {'LXI': 0x01, 'DAD': 0x09, 'INX': 0x03, 'DCX': 0x0B}[op]
            self.emit(base | rp << 4)
            if op == 'LXI':
                v = self.eval(a[1])
                self.emit(v & 0xFF, v >> 8)
        elif op in ('PUSH', 'POP'):
            self.emit((0xC5 if op == 'PUSH' else 0xC1) | RPS[a[0].upper()] << 4)
        elif op in ('STAX', 'LDAX'):
            rp = a[0].upper()
            if rp not in ('B', 'D'):
                raise AsmError(f'{op} needs B or D')
            self.emit((0x02 if op == 'STAX' else 0x0A) | RP[rp] << 4)
        elif op == 'RST':
            self.emit(0xC7 | (self.eval(a[0]) & 7) << 3)
        else:
            raise AsmError(f'unknown mnemonic {op}')

    def assemble(self, lines):
        for final in (False, True):
            self.final = final
            self.pc = 0
            self.out = {}
            self.lst = []
            for n, l in enumerate(lines, 1):
                try:
                    self.line(l)
                except AsmError as e:
                    raise AsmError(f'line {n}: {e}\n    {l.rstrip()}')


def read_source(path):
    """Read a source file, expanding INCLUDE lines (relative to the including file)."""
    import os
    out = []
    for l in open(path):
        m = re.match(r'^\s*INCLUDE\s+(\S+)', strip_comment(l), re.I)
        if m:
            out += read_source(os.path.join(os.path.dirname(path), m.group(1).strip('"\'')))
        else:
            out.append(l)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--hex')
    ap.add_argument('--lst')
    ap.add_argument('--sym')
    ap.add_argument('--size', type=int, default=8192)
    o = ap.parse_args()
    asm = Assembler()
    try:
        asm.assemble(read_source(o.src))
    except AsmError as e:
        print(f'{o.src}: {e}', file=sys.stderr)
        sys.exit(1)
    top = max(asm.out) + 1 if asm.out else 0
    if top > o.size:
        print(f'program too large: {top} > {o.size}', file=sys.stderr)
        sys.exit(1)
    img = bytearray(o.size)
    for a, b in asm.out.items():
        img[a] = b
    open(o.out, 'wb').write(img)
    if o.hex:
        open(o.hex, 'w').write(''.join(f'{b:02X}\n' for b in img))
    if o.lst:
        with open(o.lst, 'w') as f:
            for a, data, src in asm.lst:
                f.write(f'{a:04X}  {" ".join(f"{b:02X}" for b in data[:4]):<12}{src}\n')
    if o.sym:
        with open(o.sym, 'w') as f:
            for k, v in sorted(asm.sym.items(), key=lambda kv: kv[1]):
                f.write(f'{v:04X} {k}\n')
    print(f'{o.src}: {top} bytes ({top * 100 // o.size}% of {o.size})')


if __name__ == '__main__':
    main()
