// Quark UI redesign: the title logo and the Rubik font.
//
//   module.exports = function applyLogo(QModel, doc) -> doc
//
// (From the 1.18 redesign proposal's transforms; the game has drawn its title this way since 1.18.13.) Sets style "logo" on every
// QUARK title (a label whose text is "QUARK", any case, on any page: the game's ui_.labelCentered(cx, y, "QUARK", ...))
// and the layout's font to Rubik (doc.font, saved with the layout; IMPORT LAYOUT switches the customizer to it).
//
// Weight: body text Rubik 700 (Bold). The logo itself is always Rubik 800 (ExtraBold, as tools/make_icon.py draws the
// icon; render.js LOGO), whatever the body weight. 800 for body text closes the counters of e, a, s at the game's
// text scale 2 (a 15 px em at 720p) once the black outline is added; 700 keeps them open and still reads as the same
// family as the logo.
'use strict';

const BODY_WEIGHT = 700;

module.exports = function applyLogo(QModel, doc) {
  let n = 0;
  for (const p of doc.pages || []) {
    for (const e of p.elements || []) {
      if (e && e.kind === 'label' && String(e.label || '').trim().toUpperCase() === 'QUARK') { e.style = 'logo'; n++; }
    }
  }
  QModel.setLayoutFont(doc, 'rubik', BODY_WEIGHT);
  applyLogo.lastCount = n;
  return doc;
};
const applyLogo = module.exports;
applyLogo.BODY_WEIGHT = BODY_WEIGHT;
