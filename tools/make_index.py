#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the site's first page (the installer and the backup page are tools/make_installer.py's).

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
li {{ margin: .3em 0; }}
</style>
<h1>MIFM1 {version} : banc d'essai</h1>
<p>Les six modules au complet (Plaits et ses 24 modèles, Rings, Clouds, Warps, Tides, Marbles) dans un seul
firmware. Leurs plus grosses tables sont écrites à part, dans la zone de données du FM-1 : l'installeur s'en charge
juste après le firmware, sans rien demander de plus.</p>
<div class="card"><b>Installer</b><br><a href="install/">Installer MIFM1 {version}</a>
<p>Dans Chrome ou Edge, le FM-1 branché en USB et à l'arrêt (pas de lecture en cours). Laisser la page aller
jusqu'au bout : le firmware, puis les tables, puis la proposition de m-bootloader.</p></div>
<div class="card"><b>Le banc</b>
<ol><li>REC lance le banc automatique (54 scènes, 3 secondes chacune, moins de 3 minutes).</li>
<li>À la fin, la page des résultats s'affiche ; ils restent gardés même après extinction.</li>
<li>Pour les envoyer : photos de la page RÉSULTATS (SELECT pour défiler) et de la page MÉMOIRE (HOME), ou
<a href="backup/">la page de sauvegarde</a>, qui les enregistre dans un fichier .mifm1.</li></ol></div>
</html>
"""
site.mkdir(parents=True, exist_ok=True)
(site / "index.html").write_text(page, encoding="utf-8")
print(f"index: {site}/index.html")
