// .qmap reader for the editor (the game's format: src/game/map.cpp Map::parseCurrent). Lenient: what it can't read
// is listed in warnings, never thrown. Only what a top-down / 3D view and bot placement need is kept, in flat typed
// arrays so a 12k-box map (Midtown) draws without per-frame allocation.

export const MATERIALS = ["base", "accent", "wallbang", "metal", "glow", "far", "water", "lava", "trim", "clip"];
const MAT_OF = { box: 0, accent: 1, wallbang: 2, metal: 3, glow: 4, far: 5, water: 6, lava: 7, trim: 8, clip: 9 };

export const MODES = ["vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag", "points"];
export const MODE_TITLES = { vs: "VS", bomb: "Bomb", race: "Race", koth: "King of the hill", payload: "Payload", ffa: "Free for all",
    npayload: "Neutral payload", duel: "Duel", "1v1": "1v1", tourney: "Tourney", hide: "Hide and seek", tag: "Tag", points: "Multi point" };
export const TEAM_MODES = new Set(["vs", "bomb", "koth", "payload", "npayload", "points", "duel", "1v1", "tourney"]);   // (the game puts 1v1 / duel / tourney players on teams a and b too)

// What a mode needs from the map: Map::canSupportMode (src/game/map.cpp), or the game's own answer when the helper
// read the map. Staged spawns (att0 / def0 ...) are not team a / b spawns there.
export function mapSupports(map, mode) {
    if (map.modesPossible) return map.modesPossible.includes(mode);
    const has = (t) => map.spawns.some((s) => s.raw === t);
    const teams = has("a") && has("b");
    switch (mode) {
        case "vs": case "duel": case "1v1": return teams;
        case "tourney": case "ffa": return map.spawns.length > 0;
        case "bomb": return teams && map.sites.length > 0;
        case "race": return has("race") && map.checkpoints.length > 0;
        case "koth": return teams && !!map.hill;
        case "payload": case "npayload": return teams && map.path.length >= 2;
        case "hide": case "tag": return map.spawns.length >= 2;
        case "points": return map.points.length >= 2 && map.spawns.some((s) => s.stage === 0 && !s.defend) && map.spawns.some((s) => s.stage === 0 && s.defend);
        default: return false;
    }
}

const num = (s) => { const v = Number(s); return Number.isFinite(v) ? v : NaN; };

