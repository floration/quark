/* Board > Timeline: one lane per thread, a dot per change (colour = kind), wheel zoom, drag pan, range buttons. */
'use strict';
(function (DM) {
  const {h, s, clear} = DM;
  const st = DM.state;
  const KIND_RANK = {halt: -1, status: 0, added: 1, removed: 2, moved: 3, edited: 4};
  const KIND_LABEL = {halt: 'Became halting (HALT)', added: 'New entry', status: 'Status change', edited: 'Edit', moved: 'Move', removed: 'Removal'};
  const kindOf = it => it.halting === true ? 'halt' : it.kind;
  const RANGES = [['6h', '6 h', 6 * 3600e3], ['24h', '24 h', 86400e3], ['3d', '3 days', 3 * 86400e3], ['7d', '7 days', 7 * 86400e3], ['all', 'All', 0]];
  const LANE_H = 24, AXIS_H = 36;   // the axis: a row of days (Dallas midnights) over a row of hours
  let view = null; // {t0, t1} kept across data refreshes
  let lastRangeKey = null;

  function events() {
    const map = new Map();
    for (const c of st.data.changes) {
      const t = Date.parse(c.time);
      if (!t) continue;
      const items = c.items.length ? c.items : [{kind: 'edited', title: c.subject, file: c.files[0] || ''}];
      for (const it of items) {
        const e = it.entryId && st.entryById[it.entryId];
        const lane = (e && e.thread) || c.thread || '(no thread)';
        const key = c.sha + '|' + lane;
        let ev = map.get(key);
        if (!ev) map.set(key, ev = {t, c, lane, items: []});
        ev.items.push(it);
      }
    }
    for (const ev of map.values()) ev.kind = ev.items.reduce((k, it) => (KIND_RANK[kindOf(it)] ?? 9) < (KIND_RANK[k] ?? 9) ? kindOf(it) : k, 'edited');
    return [...map.values()];
  }

  // Dallas UTC offset (ms) at time t, for aligning ticks to local hours/days (formatters made once: a paint asks for
  // an offset per hour line).
  const F_MIN = new Intl.DateTimeFormat('en-US', {timeZone: 'America/Chicago', year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric', hourCycle: 'h23'});
  const F_DAY = new Intl.DateTimeFormat('en-US', {timeZone: 'America/Chicago', year: 'numeric', month: 'numeric', day: 'numeric'});
  function tzOff(t) {
    const p = {};
    for (const x of F_MIN.formatToParts(new Date(t))) p[x.type] = +x.value || 0;
    return Date.UTC(p.year, p.month - 1, p.day, p.hour % 24, p.minute) - Math.floor(t / 60000) * 60000;
  }

  // Dallas midnights in [t0, t1]: [{t, label}].
  function midnights(t0, t1) {
    const out = [];
    const p = {};
    for (const x of F_DAY.formatToParts(new Date(t0))) p[x.type] = +x.value || 0;
    for (let k = 0; k < 400; k++) {
      const u = Date.UTC(p.year, p.month - 1, p.day + k);
      let t = u - tzOff(u);
      t = u - tzOff(t);                                  // (the offset at that midnight, not 6 h later: DST days)
      if (t > t1) break;
      if (t >= t0) out.push({t, label: DM.dayLabel(new Date(t + 3600e3))});
    }
    return out;
  }

  function render(body, tools) {
    const p = st.prefs.timeline;
    const evs = events();
    if (!evs.length) { body.appendChild(h('div', {class: 'empty'}, 'No changes yet.')); return; }
    const unfinished = new Set(st.data.entries.filter(DM.isOpen).map(e => e.thread || '(no thread)'));
    const laneLast = {};
    for (const ev of evs) laneLast[ev.lane] = Math.max(laneLast[ev.lane] || 0, ev.t);
    let lanes = Object.keys(laneLast).sort((a, b) => laneLast[b] - laneLast[a]);
    if (p.openOnly) lanes = lanes.filter(l => unfinished.has(l));
    const tMin = Math.min(...evs.map(e => e.t)), tMax = Math.max(Date.now(), ...evs.map(e => e.t));
    const pad = Math.max(10 * 60e3, (tMax - tMin) * 0.02);
    const ext = [tMin - pad, tMax + pad];
    const setRange = key => {
      const r = RANGES.find(x => x[0] === key) || RANGES[2];
      // a preset never starts before the first change, so a short history fills the width
      view = r[2] ? {t0: Math.max(tMax - r[2], ext[0]), t1: tMax + Math.max(5 * 60e3, Math.min(r[2], tMax - ext[0]) * 0.03)} : {t0: ext[0], t1: ext[1]};
    };
    if (!view || lastRangeKey !== p.range) { setRange(p.range); lastRangeKey = p.range; }

    // tools: range buttons, open-only, legend
    const seg = h('div', {class: 'seg'}, RANGES.map(([k, l]) => h('button', {class: p.range === k ? 'on' : '', onclick: () => {
      p.range = k; DM.app.save(); setRange(k); lastRangeKey = k; for (const b of seg.children) b.classList.toggle('on', b.textContent === l); draw();
    }}, l)));
    tools.append(h('span', {class: 'faint'}, 'Show'), seg,
      DM.board.check('only threads with unfinished entries', p.openOnly, v => { p.openOnly = v; DM.app.save(); DM.board.redraw(); }),
      h('span', {class: 'legend'}, Object.keys(KIND_LABEL).map(k => h('span', null, h('i', {class: 'kd k-' + k}), KIND_LABEL[k]))));

    const wrap = h('div', {class: 'tl'});
    const labels = h('div', {class: 'tl-labels'}, h('div', {class: 'tl-lh', style: {height: AXIS_H + 'px'}}, lanes.length + ' threads'));
    for (const l of lanes) {
      const n = evs.filter(e => e.lane === l).length;
      labels.appendChild(h('div', {class: 'tl-ln', style: {height: LANE_H + 'px'}, title: l + ': ' + n + ' changes, last ' + DM.full(laneLast[l])},
        l === '(no thread)' ? h('span', {class: 'faint n'}, 'no thread') : DM.ext(DM.data.threadUrl(l), l, 'n'),
        h('span', {class: 'c faint'}, String(n))));
    }
    const plot = h('div', {class: 'tl-plot'});
    const H = AXIS_H + lanes.length * LANE_H + 6;
    const svg = s('svg', {class: 'tl-svg', height: H, role: 'img', 'aria-label': 'Timeline of changes per thread'});
    plot.appendChild(svg);
    wrap.append(labels, plot);
    body.appendChild(wrap);
    body.appendChild(h('p', {class: 'faint tl-foot'}, 'Wheel to zoom, drag to pan, double-click to see everything. One dot per change to a thread’s entries (several items in one commit share a dot; a bigger dot means 3 or more). Click a dot for the details; the Change log tab lists the same changes as text.'));

    const laneY = Object.fromEntries(lanes.map((l, i) => [l, AXIS_H + i * LANE_H + LANE_H / 2]));
    const shown = evs.filter(e => laneY[e.lane] != null);
    let raf = 0;
    function draw() { cancelAnimationFrame(raf); raf = requestAnimationFrame(paint); }
    function paint() {
      const W = Math.max(200, plot.clientWidth);
      svg.setAttribute('width', W);
      svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
      while (svg.firstChild) svg.removeChild(svg.firstChild);
      const {t0, t1} = view, span = t1 - t0;
      const x = t => (t - t0) / span * W;
      // lane stripes
      lanes.forEach((l, i) => { if (i % 2) svg.appendChild(s('rect', {x: 0, y: AXIS_H + i * LANE_H, width: W, height: LANE_H, class: 'tl-stripe'})); });
      // days: a line at every Dallas midnight, its date in the top row (every k-th day when they are close);
      // the day at the left edge stays labelled when no midnight is near it
      const days = midnights(t0, t1);
      const pxDay = W / (span / 86400e3), every = Math.max(1, Math.ceil(84 / pxDay));
      days.forEach((d, i) => {
        const xx = x(d.t);
        svg.appendChild(s('line', {x1: xx, x2: xx, y1: 0, y2: H, class: 'tl-day'}));
        if (i % every === 0 || every === 1) svg.appendChild(s('text', {x: xx + 4, y: 13, class: 'tl-tick day', text: d.label}));
      });
      if (!days.length || x(days[0].t) > 96) svg.appendChild(s('text', {x: 4, y: 13, class: 'tl-tick day', text: DM.dayLabel(new Date(t0))}));
      svg.appendChild(s('line', {x1: 0, x2: W, y1: 18.5, y2: 18.5, class: 'tl-grid'}));
      // hours (only while a day is wide enough to show them)
      const STEPS = [15, 30, 60, 120, 180, 360, 720].map(m => m * 60e3);
      const step = STEPS.find(sp => W / (span / sp) >= 64);
      if (step) {
        const off = tzOff(t0);
        for (let t = Math.floor((t0 + off) / step) * step - off; t <= t1; t += step) {
          const xx = x(t);
          if (xx < -1) continue;
          const local = new Date(t + tzOff(t));
          if (local.getUTCHours() === 0 && local.getUTCMinutes() === 0) continue;   // (the day line is there)
          svg.appendChild(s('line', {x1: xx, x2: xx, y1: 20, y2: H, class: 'tl-grid'}));
          svg.appendChild(s('text', {x: xx + 3, y: 31, class: 'tl-tick', text: DM.hhmm(new Date(t))}));
        }
      }
      // now
      const nx = x(Date.now());
      if (nx >= 0 && nx <= W) {
        svg.appendChild(s('line', {x1: nx, x2: nx, y1: AXIS_H - 6, y2: H, class: 'tl-now'}));
        svg.appendChild(s('text', {x: nx - 4, y: H - 6, class: 'tl-tick now', 'text-anchor': 'end', text: 'now'}));
      }
      // dots
      for (const ev of shown) {
        const xx = x(ev.t);
        if (xx < -8 || xx > W + 8) continue;
        const y = laneY[ev.lane];
        const g = s('g', {class: 'tl-ev', tabindex: 0, role: 'button', 'aria-label': ev.c.subject || 'change'});
        g.appendChild(s('circle', {cx: xx, cy: y, r: 10, class: 'hit'}));
        if (ev.kind === 'halt') g.appendChild(s('circle', {cx: xx, cy: y, r: 9, class: 'tdot-ring'}));
        g.appendChild(s('circle', {cx: xx, cy: y, r: ev.items.length > 2 || ev.kind === 'halt' ? 6.5 : 5, class: 'tdot k-' + ev.kind}));
        g.addEventListener('pointerenter', pe => DM.tip(tipFor(ev), pe.clientX, pe.clientY));
        g.addEventListener('pointermove', pe => DM.tip(tipFor(ev), pe.clientX, pe.clientY));
        g.addEventListener('pointerleave', () => DM.tip(null));
        g.addEventListener('click', () => { DM.tip(null); DM.app.openChange(ev.c.sha); });
        g.addEventListener('keydown', ke => { if (ke.key === 'Enter') DM.app.openChange(ev.c.sha); });
        svg.appendChild(g);
      }
    }
    function tipFor(ev) {
      return h('div', {class: 'tl-tip'},
        h('div', null, h('b', null, DM.dayLabel(new Date(ev.t)) + ' ' + DM.hhmm(new Date(ev.t))), ' · ', DM.rel(new Date(ev.t))),
        h('div', {class: 'dim'}, ev.c.subject || ''),
        h('ul', null, ev.items.slice(0, 6).map(it => h('li', {class: 'k-' + kindOf(it)}, h('i', {class: 'kd'}),
          it.halting === false ? 'No longer halting' : KIND_LABEL[kindOf(it)] || it.kind, ': ', DM.trimTitle(it.title || '')))),
        ev.items.length > 6 ? h('div', {class: 'faint'}, '+ ' + (ev.items.length - 6) + ' more') : null);
    }
    // zoom + pan
    svg.addEventListener('wheel', we => {
      we.preventDefault();
      const r = svg.getBoundingClientRect();
      const f = Math.exp((we.deltaY || we.deltaX) * 0.0015);
      const at = view.t0 + (we.clientX - r.left) / r.width * (view.t1 - view.t0);
      let span = Math.min(Math.max((view.t1 - view.t0) * f, 10 * 60e3), (ext[1] - ext[0]) * 1.5);
      const k = (at - view.t0) / (view.t1 - view.t0);
      view = {t0: at - k * span, t1: at - k * span + span};
      markCustom(); draw();
    }, {passive: false});
    let drag = null;
    svg.addEventListener('pointerdown', pe => {
      if (pe.button !== 0 || pe.target.closest('.tl-ev')) return;
      drag = {x: pe.clientX, v: Object.assign({}, view), w: svg.getBoundingClientRect().width};
      svg.setPointerCapture(pe.pointerId); svg.classList.add('dragging');
    });
    svg.addEventListener('pointermove', pe => {
      if (!drag) return;
      const dt = (pe.clientX - drag.x) / drag.w * (drag.v.t1 - drag.v.t0);
      view = {t0: drag.v.t0 - dt, t1: drag.v.t1 - dt};
      draw();
    });
    const end = () => { if (drag) { drag = null; svg.classList.remove('dragging'); markCustom(); } };
    svg.addEventListener('pointerup', end); svg.addEventListener('pointercancel', end);
    svg.addEventListener('dblclick', () => { p.range = 'all'; lastRangeKey = 'all'; DM.app.save(); setRange('all'); for (const b of seg.children) b.classList.toggle('on', b.textContent === 'All'); draw(); });
    function markCustom() { for (const b of seg.children) b.classList.remove('on'); }
    new ResizeObserver(draw).observe(plot);
    paint();
  }

  DM.board.SUBS.timeline = {render};
})(window.DM);
