#pragma once

static const char BOTTLE_PAGE[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Bottle</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; font-family: Georgia, serif; background: #101418; color: #f4f1ea; }
  main { max-width: 28rem; margin: 0 auto; padding: 1.5rem 1.2rem 3rem; }
  h1 { font-weight: 500; font-size: 1.4rem; margin: 0 0 0.2rem; }
  .muted { color: #b7b1a6; font-family: system-ui, sans-serif; font-size: 0.92rem; }
  .weight { font-size: 4.2rem; line-height: 1; margin: 1.2rem 0 0.3rem; }
  .unit { font-size: 1.2rem; color: #b7b1a6; }
  button, input, select { font: inherit; }
  button { background: #f4f1ea; color: #101418; border: 0; border-radius: 999px; padding: 0.7rem 1.1rem; }
  form { display: grid; gap: 0.8rem; margin-top: 1.6rem; font-family: system-ui, sans-serif; }
  label { display: grid; gap: 0.25rem; font-size: 0.92rem; }
  input, select { background: #1c232b; color: #f4f1ea; border: 1px solid #3a4450; border-radius: 0.6rem; padding: 0.55rem 0.7rem; }
  .row { display: flex; gap: 0.8rem; align-items: center; }
  .check { display: flex; gap: 0.5rem; align-items: center; }
  #phones, #wifis, #saved-wifis { display: grid; gap: 0.5rem; margin-top: 0.6rem; }
  #phones button, #wifis button { text-align: left; }
  .net { display: flex; justify-content: space-between; align-items: center; gap: 0.6rem; background: #1c232b; border: 1px solid #3a4450; border-radius: 0.6rem; padding: 0.45rem 0.7rem; }
  .net button { padding: 0.35rem 0.75rem; font-size: 0.85rem; }
</style>
</head>
<body>
<main>
  <h1>Bottle</h1>
  <p id="where" class="muted">Connecting…</p>
  <p class="weight"><span id="grams">--</span> <span class="unit">g</span></p>
  <p id="place" class="muted">Waiting for the scale…</p>
  <p class="muted">Today <span id="today">0</span> ml · Last drink <span id="last">none yet</span></p>
  <p class="row"><button type="button" id="tare">Zero the empty scale</button></p>
  <p class="muted" id="phone-now">No phone saved yet.</p>
  <p class="row"><button type="button" id="find">Search nearby phones</button></p>
  <div id="phones"></div>
  <form id="settings" method="post" action="/settings">
    <label class="check"><input id="dnd" name="dnd" type="checkbox"> Do not disturb</label>
    <label>Light color <input id="color" name="color" type="color"></label>
    <label>Time zone
      <select id="tz" name="tz">
        <option value="-720">Baker Island (UTC-12)</option>
        <option value="-660">Pago Pago (UTC-11)</option>
        <option value="-600">Honolulu (UTC-10)</option>
        <option value="-540">Anchorage (UTC-9)</option>
        <option value="-480">Los Angeles (UTC-8)</option>
        <option value="-420">Denver (UTC-7)</option>
        <option value="-360">Chicago (UTC-6)</option>
        <option value="-300">New York (UTC-5)</option>
        <option value="-240">Halifax (UTC-4)</option>
        <option value="-210">St. John's (UTC-3:30)</option>
        <option value="-180">Sao Paulo (UTC-3)</option>
        <option value="-120">Noronha (UTC-2)</option>
        <option value="-60">Azores (UTC-1)</option>
        <option value="0">London (UTC+0)</option>
        <option value="60">Paris (UTC+1)</option>
        <option value="120">Cairo (UTC+2)</option>
        <option value="180">Moscow (UTC+3)</option>
        <option value="210">Tehran (UTC+3:30)</option>
        <option value="240">Dubai (UTC+4)</option>
        <option value="270">Kabul (UTC+4:30)</option>
        <option value="300">Karachi (UTC+5)</option>
        <option value="330">Kolkata (UTC+5:30)</option>
        <option value="345">Kathmandu (UTC+5:45)</option>
        <option value="360">Dhaka (UTC+6)</option>
        <option value="390">Yangon (UTC+6:30)</option>
        <option value="420">Bangkok (UTC+7)</option>
        <option value="480">Singapore (UTC+8)</option>
        <option value="540">Tokyo (UTC+9)</option>
        <option value="570">Adelaide (UTC+9:30)</option>
        <option value="600">Sydney (UTC+10)</option>
        <option value="660">Noumea (UTC+11)</option>
        <option value="720">Auckland (UTC+12)</option>
        <option value="765">Chatham Islands (UTC+12:45)</option>
        <option value="780">Nuku'alofa (UTC+13)</option>
        <option value="840">Kiritimati (UTC+14)</option>
      </select>
    </label>
    <label>Quiet from <input id="start" name="start" type="time"></label>
    <label>Quiet until <input id="end" name="end" type="time"></label>
    <label>Check every <input id="interval" name="interval" type="number" min="1" max="240"> minutes</label>
    <label>Beep
      <select id="beep" name="beep">
        <option value="0">Off</option>
        <option value="1">Single</option>
        <option value="2">Beep beep</option>
        <option value="3">Long</option>
      </select>
    </label>
    <input id="mac" name="mac" type="hidden">
    <p class="row"><button type="button" id="scan-wifi">Search nearby Wi-Fi</button></p>
    <div id="wifis"></div>
    <p class="muted" id="wifi-saved-label">No Wi-Fi saved yet.</p>
    <div id="saved-wifis"></div>
    <label>Wi-Fi name (adds or updates up to 4 places) <input id="ssid" name="ssid" autocomplete="off" placeholder="Pick from search or type name"></label>
    <label>Wi-Fi password <input id="pass" name="pass" type="password" placeholder="Leave blank to keep saved password" autocomplete="new-password"></label>
    <label class="check"><input name="clear_wifi" type="checkbox"> Forget all saved Wi-Fi</label>
    <label>When the setup network is on
      <select id="ap" name="ap">
        <option value="0">Only if home Wi-Fi did not connect at startup</option>
        <option value="1">Whenever home Wi-Fi drops</option>
        <option value="2">Always, even on home Wi-Fi</option>
        <option value="3">Only after a long button press</option>
        <option value="4">For five minutes if startup Wi-Fi fails</option>
      </select>
    </label>
    <button type="submit">Save</button>
    <p class="muted" id="saved"></p>
  </form>
</main>
<script>
function hm(m){const h=Math.floor(m/60), n=m%60; return String(h).padStart(2,"0")+":"+String(n).padStart(2,"0");}
function hex(r,g,b){return "#"+[r,g,b].map(v=>Number(v).toString(16).padStart(2,"0")).join("");}
let formReady = false;
let refreshing = false;
let misses = 0;
async function get(url, opts, ms){
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), ms || 4000);
  try {
    const r = await fetch(url, Object.assign({cache:"no-store", signal:ctl.signal}, opts || {}));
    if (!r.ok) throw new Error(String(r.status));
    return r;
  } finally {
    clearTimeout(timer);
  }
}
function post(url, body, ms){
  return get(url, {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:body || ""}, ms);
}
function renderSavedWifis(nets, activeSsid, connected){
  const label = document.getElementById("wifi-saved-label");
  const box = document.getElementById("saved-wifis");
  box.innerHTML = "";
  const list = Array.isArray(nets) ? nets : [];
  if (!list.length) {
    label.textContent = "No Wi-Fi saved yet.";
    return;
  }
  label.textContent = "Saved Wi-Fi (" + list.length + "/4) — connects automatically when in range:";
  list.forEach((name) => {
    const row = document.createElement("div");
    row.className = "net";
    const span = document.createElement("span");
    span.textContent = name + (connected && name === activeSsid ? " (connected)" : "");
    span.onclick = () => { document.getElementById("ssid").value = name; };
    const del = document.createElement("button");
    del.type = "button";
    del.textContent = "Remove";
    del.onclick = async () => {
      try {
        await post("/wifi/remove", "ssid="+encodeURIComponent(name));
        formReady = false;
        refresh();
      } catch (e) {
        label.textContent = "Remove did not reach the bottle. Try again.";
      }
    };
    row.appendChild(span);
    row.appendChild(del);
    box.appendChild(row);
  });
}
async function refresh(){
  if (refreshing) return Promise.resolve();
  refreshing = true;
  try {
    const s = await (await get("/api", null, 3000)).json();
    misses = 0;
    document.getElementById("grams").textContent = s.grams == null ? "--" : s.grams;
    document.getElementById("place").textContent = s.fault ? "Scale fault: check the load cell wires to the HX711." : (s.grams == null ? "No weight reading." : (s.missing ? "Bottle is off the base." : "Bottle is on the base."));
    document.getElementById("today").textContent = s.today;
    document.getElementById("last").textContent = s.last == null ? "none yet" : (s.last + " ml");
    document.getElementById("phone-now").textContent = s.mac ? ("Saved phone " + s.mac) : "No phone saved yet.";
    const where = s.wifi ? ("On Wi-Fi (" + (s.ssid || "") + ") at http://" + s.ip + "/ or http://bottle.local/") : ("Setup network. Open this page at " + s.ip);
    const who = s.watching ? (s.home ? " Phone is here." : " Phone is away, so reminders stay off.") : "";
    document.getElementById("where").textContent = where + who;
    if (!formReady) {
      formReady = true;
      document.getElementById("dnd").checked = !!s.dnd;
      document.getElementById("color").value = hex(s.r, s.g, s.b);
      document.getElementById("start").value = hm(s.start);
      document.getElementById("end").value = hm(s.end);
      document.getElementById("interval").value = s.interval;
      const tz = document.getElementById("tz");
      if ([...tz.options].some(o => o.value === String(s.tz))) tz.value = String(s.tz);
      const beep = document.getElementById("beep");
      if ([...beep.options].some(o => o.value === String(s.beep))) beep.value = String(s.beep);
      document.getElementById("mac").value = s.mac || "";
      document.getElementById("ssid").value = "";
      document.getElementById("ap").value = String(s.ap);
      renderSavedWifis(s.nets, s.ssid, s.wifi);
    }
  } catch (e) {
    if (++misses >= 3) document.getElementById("where").textContent = "Weak Wi-Fi to the bottle. Showing the last weight, retrying…";
  }
  refreshing = false;
}
document.getElementById("tare").onclick = async () => {
  if (!confirm("Take the bottle off first. Zero the empty scale now?")) return;
  const place = document.getElementById("place");
  try {
    await post("/tare", "", 8000);
    place.textContent = "Scale zeroed.";
  } catch (e) {
    place.textContent = "Zero did not reach the bottle. Try again.";
  }
  refresh();
};
document.getElementById("find").onclick = async () => {
  const box = document.getElementById("phones");
  box.textContent = "Searching… keep this phone close, with Bluetooth on.";
  let list;
  try {
    list = await (await get("/phones", null, 20000)).json();
  } catch (e) {
    box.textContent = "Search did not finish. Try again.";
    return;
  }
  box.innerHTML = "";
  if (!list.length) {
    box.textContent = "No phones heard. Wake the phone, leave Bluetooth on, and search again.";
    return;
  }
  list.forEach((p) => {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = (p.name || "Nearby device") + "  " + p.mac;
    b.onclick = async () => {
      try {
        await post("/phone", "mac="+encodeURIComponent(p.mac));
      } catch (e) {
        box.textContent = "Saving the phone did not reach the bottle. Try again.";
        return;
      }
      document.getElementById("mac").value = p.mac;
      document.getElementById("phone-now").textContent = "Saved phone " + p.mac;
      box.textContent = "";
    };
    box.appendChild(b);
  });
};
document.getElementById("scan-wifi").onclick = async () => {
  const box = document.getElementById("wifis");
  box.textContent = "Scanning 2.4 GHz Wi-Fi…";
  let list;
  try {
    list = await (await get("/wifis", null, 15000)).json();
  } catch (e) {
    box.textContent = "Scan did not finish. Try again.";
    return;
  }
  box.innerHTML = "";
  if (!list.length) {
    box.textContent = "No 2.4 GHz networks found.";
    return;
  }
  list.forEach((w) => {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = w.ssid + " (" + w.rssi + " dBm" + (w.open ? ", open" : "") + ")";
    b.onclick = () => {
      document.getElementById("ssid").value = w.ssid;
      box.textContent = "Selected " + w.ssid + ". Enter password below and tap Save.";
      document.getElementById("pass").focus();
    };
    box.appendChild(b);
  });
};
document.getElementById("settings").onsubmit = async (ev) => {
  ev.preventDefault();
  const form = ev.target;
  const note = document.getElementById("saved");
  const body = new URLSearchParams(new FormData(form));
  body.set("js", "1");
  const wifiChange = !!body.get("ssid") || body.has("clear_wifi");
  note.textContent = "Saving…";
  try {
    await post("/settings", body.toString(), 8000);
    note.textContent = wifiChange ? "Saved. The bottle is reconnecting Wi-Fi; this page may pause." : "Saved.";
    document.getElementById("pass").value = "";
    form.querySelector('[name="clear_wifi"]').checked = false;
    formReady = false;
  } catch (e) {
    note.textContent = "Save did not reach the bottle. Try again.";
  }
};
function schedule(){ Promise.resolve(refresh()).finally(() => setTimeout(schedule, 1000)); }
schedule();
</script>
</body>
</html>)html";
