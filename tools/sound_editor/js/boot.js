/* ===== BOOT =========================================================================================
   Load the manifest and the five kits (data/*.js), restore the autosave, start the UI. Voices load on demand. */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui, h = U.h;

  function banner(msg, err, actions) {
    const b = U.$('#banner'); b.hidden = false; b.className = err ? 'err' : ''; b.innerHTML = '';
    b.append(h('span', msg), ...(actions || []), h('span.spacer'), h('button.mini', { type: 'button', onclick: () => { b.hidden = true; } }, 'x'));
  }
  U.banner = banner;

  async function restore() {
    const s = await Q.loadAutosave();
    if (!s || !s.state) return;
    const n = Object.keys(s.state.edits || {}).length + (s.state.groups || []).length;
    for (const [id, src] of Object.entries(s.sources || {})) Q.sources[id] = { name: src.name, data: src.data, bytes: src.bytes || null };
    Q.state = Object.assign({ edits: {}, groups: [], customs: [], meta: {} }, s.state);
    // edits whose audio didn't survive (a source missing) fall back to the shipped sound
    for (const e of Object.values(Q.state.edits)) if (e.src && !Q.sources[e.src]) e.src = null;
    if (s.mix) Q.mixFromJSON(s.mix);
    if (n) banner('Restored your last session (' + Object.keys(Q.state.edits).length + ' edited sounds, ' + Q.state.groups.length + ' groups), saved ' + new Date(s.saved).toLocaleString() + '.', false,
      [h('button.mini', { type: 'button', onclick: async () => { Q.state = { edits: {}, groups: [], customs: [], meta: {} }; Q.clearHistory(); await Q.clearAutosave(); U.$('#banner').hidden = true; Q.onStateChanged('discard'); } }, 'start fresh instead')]);
  }

  async function boot() {
    U.initShell(); U.scenes.init(); U.groups.init(); U.missing.init(); U.files.init();
    try {
      await new Promise((res, rej) => { Q.onManifest = res; Q.loadScript('data/manifest.js').catch(rej); });
    } catch (e) {
      banner('No sound data next to this page (data/manifest.js). Build it: python tools/sound_editor/build.py <export folder> <this folder>', true);
      Q.bootError = String(e); return;
    }
    U.$('#gameVersion').textContent = 'game ' + Q.manifest.version;
    Q.QUIPS = Q.manifest.quips || [];
    Q.buildAssets(Q.manifest);
    await restore();
    U.missing.ensureAssets();
    let tab = 'sounds'; try { tab = localStorage.getItem('qse.tab') || 'sounds'; } catch (e) { }
    U.showTab(tab);
    // the kits: DEFAULT first (the others are levelled against it in the game), then the other four together
    // (each pack redraws its view as it lands: Q.onPackLoaded)
    const kit = k => Q.loadPack('kit', k.dir).catch(e => U.fail('kit ' + k.dir, e));
    await kit(Q.KITS[0]);
    await Promise.all(Q.KITS.slice(1).map(kit));
    Q.ready = true;
    if (Q.tab !== 'sounds') U.refreshView();
    if (Q.sel) U.editor.refresh();
  }
  Q.onPackLoaded = (type, dir) => {
    if (Q.sel && Q.assets[Q.sel] && Q.assets[Q.sel].pack === dir) U.editor.refresh();
    U.library.packLoaded(type);
  };
  boot().catch(e => U.fail('start', e));
})();
