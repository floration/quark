// Runs the worker against an in-memory SQLite with a tiny D1 lookalike: node tools/master/test.mjs
import { DatabaseSync } from "node:sqlite";
import { readFileSync } from "node:fs";
import { createHash, createPrivateKey, createPublicKey, sign as edSign } from "node:crypto";
import worker, { rateTeams, rankKeyOf, prune } from "./worker.js";

const db = new DatabaseSync(":memory:");
db.exec(readFileSync(new URL("./schema.sql", import.meta.url), "utf8"));
const stmt = (sql, args = []) => ({
  bind: (...a) => stmt(sql, a),
  run: async () => ({ meta: { changes: Number(db.prepare(sql).run(...args).changes) } }),
  first: async () => db.prepare(sql).get(...args) ?? null,
  all: async () => ({ results: db.prepare(sql).all(...args) }),
});
// a D1 batch is one transaction: all or nothing
const env = { DB: { prepare: (sql) => stmt(sql), batch: async (list) => {
  db.exec("BEGIN");
  try { const out = []; for (const s of list) out.push(await s.run()); db.exec("COMMIT"); return out; }
  catch (e) { db.exec("ROLLBACK"); throw e; }
} } };

let fails = 0;
const check = (ok, what) => { if (!ok) { fails++; console.log("FAIL", what); } };
const sha = (t) => createHash("sha256").update(t).digest("hex");
const secret = (n) => sha("s" + n);
// Ranked keys, derived independently of worker.js (node:crypto), exactly as the game does (ranked.cpp)
const keyOf = (sec) => {
  const seed = Buffer.from(sha("quark-rank-ed25519-v1:" + sec), "hex");
  const priv = createPrivateKey({ key: Buffer.concat([Buffer.from("302e020100300506032b657004220420", "hex"), seed]), format: "der", type: "pkcs8" });
  const pub = createPublicKey(priv).export({ format: "der", type: "spki" }).subarray(-32).toString("hex");
  return { priv, pub, id: sha(pub).slice(0, 32) };
};
const keys = new Map();
const key = (n) => { if (!keys.has(n)) keys.set(n, keyOf(secret(n))); return keys.get(n); };
const id = (n) => key(n).id;
const legacy = (n) => sha(secret(n)).slice(0, 32);
// the game signs the match id and the server address it dialled (ranked.cpp rankTicketMessage; quark_tests checks the text)
const signTicket = (n, match, addr) => edSign(null, Buffer.from("quark-ticket-v2 " + match + " " + addr), key(n).priv).toString("hex");
let callN = 0; // each call from its own (IPv6) client so per-IP rate limits don't trip; announce tests pass `from`
const fetchRaw = async (path, form, from, extra = {}) => {
  const headers = { "CF-Connecting-IP": from || "2001:db8::" + (++callN).toString(16), ...extra };
  const init = form ? { method: "POST", body: new URLSearchParams(form), headers } : { headers };
  return worker.fetch(new Request("https://x" + path, init), env, { waitUntil: () => {} });
};
const call = async (path, form, from) => (await fetchRaw(path, form, from)).text();

// key derivation: the same numbers as the game (quark_tests "ranked" checks this vector on the C++ side)
{
  const k = keyOf("a".repeat(64)), w = await rankKeyOf("a".repeat(64));
  check(k.pub === "7361c4bb035a82708b4b001bcfd4b748ff9f89b9647a51d2dc4a7f0dc20d497d" && k.id === "694e20014c510c34c129e731ef06fe1d", "key vector " + k.pub);
  check(w.pub === k.pub && w.id === k.id && w.legacy === sha("a".repeat(64)).slice(0, 32), "worker derives the same key");
}
const ratings = async (ns, ladder = "vanilla") => Object.fromEntries((await call("/ratings?ladder=" + ladder + "&ids=" + ns.map(id).join(","))).split("\n")
  .map((l) => l.split(" ")).map(([i, mu, s, g]) => [i, { mu: +mu, sigma: +s, games: +g }]));

// math: winners gain, losers lose, equal and opposite in a fair 1v1; uncertainty shrinks
{
  const t = [{ players: [{ mu: 25, sigma: 25 / 3 }], rank: 0 }, { players: [{ mu: 25, sigma: 25 / 3 }], rank: 1 }];
  rateTeams(t);
  const [w, l] = [t[0].players[0], t[1].players[0]];
  check(w.mu > 25 && l.mu < 25 && Math.abs((w.mu - 25) + (l.mu - 25)) < 1e-9 && w.sigma < 25 / 3, "1v1 update " + JSON.stringify(t));
  console.log("1v1 from defaults: winner", w.mu.toFixed(3), w.sigma.toFixed(3), "loser", l.mu.toFixed(3));
  // 3v3 and 4-player ffa with a tie run without NaN
  const p = () => ({ mu: 25, sigma: 25 / 3 });
  const t3 = [{ players: [p(), p(), p()], rank: 0 }, { players: [p(), p(), p()], rank: 1 }];
  rateTeams(t3);
  check(t3.flat().every((t) => t.players.every((q) => isFinite(q.mu) && isFinite(q.sigma))), "3v3 finite");
  check(t3[0].players[0].mu > 25 && t3[1].players[0].mu < 25, "3v3 direction");
  const ffa = [0, 1, 1, 2].map((r) => ({ players: [p()], rank: r }));
  rateTeams(ffa);
  check(ffa[0].players[0].mu > ffa[1].players[0].mu && Math.abs(ffa[1].players[0].mu - ffa[2].players[0].mu) < 1e-9 && ffa[3].players[0].mu < 25, "ffa tie");
  // an upset moves more than an expected win
  const strong = { mu: 35, sigma: 3 }, weak = { mu: 20, sigma: 3 };
  const exp = [{ players: [{ ...strong }], rank: 0 }, { players: [{ ...weak }], rank: 1 }];
  const ups = [{ players: [{ ...strong }], rank: 1 }, { players: [{ ...weak }], rank: 0 }];
  rateTeams(exp); rateTeams(ups);
  check(ups[1].players[0].mu - 20 > exp[0].players[0].mu - 35, "upset gains more");
}

