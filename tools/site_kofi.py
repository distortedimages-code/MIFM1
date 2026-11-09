#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the Ko-fi button on each of the site's pages, in the site's look (black ink, aluminium, one orange,
Archivo), bottom left. Pressed, Ko-fi's own panel (the page amnesy: ?widget=true&embed=true, the one Ko-fi's
floating-chat widget opens) unfolds above it, loaded only then; without script, or with a key held, it is a link to
the page. Ko-fi's logo (from Ko-fi, as their widget shows it) in a white disc; gone if it does not load. On the
machine's page the button steps aside while the machine is under it (#device: its knobs, its keys).

    python3 tools/site_kofi.py site          (after the pages: every index.html under site)"""
import sys
from pathlib import Path

KOFI = """<style>
/* the Ko-fi button (tools/site_kofi.py): the site's ink, aluminium and orange, whatever the page's own look */
.kofi-b { position: fixed; left: 16px; bottom: 16px; z-index: 60; display: inline-flex; align-items: center; gap: 8px;
          height: 38px; padding: 0 15px 0 6px; border-radius: 999px; background: #000; color: #e4e5e0;
          font: 800 12px/1 "Archivo", "Helvetica Neue", Arial, sans-serif; font-stretch: 125%; letter-spacing: .07em;
          text-transform: uppercase; text-decoration: none; white-space: nowrap; box-shadow: 0 1px 0 rgba(0, 0, 0, .25);
          transition: background .15s, color .15s, opacity .2s, visibility .2s; -webkit-tap-highlight-color: transparent; }
.kofi-b:hover, .kofi-b[aria-expanded="true"] { color: #ff5000; }   /* (the site's links: orange; not the button, over the orange cards) */
.kofi-b:focus-visible { outline: 3px solid #0050ff; outline-offset: 2px; }
.kofi-b .kofi-cup { flex: none; width: 26px; height: 26px; border-radius: 50%; background: #fff; display: grid; place-items: center; }
.kofi-b .kofi-cup img { width: 19px; height: 19px; object-fit: contain; display: block; }
html[lang="fr"] .kofi-en, html:not([lang="fr"]) .kofi-fr { display: none; }
html.kofi-away .kofi-b { opacity: 0; visibility: hidden; }
.kofi-p { position: fixed; left: 16px; bottom: 64px; z-index: 60; width: 340px; max-width: calc(100vw - 32px);
          height: min(712px, calc(100vh - 96px)); height: min(712px, calc(100dvh - 96px)); display: flex; flex-direction: column;
          background: #f8f8f5; border: 2px solid #000; box-shadow: 0 8px 28px rgba(0, 0, 0, .22); }
.kofi-p[hidden] { display: none; }
.kofi-h { display: flex; align-items: center; gap: 10px; padding: 0 0 0 12px; border-bottom: 2px solid #000; height: 38px; flex: none;
          font: 800 12px/1 "Archivo", "Helvetica Neue", Arial, sans-serif; font-stretch: 125%; letter-spacing: .07em; color: #000; }
.kofi-h a { margin-left: auto; color: #5c5d58; font-weight: 600; font-stretch: 100%; letter-spacing: 0; text-transform: none;
            text-decoration: none; font-size: 13px; }
.kofi-h a:hover { color: #ff5000; }
.kofi-h button { appearance: none; border: 0; border-left: 2px solid #000; border-radius: 0; background: transparent; color: #000;
                 width: 38px; height: 100%; font: 400 22px/1 Arial, sans-serif; cursor: pointer; }
.kofi-h button:hover { background: #ff5000; }
.kofi-h :focus-visible { outline: 3px solid #0050ff; outline-offset: -3px; }
.kofi-p iframe { flex: 1; width: 100%; border: 0; background: #f9f9f9; }
@media (max-width: 520px) { .kofi-b { left: 12px; bottom: 12px; } .kofi-p { left: 8px; right: 8px; bottom: 58px; width: auto; max-width: none;
                                                                       height: calc(100vh - 70px); height: calc(100dvh - 70px); } }
@media print { .kofi-b, .kofi-p { display: none; } }
</style>
<a class="kofi-b" id="kofi-b" href="https://ko-fi.com/amnesy" target="_blank" rel="noopener" aria-controls="kofi-p" aria-expanded="false"><span class="kofi-cup"><img src="https://storage.ko-fi.com/cdn/logomarkLogo.png" alt="Ko-fi" width="19" height="19" onerror="this.parentNode.remove()"></span><span class="kofi-en" lang="en">Support me</span><span class="kofi-fr" lang="fr">Me soutenir</span></a>
<div class="kofi-p" id="kofi-p" role="dialog" aria-label="Ko-fi" hidden>
  <div class="kofi-h">KO-FI<a href="https://ko-fi.com/amnesy" target="_blank" rel="noopener">ko-fi.com/amnesy ↗</a><button type="button" aria-label="Close">×</button></div>
</div>
<script>
(function () {   /* the panel: Ko-fi's page in a frame, made at the first press */
  var b = document.getElementById("kofi-b"), p = document.getElementById("kofi-p"), x = p.querySelector("button");
  var d = document.documentElement, dev = document.getElementById("device"), q = 0;
  function show(on) {
    if (on && !p.querySelector("iframe")) {
      var f = document.createElement("iframe");
      f.src = "https://ko-fi.com/amnesy/?hidefeed=true&widget=true&embed=true";
      f.title = "Ko-fi: amnesy";
      p.appendChild(f);
    }
    p.hidden = !on;
    b.setAttribute("aria-expanded", on ? "true" : "false");
    x.setAttribute("aria-label", (d.lang || "").indexOf("fr") === 0 ? "Fermer" : "Close");
    if (on) x.focus(); else look();
  }
  b.addEventListener("click", function (e) {
    if (e.button || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;   /* (a new tab, as a link) */
    e.preventDefault();
    show(p.hidden);
  });
  x.addEventListener("click", function () { show(false); b.focus(); });
  addEventListener("keydown", function (e) { if (e.key === "Escape" && !p.hidden) { show(false); b.focus(); } });
  function look() {   /* (the machine's page: the button away while the machine is under its place) */
    q = 0;
    if (!dev) return;
    var r = dev.getBoundingClientRect(), h = window.innerHeight;
    d.classList.toggle("kofi-away", p.hidden && r.left < 190 && r.right > 4 && r.top < h - 4 && r.bottom > h - 64);
  }
  function ask() { if (!q) { q = 1; requestAnimationFrame(look); } }
  if (dev) { addEventListener("scroll", ask, { passive: true }); addEventListener("resize", ask); look(); }
})();
</script>
"""


def add(html, where):
    if 'id="kofi-b"' in html:
        return html
    if html.count("</body>") != 1:
        raise SystemExit(f"{where}: </body> expected once")
    return html.replace("</body>", KOFI + "</body>")


if __name__ == "__main__":
    site = Path(sys.argv[1])
    pages = sorted(site.rglob("index.html"))
    for p in pages:
        p.write_text(add(p.read_text(encoding="utf-8"), p), encoding="utf-8")
    print("kofi: " + " ".join(str(p.relative_to(site)) for p in pages))