export function parseQmap(text, name = "") {
    const m = {
        name, title: name, version: 1, handmade: false, setting: "", forMode: "", modes: [],
        boxes: null, nBoxes: 0,              // Float32Array [x0,y0,z0,x1,y1,z1]*n
        mats: null,                          // Uint8Array material per box
        spawns: [], sites: [], hill: null, points: [], ladders: [], checkpoints: [], path: [], cartChecks: [],
        lights: [], groups: [], voidY: null, indoor: null, holdFire: 0, palette: null,
        bounds: { x0: 1e9, y0: 1e9, z0: 1e9, x1: -1e9, y1: -1e9, z1: -1e9 },
        warnings: [],
    };
    const raw = [];           // flat numbers, grown then copied once
    const mats = [];
    const box = (a, i) => {
        const v = [num(a[i]), num(a[i + 1]), num(a[i + 2]), num(a[i + 3]), num(a[i + 4]), num(a[i + 5])];
        if (v.some(Number.isNaN)) return null;
        return { x0: Math.min(v[0], v[3]), y0: Math.min(v[1], v[4]), z0: Math.min(v[2], v[5]),
                 x1: Math.max(v[0], v[3]), y1: Math.max(v[1], v[4]), z1: Math.max(v[2], v[5]) };
    };
    const pushBox = (b, mat) => { raw.push(b.x0, b.y0, b.z0, b.x1, b.y1, b.z1); mats.push(mat); };
    const lines = text.split("\n");
    for (let ln = 0; ln < lines.length; ln++) {
        let line = lines[ln];
        const h = line.indexOf("#");
        if (h >= 0) line = line.slice(0, h);
        const a = line.trim().split(/\s+/);
        if (!a[0]) continue;
        const kw = a[0];
        const bad = () => m.warnings.push(`line ${ln + 1}: can't read '${line.trim().slice(0, 60)}'`);
        if (kw in MAT_OF) { const b = box(a, 1); if (b) pushBox(b, MAT_OF[kw]); else bad(); }
        else if (kw === "version") m.version = num(a[1]) || 1;
        else if (kw === "title") m.title = line.trim().slice(5).trim() || name;
        else if (kw === "handmade") m.handmade = true;
        else if (kw === "setting") m.setting = line.trim().slice(7).trim();
        else if (kw === "for") m.forMode = a[1] || "";
        else if (kw === "modes") m.modes = a.slice(1);
        else if (kw === "spawn") {
            const t = a[1], x = num(a[2]), y = num(a[3]), z = num(a[4]), yawDeg = a.length > 5 ? num(a[5]) : 0;
            if ([x, y, z, yawDeg].some(Number.isNaN)) { bad(); continue; }
            let team = t, stage = -1, defend = false;
            if (/^(att|def)[0-9]$/.test(t)) { stage = +t[3]; defend = t[0] === "d"; team = defend ? "b" : "a"; }
            m.spawns.push({ team, raw: t, stage, defend, x, y, z, yaw: yawDeg * Math.PI / 180 });
        }
        else if (kw === "site") { const b = box(a, 1); if (b) m.sites.push(b); else bad(); }
        else if (kw === "hill") { const b = box(a, 1); if (b) m.hill = b; else bad(); }
        else if (kw === "point") { const b = box(a, 1); if (b) m.points.push(b); else bad(); }
        else if (kw === "checkpoint") { const b = box(a, 1); if (b) m.checkpoints.push(b); else bad(); }
        else if (kw === "ladder") { const b = box(a, 1); if (b) { b.auto = a[7] === "auto"; m.ladders.push(b); } else bad(); }
        else if (kw === "path") { const x = num(a[1]), y = num(a[2]), z = num(a[3]); if ([x, y, z].some(Number.isNaN)) bad(); else m.path.push({ x, y, z }); }
        else if (kw === "cartcheck") m.cartChecks.push(num(a[1]));
        else if (kw === "light") m.lights.push({ x: num(a[1]), y: num(a[2]), z: num(a[3]), r: num(a[4]), i: a[5] ? num(a[5]) : 1 });
        else if (kw === "void") m.voidY = num(a[1]);
        else if (kw === "indoor") m.indoor = num(a[1]);
        else if (kw === "holdfire") m.holdFire = num(a[1]);
        else if (kw === "group") {
            const b = box(a, 4);
            if (!b) { bad(); continue; }
            m.groups.push({ type: a[1], material: num(a[2]) | 0, dir: num(a[3]) | 0, box: b, parts: [] });
        }
        else if (kw === "part") {
            const g = m.groups[m.groups.length - 1], b = box(a, 2);
            if (!g || !b) { bad(); continue; }
            g.parts.push({ material: num(a[1]) | 0, box: b });
        }
        else if (kw === "palette") {
            const p = {};
            for (let i = 1; i + 3 < a.length; i += 4) p[a[i]] = [num(a[i + 1]), num(a[i + 2]), num(a[i + 3])];
            m.palette = p;
        }
        else if (kw === "sun" || kw === "stairs" || kw === "ranked") { /* not needed here */ }
        else m.warnings.push(`line ${ln + 1}: unknown '${kw}' skipped`);
    }
    // Generated boxes: auto ladders' rails and rungs, groups' parts (Map::genLadderBrushes / groupBoxes).
    for (const l of m.ladders) if (l.auto) ladderBoxes(l, pushBox);
    for (const g of m.groups) groupBoxes(g, pushBox);
    m.nBoxes = mats.length;
    m.boxes = new Float32Array(raw);
    m.mats = new Uint8Array(mats);
    computeBounds(m);
    return m;
}

