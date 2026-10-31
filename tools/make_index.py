#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the site's first page, the two firmwares' installers side by side.

    python3 tools/make_index.py site 0.0"""
import sys
from pathlib import Path

site, version = Path(sys.argv[1]), sys.argv[2]
page = f"""<!doctype html>
<html lang="fr"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>MIFM1 {version}</title>
<style>
body {{ font: 16px/1.5 system-ui, sans-serif; max-width: 34em; margin: 2em auto; padding: 0 1em; background: #111; color: #ddd; }}
h1 {{ font-size: 1.4em; }} a {{ color: #7cf; }} .card {{ border: 1px solid #333; border-radius: 8px; padding: 1em; margin: 1em 0; }}
</style>
<h1>MIFM1 {version} : banc d'essai</h1>
<p>Deux firmwares : le code des six modules ne tient pas d'un coup dans la flash du FM-1. Installe-les l'un après
l'autre, lance le banc (REC) et photographie la page des résultats.</p>
<div class="card"><b>1. SONS</b> : Plaits (24 modèles), Rings, Tides, Marbles, et les mix sur deux cœurs.<br>
<a href="sons/install/">Installer MIFM1 SONS</a></div>
<div class="card"><b>2. EFFETS</b> : Clouds, Warps, et les mix sur deux cœurs.<br>
<a href="effets/install/">Installer MIFM1 EFFETS</a></div>
<p>Dans Chrome ou Edge, le FM-1 branché en USB.</p>
</html>
"""
site.mkdir(parents=True, exist_ok=True)
(site / "index.html").write_text(page, encoding="utf-8")
print(f"index: {site}/index.html")
