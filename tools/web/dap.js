// CMSIS-DAP v2 (WebUSB) + SWD/ADIv5 memory access + Cortex-M debug, for the
// debugprobe on the RP2040-Zero (doc/debugger_connection.md).
// Only what the web flasher needs: no JTAG, one AHB-AP (AP 0).

export class DapError extends Error {}

const CMD = {
  INFO: 0x00, CONNECT: 0x02, DISCONNECT: 0x03, XFER_CONFIG: 0x04,
  XFER: 0x05, XFER_BLOCK: 0x06, SWJ_CLOCK: 0x11, SWJ_SEQ: 0x12, SWD_CONFIG: 0x13,
};
const ACK = { 1: 'OK', 2: 'WAIT', 4: 'FAULT', 7: 'no answer' };

// DP / AP registers (address bits [3:2] of the request)
const DP_DPIDR = 0x0, DP_ABORT = 0x0, DP_CTRL = 0x4, DP_SELECT = 0x8;
const AP_CSW = 0x0, AP_TAR = 0x4, AP_DRW = 0xC;
const CSW_VALUE = 0x23000052; // 32-bit, auto-increment, HPROT, MasterType debug

// Cortex-M debug registers
export const DHCSR = 0xE000EDF0, DCRSR = 0xE000EDF4, DCRDR = 0xE000EDF8,
  DEMCR = 0xE000EDFC, AIRCR = 0xE000ED0C, CPUID = 0xE000ED00;
const DBGKEY = 0xA05F0000;
const C_DEBUGEN = 1, C_HALT = 2, C_MASKINTS = 8;
const S_REGRDY = 1 << 16, S_HALT = 1 << 17;

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const req = (ap, rnw, addr) => (ap ? 1 : 0) | (rnw ? 2 : 0) | (addr & 0xC);

// Probes the page can open. 2E8A:000C is the Raspberry Pi debugprobe.
export const USB_FILTERS = [{ vendorId: 0x2E8A, productId: 0x000C }];

export class Dap {
  constructor(device) {
    this.dev = device;
    this.packetSize = 64;
    this.log = () => {};
  }

  get name() {
    return `${this.dev.productName || 'CMSIS-DAP'} (${this.dev.serialNumber || 'no serial'})`;
  }

  // ---- USB ----

  async open() {
    const d = this.dev;
    if (!d.opened) await d.open();
    if (!d.configuration) await d.selectConfiguration(1);
    let found = null;
    for (const intf of d.configuration.interfaces) {
      const alt = intf.alternates[0];
      if (alt.interfaceClass !== 0xFF) continue;
      const epIn = alt.endpoints.find((e) => e.direction === 'in' && e.type === 'bulk');
      const epOut = alt.endpoints.find((e) => e.direction === 'out' && e.type === 'bulk');
      if (!epIn || !epOut) continue;
      const cand = { intf: intf.interfaceNumber, epIn: epIn.endpointNumber, epOut: epOut.endpointNumber };
      if (!found || /CMSIS-DAP/i.test(alt.interfaceName || '')) found = cand;
    }
    if (!found) throw new DapError('CMSIS-DAP v2（USBバルク）のインターフェースが見つからない');
    Object.assign(this, found);
    await d.claimInterface(this.intf);
    const ps = await this.info(0xFF);
    if (ps.length >= 2) this.packetSize = ps[0] | (ps[1] << 8);
    // Words per DAP_TransferBlock. Keep a response that is a multiple of the
    // 64-byte USB packet but shorter than packetSize out (it would need a ZLP).
    this.blockRead = Math.floor((this.packetSize - 4) / 4);
    while ((4 + 4 * this.blockRead) % 64 === 0 && 4 + 4 * this.blockRead !== this.packetSize) this.blockRead--;
    this.blockWrite = Math.floor((this.packetSize - 5) / 4);
  }

  async close() {
    try { await this.cmd([CMD.DISCONNECT]); } catch (e) { /* ignore */ }
    try { await this.dev.releaseInterface(this.intf); } catch (e) { /* ignore */ }
    try { await this.dev.close(); } catch (e) { /* ignore */ }
  }

  async cmd(bytes) {
    const out = new Uint8Array(bytes.length % 64 === 0 && bytes.length < this.packetSize ? bytes.length + 1 : bytes.length);
    out.set(bytes);
    const w = await this.dev.transferOut(this.epOut, out);
    if (w.status !== 'ok') throw new DapError(`USB送信エラー: ${w.status}`);
    const r = await this.dev.transferIn(this.epIn, this.packetSize);
    if (r.status !== 'ok') throw new DapError(`USB受信エラー: ${r.status}`);
    const resp = new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
    if (resp[0] !== bytes[0]) throw new DapError(`CMSIS-DAPの応答が不正 (cmd ${bytes[0]}, resp ${resp[0]})`);
    return resp;
  }

