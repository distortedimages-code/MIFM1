# Mutable Instruments' code

The modules' DSP, copied from Emilie Gillet's [eurorack](https://github.com/pichenettes/eurorack) repository
(commit f4cfa7697095a28d292bff758beb51deb49b5a86) and its [stmlib](https://github.com/pichenettes/stmlib)
submodule (commit e3bd7c9cc00e4364166f9905c0509b6ffd0535ec) by `tools/vendor_mi.sh`: stmlib's DSP and utilities,
and the `dsp` code and tables of Plaits, Clouds, Warps, Tides 2 and Marbles, and Marbles' preset scales (the one
table of its `settings.cc`, as `marbles/preset_scales.h`). Nothing of their hardware code.

The STM32 projects' code is under the MIT licence (the eurorack README; each file's header): LICENSE-eurorack.
stmlib's own LICENSE is beside it. Mutable Instruments is a registered trademark: this project does not use the
name for itself, and names the modules only to say whose code it runs.

The few changes made to these copies are in PATCHES.md, applied by `tools/patch_mi.sh`.
