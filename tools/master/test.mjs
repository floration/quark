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
const call = async (path, form) => {
  const init = form ? { method: "POST", body: new URLSearchParams(form) } : {};
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

// reporting: 2v2, everyone must agree
{
  const p = `${id(1)}:0:0,${id(2)}:0:0,${id(3)}:1:1,${id(4)}:1:1`;
  check((await call("/report", { secret: secret(1), match: "aaaaaaaaaaaaaaaa", ladder: "vanilla", p })) === "pending 1/4", "first report pending");
  check((await call("/report", { secret: secret(1), match: "aaaaaaaaaaaaaaaa", ladder: "vanilla", p })) === "pending 1/4", "duplicate report ignored");
  check((await call("/report", { secret: secret(9), match: "aaaaaaaaaaaaaaaa", ladder: "vanilla", p })) === "not in this match", "outsider refused");
  await call("/report", { secret: secret(2), match: "aaaaaaaaaaaaaaaa", ladder: "vanilla", p });
  await call("/report", { secret: secret(3), match: "aaaaaaaaaaaaaaaa", ladder: "vanilla", p });
  let r = await ratings([1, 3]);
  check(r[id(1)].games === 0, "not rated before everyone reported");
  check((await call("/report", { secret: secret(4), match: "aaaaaaaaaaaaaaaa", ladder: "vanilla", p })) === "done", "last report rates");
  r = await ratings([1, 2, 3, 4]);
  check(r[id(1)].mu > 25 && r[id(2)].mu > 25 && r[id(3)].mu < 25 && r[id(1)].games === 1, "2v2 rated");
  check((await call("/report", { secret: secret(4), match: "aaaaaaaaaaaaaaaa", ladder: "vanilla", p })) === "done", "rated only once");
  r = await ratings([1]);
  check(r[id(1)].games === 1, "still one game");
}
// a disagreeing report voids the match
{
  const won = `${id(5)}:0:0,${id(6)}:1:1`, lost = `${id(5)}:0:1,${id(6)}:1:0`;
  await call("/report", { secret: secret(5), match: "bbbbbbbbbbbbbbbb", ladder: "vanilla", p: won });
  check((await call("/report", { secret: secret(6), match: "bbbbbbbbbbbbbbbb", ladder: "vanilla", p: lost })).startsWith("void"), "disagreement voids");
  check((await call("/report", { secret: secret(6), match: "bbbbbbbbbbbbbbbb", ladder: "vanilla", p: won })) === "void", "stays void");
  const r = await ratings([5, 6]);
  check(r[id(5)].games === 0 && r[id(6)].games === 0, "void match not rated");
}
// ladders are separate: an instagib win doesn't touch vanilla
{
  const p = `${id(7)}:0:0,${id(8)}:1:1`;
  await call("/report", { secret: secret(7), match: "eeeeeeeeeeeeeeee", ladder: "instagib", p });
  await call("/report", { secret: secret(8), match: "eeeeeeeeeeeeeeee", ladder: "instagib", p });
  const ig = await ratings([7, 8], "instagib"), va = await ratings([7, 8], "vanilla"), vp = await ratings([7], "vampire");
  check(ig[id(7)].games === 1 && ig[id(7)].mu > 25 && va[id(7)].games === 0 && va[id(7)].mu === 25 && vp[id(7)].games === 0, "ladders separate");
  check((await call("/report", { secret: secret(7), match: "ffffffffffffffff", ladder: "boomers", p })) === "not a ranked ladder", "unranked ruleset refused");
}
// leaderboard: names from the reports, 3+ games, best first
{
  const hex = (t) => [...t].map((c) => c.charCodeAt(0).toString(16).padStart(2, "0")).join("");
  for (let g = 0; g < 3; g++) {
    const p = `${id(11)}:0:0,${id(12)}:1:1`, match = "abcd" + String(g).padStart(12, "0");
    await call("/report", { secret: secret(11), match, ladder: "vampire", p, name: hex("Top Dog") });
    await call("/report", { secret: secret(12), match, ladder: "vampire", p, name: hex("under dog") });
  }
  const rows = (await call("/leaderboard?ladder=vampire")).split("\n").map((l) => l.split(" "));
  check(rows.length === 2 && rows[0].slice(2).join(" ") === "Top Dog" && +rows[0][0] > +rows[1][0] && rows[0][1] === "3", "leaderboard " + JSON.stringify(rows));
  check((await call("/leaderboard?ladder=boomers")) === "not a ranked ladder", "leaderboard ladder check");
}
check((await call("/report", { secret: secret(1), match: "cccccccccccccccc", ladder: "vanilla", p: `${id(1)}:0:0` })) === "bad result", "one player refused");
check((await call("/report", { secret: secret(1), match: "dddddddddddddddd", ladder: "vanilla", p: `${id(1)}:0:0,${id(2)}:0:1` })) === "bad result", "one side refused");
// race time trials: only a faster run replaces your best; the board is fastest first
{
  const name = Buffer.from("racer").toString("hex");
  check((await call("/racetime", { secret: secret(5), map: "viaduct", ms: "40000", splits: "10000,25000,40000", name })) === "first time", "first race time");
  check((await call("/racetime", { secret: secret(5), map: "viaduct", ms: "41000", splits: "10000,25000,41000" })).startsWith("not faster"), "slower time kept out");
  check((await call("/racetime", { secret: secret(5), map: "viaduct", ms: "38000", splits: "9000,24000,38000" })) === "new best (was 40000)", "faster time replaces");
  check((await call("/racetime", { secret: secret(6), map: "viaduct", ms: "39000", splits: "9000,39000" })) === "first time", "second racer");
  check((await call("/racetime", { secret: secret(6), map: "viaduct", ms: "30000", splits: "9000,29000" })) === "bad splits", "last split must be the time");
  check((await call("/racetime", { secret: secret(6), map: "viaduct", ms: "30000", splits: "9000,8000,30000" })) === "bad splits", "splits must increase");
  check((await call("/racetime", { secret: secret(6), map: "viaduct", ms: "900", splits: "900" })) === "bad time", "impossible time");
  check((await call("/racetime", { secret: secret(6), map: "temple", ms: "30000", splits: "30000" })) === "not a ranked race map", "race map only");
  const board = (await call("/raceboard?map=viaduct")).split("\n");
  check(board[0] === "38000 racer" && board[1] === "39000 player" && board.length === 2, "race board " + JSON.stringify(board));
}
console.log(fails ? fails + " FAILURE(S)" : "worker tests passed");
process.exit(fails ? 1 : 0);
