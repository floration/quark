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
const ratings = async (ns) => Object.fromEntries((await call("/ratings?ids=" + ns.map(id).join(","))).split("\n")
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
  check((await call("/report", { secret: secret(1), match: "aaaaaaaaaaaaaaaa", mode: "duel", p })) === "pending 1/4", "first report pending");
  check((await call("/report", { secret: secret(1), match: "aaaaaaaaaaaaaaaa", mode: "duel", p })) === "pending 1/4", "duplicate report ignored");
  check((await call("/report", { secret: secret(9), match: "aaaaaaaaaaaaaaaa", mode: "duel", p })) === "not in this match", "outsider refused");
  await call("/report", { secret: secret(2), match: "aaaaaaaaaaaaaaaa", mode: "duel", p });
  await call("/report", { secret: secret(3), match: "aaaaaaaaaaaaaaaa", mode: "duel", p });
  let r = await ratings([1, 3]);
  check(r[id(1)].games === 0, "not rated before everyone reported");
  check((await call("/report", { secret: secret(4), match: "aaaaaaaaaaaaaaaa", mode: "duel", p })) === "done", "last report rates");
  r = await ratings([1, 2, 3, 4]);
  check(r[id(1)].mu > 25 && r[id(2)].mu > 25 && r[id(3)].mu < 25 && r[id(1)].games === 1, "2v2 rated");
  check((await call("/report", { secret: secret(4), match: "aaaaaaaaaaaaaaaa", mode: "duel", p })) === "done", "rated only once");
  r = await ratings([1]);
  check(r[id(1)].games === 1, "still one game");
}
// a disagreeing report voids the match
{
  const won = `${id(5)}:0:0,${id(6)}:1:1`, lost = `${id(5)}:0:1,${id(6)}:1:0`;
  await call("/report", { secret: secret(5), match: "bbbbbbbbbbbbbbbb", mode: "duel", p: won });
  check((await call("/report", { secret: secret(6), match: "bbbbbbbbbbbbbbbb", mode: "duel", p: lost })).startsWith("void"), "disagreement voids");
  check((await call("/report", { secret: secret(6), match: "bbbbbbbbbbbbbbbb", mode: "duel", p: won })) === "void", "stays void");
  const r = await ratings([5, 6]);
  check(r[id(5)].games === 0 && r[id(6)].games === 0, "void match not rated");
}
check((await call("/report", { secret: secret(1), match: "cccccccccccccccc", mode: "duel", p: `${id(1)}:0:0` })) === "bad result", "one player refused");
check((await call("/report", { secret: secret(1), match: "dddddddddddddddd", mode: "duel", p: `${id(1)}:0:0,${id(2)}:0:1` })) === "bad result", "one side refused");
console.log(fails ? fails + " FAILURE(S)" : "worker tests passed");
process.exit(fails ? 1 : 0);
