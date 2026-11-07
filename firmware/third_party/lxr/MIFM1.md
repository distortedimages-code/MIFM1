# The LXR-02's drum voices, in MIFM1

The sound engine's voices of Erica Synths' LXR firmware (the LXR-02), copied from
[lxr-desktop-app](https://github.com/erica-synths/lxr-desktop-app) (commit a43cd4fcda23ec3d577dc425261c3eb28bd5abff,
23 October 2026) by `tools/vendor_lxr.py`: the drum voice (kick, toms), the snare, the cymbal and the hi-hat, with
what they are made of (`Soundengine/DSPAudio`: the oscillators and their tables, the transients, the filter, the
envelopes, the distortion), and the headers they need. Nothing of the LXR-02's hardware code, sequencer, MIDI
parser, menus, LFOs, modulation matrix, mixer or effects.

Licence: GPL-3.0-only, with the additional permission and terms of `NOTICE.md` (Copyright (C) 2012-2026 Erica
Synths): `LICENSE` and `NOTICE.md` here are the repository's own. The names "Erica Synths", "LXR" and "LXR-02" are
not used for MIFM1 or its models (on the screen: FM DRUM, FM SNARE; FM CYMBAL and FM HI-HAT were taken out of
MIFM1 in 0.16.43, their code kept here for `make lxr-check`); they only say where this code comes from. MIFM1's firmware, which contains these voices, is distributed under GPL-3.0 as a whole.

## What MIFM1 changed (2026)

Each changed file says so under its licence header. `tools/vendor_lxr.py` makes every change below from the
original files, the same each time it runs.

- **C to C++.** The `.c` files are compiled as C++ (`.cc`), as the FM-1's build compiles its modules: `true` and
  `false` no longer defined (`config.h`); `SVF_init()`'s declaration given its argument; `register` and `__inline`
  (on a function other units call) removed; the `#pragma GCC optimize("O0")` around `removeMaliciousSample` removed
  (the FM-1's build has no fast-math: `x != x` stays). The transients' `int8_t` samples written 0x00..0xFF are
  written as C reads them (0x80..0xFF negative), which a C++ brace list asks for.
- **The STM32.** `lxr_port.h` (MIFM1's) in place of the HAL and of the firmware's other headers: CMSIS's `__CLZ`,
  `__SSAT`, `__QADD16`, `__QSUB16` as the Cortex-M7 computes them; the MIDI parser's `midiParser_calcDetune`; the
  sequencer's `SEQ_DEFAULT_NOTE`; the faders (none: 1, as on the LXR-02); `ptrValue`. `lxr_port.cc`: the LFO and
  modulation node calls the voices make, doing nothing (MIFM1 has its own LFOs and cables). The memory sections
  (`INDTCM`, `IN_ITCM_RAM`) are empty. `REAL_FS` is 44100 Hz, the FM-1's rate (the LXR-02's I2S runs at 44140).
- **The Cortex-M7's conversions.** Where the code converts a float that may be negative or past 2^32 to an
  unsigned integer (an FM index, a phase increment, the PWM's second phase) or a float to an `int32_t` before
  `__SSAT` (the filter), `lxr_f2u` and `lxr_f2i` do what the Cortex-M7's VCVT does (toward zero, saturated: a
  negative FM index is 0 there), which a computer and the FM-1 would not do by themselves.
- **No 64-bit multiply.** The wavetables' interpolation, `((int64_t)d * frac) >> 22` (`>> 20` for the sine), is
  computed with 32-bit multiplies (`lxr_mul_shr22`, `lxr_mul_shr20`): the same number, for every d and frac it
  meets. The FM-1's CPU now and then gets a 64-bit multiply wrong beside 16-bit loads (fm1-base `bsp/docs/CPU.md`).
- **Single precision.** The FM-1's FPU has no double precision. What the LXR-02 computes in double is computed in
  float (`LXR_DOUBLE`), and its `fmaf` (VFMA) as a multiply and an add (`LXR_FMAF`): a few filter coefficients,
  the drum's gain interpolation, the fader and drive smoothing. The **amplitude envelope** (`SlopeEg2`) is the
  exception: a float would round its state (the cymbal's steep curve showed it at -60 dB), so `lxr_port.cc` keeps
  the LXR-02's double exactly (its sums of floats are exact in a double: here, integers of 2^-40) and computes its
  curve in pairs of floats; `SlopeEg2.c`'s own code is kept for the comparison build. Built with `LXR_HARD_DOUBLE`,
  the copies compute as the LXR-02 does: `make lxr-check` plays the voices both ways, sample for sample
  (99.965 % identical since 0.16.50, its PWM wave sounding; the others 26 LSB at most, -113 dB), and `make test`
  checks the envelope against the double, step by step.
- **One voice of each a track.** The snare, the cymbal and the hi-hat were single globals: each is an array of four
  (`LXR_VOICES`), its functions given the index `v` (`snareVoice.` becomes `snareVoice[v].`); the drum voices are
  four (`NUM_DRUM_VOICES`). The drum's `volEgValueBlock`, used only with `USE_AMP_FILTER`, is only there with it.
- **Noise.** `GetRngValue` is an oscillator's own xorshift (`OscInfo.rng`) instead of the STM32's random number
  generator. `calcNoise` (one sample, reading the Cortex-M's status register) is not built: nothing calls it.
- **The tables**, for the FM-1's resource region (`tools/split_tables.py`): the same numbers, in arrays of one
  dimension, decimal. The saw, rectangle and triangle tables (11 rows of 1024 each) are one array,
  `lxr_wave_tables`, in the order `wavetable.c` defines them, then a copy of the triangle's last row's first sample:
  the interpolation at a row's last sample reads one past it (the next row's first, past a table's last row the
  next table's first), now always inside the array. The transients' samples are `lxr_transient_data` (12 of 2205;
  the last one, given 2192 long, zeros to its end as C fills it).

## How MIFM1 plays them (`mi/mi_lxr.cc`)

As the LXR-02's mixer does: in blocks of 64 frames, each block the filter's frequency, then the voice's envelopes
(`calcAsync`), then its samples (`calcSyncBlock`). A trig starts a new block where it lands (the rest of the block
under way is dropped: the voice is retriggered there, as the LXR-02 retriggers it between two blocks), at velocity
127. The parameters are set as the LXR-02's MIDI parser sets them for a modulation (`isFloatModulator`: the knob's
0..1, its MIDI value v x 127), from the INIT kit's values (`initKit.c`), a few changed for a ready sound (see
`mi_lxr.cc`). The played note moves the oscillators as a MIDI note over `SEQ_DEFAULT_NOTE` does (MIFM1's C3 plays a
kit's pitch); its fraction goes on the main oscillator's fine tune.

The FM DRUM's WAVE knob (0.16.50): its first five sixths the first five waveforms (`wave_of`, as the parser's
`OSC_WAVE_DRUM1` takes a modulation), its last sixth `PWM`, cut in eight steps that also set the voice's `pwm`
(`CC2_PWM_1`, 4 % to 50 % of a cycle: `calcPwmBlock` subtracts from the saw the same saw that much on). Before, the
pulse width stayed at the INIT kit's 0: the two saws in phase cancel, and `PWM` played nothing but the click.
