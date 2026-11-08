#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the site's "Which FM-1?" page (site/fm1/index.html). The two FM-1s, drawn: the first one, and the units sold
since autumn 2026 (a white M-VAVE under the screen, a UC25HQ80 flash in place of the Puya P25Q80), each with its
steps to install MIFM1; then M-VAVE's firmware V16, step by step. In English and in French, EN / FR on top (the same
choice as the other pages: mifm1-lang), its look the first page's (tools/make_index.py): aluminium, black ink, one
orange, Archivo's widths.

    python3 tools/make_fm1.py site 0.16.62"""
import re
import sys
from pathlib import Path

INSTALL = "../install/"
RESCUE = "https://github.com/hugelton/felucca-bsp/tree/main/tools"
ISSUES = "https://github.com/distortedimages-code/MIFM1/issues"
MVAVE = "https://www.m-vave.com/download"
BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]   # (the black keys, as the machine's page draws them)


def nw(s):
    """M-VAVE and FM-1 kept whole (a line never broken at their hyphen)"""
    return re.sub(r"(M-VAVE|FM-1)", r'<span class="nw">\1</span>', s)


def t(en, fr, tag="span", cls=""):
    """a sentence in both languages (the one not chosen hidden); the French with its spaces before ; : ? ! » and
    after « unbreakable"""
    c = f" {cls}" if cls else ""
    fr = re.sub(r" ([;:?!»])", "\u00a0\\1", fr).replace("« ", "«\u00a0")
    return f'<{tag} class="en{c}" lang="en">{nw(en)}</{tag}><{tag} class="fr{c}" lang="fr">{nw(fr)}</{tag}>'


def fm1(logo):
    """the FM-1 in wireframe, as the machine's page lays it out (viewBox 1000 x 640); logo: the M-VAVE under the
    screen. The place under the screen ringed either way: where to look."""
    s = ['<rect class="b" x="14" y="14" width="972" height="612" rx="36"/>']
    for x, y in ((105, 100), (215, 100), (105, 205), (215, 205)):
        s.append(f'<circle cx="{x}" cy="{y}" r="24"/><line x1="{x}" y1="{y - 19}" x2="{x}" y2="{y - 10}"/>')
    for i in range(4):
        x = 610 + i * 104
        s.append(f'<circle cx="{x}" cy="100" r="22"/><line x1="{x}" y1="83" x2="{x}" y2="74"/>')
    s.append('<rect x="72" y="268" width="82" height="38" rx="9"/><rect x="170" y="268" width="82" height="38" rx="9"/>')
    s.append('<rect class="b" x="300" y="44" width="236" height="236" rx="26"/>'
             '<rect class="scr" x="320" y="64" width="196" height="196" rx="8"/>')
    for row in (174, 236):
        for i in range(6):
            s.append(f'<rect x="{582 + i * 63}" y="{row}" width="54" height="48" rx="9"/>')
    x0, y0, w, h = 36, 340, 928, 270
    pitch = (w - 24) / 16
    kw = pitch * 0.78
    s.append(f'<rect class="k" x="{x0}" y="{y0}" width="{w}" height="{h}" rx="18"/>')
    for a in BLACK_AFTER:
        s.append(f'<rect class="k" x="{x0 + 12 + (a + 1) * pitch - kw / 2:.1f}" y="{y0 + h * .06:.1f}" width="{kw:.1f}" '
                 f'height="{h * .33:.1f}" rx="{kw / 2:.1f}"/>')
    for i in range(16):
        s.append(f'<rect class="k" x="{x0 + 12 + i * pitch + (pitch - kw) / 2:.1f}" y="{y0 + h * .43:.1f}" '
                 f'width="{kw:.1f}" height="{h * .52:.1f}" rx="{kw / 2:.1f}"/>')
    s.append('<ellipse class="ring" cx="418" cy="310" rx="122" ry="27"/>')
    if logo:
        s.append('<text class="logo" x="418" y="322">M-VAVE</text>')
    return f'<svg class="wf" viewBox="0 0 1000 640" aria-hidden="true" focusable="false">{"".join(s)}</svg>'


def step(n, title, body, link=None):
    a = f'<p class="act">{link}</p>' if link else ""
    return f'<li><span class="n">{n:02d}</span><div><h3>{title}</h3><p>{body}</p>{a}</div></li>'


def main(site, version):
    out = Path(site) / "fm1"
    out.mkdir(parents=True, exist_ok=True)
    go_install = f'<a class="btn" href="{INSTALL}">{t(f"Open the installer (MIFM1 {version})", f"Ouvrir l’installateur (MIFM1 {version})")} →</a>'
    go_rescue = f'<a class="btn ghost" href="{RESCUE}">{t("The recovery tool (Mac)", "L’outil de récupération (Mac)")} →</a>'
    plug = step(0, t("Plug it in", "Branche-le"),
                t("The FM-1 to the computer over USB, directly, with a data cable. Open the installer in Chrome or Edge.",
                  "Le FM-1 à l’ordinateur en USB, directement, avec un câble de données. Ouvre l’installateur dans "
                  "Chrome ou Edge."), go_install)
    install = step(0, t("Install", "Installe"),
                   t("Press Install and let the page run to the end: the firmware, then the modules’ tables. Do not "
                     "unplug while it writes.",
                     "Clique Installer et laisse la page aller jusqu’au bout : le firmware, puis les tables des "
                     "modules. Ne débranche pas pendant l’écriture."))

    def numbered(items):
        return "".join(i.replace('<span class="n">00</span>', f'<span class="n">{k + 1:02d}</span>', 1)
                       for k, i in enumerate(items))

    first = numbered([
        plug, install,
        step(0, t("m-bootloader, if you like", "m-bootloader, si tu veux"),
             t("“Also install m-bootloader” adds a recovery menu (PLAY held at power-on) and an update mode (REC), "
               "even when a firmware no longer starts. Optional.",
               "« Also install m-bootloader » ajoute un menu de secours (PLAY tenu à l’allumage) et un mode mise à "
               "jour (REC), même quand un firmware ne démarre plus. Facultatif.")),
    ])
    recent = numbered([
        step(0, t("MIFM1 already on it?", "MIFM1 déjà dessus ?"),
             t("Only if a MIFM1 older than 0.16.62 is installed: that version writes nothing on this chip, so the "
               "page cannot reach it. Put the stock firmware back first with the recovery tool, then go on. Still on "
               "M-VAVE’s firmware: skip this step.",
               "Seulement si un MIFM1 antérieur à 0.16.62 est installé : cette version n’écrit rien sur cette puce, "
               "la page ne la trouve donc pas. Remets d’abord le firmware d’origine avec l’outil de récupération, "
               "puis continue. Encore sur le firmware d’M-VAVE : passe cette étape."), go_rescue),
        plug,
        step(0, t("Leave m-bootloader unticked", "Laisse m-bootloader décoché"),
             t("Do not tick “Also install m-bootloader”: whether it handles this chip is not known yet.",
               "Ne coche pas « Also install m-bootloader » : on ne sait pas encore s’il gère cette puce.")),
        install,
        step(0, t("Check", "Vérifie"),
             t("GLO, then SELECT one notch: the PROJECT page’s last line says “tables OK”, and what you save stays "
               "after power-off. FLASH NOT KNOWN on the screen: tell us.",
               "GLO, puis SELECT d’un cran : la dernière ligne de la page PROJECT dit « tables OK », et ce que tu "
               "sauvegardes reste après l’extinction. FLASH NOT KNOWN à l’écran : dis-le-nous.")),
    ])
    v16 = numbered([
        step(0, t("Stay on V16", "Reste en V16"),
             t("No need to go back to V15 first: fm1-base, which MIFM1 is built on, installs over V16 (tested by its "
               "authors on an FM-1).",
               "Pas besoin de repasser en V15 avant : fm1-base, sur lequel MIFM1 est construit, s’installe "
               "par-dessus la V16 (testé par ses auteurs sur un FM-1).")),
        step(0, t("Follow your model’s steps", "Suis les étapes de ton modèle"),
             t("The first model or the recent one, above: V16 has nothing to do with the memory chip, both models "
               "can run V15 or V16.",
               "Le premier modèle ou le récent, plus haut : la V16 n’a rien à voir avec la puce mémoire, les deux "
               "modèles peuvent être en V15 ou en V16.")),
        step(0, t("Leave m-bootloader unticked", "Laisse m-bootloader décoché"),
             t("V16 brings its own new bootloader, which m-bootloader would replace: that case is not confirmed yet.",
               "La V16 apporte son propre bootloader, que m-bootloader remplacerait : ce cas n’est pas encore "
               "confirmé.")),
        step(0, t("Stuck? Do not loop", "Bloqué ? Pas en boucle"),
             t("If the page does not find the FM-1 or stops on an error, do not start again and again: note the "
               "message and tell us. “Back to the stock firmware”, at the bottom of the installer, puts M-VAVE’s "
               "firmware back if needed.",
               "Si la page ne trouve pas le FM-1 ou s’arrête sur une erreur, ne relance pas en boucle : note le "
               "message et signale-le. « Back to the stock firmware », en bas de l’installateur, remet le firmware "
               "d’M-VAVE si besoin."),
             f'<a class="btn ghost" href="{ISSUES}">{t("Report a problem", "Signaler un problème")} →</a>'),
        step(0, t("Back to V16 later", "Revenir à la V16 plus tard"),
             t("Download M-VAVE’s file from its site, then “Back to the stock firmware” at the bottom of the installer.",
               "Télécharge le fichier sur le site d’M-VAVE, puis « Back to the stock firmware » en bas de "
               "l’installateur."),
             f'<a class="btn ghost" href="{MVAVE}">m-vave.com/download →</a>'),
    ])

    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Which FM-1? · MIFM1</title>
<meta name="description" content="The two M-VAVE FM-1s (the white M-VAVE under the screen of the units sold since autumn 2026), how to install MIFM1 on each, and M-VAVE's firmware V16, step by step.">
<meta name="theme-color" content="#e4e5e0">
<script>(function () {{   /* the language chosen (?lang=, or before, on any of the site's pages) */
  var d = document.documentElement, l = null;
  try {{ l = new URLSearchParams(location.search).get("lang") || localStorage.getItem("mifm1-lang"); }} catch (e) {{}}
  if (l === "fr") d.lang = "fr";
}})()</script>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,300..900&display=swap">
<style>
/* the first page's look (tools/make_index.py): aluminium, black ink, one orange (the titles' lines a little
   apart: the French capitals' accents) */
:root {{ --alu: #e4e5e0; --paper: #f8f8f5; --ink: #000; --grey: #5c5d58; --orange: #ff5000; --focus: #0050ff; color-scheme: light; }}
* {{ box-sizing: border-box; }}
html {{ background: var(--alu); scroll-behavior: smooth; }}
body {{ margin: 0; background: var(--alu); color: var(--ink); font: 400 17px/1.5 "Archivo", "Helvetica Neue", Arial, sans-serif;
       -webkit-font-smoothing: antialiased; }}
html[lang="en"] .fr, html[lang="fr"] .en {{ display: none !important; }}
a {{ color: inherit; text-underline-offset: 3px; }} a:hover {{ color: var(--orange); }}
:focus-visible {{ outline: 3px solid var(--focus); outline-offset: 2px; }}
.wrap {{ max-width: 1120px; margin: 0 auto; padding: 0 24px; }}
.top {{ display: flex; align-items: center; gap: 1.5rem; padding-block: 18px; font-size: 15px; }}
.top .name {{ font-weight: 800; font-stretch: 125%; letter-spacing: .02em; text-decoration: none; }}
.top nav {{ margin-left: auto; display: flex; gap: 1.4rem; }}
.top nav a {{ text-decoration: none; color: var(--grey); white-space: nowrap; }} .top nav a:hover {{ color: var(--ink); }}
.lang {{ display: flex; border: 2px solid var(--ink); }}
.lang button {{ appearance: none; margin: 0; border: 0; border-radius: 0; padding: 3px 8px 2px; background: transparent; color: var(--ink);
               font-family: inherit; font-size: 15px; line-height: 1; font-weight: 900; font-stretch: 62%; letter-spacing: .06em; cursor: pointer; }}
.lang button + button {{ border-left: 2px solid var(--ink); }}
.lang button[aria-pressed="true"] {{ background: var(--ink); color: var(--alu); cursor: default; }}
.lang button:not([aria-pressed="true"]):hover {{ background: var(--orange); }}

.kick {{ margin: 40px 0 0; font-size: 15px; font-weight: 300; font-stretch: 125%; letter-spacing: .14em; color: var(--orange); text-transform: uppercase; }}
h1 {{ margin: .3rem 0 0; font-size: clamp(3.6rem, 12vw, 9.5rem); line-height: .82; font-weight: 900; font-stretch: 62%;
     letter-spacing: -.01em; text-transform: uppercase; }}
.intro {{ display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1.25fr); gap: .8rem 3rem; margin-top: 1.4rem;
         border-top: 10px solid var(--ink); padding-top: 1rem; }}
.lead {{ margin: 0; font-size: clamp(1.5rem, 2.6vw, 2.2rem); line-height: 1; font-weight: 900; font-stretch: 62%; text-transform: uppercase; max-width: 18ch; }}
.sub {{ margin: 0; color: var(--grey); max-width: 60ch; }}

/* the two FM-1s: a choice */
.pick {{ display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px; margin: 56px 0 0; }}
.card {{ appearance: none; display: flex; flex-direction: column; gap: 14px; margin: 0; padding: 26px 22px 22px; border: 0; border-radius: 0;
        background: var(--paper); color: var(--ink); text-align: left; font: inherit; cursor: pointer;
        box-shadow: inset 0 6px 0 var(--ink); transition: box-shadow .18s ease, background-color .18s ease, transform .18s ease; }}
.card:hover {{ box-shadow: inset 0 14px 0 var(--ink); }}
.card[aria-pressed="true"] {{ background: var(--orange); box-shadow: inset 0 14px 0 var(--ink); }}
.card .tag {{ font-size: 14px; font-weight: 300; font-stretch: 125%; letter-spacing: .14em; text-transform: uppercase; }}
.card h2 {{ margin: 0; font-size: clamp(2.2rem, 4.4vw, 3.6rem); line-height: 1.02; font-weight: 900; font-stretch: 62%; text-transform: uppercase; }}
.card .what {{ margin: 0; color: var(--grey); }}
.card[aria-pressed="true"] .what {{ color: var(--ink); }}
.card .row {{ display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-top: auto; }}
.card .sel {{ font-weight: 800; font-stretch: 112%; }}
.card .dot {{ flex: none; width: 26px; height: 26px; border: 3px solid var(--ink); border-radius: 50%; }}
.card[aria-pressed="true"] .dot {{ background: var(--ink); box-shadow: inset 0 0 0 5px var(--orange); }}
.wf {{ display: block; width: 100%; height: auto; overflow: visible; }}
.wf rect, .wf circle, .wf line, .wf ellipse {{ fill: none; stroke: var(--ink); stroke-width: 1.4; vector-effect: non-scaling-stroke; }}
.wf .b {{ stroke-width: 2.4; }}
.wf .k {{ stroke: var(--grey); stroke-width: 1.1; }}
.wf .scr {{ fill: var(--ink); stroke: none; }}
.wf .ring {{ stroke: var(--orange); stroke-width: 3; stroke-dasharray: 7 6; }}
.card[aria-pressed="true"] .wf .ring {{ stroke: var(--ink); }}
.card[aria-pressed="true"] .wf .k {{ stroke: var(--ink); }}
.wf .logo {{ fill: var(--orange); font: 800 33px "Archivo", Arial, sans-serif; letter-spacing: .16em; text-anchor: middle; }}
.card[aria-pressed="true"] .wf .logo {{ fill: var(--ink); }}
.unsure {{ appearance: none; display: flex; align-items: center; justify-content: space-between; gap: 16px; width: 100%; margin: 8px 0 0;
          padding: 16px 22px; border: 2px solid var(--ink); border-radius: 0; background: transparent; color: var(--ink);
          font: inherit; text-align: left; cursor: pointer; transition: background-color .18s ease; }}
.unsure:hover {{ background: var(--paper); }}
.unsure[aria-pressed="true"] {{ background: var(--ink); color: var(--alu); }}
.unsure b {{ font-weight: 900; font-stretch: 62%; font-size: 1.5rem; text-transform: uppercase; line-height: 1; }}

/* the steps */
.steps {{ margin: 56px 0 0; }}
.steps > header {{ display: flex; align-items: baseline; justify-content: space-between; gap: 16px; flex-wrap: wrap;
                  border-top: 10px solid var(--ink); padding-top: .8rem; }}
.steps h2, .v16 h2 {{ margin: 0; font-size: clamp(2.4rem, 5vw, 4.2rem); line-height: 1; font-weight: 900; font-stretch: 62%; text-transform: uppercase; }}
.steps .for {{ margin: 0; color: var(--grey); }}
.wait {{ margin: 18px 0 0; padding: 22px; background: var(--paper); color: var(--grey); }}
ol.do {{ list-style: none; margin: 18px 0 0; padding: 0; display: grid; gap: 8px; }}
ol.do li {{ display: grid; grid-template-columns: 92px minmax(0, 1fr); gap: 20px; padding: 20px 22px 22px; background: var(--paper); }}
ol.do .n {{ font-size: 3.4rem; line-height: .8; font-weight: 900; font-stretch: 62%; color: var(--orange); }}
ol.do h3 {{ margin: 0 0 .3rem; font-size: 1.35rem; line-height: 1.1; font-weight: 800; font-stretch: 112%; }}
ol.do p {{ margin: 0; max-width: 64ch; }}
ol.do p.act {{ margin-top: 14px; }}
.btn {{ display: inline-block; padding: 10px 16px 9px; background: var(--ink); color: var(--alu); font-weight: 800; font-stretch: 112%;
       text-decoration: none; transition: background-color .15s ease, color .15s ease; }}
.btn:hover {{ background: var(--orange); color: var(--ink); }}
.btn.ghost {{ background: transparent; color: var(--ink); box-shadow: inset 0 0 0 2px var(--ink); }}
.btn.ghost:hover {{ background: var(--ink); color: var(--alu); }}
.done {{ margin: 8px 0 0; padding: 18px 22px; background: var(--ink); color: var(--alu); font-weight: 700; }}
.sure {{ margin: 18px 0 0; padding: 4px 0 4px 16px; border-left: 4px solid var(--orange); color: var(--grey); font-size: 15px; max-width: 76ch; }}
.sure code {{ font: 600 14px ui-monospace, Menlo, Consolas, monospace; color: var(--ink); }}
[hidden] {{ display: none !important; }}
.nw {{ white-space: nowrap; }}
.show {{ animation: rise .32s ease both; }}
@keyframes rise {{ from {{ opacity: 0; transform: translateY(10px); }} to {{ opacity: 1; transform: none; }} }}

/* V16 */
.v16 {{ margin: 80px 0 0; display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1.6fr); gap: 1.2rem 3rem;
       border-top: 10px solid var(--ink); padding-top: 1rem; }}
.v16 .badge {{ display: inline-block; margin: 0 0 .6rem; padding: 4px 9px 3px; background: var(--orange); font-weight: 900; font-stretch: 62%;
              font-size: 1.2rem; letter-spacing: .06em; line-height: 1; }}
.v16 .why {{ margin: .9rem 0 0; color: var(--grey); }}
.v16 ol.do {{ margin: 0; }}
.v16 ol.do li {{ grid-template-columns: 64px minmax(0, 1fr); }}
.v16 ol.do .n {{ font-size: 2.4rem; }}
.fine {{ margin: 64px 0 48px; padding-top: 1rem; border-top: 2px solid var(--ink); color: var(--grey); font-size: 14px; }}

@media (max-width: 860px) {{ .intro, .v16 {{ grid-template-columns: minmax(0, 1fr); }} }}
@media (max-width: 720px) {{ .pick {{ grid-template-columns: minmax(0, 1fr); }} }}
@media (max-width: 600px) {{
  .wrap {{ padding: 0 16px; }} .intro, .steps > header, .v16 {{ border-top-width: 7px; }}
  .top {{ gap: .8rem; font-size: 13px; }} .top nav {{ gap: .8rem; }} .top .name {{ font-stretch: 100%; }}
  .lang button {{ font-size: 13px; padding: 3px 6px 2px; }}
  ol.do li, .v16 ol.do li {{ grid-template-columns: minmax(0, 1fr); gap: 8px; padding: 18px 16px 20px; }}
  .card {{ padding: 22px 16px 18px; }} .unsure {{ flex-direction: column; align-items: flex-start; gap: 6px; padding: 14px 16px; }}
}}
@media (prefers-reduced-motion: reduce) {{ html {{ scroll-behavior: auto; }} * {{ transition: none !important; animation: none !important; }} }}
</style>
</head>
<body>
<div class="wrap">
<header class="top">
  <a class="name" href="../">MIFM1</a>
  <nav>
    <a href="{INSTALL}">{t("Install", "Installer")}</a>
    <a href="../emu/">{t("In the browser", "Dans le navigateur")}</a>
  </nav>
  <div class="lang" role="group" aria-label="Language / Langue">
    <button type="button" lang="en" aria-pressed="true">EN</button><button type="button" lang="fr" aria-pressed="false">FR</button>
  </div>
</header>
<main>
  <p class="kick">{t(f"Before installing · MIFM1 {version}", f"Avant d’installer · MIFM1 {version}")}</p>
  <h1>{t("Which FM-1?", "Quel FM-1 ?")}</h1>
  <div class="intro">
    <p class="lead">{t("Look under the screen.", "Regarde sous l’écran.")}</p>
    <p class="sub">{t("The FM-1s sold since autumn 2026 carry another memory chip, and a white M-VAVE printed under the "
                      "screen. MIFM1 works on both; only a few steps differ. Pick yours.",
                      "Les FM-1 vendus depuis l’automne 2026 ont une autre puce mémoire, et un M-VAVE blanc imprimé "
                      "sous l’écran. MIFM1 marche sur les deux ; seules quelques étapes changent. Choisis le tien.")}</p>
  </div>

  <div class="pick" role="group" aria-label="FM-1">
    <button type="button" class="card" id="c-first" aria-pressed="false" aria-controls="steps" data-pick="first">
      <span class="tag">{t("Nothing under the screen", "Rien sous l’écran")}</span>
      {fm1(False)}
      <h2>{t("The first model", "Le premier modèle")}</h2>
      <p class="what">{t("Sold until autumn 2026. Its flash: a Puya P25Q80.", "Vendu jusqu’à l’automne 2026. Sa mémoire : une Puya P25Q80.")}</p>
      <span class="row"><span class="sel">{t("That’s mine", "C’est le mien")}</span><span class="dot"></span></span>
    </button>
    <button type="button" class="card" id="c-recent" aria-pressed="false" aria-controls="steps" data-pick="recent">
      <span class="tag">{t("M-VAVE under the screen", "M-VAVE sous l’écran")}</span>
      {fm1(True)}
      <h2>{t("The recent model", "Le modèle récent")}</h2>
      <p class="what">{t("Sold since autumn 2026, mostly on AliExpress. Its flash: a UC25HQ80.",
                         "Vendu depuis l’automne 2026, surtout sur AliExpress. Sa mémoire : une UC25HQ80.")}</p>
      <span class="row"><span class="sel">{t("That’s mine", "C’est le mien")}</span><span class="dot"></span></span>
    </button>
  </div>
  <button type="button" class="unsure" id="c-unsure" aria-pressed="false" aria-controls="steps" data-pick="unsure">
    <b>{t("Not sure?", "Pas sûr ?")}</b>
    <span>{t("Follow the recent model’s steps: they work on both.", "Suis les étapes du modèle récent : elles marchent sur les deux.")} →</span>
  </button>

  <section class="steps" id="steps" aria-live="polite">
    <header>
      <h2>{t("The steps", "Les étapes")}</h2>
      <p class="for" id="for"></p>
    </header>
    <p class="wait" id="wait">{t("Pick your FM-1 above: its steps show here.", "Choisis ton FM-1 plus haut : ses étapes s’affichent ici.")}</p>
    <div id="s-first" hidden>
      <ol class="do">{first}</ol>
      <p class="done">{t("That’s all. The next updates go the same way, from MIFM1.", "C’est tout. Les mises à jour suivantes se font de la même façon, depuis MIFM1.")}</p>
    </div>
    <div id="s-recent" hidden>
      <ol class="do">{recent}</ol>
      <p class="done">{t("That’s all. The next updates go the same way, from MIFM1.", "C’est tout. Les mises à jour suivantes se font de la même façon, depuis MIFM1.")}</p>
    </div>
    <p class="sure">{t("The white M-VAVE is a visual guide. The sure sign is the flash chip’s ID: M-Flasher shows it in download "
                       "mode, <code>0x856014</code> for the first model (Puya), <code>0xB36014</code> for the recent one "
                       "(UC25HQ80). Use it only to read: it can also write a firmware and change the bootloader.",
                       "Le M-VAVE blanc est un repère visuel. Le signe sûr, c’est l’identifiant de la puce : M-Flasher "
                       "l’affiche en mode téléchargement, <code>0x856014</code> pour le premier modèle (Puya), "
                       "<code>0xB36014</code> pour le récent (UC25HQ80). Ne t’en sers que pour lire : il sait aussi écrire "
                       "un firmware et changer le bootloader.")}</p>
  </section>

  <section class="v16" id="v16" aria-labelledby="v16h">
    <div>
      <span class="badge">V16</span>
      <h2 id="v16h">{t("M-VAVE’s firmware V16", "Le firmware V16 d’M-VAVE")}</h2>
      <p class="why">{t("V16 is the latest update of the stock firmware (autumn 2026), with a new bootloader. Some other "
                        "firmwares cannot install over it yet, hence the rumour that it blocks them all. It is a "
                        "software update: either model can run it.",
                        "La V16 est la dernière mise à jour du firmware d’origine (automne 2026), avec un nouveau "
                        "bootloader. Certains autres firmwares ne savent pas encore s’installer par-dessus, d’où la "
                        "rumeur qu’elle les bloque tous. C’est une mise à jour logicielle : les deux modèles peuvent "
                        "l’avoir.")}</p>
    </div>
    <ol class="do">{v16}</ol>
  </section>

  <p class="fine">{t(f'A problem, a doubt, an FM-1 that does not match? <a href="{ISSUES}">MIFM1’s GitHub issues</a>. '
                     f'Sources: <a href="https://github.com/charlesvestal/fm1-base/commit/e75b072edfb74d3943359ab8e216799452e51759">fm1-base (the two chips)</a>, '
                     f'<a href="https://www.musicfreaks.jp/archives/10635">musicfreaks.jp (the M-VAVE under the screen)</a>, '
                     f'<a href="https://github.com/kmplngj/Felucca/issues/619">Felucca #619 (V16)</a>.',
                     f'Un problème, un doute, un FM-1 qui ne ressemble à aucun des deux ? <a href="{ISSUES}">Les issues GitHub de MIFM1</a>. '
                     f'Sources : <a href="https://github.com/charlesvestal/fm1-base/commit/e75b072edfb74d3943359ab8e216799452e51759">fm1-base (les deux puces)</a>, '
                     f'<a href="https://www.musicfreaks.jp/archives/10635">musicfreaks.jp (le M-VAVE sous l’écran)</a>, '
                     f'<a href="https://github.com/kmplngj/Felucca/issues/619">Felucca #619 (V16)</a>.')}</p>
</main>
</div>
<script>
// EN / FR: the page in that language at once, the choice kept for the site's other pages
const buttons = document.querySelectorAll(".lang button");
function setLang(l, keep) {{
  document.documentElement.lang = l;
  buttons.forEach((b) => b.setAttribute("aria-pressed", b.lang === l ? "true" : "false"));
  if (keep) try {{ localStorage.setItem("mifm1-lang", l); }} catch {{ /* private window */ }}
  say();
}}
buttons.forEach((b) => b.addEventListener("click", () => setLang(b.lang, true)));
// the FM-1 picked (also from the address: #first, #recent, #unsure): its steps
const picks = document.querySelectorAll("[data-pick]");
const FOR = {{ first: ["For the first model", "Pour le premier modèle"], recent: ["For the recent model", "Pour le modèle récent"],
              unsure: ["The recent model’s: they work on both", "Celles du modèle récent : elles marchent sur les deux"] }};
let picked = null;
function say() {{ document.getElementById("for").textContent = picked ? FOR[picked][document.documentElement.lang === "fr" ? 1 : 0] : ""; }}
function pick(p, scroll) {{
  picked = p;
  picks.forEach((b) => b.setAttribute("aria-pressed", b.dataset.pick === p ? "true" : "false"));
  const recent = p === "recent" || p === "unsure";
  document.getElementById("wait").hidden = true;
  const a = document.getElementById("s-first"), b = document.getElementById("s-recent"), on = recent ? b : a;
  a.hidden = recent; b.hidden = !recent;
  on.classList.remove("show"); void on.offsetWidth; on.classList.add("show");
  say();
  try {{ history.replaceState(null, "", "#" + p); }} catch {{ /* file: */ }}
  if (scroll) document.getElementById("steps").scrollIntoView({{ block: "start" }});
}}
picks.forEach((b) => b.addEventListener("click", () => pick(b.dataset.pick, true)));
const h = location.hash.slice(1);
if (h in FOR) pick(h, false);
setLang(document.documentElement.lang === "fr" ? "fr" : "en");
</script>
</body>
</html>
"""
    (out / "index.html").write_text(page, encoding="utf-8")
    print(f"fm1: {out}/index.html")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
