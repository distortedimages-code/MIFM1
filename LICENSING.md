# MIFM1's licences

**The firmware** (the `.fwsc`, the installer's packages, the `.rfm1`) contains the drum voices of Erica Synths' LXR
firmware (the LXR-02), which are GPL-3.0-only: the firmware as a whole is distributed under the **GNU General Public
License version 3** (`COPYING`), with the terms of the LXR firmware's `NOTICE.md` for its part. Its source is this
repository, at the commit the package names.

**The files** keep their own licences, as their headers say:

| What | Where | Licence |
| --- | --- | --- |
| MIFM1's own code | `firmware/app.c`, `firmware/mi/` (but `mi_lxr.cc`), `firmware/tests/`, `firmware/shim/`, `tools/` (but `vendor_lxr.py`, `lxr_check.cc`) | MPL-2.0 (`LICENSE`): MPL-2.0 files may be part of a GPL work (its section 3.3) and stay MPL-2.0 file by file |
| fm1-base and felucca-bsp | `base/` (a submodule) | MPL-2.0 |
| MIFM1's changes to them (the USB audio input's 10 channels), applied to `base/` before a build | `patches/`, `tools/patch_base.sh` | MPL-2.0, as the files they change |
| Mutable Instruments' code (Emilie Gillet) | `firmware/third_party/` (stmlib, Plaits, Clouds, Warps, Tides 2, Marbles) | MIT (`firmware/third_party/LICENSE-eurorack`, `LICENSE-stmlib`) |
| The LXR-02's drum voices (Copyright Erica Synths), MIFM1's port of them and the code that reuses theirs | `firmware/third_party/lxr/`, `firmware/mi/mi_lxr.cc`, `tools/vendor_lxr.py`, `tools/lxr_check.cc` | GPL-3.0-only with the additional permission and terms of `firmware/third_party/lxr/NOTICE.md` |

The name at power-on is drawn in **Syne** (Copyright 2017 The Syne Project Authors), under the SIL Open Font License
1.1: `firmware/fonts/Syne-OFL.txt`, beside the font; `firmware/logo_font.h` and `firmware/rand_font.h` are made from it by `tools/gen_logo_font.py`.
The interface's fonts are fm1-base's (Barlow, SIL OFL 1.1).

Trademarks: Mutable Instruments, Erica Synths, LXR and LXR-02 are their owners'. MIFM1 does not use them as its
names, nor as its models' (on the screen: FM DRUM, FM SNARE, FM CYMBAL, FM HI-HAT); it names them only to say whose
code it runs. MIFM1 is not made, approved or supported by Mutable Instruments or Erica Synths.