// Replace the geometry with the game's own reading of the map (quark_maptool `parse`: brushes plus the generated
// ladder and group boxes, its ladders, modes and warnings). The page's reader above stays the fallback (no helper,
// or the helper failed). Spawns and objectives keep the page's reading: the same lines, read the same way.
export function applyHelperParse(m, j) {
    if (!j || !Array.isArray(j.brushes)) return false;
    const all = j.brushes.concat(j.generated || []);
    const boxes = new Float32Array(all.length * 6), mats = new Uint8Array(all.length);
    all.forEach((b, i) => {
        boxes.set([Math.min(b.min[0], b.max[0]), Math.min(b.min[1], b.max[1]), Math.min(b.min[2], b.max[2]), Math.max(b.min[0], b.max[0]), Math.max(b.min[1], b.max[1]), Math.max(b.min[2], b.max[2])], i * 6);
        mats[i] = Math.max(0, Math.min(MATERIALS.length - 1, b.mat | 0));
    });
    m.boxes = boxes; m.mats = mats; m.nBoxes = all.length;
    if (Array.isArray(j.ladders)) m.ladders = j.ladders.map((l) => ({ x0: l.min[0], y0: l.min[1], z0: l.min[2], x1: l.max[0], y1: l.max[1], z1: l.max[2], auto: !!l.auto }));
    if (Array.isArray(j.modesPossible)) m.modesPossible = j.modesPossible;
    if (Array.isArray(j.modesSupported)) m.modesSupported = j.modesSupported;
    if (Array.isArray(j.warnings)) m.warnings = j.warnings.map(String);
    m.bounds = { x0: 1e9, y0: 1e9, z0: 1e9, x1: -1e9, y1: -1e9, z1: -1e9 };
    computeBounds(m);
    m.geometryFrom = "quark_maptool parse";
    return true;
}

function computeBounds(m) {
    const B = m.bounds;
    for (let i = 0; i < m.nBoxes; i++) {
        if (m.mats[i] >= 5) continue;   // far / water / lava / trim / clip: not the playable bounds (as the game)
        const o = i * 6;
        if (m.boxes[o] < B.x0) B.x0 = m.boxes[o]; if (m.boxes[o + 1] < B.y0) B.y0 = m.boxes[o + 1]; if (m.boxes[o + 2] < B.z0) B.z0 = m.boxes[o + 2];
        if (m.boxes[o + 3] > B.x1) B.x1 = m.boxes[o + 3]; if (m.boxes[o + 4] > B.y1) B.y1 = m.boxes[o + 4]; if (m.boxes[o + 5] > B.z1) B.z1 = m.boxes[o + 5];
    }
    if (B.x0 > B.x1) Object.assign(B, { x0: -10, y0: -1, z0: -10, x1: 10, y1: 1, z1: 10 });
}

function ladderBoxes(l, push) {
    const alongX = (l.x1 - l.x0) >= (l.z1 - l.z0);
    const top = Math.max(l.y0 + 0.5, l.y1 - 1.0);
    const add = (a0, y0, a1, y1, d0, d1) => push(alongX ? { x0: a0, y0, z0: d0, x1: a1, y1, z1: d1 } : { x0: d0, y0, z0: a0, x1: d1, y1, z1: a1 }, 8);
    const w0 = alongX ? l.x0 : l.z0, w1 = alongX ? l.x1 : l.z1, dc = alongX ? (l.z0 + l.z1) * 0.5 : (l.x0 + l.x1) * 0.5;
    add(w0, l.y0, w0 + 0.08, top, dc - 0.04, dc + 0.04);
    add(w1 - 0.08, l.y0, w1, top, dc - 0.04, dc + 0.04);
    for (let y = l.y0 + 0.35; y < top; y += 0.4) add(w0 + 0.08, y, w1 - 0.08, y + 0.06, dc - 0.03, dc + 0.03);
}