// Game servers: announce (hidden) for a token, then open tickets / post results and race runs with it.
let srvN = 0;
const server = async () => {
  const ip = "198.18.0." + (++srvN), port = 27600 + srvN;
  const tok = (await call("/announce", { port: String(port), v: "t9", hidden: "1" }, ip)).split(" ")[1];
  const S = { ip, addr: ip + ":" + port, f: { port: String(port), token: tok } };
  S.post = (path, form) => call(path, { ...S.f, ...form }, S.ip);
  // every player signs the match id in the game; the server forwards keys and signatures with the ticket
  // (each signature names the address that player's game dialled: this server's, unless a test says otherwise)
  S.ticket = (match, ladder, ns, map = "temple", signers = ns, dialled = ns.map(() => S.addr)) => S.post("/ticket", { kind: "match", match, ladder, map,
    players: ns.map(id).join(","), keys: ns.map((n) => key(n).pub).join(","), sigs: ns.map((n, i) => signTicket(signers[i], match, dialled[i])).join(","),
    addrs: dialled.join(",") });
  S.result = (match, p) => S.post("/result", { match, p });
  // a time trial: the racer's game signs the race ticket's id the same way
  S.race = (match, n, map = "viaduct", signer = n, dialled = S.addr) => S.post("/ticket", { kind: "race", match, map, players: id(n), keys: key(n).pub,
    sigs: signTicket(signer, match, dialled), addrs: dialled });
  return S;
};
const age = (match, ms = 120000) => db.prepare("UPDATE tickets SET created = created - ? WHERE id = ?").run(ms, match);
// A whole ticketed match: ticket, a minute passes, the server's result, every player's report. Returns the last reply.
const play = async (S, match, ladder, entries, extra = {}) => {
  const ns = entries.map((e) => e[0]);
  check((await S.ticket(match, ladder, ns)) === "ticket " + match, "ticket " + match);
  age(match);
  const p = entries.map(([n, team, place]) => `${id(n)}:${team}:${place}`).join(",");
  await S.result(match, p);
  let last = "";
  for (const n of ns) last = await call("/report", { secret: secret(n), match, ladder, p, ...(extra[n] || {}) });
  return last;
};

