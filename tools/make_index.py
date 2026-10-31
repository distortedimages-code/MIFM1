#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the site's first page (the installer and the backup page are tools/make_installer.py's).

    python3 tools/make_index.py site 0.1"""
import sys
from pathlib import Path

site, version = Path(sys.argv[1]), sys.argv[2]
page = f"""<!doctype html>
<html lang="fr"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>MIFM1 {version}</title>
<style>
body {{ font: 16px/1.5 system-ui, sans-serif; max-width: 36em; margin: 2em auto; padding: 0 1em; background: #111; color: #ddd; }}
h1 {{ font-size: 1.4em; }} h2 {{ font-size: 1.1em; margin-top: 1.6em; }} a {{ color: #7cf; }}
.card {{ border: 1px solid #333; border-radius: 8px; padding: 1em; margin: 1em 0; }}
table {{ border-collapse: collapse; width: 100%; }} td {{ border-top: 1px solid #333; padding: .3em .4em; vertical-align: top; }}
td:first-child {{ white-space: nowrap; color: #7cf; }}
</style>
<h1>MIFM1 {version}</h1>
<p>Une groovebox 4 pistes pour le M-VAVE FM-1 dont tout le son vient du code open source de Mutable Instruments :
Plaits sur chaque piste (ses 24 modèles), Marbles comme seul séquenceur, deux Tides pour moduler, et une matrice de
câbles.</p>
<div class="card"><b>Installer</b><br><a href="install/">Installer MIFM1 {version}</a>
<p>Dans Chrome ou Edge, le FM-1 branché en USB et à l'arrêt. Laisser la page aller jusqu'au bout : le firmware, puis
les tables des modules.</p></div>
<h2>Les commandes</h2>
<table>
<tr><td>KNOB 1 à 4</td><td>les 4 paramètres de la page</td></tr>
<tr><td>SELECT</td><td>l'autre face du module (1/3, 2/3…)</td></tr>
<tr><td>ALGORITHM</td><td>le modèle de Plaits, le mode de Tides, le modèle de T, la gamme</td></tr>
<tr><td>PRESETS</td><td>la piste active, de 1 à 4</td></tr>
<tr><td>EDIT · LFO · SEQ · ARP · HOME</td><td>Plaits · Tides A puis B · Marbles T · Marbles X · mixer</td></tr>
<tr><td>PLAY</td><td>l'horloge de Marbles tourne ou s'arrête</td></tr>
<tr><td>clavier</td><td>joue la piste active, même à l'arrêt (OCT− OCT+ pour l'octave)</td></tr>
<tr><td>SEL</td><td>mode MOD : tenir une touche-source et tourner un potard pose un câble (touches 1-4 Tides A,
5-8 Tides B, 9-11 X1-X3, 12 Y, 13-15 T1-T3, 17-18 Tides A ou B en éventail sur les 4 potards)</td></tr>
<tr><td>SEL tenu</td><td>la matrice de la piste : K1 le câble, K2 la source, K3 la cible, K4 la dose</td></tr>
<tr><td>SAVE tenu</td><td>sauvegarde le projet (à l'arrêt)</td></tr>
<tr><td>GLO</td><td>la charge des deux cœurs, les tables</td></tr>
</table>
<p><a href="backup/">Sauvegarder ou restaurer le projet dans un fichier</a></p>
</html>
"""
site.mkdir(parents=True, exist_ok=True)
(site / "index.html").write_text(page, encoding="utf-8")
print(f"index: {site}/index.html")