function groupBoxes(g, push) {
    const b = g.box, sx = b.x1 - b.x0, sy = b.y1 - b.y0, sz = b.z1 - b.z0;
    const add = (x0, y0, z0, x1, y1, z1, mat = g.material) => { if (x1 - x0 > 1e-3 && y1 - y0 > 1e-3 && z1 - z0 > 1e-3) push({ x0, y0, z0, x1, y1, z1 }, mat); };
    if (g.type === "custom") {
        for (const p of g.parts) add(b.x0 + p.box.x0 * sx, b.y0 + p.box.y0 * sy, b.z0 + p.box.z0 * sz, b.x0 + p.box.x1 * sx, b.y0 + p.box.y1 * sy, b.z0 + p.box.z1 * sz, p.material);
    } else if (g.type === "room") {
        const t = Math.min(0.25, Math.min(sx, sy, sz) * 0.25);
        add(b.x0, b.y0, b.z0, b.x1, b.y0 + t, b.z1);
        add(b.x0, b.y1 - t, b.z0, b.x1, b.y1, b.z1);
        const y0 = b.y0 + t, y1 = b.y1 - t, dh = Math.min(2.4, (y1 - y0) * 0.85);
        for (let w = 0; w < 4; w++) {
            const alongX = w < 2;
            const s0 = alongX ? b.x0 : b.z0 + t, s1 = alongX ? b.x1 : b.z1 - t;
            const d0 = alongX ? (w === 0 ? b.z0 : b.z1 - t) : (w === 2 ? b.x0 : b.x1 - t), d1 = d0 + t;
            const wall = (u0, v0, u1, v1) => alongX ? add(u0, v0, d0, u1, v1, d1) : add(d0, v0, u0, d1, v1, u1);
            const len = s1 - s0, dw = Math.min(1.6, len - 2 * t - 0.5);
            if (dw < 0.9 || dh < 1.0) { wall(s0, y0, s1, y1); continue; }
            const mid = (s0 + s1) * 0.5;
            wall(s0, y0, mid - dw * 0.5, y1);
            wall(mid + dw * 0.5, y0, s1, y1);
            wall(mid - dw * 0.5, y0 + dh, mid + dw * 0.5, y1);
        }
    } else {   // stairs
        const ax = g.dir < 2, neg = (g.dir & 1) === 1;
        const n = Math.min(128, Math.max(1, Math.ceil(sy / 0.5 - 1e-4)));
        const len = ax ? sx : sz, run = len / n, rise = sy / n;
        for (let k = 0; k < n; k++) {
            let a0, a1;
            if (neg) { a1 = (ax ? b.x1 : b.z1) - run * k; a0 = a1 - run; } else { a0 = (ax ? b.x0 : b.z0) + run * k; a1 = a0 + run; }
            const yTop = b.y0 + rise * (k + 1);
            if (ax) add(a0, b.y0, b.z0, a1, yTop, b.z1); else add(b.x0, b.y0, a0, b.x1, yTop, a1);
        }
    }
}

// The mode objectives a bot can be told to attack / defend on this map (for per-bot overrides and the mock).
export function mapObjectives(map) {
    const c = (b) => ({ x: (b.x0 + b.x1) / 2, y: b.y0, z: (b.z0 + b.z1) / 2, r: Math.max(1, Math.hypot(b.x1 - b.x0, b.z1 - b.z0) / 2) });
    const out = [];
    map.sites.forEach((b, i) => out.push({ kind: "site", index: i, label: "Bomb site " + "ABCDEFGH"[i % 8], ...c(b) }));
    if (map.hill) out.push({ kind: "hill", index: 0, label: "Hill", ...c(map.hill) });
    map.points.forEach((b, i) => out.push({ kind: "point", index: i, label: "Point " + (i + 1), ...c(b) }));
    map.checkpoints.forEach((b, i) => out.push({ kind: "checkpoint", index: i, label: (i === map.checkpoints.length - 1 ? "Finish" : "Checkpoint " + (i + 1)), ...c(b) }));
    if (map.path.length >= 2) {
        const p = map.path[0], q = map.path[map.path.length - 1];
        out.push({ kind: "cart", index: 0, label: "Payload start", x: p.x, y: p.y, z: p.z, r: 3 });
        out.push({ kind: "cart", index: 1, label: "Payload end", x: q.x, y: q.y, z: q.z, r: 3 });
    }
    return out;
}

// Floor height under (x, z): the top of the highest walkable box whose footprint holds the point and whose top is at
// or below `below` (default: anywhere). Used to drop placed bots onto the floor.
export function floorAt(map, x, z, below = 1e9) {
    let best = -1e9;
    const B = map.boxes;
    for (let i = 0; i < map.nBoxes; i++) {
        const mt = map.mats[i];
        if (mt >= 5 && mt !== 6) continue;   // far / lava / trim / clip: not a floor (water: stands on its surface for display)
        const o = i * 6;
        if (x >= B[o] && x <= B[o + 3] && z >= B[o + 2] && z <= B[o + 5]) {
            const top = B[o + 4];
            if (top <= below + 1e-3 && top > best) best = top;
        }
    }
    return best > -1e9 ? best : 0;
}
