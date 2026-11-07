#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the site for a release package, without the parts of fm1-base's tools/make_site.py MIFM1 has not yet (a
front page in Markdown, a manual, the firmware in the browser). The same installer page (base/web/installer.html,
with fm1ports.js, fm1pkg.js and fm1ota.js inlined): it writes the firmware, then the resource region (the Mutable
Instruments tables, NAME-VERSION-res.bin, from beside the package), then offers m-bootloader. And the backup page
(base/web/backup/index.html): the saved project to a file, and back; (0.16.53) the mirror's (base/web/mirror/index.html):
the screen and the panel on the computer.

    python3 tools/make_installer.py firmware/build/mifm1-0.1.fwsc 0.1 site

Then serve the folder (python3 -m http.server 8000 --directory site) and open http://localhost:8000/install/ in
Chrome or Edge (Web MIDI wants a page from a server, not a file)."""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "base"
APP = ROOT / "firmware"
sys.path.insert(0, str(BASE / "tools"))
from make_site import MBOOT_VERSION, product_of, strip_module, tool_pages  # noqa: E402
from build import read_conf  # noqa: E402
from fm1_resources import ResError, payload_of  # noqa: E402


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    conf = read_conf(APP / "fm1.toml")
    name = conf["name"]
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(re.escape(conf["id_prefix"]) + r"\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: a release build is needed (tools/build.py ../firmware --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    file_name = f"{name.lower()}-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    res_src, res_name = pkg.with_name(pkg.stem + "-res.bin"), Path(file_name).stem + "-res.bin"
    if not res_src.exists():
        raise SystemExit(f"{res_src}: the resource region is missing (fm1.toml resources; tools/build.py writes it)")
    try:
        payload, _ = payload_of(res_src.read_bytes())   # checked as the FM-1 checks it
    except ResError as e:
        raise SystemExit(f"{res_src}: {e}")
    if payload != (APP / conf["resources"]).read_bytes():
        raise SystemExit(f"{res_src}: not {conf['resources']}")
    boot = BASE / "web" / "m-bootloader" / f"fm1-uboot-{MBOOT_VERSION}.fwsc"
    boot_name = f"m-bootloader-{MBOOT_VERSION}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    (out / "install").mkdir(parents=True)
    (out / "firmware").mkdir()
    shutil.copy(pkg, out / "firmware" / file_name)
    shutil.copy(res_src, out / "firmware" / res_name)
    shutil.copy(boot, out / "firmware" / boot_name)
    shutil.copy(BASE / "web" / "m-bootloader" / "LICENSE", out / "firmware" / "m-bootloader-LICENSE.txt")
    for lic in sorted((BASE / "LICENSES").glob("*.txt")):
        shutil.copy(lic, out / "firmware" / lic.name)
    html = (BASE / "web" / "installer.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((BASE / "web" / f).read_text(encoding="utf-8"))
                    for f in ("fm1ports.js", "fm1pkg.js", "fm1ota.js"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + file_name,
                       "backup": bool(conf.get("backup")), "res": "../firmware/" + res_name,
                       "boot": {"pkg": "../firmware/" + boot_name, "version": MBOOT_VERSION,
                                "sha256": hashlib.sha256(boot.read_bytes()).hexdigest()}})
    for mark in ("/*LIB*/", "/*META*/", "/*APP*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"base/web/installer.html: {mark} expected once (another fm1-base version?)")
    repo, credits = conf.get("repo", ""), conf.get("credits", "")
    for key, val in (("RES", conf.get("resources_what", "{{NAME}}'s data")),
                     ("RES_JA", conf.get("resources_what_ja", "{{NAME}} のデータ")),
                     ("NAME", name), ("REPO", repo), ("REPO_SHORT", re.sub(r"^https?://", "", repo)),
                     ("LICENSE", conf.get("licence", "MPL-2.0")), ("CREDITS", " " + credits if credits else "")):
        html = html.replace("{{" + key + "}}", val)
    left = sorted(set(re.findall(r"\{\{[A-Z_]+\}\}", html)))
    if left:
        raise SystemExit(f"base/web/installer.html: {' '.join(left)} not filled in")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*APP*/", "null")
    (out / "install" / "index.html").write_text(html, encoding="utf-8")
    for p, h in tool_pages(APP, conf).items():           # backup/index.html, mirror/index.html
        (out / p).parent.mkdir(parents=True, exist_ok=True)
        (out / p).write_text(h, encoding="utf-8")
    words = APP / "web" / "words"                        # the screen reader's words (the build writes them)
    if words.is_dir():
        shutil.copytree(words, out / "words")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: install/ ({product}, {file_name}, {res_name}: {len(payload)} B, m-bootloader "
          f"{MBOOT_VERSION}), backup/, mirror/")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
