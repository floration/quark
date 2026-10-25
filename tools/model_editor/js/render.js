/* ===== RENDER ========================================================================================
   WebGL2, no libraries. The game's dynamic-object look (src/client/renderer.cpp kDynVS / kDynFS: sky + sun light with
   the sun visibility, the 1 px box outlines, fog, the shadow map with its 4 taps; kFlatFS for translucent parts and
   trails; the sky shader), its camera (horizontal fov, near 0.05) and its passes (shadow, world, dynamic, translucent,
   then the view model after a depth clear). One vertex buffer and one draw per model; buffers are rebuilt only when a
   model changes (or, for the few animated box parts, refilled in place). Nothing is allocated per frame. */
(function () {
  'use strict';
  const Q = window.Q;
  const R = Q.R = {};
  const OP = 20, FL = 14;   // floats per opaque / flat vertex (below)
  R.FL = FL;

  // --- small math (column-major, out parameters: no allocation in the frame loop) ---------------------------------------
  const M = R.mat = {
    ident(o) { o.fill(0); o[0] = o[5] = o[10] = o[15] = 1; return o; },
    mul(o, a, b) {   // o = a * b (o may alias neither)
      for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++)
        o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
      return o;
    },
    perspective(o, fovy, aspect, zn, zf) {
      const f = 1 / Math.tan(fovy / 2); o.fill(0);
      o[0] = f / aspect; o[5] = f; o[10] = (zf + zn) / (zn - zf); o[11] = -1; o[14] = 2 * zf * zn / (zn - zf); return o;
    },
    ortho(o, l, r, b, t, n, f) {
      M.ident(o); o[0] = 2 / (r - l); o[5] = 2 / (t - b); o[10] = -2 / (f - n);
      o[12] = -(r + l) / (r - l); o[13] = -(t + b) / (t - b); o[14] = -(f + n) / (f - n); return o;
    },
    lookAt(o, ex, ey, ez, ax, ay, az, ux, uy, uz) {
      let fx = ax - ex, fy = ay - ey, fz = az - ez; const fl = Math.hypot(fx, fy, fz) || 1; fx /= fl; fy /= fl; fz /= fl;
      let sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux; const sl = Math.hypot(sx, sy, sz) || 1; sx /= sl; sy /= sl; sz /= sl;
      const vx = sy * fz - sz * fy, vy = sz * fx - sx * fz, vz = sx * fy - sy * fx;
      M.ident(o);
      o[0] = sx; o[4] = sy; o[8] = sz; o[1] = vx; o[5] = vy; o[9] = vz; o[2] = -fx; o[6] = -fy; o[10] = -fz;
      o[12] = -(sx * ex + sy * ey + sz * ez); o[13] = -(vx * ex + vy * ey + vz * ez); o[14] = fx * ex + fy * ey + fz * ez; return o;
    },
    // o = T(t) * Ry(yaw) * Rx(pitch) * S(sx, sy, sz)
    trs(o, tx, ty, tz, yaw, pitch, sx, sy, sz) {
      const cy = Math.cos(yaw), sny = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
      // Ry * Rx columns: x = (cy, 0, -sy), y = (sy*sp, cp, cy*sp), z = (sy*cp, -sp, cy*cp)
      o[0] = cy * sx; o[1] = 0; o[2] = -sny * sx; o[3] = 0;
      o[4] = sny * sp * sy; o[5] = cp * sy; o[6] = cy * sp * sy; o[7] = 0;
      o[8] = sny * cp * sz; o[9] = -sp * sz; o[10] = cy * cp * sz; o[11] = 0;
      o[12] = tx; o[13] = ty; o[14] = tz; o[15] = 1; return o;
    },
    invert(o, m) {
      const a = m, b00 = a[0] * a[5] - a[1] * a[4], b01 = a[0] * a[6] - a[2] * a[4], b02 = a[0] * a[7] - a[3] * a[4], b03 = a[1] * a[6] - a[2] * a[5];
      const b04 = a[1] * a[7] - a[3] * a[5], b05 = a[2] * a[7] - a[3] * a[6], b06 = a[8] * a[13] - a[9] * a[12], b07 = a[8] * a[14] - a[10] * a[12];
      const b08 = a[8] * a[15] - a[11] * a[12], b09 = a[9] * a[14] - a[10] * a[13], b10 = a[9] * a[15] - a[11] * a[13], b11 = a[10] * a[15] - a[11] * a[14];
      let det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
      if (!det) return null; det = 1 / det;
      o[0] = (a[5] * b11 - a[6] * b10 + a[7] * b09) * det; o[1] = (a[2] * b10 - a[1] * b11 - a[3] * b09) * det;
      o[2] = (a[13] * b05 - a[14] * b04 + a[15] * b03) * det; o[3] = (a[10] * b04 - a[9] * b05 - a[11] * b03) * det;
      o[4] = (a[6] * b08 - a[4] * b11 - a[7] * b07) * det; o[5] = (a[0] * b11 - a[2] * b08 + a[3] * b07) * det;
      o[6] = (a[14] * b02 - a[12] * b05 - a[15] * b01) * det; o[7] = (a[8] * b05 - a[10] * b02 + a[11] * b01) * det;
      o[8] = (a[4] * b10 - a[5] * b08 + a[7] * b06) * det; o[9] = (a[1] * b08 - a[0] * b10 - a[3] * b06) * det;
      o[10] = (a[12] * b04 - a[13] * b02 + a[15] * b00) * det; o[11] = (a[9] * b02 - a[8] * b04 - a[11] * b00) * det;
      o[12] = (a[5] * b07 - a[4] * b09 - a[6] * b06) * det; o[13] = (a[0] * b09 - a[1] * b07 + a[2] * b06) * det;
      o[14] = (a[13] * b01 - a[12] * b03 - a[14] * b00) * det; o[15] = (a[8] * b03 - a[9] * b01 + a[10] * b00) * det;
      return o;
    },
  };
  R.forward = (yaw, pitch, o) => { o = o || [0, 0, 0]; o[0] = -Math.sin(yaw) * Math.cos(pitch); o[1] = Math.sin(pitch); o[2] = -Math.cos(yaw) * Math.cos(pitch); return o; };

  // --- shaders (GLSL ES 3.00 ports) ------------------------------------------------------------------------------------
  const OUTLINE_FN = `
float outline(vec4 e, float w) {
    if (w <= 0.0) return 1.0;
    vec2 d2 = min(e.xy, e.zw - e.xy);
    float d = min(d2.x, d2.y);
    float px = fwidth(d);
    return smoothstep(w, w + 1.0, d / max(px, 1e-5));
}
uniform highp sampler2DShadow uShadow;
uniform int uShadowOn;
uniform float uShadowTexel;
float shadowAt(vec4 sc) {
    if (uShadowOn == 0) return 1.0;
    vec3 p = sc.xyz / sc.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0 || p.z >= 1.0) return 1.0;
    float s = 0.0;
    s += texture(uShadow, vec3(p.xy + vec2(-0.5, -0.5) * uShadowTexel, p.z - 0.0015));
    s += texture(uShadow, vec3(p.xy + vec2( 0.5, -0.5) * uShadowTexel, p.z - 0.0015));
    s += texture(uShadow, vec3(p.xy + vec2(-0.5,  0.5) * uShadowTexel, p.z - 0.0015));
    s += texture(uShadow, vec3(p.xy + vec2( 0.5,  0.5) * uShadowTexel, p.z - 0.0015));
    return s * 0.25;
}`;
  const DYN_VS = `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;   // mul (slot 0: the colour), tint slot
layout(location=3) in vec4 aEdge;
layout(location=4) in vec3 aAdd;
layout(location=5) in vec3 aTeamMul;
uniform mat4 uViewProj, uModel, uShadowMat;
uniform vec3 uTint[6];               // the slot colours (drivenPalette; slot 0 = 1)
out vec3 vPos; out vec3 vN; out vec3 vColor; out vec4 vShadow; out vec4 vEdge;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vec3 n = normalize(mat3(uModel) * aNormal);
    vPos = wp.xyz; vN = n; vEdge = aEdge;
    vColor = aColor.rgb * uTint[int(aColor.a + 0.5)] + aTeamMul * uTint[1] + aAdd;   // slot * mul + team * teamMul + add
    vShadow = uShadowMat * vec4(wp.xyz + n * 0.03, 1.0);
    gl_Position = uViewProj * wp;
}`;
  const DYN_FS = `#version 300 es
precision highp float;
${OUTLINE_FN}
in vec3 vPos; in vec3 vN; in vec3 vColor; in vec4 vShadow; in vec4 vEdge;
uniform vec3 uSunColor, uSkyColor, uSunDir, uFogColor, uCamPos, uOutline;
uniform float uFogDensity, uBright, uSunVis, uOutlineW;
uniform int uReceiveShadow;
uniform vec4 uHighlight;   // (editor: rgb + amount, the selected model in a lineup; 0 = the game's look)
out vec4 fragColor;
void main() {
    float sh = uReceiveShadow != 0 ? shadowAt(vShadow) : 1.0;
    float ndl = max(dot(normalize(vN), uSunDir), 0.0);
    vec3 light = uSkyColor * (0.6 + 0.25 * vN.y) + uSunColor * ndl * uSunVis * sh;
    vec3 c = mix(uOutline, vColor * (light * 0.7 + 0.3), outline(vEdge, uOutlineW));
    float d = length(vPos - uCamPos);
    c = mix(c, uFogColor, 1.0 - exp(-d * uFogDensity)) * uBright;
    c = mix(c, uHighlight.rgb, uHighlight.a);
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}`;
  const FLAT_VS = `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
layout(location=2) in float aSlot;
layout(location=3) in vec3 aAdd;
layout(location=4) in vec3 aTeamMul;
uniform mat4 uViewProj, uModel;
uniform vec3 uTint[6];
out vec4 vColor;
void main() { vColor = vec4(aColor.rgb * uTint[int(aSlot + 0.5)] + aTeamMul * uTint[1] + aAdd, aColor.a); gl_Position = uViewProj * uModel * vec4(aPos, 1.0); }`;
  const FLAT_FS = `#version 300 es
precision highp float;
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(pow(max(vColor.rgb, 0.0), vec3(1.0 / 2.2)), vColor.a); }`;
  const SHADOW_VS = `#version 300 es
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj, uModel;
void main() { gl_Position = uViewProj * uModel * vec4(aPos, 1.0); }`;
  const SHADOW_FS = `#version 300 es
precision mediump float;
out vec4 fragColor;
void main() { fragColor = vec4(1.0); }`;
  const SKY_VS = `#version 300 es
out vec2 vNdc;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vNdc = p; gl_Position = vec4(p, 1.0, 1.0);
}`;
  const SKY_FS = `#version 300 es
precision highp float;
in vec2 vNdc;
uniform vec3 uFwd, uRight, uUp, uSky, uSunDir;
uniform vec2 uTan;
uniform float uBright;
out vec4 fragColor;
void main() {
    vec3 d = normalize(uFwd + vNdc.x * uTan.x * uRight + vNdc.y * uTan.y * uUp);
    float up = clamp(d.y, 0.0, 1.0);
    vec3 zenith = uSky * vec3(0.55, 0.62, 0.8);
    vec3 c = mix(uSky, zenith, pow(up, 0.6));
    float s = max(dot(d, uSunDir), 0.0);
    c += vec3(1.0, 0.9, 0.7) * (pow(s, 600.0) * 4.0 + pow(s, 12.0) * 0.25);
    float dark = 1.0 - smoothstep(0.03, 0.08, dot(uSky, vec3(0.3, 0.59, 0.11)));
    if (dark > 0.0) {
        vec3 g = d * 260.0, cell = floor(g);
        float h = fract(sin(dot(cell, vec3(127.1, 311.7, 74.7))) * 43758.5453);
        if (h > 0.985) {
            vec3 j = fract(vec3(h * 17.0, h * 31.0, h * 47.0)) * 0.5 + 0.25;
            float r = length(g - cell - j);
            float b = (h - 0.985) / 0.015;
            c += mix(vec3(0.7, 0.8, 1.0), vec3(1.0, 0.9, 0.75), fract(h * 91.0)) * smoothstep(0.22, 0.0, r) * (0.6 + 2.4 * b * b) * dark;
        }
    }
    c *= uBright;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0) * 1.08;
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}`;
  // a stand-in floor (the game's world shader without its lightmap: sky light, sun, the dynamic shadows, fog, filmic)
  const GROUND_VS = `#version 300 es
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj, uShadowMat;
out vec3 vPos; out vec4 vShadow;
void main() { vPos = aPos; vShadow = uShadowMat * vec4(aPos + vec3(0.0, 0.04, 0.0), 1.0); gl_Position = uViewProj * vec4(aPos, 1.0); }`;
  const GROUND_FS = `#version 300 es
precision highp float;
${OUTLINE_FN}
in vec3 vPos; in vec4 vShadow;
uniform vec3 uSunColor, uSkyColor, uSunDir, uFogColor, uCamPos, uAlbedo;
uniform float uFogDensity, uBright, uGrid;
out vec4 fragColor;
void main() {
    vec2 pp = vPos.xz * 0.25, f = fract(pp), w = fwidth(pp) * 1.5;
    float seam = min(smoothstep(0.0, w.x + 0.012, min(f.x, 1.0 - f.x)), smoothstep(0.0, w.y + 0.012, min(f.y, 1.0 - f.y)));
    vec3 albedo = uAlbedo * mix(0.82, 1.0, seam);
    vec3 light = uSkyColor + uSunColor * max(uSunDir.y, 0.0) * shadowAt(vShadow);
    vec3 c = albedo * light * 0.62;
    if (uGrid > 0.0) {   // editor grid: 0.1 m lines, 1 m stronger
        vec2 g1 = abs(fract(vPos.xz + 0.5) - 0.5) / fwidth(vPos.xz), g2 = abs(fract(vPos.xz * 10.0 + 0.5) - 0.5) / fwidth(vPos.xz * 10.0);
        float l1 = 1.0 - min(min(g1.x, g1.y), 1.0), l2 = 1.0 - min(min(g2.x, g2.y), 1.0);
        c = mix(c, vec3(0.0), uGrid * max(l1 * 0.55, l2 * 0.18));
    }
    float d = length(vPos - uCamPos);
    c = mix(c, uFogColor, 1.0 - exp(-d * uFogDensity)) * uBright;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0) * 1.08;
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}`;
  const LINE_VS = `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = uViewProj * vec4(aPos, 1.0); }`;
  const LINE_FS = `#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }`;

  let gl = null, canvas = null;
  const P = {};   // programs: { prog, u: {name: location} }
  function compile(vs, fs, names) {
    const sh = (type, src) => {
      const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s) + '\n' + src.split('\n').slice(0, 3).join('\n'));
      return s;
    };
    const p = gl.createProgram();
    gl.attachShader(p, sh(gl.VERTEX_SHADER, vs)); gl.attachShader(p, sh(gl.FRAGMENT_SHADER, fs)); gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    const u = {};
    for (const n of names) u[n] = gl.getUniformLocation(p, n);
    return { prog: p, u };
  }

  const SHADOW_RES = 2048;   // config.h shadowRes
  let shadowTex = null, shadowFbo = null, groundVao = null, emptyVao = null, lineVao = null, lineVbo = null, lineCap = 0;
  let trailVao = null, trailVbo = null, trailCap = 0, pickFbo = null, pickTex = null, pickDepth = null, pickW = 0, pickH = 0;
  R.init = function (cv, opts) {
    canvas = cv;
    gl = cv.getContext('webgl2', Object.assign({ antialias: false, alpha: false, preserveDrawingBuffer: true, powerPreference: 'high-performance' }, opts || {}));
    if (!gl) throw new Error('WebGL2 is not available in this browser');
    const common = ['uViewProj', 'uModel', 'uShadowMat', 'uTint', 'uShadow', 'uShadowOn', 'uShadowTexel'];
    P.dyn = compile(DYN_VS, DYN_FS, common.concat(['uSunColor', 'uSkyColor', 'uSunDir', 'uFogColor', 'uCamPos', 'uOutline', 'uFogDensity', 'uBright', 'uSunVis', 'uOutlineW', 'uReceiveShadow', 'uHighlight']));
    P.flat = compile(FLAT_VS, FLAT_FS, ['uViewProj', 'uModel', 'uTint']);
    P.shadow = compile(SHADOW_VS, SHADOW_FS, ['uViewProj', 'uModel']);
    P.sky = compile(SKY_VS, SKY_FS, ['uFwd', 'uRight', 'uUp', 'uSky', 'uSunDir', 'uTan', 'uBright']);
    P.ground = compile(GROUND_VS, GROUND_FS, ['uViewProj', 'uShadowMat', 'uShadow', 'uShadowOn', 'uShadowTexel', 'uSunColor', 'uSkyColor', 'uSunDir', 'uFogColor', 'uCamPos', 'uAlbedo', 'uFogDensity', 'uBright', 'uGrid']);
    P.line = compile(LINE_VS, LINE_FS, ['uViewProj']);
    shadowTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, shadowTex);
    gl.texStorage2D(gl.TEXTURE_2D, 1, gl.DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_MODE, gl.COMPARE_REF_TO_TEXTURE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_FUNC, gl.LEQUAL);
    shadowFbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, shadowFbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, shadowTex, 0);
    gl.drawBuffers([gl.NONE]); gl.readBuffer(gl.NONE);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    // the floor: one 200 m quad (y set per scene by uViewProj's translation: drawn at 0, scenes move the world instead)
    groundVao = gl.createVertexArray(); gl.bindVertexArray(groundVao);
    const gb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, gb);
    const S = 100;
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-S, 0, -S, -S, 0, S, S, 0, S, -S, 0, -S, S, 0, S, S, 0, -S]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 12, 0);
    emptyVao = gl.createVertexArray();
    lineVao = gl.createVertexArray(); gl.bindVertexArray(lineVao);
    lineVbo = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, lineVbo);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);
    gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 28, 12);
    trailVao = gl.createVertexArray(); gl.bindVertexArray(trailVao);
    trailVbo = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, trailVbo);
    flatLayout();
    gl.bindVertexArray(null);
    return gl;
  };
  R.gl = () => gl;
  function flatLayout() {   // pos 3, mul 3 + alpha, slot, add 3, teamMul 3 (FL floats)
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, FL * 4, 0);
    gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 4, gl.FLOAT, false, FL * 4, 12);
    gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 1, gl.FLOAT, false, FL * 4, 28);
    gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 3, gl.FLOAT, false, FL * 4, 32);
    gl.enableVertexAttribArray(4); gl.vertexAttribPointer(4, 3, gl.FLOAT, false, FL * 4, 44);
  }

  // --- GPU meshes ---------------------------------------------------------------------------------------------------
  // Opaque vertex: pos 3, normal 3, mul 3 + slot, edge 4, add 3, teamMul 3 (20 floats). Flat vertex: pos 3, mul 3 +
  // alpha, slot, add 3, teamMul 3 (14). The colour is made in the shader: slot colour * mul + team * teamMul + add (the
  // box model formula; a mesh: mul = its colour / the reference, add 0). Casters first: the shadow pass draws [0, casters).
  R.createMesh = function () {
    const m = { vao: gl.createVertexArray(), vbo: gl.createBuffer(), cap: 0, count: 0, casters: 0,
      fvao: gl.createVertexArray(), fvbo: gl.createBuffer(), fcap: 0, fcount: 0, op: new Float32Array(1024), fl: new Float32Array(256),
      mn: [0, 0, 0], mx: [0, 0, 0] };
    gl.bindVertexArray(m.vao); gl.bindBuffer(gl.ARRAY_BUFFER, m.vbo);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, OP * 4, 0);
    gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, OP * 4, 12);
    gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 4, gl.FLOAT, false, OP * 4, 24);
    gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 4, gl.FLOAT, false, OP * 4, 40);
    gl.enableVertexAttribArray(4); gl.vertexAttribPointer(4, 3, gl.FLOAT, false, OP * 4, 56);
    gl.enableVertexAttribArray(5); gl.vertexAttribPointer(5, 3, gl.FLOAT, false, OP * 4, 68);
    gl.bindVertexArray(m.fvao); gl.bindBuffer(gl.ARRAY_BUFFER, m.fvbo); flatLayout();
    gl.bindVertexArray(null);
    return m;
  };
  R.deleteMesh = function (m) { if (!m) return; gl.deleteVertexArray(m.vao); gl.deleteBuffer(m.vbo); gl.deleteVertexArray(m.fvao); gl.deleteBuffer(m.fvbo); };
  function grow(arr, n) { if (arr.length >= n) return arr; let c = arr.length; while (c < n) c *= 2; return new Float32Array(c); }
  function upload(m) {
    gl.bindBuffer(gl.ARRAY_BUFFER, m.vbo);
    if (m.count * OP > m.cap) { m.cap = m.op.length; gl.bufferData(gl.ARRAY_BUFFER, m.cap * 4, gl.DYNAMIC_DRAW); }
    if (m.count) gl.bufferSubData(gl.ARRAY_BUFFER, 0, m.op, 0, m.count * OP);
    gl.bindBuffer(gl.ARRAY_BUFFER, m.fvbo);
    if (m.fcount * FL > m.fcap) { m.fcap = m.fl.length; gl.bufferData(gl.ARRAY_BUFFER, m.fcap * 4, gl.DYNAMIC_DRAW); }
    if (m.fcount) gl.bufferSubData(gl.ARRAY_BUFFER, 0, m.fl, 0, m.fcount * FL);
  }
  // boxes. opts: { outline: edges for the 1 px outline (the box path), flatAlpha: translucent parts go to the flat pass
  // (else drawn opaque, as the game's box path does outside projectiles), flatShaded: their colour with the face shade,
  // noShadowFlags: every part casts, hs + crouch: a player's crouch ('parts': every centre's height times hs, a half
  // height too where the part says crouch; 'centres': the centres only, the bomb on its carrier's back) }
  const FACES = () => Q.boxes.FACES, Z3 = [0, 0, 0];
  R.fillBoxes = function (m, parts, opts) {
    let nOp = 0, nFl = 0;
    for (const p of parts) { if (opts.flatAlpha && (p.alpha == null ? 1 : p.alpha) < 0.999) nFl += 36; else nOp += 36; }
    m.op = grow(m.op, nOp * OP); m.fl = grow(m.fl, nFl * FL);
    const op = m.op, fl = m.fl, F = FACES(), hs = opts.hs || 1, crouchParts = opts.crouch === 'parts';
    let o = 0, f = 0, casters = 0;
    const mn = m.mn, mx = m.mx; mn[0] = mn[1] = mn[2] = Infinity; mx[0] = mx[1] = mx[2] = -Infinity;
    for (let pass = 0; pass < 2; pass++) for (const p of parts) {
      const a = p.alpha == null ? 1 : p.alpha, isFlat = opts.flatAlpha && a < 0.999;
      const casts = opts.noShadowFlags || p.shadow !== false;
      if (!isFlat && (pass === 0) !== casts) continue;
      if (isFlat && pass === 1) continue;
      const slot = p.slot | 0, mul = slot ? (p.mul || p.color) : p.color, add = slot && p.add ? p.add : Z3, tm = slot && p.teamMul ? p.teamMul : Z3;
      const cx = p.c[0], cy = p.c[1] * hs, cz = p.c[2], hx = p.h[0], hy = crouchParts && p.crouch ? p.h[1] * hs : p.h[1], hz = p.h[2];
      if (cx - hx < mn[0]) mn[0] = cx - hx; if (cy - hy < mn[1]) mn[1] = cy - hy; if (cz - hz < mn[2]) mn[2] = cz - hz;
      if (cx + hx > mx[0]) mx[0] = cx + hx; if (cy + hy > mx[1]) mx[1] = cy + hy; if (cz + hz > mx[2]) mx[2] = cz + hz;
      for (const face of F) {
        const sh = isFlat && !opts.flatShaded ? 1 : face.shade;
        const c = face.c;
        // face size for the outline (addQuad: w = |c1 - c0|, h = |c3 - c0|)
        let wx = (c[1][0] - c[0][0]) * hx, wy = (c[1][1] - c[0][1]) * hy, wz = (c[1][2] - c[0][2]) * hz;
        const w = Math.hypot(wx, wy, wz);
        wx = (c[3][0] - c[0][0]) * hx; wy = (c[3][1] - c[0][1]) * hy; wz = (c[3][2] - c[0][2]) * hz;
        const h = Math.hypot(wx, wy, wz);
        for (let t = 0; t < 6; t++) {
          const ci = t < 3 ? t : (t === 3 ? 0 : t === 4 ? 2 : 3), k = c[ci];
          const x = cx + k[0] * hx, y = cy + k[1] * hy, z = cz + k[2] * hz;
          if (isFlat) {
            fl[f++] = x; fl[f++] = y; fl[f++] = z; fl[f++] = mul[0] * sh; fl[f++] = mul[1] * sh; fl[f++] = mul[2] * sh; fl[f++] = a; fl[f++] = slot;
            fl[f++] = add[0] * sh; fl[f++] = add[1] * sh; fl[f++] = add[2] * sh; fl[f++] = tm[0] * sh; fl[f++] = tm[1] * sh; fl[f++] = tm[2] * sh;
          } else {
            op[o++] = x; op[o++] = y; op[o++] = z; op[o++] = face.n[0]; op[o++] = face.n[1]; op[o++] = face.n[2];
            op[o++] = mul[0] * sh; op[o++] = mul[1] * sh; op[o++] = mul[2] * sh; op[o++] = slot;
            if (opts.outline) { op[o++] = ci === 1 || ci === 2 ? w : 0; op[o++] = ci >= 2 ? h : 0; op[o++] = w; op[o++] = h; }
            else { op[o++] = 0.5; op[o++] = 0.5; op[o++] = 1; op[o++] = 1; }
            op[o++] = add[0] * sh; op[o++] = add[1] * sh; op[o++] = add[2] * sh; op[o++] = tm[0] * sh; op[o++] = tm[1] * sh; op[o++] = tm[2] * sh;
          }
        }
      }
      if (!isFlat && pass === 0) casters += 36;
    }
    m.count = o / OP; m.casters = casters; m.fcount = f / FL;
    if (!isFinite(mn[0])) { mn.fill(0); mx.fill(0); }
    upload(m);
    return m;
  };
  // a mesh as the game's loader made it (an override): every opaque triangle casts, no outline; a slot's colours are
  // tinted by current / reference (ref: the model's reference palette), the same as mul = colour / reference
  R.fillMesh = function (m, mesh, ref) {
    let nOp = 0, nFl = 0;
    for (const p of mesh.prims) { if (p.alpha < 0.999) nFl += p.pos.length / 3; else nOp += p.pos.length / 3; }
    m.op = grow(m.op, nOp * OP); m.fl = grow(m.fl, nFl * FL);
    const op = m.op, fl = m.fl;
    let o = 0, f = 0;
    const mn = m.mn, mx = m.mx; mn[0] = mn[1] = mn[2] = Infinity; mx[0] = mx[1] = mx[2] = -Infinity;
    for (const p of mesh.prims) {
      const n = p.pos.length / 3, isFlat = p.alpha < 0.999, r = p.slot && ref ? ref[p.slot] : null;
      const k0 = r && r[0] > 1e-3 ? 1 / r[0] : 1, k1 = r && r[1] > 1e-3 ? 1 / r[1] : 1, k2 = r && r[2] > 1e-3 ? 1 / r[2] : 1;
      for (let i = 0; i < n; i++) {
        const x = p.pos[i * 3], y = p.pos[i * 3 + 1], z = p.pos[i * 3 + 2];
        if (x < mn[0]) mn[0] = x; if (y < mn[1]) mn[1] = y; if (z < mn[2]) mn[2] = z;
        if (x > mx[0]) mx[0] = x; if (y > mx[1]) mx[1] = y; if (z > mx[2]) mx[2] = z;
        const c0 = p.col[i * 3] * k0, c1 = p.col[i * 3 + 1] * k1, c2 = p.col[i * 3 + 2] * k2;
        if (isFlat) { fl[f++] = x; fl[f++] = y; fl[f++] = z; fl[f++] = c0; fl[f++] = c1; fl[f++] = c2; fl[f++] = p.alpha; fl[f++] = p.slot; for (let q = 0; q < 6; q++) fl[f++] = 0; }
        else {
          op[o++] = x; op[o++] = y; op[o++] = z; op[o++] = p.nrm[i * 3]; op[o++] = p.nrm[i * 3 + 1]; op[o++] = p.nrm[i * 3 + 2];
          op[o++] = c0; op[o++] = c1; op[o++] = c2; op[o++] = p.slot;
          op[o++] = 0.5; op[o++] = 0.5; op[o++] = 1; op[o++] = 1;
          for (let q = 0; q < 6; q++) op[o++] = 0;
        }
      }
    }
    m.count = o / OP; m.casters = m.count; m.fcount = f / FL;
    if (!isFinite(mn[0])) { mn.fill(0); mx.fill(0); }
    upload(m);
    return m;
  };

  // --- the frame -----------------------------------------------------------------------------------------------------
  // scene: { cam: { pos, yaw, pitch, fov }, pal (map palette), sky, ground: { y, grid, z (an offset along z: the
  //   in-flight view streams it) } | null, shadows, bright,
  //   items: [{ mesh, model (Float32Array 16), tint (Float32Array 18: the slot colours), outlineW, outline [3], sunVis, view (bool), highlight }],
  //   trails: { data: Float32Array, count }, lines: { data, count, depth } , overlay: { data, count } }
  const vp = new Float32Array(16), proj = new Float32Array(16), view = new Float32Array(16), shadowMat = new Float32Array(16), lv = new Float32Array(16);
  const tmp = new Float32Array(16), fwd = [0, 0, 0], ID = M.ident(new Float32Array(16)), NOHL = new Float32Array(4), ONE_TINT = new Float32Array(18).fill(1);
  let gtrans = new Float32Array(16);
  R.viewProj = vp;
  R.camera = function (cam, w, h) {   // Renderer::setCamera
    const aspect = w / Math.max(1, h), vfov = 2 * Math.atan(Math.tan(cam.fov * Math.PI / 360) / aspect);
    M.perspective(proj, vfov, aspect, Q.NEAR, Q.FAR);
    R.forward(cam.yaw, cam.pitch, fwd);
    M.lookAt(view, cam.pos[0], cam.pos[1], cam.pos[2], cam.pos[0] + fwd[0], cam.pos[1] + fwd[1], cam.pos[2] + fwd[2], 0, 1, 0);
    M.mul(vp, proj, view);
    R.tanY = Math.tan(vfov / 2); R.tanX = R.tanY * aspect;
    return vp;
  };
  // the shadow map's matrix, as prepareScene (R = 32 m around a point 16 m ahead, snapped to texels)
  const UP_Z = [0, 0, 1], UP_Y = [0, 1, 0];
  function shadowMatrix(sc) {
    const s = sc.pal.sun, R2 = 32, up = Math.abs(s[1]) > 0.99 ? UP_Z : UP_Y;
    M.lookAt(lv, s[0] * 100, s[1] * 100, s[2] * 100, 0, 0, 0, up[0], up[1], up[2]);
    const f0 = -Math.sin(sc.cam.yaw), f2 = -Math.cos(sc.cam.yaw);
    const fx = sc.cam.pos[0] + f0 * R2 * 0.5, fy = sc.cam.pos[1], fz = sc.cam.pos[2] + f2 * R2 * 0.5;
    let cx = lv[0] * fx + lv[4] * fy + lv[8] * fz + lv[12], cy = lv[1] * fx + lv[5] * fy + lv[9] * fz + lv[13];
    const cz = lv[2] * fx + lv[6] * fy + lv[10] * fz + lv[14];
    const texel = 2 * R2 / SHADOW_RES;
    cx = Math.floor(cx / texel) * texel; cy = Math.floor(cy / texel) * texel;
    M.ortho(tmp, cx - R2, cx + R2, cy - R2, cy + R2, -cz - 120, -cz + 120);
    M.mul(shadowMat, tmp, lv);
  }
  R.draw = function (sc, target) {
    const w = target ? target.w : canvas.width, h = target ? target.h : canvas.height;
    R.camera(sc.cam, w, h);
    const pal = sc.pal, sky = pal.sky, useShadows = sc.shadows !== false;
    if (useShadows) {
      shadowMatrix(sc);
      gl.bindFramebuffer(gl.FRAMEBUFFER, shadowFbo);
      gl.viewport(0, 0, SHADOW_RES, SHADOW_RES);
      gl.clear(gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL); gl.disable(gl.CULL_FACE);
      gl.useProgram(P.shadow.prog);
      gl.uniformMatrix4fv(P.shadow.u.uViewProj, false, shadowMat);
      for (let i = 0; i < sc.items.length; i++) {
        const it = sc.items[i];
        if (it.view || it.hidden || !it.castShadow || !it.mesh.casters) continue;
        gl.uniformMatrix4fv(P.shadow.u.uModel, false, it.model);
        gl.bindVertexArray(it.mesh.vao);
        gl.drawArrays(gl.TRIANGLES, 0, it.mesh.casters);
      }
    } else M.ident(shadowMat);
    gl.bindFramebuffer(gl.FRAMEBUFFER, target ? target.fbo : null);
    gl.viewport(0, 0, w, h);
    gl.clearColor(Math.pow(sky[0], 1 / 2.2), Math.pow(sky[1], 1 / 2.2), Math.pow(sky[2], 1 / 2.2), 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL); gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); gl.disable(gl.BLEND);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, shadowTex); gl.activeTexture(gl.TEXTURE0);
    const bright = sc.bright || 1, cam = sc.cam.pos;
    if (sc.ground) {
      const g = P.ground, u = g.u;
      gl.useProgram(g.prog);
      const gz = sc.ground.z || 0;
      M.ident(gtrans); gtrans[13] = sc.ground.y; gtrans[14] = gz;
      M.mul(tmp, vp, gtrans);
      gl.uniformMatrix4fv(u.uViewProj, false, tmp);
      M.mul(lv, shadowMat, gtrans);
      gl.uniformMatrix4fv(u.uShadowMat, false, lv);
      gl.uniform1i(u.uShadow, 1); gl.uniform1i(u.uShadowOn, useShadows ? 1 : 0); gl.uniform1f(u.uShadowTexel, 1 / SHADOW_RES);
      gl.uniform3fv(u.uSunColor, Q.SUN_COLOR); gl.uniform3fv(u.uSkyColor, Q.SKY_LIGHT); gl.uniform3fv(u.uSunDir, pal.sun);
      gl.uniform3fv(u.uFogColor, sky); gl.uniform3f(u.uCamPos, cam[0], cam[1] - sc.ground.y, cam[2] - gz);
      const b = pal.base, mt = pal.metal, L = (c, k) => Math.pow(c, 2.2) * k;
      gl.uniform3f(u.uAlbedo, L(b[0], 1.5) * 0.45 + L(mt[0], 2) * 0.55, L(b[1], 1.5) * 0.45 + L(mt[1], 2) * 0.55, L(b[2], 1.5) * 0.45 + L(mt[2], 2) * 0.55);
      gl.uniform1f(u.uFogDensity, Q.FOG); gl.uniform1f(u.uBright, bright); gl.uniform1f(u.uGrid, sc.ground.grid ? 1 : 0);
      gl.bindVertexArray(groundVao);
      gl.drawArrays(gl.TRIANGLES, 0, 6);
    }
    if (sc.sky !== false) {   // after the floor: only uncovered pixels
      const s = P.sky, u = s.u;
      gl.useProgram(s.prog);
      const yaw = sc.cam.yaw, pitch = sc.cam.pitch;
      R.forward(yaw, pitch, fwd);
      let rx = -fwd[2], rz = fwd[0]; const rl = Math.hypot(rx, rz) || 1; rx /= rl; rz /= rl;   // normalize(cross(fwd, up))
      gl.uniform3fv(u.uFwd, fwd); gl.uniform3f(u.uRight, rx, 0, rz);
      gl.uniform3f(u.uUp, -rz * fwd[1], rz * fwd[0] - rx * fwd[2], rx * fwd[1]);   // cross(right, fwd)
      gl.uniform3fv(u.uSky, sky); gl.uniform3fv(u.uSunDir, pal.sun);
      gl.uniform2f(u.uTan, R.tanX, R.tanY); gl.uniform1f(u.uBright, bright);
      gl.depthMask(false); gl.bindVertexArray(emptyVao); gl.drawArrays(gl.TRIANGLES, 0, 3); gl.depthMask(true);
    }
    const d = P.dyn, u = d.u;
    gl.useProgram(d.prog);
    gl.uniformMatrix4fv(u.uShadowMat, false, shadowMat);
    gl.uniform1i(u.uShadow, 1); gl.uniform1i(u.uShadowOn, useShadows ? 1 : 0); gl.uniform1f(u.uShadowTexel, 1 / SHADOW_RES);
    gl.uniform3fv(u.uSunColor, Q.SUN_COLOR); gl.uniform3fv(u.uSkyColor, Q.SKY_LIGHT); gl.uniform3fv(u.uSunDir, pal.sun);
    gl.uniform3fv(u.uFogColor, sky); gl.uniform3f(u.uCamPos, cam[0], cam[1], cam[2]);
    gl.uniform1f(u.uFogDensity, Q.FOG); gl.uniform1f(u.uBright, bright);
    drawOpaque(sc, false);
    if (sc.lines && sc.lines.count) drawLines(sc.lines, true);
    drawFlat(sc, false);
    let anyView = false;
    for (let i = 0; i < sc.items.length; i++) if (sc.items[i].view) { anyView = true; break; }
    if (anyView) {
      gl.clear(gl.DEPTH_BUFFER_BIT);
      drawOpaque(sc, true);
      drawFlat(sc, true);
    }
    if (sc.overlay && sc.overlay.count) { gl.disable(gl.DEPTH_TEST); drawLines(sc.overlay, false); gl.enable(gl.DEPTH_TEST); }
    gl.bindVertexArray(null);
  };
  function drawOpaque(sc, viewPass) {
    const u = P.dyn.u;
    gl.uniformMatrix4fv(u.uViewProj, false, vp);
    gl.uniform1i(u.uReceiveShadow, viewPass ? 0 : 1);
    for (let i = 0; i < sc.items.length; i++) {
      const it = sc.items[i];
      if (!!it.view !== viewPass || it.hidden || !it.mesh.count) continue;
      gl.uniformMatrix4fv(u.uModel, false, it.model);
      gl.uniform3fv(u.uTint, it.tint);
      gl.uniform1f(u.uOutlineW, it.outlineW || 0);
      gl.uniform3fv(u.uOutline, it.outline || Q.OUTLINE_COLORS.black);
      gl.uniform1f(u.uSunVis, it.sunVis == null ? 1 : it.sunVis);
      gl.uniform4fv(u.uHighlight, it.highlight || NOHL);
      gl.bindVertexArray(it.mesh.vao);
      gl.drawArrays(gl.TRIANGLES, 0, it.mesh.count);
    }
  }
  function drawFlat(sc, viewPass) {
    let any = false;
    for (let i = 0; i < sc.items.length; i++) { const it = sc.items[i]; if (!!it.view === viewPass && !it.hidden && it.mesh.fcount) { any = true; break; } }
    const trails = !viewPass && sc.trails && sc.trails.count;
    if (!any && !trails) return;
    gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false); gl.disable(gl.CULL_FACE);
    const f = P.flat;
    gl.useProgram(f.prog);
    gl.uniformMatrix4fv(f.u.uViewProj, false, vp);
    for (let i = 0; i < sc.items.length; i++) {
      const it = sc.items[i];
      if (!!it.view !== viewPass || it.hidden || !it.mesh.fcount) continue;
      gl.uniformMatrix4fv(f.u.uModel, false, it.model);
      gl.uniform3fv(f.u.uTint, it.tint);
      gl.bindVertexArray(it.mesh.fvao);
      gl.drawArrays(gl.TRIANGLES, 0, it.mesh.fcount);
    }
    if (trails) {
      gl.uniformMatrix4fv(f.u.uModel, false, ID); gl.uniform3fv(f.u.uTint, ONE_TINT);
      gl.bindVertexArray(trailVao); gl.bindBuffer(gl.ARRAY_BUFFER, trailVbo);
      if (sc.trails.data.length > trailCap) { trailCap = sc.trails.data.length; gl.bufferData(gl.ARRAY_BUFFER, trailCap * 4, gl.DYNAMIC_DRAW); }
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, sc.trails.data, 0, sc.trails.count * FL);
      gl.drawArrays(gl.TRIANGLES, 0, sc.trails.count);
    }
    gl.depthMask(true); gl.disable(gl.BLEND); gl.enable(gl.CULL_FACE);
    gl.useProgram(P.dyn.prog);
  }
  function drawLines(L, depth) {
    gl.useProgram(P.line.prog);
    gl.uniformMatrix4fv(P.line.u.uViewProj, false, vp);
    gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    if (depth) gl.depthMask(false);
    gl.bindVertexArray(lineVao); gl.bindBuffer(gl.ARRAY_BUFFER, lineVbo);
    if (L.data.length > lineCap) { lineCap = L.data.length; gl.bufferData(gl.ARRAY_BUFFER, lineCap * 4, gl.DYNAMIC_DRAW); }
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, L.data, 0, L.count * 7);
    gl.drawArrays(gl.LINES, 0, L.count);
    gl.disable(gl.BLEND); gl.depthMask(true);
    gl.useProgram(P.dyn.prog);
  }

  // --- off-screen targets (thumbnails, fixed-size shots) -----------------------------------------------------------------
  const targets = {};
  R.cachedTarget = (w, h) => targets[w + 'x' + h] || (targets[w + 'x' + h] = R.target(w, h));
  R.target = function (w, h) {
    const fbo = gl.createFramebuffer(), tex = gl.createRenderbuffer(), dep = gl.createRenderbuffer();
    gl.bindRenderbuffer(gl.RENDERBUFFER, tex); gl.renderbufferStorage(gl.RENDERBUFFER, gl.RGBA8, w, h);
    gl.bindRenderbuffer(gl.RENDERBUFFER, dep); gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, w, h);
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, tex);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, dep);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    return { fbo, w, h, pixels: new Uint8Array(w * h * 4) };
  };
  function putFlipped(pixels, w, h, ctx2d) {   // into a 2D canvas (flipped: GL rows are bottom-up)
    const img = ctx2d.createImageData(w, h), row = w * 4;
    for (let y = 0; y < h; y++) img.data.set(pixels.subarray((h - 1 - y) * row, (h - y) * row), y * row);
    ctx2d.putImageData(img, 0, 0);
  }
  R.readTarget = function (t, ctx2d) {   // now (waits for the GPU: fixed-size shots only)
    gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
    gl.readPixels(0, 0, t.w, t.h, gl.RGBA, gl.UNSIGNED_BYTE, t.pixels);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    putFlipped(t.pixels, t.w, t.h, ctx2d);
  };
  // later, without a stall (thumbnails): the pixels are copied into a pixel-pack buffer on the GPU behind a fence, and
  // read once the fence has passed (R.pollReads, every frame), so the page never waits for the GPU to finish the
  // frames still queued (a synchronous readPixels waited for the viewport's whole frame, its 2048 shadow map too).
  // done(pixels, w, h) gets the rows bottom-up; R.putFlipped draws them into a 2D canvas.
  const reads = [], freePbos = [];
  R.putFlipped = putFlipped;
  R.readTargetAsync = function (t, done) {
    const pbo = freePbos.pop() || gl.createBuffer(), bytes = t.w * t.h * 4;
    gl.bindBuffer(gl.PIXEL_PACK_BUFFER, pbo);
    gl.bufferData(gl.PIXEL_PACK_BUFFER, bytes, gl.STREAM_READ);
    gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
    gl.readPixels(0, 0, t.w, t.h, gl.RGBA, gl.UNSIGNED_BYTE, 0);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
    reads.push({ sync: gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0), pbo, w: t.w, h: t.h, bytes, done });
    gl.flush();
  };
  R.pendingReads = () => reads.length;
  R.pollReads = function () {
    while (reads.length && gl.getSyncParameter(reads[0].sync, gl.SYNC_STATUS) === gl.SIGNALED) {   // (in order)
      const r = reads.shift(), px = new Uint8Array(r.bytes);
      gl.deleteSync(r.sync);
      gl.bindBuffer(gl.PIXEL_PACK_BUFFER, r.pbo);
      gl.getBufferSubData(gl.PIXEL_PACK_BUFFER, 0, px);
      gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
      freePbos.push(r.pbo);
      try { r.done(px, r.w, r.h); } catch (e) { console.warn(e); }
    }
  };
})();
