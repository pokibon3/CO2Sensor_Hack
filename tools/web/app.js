import { Dap, DapError, USB_FILTERS } from './dap.js';
import { identify, describe, writeFlash, readDiag, sleep, AT32_DPIDR } from './at32.js';

const WRITE_TRIES = 3;   // as tools/flashfw.py
const STARTUP_MS = 2000; // the games latch the power after PWR is held 1 s
const FLASH_SIZE = 0x20000;
const STAGES = { 0: 'mainに届いていない（電源ON待ち）', 1: 'LT7680Bの初期化で停止', 2: 'LT7680B初期化の後で停止',
  3: '動作中', 0xE1: 'LT7680Bの初期化に失敗' };

const $ = (id) => document.getElementById(id);
let dap = null;
let manifest = null;
let busy = false;
const choices = []; // {id, title, detail, load(): Promise<Uint8Array>, diag?, fields?, check?(bytes)}

function log(msg) {
  const t = new Date().toLocaleTimeString();
  $('log').textContent += `[${t}] ${msg}\n`;
  $('log').scrollTop = $('log').scrollHeight;
}

function banner(el, kind, text) {
  el.className = `banner show ${kind}`;
  el.textContent = text;
}

function hideBanner(el) {
  el.className = 'banner';
}

function setBusy(b) {
  busy = b;
  $('pick').disabled = b;
  $('check').disabled = b || !dap;
  $('write').disabled = b || !dap || !selected();
}

function selected() {
  const r = document.querySelector('input[name=fw]:checked');
  return r ? choices.find((c) => c.id === r.value) : null;
}