  async info(id) {
    const r = await this.cmd([CMD.INFO, id]);
    return r.slice(2, 2 + r[1]);
  }

  async infoString(id) {
    return new TextDecoder().decode(await this.info(id)).replace(/\0+$/, '');
  }

  // ---- SWD link ----

  async connect(hz = 1000000) {
    let r = await this.cmd([CMD.CONNECT, 1]);
    if (r[1] !== 1) throw new DapError('SWDモードで接続できない');
    r = await this.cmd([CMD.SWJ_CLOCK, hz & 0xFF, (hz >> 8) & 0xFF, (hz >> 16) & 0xFF, (hz >>> 24) & 0xFF]);
    if (r[1] !== 0) throw new DapError('SWDクロックを設定できない');
    // idle cycles 0, WAIT retry 100, match retry 0
    await this.cmd([CMD.XFER_CONFIG, 0, 100, 0, 0, 0]);
    await this.cmd([CMD.SWD_CONFIG, 0]);
    return this.lineReset();
  }

  // Line reset, JTAG-to-SWD, line reset, idle; then read DPIDR and power up the debug domain.
  async lineReset() {
    const ones = [0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF];
    await this.cmd([CMD.SWJ_SEQ, 56, ...ones]);
    await this.cmd([CMD.SWJ_SEQ, 16, 0x9E, 0xE7]);
    await this.cmd([CMD.SWJ_SEQ, 56, ...ones]);
    await this.cmd([CMD.SWJ_SEQ, 8, 0x00]);
    const [dpidr] = await this.transfer([{ r: true, addr: DP_DPIDR }]);
    await this.transfer([{ addr: DP_ABORT, v: 0x1E }]);
    await this.transfer([{ addr: DP_CTRL, v: 0x50000000 }]);
    for (let i = 0; ; i++) {
      const [st] = await this.transfer([{ r: true, addr: DP_CTRL }]);
      if (((st & 0xA0000000) >>> 0) === 0xA0000000) break;
      if (i > 100) throw new DapError('デバッグ電源が入らない（CTRL/STAT）');
      await sleep(1);
    }
    await this.transfer([{ addr: DP_SELECT, v: 0 }, { ap: true, addr: AP_CSW, v: CSW_VALUE }]);
    this.dpidr = dpidr >>> 0;
    return this.dpidr;
  }

  // ops: [{ap, r, addr, v}] -> values of the reads
  async transfer(ops) {
    const b = [CMD.XFER, 0, ops.length];
    for (const o of ops) {
      b.push(req(o.ap, o.r, o.addr));
      if (!o.r) b.push(o.v & 0xFF, (o.v >> 8) & 0xFF, (o.v >> 16) & 0xFF, (o.v >>> 24) & 0xFF);
    }
    const r = await this.cmd(b);
    const ack = r[2] & 7;
    if (r[1] !== ops.length || ack !== 1) {
      await this.clearErrors();
      throw new DapError(`SWD転送エラー: ${ACK[ack] || `ack ${ack}`}${r[2] & 8 ? '（パリティ）' : ''}`);
    }
    const dv = new DataView(r.buffer, r.byteOffset + 3);
    const vals = [];
    for (let i = 0; i < dv.byteLength / 4; i++) vals.push(dv.getUint32(i * 4, true));
    return vals;
  }

  async clearErrors() {
    try {
      await this.cmd([CMD.XFER, 0, 1, req(false, false, DP_ABORT), 0x1E, 0, 0, 0]);
    } catch (e) { /* ignore */ }
  }

  // ---- memory ----

  async read32(addr) {
    const [v] = await this.transfer([{ ap: true, addr: AP_TAR, v: addr }, { ap: true, r: true, addr: AP_DRW }]);
    return v >>> 0;
  }

  async write32(addr, v) {
    await this.transfer([{ ap: true, addr: AP_TAR, v: addr }, { ap: true, addr: AP_DRW, v }]);
  }

  // TAR auto-increment wraps at 1 KB, so split there.
  async readWords(addr, n, progress) {
    const out = new Uint32Array(n);
    let done = 0;
    while (done < n) {
      const a = addr + done * 4;
      const cnt = Math.min(n - done, this.blockRead, (0x400 - (a & 0x3FF)) / 4);
      await this.transfer([{ ap: true, addr: AP_TAR, v: a }]);
      const r = await this.cmd([CMD.XFER_BLOCK, 0, cnt & 0xFF, cnt >> 8, req(true, true, AP_DRW)]);
      const got = r[1] | (r[2] << 8);
      if (got !== cnt || (r[3] & 7) !== 1) {
        await this.clearErrors();
        throw new DapError(`メモリ読み出しエラー @0x${a.toString(16)}: ${ACK[r[3] & 7] || r[3]}`);
      }
      const dv = new DataView(r.buffer, r.byteOffset + 4, cnt * 4);
      for (let i = 0; i < cnt; i++) out[done + i] = dv.getUint32(i * 4, true);
      done += cnt;
      if (progress) progress(done, n);
    }
    return out;
  }

