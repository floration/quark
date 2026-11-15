// Shared by every page of the quark site: escaping, the small markdown the changelogs and release notes use, the
// ranking server's address (master.txt), leaderboard parsing and the GitHub releases list. No libraries, no tracking.
"use strict";
const Q = (() => {
  const here = document.currentScript ? document.currentScript.src : location.href;
  const ROOT = new URL(".", here).href;   // the site's root, wherever this page sits
  const REPO = "floration/quark";

  const esc = s => String(s).replace(/[&<>"']/g, c => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"}[c]));

  // inline markdown: `code` first (nothing inside is formatted), then [links](http(s) only), **bold**, *italic*
  function inline(text) {
    const codes = [];
    let s = esc(text).replace(/`([^`]+)`/g, (_, c) => { codes.push("<code>" + c + "</code>"); return "\u0000" + (codes.length - 1) + "\u0000"; });
    s = s.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (m, label, url) => {
      const raw = url.replace(/&amp;/g, "&");
      return /^https?:\/\//i.test(raw) ? `<a href="${esc(raw)}">${label}</a>` : m;
    });
    s = s.replace(/\*\*(.+?)\*\*/g, "<b>$1</b>").replace(/(^|[^\w*])\*([^\s*](?:[^*]*[^\s*])?)\*(?![\w*])/g, "$1<i>$2</i>");   // (no lookbehind: older Safari)
    return s.replace(/\u0000(\d+)\u0000/g, (_, i) => codes[+i]);
  }

  // block markdown: headings (h1 skipped, the rest one level down from `level`), nested lists, tables, paragraphs
  function md(text, level = 3) {
    const out = [], para = [], stack = [];
    const flush = () => { if (para.length) { out.push("<p>" + inline(para.join(" ")) + "</p>"); para.length = 0; } };
    const close = (to = -1) => { while (stack.length && stack[stack.length - 1][0] > to) out.push("</li></" + stack.pop()[1] + ">"); };
    const lines = String(text || "").replace(/\r\n/g, "\n").split("\n");
    for (let i = 0; i < lines.length; i++) {
      const raw = lines[i], line = raw.trim();
      if (!line) { flush(); continue; }
      if (/^\|.*\|$/.test(line)) {
        flush(); close();
        const rows = [];
        while (i < lines.length && /^\|.*\|$/.test(lines[i].trim())) rows.push(lines[i++].trim());
        i--;
        const cells = r => r.slice(1, -1).split("|").map(c => c.trim());
        let t = '<div class="scroll"><table><tr>' + cells(rows[0]).map(c => `<th>${inline(c)}</th>`).join("") + "</tr>";
        for (const r of rows.slice(1)) if (!/^\|[\s:|-]+\|$/.test(r)) t += "<tr>" + cells(r).map(c => `<td>${inline(c)}</td>`).join("") + "</tr>";
        out.push(t + "</table></div>");
        continue;
      }
      const m = raw.match(/^(\s*)([-*+]|\d+[.)])\s+(.*)$/);
      if (m) {
        flush();
        const indent = m[1].replace(/\t/g, "    ").length, kind = /\d/.test(m[2][0]) ? "ol" : "ul";
        const top = stack[stack.length - 1];
        if (top && indent > top[0]) { out.push("<" + kind + ">"); stack.push([indent, kind]); }
        else {
          close(indent);
          const t = stack[stack.length - 1];
          if (t && t[0] === indent && t[1] === kind) out.push("</li>");
          else {
            if (t && t[0] === indent) out.push("</li></" + stack.pop()[1] + ">");
            out.push("<" + kind + ">"); stack.push([indent, kind]);
          }
        }
        out.push("<li>" + inline(m[3]));
        continue;
      }
      if (stack.length && /^[ \t]/.test(raw)) { out.push(" " + inline(line)); continue; }
      close();
      const h = line.match(/^(#{1,6})\s+(.*)$/);
      if (h) {
        flush();
        if (h[1].length === 1) continue;
        const n = Math.min(6, h[1].length - 2 + level);
        out.push(`<h${n}>${inline(h[2])}</h${n}>`);
      } else para.push(line);
    }
    flush(); close();
    return out.join("\n");
  }

  const fmtTime = ms => `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, "0")}.${String(ms % 1000).padStart(3, "0")}`;
  const fmtDate = iso => new Date(iso).toLocaleDateString("en-GB", {day: "numeric", month: "short", year: "numeric"});

  // the ranking server: its address is published next to the site (master.txt)
  let masterP = null;
  const master = () => masterP || (masterP = fetch(ROOT + "master.txt", {cache: "no-cache"}).then(r => {
    if (!r.ok) throw new Error("master.txt " + r.status);
    return r.text();
  }).then(t => {
    const base = t.trim().split(/\s+/)[0].replace(/\/+$/, "");
    if (!/^https:\/\/[^\s/]+/.test(base)) throw new Error("bad master.txt");
    return base;
  }));
  const getText = url => fetch(url).then(r => { if (!r.ok) throw new Error(url + " " + r.status); return r.text(); });

  const LADDERS = [
    {key: "vanilla", title: "1v1 Vanilla", short: "Vanilla"},
    {key: "vampire", title: "1v1 Laser Vampire", short: "Laser Vampire"},
    {key: "instagib", title: "FFA Instagib", short: "Instagib"},
  ];
  // "rating games name" per line, best first; names are whatever players typed: always escaped where shown
  const parseLadder = t => t.split("\n").map(l => l.match(/^(\d+) (\d+) (.+)$/)).filter(Boolean).map(m => ({rating: +m[1], games: +m[2], name: m[3]}));
  // "ms name" per line, fastest first
  const parseRace = t => t.split("\n").map(l => l.match(/^(\d+) (.+)$/)).filter(Boolean).map(m => ({ms: +m[1], name: m[2]}));

  const ICON_OFF = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true"><path d="M2 8.8a15 15 0 0 1 20 0M5 12.5a10 10 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0M12 19.5h.01M3 3l18 18"/></svg>';
  const state = (title, text) => `<div class="state" role="status">${ICON_OFF}<div><b>${esc(title)}</b>${text}</div></div>`;

  // GitHub releases (newest first), cached for 5 minutes in this tab: the API allows 60 requests an hour per address
  function releases() {
    const KEY = "quark-releases", now = Date.now();
    try {
      const c = JSON.parse(sessionStorage.getItem(KEY) || "null");
      if (c && now - c.t < 300000 && Array.isArray(c.d)) return Promise.resolve(c.d);
    } catch (e) { /* storage off: just fetch */ }
    return fetch(`https://api.github.com/repos/${REPO}/releases?per_page=100`, {headers: {Accept: "application/vnd.github+json"}}).then(r => {
      if (!r.ok) throw new Error(r.status === 403 || r.status === 429 ? "rate" : "http " + r.status);
      return r.json();
    }).then(d => {
      if (!Array.isArray(d)) throw new Error("bad reply");
      const slim = d.filter(r => !r.draft).map(r => ({tag: r.tag_name, name: r.name, pre: r.prerelease, date: r.published_at || r.created_at,
        url: r.html_url, body: r.body || "", assets: (r.assets || []).map(a => ({name: a.name, url: a.browser_download_url, size: a.size}))}));
      try { sessionStorage.setItem(KEY, JSON.stringify({t: now, d: slim})); } catch (e) { /* fine */ }
      return slim;
    });
  }
  const isTesting = r => r.pre && r.tag.startsWith("test-");
  const isProduction = r => !r.pre && /^v\d+\.\d+\.\d+$/.test(r.tag);
  // the downloads people want from a release (signatures, packages and checksums stay on the release page)
  const PLATFORM_FILES = [
    ["quark-launcher-windows.exe", "Windows launcher"], ["quark-windows.zip", "Windows game only"],
    ["quark-launcher-linux", "Linux launcher"], ["quark-linux.tar.gz", "Linux game only"],
    ["quark-launcher-macos.zip", "macOS launcher"], ["quark-macos.zip", "macOS game only"],
  ];
  const platforms = r => ["windows", "linux", "macos"].filter(p => r.assets.some(a => a.name === `quark-${p}.qpak` || a.name.startsWith(`quark-launcher-${p}`)));
  const PNAME = {windows: "Windows", linux: "Linux", macos: "macOS"};
  const assetLinks = r => PLATFORM_FILES.map(([n, label]) => {
    const a = r.assets.find(x => x.name === n);
    return a ? `<li><a href="${esc(a.url)}">${esc(label)}</a></li>` : "";
  }).join("");

  // the assets and sounds pages: each channel's export of the game's assets (assets/data/<channel>/manifest.json and
  // every file it names; tools/site_assets.py locally, the release workflow on the live site)
  const CHANNELS = [["production", "Production"], ["testing", "Testing"]];
  const channelOf = () => { try { return new URLSearchParams(location.search).get("channel") === "testing" ? "testing" : "production"; } catch (e) { return "production"; } };
  function assetData(ch) {
    const base = ROOT + "assets/data/" + ch + "/";
    return fetch(base + "manifest.json", {cache: "no-cache"}).then(r => { if (!r.ok) throw new Error("none"); return r.json(); })
      .then(m => { if (!m || !Array.isArray(m.items)) throw new Error("none"); return {version: String(m.version || ""), items: m.items, base, channel: ch}; });
  }
  // a PRODUCTION / TESTING switch in `el`; onChange(channel) runs on a click (the URL keeps the choice: ?channel=testing)
  function channelSwitch(el, current, onChange) {
    el.innerHTML = CHANNELS.map(([k, l]) => `<button type="button" data-ch="${k}" aria-pressed="${k === current}">${l}</button>`).join("");
    el.querySelectorAll("button").forEach(b => b.onclick = () => {
      el.querySelectorAll("button").forEach(x => x.setAttribute("aria-pressed", String(x === b)));
      try { const u = new URL(location.href); if (b.dataset.ch === "testing") u.searchParams.set("channel", "testing"); else u.searchParams.delete("channel"); history.replaceState(null, "", u); } catch (e) { /* fine */ }
      onChange(b.dataset.ch);
    });
  }
  const noData = ch => state(ch === "testing" ? "No testing build published yet." : "No production assets published yet.",
    ch === "testing" ? " The testing channel's assets appear here after the next push to the testing branch."
      : " They appear with the next production release. Building locally? Run <code>python3 tools/site_assets.py</code>.");

  return {ROOT, REPO, esc, inline, md, fmtTime, fmtDate, master, getText, LADDERS, parseLadder, parseRace, state, releases,
          isTesting, isProduction, platforms, PNAME, assetLinks, CHANNELS, channelOf, assetData, channelSwitch, noData};
})();
