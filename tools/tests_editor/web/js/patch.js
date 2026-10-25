// Block edits as text patches. Every function takes the whole file text (and the model's line numbers, 1-based) and
// returns the new whole text; the file's indentation, comments and line endings around the edit are kept. No DOM:
// test/patch_test.mjs runs these under node.

// ======================================================================= C++ (tests/tests.cpp: groups, commands)
const lineStarts = (text) => { const s = [0]; for (let i = 0; i < text.length; i++) if (text.charCodeAt(i) === 10) s.push(i + 1); return s; };

// skips a string / char literal / comment starting at i; returns the index after it (or i when none starts there)
function skipLit(t, i) {
    const c = t[i];
    if (c === '"' || c === "'") {
        if (c === '"' && t[i - 1] === "R" && /[\w]/.test(t[i - 2] || " ") === false) {   // R"delim( ... )delim"
            const m = /^"([^(\s]*)\(/.exec(t.slice(i, i + 20));
            if (m) { const end = t.indexOf(")" + m[1] + '"', i); return end < 0 ? t.length : end + m[1].length + 2; }
        }
        let j = i + 1;
        while (j < t.length && t[j] !== c) { if (t[j] === "\\") j++; if (t[j] === "\n") break; j++; }
        return j + 1;
    }
    if (c === "/" && t[i + 1] === "/") { const e = t.indexOf("\n", i); return e < 0 ? t.length : e; }
    if (c === "/" && t[i + 1] === "*") { const e = t.indexOf("*/", i + 2); return e < 0 ? t.length : e + 2; }
    return i;
}
export function matchClose(t, open) {
    const want = { "{": "}", "(": ")", "[": "]" };
    const stack = [want[t[open]]];
    for (let i = open + 1; i < t.length; i++) {
        const j = skipLit(t, i);
        if (j !== i) { i = j - 1; continue; }
        const c = t[i];
        if (want[c]) stack.push(want[c]);
        else if (c === "}" || c === ")" || c === "]") { if (stack.pop() !== c) return -1; if (!stack.length) return i; }
    }
    return -1;
}

// split a block body into statements: [{pre, code}] + tail (pre: the whitespace / comments before the statement)
export function splitStatements(body) {
    const segs = [];
    let depth = 0, start = 0, codeStart = -1;
    for (let i = 0; i < body.length; i++) {
        const j = skipLit(body, i);
        if (j !== i) { if (body[i] === '"' || body[i] === "'") { if (codeStart < 0) codeStart = i; } i = j - 1; continue; }
        const c = body[i];
        if (/\s/.test(c)) continue;
        if (codeStart < 0) codeStart = i;
        if (c === "(" || c === "{" || c === "[") depth++;
        else if (c === ")" || c === "}" || c === "]") depth--;
        else if (c === ";" && depth === 0) { segs.push({ pre: body.slice(start, codeStart), code: body.slice(codeStart, i + 1) }); start = i + 1; codeStart = -1; }
    }
    return { segs, tail: body.slice(start) };
}
// a statement that is one test function call: {prefix, fn, args}
export function asCall(code, isFn) {
    const m = /^((?:g_fail\s*\+=\s*)?)([A-Za-z_]\w*)\s*\(/.exec(code);
    if (!m || (isFn && !isFn(m[2]))) return null;
    const open = m[0].length - 1, close = matchClose(code, open);
    if (close < 0 || code.slice(close + 1).trim() !== ";") return null;
    return { prefix: m[1], fn: m[2], args: code.slice(open + 1, close) };
}

// where an entry's block body is: a group ({"name", [] { ... }}) or a command (if (... == "name") { ... })
export function findBody(text, entry) {
    const ls = lineStarts(text);
    const a = ls[entry.line - 1];
    const endLine = entry.endLine || entry.line;
    const b = endLine < ls.length ? ls[endLine] : text.length;
    const chunk = text.slice(a, b);
    let open = -1;
    if (entry.kind === "group") { const m = /\[[^\]]*\]\s*(\([^)]*\)\s*)?\{/.exec(chunk); if (m) open = a + m.index + m[0].length - 1; }
    else { const m = new RegExp('==\\s*"' + entry.name.replace(/\W/g, "\\$&") + '"\\s*\\)\\s*\\{').exec(chunk); if (m) open = a + m.index + m[0].length - 1; }
    if (open < 0) return null;
    const close = matchClose(text, open);
    if (close < 0) return null;
    return { open, close, body: text.slice(open + 1, close) };
}

