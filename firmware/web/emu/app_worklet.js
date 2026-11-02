// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's part of the browser emulator's worklet (fm1-base's web/emu/worklet.js loads it first): twice a second,
// the FM-1's load as the engine models it (firmware/web.c web_fm1_load) to the page, for the line under the panel.
globalThis.fm1App = {
  n: 0,
  process(fm1) {
    if (++this.n % 172) return;                       // (128-frame quanta: ~0.5 s)
    const ex = fm1.ex;
    if (!ex.web_fm1_load) return;
    fm1.port.postMessage({ type: "app", fm1: [ex.web_fm1_load(0), ex.web_fm1_load(1), ex.web_fm1_load(2)] });
  },
};
