#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1 as an .rfm1 (0.16.55): the firmware for host apps, RemoteFM1 for iOS first (an iPhone or an iPad, alone or
with the FM-1 as its panel, or an AUv3 in AUM). fm1-base's tools/build.py --rfm1 builds it: the host simulator,
the same app as on the FM-1, compiled to WebAssembly (RemoteFM1IOS's docs/RFM1-FORMAT.md, version 1, ABI 6), here
with Zig's clang as the browser emulator is (tools/web/emcc, wasm32-wasi: wasi-libc, not Emscripten's runtime). Then
the file packed again with what that build cannot know: the compiler that built it (built_by), and the licence texts
of everything in it (MIFM1's, its modules' and the LXR-02's notices, fm1-base's and felucca-bsp's, wasi-libc's and
Zig's, whose runtime parts are compiled in).

    python3 tools/make_rfm1.py [--release X.Y]      -> firmware/build/rfm1/mifm1-X.Y.rfm1 (dev+COMMIT without)

Commit first: the manifest's source_url names the commit (the source a user is owed). Needs pip install ziglang."""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "base"
OUT = ROOT / "firmware" / "build" / "rfm1"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--release", help="the version (the CI's VERSION)")
    a = ap.parse_args()
    env = dict(os.environ, PATH=f"{ROOT / 'tools' / 'web'}{os.pathsep}{os.environ.get('PATH', '')}")
    cmd = [sys.executable, "tools/build.py", "../firmware", "--rfm1", *(["--release", a.release] if a.release else [])]
    r = subprocess.run(cmd, cwd=BASE, env=env)
    if r.returncode:
        return r.returncode
    man = json.loads((OUT / "manifest.json").read_text())
    built = sorted(OUT.glob(f"{man['id']}-{man['version']}.rfm1"))
    if len(built) != 1:
        raise SystemExit(f"make_rfm1: {OUT}/{man['id']}-{man['version']}.rfm1 not built")
    zig = subprocess.run([sys.executable, "-m", "ziglang", "version"], capture_output=True, text=True).stdout.strip()
    import ziglang
    zdir = Path(ziglang.__file__).parent
    man["built_by"] = re.sub(r", emcc .*$", f", zig cc {zig} (wasm32-wasi, MIFM1's tools/web/emcc)", man["built_by"])
    man["glue_url"] = man.get("glue_url")         # (fm1-base at its commit; MIFM1's patches/ are in source_url's tree)
    (OUT / "manifest.json").write_text(json.dumps(man, indent=1, ensure_ascii=False))
    lic = [("MIFM1: COPYING (GPL-3.0, the firmware as a whole)", ROOT / "COPYING"),
           ("MIFM1: LICENSING.md (the files' licences)", ROOT / "LICENSING.md"),
           ("MIFM1: LICENSE (MPL-2.0, its own files)", ROOT / "LICENSE"),
           ("Mutable Instruments' code (MIT): LICENSE-eurorack", ROOT / "firmware/third_party/LICENSE-eurorack"),
           ("stmlib (MIT): LICENSE-stmlib", ROOT / "firmware/third_party/LICENSE-stmlib"),
           ("The LXR-02's drum voices: NOTICE.md (GPL-3.0-only, with its terms)", ROOT / "firmware/third_party/lxr/NOTICE.md"),
           ("The LXR-02's drum voices: LICENSE", ROOT / "firmware/third_party/lxr/LICENSE"),
           ("fm1-base: LICENSE (MPL-2.0)", BASE / "LICENSE"),
           ("felucca-bsp: LICENSE", BASE / "bsp" / "LICENSE"),
           ("wasi-libc (compiled in): LICENSE", zdir / "lib/libc/wasi/LICENSE"),
           ("wasi-libc: LICENSE-APACHE-LLVM", zdir / "lib/libc/wasi/LICENSE-APACHE-LLVM"),
           ("wasi-libc: LICENSE-APACHE", zdir / "lib/libc/wasi/LICENSE-APACHE"),
           ("wasi-libc: LICENSE-MIT", zdir / "lib/libc/wasi/LICENSE-MIT"),
           ("Zig (its compiler-rt, compiled in): LICENSE (MIT)", zdir / "LICENSE")]
    missing = [str(f) for _, f in lic if not f.is_file()]
    if missing:
        raise SystemExit("make_rfm1: missing " + ", ".join(missing))
    dst = built[0]
    r = subprocess.run([sys.executable, str(BASE / "tools" / "rfm1_pack.py"), str(OUT / "app.wasm"), str(dst),
                        "--manifest", str(OUT / "manifest.json"), "--credits", str(OUT / "credits.txt"),
                        *[x for n, f in lic for x in ("--license", f"{n}={f}")]])
    if r.returncode:
        raise SystemExit("make_rfm1: the .rfm1 is not to the format (base/tools/rfm1_pack.py)")
    print(f"rfm1     {dst}  ({dst.stat().st_size} B; {man['built_by']}; {len(lic)} licence texts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
