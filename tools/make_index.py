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
/* the screen's colours: dark, lit in Mutable Instruments' teal, orange and pink */
body {{ font: 16px/1.5 system-ui, sans-serif; max-width: 36em; margin: 2em auto; padding: 0 1em; background: #191718;
       color: #f0efeb; }}
h1 {{ font-size: 1.4em; letter-spacing: .04em; }}
h2 {{ font-size: 1.1em; margin-top: 1.6em; text-transform: uppercase; letter-spacing: .04em; color: #9a9798; }}
a {{ color: #18c9d4; font-weight: 600; }}
.card {{ border: 2px solid #ff4a7d; box-shadow: 0 0 12px rgba(255, 74, 125, .35); border-radius: 10px; padding: 1em;
        margin: 1em 0; }}
.card a {{ color: #191718; background: #18c9d4; padding: .15em .7em; border-radius: 1em; text-decoration: none; }}
table {{ border-collapse: collapse; width: 100%; }} td {{ border-top: 1px solid #2e2b2c; padding: .3em .4em; vertical-align: top; }}
td:first-child {{ white-space: nowrap; font-weight: 700; color: #ffb22e; }}
</style>
<h1>MIFM1 {version}</h1>
<p>Une groovebox 4 pistes pour le M-VAVE FM-1 dont tout le son vient du code open source de Mutable Instruments :
Plaits sur chaque piste (ses 24 modèles), un séquenceur pas à pas avec P-locks (modes KEY et STEP), un Endless par
piste, probabilités, ratchets, longueurs, aléatoire et swing, un quantiseur global, deux Tides pour moduler, une
matrice de câbles, et trois effets avec chacun sa piste de pas : un delay (filtre, drive, ping-pong), la réverbe de
Clouds et Warps entre deux pistes.</p>
<div class="card"><b>Installer</b><br><a href="install/">Installer MIFM1 {version}</a>
<p>Dans Chrome ou Edge, le FM-1 branché en USB et à l'arrêt. Laisser la page aller jusqu'au bout : le firmware, puis
les tables des modules.</p></div>
<h2>Les commandes</h2>
<table>
<tr><td>KNOB 1 à 4</td><td>les 4 paramètres de la page</td></tr>
<tr><td>SELECT</td><td>l'autre face du module (1/3, 2/3…) : ses potards glissent, les noms se réécrivent</td></tr>
<tr><td>ALGORITHM</td><td>le modèle de Plaits, le mode de Tides ; Warps : d'un repère au suivant ; le delay : son temps</td></tr>
<tr><td>PRESETS</td><td>la piste active, de 1 à 4</td></tr>
<tr><td>EDIT · LFO</td><td>Plaits · Tides A puis B</td></tr>
<tr><td>SEQ</td><td>les pas de la piste : NOTE, PROB, RATCH, LENGTH, puis RANDOM (PAN, DECAY, TUNE, RATCH) ;
d'un effet : DECAY, PROB, DEPTH, LENGTH</td></tr>
<tr><td>ARP</td><td>Endless : REC enregistre (une touche = une note, SEL = un silence), REC à nouveau le joue, une touche le
transpose, REC tenu l'efface</td></tr>
<tr><td>HOME</td><td>le mixer : les niveaux, les pans</td></tr>
<tr><td>FX</td><td>les effets l'un après l'autre : le delay (TIME, FDBK, FILTER, DRIVE ; RESO, MODE, MOD,
RETURN), la réverbe, Warps (ALGO, TIMBRE, LEVEL 1, LEVEL 2 ; CARRIER, MOD, DRY, MODE) ; SELECT : leurs
faces, les envois des pistes, leurs pas</td></tr>
<tr><td>PLAY</td><td>le séquenceur tourne ou s'arrête</td></tr>
<tr><td>REC</td><td>KEY (le clavier joue la piste active, même à l'arrêt, OCT− OCT+ pour l'octave) ou STEP : les
touches blanches sont les 16 pas, les 7 premières noires les pistes de pas (1-4 les pistes, 5 le delay, 6 la réverbe,
7 Warps), OCT− OCT+ les pages ; tenir un pas + un potard = P-lock</td></tr>
<tr><td>REC tenu (pas)</td><td>efface la piste de pas (trigs, locks, aléatoires ; d'une piste ses câbles) ; avec GLO tenu, toutes et les câbles globaux</td></tr>
<tr><td>les pas d'un effet</td><td>sans trig, l'effet est toujours là ; avec des trigs, chaque trig l'ouvre et il se
referme avec DECAY ; ses P-locks changent ses réglages pas par pas</td></tr>
<tr><td>SEL</td><td>mode MOD : tenir une touche-source et tourner un potard pose un câble (touches 1-4 Tides A,
5-8 Tides B, 9-10 Tides A ou B en éventail sur les 4 potards)</td></tr>
<tr><td>SEL tenu</td><td>la matrice de la piste : K1 le câble, K2 la source, K3 la cible, K4 la dose</td></tr>
<tr><td>SAVE tenu</td><td>sauvegarde le projet (à l'arrêt)</td></tr>
<tr><td>GLO</td><td>TEMPO, SCALE, ROOT, SWING ; SELECT : le projet et la machine</td></tr>
<tr><td>GLO tenu + potard</td><td>ALL : le même réglage sur les 4 pistes, l'écran en jaune et noir ; pendant 4 s, GLO remet tout comme avant au début de la mesure suivante</td></tr>
<tr><td>ENV</td><td>l'enveloppe de la piste : DECAY, LPG COLOUR, TIMBRE ENV, MORPH ENV</td></tr>
<tr><td>ENV tenu + potard</td><td>l'aléatoire de ce réglage : à droite nouveau à chaque trig, à gauche en boucle</td></tr>
<tr><td>GLO, SELECT, K1, REC tenu</td><td>INIT : un projet neuf (4 Plaits sur VIRTUAL ANALOG, rien de câblé, aucun pas), le
patch de démo ou le projet sauvegardé ; rien n'est sauvegardé avant SAVE tenu</td></tr>
</table>
<p><a href="backup/">Sauvegarder ou restaurer le projet dans un fichier</a></p>
</html>
"""
site.mkdir(parents=True, exist_ok=True)
(site / "index.html").write_text(page, encoding="utf-8")
print(f"index: {site}/index.html")
