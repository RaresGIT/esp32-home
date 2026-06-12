'use strict';
const $ = s => document.querySelector(s);

function toast(msg, ms = 3000) {
  const t = $('#toast');
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(t._h);
  t._h = setTimeout(() => (t.hidden = true), ms);
}

async function api(path, opts = {}) {
  const r = await fetch(path, opts);
  let body = {};
  try { body = await r.json(); } catch {}
  if (!r.ok) throw new Error(body.error || 'HTTP ' + r.status);
  return body;
}
const post = (path, body) =>
  api(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

// ---- tabs
document.querySelectorAll('nav .tab').forEach(b =>
  b.addEventListener('click', () => {
    document.querySelectorAll('nav .tab').forEach(x => x.classList.toggle('active', x === b));
    document.querySelectorAll('.tabpane').forEach(p => p.classList.toggle('active', p.id === 'tab-' + b.dataset.tab));
    if (b.dataset.tab === 'settings') scanWifi();  // results ready by the time you look
  })
);

// ---- live event stream
let ws;
function wsConnect() {
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onopen = () => $('#wsDot').classList.add('on');
  ws.onclose = () => {
    $('#wsDot').classList.remove('on');
    setTimeout(wsConnect, 2000);
  };
  ws.onmessage = e => {
    try { addEvent(JSON.parse(e.data)); } catch {}
  };
}

function btn(label, fn, cls = '') {
  const b = document.createElement('button');
  b.textContent = label;
  b.className = 'mini ' + cls;
  b.addEventListener('click', fn);
  return b;
}

function addEvent(m) {
  const box = $('#events');
  const div = document.createElement('div');
  div.className = 'event';
  const time = `<span class="ts">${new Date().toLocaleTimeString()}</span>`;
  let signal = null, head = '';

  if (m.type === 'tx') {
    div.classList.add('tx');
    head = `<span class="badge btx">TX</span> replayed <b>${esc(m.name)}</b>`;
  } else if (m.mode === 'decoded') {
    if (!m.code) {
      div.classList.add('noise');
      head = `<span class="badge">RX</span> undecodable burst (noise or unsupported protocol — try Raw mode)`;
    } else {
      signal = { protocol: m.protocol, bits: m.bits, code: m.code, pulse: m.pulse };
      head = `<span class="badge brx">RX</span> <b class="mono">${m.code}</b>
        <span class="mono muted">0x${m.code.toString(16).toUpperCase()}</span>
        · proto ${m.protocol} · ${m.bits}-bit · pulse ${m.pulse} µs`;
    }
  } else {
    signal = { timings: m.timings };
    head = `<span class="badge brx">RX</span> raw frame · <b>${m.edges}</b> edges
      <span class="mono muted">[${(m.timings || []).slice(0, 10).join(' ')}${m.edges > 10 ? ' …' : ''}]</span>`;
  }

  div.innerHTML = head + ' ' + time;
  if (signal) {
    const act = document.createElement('span');
    act.className = 'actions';
    act.append(
      btn('Replay', () => post('/api/replay', signal).then(() => toast('Replayed')).catch(e => toast('Error: ' + e.message))),
      btn('Save…', () => openSave(signal))
    );
    div.append(act);
  }
  box.prepend(div);
  while (box.children.length > 100) box.lastChild.remove();
}

// ---- save dialog
let pendingSignal = null;
function openSave(signal) {
  pendingSignal = signal;
  $('#saveDesc').textContent = signal.timings
    ? `Raw signal, ${signal.timings.length} edges`
    : `Code ${signal.code} (protocol ${signal.protocol}, ${signal.bits}-bit, pulse ${signal.pulse} µs)`;
  $('#saveDlg').showModal();
}
$('#saveForm').addEventListener('submit', e => {
  if (e.submitter && e.submitter.value === 'cancel') return;
  const fd = new FormData(e.target);
  post('/api/codes', { ...pendingSignal, name: fd.get('name'), repeats: +fd.get('repeats') })
    .then(() => { toast('Saved'); loadCodes(); })
    .catch(err => toast('Error: ' + err.message));
});

// ---- receiver mode
function setMode(m, announce = true) {
  $('#modeDecoded').classList.toggle('active', m === 'decoded');
  $('#modeRaw').classList.toggle('active', m === 'raw');
  if (announce)
    post('/api/mode', { mode: m })
      .then(() => toast('Receiver in ' + m + ' mode'))
      .catch(e => toast('Error: ' + e.message));
}
$('#modeDecoded').onclick = () => setMode('decoded');
$('#modeRaw').onclick = () => setMode('raw');
$('#clearEvents').onclick = () => $('#events').replaceChildren();

// ---- codes
async function loadCodes() {
  const { codes = [] } = await api('/api/codes').catch(() => ({ codes: [] }));
  const tb = $('#codesTbl tbody');
  tb.replaceChildren();
  if (!codes.length) {
    tb.innerHTML = '<tr><td colspan="5" class="muted">No saved codes yet — capture one in the Sniffer tab.</td></tr>';
    return;
  }
  for (const c of codes) {
    const tr = document.createElement('tr');
    const details = c.raw
      ? `${c.timings.length} edges`
      : `code ${c.code} · proto ${c.protocol} · ${c.bits}-bit · ${c.pulse} µs`;
    tr.innerHTML = `<td><b>${esc(c.name)}</b></td><td><span class="chip">${c.raw ? 'raw' : 'decoded'}</span></td>
      <td class="mono muted">${details}</td><td>${c.repeats}</td>`;
    const td = document.createElement('td');
    td.append(
      btn('Replay', () => post('/api/replay', { id: c.id }).then(() => toast('Replayed ' + c.name)).catch(e => toast('Error: ' + e.message))),
      btn('Delete', () => {
        if (confirm(`Delete "${c.name}"?`)) api('/api/codes?id=' + c.id, { method: 'DELETE' }).then(loadCodes);
      }, 'danger')
    );
    tr.append(td);
    tb.append(tr);
  }
}

// ---- settings
async function loadSettings() {
  const s = await api('/api/settings').catch(() => null);
  if (!s) return;
  $('#wifiForm [name=ssid]').value = s.ssid || '';
  $('#mqttForm [name=host]').value = s.mqttHost || '';
  $('#mqttForm [name=port]').value = s.mqttPort || 1883;
  $('#mqttForm [name=user]').value = s.mqttUser || '';
}

function renderNets(networks, refreshing) {
  const seen = new Set();
  const nets = (networks || [])
    .filter(n => n.ssid && !seen.has(n.ssid) && seen.add(n.ssid))
    .sort((a, b) => b.rssi - a.rssi);
  const items = nets.map(n => {
    const li = document.createElement('li');
    const bars = n.rssi > -55 ? '▂▄▆█' : n.rssi > -65 ? '▂▄▆ ' : n.rssi > -75 ? '▂▄  ' : '▂   ';
    li.innerHTML = `<span class="bars mono">${bars}</span> <b>${esc(n.ssid)}</b>${n.secure ? ' <span class="muted">🔒</span>' : ''}`;
    li.onclick = () => openJoin(n);
    return li;
  });
  if (refreshing) {
    const li = document.createElement('li');
    li.className = 'muted';
    li.textContent = 'refreshing list…';
    items.push(li);
  }
  if (!items.length) {
    $('#scanList').innerHTML = '<li class="muted">No networks found — try Rescan.</li>';
    return;
  }
  $('#scanList').replaceChildren(...items);
}

let scanInFlight = false;
async function scanWifi() {
  if (scanInFlight) return;
  scanInFlight = true;
  if (!$('#scanList').querySelector('li:not(.muted)'))
    $('#scanList').innerHTML = '<li class="muted">Scanning…</li>';
  try {
    // Cached results come back instantly; keep polling in the background
    // until a fresh scan lands (slow while the device is on Wi-Fi).
    for (let i = 0; i < 90; i++) {
      const r = await api('/api/scan');
      if (r.networks) renderNets(r.networks, r.refreshing);
      if (r.networks && !r.refreshing) return;
      await new Promise(res => setTimeout(res, 1000));
    }
  } catch (e) {
    $('#scanList').innerHTML = '<li class="muted">Scan failed — try Rescan.</li>';
  } finally {
    scanInFlight = false;
  }
}
$('#scanBtn').onclick = scanWifi;

let joinNet = null;
function openJoin(n) {
  joinNet = n;
  $('#joinSsid').textContent = n.ssid;
  $('#joinPassRow').style.display = n.secure ? '' : 'none';
  $('#joinForm [name=pass]').value = '';
  $('#joinDlg').showModal();
  if (n.secure) $('#joinForm [name=pass]').focus();
}
$('#joinForm').addEventListener('submit', e => {
  if (e.submitter && e.submitter.value === 'cancel') return;
  post('/api/wifi', { ssid: joinNet.ssid, pass: $('#joinForm [name=pass]').value })
    .then(() => toast(`Joining "${joinNet.ssid}" — device rebooting. Reconnect to that network, then open http://windowctl.local`, 10000))
    .catch(err => toast('Error: ' + err.message));
});

$('#wifiForm').addEventListener('submit', e => {
  e.preventDefault();
  const fd = new FormData(e.target);
  post('/api/wifi', { ssid: fd.get('ssid'), pass: fd.get('pass') })
    .then(() => toast('Saved — rebooting. Reconnect to your Wi-Fi, then open http://windowctl.local', 9000))
    .catch(err => toast('Error: ' + err.message));
});

$('#mqttForm').addEventListener('submit', e => {
  e.preventDefault();
  const fd = new FormData(e.target);
  post('/api/mqtt', { host: fd.get('host'), port: +fd.get('port'), user: fd.get('user'), pass: fd.get('pass') })
    .then(() => toast('Saved — rebooting…', 6000))
    .catch(err => toast('Error: ' + err.message));
});

$('#otaForm').addEventListener('submit', e => {
  e.preventDefault();
  const f = e.target.file.files[0];
  if (!f) return;
  const prog = $('#otaProg');
  prog.hidden = false;
  prog.value = 0;
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update?type=' + e.target.type.value);
  xhr.upload.onprogress = ev => { if (ev.lengthComputable) prog.value = (ev.loaded / ev.total) * 100; };
  xhr.onload = () => {
    prog.hidden = true;
    if (xhr.status === 200) {
      toast('Update OK — rebooting…', 6000);
      setTimeout(() => location.reload(), 8000);
    } else toast('Update failed: ' + xhr.responseText, 6000);
  };
  xhr.onerror = () => { prog.hidden = true; toast('Upload failed'); };
  const fd = new FormData();
  fd.append('file', f);
  xhr.send(fd);
});

$('#factoryBtn').onclick = () => {
  if (confirm('Erase Wi-Fi, MQTT and all saved codes?'))
    post('/api/factory-reset', {}).then(() => toast('Factory reset — rebooting into setup AP', 9000));
};

$('#hkResetBtn').onclick = () => {
  if (confirm('Erase HomeKit pairing data? You will need to re-add the accessory in the Home app.'))
    post('/api/homekit-reset', {}).then(() => toast('Pairing erased — rebooting, re-add from the Home app', 9000));
};

// ---- homekit pairing
async function loadHomekit() {
  const h = await api('/api/homekit').catch(() => null);
  if (!h) return;
  $('#hkCode').textContent = h.code.replace(/(\d{3})(\d{2})(\d{3})/, '$1-$2-$3');
  if (!h.active) {
    $('#hkHint').textContent = 'HomeKit pairing becomes available once the device is on your Wi-Fi network (not in setup AP mode).';
    return;
  }
  try {
    const qr = qrcode(0, 'M');
    qr.addData(h.uri);
    qr.make();
    $('#hkQr').innerHTML = qr.createSvgTag({ cellSize: 5, margin: 3 });
    $('#hkHint').textContent = 'Already paired? Manage or remove the accessory from the Home app.';
  } catch (e) {
    $('#hkHint').textContent = 'QR render failed — use the manual code above. (' + e.message + ')';
  }
}

// ---- stats
function fmtUptime(sec) {
  const d = Math.floor(sec / 86400), h = Math.floor((sec % 86400) / 3600),
        m = Math.floor((sec % 3600) / 60), s = sec % 60;
  return (d ? d + 'd ' : '') + (h ? h + 'h ' : '') + (m ? m + 'm ' : '') + s + 's';
}

async function refreshStats() {
  const s = await api('/api/status').catch(() => null);
  if (!s) return;
  $('#devId').textContent = (s.id ? 'id ' + s.id : '') + (s.ip ? ' · ' + s.ip : '');
  setMode(s.mode, false);
  if (s.ap && !window._autoScanned) {  // setup mode: scan immediately, it's the next step
    window._autoScanned = true;
    scanWifi();
  }
  const rows = [
    ['Firmware', s.fw],
    ['Uptime', fmtUptime(s.uptime)],
    ['Wi-Fi', s.ap ? 'setup AP mode' : `${esc(s.ssid)} (${s.rssi} dBm)`],
    ['Free heap', Math.round(s.heap / 1024) + ' KB'],
    ['Signals sent', s.tx],
    ['Signals received', s.rxc],
    ['MQTT', s.mqtt ? 'connected' : 'not connected'],
    ['Window (simulated)', s.window || 'unknown'],
    ['Receiver mode', s.mode],
    ['Saved codes', s.codes],
  ];
  $('#statsGrid').replaceChildren(
    ...rows.map(([k, v]) => {
      const d = document.createElement('div');
      d.className = 'stat';
      d.innerHTML = `<div class="k">${k}</div><div class="v">${v}</div>`;
      return d;
    })
  );
}

wsConnect();
loadCodes();
loadSettings();
loadHomekit();
refreshStats();
setInterval(refreshStats, 5000);
