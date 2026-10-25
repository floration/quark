// Quark HUD Editor page: the built-in HUD elements (quark.cfg hud_<N>) and the scriptable widgets (hud.cfg) on a
// 1280x720 preview (the game's 720p units: everything scales with the window, so this is every resolution).
// Formats mirror the game: Settings::set / save (src/client/config.cpp) and HudWidgets::fromText / toText
// (src/client/hudwidgets.cpp). Element shapes mirror Game::drawHud* (src/client/game.cpp) closely, not exactly.
"use strict";
const W = 1280, H = 720;
const $ = (id) => document.getElementById(id);
const cv = $("cv"), g = cv.getContext("2d");

// ---- formats ---------------------------------------------------------------------------------------------------
const VALUES = ["health", "speed", "dash", "hook", "fps", "fpsmin", "ping", "loss", "time", "enemies", "teammates", "kills", "deaths", "score", "tickrate"];
const SAMPLE = {health: [100, 100], speed: [12, 40], dash: [2, 2], hook: [100, 100], fps: [240, 500], fpsmin: [180, 500], ping: [35, 200],
  loss: [0, 100], time: [125, 600], enemies: [3, 7], teammates: [2, 7], kills: [7, 30], deaths: [3, 30], score: [12, 50], tickrate: [1000, 1000]};
const STYLES = ["number", "bar", "gradbar", "icon", "iconbar"];
const ANCHORS = ["tl", "tr", "bl", "br", "c"];
const ANCHOR_NAMES = {tl: "top left", tr: "top right", bl: "bottom left", br: "bottom right", c: "centre"};
const ICONS = ["heart", "bolt", "hook", "skull", "star", "shield", "clock", "eye"];
const ICON_ROWS = [
  [".##.##.", "#######", "#######", ".#####.", "..###..", "...#...", "......."],
  ["...##..", "..##...", ".#####.", "...##..", "..##...", ".##....", "#......"],
  ["..###..", ".#...#.", ".....#.", "...##..", "...#...", "...#...", "..###.."],
  [".#####.", "#.###.#", "#######", ".##.##.", "..###..", "..#.#..", "......."],
  ["...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", "#.....#"],
  ["#######", "#######", "#######", ".#####.", ".#####.", "..###..", "...#..."],
  ["..###..", ".#..#..", "#...#.#", "#.###.#", "#.....#", ".#...#.", "..###.."],
  [".......", "..###..", ".#...#.", "#..#..#", ".#...#.", "..###..", "......."],
];
// Settings::HUD_* (config.h). 7 = the grapple launcher's own element (hud_7, from the grapple arm follow-up): until a
// quark.cfg has a hud_7 line the launcher follows the weapon model, as the game did before.
const ELEMENTS = [
  {i: 5, name: "Weapon model", kind: "view", hint: "First-person weapon. x / y move it, depth pushes it away, colour tints its accent parts."},
  {i: 7, name: "Grapple launcher", kind: "view", hint: "The launcher on your left arm (first person). Its own position, size and hide; tilt in degrees."},
  {i: 0, name: "Health", hint: "Number and bar, bottom left."},
  {i: 1, name: "Weapon name", hint: "Held weapon's name, bottom right."},
  {i: 2, name: "Cooldown bars", hint: "Weapon / dash / hook bars under the crosshair."},
  {i: 3, name: "Speed meter", hint: "Under the cooldown bars."},
  {i: 4, name: "Weapon slots", hint: "Slot list, bottom right."},
  {i: 6, name: "Key overlay", hint: "WASD / mouse keys; shown with the info block (hud_verbose) on."},
];
const SLOT_TEXT = ["1 ROCKET", "2 RIFLE", "3 LASER", "4 SHOTGUN", "5 SNIPER", "6 KNIFE"];
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const num = (v, d = 0) => { const f = parseFloat(v); return Number.isFinite(f) ? f : d; };
const fmt = (v) => { const r = Math.round(v * 1000) / 1000; return String(Object.is(r, -0) ? 0 : r); };

