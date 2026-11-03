#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the site's first page (the installer and the backup page are tools/make_installer.py's, the machine in the
browser firmware/web/emu/index.html). In English and in French, EN / FR on top as on the machine's page (the same
choice, kept in the same place: mifm1-lang), its look the same: aluminium, black ink, one orange, Archivo's widths.

    python3 tools/make_index.py site 0.1"""
import sys
from pathlib import Path

site, version = Path(sys.argv[1]), sys.argv[2]


def t(en, fr, tag="span"):
    """a sentence in both languages (the one not chosen hidden)"""
    return f'<{tag} class="en" lang="en">{en}</{tag}><{tag} class="fr" lang="fr">{fr}</{tag}>'


SUB_EN = ("Its own drums, those of the LXR-02, ZERO, a four-operator FM, PADS and its chord progressions, two analog "
          "polysynths (POLY, PROPHET), the best engines from Plaits, four modeled analog filters, a sequencer with "
          "P-locks and a random dose that reinvents every note. All the sound is open source.")
SUB_FR = ("Ses propres batteries, celles du LXR-02, ZERO, une FM à quatre opérateurs, PADS et ses progressions "
          "d'accords, deux polyphoniques analogiques (POLY, PROPHET), les meilleurs moteurs de Plaits, quatre filtres "
          "analogiques modélisés, un séquenceur à P-locks et une dose d'aléatoire qui réinvente chaque note. Tout le "
          "son est open source.")

page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>MIFM1 {version}</title>
<meta name="description" content="MIFM1: a 4-track synth machine for the M-VAVE FM-1, all of its sound open source.">
<meta name="theme-color" content="#e4e5e0">
<script>(function () {{   /* the language chosen (?lang=, or before, here or on the machine's page) */
  var d = document.documentElement, l = null;
  try {{ l = new URLSearchParams(location.search).get("lang") || localStorage.getItem("mifm1-lang"); }} catch (e) {{}}
  if (l === "fr") d.lang = "fr";
}})()</script>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,300..900&display=swap">
<style>
/* the machine's page's look: aluminium, black ink, one orange; the titles heavy, their widths set against each other */
:root {{ --alu: #e4e5e0; --paper: #f8f8f5; --ink: #000; --grey: #5c5d58; --orange: #ff5000; --focus: #0050ff; color-scheme: light; }}
* {{ box-sizing: border-box; }}
html {{ background: var(--alu); }}
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
.mark {{ margin: 20px 0 0; font-size: clamp(4.2rem, 20.6vw, 16.5rem); line-height: .8; font-weight: 900; font-stretch: 125%;
        letter-spacing: -.045em; white-space: nowrap; }}
.ver {{ margin: .6rem 0 0; font-size: clamp(1rem, 1.4vw, 1.25rem); font-weight: 300; font-stretch: 125%; letter-spacing: .14em; color: var(--orange); }}
.intro {{ display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1.25fr); gap: .8rem 3rem; margin-top: 1.4rem;
         border-top: 10px solid var(--ink); padding-top: 1rem; }}
.lead {{ margin: 0; font-size: clamp(1.9rem, 3.4vw, 3rem); line-height: .86; font-weight: 900; font-stretch: 62%; text-transform: uppercase; max-width: 16ch; }}
.sub {{ margin: 0; color: var(--grey); max-width: 58ch; }}
.ways {{ list-style: none; margin: 64px 0 0; padding: 0; display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; }}
.ways a {{ display: flex; flex-direction: column; height: 100%; padding: 22px 22px 26px; background: var(--paper); text-decoration: none;
          box-shadow: inset 0 6px 0 var(--ink); }}
.ways li:first-child a {{ background: var(--orange); box-shadow: inset 0 6px 0 var(--ink); }}
.ways a:hover {{ color: inherit; box-shadow: inset 0 14px 0 var(--ink); }}
.ways .n {{ font-size: 1rem; font-weight: 300; font-stretch: 125%; letter-spacing: .14em; }}
.ways h2 {{ margin: .5rem 0 .8rem; font-size: clamp(3rem, 6vw, 5.4rem); line-height: .8; font-weight: 900; font-stretch: 62%; text-transform: uppercase; }}
.ways p {{ margin: 0; }}
.ways .go {{ margin-top: auto; padding-top: 1.2rem; font-weight: 800; font-stretch: 112%; }}
.fine {{ margin: 56px 0 48px; padding-top: 1rem; border-top: 2px solid var(--ink); color: var(--grey); font-size: 14px; }}
@media (max-width: 860px) {{ .intro, .ways {{ grid-template-columns: minmax(0, 1fr); }} .ways {{ margin-top: 40px; }} }}
@media (max-width: 600px) {{
  .wrap {{ padding: 0 16px; }} .mark {{ font-size: 22vw; }} .intro {{ border-top-width: 7px; }} .lead {{ font-size: 2rem; }}
  .top {{ gap: .8rem; font-size: 13px; }} .top nav {{ gap: .8rem; }} .top .name {{ font-stretch: 100%; }}
  .lang button {{ font-size: 13px; padding: 3px 6px 2px; }}
}}
</style>
</head>
<body>
<div class="wrap">
<header class="top">
  <a class="name" href="./">MIFM1</a>
  <nav>
    <a href="emu/">{t("In the browser", "Dans le navigateur")}</a>
    <a href="https://github.com/distortedimages-code/MIFM1">{t("Source code", "Code source")}</a>
  </nav>
  <div class="lang" role="group" aria-label="Language / Langue">
    <button type="button" lang="en" aria-pressed="true">EN</button><button type="button" lang="fr" aria-pressed="false">FR</button>
  </div>
</header>
<main>
  <h1 class="mark">MIFM1</h1>
  <p class="ver">{version}</p>
  <div class="intro">
    <p class="lead">{t("A 4-track synth machine for the M-VAVE FM-1.", "Une machine de synthèse 4 pistes pour le M-VAVE FM-1.")}</p>
    <p class="sub">{t(SUB_EN, SUB_FR)}</p>
  </div>
  <ul class="ways">
    <li><a href="emu/"><span class="n">01</span><h2>{t("Try it", "Essayer")}</h2>
      <p>{t("The firmware itself, compiled for the browser (computer or phone): the same sound, the same screen, the demo "
            "playing as it powers on, and the whole machine explained, chapter by chapter.",
            "Le firmware lui-même, compilé pour le navigateur (ordinateur ou téléphone) : le même son, le même écran, "
            "la démo qui joue à l'allumage, et toute la machine expliquée, chapitre par chapitre.")}</p>
      <span class="go">{t("MIFM1 without the FM-1 →", "MIFM1 sans le FM-1 →")}</span></a></li>
    <li><a href="install/"><span class="n">02</span><h2>{t("Install", "Installer")}</h2>
      <p>{t("In Chrome or Edge, the FM-1 plugged in over USB and switched off. Let the page run to the end: the "
            "firmware, then the modules' tables.",
            "Dans Chrome ou Edge, le FM-1 branché en USB et à l'arrêt. Laisser la page aller jusqu'au bout : le "
            "firmware, puis les tables des modules.")}</p>
      <span class="go">{t(f"Install MIFM1 {version} →", f"Installer MIFM1 {version} →")}</span></a></li>
    <li><a href="backup/"><span class="n">03</span><h2>{t("Back up", "Sauver")}</h2>
      <p>{t("The project saved on the FM-1, to a file on the computer, and back.",
            "Le projet sauvegardé sur le FM-1, vers un fichier sur l'ordinateur, et retour.")}</p>
      <span class="go">{t("Save or restore →", "Sauvegarder ou restaurer →")}</span></a></li>
  </ul>
  <p class="fine">{t('Open source (MPL-2.0), built on <a href="https://github.com/charlesvestal/fm1-base">fm1-base</a> '
                     'and the code of Mutable Instruments. The controls, one by one: <a href="emu/#h-panel">the panel</a>.',
                     'Open source (MPL-2.0), sur <a href="https://github.com/charlesvestal/fm1-base">fm1-base</a> et le '
                     'code de Mutable Instruments. Les commandes, une à une : <a href="emu/#h-panel">le panneau</a>.')}</p>
</main>
</div>
<script>
// EN / FR: the page in that language at once, the choice kept for the machine's page too
const buttons = document.querySelectorAll(".lang button");
function setLang(l, keep) {{
  document.documentElement.lang = l;
  buttons.forEach((b) => b.setAttribute("aria-pressed", b.lang === l ? "true" : "false"));
  if (keep) try {{ localStorage.setItem("mifm1-lang", l); }} catch {{ /* private window */ }}
}}
buttons.forEach((b) => b.addEventListener("click", () => setLang(b.lang, true)));
setLang(document.documentElement.lang === "fr" ? "fr" : "en");
</script>
</body>
</html>
"""
site.mkdir(parents=True, exist_ok=True)
(site / "index.html").write_text(page, encoding="utf-8")
print(f"index: {site}/index.html")