// the editable sequence of an entry: [{kind: "call", fn, args, prefix} | {kind: "code", code}] (+ what rebuilds it)
export function entrySequence(text, entry, isFn) {
    const fb = findBody(text, entry);
    if (!fb) return null;
    const { segs, tail } = splitStatements(fb.body);
    const seq = segs.map((s, i) => {
        const c = asCall(s.code, isFn);
        return c ? { kind: "call", ...c, pre: s.pre, orig: i } : { kind: "code", code: s.code, pre: s.pre, orig: i };
    });
    return { seq, tail, fb, multiline: fb.body.includes("\n") };
}
export function stmtCode(s) { return s.kind === "call" ? (s.prefix || "") + s.fn + "(" + (s.args || "") + ");" : s.code; }

// rebuild an entry's body from a new sequence (moved / removed / added / edited statements)
export function writeSequence(text, entry, newSeq, isFn) {
    const cur = entrySequence(text, entry, isFn);
    if (!cur) throw new Error("can't find the body of " + entry.name);
    const { seq, tail, fb } = cur;
    let sep = seq.length > 1 ? seq[1].pre : seq.length ? (fb.body.includes("\n") ? seq[0].pre : " ") : " ";
    if (!/\S/.test(sep) && sep === "") sep = " ";
    const pre0 = seq.length ? seq[0].pre : " ";
    let out = "";
    const pres = [];
    newSeq.forEach((s, i) => {
        let pre;
        if (i === 0) pre = pre0;
        else if (s.orig !== undefined && s.orig !== 0 && seq[s.orig]) pre = seq[s.orig].pre;
        else pre = sepOnly(i > 1 || pres[i - 1] !== pre0 ? pres[i - 1] : sep);   // (a new one: spaced like the one before it)
        pres.push(pre);
        out += pre + stmtCode(s);
    });
    let t = seq.length ? tail : " ";
    if (!newSeq.length) t = " ";
    return text.slice(0, fb.open + 1) + out + t + text.slice(fb.close);
}
const sepOnly = (s) => { const m = /\s*$/.exec(s.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\//g, "")); const w = m ? m[0] : " "; return w || " "; };

// ---- the par / alone vectors
export function findVectors(text) {
    const lines = text.split("\n"), out = {};
    for (let i = 0; i < lines.length; i++) {
        const m = /std::vector<T>\s+(\w+)\s*=\s*\{/.exec(lines[i]);
        if (!m) continue;
        let j = i + 1;
        while (j < lines.length && lines[j].trim() !== "};") j++;
        out[m[1]] = { start: i + 1, end: j + 1 };   // 1-based: the opening line and the "};" line
    }
    return out;
}
const groupStartRe = (name) => new RegExp('^\\s*\\{\\s*"' + name.replace(/\W/g, "\\$&") + '"\\s*,');
// the groups of each list, located by name: {par: [{name, from, to}], alone: [...]} (0-based line indices, inclusive)
export function listEntries(text) {
    const lines = text.split("\n"), ls = lineStarts(text), v = findVectors(text), out = {};
    for (const list in v) {
        out[list] = [];
        for (let i = v[list].start; i < v[list].end - 1; i++) {
            const m = /^\s*\{\s*"([\w-]+)"\s*,/.exec(lines[i]);
            if (!m) continue;
            const open = ls[i] + lines[i].indexOf("{");
            const close = matchClose(text, open);
            let to = i;
            while (to + 1 < ls.length && ls[to + 1] <= close) to++;
            out[list].push({ name: m[1], from: i, to });
            i = to;
        }
    }
    return out;
}
// rewrite the par / alone vectors to hold these names in this order (a name not in the file becomes an empty group)
export function writeLists(text, want) {
    const lines = text.split("\n");
    const v = findVectors(text), ent = listEntries(text);
    const blocks = new Map();
    // a group's block: its lines, and the // comment lines right above it (they move with it), up to the previous
    // group's last line or the list's start
    for (const list in ent) ent[list].forEach((e, k) => {
        const floor = k ? ent[list][k - 1].to + 1 : v[list].start;
        let from = e.from;
        while (from - 1 >= floor && /^\s*\/\//.test(lines[from - 1])) from--;
        e.lead = from;
        blocks.set(e.name, lines.slice(from, e.to + 1));
    });
    const lists = Object.keys(want).filter((l) => v[l]).sort((x, y) => v[y].start - v[x].start);   // bottom-up: line numbers stay valid
    for (const list of lists) {
        const inside = lines.slice(v[list].start, v[list].end - 1);
        const used = new Set();
        for (const e of ent[list]) for (let i = e.lead; i <= e.to; i++) used.add(i - v[list].start);
        const keep = inside.filter((_, i) => !used.has(i));   // other comments / blank lines in the list stay first
        const ind = ent[list].length ? /^\s*/.exec(lines[ent[list][0].from])[0] : "        ";
        const body = [...keep];
        for (const n of want[list]) body.push(...(blocks.get(n) || [`${ind}{"${n}", [] { }},`]));
        lines.splice(v[list].start, v[list].end - 1 - v[list].start, ...body);
    }
    return lines.join("\n");
}
export function moveGroup(text, group, list, before = null) {
    const ent = listEntries(text), want = {};
    for (const l in ent) want[l] = ent[l].map((e) => e.name).filter((n) => n !== group.name);
    if (!want[list]) throw new Error("no std::vector<T> " + list);
    const at = before ? want[list].indexOf(before) : -1;
    want[list].splice(at < 0 ? want[list].length : at, 0, group.name);
    return writeLists(text, want);
}
export function addGroup(text, list, name) {
    const ent = listEntries(text), want = {};
    for (const l in ent) want[l] = ent[l].map((e) => e.name);
    want[list].push(name);
    return writeLists(text, want);
}
export function removeGroup(text, group) {
    const ent = listEntries(text), want = {};
    for (const l in ent) want[l] = ent[l].map((e) => e.name).filter((n) => n !== group.name);
    return writeLists(text, want);
}

// ======================================================================= YAML (workflows: jobs and steps)
const indentOf = (l) => /^\s*/.exec(l)[0].length;
// value + trailing comment of "key: value   # comment"
export function splitYamlComment(v) {
    let q = null;
    for (let i = 0; i < v.length; i++) {
        const c = v[i];
        if (q) { if (c === q) q = null; continue; }
        if (c === '"' || c === "'") { if (i === 0 || /\s/.test(v[i - 1])) q = c; continue; }
        if (c === "#" && (i === 0 || /\s/.test(v[i - 1]))) { let s = i; while (s > 0 && /\s/.test(v[s - 1])) s--; return [v.slice(0, s), v.slice(s)]; }
    }
    return [v.replace(/\s+$/, ""), ""];
}
export function yamlScalar(v) {
    v = String(v).trim();
    if (!v) return '""';
    if (/^\$\{\{[\s\S]*\}\}$/.test(v)) return v;
    if (/^"[\s\S]*"$/.test(v) || /^'[\s\S]*'$/.test(v)) return v;
    if (/^[!&*{}\[\]|>%@`,?:-]/.test(v) || /: |\s#|^\s|\s$/.test(v) || /^(true|false|yes|no|null|~|on|off)$/i.test(v)) return JSON.stringify(v);
    return v;
}
function keyLineValue(line, key) {
    const m = new RegExp("^(\\s*(?:- )?)" + key + ":(\\s*)(.*)$").exec(line);
    if (!m) return null;
    const [val, comment] = splitYamlComment(m[3]);
    return { lead: m[1], gap: m[2] || " ", val, comment };
}
// set / insert / remove a job-level key ("if", "needs", "runs-on"); value null = remove
export function setJobKey(text, job, key, value, lineNo) {
    const lines = text.split("\n");
    const jl = job.line - 1;
    const ki = indentOf(lines[jl]) + 2;
    if (lineNo) {
        const i = lineNo - 1;
        const kv = keyLineValue(lines[i], key);
        if (!kv) throw new Error(`line ${lineNo} isn't the job's ${key}:`);
        let extra = 0;   // a block list ("needs:\n  - a") or a folded value under it
        if (!kv.val) while (i + 1 + extra < lines.length && lines[i + 1 + extra].trim() && indentOf(lines[i + 1 + extra]) > ki) extra++;
        if (value === null) lines.splice(i, 1 + extra);
        else lines.splice(i, 1 + extra, kv.lead + key + ": " + value + kv.comment);
    } else if (value !== null) {
        // keep the usual order: needs, if, runs-on (after the job's own line and its comments)
        const order = ["needs", "if", "runs-on"];
        let at = jl + 1;
        const end = (job.endLine || lines.length) - 1;
        for (let i = jl + 1; i <= end && i < lines.length; i++) {
            const l = lines[i];
            if (!l.trim() || l.trim().startsWith("#")) { if (at === i) at = i + 1; continue; }
            if (indentOf(l) !== ki) continue;
            const k = /^\s*([\w-]+):/.exec(l);
            if (k && order.includes(k[1]) && order.indexOf(k[1]) < order.indexOf(key)) at = i + 1;
            else if (k && order.includes(k[1])) break;
        }
        lines.splice(at, 0, " ".repeat(ki) + key + ": " + value);
    }
    return lines.join("\n");
}
export function needsValue(list, wasList) {
    if (!list.length) return null;
    return list.length === 1 && !wasList ? list[0] : "[" + list.join(", ") + "]";
}
export const setJobIf = (text, job, expr) => setJobKey(text, job, "if", expr && expr.trim() ? yamlScalar(expr) : null, job.lines && job.lines.if);
export function setJobNeeds(text, job, list) {
    const lines = text.split("\n");
    const ln = job.lines && job.lines.needs;
    const wasList = ln ? /\[/.test(lines[ln - 1]) : false;
    return setJobKey(text, job, "needs", needsValue(list, wasList || list.length > 1), ln);
}
export const setRunsOn = (text, job, v) => setJobKey(text, job, "runs-on", v && v.trim() ? yamlScalar(v) : null, job.lines && job.lines.runsOn);

// ---- steps
function stepBlocks(lines, job) {
    const st = job.steps || [];
    const blocks = [];
    let prevEnd = st.length ? st[0].line - 1 : 0;   // 0-based index after the previous block
    st.forEach((s, i) => {
        let end = (s.endLine || s.line) - 1;
        while (end > s.line - 1 && !lines[end].trim()) end--;   // (trailing blank lines stay where they are)
        const from = i === 0 ? s.line - 1 : prevEnd;
        blocks.push({ from, to: end, lines: lines.slice(from, end + 1), step: s, index: i });
        prevEnd = end + 1;
    });
    return blocks;
}
function stepIndents(lines, job) {
    const s0 = job.steps && job.steps[0];
    const ref = s0 ? lines[s0.line - 1] : null;
    const dash = ref ? indentOf(ref) : indentOf(lines[job.line - 1]) + 6;
    return { dash, key: dash + 2 };
}
// order: [old step index | {new step fields}] -> the job's steps in that order
export function writeSteps(text, job, order, edits = {}) {
    const lines = text.split("\n");
    const ind = stepIndents(lines, job);
    const bl = stepBlocks(lines, job);
    const out = [];
    for (const o of order) {
        if (typeof o === "number") out.push(...(edits[o] ? editStepLines(bl[o].lines, bl[o].step.line - 1 - bl[o].from, edits[o]) : bl[o].lines));
        else out.push(...stepText(o, ind.dash));
    }
    if (!bl.length) {   // no steps yet: after "steps:" (or a new "steps:" at the job's end)
        const sl = job.lines && job.lines.steps;
        if (sl) { lines.splice(sl, 0, ...out); return lines.join("\n"); }
        let end = (job.endLine || job.line) - 1;
        while (end > job.line - 1 && !lines[end].trim()) end--;
        lines.splice(end + 1, 0, " ".repeat(indentOf(lines[job.line - 1]) + 2) + "steps:", ...out);
        return lines.join("\n");
    }
    const from = bl[0].from, to = bl[bl.length - 1].to;
    lines.splice(from, to - from + 1, ...out);
    return lines.join("\n");
}
export function stepText(s, dash) {
    const k = " ".repeat(dash + 2), out = [];
    const keys = [];
    if (s.name) keys.push("name: " + yamlScalar(s.name));
    if (s.id) keys.push("id: " + s.id);
    if (s.if) keys.push("if: " + yamlScalar(s.if));
    if (s.uses) keys.push("uses: " + s.uses);
    if (s.with) for (const w in s.with) keys.push("with:"), keys.push(`  ${w}: ${s.with[w]}`);
    if (s.run !== undefined && s.run !== null) {
        const r = String(s.run).replace(/\s+$/, "");
        if (r.includes("\n")) { keys.push("run: |"); for (const l of r.split("\n")) keys.push(l ? "  " + l : ""); }
        else keys.push("run: " + yamlScalar(r));
    }
    if (!keys.length) keys.push("run: echo");
    keys.forEach((kk, i) => out.push(i === 0 ? " ".repeat(dash) + "- " + kk : kk === "" ? "" : k + kk));
    return out;
}
// edit one step's name / if / run in place (null = remove the key; undefined = leave it)
export function editStep(text, job, index, fields) {
    return writeSteps(text, job, (job.steps || []).map((_, i) => i), { [index]: fields });
}
function editStepLines(L, first, fields) {
    L = L.slice();
    const dash = indentOf(L[first]), ki = dash + 2;
    const keyAt = (key) => { for (let i = first; i < L.length; i++) { const l = L[i]; const kv = i === first ? new RegExp("^\\s*- " + key + ":").test(l) : indentOf(l) === ki && new RegExp("^\\s*" + key + ":").test(l); if (kv) return i; } return -1; };
    const extent = (i) => { let j = i + 1; while (j < L.length && (!L[j].trim() || indentOf(L[j]) > ki)) j++; while (j - 1 > i && !L[j - 1].trim()) j--; return j; };   // [i, j)
    const lead = (i) => (i === first ? " ".repeat(dash) + "- " : " ".repeat(ki));
    const removeKey = (i) => {
        const j = extent(i);
        L.splice(i, j - i);
        if (i === first) {   // the next key takes the dash
            for (let n = first; n < L.length; n++) if (L[n].trim() && indentOf(L[n]) === ki) { L[n] = " ".repeat(dash) + "- " + L[n].trimStart(); break; }
        }
    };
    const setScalar = (key, v, insertAfterKeys) => {
        const i = keyAt(key);
        if (v === undefined) return;
        if (v === null || v === "") { if (i >= 0) removeKey(i); return; }
        if (i >= 0) {
            const kv = keyLineValue(L[i], key);
            const j = extent(i);
            L.splice(i, j - i, lead(i) + key + ": " + yamlScalar(v) + (kv ? kv.comment : ""));
            return;
        }
        if (key === "name") {   // name goes first: it takes the dash
            L[first] = " ".repeat(ki) + L[first].trimStart().replace(/^- /, "");
            L.splice(first, 0, " ".repeat(dash) + "- name: " + yamlScalar(v));
            return;
        }
        let at = first + 1;
        for (const k of insertAfterKeys) { const p = keyAt(k); if (p >= 0) at = Math.max(at, extent(p)); }
        L.splice(at, 0, " ".repeat(ki) + key + ": " + yamlScalar(v));
    };
    setScalar("name", fields.name, []);
    setScalar("if", fields.if, ["name", "id"]);
    if (fields.run !== undefined) {
        const i = keyAt("run");
        const r = fields.run === null ? null : String(fields.run).replace(/\s+$/, "");
        if (r === null || r === "") { if (i >= 0) removeKey(i); }
        else {
            const body = r.includes("\n") ? ["run: |", ...r.split("\n").map((l) => (l ? " ".repeat(ki + 2) + l : ""))] : ["run: " + yamlScalar(r)];
            if (i >= 0) {
                const kv = keyLineValue(L[i], "run");
                const j = extent(i);
                const keepComment = kv && kv.comment && !r.includes("\n") ? kv.comment : "";
                L.splice(i, j - i, lead(i) + body[0] + keepComment, ...body.slice(1));
            } else {
                let end = L.length; while (end > first + 1 && !L[end - 1].trim()) end--;
                L.splice(end, 0, " ".repeat(ki) + body[0], ...body.slice(1));
            }
        }
    }
    return L;
}

// a whole job draft at once: {steps: [old index | new step], stepEdits: {i: fields}, if, needs, runsOn} (undefined = unchanged)
export function applyJobDraft(text, job, d) {
    let t = d.steps ? writeSteps(text, job, d.steps, d.stepEdits || {}) : text;
    const keys = [];   // bottom-up by line, so each line number is still right when it's used
    if (d.runsOn !== undefined) keys.push({ line: job.lines && job.lines.runsOn, f: (x) => setRunsOn(x, job, d.runsOn) });
    if (d.if !== undefined) keys.push({ line: job.lines && job.lines.if, f: (x) => setJobIf(x, job, d.if) });
    if (d.needs !== undefined) keys.push({ line: job.lines && job.lines.needs, f: (x) => setJobNeeds(x, job, d.needs) });
    keys.sort((a, b) => (b.line || 0) - (a.line || 0));
    for (const k of keys) t = k.f(t);
    return t;
}

// ======================================================================= rules (warn before a save)
const count = (t, s) => t.split(s).length - 1;
export function ruleHits(path, oldText, newText, model, added) {
    const hits = [];
    const rule = (id) => ((model && model.rules) || []).find((r) => r.id === id);
    if (count(newText, "[final]") > count(oldText, "[final]"))
        hits.push({ id: "final", text: (rule("final") || {}).text || "Only a human marks [final].", detail: "this change adds a [final] marker" });
    if (/^\.github\/workflows\//.test(path)) {
        const before = gameplayRuns(oldText, model), after = gameplayRuns(newText, model);
        if (after.length > before.length) {
            const o = after.find((x) => !before.some((y) => y.text === x.text)) || after[after.length - 1];
            hits.push({ id: "ci-scope", text: (rule("ci-scope") || {}).text || "CI runs only meta, builds and sanitizers.", detail: `line ${o.line}: runs quark_tests ${o.what} in CI (only the sanitizers job may run quark_tests)` });
        }
    }
    return hits;
}

// quark_tests runs of gameplay tests in a workflow's text (not a tool command such as exportassets): [{line, text, what}]
const TOOLS = new Set(["exportassets", "groups", "racefloors", "serverbench", "snapbench", "navbench", "playtest", "bot", "ffaexperts", "bounds",
    "exportbrain", "checkbrain", "brainast", "printbrain", "brainbench", "exportmode", "checkmode", "modeast", "printmode", "modebench"]);
export function gameplayRuns(text, model) {
    const tools = new Set(TOOLS);
    for (const s of (model && model.suites) || []) for (const it of s.items || []) if (it.kind === "tool") tools.add(it.name);
    const out = [];
    text.split("\n").forEach((l, i) => {
        const code = l.trim().startsWith("#") ? "" : l.replace(/\s#.*$/, "");
        for (const m of code.matchAll(/(^|[\s;&|(])(\.\/)?([\w.-]+\/)*quark_tests(\.exe)?(?![\w.-])(\s+(group\s+)?([\w-]+))?/g)) {
            if (/--target\s*$|cmake/.test(code.slice(0, m.index + m[1].length))) continue;
            const arg = m[7];
            if (arg && tools.has(arg)) continue;
            out.push({ line: i + 1, text: l.trim(), what: arg ? (m[6] ? "group " + arg : arg) : "(every group)" });
        }
        const g = /\bgroup\s*:\s*\[(.*)\]/.exec(code);
        if (g) out.push({ line: i + 1, text: l.trim(), what: "a matrix of groups" });
    });
    return out;
}
