#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""The browser emulator's page in two languages (0.13): English in the page, French in a dictionary inside it.

The page (firmware/web/emu/index.html) is written in English; every element whose words change with the language
carries data-t="n" (its inner HTML) or data-t-alt / data-t-aria-label / data-t-content / data-t-title="n" (that
attribute), and the French of each n is a JSON object in the page's <script type="application/json" id="i18n-fr">,
before the page's script. The script swaps them (EN / FR on top: the page is not reloaded, the machine keeps playing);
an element's words are swapped between its tags, its elements kept (their listeners, ids, a checkbox's state), so a
unit's French has the same tags, in the same order, as its English. Words the page's script writes itself are in it,
in both languages (L("...", "...")). Elements the same in both languages (a model's name) carry nothing.

A sentence added or changed: its English in the page, its element a data-t (the next free n), its French in the JSON;
then the check.

  python3 tools/web/i18n.py check PAGE                 every data-t its French, every French its data-t, the same
                                                        tags in both
  python3 tools/web/i18n.py units PAGE OUT.json        the units of a page (as written: its elements' inner HTML and
                                                        attributes that hold words), as a JSON list
  python3 tools/web/i18n.py build PAGE_FR EN.json OUT  the English page from a French one and the units' English
                                                        (EN.json: {"id": "English"}), the French dictionary inside it
                                                        (how the page went English, 0.13)

A unit is an element with words and no block inside it (a p, an h3, an li of words, a figcaption, a button, an SVG
text...): its inner HTML, inline elements (a, b, em, span, br...) kept in it."""
import html
import json
import re
import sys
from pathlib import Path

INLINE = {"a", "b", "i", "em", "strong", "span", "br", "kbd", "abbr", "code", "sup", "sub", "small", "s", "u", "mark",
          "q", "cite", "time", "var", "wbr", "tspan", "input"}
VOID = {"br", "img", "input", "meta", "link", "hr", "source", "wbr", "area", "base", "col", "embed", "param", "track"}
RAW = {"script", "style"}
ATTRS = ("alt", "aria-label", "title", "placeholder")
TAG = re.compile(r"<(/?)([a-zA-Z][\w:-]*)((?:[^>\"']|\"[^\"]*\"|'[^']*')*?)(/?)>")
ATTR = re.compile(r"""(\s)([\w:-]+)(=)("([^"]*)"|'([^']*)')""")
WORDS = re.compile(r"[A-Za-zÀ-ÿ]{2,}")


def scan(s):
    """the units: (kind, start, end, tag, attr): kind 'html' (s[start:end] the inner HTML) or 'attr' (the value)"""
    units, stack, i, n = [], [], 0, len(s)
    while i < n:
        if s.startswith("<!--", i):
            i = s.index("-->", i) + 3
            continue
        if s.startswith("<!", i):
            i = s.index(">", i) + 1
            continue
        if s[i] == "<":
            m = TAG.match(s, i)
            if not m:
                i += 1
                continue
            close, tag, attrs, selfc = m.group(1), m.group(2).lower(), m.group(3), m.group(4)
            if not close:
                for a in ATTR.finditer(attrs):   # (its attributes with words)
                    name, val = a.group(2).lower(), a.group(5) if a.group(5) is not None else a.group(6)
                    want = name in ATTRS or (name == "content" and tag == "meta" and re.search(
                        r"""name=["'](description|og:description|twitter:description)""", attrs))
                    if want and WORDS.search(val or ""):
                        vs = m.start(3) + a.start(4) + 1
                        units.append(("attr", vs, vs + len(val), tag, name))
                if stack:                       # (a child: its parent has a block, unless it is inline)
                    if tag not in INLINE:
                        for e in stack:
                            e["block"] = True
                if tag in RAW:
                    j = s.lower().index(f"</{tag}", m.end())
                    i = s.index(">", j) + 1
                    continue
                if tag in VOID or selfc:
                    i = m.end()
                    continue
                stack.append({"tag": tag, "start": m.start(), "inner": m.end(), "block": False, "words": False})
                i = m.end()
                continue
            # a closing tag: its element done (an unclosed one under it closed with it)
            while stack and stack[-1]["tag"] != tag:
                stack.pop()
            if stack:
                e = stack.pop()
                if e["words"] and not e["block"] and e["tag"] not in INLINE:
                    units.append(("html", e["inner"], m.start(), e["tag"], None))
                if e["words"] and stack:        # (its words its parent's too)
                    stack[-1]["words"] = True
            i = m.end()
            continue
        j = s.find("<", i)
        j = n if j < 0 else j
        if stack and WORDS.search(html.unescape(s[i:j])):
            stack[-1]["words"] = True
            if stack[-1]["block"]:             # (words beside a block: told, not a unit)
                sys.stderr.write(f"mixed: <{stack[-1]['tag']}> at {stack[-1]['start']}: {s[i:j].strip()[:60]!r}\n")
        i = j
    units.sort(key=lambda u: u[1])
    return units


