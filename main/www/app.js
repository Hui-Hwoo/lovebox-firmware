const $ = s => document.querySelector(s);
let STATE = null;

async function load() {
  const r = await fetch('/api/state');
  STATE = await r.json();
  renderStatus();
  renderScreens();
  renderFields();
  $('#otaStatus').textContent = STATE.ota_status || '—';
}

function renderStatus() {
  $('#st-ver').textContent  = 'v' + STATE.version;
  $('#st-build').textContent = STATE.build;
  $('#st-part').textContent = STATE.partition;
  $('#st-ip').textContent   = STATE.ip || '(未连接)';
  $('#st-wifi').textContent = STATE.wifi_ssid;
  $('#st-up').textContent   = fmtUptime(STATE.uptime);
}
function fmtUptime(s) {
  s = Math.floor(s || 0);
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
  return h + ' 小时 ' + m + ' 分';
}

function renderScreens() {
  const sel = $('#screen');
  sel.innerHTML = '';
  STATE.screens.forEach(s => {
    const o = document.createElement('option');
    o.value = s.id; o.textContent = s.name;
    if (s.id === STATE.current) o.selected = true;
    sel.appendChild(o);
  });
  sel.onchange = renderFields;
}
function curScreen() {
  const id = $('#screen').value;
  return STATE.screens.find(s => s.id === id);
}

function renderFields() {
  const box = $('#fields');
  box.innerHTML = '';
  const s = curScreen();
  if (!s) return;
  if (!s.fields.length) {
    box.innerHTML = '<div class="hint">这个画面没有可配置项。</div>';
    return;
  }
  s.fields.forEach(f => {
    const lab = document.createElement('label');
    let extra = '';
    if (f.type === 'secret') extra = f.set ? '(已设置,留空=不改)' : '(未设置)';
    lab.textContent = f.label + extra;
    box.appendChild(lab);

    let el;
    if (f.type === 'select') {
      el = document.createElement('select');
      (f.options || '').split(',').forEach(opt => {
        const parts = opt.split(':');
        const v = parts[0], l = parts[1] || parts[0];
        const o = document.createElement('option');
        o.value = v; o.textContent = l;
        if (v === f.value) o.selected = true;
        el.appendChild(o);
      });
    } else {
      el = document.createElement('input');
      el.type = (f.type === 'secret') ? 'password' : 'text';
      if (f.type === 'secret') el.placeholder = '••••••';
      else el.value = f.value || '';
    }
    el.dataset.key = f.key;
    el.className = 'fld';
    box.appendChild(el);
  });
}

async function post(url, obj) {
  return fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(obj) });
}

async function applyScreen() {
  const id = $('#screen').value;
  $('#screenMsg').textContent = '切换中…';
  await post('/api/screen', { id });
  STATE.current = id;
  $('#screenMsg').textContent = '已切换到:' + curScreen().name;
}

async function saveCfg() {
  const s = curScreen();
  const fields = {};
  document.querySelectorAll('#fields .fld').forEach(el => { fields[el.dataset.key] = el.value; });
  $('#cfgMsg').textContent = '保存中…';
  await post('/api/config', { screen: s.id, fields });
  $('#cfgMsg').textContent = '已保存' + (s.id === STATE.current ? '(已应用到屏幕)' : '');
  setTimeout(load, 600);
}

async function saveWifi() {
  $('#wifiMsg').textContent = '保存中,设备将重启…';
  try { await post('/api/wifi', { ssid: $('#wifiSsid').value, pass: $('#wifiPass').value }); } catch (e) {}
  $('#wifiMsg').textContent = '已保存,设备重启中…';
}

async function check() {
  $('#otaStatus').textContent = '检查中…';
  try { await fetch('/checkupdate', { method: 'POST' }); $('#otaStatus').textContent = '已触发,有新版会自动升级并重启'; }
  catch (e) { $('#otaStatus').textContent = '设备可能正在升级重启…'; }
}

async function upload() {
  const f = $('#fw').files[0];
  if (!f) { $('#otaMsg').textContent = '请先选择 firmware.bin'; return; }
  $('#otaMsg').textContent = '上传中,请勿断电…';
  try { const r = await fetch('/ota', { method: 'POST', body: f }); $('#otaMsg').textContent = r.ok ? '更新成功,设备重启中…' : '失败'; }
  catch (e) { $('#otaMsg').textContent = '上传已结束(设备可能正在重启)'; }
}

$('#gear').onclick = () => { $('#wifiSsid').value = STATE ? STATE.wifi_ssid : ''; $('#wifiPass').value = ''; $('#modal').hidden = false; };
$('#closeModal').onclick = () => { $('#modal').hidden = true; };
$('#modal').onclick = e => { if (e.target === $('#modal')) $('#modal').hidden = true; };
$('#applyScreen').onclick = applyScreen;
$('#saveCfg').onclick = saveCfg;
$('#saveWifi').onclick = saveWifi;
$('#checkBtn').onclick = check;
$('#uploadBtn').onclick = upload;

load();