async function sha256(bytes) {
  const h = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(h)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

async function fetchBin(path) {
  const r = await fetch(path, { cache: 'no-store' });
  if (!r.ok) throw new Error(`${path} を読めない（HTTP ${r.status}）`);
  return new Uint8Array(await r.arrayBuffer());
}

// ---- debugger ----

async function useDevice(device) {
  if (dap) await dap.close();
  dap = new Dap(device);
  try {
    await dap.open();
  } catch (e) {
    dap = null;
    $('probe').textContent = '開けない';
    log(`デバッガを開けない: ${e.message}`);
    if (/Access denied|claim/i.test(e.message)) {
      log('他のアプリ（OpenOCDなど）が使っていないか確認する。Windowsではドライバ（WinUSB）を確認する');
    }
    setBusy(false);
    return;
  }
  const fw = await dap.infoString(0x04).catch(() => '');
  $('probe').textContent = `${dap.name}${fw ? ` / FW ${fw}` : ''}`;
  log(`デバッガ: ${dap.name}, パケット ${dap.packetSize} バイト`);
  setBusy(false);
}

$('pick').onclick = async () => {
  try {
    const d = await navigator.usb.requestDevice({ filters: USB_FILTERS });
    await useDevice(d);
  } catch (e) {
    if (e.name !== 'NotFoundError') log(`デバッガを選べない: ${e.message}`);
  }
};

async function connectTarget() {
  let dpidr;
  try {
    dpidr = await dap.connect();
  } catch (e) {
    throw new DapError('MCUが応答しない。PWRで電源を入れる（自作ファームの動作中はPWRを押したまま）。'
      + 'GP2（SWCLK）とGP3（SWDIO）が入れ替わっていないか、GNDがつながっているかも確認する');
  }
  if (dpidr !== AT32_DPIDR) throw new DapError(`想定外のDPIDR 0x${dpidr.toString(16)}（AT32F415は0x2BA01477）`);
  return dpidr;
}

$('check').onclick = async () => {
  setBusy(true);
  hideBanner($('checkResult'));
  try {
    await connectTarget();
    let info;
    try {
      info = await identify(dap);
    } catch (e) {
      throw new DapError('DPは見えるがメモリにアクセスできない。自作ファームの動作中はPWRを押したまま確認する');
    }
    describe(info).forEach(log);
    banner($('checkResult'), 'ok', `OK: AT32F415に接続できた（${describe(info).slice(1).join('、')}）`);
  } catch (e) {
    log(`NG: ${e.message}`);
    banner($('checkResult'), 'err', `NG: ${e.message}`);
  } finally {
    setBusy(false);
  }
};

if (navigator.usb) {
  navigator.usb.addEventListener('disconnect', (ev) => {
    if (dap && ev.device === dap.dev) {
      dap = null;
      $('probe').textContent = '外れた';
      log('デバッガが外れた');
      setBusy(false);
    }
  });
  navigator.usb.addEventListener('connect', async (ev) => {
    if (!dap && !busy) await useDevice(ev.device);
  });
}

// ---- firmware list ----

function fmtDate(sec) {
  return new Date(sec * 1000).toLocaleString();
}

function renderChoices() {
  const list = $('fwlist');
  list.textContent = '';
  for (const c of choices) {
    const l = document.createElement('label');
    const r = document.createElement('input');
    r.type = 'radio';
    r.name = 'fw';
    r.value = c.id;
    r.onchange = () => { hideBanner($('result')); setBusy(busy); };
    const t = document.createElement('span');
    t.className = 't';
    t.textContent = c.title;
    const d = document.createElement('span');
    d.className = 'd';
    d.textContent = c.detail;
    c.detailEl = d;
    l.append(r, t, d);
    if (c.extra) l.append(c.extra);
    list.append(l);
  }
}

async function loadManifest() {
  try {
    manifest = await (await fetch('firmware/manifest.json', { cache: 'no-store' })).json();
  } catch (e) {
    manifest = { firmware: [], factory: null };
    log('firmware/manifest.json がない。tools/web_flasher.py から起動すること');
  }
  for (const f of manifest.firmware) {
    choices.push({
      id: f.id,
      title: f.name,
      detail: `${f.source} ・ ${f.size.toLocaleString()} バイト ・ ビルド ${fmtDate(f.built)}`,
      load: async () => {
        const b = await fetchBin(f.file);
        if (await sha256(b) !== f.sha256) throw new Error(`${f.file} がmanifest.jsonと一致しない。ページを再読み込みする`);
        return b;
      },
      diag: f.g_diag, fields: f.fields,
    });
  }
  if (manifest.factory) {
    const fac = manifest.factory;
    choices.push({
      id: 'factory',
      title: '純正ファームに戻す',
      detail: 'analysis/factory_dump/factory_firmware.bin（作者の基板から読み出したもの）',
      load: async () => checkFactory(await fetchBin(fac.file), 'factory_firmware.bin'),
    });
    // A backup of this unit (backups/*/factory_firmware.bin)
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.bin';
    input.style.gridColumn = '2';
    const backup = {
      id: 'backup',
      title: 'バックアップから戻す',
      detail: 'backup_flashで保存した factory_firmware.bin を選ぶ',
      extra: input,
      file: null,
      load: async () => {
        if (!backup.file) throw new Error('ファイルを選んでいない');
        return checkFactory(new Uint8Array(await backup.file.arrayBuffer()), backup.file.name);
      },
    };
    input.onchange = () => {
      backup.file = input.files[0] || null;
      document.querySelector('input[value=backup]').checked = true;
      setBusy(busy);
    };
    choices.push(backup);
  }
  renderChoices();
}

// Size and SHA256 check, as tools/restore_flash.py.
async function checkFactory(bytes, name) {
  if (bytes.length !== FLASH_SIZE) throw new Error(`${name}: ${bytes.length} バイト（${FLASH_SIZE} バイトのはず）`);
  const h = await sha256(bytes);
  if (h === manifest.factory.sha256) {
    log(`${name}: SHA256 ${h.slice(0, 16)}… 作者の純正ダンプと一致`);
  } else {
    log(`${name}: SHA256 ${h.slice(0, 16)}… 作者の純正ダンプと異なる`);
    if (!confirm(`${name} は作者の純正ダンプと異なる（別のファームか別の個体）。書き込む？`)) throw new Error('中止した');
  }
  return bytes;
}

// ---- write ----

function progress(stage, done, total) {
  $('stage').textContent = stage;
  const p = Math.floor((done / total) * 100);
  $('pct').textContent = `${p}%`;
  $('barfill').style.width = `${p}%`;
}

$('write').onclick = async () => {
  const c = selected();
  if (!c || !dap) return;
  setBusy(true);
  hideBanner($('result'));
  $('progress').classList.add('show');
  progress('準備', 0, 1);
  try {
    const image = await c.load();
    log(`${c.title}: ${image.length} バイトを書き込む`);
    banner($('result'), 'info', 'PWRを押したまま待つ（書き込み中）');
    let ok = false;
    for (let attempt = 1; attempt <= WRITE_TRIES && !ok; attempt++) {
      try {
        await connectTarget();
        const t0 = performance.now();
        await writeFlash(dap, image, log, progress);
        log(`書き込み完了（${((performance.now() - t0) / 1000).toFixed(1)} 秒）`);
        ok = true;
      } catch (e) {
        log(`書き込み失敗（${attempt}回目）: ${e.message}`);
        if (attempt === WRITE_TRIES) throw e;
        log('やり直す。PWRは押したまま');
      }
    }

    for (let left = STARTUP_MS; left > 0; left -= 500) {
      banner($('result'), 'info', `起動中。PWRを押したまま（あと${Math.ceil(left / 1000)}秒）`);
      progress('起動待ち', STARTUP_MS - left, STARTUP_MS);
      await sleep(500);
    }
    progress('完了', 1, 1);

    if (c.diag == null) {
      banner($('result'), 'ok', '完了。PWRを離してよい');
      log('完了');
      return;
    }
    let d;
    try {
      d = await readDiag(dap, c.diag, c.fields);
    } catch (e) {
      try { await dap.lineReset(); d = await readDiag(dap, c.diag, c.fields); } catch (e2) { d = null; }
    }
    if (!d) {
      log('g_diagを読めない（PWRを押していたか確認する）');
      banner($('result'), 'err', `${c.title}の起動を確認できない。PWRを離して、ログを送る`);
      return;
    }
    const line = `g_diag: stage 0x${d.stage.toString(16)}（${STAGES[d.stage] || '不明'}）`
      + d.values.map(([n, v]) => `, ${n} ${v}`).join('');
    log(line);
    if (d.stage === 3) {
      banner($('result'), 'ok', `完了。${c.title}が起動した。PWRを離してよい`);
    } else {
      banner($('result'), 'err', `${c.title}が起動していない。PWRを離して、ログのg_diagの行を送る`);
    }
  } catch (e) {
    log(`失敗: ${e.message}`);
    banner($('result'), 'err', `失敗: ${e.message}`);
  } finally {
    setBusy(false);
  }
};

// ---- start ----

(async () => {
  if (!navigator.usb) {
    $('unsupported').hidden = false;
    $('pick').disabled = true;
  } else {
    const devs = await navigator.usb.getDevices();
    if (devs.length) await useDevice(devs[0]);
  }
  await loadManifest();
  setBusy(false);
})();
