// Runs the worker against an in-memory SQLite with a tiny D1 lookalike: node tools/master/test.mjs
import { DatabaseSync } from "node:sqlite";
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import worker, { rateTeams } from "./worker.js";

const db = new DatabaseSync(":memory:");
db.exec(readFileSync(new URL("./schema.sql", import.meta.url), "utf8"));
const stmt = (sql, args = []) => ({
  bind: (...a) => stmt(sql, a),
  run: async () => ({ meta: { changes: Number(db.prepare(sql).run(...args).changes) } }),
  first: async () => db.prepare(sql).get(...args) ?? null,
  all: async () => ({ results: db.prepare(sql).all(...args) }),
});
const env = { DB: { prepare: (sql) => stmt(sql), batch: async (list) => Promise.all(list.map((s) => s.run())) } };

let fails = 0;
const check = (ok, what) => { if (!ok) { fails++; console.log("FAIL", what); } };
const secret = (n) => createHash("sha256").update("s" + n).digest("hex");
const id = (n) => createHash("sha256").update(secret(n)).digest("hex").slice(0, 32);
let callN = 0; // each call from its own (IPv6) client so per-IP rate limits don't trip; announce tests pass `from`
const call = async (path, form, from) => {
  const headers = { "CF-Connecting-IP": from || "2001:db8::" + (++callN).toString(16) };
  const init = form ? { method: "POST", body: new URLSearchParams(form), headers } : { headers };
  return (await worker.fetch(new Request("https://x" + path, init), env)).text();
};
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
  const tok = (await call(`/announce?port=${port}&v=t9&hidden=1`, null, ip)).split(" ")[1];
  const S = { ip, f: { port: String(port), token: tok } };
  S.post = (path, form) => call(path, { ...S.f, ...form }, S.ip);
  S.ticket = (match, ladder, ns, map = "temple") => S.post("/ticket", { kind: "match", match, ladder, map, players: ns.map(id).join(",") });
  S.result = (match, p) => S.post("/result", { match, p });
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
  check((await S.ticket(match, "vanilla", [1, 2, 3, 4])) === "ticket " + match, "ticket opened");
  check((await S.ticket("abababababababab", "vanilla", [1, 2])) === "too soon", "one ticket a minute per server");
  check((await call("/report", { secret: secret(1), match, ladder: "vanilla", p })) === "too soon", "no result before the match could have been played");
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
// a disagreeing report voids the match; so does a server result that disagrees with the players
{
  const S = await server();
  const won = `${id(5)}:0:0,${id(6)}:1:1`, lost = `${id(5)}:0:1,${id(6)}:1:0`;
  await S.ticket("bbbbbbbbbbbbbbbb", "vanilla", [5, 6]);
  age("bbbbbbbbbbbbbbbb");
  await call("/report", { secret: secret(5), match: "bbbbbbbbbbbbbbbb", ladder: "vanilla", p: won });
  check((await call("/report", { secret: secret(6), match: "bbbbbbbbbbbbbbbb", ladder: "vanilla", p: lost })).startsWith("void"), "disagreement voids");
  check((await call("/report", { secret: secret(6), match: "bbbbbbbbbbbbbbbb", ladder: "vanilla", p: won })) === "void", "stays void");
  const S2 = await server();
  await S2.ticket("bcbcbcbcbcbcbcbc", "vanilla", [5, 6]);
  age("bcbcbcbcbcbcbcbc");
  await S2.result("bcbcbcbcbcbcbcbc", lost);   // a lying host
  await call("/report", { secret: secret(5), match: "bcbcbcbcbcbcbcbc", ladder: "vanilla", p: won });
  check((await call("/report", { secret: secret(6), match: "bcbcbcbcbcbcbcbc", ladder: "vanilla", p: won })).startsWith("void"), "players vs server disagree: void");
  const r = await ratings([5, 6]);
  check(r[id(5)].games === 0 && r[id(6)].games === 0, "void match not rated");
  const S3 = await server();
  await S3.ticket("bdbdbdbdbdbdbdbd", "vanilla", [5, 6]);
  age("bdbdbdbdbdbdbdbd", 3 * 3600000);
  check((await call("/report", { secret: secret(5), match: "bdbdbdbdbdbdbdbd", ladder: "vanilla", p: won })) === "expired", "ticket window closes");
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
  check((await S.post("/ticket", { kind: "race", match: "1111111111111111", map: "viaduct", players: id(5) })) === "ticket 1111111111111111", "race ticket");
  check((await S.post("/ticket", { kind: "race", match: "2222222222222222", map: "viaduct", players: id(6) })) === "ticket 2222222222222222", "second racer ticket");
  check((await S.post("/ticket", { kind: "race", match: "3333333333333333", map: "temple", players: id(6) })) === "not a ranked race map", "race map only");
  age("1111111111111111", 3600000); age("2222222222222222", 3600000);
  const s40 = splitsFor(2000), s41 = splitsFor(2100), s38 = splitsFor(1800), s39 = splitsFor(1900);
  check((await run(5, s40, "1111111111111111", { name })) === "first time", "first race time");
  check((await run(5, s41)).startsWith("not faster"), "slower time kept out");
  check((await run(5, s38)) === `new best (was ${s40[11]})`, "faster time replaces");
  check((await run(6, s39, "2222222222222222")) === "first time", "second racer");
  check((await run(6, s38, "1111111111111111")) === "no ticket", "a ticket is for its racer");
  const fast = [...s38]; for (let i = 8; i < 12; i++) fast[i] -= 1800 + 1000; // one leg 1 s under its floor
  check((await run(5, fast)) === "bad splits", "leg faster than the course allows");
  check((await run(5, s38.slice(0, 11))) === "bad splits", "every checkpoint");
  check((await run(5, [...s38.slice(0, 11), s38[11]], "1111111111111111", { ms: "99999" })) === "bad splits", "last split must be the time");
  const other = await server();
  check((await other.post("/racerun", { ticket: "1111111111111111", map: "viaduct", id: id(5), ms: String(s38[11]), splits: s38.join(",") })) === "no ticket", "only the ticket's server");
  await other.post("/ticket", { kind: "race", match: "4444444444444444", map: "viaduct", players: id(7) });
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
// server list: own source IPv4 only, ?ip= needs the token from an earlier announce
{
  const a = await call("/announce?port=27500&v=t1", null, "198.51.100.7");
  check(/^ok [0-9a-f]{32}$/.test(a), "announce gives a token " + a);
  const tok = a.split(" ")[1];
  check((await call("/list?v=t1")) === "198.51.100.7:27500", "listed");
  check((await call("/announce?port=27501&v=t1&ip=203.0.113.9", null, "2001:db8::99")).startsWith("forbidden"), "ipv6 can't list another address");
  check((await call("/remove?port=27500&ip=198.51.100.7", null, "2001:db8::99")).startsWith("forbidden"), "ipv6 can't remove without token");
  await call("/remove?port=27500&ip=198.51.100.7", null, "192.0.2.1"); // an IPv4 caller only ever names itself
  check((await call("/list?v=t1")) === "198.51.100.7:27500", "other ipv4 can't remove");
  check((await call(`/announce?port=27500&v=t1&ip=198.51.100.7&token=${tok}`, null, "2001:db8::5")) === "ok " + tok, "token announce over ipv6");
  check((await call(`/remove?port=27500&ip=198.51.100.7&token=${tok}`, null, "2001:db8::5")) === "removed", "token remove");
  check((await call("/list?v=t1")) === "", "removed from list");
  let limited = false;
  for (let i = 0; i < 40 && !limited; i++) limited = (await call("/link", { secret: secret(40 + i) }, "192.0.2.50")) === "rate limited";
  check(limited, "writes rate-limited per IP");
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
