// A tiny .qmap -> map JSON reader for the decoration tests when build/quark_maptool is missing (the shape of
// tools/map_editor/INTERFACE.md; only what the decoration engine reads). The helper's `parse` is the real one.
'use strict';
const MATS = ['box', 'accent', 'wallbang', 'metal', 'glow', 'far', 'water', 'lava', 'trim', 'clip'];
function parseQmap(text, name) {
    const m = { name: name || '', title: '', formatVersion: 1, brushes: [], groups: [], spawns: [], sites: [], hill: null, points: [], checkpoints: [],
                path: [], ladders: [], stairs: [], lights: [] };
    const bx = (w, k) => ({ min: [+w[k], +w[k + 1], +w[k + 2]], max: [+w[k + 3], +w[k + 4], +w[k + 5]] });
    for (const line of text.split(/\r?\n/)) {
        const w = line.trim().split(/\s+/);
        if (!w[0] || w[0][0] === '#') continue;
        const kw = w[0], mi = MATS.indexOf(kw);
        if (mi >= 0 && w.length >= 7) m.brushes.push(Object.assign({ mat: mi }, bx(w, 1)));
        else if (kw === 'version') m.formatVersion = +w[1];
        else if (kw === 'title') m.title = w.slice(1).join(' ');
        else if (kw === 'spawn') m.spawns.push({ team: w[1], pos: [+w[2], +w[3], +w[4]], yaw: (+w[5] || 0) * Math.PI / 180 });
        else if (kw === 'site') m.sites.push(bx(w, 1));
        else if (kw === 'hill') m.hill = bx(w, 1);
        else if (kw === 'point') m.points.push(bx(w, 1));
        else if (kw === 'checkpoint') m.checkpoints.push(bx(w, 1));
        else if (kw === 'ladder') m.ladders.push(Object.assign(bx(w, 1), { auto: w[7] === 'auto' }));
        else if (kw === 'path') m.path.push([+w[1], +w[2], +w[3]]);
        else if (kw === 'light') m.lights.push({ pos: [+w[1], +w[2], +w[3]], radius: +w[4], intensity: +w[5] });
        else if (kw === 'stairs') m.stairs.push({ x0: +w[1], z0: +w[2], x1: +w[3], z1: +w[4], base: +w[5], top: +w[6], axis: w[7], rising: +w[8], solid: w[9] !== '0' });
        else if (kw === 'group') m.groups.push(Object.assign({ type: w[1], mat: +w[2], dir: +w[3], parts: [] }, bx(w, 4)));
        else if (kw === 'part' && m.groups.length) m.groups[m.groups.length - 1].parts.push(Object.assign({ mat: +w[1] }, bx(w, 2)));
    }
    return m;
}
module.exports = { parseQmap };