function parseElem(text) {
  const p = (text || "").split(",").map((s) => s.trim());
  return {x: num(p[0]), y: num(p[1]), scale: num(p[2], 1), custom: num(p[3]) !== 0, r: num(p[4], 1), g: num(p[5], 1), b: num(p[6], 1),
    hidden: num(p[7]) !== 0, z: num(p[8]), pitch: num(p[9]), yaw: num(p[10]), roll: num(p[11]), rest: p.slice(12), had: !!text};
}
function elemText(e) {
  const a = [e.x, e.y, e.scale, e.custom ? 1 : 0, e.r, e.g, e.b, e.hidden ? 1 : 0, e.z];
  if (e.pitch || e.yaw || e.roll || e.rest.length || e.tilt) a.push(e.pitch, e.yaw, e.roll);
  return a.map(fmt).concat(e.rest).join(",");
}
function parseRgba(s, def) {
  let c = def.slice();
  if (s.length >= 6 && s.indexOf(",") < 0) {
    let v = parseInt(s, 16) >>> 0; if (s.length === 6) v = ((v << 8) | 255) >>> 0;
    for (let i = 0; i < 4; i++) c[i] = ((v >>> (24 - 8 * i)) & 255) / 255;
  } else {
    const n = s.split(",").map(parseFloat).filter((f) => Number.isFinite(f));
    for (let i = 0; i < n.length && i < 4; i++) c[i] = n[i];
    if (n.some((f) => f > 1)) for (let i = 0; i < n.length && i < 4; i++) c[i] /= 255;
  }
  return c;
}
const hex = (c) => c.map((v) => Math.round(clamp(v, 0, 1) * 255).toString(16).padStart(2, "0")).join("");
function newWidget() { return {value: "health", style: "number", anchor: "tl", x: 0, y: 0, w: 120, h: 10, size: 1, color: [1, 1, 1, 1], low: [1, 0.2, 0.2, 1], max: 0, icon: 0, grade: false, label: ""}; }
function parseWidgets(text) {
  const out = [], errs = [];
  (text || "").split(/\r?\n/).forEach((raw, ln) => {
    const line = raw.split("#")[0].trim().split(/\s+/);
    if (line[0] !== "widget") return;
    const w = newWidget();
    for (const word of line.slice(1)) {
      const eq = word.indexOf("="); if (eq < 0) continue;
      const k = word.slice(0, eq), v = word.slice(eq + 1), f = num(v);
      if (k === "value") w.value = VALUES.includes(v) ? v : null;
      else if (k === "style") { if (STYLES.includes(v)) w.style = v; }
      else if (k === "anchor") { if (ANCHORS.includes(v)) w.anchor = v; }
      else if (["x", "y", "w", "h", "max"].includes(k)) w[k] = f;
      else if (k === "size") w.size = Math.max(0.25, f);
      else if (k === "color") w.color = parseRgba(v, w.color);
      else if (k === "low") { w.low = parseRgba(v, w.low); w.grade = true; }
      else if (k === "label") w.label = v.replace(/_/g, " ").toUpperCase();
      else if (k === "icon") { const i = ICONS.indexOf(v); if (i >= 0) w.icon = i; }
    }
    if (!w.value) errs.push("line " + (ln + 1) + ": unknown value"); else out.push(w);
  });
  return {list: out, errs};
}
function widgetsText(list) {
  let out = "# quark HUD widgets - widget value=<name> style=number|bar|gradbar|icon|iconbar anchor=tl|tr|bl|br|c\n" +
            "#   x= y= w= h= size= max= color=rrggbbaa low=rrggbbaa label=TEXT icon=heart|bolt|hook|skull|star|shield|clock|eye\n";
  for (const w of list) {
    out += `widget value=${w.value} style=${w.style} anchor=${w.anchor} x=${fmt(w.x)} y=${fmt(w.y)} w=${fmt(w.w)} h=${fmt(w.h)} size=${fmt(w.size)} max=${fmt(w.max)} color=${hex(w.color)} icon=${ICONS[w.icon]}`;
    if (w.grade) out += " low=" + hex(w.low);
    if (w.label) out += " label=" + w.label.replace(/ /g, "_");
    out += "\n";
  }
  return out;
}

// ---- state + history -------------------------------------------------------------------------------------------
const S = {elems: {}, widgets: [], keys: {hud_widgets: "1", hud_verbose: "0"}, fov: 100, sel: null, tab: "elements", profiles: [], saved: ""};
let undoStack = [], redoStack = [];
const snapshot = () => JSON.stringify({elems: S.elems, widgets: S.widgets, keys: S.keys});
function restore(j) { const o = JSON.parse(j); S.elems = o.elems; S.widgets = o.widgets; S.keys = o.keys; if (S.sel && S.sel.w != null && S.sel.w >= S.widgets.length) S.sel = null; renderAll(); }
let pending = null;
function change(fn, merge) {   // merge: a drag / slider in progress keeps one undo step
  if (!merge || !pending) { undoStack.push(snapshot()); if (undoStack.length > 200) undoStack.shift(); redoStack = []; }
  pending = merge ? true : null;
  fn(); renderAll();
}
function endMerge() { pending = null; }
function undo() { if (!undoStack.length) return; redoStack.push(snapshot()); restore(undoStack.pop()); }
function redo() { if (!redoStack.length) return; undoStack.push(snapshot()); restore(redoStack.pop()); }
const isDirty = () => snapshot() !== S.saved;
const elem = (i) => S.elems[i] || (S.elems[i] = parseElem(""));