  async writeWords(addr, words, progress) {
    let done = 0;
    while (done < words.length) {
      const a = addr + done * 4;
      const cnt = Math.min(words.length - done, this.blockWrite, (0x400 - (a & 0x3FF)) / 4);
      await this.transfer([{ ap: true, addr: AP_TAR, v: a }]);
      const b = new Uint8Array(5 + cnt * 4);
      b.set([CMD.XFER_BLOCK, 0, cnt & 0xFF, cnt >> 8, req(true, false, AP_DRW)]);
      const dv = new DataView(b.buffer, 5);
      for (let i = 0; i < cnt; i++) dv.setUint32(i * 4, words[done + i], true);
      const r = await this.cmd(b);
      const got = r[1] | (r[2] << 8);
      if (got !== cnt || (r[3] & 7) !== 1) {
        await this.clearErrors();
        throw new DapError(`メモリ書き込みエラー @0x${a.toString(16)}: ${ACK[r[3] & 7] || r[3]}`);
      }
      done += cnt;
      if (progress) progress(done, words.length);
    }
  }

  // ---- Cortex-M ----

  async halt() {
    await this.write32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT);
    await this.waitHalt(500);
  }

  async waitHalt(ms) {
    const t0 = performance.now();
    for (;;) {
      const s = await this.read32(DHCSR);
      if (s & S_HALT) return s;
      if (performance.now() - t0 > ms) throw new DapError('CPUが停止しない');
      await sleep(1);
    }
  }

  // Reset with SYSRESETREQ and stop at the reset vector (VC_CORERESET).
  // Halt first: writing DHCSR without C_HALT would resume a core stopped at
  // the stub's BKPT.
  async resetHalt() {
    await this.write32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT);
    await this.write32(DEMCR, (await this.read32(DEMCR)) | 1);
    try {
      await this.write32(AIRCR, 0x05FA0004);
    } catch (e) { /* the reset may cut the answer */ }
    await sleep(20);
    let s = 0;
    for (let i = 0; ; i++) {
      try {
        s = await this.read32(DHCSR);
        if (s & S_HALT) break;
      } catch (e) {
        try { await this.lineReset(); } catch (e2) { /* retry */ }
      }
      if (i > 50) throw new DapError('リセット後にCPUが停止しない');
      await sleep(5);
    }
    await this.write32(DEMCR, (await this.read32(DEMCR)) & ~1);
  }

  // Reset into the new firmware. The core stays halted through the reset
  // (C_HALT survives a system reset) and is released from the reset vector by
  // clearing C_DEBUGEN, so it never resumes from the stub's BKPT.
  async resetRun() {
    await this.write32(DEMCR, (await this.read32(DEMCR)) & ~1);
    try {
      await this.write32(AIRCR, 0x05FA0004);
    } catch (e) { /* the reset may cut the answer */ }
    await sleep(20);
    for (let i = 0; ; i++) {
      try {
        await this.write32(DHCSR, DBGKEY);
        if (!((await this.read32(DHCSR)) & S_HALT)) return;
      } catch (e) {
        try { await this.lineReset(); } catch (e2) { /* retry */ }
      }
      if (i > 50) throw new DapError('リセット後にCPUが動き出さない');
      await sleep(5);
    }
  }

  async waitRegReady() {
    for (let i = 0; i < 100; i++) {
      if ((await this.read32(DHCSR)) & S_REGRDY) return;
    }
    throw new DapError('コアレジスタにアクセスできない');
  }

  async writeReg(n, v) {
    await this.write32(DCRDR, v);
    await this.write32(DCRSR, n | 0x10000);
    await this.waitRegReady();
  }

  async readReg(n) {
    await this.write32(DCRSR, n);
    await this.waitRegReady();
    return this.read32(DCRDR);
  }

  // Run code in RAM from pc with r0..r2 set, interrupts masked, until it halts (BKPT).
  async call(pc, sp, args, timeoutMs) {
    for (let i = 0; i < args.length; i++) await this.writeReg(i, args[i]);
    await this.writeReg(13, sp);
    await this.writeReg(15, pc);
    await this.writeReg(16, 0x01000000); // xPSR: Thumb
    await this.write32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT | C_MASKINTS);
    await this.write32(DHCSR, DBGKEY | C_DEBUGEN | C_MASKINTS);
    try {
      await this.waitHalt(timeoutMs);
    } catch (e) {
      await this.halt();
      throw new DapError('RAM上の書き込みルーチンが終わらない');
    }
    return [await this.readReg(0), await this.readReg(1)];
  }
}
