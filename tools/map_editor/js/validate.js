// Cheap checks before a save (a few ms on Midtown), on the map JSON: what the game's Map::validate refuses (errors: the
// save is refused, as the helper's serialize answer `valid: false` is) and what loads but plays wrong (warnings: the save
// goes ahead and says so). Every entry may name the object ({k, i}) so the page can select and frame it.
// Limits: src/game/map.h (kMapExtent, kMaxMap*, kMaxObjectHeight, the light range).
export const LIMITS = { extent: 4096, brushes: 60000, spawns: 256, lights: 512, ladders: 256, groups: 256, parts: 1024, stairs: 512,
  sites: 16, checkpoints: 128, path: 512, points: 16, cartChecks: 64, height: 64, title: 40, lightRadius: [0.5, 100], lightIntensity: 10 };

const solid = (mat) => mat < 5 || mat === 9 || mat === 10;   // Map::gridSolid: what players collide with (nograpple 10 too)

export function validateMap(m) {
  const errors = [], warnings = [];
  const E = (msg, ref) => errors.push({ msg, ref }), W = (msg, ref) => warnings.push({ msg, ref });
  const num = (v) => typeof v === "number" && isFinite(v) && Math.abs(v) <= LIMITS.extent;
  const vec = (p) => Array.isArray(p) && p.length >= 3 && num(p[0]) && num(p[1]) && num(p[2]);
  const box = (b) => b && vec(b.min) && vec(b.max);
  const boxes = (list, k, what) => (list || []).forEach((b, i) => {
    if (!box(b)) E(what + " #" + i + " is out of range (every coordinate within 4096 m)", { k, i });
    else if (b.min.some((v, n) => v > b.max[n])) E(what + " #" + i + " has a min above its max", { k, i });
  });
  const count = (list, max, what) => { if ((list || []).length > max) E("too many " + what + ": " + list.length + " (the game's limit is " + max + ")"); };

  count(m.brushes, LIMITS.brushes, "boxes"); count(m.spawns, LIMITS.spawns, "spawns"); count(m.lights, LIMITS.lights, "lights");
  count(m.ladders, LIMITS.ladders, "ladders"); count(m.groups, LIMITS.groups, "groups"); count(m.stairs, LIMITS.stairs, "stairs records");
  count(m.sites, LIMITS.sites, "bomb sites"); count(m.checkpoints, LIMITS.checkpoints, "checkpoints"); count(m.path, LIMITS.path, "path points");
  count(m.points, LIMITS.points, "capture points"); count(m.cartChecks, LIMITS.cartChecks, "payload checkpoints");
  if ((m.brushes || []).length + (m.generated || []).length > LIMITS.brushes) E("too many boxes with the groups' and ladders' parts (" + LIMITS.brushes + ")");
  if (!(m.brushes || []).length && !(m.groups || []).length) E("the map has no box");
  if (!(m.spawns || []).length) E("the map has no spawn");
  boxes(m.brushes, "brush", "box"); boxes(m.sites, "site", "bomb site"); boxes(m.points, "point", "capture point");
  boxes(m.checkpoints, "checkpoint", "checkpoint"); boxes(m.ladders, "ladder", "ladder"); boxes(m.groups, "group", "group");
  if (m.hill) boxes([m.hill], "hill", "the hill");
  for (const [list, k, what] of [[m.ladders, "ladder", "ladder"], [m.groups, "group", "group"]]) (list || []).forEach((b, i) => {
    if (box(b) && b.max[1] - b.min[1] > LIMITS.height) E(what + " #" + i + " is taller than 64 m", { k, i });
  });
  (m.groups || []).forEach((g, i) => {
    if ((g.parts || []).length > LIMITS.parts) E("group #" + i + " has more than 1024 parts", { k: "group", i });
    if (g.type !== "custom" && (g.parts || []).length) E("group #" + i + " (" + g.type + ") has parts: only a custom group has them", { k: "group", i });
  });
  (m.spawns || []).forEach((s, i) => { if (!vec(s.pos) || !isFinite(s.yaw)) E("spawn #" + i + " is out of range", { k: "spawn", i }); });
  (m.lights || []).forEach((l, i) => {
    if (!vec(l.pos) || !(l.radius >= LIMITS.lightRadius[0] && l.radius <= LIMITS.lightRadius[1]) || !(l.intensity > 0 && l.intensity <= LIMITS.lightIntensity))
      E("light #" + i + " is out of range (radius 0.5-100, intensity above 0, at most 10)", { k: "light", i });
  });
  (m.path || []).forEach((p, i) => { if (!vec(p)) E("path point #" + i + " is out of range", { k: "path", i }); });
  for (const k of ["void", "indoor"]) if (m[k] !== null && m[k] !== undefined && !num(m[k])) E("the map's " + k + " height is out of range");
  if (m.title && (m.title.length > LIMITS.title || /[#\x00-\x1f\x7f-￿]/.test(m.title))) E("the title is at most 40 plain characters, no #");

  // warnings: boxes with no size, spawns in a wall, objectives the map's mode needs
  (m.brushes || []).forEach((b, i) => { if (box(b) && b.min.some((v, n) => v === b.max[n])) W("box #" + i + " has no size along " + "xyz"[b.min.findIndex((v, n) => v === b.max[n])] + " (it draws and blocks nothing)", { k: "brush", i }); });
  const solids = [...(m.brushes || []), ...(m.generated || [])].filter(b => solid(b.mat) && box(b));
  (m.spawns || []).forEach((s, i) => {
    if (!vec(s.pos)) return;
    const [x, y, z] = s.pos, e = 0.02;
    if (solids.some(b => x + 0.4 - e > b.min[0] && x - 0.4 + e < b.max[0] && y + 1.8 - e > b.min[1] && y + e < b.max[1] && z + 0.4 - e > b.min[2] && z - 0.4 + e < b.max[2]))
      W("spawn #" + i + " (team " + s.team + ") is inside a solid box", { k: "spawn", i });
  });
  const need = { bomb: ["sites", "bomb sites"], koth: ["hill", "a hill"], points: ["points", "capture points"], race: ["checkpoints", "checkpoints"], payload: ["path", "a payload path"], npayload: ["path", "a payload path"] }[m.preferredMode];
  if (need && !(Array.isArray(m[need[0]]) ? m[need[0]].length : m[need[0]])) W("built for " + m.preferredMode + " but has no " + need[1]);
  return { errors, warnings };
}