// ---- geometry (720p units; s = 1) ------------------------------------------------------------------------------
g.textBaseline = "top";
function font(ts) { return `700 ${Math.round(8 * ts)}px Rubik, "Segoe UI", system-ui, sans-serif`; }
function textW(t, ts) { g.font = font(ts); return g.measureText(t).width; }
const textH = (ts) => 8 * ts;
const focal = () => (W / 2) / Math.tan((clamp(S.fov, 60, 140) * Math.PI / 180) / 2);
function project(p) { const d = Math.max(0.05, -p[2]); return [W / 2 + p[0] / d * focal(), H / 2 - p[1] / d * focal()]; }
function grappleElem() { const e = S.elems[7]; return e && e.had ? e : null; }
// a model: an outline of its box (camera space), turned by yaw / pitch (degrees) about its base
function modelBox(i) {
  const vm = elem(5);
  let e, base, near, len, wid, hgt, side;
  if (i === 5) { e = vm; side = 1; base = [0.2 + e.x * 0.0005, -0.17 - e.y * 0.0005, -0.55 - e.z * 0.001]; near = 0.08; len = 0.4; wid = 0.05; hgt = 0.07; }
  else { e = grappleElem() || vm; side = -1;   // (PROP_HOOK_ARM: housing to barrel ~0.42 at size 1)
    base = [-0.2 - e.x * 0.0005, -0.19 - e.y * 0.0005, -0.58 - e.z * 0.001]; near = 0.06; len = 0.36; wid = 0.07; hgt = 0.08; }
  const sc = e.scale * (i === 7 && !grappleElem() ? 0.7 : 1);
  const yaw = (e.yaw || 0) * Math.PI / 180 + (i === 7 ? -0.06 : 0), pit = (e.pitch || 0) * Math.PI / 180;
  const pts = [];
  for (const zz of [near, -len]) for (const xx of [-wid / 2, wid / 2]) for (const yy of [-hgt / 2, hgt / 2]) {
    let x = xx * sc, y = yy * sc, z = zz * sc;
    const y1 = y * Math.cos(pit) - z * Math.sin(pit), z1 = y * Math.sin(pit) + z * Math.cos(pit);
    const x2 = x * Math.cos(yaw) + z1 * Math.sin(yaw), z2 = -x * Math.sin(yaw) + z1 * Math.cos(yaw);
    pts.push(project([base[0] + x2, base[1] + y1, base[2] + z2]));
  }
  return {pts, e, side, depth: -base[2]};
}
function hullBox(pts) { const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]); const x = Math.min(...xs), y = Math.min(...ys); return [x, y, Math.max(...xs) - x, Math.max(...ys) - y]; }
function elemBox(i) {
  const e = elem(i), k = e.scale;
  switch (i) {
    case 0: return [24 + e.x, H + e.y - 70 * k, 200 * k, 50 * k];
    case 1: { const ts = Math.max(1, Math.round(1.5 * k)), tw = textW("ROCKET", ts); return [W - 24 + e.x - tw, H - 50 * k + e.y, tw, textH(ts)]; }
    case 2: { const lts = Math.max(0.75, 0.75 * k), bx = W / 2 + e.x - 45 * k, left = bx - textW("WEAPON", lts) - 6 * k; return [left, H / 2 + e.y + 36 * k - 1, bx + 90 * k + 1 - left, 32 * k + 5 * k + 2]; }
    case 3: { const lts = Math.max(0.75, 0.75 * k), bx = W / 2 + e.x - 45 * k, y = H / 2 + 84 * k + e.y, left = bx - textW("SPEED", lts) - 6 * k;
      return [left, y - 1, bx + 90 * k + 6 * k + textW("12.0", lts) - left, Math.max(5 * k + 2, textH(lts))]; }
    case 4: { const sts = Math.max(1, Math.round(0.75 * k)), x1 = W - 24 + e.x, y = H - 74 * k + e.y;
      let bx = x1; for (const t of SLOT_TEXT) bx -= textW(t, sts) + 14 * k; return [bx, y, x1 - bx, textH(sts)]; }
    case 6: { const u = 30 * k, gg = 4 * k, sz = 4 * u + 3 * gg; return [W / 2 - sz / 2 + e.x, H - 230 + e.y, sz, sz]; }
    case 5: case 7: return hullBox(modelBox(i).pts);
  }
  return [0, 0, 0, 0];
}
function widgetBox(w) {
  const k = w.size, ts = Math.max(1, Math.round(2 * w.size)), iw = 14 * k, bw = w.w * k;
  const v = SAMPLE[w.value][0];
  const fw = w.style === "number" ? textW(fmtVal(v), ts) : w.style === "icon" ? iw : w.style === "iconbar" ? iw + 4 * k + bw : bw;
  const fh = w.style === "number" ? textH(ts) : (w.style === "bar" || w.style === "gradbar") ? w.h * k : iw;
  let x = w.x, y = w.y;
  if (w.anchor === "tr") x = W - x - fw; else if (w.anchor === "bl") y = H - y - fh;
  else if (w.anchor === "br") { x = W - x - fw; y = H - y - fh; } else if (w.anchor === "c") { x = W / 2 + x - fw / 2; y = H / 2 + y - fh / 2; }
  return [x, y, fw, fh];
}
const fmtVal = (v) => (v !== Math.floor(v) && Math.abs(v) < 100) ? v.toFixed(1) : String(Math.round(v));
function setWidgetFromScreen(w, x, y, fw, fh) {   // HudWidgets::setFromScreen: re-anchor to the nearest corner
  const mx = x + fw / 2, my = y + fh / 2;
  if (Math.abs(mx - W / 2) < W * 0.15 && Math.abs(my - H / 2) < H * 0.15) { w.anchor = "c"; w.x = mx - W / 2; w.y = my - H / 2; }
  else { const right = mx > W / 2, bottom = my > H / 2; w.anchor = (bottom ? "b" : "t") + (right ? "r" : "l");
    w.x = right ? W - x - fw : x; w.y = bottom ? H - y - fh : y; }
  w.x = Math.round(w.x); w.y = Math.round(w.y);
}