def cmd_units(page, out):
    s = Path(page).read_text()
    res = [{"id": str(k), "kind": u[0], "tag": u[3], "attr": u[4], "text": s[u[1]:u[2]]}
           for k, u in enumerate(scan(s))]
    Path(out).write_text(json.dumps(res, ensure_ascii=False, indent=1))
    print(f"{len(res)} units: {out}")


def cmd_build(page_fr, en_json, out):
    s = Path(page_fr).read_text()
    en = json.loads(Path(en_json).read_text())
    units = scan(s)
    fr = {}
    # each unit: its English in its place, its element marked (its start tag, after its name); all the edits made from
    # the end, on the French page's offsets
    edits = []
    for k, (kind, a, b, tag, attr) in enumerate(units):
        key = str(k)
        if key not in en:
            raise SystemExit(f"build: unit {key} has no English")
        if en[key] == s[a:b]:
            continue                            # (the same in both)
        fr[key] = s[a:b]
        t0 = s.rfind("<", 0, a if kind == "attr" else a - 1)
        ins = TAG.match(s, t0).end(2)
        name = "data-t" if kind == "html" else f"data-t-{attr}"
        edits.append((a, b, en[key]))
        edits.append((ins, ins, f' {name}="{key}"'))
    out_s = s
    for a, b, text in sorted(edits, key=lambda e: (e[0], e[1]), reverse=True):
        out_s = out_s[:a] + text + out_s[b:]
    fr_json = json.dumps(fr, ensure_ascii=False).replace("</", "<\\/")
    at = out_s.index("<script", out_s.index("<body"))   # (before the page's script: it reads it as it starts)
    out_s = out_s[:at] + f'<script type="application/json" id="i18n-fr">{fr_json}</script>\n' + out_s[at:]
    Path(out).write_text(out_s)
    print(f"build: {len(fr)} units in English ({len(units) - len(fr)} the same), {out}")


def inner(s, start):
    """the inner HTML of the element whose start tag is at start"""
    m = TAG.match(s, start)
    tag, depth, i = m.group(2).lower(), 1, m.end()
    while depth:
        n = re.compile(rf"<(/?){tag}\b[^>]*>", re.I).search(s, i)
        if not n:
            raise SystemExit(f"check: <{tag}> at {start} not closed")
        depth += -1 if n.group(1) else 1
        i = n.end()
    return s[m.end():n.start()]


def cmd_check(page):
    s = Path(page).read_text()
    m = re.search(r'<script type="application/json" id="i18n-fr">(.*?)</script>', s, re.S)
    fr = json.loads(m.group(1)) if m else {}
    ids = re.findall(r'\sdata-t(?:-[\w-]+)?="(\d+)"', s)
    dup = sorted({k for k in ids if ids.count(k) > 1}, key=int)
    bad = sorted(set(ids) - set(fr), key=int), sorted(set(fr) - set(ids), key=int)
    if bad[0] or bad[1] or dup:
        raise SystemExit(f"check: without French {bad[0]}; French without an element {bad[1]}; twice {dup}")
    tags = lambda h: re.findall(r"<[^>]+>", h)
    for a in re.finditer(r'\sdata-t="(\d+)"', s):     # (an element's words: the same tags, in order, in both)
        en = inner(s, s.rfind("<", 0, a.start()))
        if tags(en) != tags(fr[a.group(1)]):
            raise SystemExit(f"check: {a.group(1)}: its tags differ\n  en {tags(en)}\n  fr {tags(fr[a.group(1)])}")
    print(f"check: {len(ids)} units, each its French, the same tags")


if __name__ == "__main__":
    c = sys.argv[1] if len(sys.argv) > 1 else ""
    if c == "units" and len(sys.argv) == 4:
        cmd_units(sys.argv[2], sys.argv[3])
    elif c == "build" and len(sys.argv) == 5:
        cmd_build(*sys.argv[2:5])
    elif c == "check" and len(sys.argv) == 3:
        cmd_check(sys.argv[2])
    else:
        raise SystemExit(__doc__)