check((await call("/list?v=t9")) === "", "hidden servers aren't listed");
// reporting: 2v2, the server and every player must agree, under a ticket
{
  const S = await server();
  const p = `${id(1)}:0:0,${id(2)}:0:0,${id(3)}:1:1,${id(4)}:1:1`, match = "aaaaaaaaaaaaaaaa";
  check((await call("/report", { secret: secret(1), match, ladder: "vanilla", p })) === "no ticket", "no ticket, no report (R1: no game at all)");
  check((await call("/ticket", { port: "27999", token: "0".repeat(32), kind: "match", match, ladder: "vanilla", map: "temple", players: [1, 2, 3, 4].map(id).join(",") }, "198.18.9.9")).startsWith("forbidden"), "unregistered server can't open tickets");
  check((await S.post("/ticket", { kind: "match", match, ladder: "vanilla", map: "temple", players: [1, 2, 3, 4].map(id).join(",") })) === "missing signatures", "a ticket needs every player's signature");
  check((await S.ticket(match, "vanilla", [1, 2, 3, 4], "temple", [1, 2, 3, 9])) === "bad signature for " + id(4), "a player who didn't sign can't be ticketed (someone else's signature)");
  check((await S.post("/ticket", { kind: "match", match, ladder: "vanilla", map: "temple", players: [1, 2].map(id).join(","), keys: [key(1).pub, key(3).pub].join(","),
    sigs: [signTicket(1, match, S.addr), signTicket(3, match, S.addr)].join(","), addrs: [S.addr, S.addr].join(",") })) === "bad key for " + id(2), "a key must hash to the id");
  {   // REVIEW 2.2: a signature made for another server (relayed by the one the player joined) opens no ticket here
    const other = await server(), S3 = await server();
    const elsewhere = [S.addr, S.addr, S.addr, other.addr];
    check((await S.ticket(match, "vanilla", [1, 2, 3, 4], "temple", [1, 2, 3, 4], elsewhere)) === "signed for another server: " + id(4), "ticket signed for another server refused");
    const lie = { kind: "match", match, ladder: "vanilla", map: "temple", players: [1, 2].map(id).join(","), keys: [key(1).pub, key(2).pub].join(","),
      sigs: [signTicket(1, match, S.addr), signTicket(2, match, other.addr)].join(","), addrs: [S.addr, S.addr].join(",") };
    check((await S.post("/ticket", lie)) === "bad signature for " + id(2), "a signature for another server can't be passed off as ours");
    const { addrs: _, ...unnamed } = lie;
    check((await S.post("/ticket", unnamed)).startsWith("update the server"), "old servers (no addrs) can't ticket");
    const port3 = S3.addr.split(":")[1];
    check((await S3.ticket("abcdabcdabcdabcd", "vanilla", [1, 2], "temple", [1, 2], ["10.1.2.3:" + (+port3 + 1), S3.addr])) === "signed for another server: " + id(1), "a LAN address on another port refused");
    check((await S3.ticket("abcdabcdabcdabcd", "vanilla", [1, 2], "temple", [1, 2], ["8.8.8.8:" + port3, S3.addr])) === "signed for another server: " + id(1), "a public address that isn't the server refused");
    check((await S3.race("abcdabcdabcdabce", 1, "viaduct", 1, other.addr)) === "signed for another server: " + id(1), "race ticket signed for another server refused");
    check((await S3.ticket("abcdabcdabcdabcd", "vanilla", [1, 2], "temple", [1, 2], ["192.168.1.20:" + port3, "127.0.0.1:" + port3])) === "ticket abcdabcdabcdabcd", "LAN / same-machine players on the server's port");
  }
  check((await S.ticket(match, "vanilla", [1, 2, 3, 4])) === "ticket " + match, "ticket opened");
  check((await S.ticket("abababababababab", "vanilla", [1, 2])) === "too soon", "one ticket a minute per server");
  check((await call("/report", { secret: secret(1), match, ladder: "vanilla", p })) === "pending 1/4", "an early report is kept, waiting for the server's result");
  age(match);
  check((await call("/report", { secret: secret(1), match, ladder: "vanilla", p })) === "pending 1/4", "first report pending");
  check((await call("/report", { secret: secret(1), match, ladder: "vanilla", p })) === "pending 1/4", "duplicate report ignored");
  check((await call("/report", { secret: secret(9), match, ladder: "vanilla", p: p + `,${id(9)}:1:1` })) === "not the ticket's players", "outsider refused");
  check((await call("/report", { secret: secret(1), match, ladder: "vampire", p })) === "not the ticket's players", "ladder must match the ticket");
  await call("/report", { secret: secret(2), match, ladder: "vanilla", p });
  await call("/report", { secret: secret(3), match, ladder: "vanilla", p });
  check((await call("/report", { secret: secret(4), match, ladder: "vanilla", p })) === "pending server", "players agree, the server hasn't reported");
  let r = await ratings([1, 3]);
  check(r[id(1)].games === 0, "not rated before the server reported");
  const other = await server();
  check((await other.result(match, p)) === "no ticket", "only the ticket's server reports its result");
  check((await S.result(match, p)) === "done", "the server's result completes it");
  r = await ratings([1, 2, 3, 4]);
  check(r[id(1)].mu > 25 && r[id(2)].mu > 25 && r[id(3)].mu < 25 && r[id(1)].games === 1, "2v2 rated");
  check((await call("/report", { secret: secret(4), match, ladder: "vanilla", p })) === "done", "rated only once");
  r = await ratings([1]);
  check(r[id(1)].games === 1, "still one game");
}
// a lone dissent is a dispute (not rated until the owner settles it); every player against the server voids it
{
  env.ADMIN_KEY = "adminkey";
  const S = await server();
  const won = `${id(5)}:0:0,${id(6)}:1:1`, lost = `${id(5)}:0:1,${id(6)}:1:0`;
  await S.ticket("bbbbbbbbbbbbbbbb", "vanilla", [5, 6]);
  age("bbbbbbbbbbbbbbbb");
  await S.result("bbbbbbbbbbbbbbbb", won);
  check((await call("/report", { secret: secret(5), match: "bbbbbbbbbbbbbbbb", ladder: "vanilla", p: won })) === "pending 1/2", "agreeing report pending");
  check((await call("/report", { secret: secret(6), match: "bbbbbbbbbbbbbbbb", ladder: "vanilla", p: won, dispute: "1" })) === "disputed", "a lone dissent is a dispute, not a void");
  check((await call("/report", { secret: secret(6), match: "bbbbbbbbbbbbbbbb", ladder: "vanilla", p: lost })) === "disputed", "stays disputed");
  let r = await ratings([5, 6]);
  check(r[id(5)].games === 0 && r[id(6)].games === 0, "a dispute isn't rated");
  check((await call("/disputes?key=adminkey")).startsWith("bbbbbbbbbbbbbbbb vanilla "), "the owner sees it");
  check((await call("/resolve", { key: "wrong", match: "bbbbbbbbbbbbbbbb", action: "rate" })) === "forbidden", "only the owner settles");
  check((await call("/resolve", { key: "adminkey", match: "bbbbbbbbbbbbbbbb", action: "rate" })) === "done", "the owner rates it as the server reported");
  r = await ratings([5, 6]);
  check(r[id(5)].games === 1 && r[id(5)].mu > 25 && r[id(6)].mu < 25, "settled dispute rated");
  // a lying host: both players report the other result
  const S2 = await server();
  await S2.ticket("bcbcbcbcbcbcbcbc", "vanilla", [5, 6]);
  age("bcbcbcbcbcbcbcbc");
  await S2.result("bcbcbcbcbcbcbcbc", lost);
  await call("/report", { secret: secret(5), match: "bcbcbcbcbcbcbcbc", ladder: "vanilla", p: lost, dispute: "1" });
  check((await call("/report", { secret: secret(6), match: "bcbcbcbcbcbcbcbc", ladder: "vanilla", p: lost, dispute: "1" })).startsWith("void"), "every player against the server: void");
  r = await ratings([5, 6]);
  check(r[id(5)].games === 1 && r[id(6)].games === 1, "void match not rated");
  const S3 = await server();
  await S3.ticket("bdbdbdbdbdbdbdbd", "vanilla", [5, 6]);
  age("bdbdbdbdbdbdbdbd", 3 * 3600000);
  check((await call("/report", { secret: secret(5), match: "bdbdbdbdbdbdbdbd", ladder: "vanilla", p: won })) === "expired", "ticket window closes");
}
// leaving: the server names who left; the first leaver loses and isn't waited for. A silent player is waited for
// REPORT_WAIT_MS, then the match is rated (by the cron) if someone agreed and nobody objected.
{
  const S = await server();
  const p = `${id(21)}:0:0,${id(22)}:1:1`;
  await S.ticket("cdcdcdcdcdcdcdcd", "vanilla", [21, 22]);
  age("cdcdcdcdcdcdcdcd");
  check((await S.post("/result", { match: "cdcdcdcdcdcdcdcd", p, left: id(9) })) === "bad left", "a leaver must be in the match");
  await S.post("/result", { match: "cdcdcdcdcdcdcdcd", p, left: id(22) });
  check((await call("/report", { secret: secret(21), match: "cdcdcdcdcdcdcdcd", ladder: "vanilla", p })) === "done", "the stayer's report rates it (the leaver can't report)");
  let r = await ratings([21, 22]);
  check(r[id(21)].mu > 25 && r[id(22)].mu < 25 && r[id(22)].games === 1, "the leaver lost");
  const S2 = await server();
  await S2.ticket("cececececececece", "vanilla", [21, 22]);
  age("cececececececece");
  await S2.result("cececececececece", p);
  check((await call("/report", { secret: secret(21), match: "cececececececece", ladder: "vanilla", p })) === "pending 1/2", "waits for the other report");
  check((await prune(env, Date.now())) === 0, "not yet");
  db.prepare("UPDATE tickets SET resulted = resulted - ? WHERE id = ?").run(11 * 60000, "cececececececece");
  check((await prune(env, Date.now())) === 1, "the cron rates it after the wait");
  r = await ratings([21, 22]);
  check(r[id(21)].games === 2 && r[id(22)].games === 2, "silent player rated (a non-report doesn't escape the result)");
  // leaving in the first minute still loses: a result naming a leaver counts before MIN_MATCH_MS (the stayer's
  // report may get there first); one without a leaver that early is void
  const S3 = await server();
  await S3.ticket("cfcfcfcfcfcfcfcf", "vanilla", [21, 22]);
  check((await call("/report", { secret: secret(21), match: "cfcfcfcfcfcfcfcf", ladder: "vanilla", p })) === "pending 1/2", "the stayer's report, before the server's result");
  check((await S3.post("/result", { match: "cfcfcfcfcfcfcfcf", p, left: id(22) })) === "done", "an early result with a leaver counts");
  r = await ratings([21, 22]);
  check(r[id(22)].games === 3 && r[id(22)].mu < 25, "the early leaver lost");
  const S4 = await server();
  await S4.ticket("cacacacacacacaca", "vanilla", [21, 22]);
  check((await S4.result("cacacacacacacaca", p)) === "void: outside the ticket's window", "an early result without a leaver is void");
}
// nothing stays pending: once a ticket expires its match is rated (result in, someone agreed, nobody against) or void;
// the cron goes oldest first, skips rows it can't decide yet, and one failing match doesn't stop the rest
{
  const status = (m) => [db.prepare("SELECT status FROM matches WHERE id = ?").get(m)?.status, db.prepare("SELECT status FROM tickets WHERE id = ?").get(m)?.status].join("/");
  const p = `${id(23)}:0:0,${id(24)}:1:1`;
  // the server's result, but no player ever reports: void once the ticket expires
  const S1 = await server();
  await S1.ticket("c1c1c1c1c1c1c1c1", "vanilla", [23, 24]);
  age("c1c1c1c1c1c1c1c1");
  await S1.result("c1c1c1c1c1c1c1c1", p);
  db.prepare("UPDATE tickets SET resulted = resulted - ? WHERE id = ?").run(11 * 60000, "c1c1c1c1c1c1c1c1");
  check((await prune(env, Date.now())) === 0 && status("c1c1c1c1c1c1c1c1") === "pending/open", "no report yet: waits");
  // players report, the server never does
  const S2 = await server();
  await S2.ticket("c2c2c2c2c2c2c2c2", "vanilla", [23, 24]);
  age("c2c2c2c2c2c2c2c2");
  check((await call("/report", { secret: secret(23), match: "c2c2c2c2c2c2c2c2", ladder: "vanilla", p })) === "pending 1/2", "report without the server's result");
  // the server's result and one agreeing report, the result posted just before the ticket ran out
  const S3 = await server();
  await S3.ticket("c3c3c3c3c3c3c3c3", "vanilla", [23, 24]);
  age("c3c3c3c3c3c3c3c3");
  await S3.result("c3c3c3c3c3c3c3c3", p);
  check((await call("/report", { secret: secret(23), match: "c3c3c3c3c3c3c3c3", ladder: "vanilla", p })) === "pending 1/2", "one agreeing report");
  for (const m of ["c1c1c1c1c1c1c1c1", "c2c2c2c2c2c2c2c2", "c3c3c3c3c3c3c3c3"]) age(m, 2 * 3600000);
  check((await prune(env, Date.now())) === 1, "expired tickets decided: one rated");
  check(status("c1c1c1c1c1c1c1c1") === "void/void", "result nobody confirmed: void " + status("c1c1c1c1c1c1c1c1"));
  check(status("c2c2c2c2c2c2c2c2") === "void/void", "no result from the server: void, match row too " + status("c2c2c2c2c2c2c2c2"));
  check(status("c3c3c3c3c3c3c3c3") === "done/done", "agreed and unopposed: rated at expiry " + status("c3c3c3c3c3c3c3c3"));
  check((await ratings([23, 24]))[id(23)].games === 1, "rated once");
  check((await call("/report", { secret: secret(24), match: "c2c2c2c2c2c2c2c2", ladder: "vanilla", p })) === "void", "a void ticket takes no reports");
  // 60 rows that can't be decided yet (a result, no report) don't hide a match that can, made after them
  const t0 = Date.now(), ids = [];
  for (let k = 0; k < 60; k++) {
    const m = "f0" + String(k).padStart(14, "0");
    ids.push(m);
    db.prepare("INSERT INTO tickets (id, kind, server, ladder, map, players, created, status, result, leavers, resulted) VALUES (?, 'match', 'x:1', 'vanilla', 'temple', ?, ?, 'open', ?, '', ?)")
      .run(m, [id(23), id(24)].sort().join(","), t0 - 30 * 60000, "vanilla|" + p, t0 - 25 * 60000);
    db.prepare("INSERT INTO matches (id, result, players, status, created) VALUES (?, ?, 2, 'pending', ?)").run(m, "vanilla|" + p, t0 - 25 * 60000);
  }
  const S4 = await server(), S5 = await server();
  for (const [S, m] of [[S4, "c4c4c4c4c4c4c4c4"], [S5, "c5c5c5c5c5c5c5c5"]]) {
    await S.ticket(m, "vanilla", [23, 24]);
    age(m);
    await S.result(m, p);
    await call("/report", { secret: secret(23), match: m, ladder: "vanilla", p });
    db.prepare("UPDATE tickets SET resulted = resulted - ? WHERE id = ?").run(m === "c4c4c4c4c4c4c4c4" ? 12 * 60000 : 11 * 60000, m);
  }
  // the older of the two throws: the cron logs it and still rates the other
  const realPrepare = env.DB.prepare, realLog = console.log;
  env.DB.prepare = (sql) => { const s = realPrepare(sql); return { ...s, bind: (...a) => { if (a.includes("c4c4c4c4c4c4c4c4") && sql.startsWith("SELECT ladder")) throw new Error("D1 hiccup"); return s.bind(...a); } }; };
  console.log = () => {};
  const n = await prune(env, Date.now());
  env.DB.prepare = realPrepare; console.log = realLog;
  check(n === 1 && status("c5c5c5c5c5c5c5c5") === "done/done" && status("c4c4c4c4c4c4c4c4") === "pending/open", "undecidable rows skipped, one failure doesn't stop the rest " + n);
  check(ids.every((m) => status(m) === "pending/open"), "rows waiting for a report stay pending until the ticket expires");
  check((await prune(env, Date.now())) === 1 && status("c4c4c4c4c4c4c4c4") === "done/done", "the failed one is rated on the next run");
  for (const m of ids) age(m, 2 * 3600000);
  await prune(env, Date.now()); await prune(env, Date.now());
  check(ids.every((m) => status(m) === "void/void"), "expired rows all decided over the runs (50 a run)");
}
// the server's next ticket voids its last one only if that has no result: a late dispute still counts
{
  const status = (m) => [db.prepare("SELECT status FROM matches WHERE id = ?").get(m)?.status, db.prepare("SELECT status FROM tickets WHERE id = ?").get(m)?.status].join("/");
  const p = `${id(25)}:0:0,${id(26)}:1:1`, S = await server();
  await S.ticket("c6c6c6c6c6c6c6c6", "vanilla", [25, 26]);
  age("c6c6c6c6c6c6c6c6");
  await S.result("c6c6c6c6c6c6c6c6", p);
  await call("/report", { secret: secret(25), match: "c6c6c6c6c6c6c6c6", ladder: "vanilla", p });
  check((await S.ticket("c7c7c7c7c7c7c7c7", "vanilla", [25, 26])) === "ticket c7c7c7c7c7c7c7c7", "next match's ticket");
  check(status("c6c6c6c6c6c6c6c6") === "pending/open", "a match with a result isn't voided by the next ticket " + status("c6c6c6c6c6c6c6c6"));
  check((await call("/report", { secret: secret(26), match: "c6c6c6c6c6c6c6c6", ladder: "vanilla", p, dispute: "1" })) === "disputed", "its late dispute counts");
  // one without a result (a report came, the server never posted) is void, its pending match row too
  age("c7c7c7c7c7c7c7c7");
  await call("/report", { secret: secret(25), match: "c7c7c7c7c7c7c7c7", ladder: "vanilla", p });
  check((await S.ticket("c8c8c8c8c8c8c8c8", "vanilla", [25, 26])) === "ticket c8c8c8c8c8c8c8c8" && status("c7c7c7c7c7c7c7c7") === "void/void", "no result: the next ticket voids it " + status("c7c7c7c7c7c7c7c7"));
}
// ladders are separate: an instagib win doesn't touch vanilla
{
  const S = await server();
  check((await play(S, "eeeeeeeeeeeeeeee", "instagib", [[7, 0, 0], [8, 1, 1]])) === "done", "instagib match");
  const ig = await ratings([7, 8], "instagib"), va = await ratings([7, 8], "vanilla"), vp = await ratings([7], "vampire");
  check(ig[id(7)].games === 1 && ig[id(7)].mu > 25 && va[id(7)].games === 0 && va[id(7)].mu === 25 && vp[id(7)].games === 0, "ladders separate");
  check((await call("/report", { secret: secret(7), match: "ffffffffffffffff", ladder: "boomers", p: `${id(7)}:0:0,${id(8)}:1:1` })) === "not a ranked ladder", "unranked ruleset refused");
  check((await S.ticket("fefefefefefefefe", "boomers", [7, 8])) === "not a ranked ladder", "no tickets for unranked rulesets");
}
// leaderboard: names from the reports, 3+ games, and only established accounts (PROVISIONAL_GAMES ticketed matches)
const hex = (t) => [...t].map((c) => c.charCodeAt(0).toString(16).padStart(2, "0")).join("");
{
  const S = await server();
  for (let g = 0; g < 5; g++) {
    const match = "abcd" + String(g).padStart(12, "0");
    await play(S, match, "vampire", [[11, 0, 0], [12, 1, 1]], { 11: { name: hex("Top Dog") }, 12: { name: hex("under dog") } });
    db.prepare("UPDATE tickets SET created = created - 61000 WHERE server = ?").run(S.ip + ":" + S.f.port); // (next ticket allowed)
    if (g === 3) check((await call("/leaderboard?ladder=vampire")) === "", "provisional accounts aren't on the board");
  }
  const rows = (await call("/leaderboard?ladder=vampire")).split("\n").map((l) => l.split(" "));
  check(rows.length === 2 && rows[0].slice(2).join(" ") === "Top Dog" && +rows[0][0] > +rows[1][0] && rows[0][1] === "5", "leaderboard " + JSON.stringify(rows));
  check((await call("/leaderboard?ladder=boomers")) === "not a ranked ladder", "leaderboard ladder check");
  // farming fresh accounts: an established player's rating doesn't move against a provisional one
  const before = (await ratings([11], "vampire"))[id(11)];
  check((await play(S, "abcdffffffffffff", "vampire", [[11, 0, 0], [30, 1, 1]])) === "done", "vs a fresh account");
  const after = await ratings([11, 30], "vampire");
  check(after[id(11)].mu === before.mu && after[id(11)].games === 5 && after[id(30)].mu < 25 && after[id(30)].games === 1, "no farming fresh accounts " + JSON.stringify(after));
}
check((await call("/report", { secret: secret(1), match: "cccccccccccccccc", ladder: "vanilla", p: `${id(1)}:0:0` })) === "bad result", "one player refused");
check((await call("/report", { secret: secret(1), match: "dddddddddddddddd", ladder: "vanilla", p: `${id(1)}:0:0,${id(2)}:0:1` })) === "bad result", "one side refused");
// race time trials: runs come from the server under a race ticket; splits must respect the course's floors
{
  const floors = [792, 606, 485, 625, 250, 928, 1333, 341, 1818, 979, 1022, 1583];
  const splitsFor = (leg) => { let t = 0; return floors.map((f) => (t += f + leg)); };
  const S = await server();
  const name = Buffer.from("racer").toString("hex");
  check((await call("/racetime", { secret: secret(5), map: "viaduct", ms: "40000", splits: "10000,25000,40000", name })).startsWith("update the game"), "self-reported times refused (R3)");
  const run = (n, sp, ticket = "1111111111111111", extra = {}) =>
    S.post("/racerun", { ticket, map: "viaduct", id: id(n), ms: String(sp[sp.length - 1]), splits: sp.join(","), ...extra });
  check((await run(5, splitsFor(2000))) === "no ticket", "no race ticket, no run");
  // a race ticket needs the racer's signature: a server can't put someone who isn't on it on the board (or rename them)
  check((await S.post("/ticket", { kind: "race", match: "1111111111111111", map: "viaduct", players: id(5) })) === "missing signatures", "unsigned race ticket refused");
  check((await S.race("1111111111111111", 5, "viaduct", 6)) === "bad signature for " + id(5), "race ticket signed by someone else refused");
  check((await S.post("/ticket", { kind: "race", match: "1111111111111111", map: "viaduct", players: id(5), keys: key(6).pub, sigs: signTicket(6, "1111111111111111", S.addr), addrs: S.addr })) === "bad key for " + id(5), "race ticket key must hash to the racer");
  check((await S.race("1111111111111111", 5)) === "ticket 1111111111111111", "race ticket");
  check((await S.race("2222222222222222", 6)) === "ticket 2222222222222222", "second racer ticket");
  check((await S.race("3333333333333333", 6, "temple")) === "not a ranked race map", "race map only");
  age("1111111111111111", 3600000); age("2222222222222222", 3600000);
  const s40 = splitsFor(2000), s41 = splitsFor(2100), s38 = splitsFor(1800), s39 = splitsFor(1900);
  check((await run(5, s40, "1111111111111111", { name })) === "first time", "first race time");
  check((await run(5, s41, "1111111111111111", { name: Buffer.from("renamed").toString("hex") })).startsWith("not faster"), "slower time kept out");
  check(db.prepare("SELECT name FROM names WHERE id = ?").get(id(5)).name === "racer", "a run that isn't kept doesn't change the name");
  check((await run(5, s38)) === `new best (was ${s40[11]})`, "faster time replaces");
  check((await run(6, s39, "2222222222222222")) === "first time", "second racer");
  check((await run(6, s38, "1111111111111111")) === "no ticket", "a ticket is for its racer");
  const fast = [...s38]; for (let i = 8; i < 12; i++) fast[i] -= 1800 + 1000; // one leg 1 s under its floor
  check((await run(5, fast)) === "bad splits", "leg faster than the course allows");
  check((await run(5, s38.slice(0, 11))) === "bad splits", "every checkpoint");
  check((await run(5, [...s38.slice(0, 11), s38[11]], "1111111111111111", { ms: "99999" })) === "bad splits", "last split must be the time");
  const other = await server();
  check((await other.post("/racerun", { ticket: "1111111111111111", map: "viaduct", id: id(5), ms: String(s38[11]), splits: s38.join(",") })) === "no ticket", "only the ticket's server");
  await other.race("4444444444444444", 7);
  check((await other.post("/racerun", { ticket: "4444444444444444", map: "viaduct", id: id(7), ms: String(s38[11]), splits: s38.join(",") })) === "bad time", "a run can't be longer than its session");
  const board = (await call("/raceboard?map=viaduct")).split("\n");
  check(board[0] === `${s38[11]} racer` && board[1] === `${s39[11]} player` && board.length === 2, "race board " + JSON.stringify(board));
}
// Discord link (verified by a code from the bot), crowns, queues shared with the bot
{
  const hx = (t) => Buffer.from(t).toString("hex");
  env.BOT_KEY = "botkey";
  check((await call("/link", { secret: secret(5), discord: hx("racer#1"), name: hx("racer") })) === "update the game: link with /link in Discord", "unverified link refused (R4)");
  check((await call("/linkcode", { bot: "nope", discord: hx("racer#1") })) === "bad request", "only the bot makes codes");
  const c = await call("/linkcode", { bot: "botkey", discord: hx("racer#1") });
  check(/^code \d{6}$/.test(c), "bot gets a code " + c);
  const code = c.split(" ")[1], wrong = String((+code + 1) % 1000000).padStart(6, "0");
  check((await call("/link", { secret: secret(5), code: wrong })) === "wrong or expired code", "wrong code");
  check((await call("/link", { secret: secret(5), code, name: hx("racer") })) === "linked racer#1", "link with the code");
  check((await call("/link", { secret: secret(6), code })) === "wrong or expired code", "a code works once");
  db.prepare("INSERT INTO links (id, discord, updated) VALUES (?, ?, 0)").run(id(6), "victim"); // an old, unverified link
  const cr = (await call("/crowns")).split("\n").filter(Boolean);
  check(cr.some((l) => l === `race:viaduct 1 ${id(5)} ${hx("racer")} ${hx("racer#1")}`), "crowns: race leader with discord " + JSON.stringify(cr));
  check(cr.some((l) => l.startsWith(`race:viaduct 2 ${id(6)} `) && l.endsWith(" -")), "crowns: unverified links get no role");
  check(cr.some((l) => l.startsWith(`vampire 1 ${id(11)} `)) && !cr.some((l) => l.includes(id(30))), "crowns: established players only");
  let limited = false;
  for (let i = 0; i < 10 && !limited; i++) limited = (await call("/link", { secret: secret(70 + i), code: wrong }, "192.0.2.77")) === "rate limited";
  check(limited, "code guessing rate-limited per IP");
  check((await call("/queue", { secret: secret(5), mode: "ranked-vampire", name: hx("racer") })) === "queued ranked-vampire 1/2", "game joins a queue");
  check((await call("/queue", { secret: secret(5), mode: "ffa" })) === "queued ffa 1/4", "and another");
  check((await call("/queue")).split("\n").sort().join("|") === "ffa 1 4|ranked-vampire 1 2", "queue counts");
  check((await call("/queue", { bot: "nope", discord: hx("dima"), mode: "ranked-vampire" })) === "bad request", "only the bot adds discord users");
  check((await call("/queue", { bot: "botkey", discord: hx("dima"), mode: "ranked-vampire" })) === "filled ranked-vampire", "bot fills it");
  const filled = (await call("/filled?since=0")).split("\n").filter(Boolean);
  check(filled.length === 1 && filled[0].includes(`${id(5)}:${hx("racer")}:${hx("racer#1")}`) && filled[0].includes(`d:dima:${hx("dima")}:${hx("dima")}`), "filled row " + JSON.stringify(filled));
  check((await call("/queue")) === "", "filled players leave every queue");
  check((await call("/filled?since=" + filled[0].split(" ")[0])) === "", "nothing newer");
  check((await call("/queue", { secret: secret(6), mode: "nope" })) === "not a queue", "unknown queue");
  check((await call("/queue", { secret: secret(6), mode: "ffa" })) === "queued ffa 1/4" && db.prepare("SELECT discord FROM queue WHERE who = ?").get(id(6)).discord === "", "unverified links aren't pinged");
  check((await call("/link", { secret: secret(5), unlink: "1" })) === "unlinked", "unlink");
}
// server list: POST only, own source IPv4 only, ip= needs the token from an earlier announce, a few per address
{
  const ann = (form, from) => call("/announce", form, from);
  check((await fetchRaw("/announce?port=27500&v=t1", null, "198.51.100.7")).status === 405, "writes are POST only (WEB-5)");
  check((await fetchRaw("/announce", { port: "27500", v: "t1" }, "198.51.100.7", { Origin: "https://evil.example" })).status === 403, "no cross-site writes");
  const wr = await fetchRaw("/announce", { port: "27500", v: "t1" }, "198.51.100.7");
  const a = await wr.text();
  check(/^ok [0-9a-f]{32}$/.test(a), "announce gives a token " + a);
  check(!wr.headers.get("access-control-allow-origin"), "no CORS on writes");
  const tok = a.split(" ")[1];
  const lr = await fetchRaw("/list?v=t1");
  check((await lr.text()) === "198.51.100.7:27500" && lr.headers.get("access-control-allow-origin") === "*", "listed (reads keep CORS)");
  check((await ann({ port: "27501", v: "t1", ip: "203.0.113.9" }, "2001:db8::99")).startsWith("forbidden"), "ipv6 can't list another address");
  check((await call("/remove", { port: "27500", ip: "198.51.100.7" }, "2001:db8::99")).startsWith("forbidden"), "ipv6 can't remove without token");
  await call("/remove", { port: "27500", ip: "198.51.100.7" }, "192.0.2.1"); // an IPv4 caller only ever names itself
  check((await call("/list?v=t1")) === "198.51.100.7:27500", "other ipv4 can't remove");
  check((await ann({ port: "27500", v: "t1", ip: "198.51.100.7", token: tok }, "2001:db8::5")) === "ok " + tok, "token announce over ipv6");
  check((await call("/remove", { port: "27500", ip: "198.51.100.7", token: tok }, "2001:db8::5")) === "removed", "token remove");
  check((await call("/list?v=t1")) === "", "removed from list");
  let last = "";
  for (let i = 0; i < 9; i++) last = await ann({ port: String(28000 + i), v: "t2" }, "198.51.100.8");
  check(last === "too many servers from this address", "per-address cap " + last);
  check((await call("/list?v=t2")).split("\n").length === 8, "one address can't flood the list");
  let limited = false;
  for (let i = 0; i < 100 && !limited; i++) limited = (await call("/link", { secret: secret(140 + i) }, "192.0.2.50")) === "rate limited";
  check(limited, "writes rate-limited per IP");
}
// rate limits with many calls from one address: the Discord bot, and players behind one NAT
{
  const hx = (t) => Buffer.from(t).toString("hex");
  env.BOT_KEY = "botkey";
  const ip = "192.0.2.60";
  // two "Queue for all" presses and more (16 POSTs each): the bot has its own bucket
  const replies = [];
  for (let i = 0; i < 70; i++) replies.push(await call("/queue", { bot: "botkey", discord: hx("user" + (i % 16)), mode: "vs", action: "leave" }, ip));
  check(replies.every((r) => r === "left vs"), "the bot's burst isn't rate limited " + replies.filter((r) => r !== "left vs").length);
  check((await call("/hello", { secret: secret(100) }, ip)) === "id " + id(100), "the bot didn't spend its address's write bucket");
  // a wrong BOT_KEY gets no bot bucket: the address's own limits apply
  let limited = false;
  for (let i = 0; i < 100 && !limited; i++) limited = (await call("/queue", { bot: "nope", discord: hx("x"), mode: "vs", action: "leave" }, "192.0.2.61")) === "rate limited";
  check(limited, "a wrong bot key is limited like anyone");
  // 16 players behind one address end a match together: everyone's /hello, /lifetime and /queue go through
  const nat = "192.0.2.62", out = [];
  for (let n = 100; n < 116; n++) {
    out.push(await call("/hello", { secret: secret(n) }, nat));
    out.push(await call("/lifetime", { secret: secret(n), kills: "1", deaths: "1" }, nat));
    out.push(await call("/queue", { secret: secret(n), mode: "vs", action: "leave" }, nat));
  }
  check(!out.includes("rate limited"), "players sharing an address aren't limited by each other " + out.filter((r) => r === "rate limited").length);
  // ...but one player hammering is still stopped by the per-id limit, without locking out their neighbours
  let mine = "";
  for (let i = 0; i < 12 && mine !== "rate limited"; i++) mine = await call("/queue", { secret: secret(116), mode: "vs", action: "leave" }, nat);
  check(mine === "rate limited", "per-id limit still holds behind a shared address");
  check((await call("/queue", { secret: secret(117), mode: "vs", action: "leave" }, nat)) === "left vs", "a neighbour still gets through");
}
// pre-1.17 ids move to the key-derived id the first time the secret shows up
{
  const n = 77, old = legacy(n);
  db.prepare("INSERT INTO players (id, mu, sigma, games, updated) VALUES (?, 30, 5, 9, 0)").run("vanilla:" + old);
  db.prepare("INSERT INTO accounts (id, tgames, created) VALUES (?, 9, 0)").run(old);
  db.prepare("INSERT INTO names (id, name, updated) VALUES (?, 'veteran', 0)").run(old);
  db.prepare("INSERT INTO race_times (map, id, ms, splits, version, created) VALUES ('viaduct', ?, 50000, '1', 'x', 0)").run(old);
  check((await call("/hello", { secret: secret(n) })) === "hello".replace("hello", "id " + id(n)), "hello answers the new id");
  const r = await ratings([n]);
  check(r[id(n)].mu === 30 && r[id(n)].games === 9, "ratings moved to the new id");
  check(db.prepare("SELECT COUNT(*) AS c FROM players WHERE id = ?").get("vanilla:" + old).c === 0 && db.prepare("SELECT tgames FROM accounts WHERE id = ?").get(id(n)).tgames === 9, "old rows gone");
  check(db.prepare("SELECT ms FROM race_times WHERE id = ?").get(id(n)).ms === 50000, "race time moved");
  check((await call("/hello", { secret: secret(n) })) === "id " + id(n), "once");
}
// REVIEW 3.6: the new id already has rows (a match rated before the secret reached the Worker): merged, not dropped
{
  const n = 78, old = legacy(n), q = (sql, ...a) => db.prepare(sql).run(...a);
  q("INSERT INTO players (id, mu, sigma, games, updated, ver) VALUES (?, 31, 4, 40, 100, 7)", "vanilla:" + old);   // the veteran's rating
  q("INSERT INTO players (id, mu, sigma, games, updated, ver) VALUES (?, 27, 8, 1, 200, 1)", "vanilla:" + id(n));   // one match since, as the new id
  q("INSERT INTO players (id, mu, sigma, games, updated, ver) VALUES (?, 20, 7, 2, 50, 3)", "vampire:" + old);
  q("INSERT INTO players (id, mu, sigma, games, updated, ver) VALUES (?, 26, 6, 3, 300, 2)", "vampire:" + id(n));  // more games on the new id: it stays
  q("INSERT INTO players (id, mu, sigma, games, updated, ver) VALUES (?, 22, 6, 4, 60, 0)", "instagib:" + old);    // only the old one: moves
  q("INSERT INTO accounts (id, tgames, created) VALUES (?, 40, 1000)", old);
  q("INSERT INTO accounts (id, tgames, created) VALUES (?, 2, 5000)", id(n));
  q("INSERT INTO names (id, name, updated) VALUES (?, 'old name', 0)", old);
  q("INSERT INTO names (id, name, updated) VALUES (?, 'new name', 9)", id(n));
  q("INSERT INTO race_times (map, id, ms, splits, version, created) VALUES ('viaduct', ?, 50000, '1', 'x', 0)", old);
  q("INSERT INTO race_times (map, id, ms, splits, version, created) VALUES ('viaduct', ?, 52000, '1', 'y', 9)", id(n));
  check((await call("/hello", { secret: secret(n) })) === "id " + id(n), "hello with rows on both ids");
  const row = (ladder) => db.prepare("SELECT mu, sigma, games, updated, ver FROM players WHERE id = ?").get(ladder + ":" + id(n));
  const v = row("vanilla"), vp = row("vampire"), ig = row("instagib");
  check(v.mu === 31 && v.sigma === 4 && v.games === 41 && v.updated === 200 && v.ver === 8, "the row with more games keeps its rating, games add up " + JSON.stringify(v));
  check(vp.mu === 26 && vp.sigma === 6 && vp.games === 5, "the new id's rating stays when it has more games " + JSON.stringify(vp));
  check(ig.mu === 22 && ig.games === 4, "a ladder only the old id played moves " + JSON.stringify(ig));
  check(db.prepare("SELECT COUNT(*) AS c FROM players WHERE id LIKE ?").get("%:" + old).c === 0, "no old ladder rows left");
  const acc = db.prepare("SELECT tgames, created FROM accounts WHERE id = ?").get(id(n));
  check(acc.tgames === 40 && acc.created === 1000 && !db.prepare("SELECT 1 FROM accounts WHERE id = ?").get(old), "account: most ticketed games, earliest start " + JSON.stringify(acc));
  check(db.prepare("SELECT name FROM names WHERE id = ?").get(id(n)).name === "new name" && !db.prepare("SELECT 1 FROM names WHERE id = ?").get(old), "the newer name stays");
  check(db.prepare("SELECT ms FROM race_times WHERE id = ?").get(id(n)).ms === 50000 && !db.prepare("SELECT 1 FROM race_times WHERE id = ?").get(old), "the faster race time");
  check((await call("/hello", { secret: secret(n) })) === "id " + id(n) && row("vanilla").games === 41, "merged once");
}
// queue fills are atomic: two joins racing for the last place fill it once
{
  const hx = (t) => Buffer.from(t).toString("hex");
  await call("/queue", { secret: secret(80), mode: "duel", name: hx("q1") });
  const both = await Promise.all([call("/queue", { secret: secret(81), mode: "duel" }), call("/queue", { secret: secret(82), mode: "duel" })]);
  const fills = db.prepare("SELECT COUNT(*) AS c FROM filled WHERE mode = 'duel'").get().c;
  check(fills === 1 && both.filter((x) => x === "filled duel").length === 1, "one fill " + JSON.stringify(both));
  check(db.prepare("SELECT COUNT(*) AS c FROM queue WHERE mode = 'duel'").get().c === 1, "the third waits");
}
// ratings: a write against a changed rating is refused as a whole (the version guard), then retried
{
  const S = await server();
  const p = `${id(31)}:0:0,${id(32)}:1:1`;
  await S.ticket("dededededededede", "instagib", [31, 32]);
  age("dededededededede");
  await S.result("dededededededede", p);
  await call("/report", { secret: secret(31), match: "dededededededede", ladder: "instagib", p });
  // someone else's match moves player 32 between this one's read and write: simulated by a trigger-free bump
  const realBatch = env.DB.batch;
  let bumped = false;
  env.DB.batch = async (list) => {
    if (!bumped) { bumped = true; db.prepare("INSERT INTO players (id, mu, sigma, games, updated, ver) VALUES (?, 20, 8, 1, 0, 1)").run("instagib:" + id(32)); }
    return realBatch(list);
  };
  check((await call("/report", { secret: secret(32), match: "dededededededede", ladder: "instagib", p })) === "done", "rated after a retry");
  env.DB.batch = realBatch;
  const r = await ratings([31, 32], "instagib");
  check(r[id(32)].games === 2 && r[id(32)].mu < 20 && r[id(31)].games === 1, "the retry saw the other match's rating " + JSON.stringify(r));
}
// lifetime kills / deaths: added up across reports, capped, rate limited
{
  check((await call("/lifetime", { secret: secret(7), kills: "12", deaths: "5", name: Buffer.from("vet").toString("hex") })) === "ok", "lifetime first report");
  check((await call("/lifetime", { secret: secret(7), kills: "3", deaths: "1" })) === "too soon", "lifetime rate limit");
  check((await call("/lifetime", { secret: secret(8), kills: "999", deaths: "0" })) === "bad numbers", "lifetime cap");
  db.prepare("UPDATE lifetime SET updated = 0").run();
  check((await call("/lifetime", { secret: secret(7), kills: "3", deaths: "1" })) === "ok", "lifetime adds up");
  check((await call("/lifetime?ids=" + id(7) + "," + id(8))) === `${id(7)} 15 6`, "lifetime lookup");
  db.prepare("UPDATE lifetime SET updated = 0, hour_kills = 1100 WHERE id = ?").run(id(7));
  check((await call("/lifetime", { secret: secret(7), kills: "200", deaths: "0" })) === "hourly limit", "lifetime hourly cap");
  check((await call("/lifetime")).startsWith(`15 6 ${id(7)} `), "lifetime top");
}
console.log(fails ? fails + " FAILURE(S)" : "worker tests passed");
process.exit(fails ? 1 : 0);