// ---- drawing ---------------------------------------------------------------------------------------------------
const rgba = (c, a) => `rgba(${Math.round(c[0] * 255)},${Math.round(c[1] * 255)},${Math.round(c[2] * 255)},${a == null ? (c[3] == null ? 1 : c[3]) : a})`;
function txt(x, y, ts, t, c) { g.font = font(ts); g.fillStyle = c; g.fillText(t, x, y); }
function tint(e, def) { return e.custom ? [e.r, e.g, e.b, def[3]] : def; }
const FRIEND = [0.35, 0.65, 1, 1], WHITE = [1, 1, 1, 1], GREY = [0.6, 0.62, 0.66, 0.9];
function drawScene() {
  const sky = g.createLinearGradient(0, 0, 0, H); sky.addColorStop(0, "#3a4656"); sky.addColorStop(0.55, "#59636e"); sky.addColorStop(0.551, "#3b3a37"); sky.addColorStop(1, "#24231f");
  g.fillStyle = sky; g.fillRect(0, 0, W, H);
  g.fillStyle = "rgba(0,0,0,.18)"; for (let i = 0; i < 6; i++) g.fillRect(140 + i * 180, 250 - (i % 3) * 30, 90, 150 + (i % 3) * 30);
  if ($("showGrid").checked) { g.strokeStyle = "rgba(255,255,255,.07)"; g.lineWidth = 1; g.beginPath();
    for (let x = 0; x <= W; x += 40) { g.moveTo(x + 0.5, 0); g.lineTo(x + 0.5, H); } for (let y = 0; y <= H; y += 40) { g.moveTo(0, y + 0.5); g.lineTo(W, y + 0.5); } g.stroke();
    g.strokeStyle = "rgba(90,169,255,.25)"; g.beginPath(); g.moveTo(W / 2 + 0.5, 0); g.lineTo(W / 2 + 0.5, H); g.moveTo(0, H / 2 + 0.5); g.lineTo(W, H / 2 + 0.5); g.stroke(); }
  g.fillStyle = "#fff"; g.fillRect(W / 2 - 12, H / 2 - 1, 8, 2); g.fillRect(W / 2 + 4, H / 2 - 1, 8, 2); g.fillRect(W / 2 - 1, H / 2 - 12, 2, 8); g.fillRect(W / 2 - 1, H / 2 + 4, 2, 8);
}
function drawModel(i, sel) {
  const m = modelBox(i), p = m.pts, e = m.e;
  const faces = [[0, 1, 3, 2], [4, 5, 7, 6], [0, 1, 5, 4], [2, 3, 7, 6], [0, 2, 6, 4], [1, 3, 7, 5]];
  const col = i === 5 ? (e.custom ? [e.r, e.g, e.b] : [0.7, 0.75, 0.82]) : [0.62, 0.64, 0.68];
  g.lineWidth = 1.5;
  for (const f of faces) { g.beginPath(); f.forEach((k, j) => j ? g.lineTo(p[k][0], p[k][1]) : g.moveTo(p[k][0], p[k][1])); g.closePath();
    g.fillStyle = rgba(col, 0.16); g.fill(); g.strokeStyle = rgba(col, 0.8); g.stroke(); }
  const [bx, by, bw] = hullBox(p);
  txt(bx + 4, by - 14, 1, i === 5 ? "WEAPON" : "GRAPPLE" + (grappleElem() ? "" : " (FOLLOWS WEAPON)"), sel ? "#5aa9ff" : "rgba(255,255,255,.6)");
}
function drawElem(i) {
  const e = elem(i), k = e.scale;
  if (i === 5 || i === 7) return drawModel(i, S.sel && S.sel.e === i);
  if (i === 0) { const [x, y] = elemBox(0), ts = Math.max(1, Math.round(2 * k)); txt(x, y, ts, "100", rgba(tint(e, WHITE)));
    g.fillStyle = "rgba(0,0,0,.5)"; g.fillRect(x, y + 42 * k, 200 * k, 8 * k); g.fillStyle = rgba(tint(e, FRIEND)); g.fillRect(x, y + 42 * k, 200 * k, 8 * k); }
  if (i === 1) { const [x, y] = elemBox(1); txt(x, y, Math.max(1, Math.round(1.5 * k)), "ROCKET", rgba(tint(e, WHITE))); }
  if (i === 2) { const lts = Math.max(0.75, 0.75 * k), bw = 90 * k, bh = 5 * k, bx = W / 2 + e.x - bw / 2, bcy = H / 2 + e.y;
    [["WEAPON", 36], ["DASH", 52], ["HOOK", 68]].forEach(([l, dy]) => { const y = bcy + dy * k;
      g.fillStyle = "rgba(0,0,0,.8)"; g.fillRect(bx - 1, y - 1, bw + 2, bh + 2); g.fillStyle = rgba(tint(e, FRIEND));
      if (l === "DASH") { const pw = (bw - 4 * k) / 2; g.fillRect(bx, y, pw, bh); g.fillRect(bx + pw + 4 * k, y, pw, bh); } else g.fillRect(bx, y, bw, bh);
      txt(bx - textW(l, lts) - 6 * k, y + (bh - textH(lts)) / 2, lts, l, "#fff"); }); }
  if (i === 3) { const lts = Math.max(0.75, 0.75 * k), bw = 90 * k, bh = 5 * k, bx = W / 2 + e.x - bw / 2, y = H / 2 + 84 * k + e.y, ly = y + (bh - textH(lts)) / 2;
    const f = 1 - Math.exp(-12 / 20); g.fillStyle = "rgba(0,0,0,.8)"; g.fillRect(bx - 1, y - 1, bw + 2, bh + 2);
    g.fillStyle = rgba(tint(e, [1, 0.85 - 0.6 * f, 0.3 - 0.2 * f, 0.95])); g.fillRect(bx, y, bw * f, bh);
    txt(bx - textW("SPEED", lts) - 6 * k, ly, lts, "SPEED", "#fff"); txt(bx + bw + 6 * k, ly, lts, "12.0", "#fff"); }
  if (i === 4) { const sts = Math.max(1, Math.round(0.75 * k)), y = H - 74 * k + e.y; let bx = W - 24 + e.x;
    SLOT_TEXT.forEach((t, j) => { bx -= textW(t, sts) + 14 * k; txt(bx, y, sts, t, j === 0 ? rgba(tint(e, FRIEND)) : "rgba(217,222,230,.9)"); }); }
  if (i === 6) { const u = 30 * k, gg = 4 * k, [ox, oy] = elemBox(6), ts = Math.max(1, Math.round(0.5 * k));
    const keys = [[1, 0, 1, "W"], [0, 1, 1, "A"], [1, 1, 1, "S"], [2, 1, 1, "D"], [3, 0, 1, "LMB"], [3, 1, 1, "RMB"], [0, 2, 2, "JUMP"], [2, 2, 2, "CROUCH"], [0, 3, 2, "DASH"], [2, 3, 2, "HOOK"]];
    for (const [c, r, wu, l] of keys) { const x = ox + c * (u + gg), y = oy + r * (u + gg), w = wu * u + (wu - 1) * gg;
      g.fillStyle = "rgba(13,15,20,.55)"; g.fillRect(x, y, w, u); txt(x + (w - textW(l, ts)) / 2, y + (u - textH(ts)) / 2, ts, l, "rgba(217,222,230,.9)"); } }
}
function drawWidget(w) {
  const [x, y, fw, fh] = widgetBox(w), k = w.size, [v, mx0] = SAMPLE[w.value], mx = w.max > 0 ? w.max : mx0, f = mx > 0 ? clamp(v / mx, 0, 1) : 0;
  const mix = (t) => w.low.map((l, j) => l + (w.color[j] - l) * t), graded = w.grade ? mix(f) : w.color;
  const ts = Math.max(1, Math.round(2 * w.size)), iw = 14 * k, bw = w.w * k;
  if (w.label) { const lts = Math.max(1, Math.round(w.size)), lw = textW(w.label, lts);
    const lx = (w.anchor === "tr" || w.anchor === "br") ? x + fw - lw : w.anchor === "c" ? x + (fw - lw) / 2 : x;
    txt(lx, y - textH(lts) - 3 * k, lts, w.label, rgba(w.color, 0.8)); }
  const icon = (ix, iy, c) => { const p = 2 * k; g.fillStyle = rgba(c); ICON_ROWS[w.icon].forEach((row, r) => { for (let col = 0; col < 7; col++) if (row[col] === "#") g.fillRect(ix + col * p, iy + r * p, p, p); }); };
  const bar = (bx, by, bwid, bh, c) => { g.fillStyle = "rgba(0,0,0,.7)"; g.fillRect(bx - 1, by - 1, bwid + 2, bh + 2); g.fillStyle = rgba(c); g.fillRect(bx, by, bwid * f, bh); };
  if (w.style === "number") txt(x, y, ts, fmtVal(v), rgba(graded));
  else if (w.style === "bar") bar(x, y, bw, fh, w.color);
  else if (w.style === "gradbar") bar(x, y, bw, fh, mix(f));
  else if (w.style === "icon") icon(x, y, [w.color[0], w.color[1], w.color[2], w.color[3] * (0.15 + 0.85 * f)]);
  else { icon(x, y, graded); bar(x + iw + 4 * k, y + iw / 2 - w.h * k / 2, bw, w.h * k, graded); }
}
function drawVerbose() {
  const lines = ["12:40", "QUARK 1.19.0", "FPS 240  MIN 180", "SPEED 12.0", "ALIVE 4"];
  lines.forEach((l, j) => { const tw = textW(l, 1); txt(W - 12 - tw, 10 + j * 11, 1, l, "rgba(255,255,255,.75)"); });
}
function outline(b, color, dashed) {
  g.save(); g.strokeStyle = color; g.lineWidth = 1.5; if (dashed) g.setLineDash([5, 4]); g.strokeRect(b[0] - 2.5, b[1] - 2.5, b[2] + 5, b[3] + 5); g.restore();
}
function draw() {
  drawScene();
  const showHidden = $("showHidden").checked;
  for (const el of ELEMENTS) {
    const e = elem(el.i);
    if (el.i === 7 && !grappleElem() && elem(5).hidden) continue;   // (follows the weapon's hide)
    if (el.i === 6 && S.keys.hud_verbose !== "1" && !(S.sel && S.sel.e === 6)) continue;
    if (e.hidden) { if (showHidden) outline(elemBox(el.i), "rgba(255,255,255,.35)", true); continue; }
    drawElem(el.i);
  }
  if (S.keys.hud_verbose === "1") drawVerbose();
  if (S.keys.hud_widgets === "1") S.widgets.forEach(drawWidget);
  else if (showHidden) S.widgets.forEach((w) => outline(widgetBox(w), "rgba(255,255,255,.35)", true));
  if (S.sel) outline(S.sel.w != null ? widgetBox(S.widgets[S.sel.w]) : elemBox(S.sel.e), "#5aa9ff");
}

