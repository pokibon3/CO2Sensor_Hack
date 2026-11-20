// AT32F415 internal flash over SWD: the same steps as tools/flashfw.py and
// tools/restore_flash.py (doc/flashing.md), without OpenOCD.
import { DapError, sleep, CPUID } from './dap.js';

export const FLASH_BASE = 0x08000000;
export const AT32_DPIDR = 0x2BA01477;
const FLASH = 0x40022000;
const UNLOCK = FLASH + 0x04, STS = FLASH + 0x0C, CTRL = FLASH + 0x10, ADDR = FLASH + 0x14,
  USD = FLASH + 0x1C, EPPS = FLASH + 0x20;
const KEY1 = 0x45670123, KEY2 = 0xCDEF89AB;
const OBF = 0x01, PRGMERR = 0x04, EPPERR = 0x10, ODF = 0x20;
const FPRGM = 0x01, SECERS = 0x02, ERSTR = 0x40, OPLK = 0x80;
const F_SIZE = 0x1FFFF7E0;
const DBGMCU_CTRL = 0xE0042004, SYST_CSR = 0xE000E010, NVIC_ICER = 0xE000E180, NVIC_ICPR = 0xE000E280;

// tools/web/stub/flash_stub.S (./build_stub.sh)
const STUB = new Uint32Array([
  0x4e114b10, 0x27aaf64a, 0x60dc2434, 0x611c2401, 0xf851b17a, 0x1c654b04,
  0x6004d007, 0xf01568dd, 0xd1fb0f01, 0x0f14f015, 0x6037d107, 0x3a013004,
  0x2400e7ee, 0x2000611c, 0x2400be00, 0x4601611c, 0xbe004628, 0x40022000,
  0x40003000,
]);
const STUB_ADDR = 0x20000000;
const STACK_TOP = 0x20000400;
const BUF_ADDR = 0x20000400;
const BUF_WORDS = 0x1000; // 16 KB per call

const hex = (v, n = 8) => '0x' + (v >>> 0).toString(16).toUpperCase().padStart(n, '0');

// Read IDs without halting (tools/check_probe.py).
export async function identify(dap) {
  const dpidr = await dap.connect();
  const info = { dpidr };
  info.cpuid = await dap.read32(CPUID);
  info.flashKB = (await dap.read32(F_SIZE)) & 0xFFFF;
  return info;
}

export function describe(info) {
  const lines = [`SWD DPIDR: ${hex(info.dpidr)}${info.dpidr === AT32_DPIDR ? '（AT32F415）' : '（AT32F415は0x2BA01477）'}`];
  if (info.cpuid !== undefined) {
    const part = (info.cpuid >> 4) & 0xFFF;
    lines.push(`CPUID: ${hex(info.cpuid)}${part === 0xC24 ? `（Cortex-M4 r${(info.cpuid >> 20) & 0xF}p${info.cpuid & 0xF}）` : ''}`);
  }
  if (info.flashKB !== undefined) lines.push(`内蔵Flash: ${info.flashKB} KB`);
  return lines;
}

async function waitIdle(dap, ms) {
  const t0 = performance.now();
  for (;;) {
    const s = await dap.read32(STS);
    if (!(s & OBF)) return s;
    if (performance.now() - t0 > ms) throw new DapError('Flashの処理が終わらない（タイムアウト）');
  }
}

