/* Files tab: each file as a readable document (or raw text), with last change and PC-sync badges. */
'use strict';
(function (DM) {
  const {h, clear, $} = DM;
  const st = DM.state;

  function render() {
    const d = st.data, p = st.prefs.files;
    if (!d.files.length) { clear($('#fBody')).append(h('div', {class: 'empty'}, 'No files.')); return; }
    if (!d.files.some(f => f.name === p.sel)) p.sel = (d.files.find(f => f.name.startsWith('07')) || d.files[0]).name;
    const list = clear($('#fList'));
    for (const f of d.files) {
      const fe = d.entries.filter(e => e.file === f.name);
      const open = fe.filter(e => !DM.isDone(e)).length;
      const nq = fe.filter(DM.needsQ).length;
      list.appendChild(h('div', {class: 'list-item' + (f.name === p.sel ? ' on' : ''), tabindex: 0, role: 'button',
        onclick: () => { DM.app.openFile(f.name); $('#fScroll').scrollTop = 0; },
        onkeydown: ev => { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); ev.currentTarget.click(); } }},
        h('div', {class: 't'}, DM.fileTitle(f.name), f.pcUnsynced ? h('span', {class: 'badge pc', title: 'Edited on the PC, not synced yet'}, 'PC') : null),
        h('div', {class: 'd'}, f.kind === 'document' ? 'document' : fe.length + ' entries' + (open ? ', ' + open + ' open' : ''),
          nq ? h('span', {class: 'nqc', title: nq + ' waiting on q'}, ' · ' + nq + ' for q') : null),
        f.lastChange && f.lastChange.time ? h('div', {class: 'd'}, 'changed ', DM.timeEl(f.lastChange.time)) : null));
    }
    const f = d.files.find(x => x.name === p.sel);
    const bar = clear($('#fBar'));
    bar.append(h('b', {class: 'f-title'}, DM.fileTitle(f.name)), h('code', null, f.name),
      f.pcUnsynced ? h('span', {class: 'pill q sm', title: 'The copy on the PC has edits that are not on GitHub yet; sync.cmd pushes them'}, 'Edited on the PC, not synced') : null,
      f.lastChange && f.lastChange.time ? h('span', {class: 'dim'}, 'Last changed ', DM.timeEl(f.lastChange.time, 'dayclock'), ' (', DM.timeEl(f.lastChange.time), ')',
        f.lastChange.subject ? h('span', {class: 'faint'}, ': ' + f.lastChange.subject) : null) : null,
      h('span', {class: 'spacer'}),
      DM.edit && DM.edit.canEdit() ? h('button', {class: 'mini', title: 'Open this file in the Edit tab', onclick: () => DM.edit.openAt(f.name, 1)}, 'Edit this file') : null,
      h('div', {class: 'seg'},
        h('button', {class: p.raw ? '' : 'on', onclick: () => { p.raw = false; DM.app.save(); render(); }}, 'Readable'),
        h('button', {class: p.raw ? 'on' : '', onclick: () => { p.raw = true; DM.app.save(); render(); }}, 'Raw text')));
    const body = clear($('#fBody'));
    body.classList.toggle('raw', p.raw);
    if (p.raw) body.appendChild(h('pre', {class: 'rawtext'}, f.markdown || ''));
    else DM.md.into(body, f.markdown || '');
  }

  (DM.views = DM.views || {}).files = {render, scroller: () => $('#fScroll')};
})(window.DM);