// ---- hit testing + drag ------------------------------------------------------------------------------------------
function canvasPt(ev) { const r = cv.getBoundingClientRect(); return [(ev.clientX - r.left) * W / r.width, (ev.clientY - r.top) * H / r.height]; }
const inBox = (p, b, pad = 4) => p[0] >= b[0] - pad && p[0] <= b[0] + b[2] + pad && p[1] >= b[1] - pad && p[1] <= b[1] + b[3] + pad;
function hit(p) {
  if (S.keys.hud_widgets === "1" || $("showHidden").checked) for (let j = S.widgets.length - 1; j >= 0; j--) if (inBox(p, widgetBox(S.widgets[j]))) return {w: j};
  const order = ELEMENTS.slice().reverse();
  for (const el of order) {
    const e = elem(el.i); if (e.hidden && !$("showHidden").checked) continue;
    if (el.i === 7 && !grappleElem()) continue;          // (no own element yet: drag the weapon)
    if (el.i === 6 && S.keys.hud_verbose !== "1") continue;
    if (inBox(p, elemBox(el.i))) return {e: el.i};
  }
  return null;
}
let drag = null;
cv.addEventListener("mousedown", (ev) => {
  const p = canvasPt(ev), h = hit(p);
  S.sel = h; if (h) { S.tab = h.w != null ? "widgets" : "elements"; }
  drag = h ? {start: p, h, orig: JSON.parse(JSON.stringify(h.w != null ? S.widgets[h.w] : elem(h.e))), box: h.w != null ? widgetBox(S.widgets[h.w]) : null} : null;
  renderAll();
});
window.addEventListener("mousemove", (ev) => {
  if (!drag) { const h = hit(canvasPt(ev)); cv.style.cursor = h ? "move" : "default"; return; }
  const p = canvasPt(ev), dx = p[0] - drag.start[0], dy = p[1] - drag.start[1];
  change(() => {
    if (drag.h.w != null) { const w = S.widgets[drag.h.w]; Object.assign(w, JSON.parse(JSON.stringify(drag.orig)));
      setWidgetFromScreen(w, drag.box[0] + dx, drag.box[1] + dy, drag.box[2], drag.box[3]); }
    else { const e = elem(drag.h.e), o = drag.orig;
      if (drag.h.e === 5 || drag.h.e === 7) { const k = 0.0005 * focal() / modelBox(drag.h.e).depth, sx = drag.h.e === 7 ? -1 : 1;
        e.x = Math.round(clamp(o.x + sx * dx / k, -1280, 1280)); e.y = Math.round(clamp(o.y + dy / k, -720, 720)); }
      else { e.x = Math.round(clamp(o.x + dx, -1280, 1280)); e.y = Math.round(clamp(o.y + dy, -720, 720)); }
      e.had = true; }
  }, true);
});
window.addEventListener("mouseup", () => { if (drag) { drag = null; endMerge(); } });
cv.addEventListener("wheel", (ev) => {
  if (!S.sel) return; ev.preventDefault();
  const d = ev.deltaY < 0 ? 1.05 : 1 / 1.05;
  change(() => { if (S.sel.w != null) { const w = S.widgets[S.sel.w]; w.size = Math.round(clamp(w.size * d, 0.25, 8) * 100) / 100; }
    else { const e = elem(S.sel.e); e.scale = Math.round(clamp(e.scale * d, 0.5, 3) * 100) / 100; e.had = true; } }, true);
  clearTimeout(cv._wt); cv._wt = setTimeout(endMerge, 400);
}, {passive: false});
window.addEventListener("keydown", (ev) => {
  const typing = /INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName);
  if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === "s") { ev.preventDefault(); save(); return; }
  if (typing) return;
  if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === "z") { ev.preventDefault(); ev.shiftKey ? redo() : undo(); return; }
  if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === "y") { ev.preventDefault(); redo(); return; }
  const arrows = {ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1]};
  if (S.sel && arrows[ev.key]) { ev.preventDefault(); const st = ev.shiftKey ? 10 : 1, [ax, ay] = arrows[ev.key];
    change(() => { if (S.sel.w != null) { const w = S.widgets[S.sel.w], b = widgetBox(w); setWidgetFromScreen(w, b[0] + ax * st, b[1] + ay * st, b[2], b[3]); }
      else { const e = elem(S.sel.e), sx = S.sel.e === 7 ? -1 : 1; e.x += sx * ax * st; e.y += ay * st; e.had = true; } });
  }
  if (S.sel && S.sel.w != null && (ev.key === "Delete" || ev.key === "Backspace")) { ev.preventDefault(); delWidget(); }
});

