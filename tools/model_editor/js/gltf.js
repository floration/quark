/* ===== glTF ==========================================================================================
   Reading: the game's loader (src/client/gltf.cpp loadGltf) ported, so a model shows here exactly as the game will
   draw it: .gltf (buffers as data: URIs or files given with it) or .glb; every triangle (lists / strips / fans,
   indexed or not) of every mesh in the default scene with its node transforms (matrix or TRS, nested); POSITION,
   NORMAL (flat when missing), COLOR_0 (float / normalised u8 / u16, vec3 / vec4) times the material's baseColorFactor;
   alphaMode BLEND = translucent at baseColorFactor's alpha; the material name picks the tint slot. Textures ignored.
   Writing: a .glb laid out like the game's exporter (writeGltf): one primitive per material, POSITION / NORMAL /
   COLOR_0 floats and uint32 indices. Boxes: the game's partsToMesh plus the box list it draws (extras.quark version 2,
   docs/ASSETS.md "Box models": read with the game's checks, written with 9 significant digits), and the way back. */
(function () {
  'use strict';
  const Q = window.Q;
  const G = Q.gltf = {};

  // --- parse ---------------------------------------------------------------------------------------------------------
  // bytes: Uint8Array (.glb or .gltf text); files: { 'name.bin': Uint8Array } for external buffers
  // -> { prims: [{ material, slot, alpha, pos: Float32Array, nrm, col }] (flat triangle lists), tris, extras,
  //      provenance (asset.extras.provenance: who made it, as this editor writes it into every export) | null }
  G.parse = function (bytes, name, files) {
    let json = null, bin = null;
    const isGlb = bytes.length >= 12 && bytes[0] === 0x67 && bytes[1] === 0x6c && bytes[2] === 0x54 && bytes[3] === 0x46;
    if (isGlb) {
      const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), len = dv.getUint32(8, true);
      let p = 12;
      while (p + 8 <= bytes.length && p + 8 <= len) {
        const cl = dv.getUint32(p, true), ct = dv.getUint32(p + 4, true);
        if (p + 8 + cl > bytes.length) throw new Error('truncated .glb');
        if (ct === 0x4E4F534A) json = new TextDecoder().decode(bytes.subarray(p + 8, p + 8 + cl));
        else if (ct === 0x004E4942) bin = bytes.subarray(p + 8, p + 8 + cl);
        p += 8 + ((cl + 3) & ~3);
      }
      if (json == null) throw new Error('.glb without JSON');
    } else json = new TextDecoder().decode(bytes);
    let root;
    try { root = JSON.parse(json); } catch (e) { throw new Error('not valid glTF JSON'); }
    if (!root || typeof root !== 'object' || Array.isArray(root)) throw new Error('not valid glTF JSON');
    const buffers = (root.buffers || []).map((b, i) => {
      const uri = b.uri || '';
      if (!uri) { if (!isGlb || i !== 0 || !bin) throw new Error('buffer without data'); return bin; }
      if (uri.startsWith('data:')) {
        const comma = uri.indexOf(',');
        if (comma < 0 || uri.indexOf(';base64') < 0) throw new Error('bad data: URI');
        return Q.b64ToBytes(uri.slice(comma + 1).replace(/[^A-Za-z0-9+/]/g, ''));
      }
      if (uri.indexOf('..') >= 0) throw new Error('missing buffer file ' + uri);
      const f = files && (files[uri] || files[decodeURIComponent(uri)] || files[uri.split('/').pop()]);
      if (!f) throw new Error('missing buffer file ' + uri + ' (pick it together with the .gltf)');
      return f;
    });
    const out = { prims: [], tris: 0, extras: null, name: name || '' };
    const meshes = root.meshes || [];
    if (meshes[0] && meshes[0].extras && meshes[0].extras.quark) out.extras = meshes[0].extras.quark;
    const pv = root.asset && root.asset.extras && root.asset.extras.provenance;
    out.provenance = pv && typeof pv === 'object' && !Array.isArray(pv) ? pv : null;
    const ID = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const scenes = root.scenes;
    if (scenes && scenes.length) {
      let si = typeof root.scene === 'number' ? root.scene : 0;
      if (si < 0 || si >= scenes.length) si = 0;
      for (const r of scenes[si].nodes || []) addNode(root, buffers, r, ID, out, 0);
    } else {
      for (const m of meshes) for (const p of m.primitives || []) addPrimitive(root, buffers, p, ID, out);
    }
    if (!out.prims.length) throw new Error('no triangles');
    for (const p of out.prims) out.tris += p.pos.length / 9;
    return out;
  };

  function mul(a, b) {   // column-major 4x4
    const r = new Array(16);
    for (let c = 0; c < 4; c++) for (let rr = 0; rr < 4; rr++) {
      let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
      r[c * 4 + rr] = s;
    }
    return r;
  }
  function nodeMatrix(n) {
    const r = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    if (n.matrix) { if (n.matrix.length === 16) for (let i = 0; i < 16; i++) r[i] = +n.matrix[i]; return r; }
    const t = n.translation && n.translation.length === 3 ? n.translation : [0, 0, 0];
    const q = n.rotation && n.rotation.length === 4 ? n.rotation : [0, 0, 0, 1];
    const s = n.scale && n.scale.length === 3 ? n.scale : [1, 1, 1];
    const [x, y, z, w] = q;
    const R = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
      2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
      2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)];
    for (let c = 0; c < 3; c++) for (let rr = 0; rr < 3; rr++) r[c * 4 + rr] = R[c * 3 + rr] * s[c];
    r[12] = t[0]; r[13] = t[1]; r[14] = t[2];
    return r;
  }
  function addNode(root, buffers, ni, parent, out, depth) {
    const nodes = root.nodes || [];
    if (ni < 0 || ni >= nodes.length || depth > 64) throw new Error('bad node');
    const n = nodes[ni], xf = mul(parent, nodeMatrix(n));
    if (n.mesh != null) {
      const m = (root.meshes || [])[n.mesh];
      if (!m) throw new Error('bad mesh');
      for (const p of m.primitives || []) addPrimitive(root, buffers, p, xf, out);
    }
    for (const c of n.children || []) addNode(root, buffers, c, xf, out, depth + 1);
  }
  function readAccessor(root, buffers, ai) {
    const acc = (root.accessors || [])[ai];
    if (!acc) throw new Error('bad accessor');
    const comps = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[acc.type] || 0;
    const ct = acc.componentType | 0, count = acc.count | 0, norm = !!acc.normalized;
    const csize = ct === 5120 || ct === 5121 ? 1 : ct === 5122 || ct === 5123 ? 2 : ct === 5125 || ct === 5126 ? 4 : 0;
    if (!comps || !csize || count < 0) throw new Error('unsupported accessor');
    const out = new Float32Array(count * comps);
    if (acc.bufferView == null) return { data: out, comps };
    const v = (root.bufferViews || [])[acc.bufferView];
    if (!v) throw new Error('bad bufferView');
    const buf = buffers[v.buffer | 0];
    if (!buf) throw new Error('bad buffer');
    const off = (v.byteOffset || 0) + (acc.byteOffset || 0), stride = v.byteStride || comps * csize;
    if (count && off + stride * (count - 1) + comps * csize > buf.length) throw new Error('accessor out of range');
    const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    for (let i = 0; i < count; i++) for (let c = 0; c < comps; c++) {
      const p = off + stride * i + c * csize;
      let f = 0;
      switch (ct) {
        case 5126: f = dv.getFloat32(p, true); break;
        case 5121: f = norm ? dv.getUint8(p) / 255 : dv.getUint8(p); break;
        case 5120: f = norm ? Math.max(-1, dv.getInt8(p) / 127) : dv.getInt8(p); break;
        case 5123: f = norm ? dv.getUint16(p, true) / 65535 : dv.getUint16(p, true); break;
        case 5122: f = norm ? Math.max(-1, dv.getInt16(p, true) / 32767) : dv.getInt16(p, true); break;
        case 5125: f = dv.getUint32(p, true); break;
      }
      out[i * comps + c] = f;
    }
    return { data: out, comps };
  }
  function addPrimitive(root, buffers, prim, m, out) {
    const mode = prim.mode == null ? 4 : prim.mode;
    if (mode !== 4 && mode !== 5 && mode !== 6) return;
    const attrs = prim.attributes;
    if (!attrs || attrs.POSITION == null) return;
    const P = readAccessor(root, buffers, attrs.POSITION);
    if (P.comps !== 3) throw new Error('POSITION must be VEC3');
    const pos = P.data, nv = pos.length / 3;
    let nrm = null, col = null, cc = 0;
    if (attrs.NORMAL != null) { try { const N = readAccessor(root, buffers, attrs.NORMAL); if (N.comps === 3) nrm = N.data; } catch (e) { } }
    if (attrs.COLOR_0 != null) { try { const C = readAccessor(root, buffers, attrs.COLOR_0); if (C.comps === 3 || C.comps === 4) { col = C.data; cc = C.comps; } } catch (e) { } }
    let idx;
    if (prim.indices != null) {
      const I = readAccessor(root, buffers, prim.indices);
      if (I.comps !== 1) throw new Error('bad indices');
      idx = new Uint32Array(I.data.length);
      for (let i = 0; i < idx.length; i++) idx[i] = I.data[i] >>> 0;
    } else { idx = new Uint32Array(nv); for (let i = 0; i < nv; i++) idx[i] = i; }
    for (let i = 0; i < idx.length; i++) if (idx[i] >= nv) throw new Error('index out of range');
    let tris = [];
    if (mode === 4) tris = idx;
    else for (let i = 2; i < idx.length; i++) {
      if (mode === 5) { if (i % 2 === 0) tris.push(idx[i - 2], idx[i - 1], idx[i]); else tris.push(idx[i - 1], idx[i - 2], idx[i]); }
      else tris.push(idx[0], idx[i - 1], idx[i]);
    }
    let base = [1, 1, 1, 1], material = 'body', alpha = 1;
    const mat = (root.materials || [])[prim.material == null ? -1 : prim.material];
    if (mat) {
      if (mat.name) material = String(mat.name);
      const f = mat.pbrMetallicRoughness && mat.pbrMetallicRoughness.baseColorFactor;
      if (f && f.length === 4) base = f.map(Number);
      if (mat.alphaMode === 'BLEND') alpha = base[3];
    }
    // positions by m, normals by its cofactor matrix (inverse transpose x det); a mirroring transform flips the winding
    const c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    const c10 = m[9] * m[2] - m[10] * m[1], c11 = m[10] * m[0] - m[8] * m[2], c12 = m[8] * m[1] - m[9] * m[0];
    const c20 = m[1] * m[6] - m[2] * m[5], c21 = m[2] * m[4] - m[0] * m[6], c22 = m[0] * m[5] - m[1] * m[4];
    const det = m[0] * c00 + m[4] * c10 + m[8] * c20;
    const nt = Math.floor(tris.length / 3);
    const opos = new Float32Array(nt * 9), onrm = new Float32Array(nt * 9), ocol = new Float32Array(nt * 9);
    const tri = [0, 0, 0], wp = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
    for (let t = 0; t < nt; t++) {
      tri[0] = tris[t * 3]; tri[1] = tris[t * 3 + 1]; tri[2] = tris[t * 3 + 2];
      if (det < 0) { const s = tri[1]; tri[1] = tri[2]; tri[2] = s; }
      for (let k = 0; k < 3; k++) {
        const i = tri[k] * 3, x = pos[i], y = pos[i + 1], z = pos[i + 2];
        wp[k][0] = Math.fround(m[0] * x + m[4] * y + m[8] * z + m[12]);
        wp[k][1] = Math.fround(m[1] * x + m[5] * y + m[9] * z + m[13]);
        wp[k][2] = Math.fround(m[2] * x + m[6] * y + m[10] * z + m[14]);
      }
      const ax = wp[1][0] - wp[0][0], ay = wp[1][1] - wp[0][1], az = wp[1][2] - wp[0][2];
      const bx = wp[2][0] - wp[0][0], by = wp[2][1] - wp[0][1], bz = wp[2][2] - wp[0][2];
      let fx = ay * bz - az * by, fy = az * bx - ax * bz, fz = ax * by - ay * bx;
      const fl = Math.hypot(fx, fy, fz);
      if (fl > 1e-12) { fx /= fl; fy /= fl; fz /= fl; } else { fx = 0; fy = 1; fz = 0; }
      for (let k = 0; k < 3; k++) {
        const v = tri[k], o = t * 9 + k * 3;
        let nx = fx, ny = fy, nz = fz;
        if (nrm) {
          const q0 = nrm[v * 3], q1 = nrm[v * 3 + 1], q2 = nrm[v * 3 + 2];
          let ux = c00 * q0 + c10 * q1 + c20 * q2, uy = c01 * q0 + c11 * q1 + c21 * q2, uz = c02 * q0 + c12 * q1 + c22 * q2;
          if (det < 0) { ux = -ux; uy = -uy; uz = -uz; }
          const l = Math.hypot(ux, uy, uz);
          if (l > 1e-12) { nx = ux / l; ny = uy / l; nz = uz / l; }
        }
        let r = 1, g = 1, b = 1;
        if (col) { r = col[v * cc]; g = col[v * cc + 1]; b = col[v * cc + 2]; }
        opos[o] = wp[k][0]; opos[o + 1] = wp[k][1]; opos[o + 2] = wp[k][2];
        onrm[o] = nx; onrm[o + 1] = ny; onrm[o + 2] = nz;
        ocol[o] = r * base[0]; ocol[o + 1] = g * base[1]; ocol[o + 2] = b * base[2];
      }
    }
    if (nt) out.prims.push({ material, slot: Q.slotFromName(material), alpha, pos: opos, nrm: onrm, col: ocol });
  }

  G.bounds = function (mesh) {
    const mn = [1e30, 1e30, 1e30], mx = [-1e30, -1e30, -1e30];
    for (const p of mesh.prims) for (let i = 0; i < p.pos.length; i += 3)
      for (let k = 0; k < 3; k++) { const v = p.pos[i + k]; if (v < mn[k]) mn[k] = v; if (v > mx[k]) mx[k] = v; }
    return { mn, mx };
  };

  // --- write a .glb ------------------------------------------------------------------------------------------------
  // mesh: { prims: [{ material, alpha, pos, nrm, col (Float32Array, 3 per vertex), idx (Uint32Array) }] }; provenance:
  // the provenance.json entry, kept in asset.extras (the game ignores it) so the origin travels with the file
  G.writeGlb = function (mesh, name, extras, provenance) {
    const chunks = [];
    let binLen = 0;
    const views = [], accessors = [], prims = [], materials = [];
    const put = (arr, target) => {
      const u8 = new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength);
      const off = binLen;
      chunks.push(u8); binLen += u8.length;
      const pad = (4 - (binLen % 4)) % 4;
      if (pad) { chunks.push(new Uint8Array(pad)); binLen += pad; }
      views.push({ buffer: 0, byteOffset: off, byteLength: u8.length, target });
      return views.length - 1;
    };
    const acc = (view, ctype, count, type, extra) => { accessors.push(Object.assign({ bufferView: view, componentType: ctype, count, type }, extra || {})); return accessors.length - 1; };
    mesh.prims.forEach((p, i) => {
      const n = p.pos.length / 3;
      const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
      for (let k = 0; k < n; k++) for (let a = 0; a < 3; a++) { const v = p.pos[k * 3 + a]; if (v < mn[a]) mn[a] = v; if (v > mx[a]) mx[a] = v; }
      const f32 = a => a instanceof Float32Array ? a : new Float32Array(a);
      const aPos = acc(put(f32(p.pos), 34962), 5126, n, 'VEC3', { min: mn.map(r7), max: mx.map(r7) });
      const aNrm = acc(put(f32(p.nrm), 34962), 5126, n, 'VEC3');
      const aCol = acc(put(f32(p.col), 34962), 5126, n, 'VEC3');
      let idx = p.idx;
      if (!idx) { idx = new Uint32Array(n); for (let k = 0; k < n; k++) idx[k] = k; }
      const aIdx = acc(put(idx instanceof Uint32Array ? idx : new Uint32Array(idx), 34963), 5125, idx.length, 'SCALAR');
      prims.push({ attributes: { POSITION: aPos, NORMAL: aNrm, COLOR_0: aCol }, indices: aIdx, material: i, mode: 4 });
      const alpha = p.alpha == null ? 1 : p.alpha;
      const mat = { name: p.material, pbrMetallicRoughness: { baseColorFactor: [1, 1, 1, r7(alpha)], metallicFactor: 0, roughnessFactor: 1 } };
      if (alpha < 1) mat.alphaMode = 'BLEND';
      materials.push(mat);
    });
    const meshObj = { name, primitives: prims };
    if (extras) meshObj.extras = { quark: extras };
    const asset = { version: '2.0', generator: 'quark model editor' };
    if (provenance) asset.extras = { provenance };
    const gl = { asset, scene: 0, scenes: [{ nodes: [0] }], nodes: [{ name, mesh: 0 }],
      meshes: [meshObj], materials, accessors, bufferViews: views, buffers: [{ byteLength: binLen }] };
    let js = new TextEncoder().encode(JSON.stringify(gl));
    const jpad = (4 - (js.length % 4)) % 4;
    if (jpad) { const t = new Uint8Array(js.length + jpad); t.set(js); t.fill(0x20, js.length); js = t; }
    const total = 12 + 8 + js.length + 8 + binLen, out = new Uint8Array(total), dv = new DataView(out.buffer);
    dv.setUint32(0, 0x46546C67, true); dv.setUint32(4, 2, true); dv.setUint32(8, total, true);
    dv.setUint32(12, js.length, true); dv.setUint32(16, 0x4E4F534A, true); out.set(js, 20);
    let p = 20 + js.length;
    dv.setUint32(p, binLen, true); dv.setUint32(p + 4, 0x004E4942, true); p += 8;
    for (const c of chunks) { out.set(c, p); p += c.length; }
    return out;
  };
  function r7(v) { return Math.abs(v) < 1e-12 ? 0 : +(+v).toPrecision(7); }

  // a parsed mesh (flat triangles) back to the writer's layout: what the game's loader produced, so it draws the same
  G.meshForWrite = function (mesh) {
    return { prims: mesh.prims.map(p => ({ material: p.material, alpha: p.alpha, pos: p.pos, nrm: p.nrm, col: p.col, idx: null })) };
  };

  // --- boxes -------------------------------------------------------------------------------------------------------
  const B = Q.boxes = {};
  // the box renderer's faces (Renderer::emitBox / partsToMesh order, corners, shades)
  B.FACES = [
    { n: [1, 0, 0], shade: 1.0, c: [[1, -1, 1], [1, -1, -1], [1, 1, -1], [1, 1, 1]] },
    { n: [-1, 0, 0], shade: 0.92, c: [[-1, -1, -1], [-1, -1, 1], [-1, 1, 1], [-1, 1, -1]] },
    { n: [0, 1, 0], shade: 1.0, c: [[-1, 1, 1], [1, 1, 1], [1, 1, -1], [-1, 1, -1]] },
    { n: [0, -1, 0], shade: 0.8, c: [[-1, -1, -1], [1, -1, -1], [1, -1, 1], [-1, -1, 1]] },
    { n: [0, 0, 1], shade: 0.96, c: [[-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]] },
    { n: [0, 0, -1], shade: 0.96, c: [[1, -1, -1], [-1, -1, -1], [-1, 1, -1], [1, 1, -1]] },
  ];
  B.matName = p => Q.SLOT_NAMES[p.slot | 0] + ((p.alpha == null ? 1 : p.alpha) < 1 ? '_translucent' : '');
  // partsToMesh: one primitive per (slot, alpha), each box's colour at the reference palette with the face shades
  // baked in (what other viewers show), and the box list itself as extras.quark version 2: what the game draws.
  B.toMesh = function (parts, ref) {
    const groups = [];
    parts.forEach((p, i) => {
      const a = p.alpha == null ? 1 : p.alpha;
      let g = groups.find(q => q.slot === (p.slot | 0) && q.alpha === a);
      if (!g) groups.push(g = { slot: p.slot | 0, alpha: a, parts: [] });
      g.parts.push(i);
    });
    const prims = [];
    for (const g of groups) {
      const n = g.parts.length * 24, pos = new Float32Array(n * 3), nrm = new Float32Array(n * 3), col = new Float32Array(n * 3), idx = new Uint32Array(g.parts.length * 36);
      let v = 0, ii = 0;
      for (const pi of g.parts) {
        const p = parts[pi], c = B.shownColor(p, ref);
        for (const f of B.FACES) {
          const base = v;
          for (const k of f.c) {
            pos[v * 3] = p.c[0] + k[0] * p.h[0]; pos[v * 3 + 1] = p.c[1] + k[1] * p.h[1]; pos[v * 3 + 2] = p.c[2] + k[2] * p.h[2];
            nrm[v * 3] = f.n[0]; nrm[v * 3 + 1] = f.n[1]; nrm[v * 3 + 2] = f.n[2];
            col[v * 3] = c[0] * f.shade; col[v * 3 + 1] = c[1] * f.shade; col[v * 3 + 2] = c[2] * f.shade;
            v++;
          }
          idx[ii++] = base; idx[ii++] = base + 1; idx[ii++] = base + 2; idx[ii++] = base; idx[ii++] = base + 2; idx[ii++] = base + 3;
        }
      }
      prims.push({ material: B.matName({ slot: g.slot, alpha: g.alpha }), slot: g.slot, alpha: g.alpha, pos, nrm, col, idx });
    }
    return { prims, extras: B.toExtras(parts, ref) };
  };
  // extras.quark version 2 (gltf.cpp boxModelJson): numbers as floats with 9 significant digits (they read back exactly)
  const n9 = v => { const f = Math.fround(+v); return Math.abs(f) < 1e-30 ? 0 : +f.toPrecision(9); };
  const v9 = a => [n9(a[0]), n9(a[1]), n9(a[2])];
  // a box's colour at the reference (its own `color` when that agrees, so an unchanged default writes its numbers back)
  B.shownColor = function (p, ref) {
    if (!ref) return p.color;
    const c = Q.colorAtRef(p, ref);
    return c.every((v, k) => Math.abs(v - p.color[k]) <= 1e-6 * Math.max(1, v)) ? p.color : c;
  };
  B.toExtras = function (parts, ref) {
    return { version: 2, boxes: parts.map(p => {
      if ((p.slot | 0) && !p.mul && ref) p = Q.finishPart(Q.clonePart(p), ref);
      const slot = p.slot | 0, o = { c: v9(p.c), h: v9(p.h), slot: Q.SLOT_NAMES[slot], color: v9(B.shownColor(p, ref)) };
      if (slot) {
        o.mul = v9(p.mul || [1, 1, 1]); o.add = v9(p.add || [0, 0, 0]);
        if (slot !== 1 && p.teamMul && p.teamMul.some(x => n9(x) !== 0)) o.teamMul = v9(p.teamMul);
      }
      const a = p.alpha == null ? 1 : p.alpha;
      if (n9(a) !== 1) o.alpha = n9(a);
      o.shadow = p.shadow !== false; o.visOnly = !!p.visOnly; o.crouch = !!p.crouch;
      if (p.name) o.name = String(p.name);
      return o;
    }) };
  };
  // extras.quark of a file -> parts, with the game's checks (gltf.cpp boxesFrom): null when it is not a box model
  // (no extras, another version: the game draws the mesh), { error } when its box list fails them (the game logs it
  // and keeps the default)
  B.fromExtras = function (q, ref) {
    if (!q || typeof q !== 'object' || Array.isArray(q) || q.version !== 2) return null;
    const err = e => ({ error: e });
    const list = q.boxes;
    if (!Array.isArray(list) || !list.length) return err('no boxes');
    if (list.length > Q.MAX_BOXES) return err(list.length + ' boxes (at most ' + Q.MAX_BOXES + ')');
    const vec = (b, k, lo, hi, need) => {
      const a = b[k];
      if (a === undefined) return need ? false : null;
      if (!Array.isArray(a) || a.length !== 3) return false;
      for (const x of a) if (typeof x !== 'number' || !Number.isFinite(x) || x < lo || x > hi) return false;
      return a.map(Math.fround);
    };
    const parts = [];
    for (let i = 0; i < list.length; i++) {
      const b = list[i], where = 'box ' + (i + 1) + ': ';
      if (!b || typeof b !== 'object' || Array.isArray(b)) return err(where + 'not an object');
      const c = vec(b, 'c', -64, 64, true); if (!c) return err(where + 'c must be 3 numbers within 64 m');
      const h = vec(b, 'h', 0, 32, true); if (!h || !(h[0] > 0 && h[1] > 0 && h[2] > 0)) return err(where + 'h must be 3 numbers, each over 0 and at most 32 m');
      const color = vec(b, 'color', 0, 16, true); if (!color) return err(where + 'color must be 3 numbers from 0 to 16');
      let slot = 0;
      if (b.slot !== undefined) { slot = Q.SLOT_NAMES.indexOf(typeof b.slot === 'string' ? b.slot : ''); if (slot < 0) return err(where + 'unknown slot "' + b.slot + '"'); }
      const add = vec(b, 'add', 0, 16, false); if (add === false) return err(where + 'add must be 3 numbers from 0 to 16');
      const teamMul = vec(b, 'teamMul', 0, 16, false); if (teamMul === false) return err(where + 'teamMul must be 3 numbers from 0 to 16');
      const mul = vec(b, 'mul', 0, 16, false); if (mul === false) return err(where + 'mul must be 3 numbers from 0 to 16');
      const al = b.alpha;
      if (al !== undefined && (typeof al !== 'number' || !Number.isFinite(al) || al <= 0 || al > 1)) return err(where + 'alpha must be over 0 and at most 1');
      const flag = (k, def) => b[k] === undefined ? def : b[k];
      const shadow = flag('shadow', true), visOnly = flag('visOnly', false), crouch = flag('crouch', false);
      if (typeof shadow !== 'boolean' || typeof visOnly !== 'boolean' || typeof crouch !== 'boolean') return err(where + 'shadow / visOnly / crouch must be true or false');
      const p = { c, h, color, slot, mul: slot ? mul : null, add: slot ? (add || [0, 0, 0]) : null, teamMul: slot ? (teamMul || [0, 0, 0]) : null,
        alpha: al === undefined ? 1 : Math.fround(al), shadow, visOnly, crouch, name: typeof b.name === 'string' ? b.name : '' };
      if (slot && !mul) { p.mul = null; p.add = [0, 0, 0]; p.teamMul = [0, 0, 0]; }   // (finishBoxModel: colour / reference)
      if (slot === 1) p.teamMul = [0, 0, 0];   // (the game ignores a team box's teamMul: it is the team colour already)
      parts.push(ref ? Q.finishPart(p, ref) : p);
    }
    return parts;
  };
  // the boxes of a parsed file: its box list (version 2), else (allowMesh) a mesh that is exactly boxes as the
  // exporter writes them (an older export of this editor: its version 1 extras name them), else null.
  // Returns parts, null, or { error } (a version 2 list the game refuses).
  B.partsOf = function (mesh, key, defaults) {
    const ref = Q.refOf(key);
    const ex = B.fromExtras(mesh.extras, ref);
    if (ex) return ex;
    const parts = B.fromMesh(mesh);
    if (!parts) return null;
    for (const p of parts) Q.finishPart(p, ref);
    void defaults;
    return parts;
  };
  B.triangles = parts => parts.length * 12;

  // A mesh that is exactly boxes as partsToMesh writes them (36 corners each, faces in order or not, the face shades
  // in the colours) back to parts; null when it isn't (then it is edited as a whole, not box by box).
  B.fromMesh = function (mesh) {
    const parts = [], ex = mesh.extras && mesh.extras.version === 1 && mesh.extras.boxes;
    for (let pi = 0; pi < mesh.prims.length; pi++) {
      const pr = mesh.prims[pi], nv = pr.pos.length / 3;
      if (nv % 36) return null;
      for (let b = 0; b < nv / 36; b++) {
        const part = boxAt(pr, b * 36);
        if (!part) return null;
        part.slot = pr.slot; part.alpha = pr.alpha;
        const meta = ex && Array.isArray(ex[pi]) && ex[pi][b];   // (version 1 extras: this editor's older exports)
        if (meta) { part.name = meta.name || ''; part.shadow = meta.shadow !== false; part.visOnly = !!meta.visOnly; part._i = meta.i; }
        parts.push(part);
      }
    }
    if (!parts.length) return null;
    if (parts.every(p => typeof p._i === 'number')) parts.sort((a, b) => a._i - b._i);
    for (const p of parts) delete p._i;
    return parts;
  };
  function boxAt(pr, v0) {
    const P = pr.pos, N = pr.nrm, C = pr.col;
    const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (let v = v0; v < v0 + 36; v++) for (let k = 0; k < 3; k++) { const x = P[v * 3 + k]; if (x < mn[k]) mn[k] = x; if (x > mx[k]) mx[k] = x; }
    const ext = Math.max(mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]), eps = Math.max(1e-6, ext * 1e-4);
    if (!(ext > 0)) return null;
    const seen = [0, 0, 0, 0, 0, 0], fc = [];
    for (let t = 0; t < 12; t++) {
      const v = v0 + t * 3;
      // every corner on the box's bounds; the triangle's normal along one axis, its corners on that face
      let face = -1;
      const nx = N[v * 3], ny = N[v * 3 + 1], nz = N[v * 3 + 2];
      for (let f = 0; f < 6; f++) { const n = B.FACES[f].n; if (Math.abs(nx - n[0]) < 1e-3 && Math.abs(ny - n[1]) < 1e-3 && Math.abs(nz - n[2]) < 1e-3) face = f; }
      if (face < 0) return null;
      const ax = face >> 1, onMax = (face & 1) === 0;
      for (let k = 0; k < 3; k++) {
        const w = v + k;
        for (let a = 0; a < 3; a++) { const x = P[w * 3 + a]; if (Math.abs(x - mn[a]) > eps && Math.abs(x - mx[a]) > eps) return null; }
        if (Math.abs(P[w * 3 + ax] - (onMax ? mx[ax] : mn[ax])) > eps) return null;
        if (Math.abs(N[w * 3] - nx) > 1e-4 || Math.abs(N[w * 3 + 1] - ny) > 1e-4 || Math.abs(N[w * 3 + 2] - nz) > 1e-4) return null;
      }
      const sh = B.FACES[face].shade, c = [C[v * 3] / sh, C[v * 3 + 1] / sh, C[v * 3 + 2] / sh];
      for (let k = 1; k < 3; k++) for (let a = 0; a < 3; a++) if (Math.abs(C[(v + k) * 3 + a] - C[v * 3 + a]) > 1e-5) return null;
      seen[face]++;
      fc.push(c);
      if (face === 0 && !fc.x) fc.x = c;
    }
    for (let f = 0; f < 6; f++) if (seen[f] !== 2) return null;
    const col = fc.x;
    for (const c of fc) for (let a = 0; a < 3; a++) if (Math.abs(c[a] - col[a]) > 2e-3 * Math.max(1, col[a])) return null;
    return { c: [r6((mn[0] + mx[0]) / 2), r6((mn[1] + mx[1]) / 2), r6((mn[2] + mx[2]) / 2)], h: [r6((mx[0] - mn[0]) / 2), r6((mx[1] - mn[1]) / 2), r6((mx[2] - mn[2]) / 2)],
      color: col.map(r6), slot: 0, mul: null, add: null, teamMul: null, alpha: 1, shadow: true, visOnly: false, crouch: false, name: '' };
  }
  function r6(v) { return Math.round(v * 1e6) / 1e6; }

  // an imported / default mesh in the writer's layout, or boxes
  G.exportBytes = function (key) {
    const cur = Q.current(key), m = Q.models[key];
    const name = m ? m.name : key.split('/').pop();
    const prov = Q.provFor ? Q.provFor(key) : null;
    if (cur.type === 'boxes') { const mesh = B.toMesh(cur.parts, Q.refOf(key)); return G.writeGlb(mesh, name, mesh.extras, prov); }
    if (cur.type === 'mesh') return G.writeGlb(G.meshForWrite(cur.mesh), name, null, prov);
    return null;
  };
})();
