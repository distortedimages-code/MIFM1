#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the site for a release package, without the parts of fm1-base's tools/make_site.py MIFM1 has not yet (a
front page in Markdown, a manual, the firmware in the browser). The same installer page (base/web/installer.html,
with fm1ports.js, fm1pkg.js and fm1ota.js inlined): it writes the firmware, then the resource region (the Mutable
Instruments tables, NAME-VERSION-res.bin, from beside the package), then offers m-bootloader. And the backup page
(base/web/backup/index.html): the saved project to a file, and back.

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


# (0.16.62) the FM-1s sold since autumn 2026, their UC25HQ80 flash: before the m-bootloader option, in the page's two
# languages (its EN / 日本語 button sets the page's lang; the Japanese paragraphs on one line each: a line's break
# would show as a space)
NEW_UNITS_CSS = """  .units { border-left: 3px solid var(--dim); padding: 2px 0 2px 12px; margin: 16px 0; }
  .units h2 { font-size: 15px; margin: 0 0 6px; }
  html[lang="ja"] .units[lang="en"], html[lang="en"] .units[lang="ja"] { display: none; }
"""
NEW_UNITS = """  <div class="units" lang="en">
    <h2>FM-1s sold since autumn 2026</h2>
    <p class="small">They carry another flash chip (a UC25HQ80 in place of the Puya P25Q80). MIFM1 0.16.62 and later
      work on them: install as on any FM-1, from M-VAVE's firmware.</p>
    <p class="small">If MIFM1 0.16.61 or older is on such an FM-1, this page cannot reach it (that version takes no
      update on this chip): first put the stock firmware back with the recovery link above (a Mac), then install.</p>
    <p class="small">On these FM-1s (or if you are not sure), leave "Also install m-bootloader" unticked for now:
      whether it supports this chip is not known yet.</p>
    <p class="small">M-VAVE's firmware V16 has been said to refuse other firmwares (not confirmed): if this page does
      not find an FM-1 running V16, say so under "Report a problem" below.</p>
  </div>
  <div class="units" lang="ja">
    <h2>2026年秋以降に販売された FM-1</h2>
    <p class="small">これらは別のフラッシュチップ（Puya P25Q80 の代わりに UC25HQ80）を搭載しています。MIFM1 0.16.62以降はこれらでも動きます。ほかの FM-1 と同じく、M-VAVE のファームウェアからインストールしてください。</p>
    <p class="small">このような FM-1 に MIFM1 0.16.61 以前が入っている場合、このページからは届きません（そのバージョンはこのチップではアップデートを受けられません）。まず上の復旧のリンク（Mac）で標準ファームウェアに戻してから、インストールしてください。</p>
    <p class="small">これらの FM-1 では（またはわからない場合は）、今のところ「m-bootloader も書き込む」にチェックを入れないでください。このチップに対応しているかはまだわかっていません。</p>
    <p class="small">M-VAVE のファームウェア V16 はほかのファームウェアを受け付けないという話があります（未確認）。V16 の FM-1 がこのページで見つからない場合は、下の「不具合を報告する」から知らせてください。</p>
  </div>
"""


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
    for mark in ("  <div id=\"mbootopt\" hidden>\n", "</style>"):
        if html.count(mark) != 1:
            raise SystemExit(f"base/web/installer.html: {mark.strip()} expected once (another fm1-base version?)")
    html = html.replace("</style>", NEW_UNITS_CSS + "</style>").replace("  <div id=\"mbootopt\" hidden>\n",
                                                                       NEW_UNITS + "  <div id=\"mbootopt\" hidden>\n")
    (out / "install" / "index.html").write_text(html, encoding="utf-8")
    for p, h in tool_pages(APP, conf).items():           # backup/index.html
        (out / p).parent.mkdir(parents=True, exist_ok=True)
        (out / p).write_text(h, encoding="utf-8")
    words = APP / "web" / "words"                        # the screen reader's words (the build writes them)
    if words.is_dir():
        shutil.copytree(words, out / "words")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: install/ ({product}, {file_name}, {res_name}: {len(payload)} B, m-bootloader "
          f"{MBOOT_VERSION}), backup/")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