// ---- side panel ------------------------------------------------------------------------------------------------
function h(tag, attrs, ...kids) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) { if (k.startsWith("on")) n.addEventListener(k.slice(2), v); else if (v === true) n.setAttribute(k, ""); else if (v !== false && v != null) n.setAttribute(k, v); }
  for (const c of kids) n.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return n;
}
function numField(label, get, set, min, max, step, slider) {
  const inp = h("input", {type: "number", min, max, step, value: fmt(get())});
  const rng = slider ? h("input", {type: "range", min, max, step, value: get()}) : null;
  const apply = (v, merge) => { v = clamp(num(v, get()), min, max); change(() => set(v), merge); };
  inp.addEventListener("change", () => apply(inp.value));
  if (rng) { rng.addEventListener("input", () => apply(rng.value, true)); rng.addEventListener("change", endMerge); }
  return [h("label", {}, label), h("div", {class: "pair"}, inp, ...(rng ? [rng] : []))];
}
function colorField(label, get, set) {
  const c = get(); const inp = h("input", {type: "color", value: "#" + hex(c.slice(0, 3))});
  inp.addEventListener("input", () => { const v = inp.value.slice(1); change(() => set([0, 2, 4].map((o) => parseInt(v.substr(o, 2), 16) / 255)), true); });
  inp.addEventListener("change", endMerge);
  return [h("label", {}, label), h("div", {class: "pair"}, inp)];
}
function checkField(label, get, set) {
  const inp = h("input", {type: "checkbox", checked: !!get()}); inp.addEventListener("change", () => change(() => set(inp.checked)));
  return [h("label", {}, label), h("div", {class: "pair"}, inp)];
}
function selectField(label, opts, get, set) {
  const sel = h("select", {}, ...opts.map(([v, t]) => h("option", {value: v, selected: String(v) === String(get())}, t)));
  sel.addEventListener("change", () => change(() => set(sel.value)));
  return [h("label", {}, label), sel];
}
function renderElements() {
  const list = $("elList"); list.replaceChildren();
  for (const el of ELEMENTS) {
    const e = elem(el.i), own = el.i !== 7 || grappleElem();
    list.append(h("div", {class: "it" + (S.sel && S.sel.e === el.i ? " sel" : ""), onclick: () => { S.sel = {e: el.i}; renderAll(); }},
      h("span", {class: e.hidden ? "off" : ""}, el.name), h("span", {class: "grow"}), h("span", {class: "small muted"}, own ? (e.hidden ? "hidden" : "") : "follows weapon")));
  }
  const ins = $("elInspector"); ins.replaceChildren();
  if (!S.sel || S.sel.e == null) { ins.append(h("p", {class: "hint small muted"}, "Pick an element in the list or on the preview.")); return; }
  const el = ELEMENTS.find((x) => x.i === S.sel.e), e = elem(el.i), mark = () => { e.had = true; };
  ins.append(h("h4", {}, el.name + "  (hud_" + el.i + ")"), h("p", {class: "hint small muted"}, el.hint));
  if (el.i === 7 && !grappleElem()) {
    ins.append(h("p", {class: "hint small warn"}, "This quark.cfg has no hud_7 line yet, so the launcher follows the weapon model (mirrored, at 70% size). Give it its own settings to place it apart; a game build older than the grapple follow-up ignores them."),
      h("div", {class: "hint"}, h("button", {onclick: () => change(() => { const vm = elem(5); S.elems[7] = Object.assign(parseElem(""), {scale: vm.scale, hidden: vm.hidden, had: true}); })}, "Give it its own settings")));
    return;
  }
  ins.append(...checkField("Hidden", () => e.hidden, (v) => { e.hidden = v; mark(); }));
  ins.append(...numField("X", () => e.x, (v) => { e.x = v; mark(); }, -1280, 1280, 1, true));
  ins.append(...numField("Y", () => e.y, (v) => { e.y = v; mark(); }, -720, 720, 1, true));
  ins.append(...numField(el.kind === "view" ? "Size" : "Scale", () => e.scale, (v) => { e.scale = v; mark(); }, 0.5, 3, 0.05, true));
  if (el.kind === "view") {
    ins.append(...numField("Depth (z)", () => e.z, (v) => { e.z = v; mark(); }, -300, 300, 1, true));
    ins.append(h("h4", {}, "Tilt (degrees; needs the grapple follow-up build)"));
    ins.append(...numField("Pitch", () => e.pitch, (v) => { e.pitch = v; e.tilt = true; mark(); }, -90, 90, 1, true));
    ins.append(...numField("Yaw", () => e.yaw, (v) => { e.yaw = v; e.tilt = true; mark(); }, -90, 90, 1, true));
    ins.append(...numField("Roll", () => e.roll, (v) => { e.roll = v; e.tilt = true; mark(); }, -180, 180, 1, true));
  }
  if (el.i !== 7) {
    ins.append(...checkField("Own colour", () => e.custom, (v) => { e.custom = v; mark(); }));
    if (e.custom) ins.append(...colorField("Colour", () => [e.r, e.g, e.b], (c) => { [e.r, e.g, e.b] = c; mark(); }));
  }
  ins.append(h("div", {class: "hint row"}, h("button", {onclick: () => change(() => { const keep = e.rest; S.elems[el.i] = Object.assign(parseElem(""), {rest: keep, had: true}); })}, "Reset to default")));
}
function widgetName(w) { return (w.label ? w.label + " - " : "") + w.value + " " + w.style; }
function renderWidgets() {
  const list = $("wList"); list.replaceChildren();
  if (!S.widgets.length) list.append(h("div", {class: "empty small"}, "No widgets. Add one: any game value as a number, bar, colour-graded bar, icon or icon + bar."));
  S.widgets.forEach((w, j) => list.append(h("div", {class: "it" + (S.sel && S.sel.w === j ? " sel" : ""), onclick: () => { S.sel = {w: j}; renderAll(); }},
    h("span", {}, widgetName(w)), h("span", {class: "grow"}), h("span", {class: "small muted"}, ANCHOR_NAMES[w.anchor]))));
  ["wDup", "wDel", "wUp", "wDown"].forEach((id) => $(id).disabled = !(S.sel && S.sel.w != null));
  const ins = $("wInspector"); ins.replaceChildren();
  if (!S.sel || S.sel.w == null) return;
  const w = S.widgets[S.sel.w];
  ins.append(...selectField("Value", VALUES.map((v) => [v, v]), () => w.value, (v) => { w.value = v; }));
  ins.append(...selectField("Style", STYLES.map((v) => [v, v]), () => w.style, (v) => { w.style = v; }));
  ins.append(...selectField("Anchor", ANCHORS.map((v) => [v, ANCHOR_NAMES[v]]), () => w.anchor, (v) => { w.anchor = v; }));
  ins.append(...numField("X", () => w.x, (v) => { w.x = v; }, -1280, 1280, 1, true));
  ins.append(...numField("Y", () => w.y, (v) => { w.y = v; }, -720, 720, 1, true));
  ins.append(...numField("Size", () => w.size, (v) => { w.size = v; }, 0.25, 8, 0.05, true));
  if (w.style !== "number" && w.style !== "icon") { ins.append(...numField("Bar width", () => w.w, (v) => { w.w = v; }, 1, 1280, 1, true)); ins.append(...numField("Bar height", () => w.h, (v) => { w.h = v; }, 1, 200, 1, true)); }
  ins.append(...numField("Max (0 = natural)", () => w.max, (v) => { w.max = v; }, 0, 100000, 1));
  if (w.style === "icon" || w.style === "iconbar") ins.append(...selectField("Icon", ICONS.map((v, j) => [j, v]), () => w.icon, (v) => { w.icon = +v; }));
  ins.append(...colorField("Colour", () => w.color, (c) => { w.color = c.concat([w.color[3]]); }));
  ins.append(...numField("Opacity", () => w.color[3], (v) => { w.color[3] = v; }, 0, 1, 0.05, true));
  if (w.style !== "gradbar") ins.append(...checkField("Grade (low colour)", () => w.grade, (v) => { w.grade = v; }));
  if (w.grade || w.style === "gradbar") ins.append(...colorField("Low colour", () => w.low, (c) => { w.low = c.concat([w.low[3]]); }));
  const lab = h("input", {value: w.label, placeholder: "none"}); lab.addEventListener("change", () => change(() => { w.label = lab.value.toUpperCase().replace(/[#=\s]+/g, " ").trim(); }));
  ins.append(h("label", {}, "Label"), lab);
}
function renderProfiles() {
  const list = $("pList"); list.replaceChildren();
  if (!S.profiles.length) list.append(h("div", {class: "empty small"}, "No profiles in huds/ yet."));
  S.profiles.forEach((n) => list.append(h("div", {class: "it", onclick: () => openProfile(n)}, h("span", {}, n), h("span", {class: "grow"}), h("span", {class: "small muted"}, "open"))));
}
function renderAll() {
  document.querySelectorAll("#tabs button").forEach((b) => b.classList.toggle("on", b.dataset.tab === S.tab));
  for (const t of ["elements", "widgets", "profiles", "text"]) $("tab-" + t).hidden = t !== S.tab;
  $("widgetsOn").checked = S.keys.hud_widgets === "1"; $("showVerbose").checked = S.keys.hud_verbose === "1";
  if (S.tab === "elements") renderElements(); else if (S.tab === "widgets") renderWidgets(); else if (S.tab === "profiles") renderProfiles();
  else if (document.activeElement !== $("txt")) $("txt").value = widgetsText(S.widgets);
  $("undo").disabled = !undoStack.length; $("redo").disabled = !redoStack.length;
  $("dirty").textContent = isDirty() ? "unsaved changes" : "";
  draw();
}

// ---- widget list actions ---------------------------------------------------------------------------------------
$("wAdd").onclick = () => change(() => { const w = newWidget(); w.x = 40; w.y = 40 + 24 * (S.widgets.length % 10); S.widgets.push(w); S.sel = {w: S.widgets.length - 1}; });
$("wDup").onclick = () => S.sel && S.sel.w != null && change(() => { const c = JSON.parse(JSON.stringify(S.widgets[S.sel.w])); c.y += 20; S.widgets.splice(S.sel.w + 1, 0, c); S.sel = {w: S.sel.w + 1}; });
function delWidget() { if (S.sel && S.sel.w != null) change(() => { S.widgets.splice(S.sel.w, 1); S.sel = null; }); }
$("wDel").onclick = delWidget;
const moveW = (d) => S.sel && S.sel.w != null && change(() => { const j = S.sel.w, k = j + d; if (k < 0 || k >= S.widgets.length) return; [S.widgets[j], S.widgets[k]] = [S.widgets[k], S.widgets[j]]; S.sel = {w: k}; });
$("wUp").onclick = () => moveW(-1); $("wDown").onclick = () => moveW(1);
$("txtApply").onclick = () => { const r = parseWidgets($("txt").value); $("txtErr").textContent = r.errs.join("; "); change(() => { S.widgets = r.list; S.sel = null; }); };
$("txtCopy").onclick = () => { navigator.clipboard.writeText(widgetsText(S.widgets)).then(() => toast("Copied (paste into Settings > HUD > widgets IMPORT)"), () => toast("Copy failed", true)); };
document.querySelectorAll("#tabs button").forEach((b) => b.onclick = () => { S.tab = b.dataset.tab; renderAll(); });
$("showHidden").onchange = $("showGrid").onchange = draw;
$("widgetsOn").onchange = () => change(() => { S.keys.hud_widgets = $("widgetsOn").checked ? "1" : "0"; });
$("showVerbose").onchange = () => change(() => { S.keys.hud_verbose = $("showVerbose").checked ? "1" : "0"; });
$("fov").onchange = () => { S.fov = clamp(num($("fov").value, 100), 60, 140); draw(); };
$("undo").onclick = undo; $("redo").onclick = redo;

// ---- server ----------------------------------------------------------------------------------------------------
function toast(msg, bad) { const t = $("toast"); t.textContent = msg; t.className = "toast" + (bad ? " bad" : ""); t.hidden = false; clearTimeout(t._t); t._t = setTimeout(() => t.hidden = true, 3500); }
async function api(path, body) {
  const r = await fetch(path, body ? {method: "POST", headers: {"Content-Type": "application/json", "X-Quark-Editor": "1"}, body: JSON.stringify(body)} : {});
  const j = await r.json().catch(() => ({error: "bad answer"}));
  if (!r.ok) throw new Error(j.error || r.status);
  return j;
}
async function load() {
  try {
    const st = await api("/api/state");
    $("dataDir").textContent = st.dataDir;
    $("gameNote").hidden = !st.gameRunning;
    S.elems = {};
    for (const [n, v] of Object.entries(st.quarkCfg.hud)) S.elems[n] = parseElem(v);
    for (const el of ELEMENTS) if (!S.elems[el.i]) S.elems[el.i] = parseElem("");
    S.keys = {hud_widgets: st.quarkCfg.keys.hud_widgets === "0" ? "0" : "1", hud_verbose: st.quarkCfg.keys.hud_verbose === "1" ? "1" : "0"};
    S.fov = clamp(num(st.quarkCfg.keys.fov, 100), 60, 140); $("fov").value = S.fov;
    const r = parseWidgets(st.hudCfg || ""); S.widgets = r.list;
    if (r.errs.length) toast("hud.cfg: " + r.errs.join("; "), true);
    S.profiles = st.profiles; S.sel = null; undoStack = []; redoStack = [];
    S.saved = snapshot();
    if (!st.hasQuarkCfg) toast("No quark.cfg in " + st.dataDir + " yet (start the game once): showing defaults");
    renderAll();
  } catch (ex) { toast("Couldn't read the game's files: " + ex.message, true); renderAll(); }
}
async function save() {
  try {
    await api("/api/hudcfg", {text: widgetsText(S.widgets)});
    const hud = {};
    for (const [n, e] of Object.entries(S.elems)) if (e.had) hud[n] = elemText(e);
    const res = await api("/api/elements", {hud, keys: S.keys});
    S.saved = snapshot(); renderAll();
    $("gameNote").hidden = !res.gameRunning;
    toast(res.gameRunning ? "Saved. Widgets show in game now; restart the game for built-in element changes." : "Saved hud.cfg and quark.cfg");
  } catch (ex) { toast("Save failed: " + ex.message, true); }
}
async function openProfile(n) {
  try { const j = await api("/api/profile?name=" + encodeURIComponent(n)); const r = parseWidgets(j.text);
    change(() => { S.widgets = r.list; S.sel = null; }); S.tab = "widgets"; renderAll(); toast("Opened " + n + " (" + r.list.length + " widgets): Save writes it to hud.cfg"); }
  catch (ex) { toast("Couldn't open " + n + ": " + ex.message, true); }
}
$("pSave").onclick = async () => {
  const n = $("pName").value.trim();
  if (!n || /[\\/:*?"<>|]/.test(n) || n.startsWith(".")) return toast("Pick a plain file name", true);
  try { const j = await api("/api/profile", {name: n, text: widgetsText(S.widgets)}); S.profiles = j.profiles; renderAll(); toast("Saved huds/" + n); }
  catch (ex) { toast("Save failed: " + ex.message, true); }
};
$("save").onclick = save;
$("reload").onclick = () => { if (!isDirty() || confirm("Drop the unsaved changes and read the files again?")) load(); };
window.addEventListener("beforeunload", (ev) => { if (isDirty()) { ev.preventDefault(); ev.returnValue = ""; } });
if (new URLSearchParams(location.search).get("embed") === "1") document.body.classList.add("embed");
load();