// Halt (with a reset), stop WDT/SysTick/interrupts, unlock the flash.
async function prepare(dap, log) {
  log('リセットしてCPUを停止');
  await dap.resetHalt();
  await dap.write32(DBGMCU_CTRL, 0x300); // stop WDT and WWDT while halted
  await dap.write32(SYST_CSR, 0);
  await dap.write32(NVIC_ICER, 0xFFFFFFFF);
  await dap.write32(NVIC_ICER + 4, 0xFFFFFFFF);
  await dap.write32(NVIC_ICPR, 0xFFFFFFFF);
  await dap.write32(NVIC_ICPR + 4, 0xFFFFFFFF);

  const usd = await dap.read32(USD);
  if (usd & 2) throw new DapError('内蔵Flashのアクセス保護（FAP）が有効なので書き込めない');
  const epps = await dap.read32(EPPS);
  if (epps !== 0xFFFFFFFF) throw new DapError(`消去/書き込み保護（EPPS ${hex(epps)}）が有効なセクタがある`);

  if ((await dap.read32(CTRL)) & OPLK) {
    await dap.write32(UNLOCK, KEY1);
    await dap.write32(UNLOCK, KEY2);
    if ((await dap.read32(CTRL)) & OPLK) throw new DapError('Flashのロックを解除できない');
  }
  const kb = (await dap.read32(F_SIZE)) & 0xFFFF;
  return { sector: kb >= 256 ? 2048 : 1024, size: kb * 1024 };
}

async function eraseSector(dap, addr) {
  await dap.write32(STS, ODF | EPPERR | PRGMERR);
  await dap.write32(CTRL, SECERS);
  await dap.write32(ADDR, addr);
  await dap.write32(CTRL, SECERS | ERSTR);
  const s = await waitIdle(dap, 2000);
  await dap.write32(CTRL, 0);
  if (s & (EPPERR | PRGMERR)) throw new DapError(`セクタ ${hex(addr)} を消去できない（STS ${hex(s, 2)}）`);
}

function toWords(bytes) {
  const n = Math.ceil(bytes.length / 4);
  const b = new Uint8Array(n * 4).fill(0xFF);
  b.set(bytes);
  return new Uint32Array(b.buffer);
}

// Write image to FLASH_BASE: erase the sectors it covers, program through the
// RAM stub, read back and compare, lock. progress(stage, done, total).
export async function writeFlash(dap, image, log, progress) {
  const { sector, size } = await prepare(dap, log);
  if (image.length > size) throw new DapError(`イメージ（${image.length} バイト）が内蔵Flash（${size} バイト）より大きい`);
  const words = toWords(image);
  const end = FLASH_BASE + words.length * 4;

  const nsec = Math.ceil((end - FLASH_BASE) / sector);
  log(`消去: ${nsec} セクタ（${sector} バイト単位）`);
  for (let i = 0; i < nsec; i++) {
    await eraseSector(dap, FLASH_BASE + i * sector);
    progress('消去', i + 1, nsec);
  }

  log(`書き込み: ${image.length} バイト`);
  await dap.writeWords(STUB_ADDR, STUB);
  for (let off = 0; off < words.length; off += BUF_WORDS) {
    const chunk = words.subarray(off, off + BUF_WORDS);
    await dap.writeWords(BUF_ADDR, chunk);
    const [r0, r1] = await dap.call(STUB_ADDR | 1, STACK_TOP, [FLASH_BASE + off * 4, BUF_ADDR, chunk.length], 5000);
    if (r0 !== 0) throw new DapError(`${hex(r1)} に書き込めない（STS ${hex(r0, 2)}）`);
    progress('書き込み', off + chunk.length, words.length);
  }

  log('照合');
  const back = await dap.readWords(FLASH_BASE, words.length, (d, n) => progress('照合', d, n));
  for (let i = 0; i < words.length; i++) {
    if (back[i] !== words[i]) {
      throw new DapError(`照合エラー @${hex(FLASH_BASE + i * 4)}: 書いた値 ${hex(words[i])}, 読んだ値 ${hex(back[i])}`);
    }
  }
  await dap.write32(CTRL, OPLK);
  log('照合OK。新しいファームで起動');
  await dap.resetRun();
}

// Read g_diag without halting. Returns {stage, values} or throws.
export async function readDiag(dap, addr, fields) {
  const stage = await dap.read32(addr);
  const values = [];
  for (const [name, idx] of fields) values.push([name, await dap.read32(addr + 4 * idx)]);
  return { stage, values };
}

export { sleep };
