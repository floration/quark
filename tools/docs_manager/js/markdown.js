/* Small safe markdown renderer. Every piece of doc text is HTML-escaped before any markup is added;
   links are kept only for http(s) URLs. Output is an HTML string made only of our own tags. */
'use strict';
(function (DM) {
  const esc = DM.esc;
  const P0 = '\u0001', P1 = '\u0002';

  function linkHtml(url, innerHtml) {
    const u = DM.safeUrl(url);
    if (!u) return innerHtml + (url ? ' <span class="md-badlink" title="link removed: only http/https links are kept">(' + esc(url) + ')</span>' : '');
    return '<a href="' + esc(u) + '" target="_blank" rel="noopener noreferrer">' + innerHtml + '</a>';
  }

  // Inline markdown: `code`, [text](url), bare URLs, **bold**, *italic* / _italic_, ~~strike~~.
  function inline(raw, noLinks, shared) {
    // nested (a link label) shares the outer call's slots; the outer call restores them
    let text = String(raw == null ? '' : raw);
    if (!shared) text = text.replace(/[\u0001\u0002]/g, '');
    const slots = shared || [];
    const put = html => P0 + (slots.push(html) - 1) + P1;
    text = text.replace(/(`+)([^`\n]|[^`\n][\s\S]*?[^`\n])\1(?!`)/g, (_, _t, c) => put('<code>' + esc(c) + '</code>'));
    text = text.replace(/\\([\\`*_{}\[\]()#+\-.!|~<>])/g, (_, ch) => put(esc(ch)));
    if (!noLinks) {
      text = text.replace(/\[([^\]\n]+)\]\(\s*([^)\s]+)(?:\s+"[^"]*")?\s*\)/g, (_, label, url) => put(linkHtml(url, inline(label, true, slots))));
      text = text.replace(/\bhttps?:\/\/[^\s<>"'`]+/g, m => {
        let url = m, tail = '';
        while (/[.,;:!?)\]*_]$/.test(url)) {
          if (url.endsWith(')') && (url.split('(').length - 1) >= (url.split(')').length - 1)) break;
          tail = url.slice(-1) + tail; url = url.slice(0, -1);
        }
        return put(linkHtml(url, esc(url))) + tail;
      });
    }
    let out = esc(text);
    out = out.replace(/\*\*(?=\S)([\s\S]*?\S)\*\*/g, '<strong>$1</strong>');
    out = out.replace(/__(?=\S)([\s\S]*?\S)__(?![\w])/g, '<strong>$1</strong>');
    out = out.replace(/(^|[^\w*])\*(?=[^\s*])([^*\n]*?[^\s*])\*(?![\w*])/g, '$1<em>$2</em>');
    out = out.replace(/(^|[^\w\\])_(?=[^\s_])([^_\n]*?[^\s_])_(?![\w])/g, '$1<em>$2</em>');
    out = out.replace(/~~(?=\S)([^~\n]*?\S)~~/g, '<del>$1</del>');
    if (shared) return out;
    // restore slots (slots may nest: a link label holding code)
    for (let i = 0; i < 3 && out.includes(P0); i++) out = out.replace(/\u0001(\d+)\u0002/g, (_, n) => slots[+n]);
    return out;
  }

  const RE_FENCE = /^\s{0,3}(```+|~~~+)\s*([\w+-]*)\s*$/;
  const RE_HEAD = /^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$/;
  const RE_HR = /^\s{0,3}([-*_])(\s*\1){2,}\s*$/;
  const RE_ITEM = /^(\s*)([-*+]|\d{1,3}[.)])\s+(.*)$/;
  const RE_TSEP = /^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$/;
  const RE_QUOTE = /^\s{0,3}>\s?(.*)$/;

  function splitRow(line) {
    let t = line.trim();
    if (t.startsWith('|')) t = t.slice(1);
    if (t.endsWith('|') && !t.endsWith('\\|')) t = t.slice(0, -1);
    const cells = []; let cur = '', tick = false;
    for (let i = 0; i < t.length; i++) {
      const c = t[i];
      if (c === '\\' && t[i + 1] === '|') { cur += '|'; i++; continue; }
      if (c === '`') tick = !tick;
      if (c === '|' && !tick) { cells.push(cur.trim()); cur = ''; continue; }
      cur += c;
    }
    cells.push(cur.trim());
    return cells;
  }

  function isBlockStart(line, next) {
    return RE_FENCE.test(line) || RE_HEAD.test(line) || RE_HR.test(line) || RE_ITEM.test(line) || RE_QUOTE.test(line) ||
      (line.includes('|') && next != null && RE_TSEP.test(next));
  }

  // opts.lines: each block (and list item) gets data-l="<1-based source line>", for scroll sync and "show line N".
  function renderList(lines, i0, attr) {
    // Collect the list block: items + continuation lines; a blank line ends it unless more list follows.
    const items = [];
    let i = i0;
    for (; i < lines.length; i++) {
      const line = lines[i];
      const m = RE_ITEM.exec(line);
      if (m && !RE_HR.test(line)) {
        items.push({indent: m[1].replace(/\t/g, '    ').length, ordered: /\d/.test(m[2]), start: parseInt(m[2], 10) || 1, text: [m[3]], line: i});
        continue;
      }
      if (!line.trim()) {
        const nxt = lines.slice(i + 1).find(l => l.trim());
        if (nxt != null && (RE_ITEM.test(nxt) || /^\s{2,}\S/.test(nxt))) continue;
        break;
      }
      if (/^\s/.test(line) || (!isBlockStart(line, lines[i + 1]) && items.length)) {
        if (RE_FENCE.test(line) || RE_HEAD.test(line)) break;
        items[items.length - 1].text.push(line.trim());
        continue;
      }
      break;
    }
    // Build the tree by indent.
    const root = {kids: []}, stack = [{indent: -1, node: root}];
    for (const it of items) {
      while (stack.length > 1 && stack[stack.length - 1].indent >= it.indent) stack.pop();
      const node = {it, kids: []};
      stack[stack.length - 1].node.kids.push(node);
      stack.push({indent: it.indent, node});
    }
    const emit = kids => {
      let html = '', k = 0;
      while (k < kids.length) {
        const ordered = kids[k].it.ordered;
        const tag = ordered ? 'ol' : 'ul';
        html += '<' + tag + (ordered && kids[k].it.start !== 1 ? ' start="' + kids[k].it.start + '"' : '') + '>';
        while (k < kids.length && kids[k].it.ordered === ordered) {
          const n = kids[k++];
          html += '<li' + attr(n.it.line) + '>' + inline(n.it.text.join(' ')) + (n.kids.length ? emit(n.kids) : '') + '</li>';
        }
        html += '</' + tag + '>';
      }
      return html;
    };
    return {html: emit(root.kids), next: i};
  }

  function render(src, opts) {
    const lines = String(src == null ? '' : src).replace(/\r\n?/g, '\n').split('\n');
    const attr = opts && opts.lines ? i => ' data-l="' + (i + 1) + '"' : () => '';
    let html = '', para = [], paraAt = 0;
    const flush = () => { if (para.length) { html += '<p' + attr(paraAt) + '>' + inline(para.join('\n')).replace(/\n/g, ' ') + '</p>'; para = []; } };
    for (let i = 0; i < lines.length;) {
      const line = lines[i];
      let m;
      if ((m = RE_FENCE.exec(line))) {
        flush();
        const fence = m[1][0], body = [], at = i;
        i++;
        while (i < lines.length && !(new RegExp('^\\s{0,3}' + (fence === '`' ? '```' : '~~~')).test(lines[i]))) body.push(lines[i++]);
        i++;
        html += '<pre' + attr(at) + '><code>' + esc(body.join('\n')) + '</code></pre>';
        continue;
      }
      if (!line.trim()) { flush(); i++; continue; }
      if ((m = RE_HEAD.exec(line))) { flush(); const n = m[1].length; html += '<h' + n + attr(i) + '>' + inline(m[2]) + '</h' + n + '>'; i++; continue; }
      if (RE_HR.test(line)) { flush(); html += '<hr' + attr(i) + '>'; i++; continue; }
      if (line.includes('|') && i + 1 < lines.length && RE_TSEP.test(lines[i + 1])) {
        flush();
        const head = splitRow(line), al = splitRow(lines[i + 1]).map(c => /^:-+:$/.test(c) ? 'center' : /-+:$/.test(c) ? 'right' : '');
        const att = j => al[j] ? ' style="text-align:' + al[j] + '"' : '';
        html += '<div class="md-table"' + attr(i) + '><table><thead><tr>' + head.map((c, j) => '<th' + att(j) + '>' + inline(c) + '</th>').join('') + '</tr></thead><tbody>';
        i += 2;
        while (i < lines.length && lines[i].includes('|') && lines[i].trim()) {
          const cells = splitRow(lines[i++]);
          html += '<tr>' + head.map((_, j) => '<td' + att(j) + '>' + inline(cells[j] || '') + '</td>').join('') + '</tr>';
        }
        html += '</tbody></table></div>';
        continue;
      }
      if (RE_ITEM.test(line)) {
        flush();
        const r = renderList(lines, i, attr);
        html += r.html; i = r.next;
        continue;
      }
      if (RE_QUOTE.test(line)) {
        flush();
        const q = [], at = i;
        while (i < lines.length && RE_QUOTE.test(lines[i])) q.push(RE_QUOTE.exec(lines[i++])[1]);
        html += '<blockquote' + attr(at) + '>' + render(q.join('\n')) + '</blockquote>';
        continue;
      }
      if (!para.length) paraAt = i;
      para.push(line.trim());
      i++;
    }
    flush();
    return html;
  }

  // Put rendered markdown into an element. The only innerHTML use on doc text, and its input is escaped above.
  function into(el, src, inlineOnly, opts) {
    el.innerHTML = inlineOnly ? inline(src) : render(src, opts);
    return el;
  }

  DM.md = {render, inline, into};
})(window.DM);
