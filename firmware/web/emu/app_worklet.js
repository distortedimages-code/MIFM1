// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's part of the browser emulator's worklet (fm1-base's web/emu/worklet.js loads it first): twice a second,
// the FM-1's load as the engine models it (firmware/web.c web_fm1_load) to the page, for its gauges; the page's
// "Essayer" (a model or a filter on the track playing: web_try).
globalThis.fm1App = {
  n: 0,
  process(fm1) {
    if (++this.n % 172) return;                       // (128-frame quanta: ~0.5 s)
    const ex = fm1.ex;
    if (!ex.web_fm1_load) return;
    fm1.port.postMessage({ type: "app", fm1: [ex.web_fm1_load(0), ex.web_fm1_load(1), ex.web_fm1_load(2)] });
  },
  message(fm1, m) {
    if (fm1.ex.web_try && m.kind !== undefined) fm1.ex.web_try(m.kind >>> 0, m.idx >>> 0);
  },
};

// (0.16.25) the page's examples ("the lab"): a second copy of app.wasm, its own memory, never booted: the engine alone
// (firmware/web.c lab_*), a small project an example, up to four knobs; one example at a time, the page's to choose
class Mifm1Lab extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.port.onmessage = (e) => this.message(e.data);
  }
  text(p) {                                           // a C string in its memory
    const b = new Uint8Array(this.ex.memory.buffer, p, 32);
    let s = "";
    for (let i = 0; i < 32 && b[i]; i++) s += String.fromCharCode(b[i]);
    return s;
  }
  async message(m) {
    if (m.op === "load") {
      const stub = new Proxy({ clock_time_get: () => 0, fd_write: () => 0, proc_exit: () => {} },
                             { get: (t, k) => (k in t ? t[k] : () => 0) });
      const env = new Proxy({}, { get: () => () => 0 });
      const { instance } = await WebAssembly.instantiate(m.wasm, { wasi_snapshot_preview1: stub, env });
      this.ex = instance.exports;
      if (this.ex._initialize) this.ex._initialize();
      this.port.postMessage({ op: "ready" });
    } else if (!this.ex) {
      return;
    } else if (m.op === "setup") {                    // an example: its knobs, where they are, their texts
      const n = this.ex.lab_setup(m.id >>> 0);
      const knobs = [];
      for (let i = 0; i < n; i++) {
        const name = this.text(this.ex.lab_name(i));
        const u = this.ex.lab_get(i);
        knobs.push({ name, u, text: this.text(this.ex.lab_set(i, u)) });
      }
      this.port.postMessage({ op: "setup", id: m.id, knobs });
    } else if (m.op === "knob") {
      this.port.postMessage({ op: "text", id: m.id, i: m.i, text: this.text(this.ex.lab_set(m.i >>> 0, m.u)) });
    } else if (m.op === "play") {
      this.ex.lab_play(m.on ? 1 : 0);
    }
  }
  process(inputs, outputs) {
    if (!this.ex) return true;
    const out = outputs[0], n = out[0].length;
    this.ex.lab_render(n);
    out[0].set(new Float32Array(this.ex.memory.buffer, this.ex.lab_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(this.ex.memory.buffer, this.ex.lab_out_r(), n));
    return true;
  }
}
registerProcessor("mifm1-lab", Mifm1Lab);
