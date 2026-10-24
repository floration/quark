// The decoration engine in a Web Worker: apply / remove / preview run here, so Regenerate all on Midtown (about 2 s)
// doesn't freeze the page. Loads the same engine files the page does (deco.js, deco_rules.js: plain scripts that hang
// everything off globalThis.MapDeco). Not a deco*.js name on purpose: the page loads every js/deco*.js as a module.
/* global importScripts */
importScripts("deco.js", "deco_rules.js");
const OPS = new Set(["apply", "remove", "preview"]);
self.onmessage = (e) => {
  const { id, op, args } = e.data || {};
  try {
    if (!OPS.has(op)) throw new Error("unknown operation " + op);
    self.postMessage({ id, result: self.MapDeco[op](...args) });
  } catch (err) {
    self.postMessage({ id, error: String((err && err.stack) || err) });
  }
};
