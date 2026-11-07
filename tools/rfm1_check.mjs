// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// (0.16.56) What fm1-base's tests/rfm1_test.mjs leaves out and RemoteFM1 needs: memory from rfm1_alloc. The app
// takes a scratch block first (strings, MIDI, the audio: 17440 bytes in its build of 2026-11-07), and a host stages
// a saved flash (1 MiB) the same way; 0.16.55's malloc (wasi-libc's) asked memory.grow for every byte of a memory
// the format fixes, so every rfm1_alloc gave 0 and RemoteFM1 refused the file. Here: those blocks, inside the
// memory and apart, written and read back, freed and given again; then a boot whose strings are in them, and the
// screen drawn.
//   node tools/rfm1_check.mjs FILE.rfm1
import { readFileSync } from "node:fs";

const mod = new WebAssembly.Module(readFileSync(process.argv[2]));
let mem, fails = 0;
const ok = (c, what) => { console.log(`  ${c ? "ok  " : "FAIL"} ${what}`); if (!c) fails++; };
const wasi = {
  fd_write: (fd, iov, n, nw) => { new DataView(mem.buffer).setUint32(nw, 0, true); return 0; },
  fd_read: () => 8, fd_close: () => 8, fd_seek: () => 8,
  clock_time_get: (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  proc_exit: (c) => { throw new Error(`proc_exit ${c}`); },
};
const x = new WebAssembly.Instance(mod, { wasi_snapshot_preview1: wasi }).exports;
mem = x.memory;
if (x._initialize) x._initialize();
const size = mem.buffer.byteLength;
const inMem = (p, n) => p > 0 && p + n <= size;
const desc = x.rfm1_get(6);
ok(desc > 0, "rfm1_get(6)");
const sizes = [17440, 1 << 20, 64 * 1024, 32];
const got = sizes.map((n) => x.rfm1_alloc(n));
ok(got.every((p, i) => inMem(p, sizes[i])), `rfm1_alloc ${sizes.join(", ")}: ${got.join(", ")} (memory ${size} B)`);
const spans = got.map((p, i) => [p, p + sizes[i]]).sort((a, b) => a[0] - b[0]);
ok(spans.every((s, i) => !i || spans[i - 1][1] <= s[0]), "the blocks apart");
{
  const u = new Uint8Array(mem.buffer);
  got.forEach((p, i) => u.fill(i + 1, p, p + sizes[i]));
  ok(got.every((p, i) => u[p] === i + 1 && u[p + sizes[i] - 1] === i + 1), "written and read back");
}
x.rfm1_free(got[1]);
const again = x.rfm1_alloc(1 << 20);
ok(inMem(again, 1 << 20), `freed, 1 MiB again: ${again}`);
if (x.heap_free_bytes) console.log(`       heap: ${x.heap_free_bytes(0)} B free, ${x.heap_free_bytes(1)} B the largest block`);
// a boot as the app does it, its string in a block of rfm1_alloc's
const panel = new DataView(mem.buffer).getUint32(desc + x.rfm1_layout(30), true);
x.rfm1_flash_stage(0, 0);
const s = x.rfm1_alloc(64);
new Uint8Array(mem.buffer).set(new TextEncoder().encode("fm1core:staged\0"), s);
x.rfm1_options(s, 0, 0, 1);
x.rfm1_boot_options(-1, 0, -1);
new Uint8Array(mem.buffer)[panel + x.rfm1_layout(16)] = 2;
x.rfm1_init(0);
const writes = () => new DataView(mem.buffer).getUint32(panel + x.rfm1_layout(12), true);
const w0 = writes();
for (let ms = 0; ms < 2000; ms++) { x.rfm1_tick(1); if (ms % 15 === 0) x.rfm1_frame(); else x.rfm1_idle(); }
ok(writes() > w0, `booted, the screen drawn (${writes() - w0} writes)`);
console.log(fails ? `rfm1_check: ${fails} FAILED` : "rfm1_check: all ok");
process.exit(fails ? 1 : 0);
