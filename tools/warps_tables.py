#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Warps' tables that depend on its sample rate, made again for the FM-1's 44.1 kHz: Emilie Gillet's own scripts
# (eurorack/warps/resources/filter_bank.py and lookup_tables.py, MIT), in Python 3, SAMPLE_RATE given; and the filters
# of the cross-modulations' 6x oversampling, by the recipe written above them. Four tables:
#   - the vocoder's filter bank: 20 bands a third of an octave apart (87 Hz to 7 kHz), two Chamberlin passes each, at
#     the sample rate decimated by 12, by 3 or not at all. At 96 kHz the groups hold 13, 6 and 1 bands, each band at
#     most 0.35 of its group's Nyquist; at 44.1 kHz the same rule makes them 9, 6 and 5 bands;
#   - the oscillators' MIDI-note-to-frequency table;
#   - the frequency shifter's allpass poles (its 90-degree pairs, 10 Hz to 20 kHz);
#   - the 6x oversampling filters (sample_rate_conversion_filters.h: remez(48, [0, 0.06 / 6, 0.5 / 6, 0.5]): flat to
#     5.8 kHz at 96 kHz, -1 dB at 16 kHz, the images, from the input's Nyquist (48 kHz), at -68 dB). The same recipe
#     at 44.1 kHz would pass 2.6 kHz and lose 17 dB at 16 kHz; the 44.1 kHz pair is 72 taps, flat to 12 kHz (-0.0 dB),
#     -3.5 dB at 16 kHz, its images (from 22.05 kHz) at -52 dB, and the modulator takes it (modulator.h: 48 -> 72).
# Run with --check: the 96 kHz tables made by this port, against the vendored resources.cc (the port is the script).
#   tools/warps_tables.py [--check] [warps/]   (default: firmware/third_party/warps/: resources.cc,
#   dsp/sample_rate_conversion_filters.h and dsp/modulator.h rewritten)
import os
import re
import sys

try:                                     # (only to make the tables: patch_mi.sh on tables already made needs neither)
    import numpy as np
    import scipy.signal
except ImportError:
    np = None


def pole_pair_to_f_fq(pole_pair):
    fq = 1 - pole_pair.prod()
    f = -(2 - fq - (pole_pair.sum())) ** 0.5
    return f.real, fq.real


def modified_chamberlin(f, fq, x, mode='bp'):
    lp = 0.0
    bp = 0.0
    y = np.zeros(x.shape)
    x_ = 0.0
    coefficient = 1.0 if mode == 'bp' else 0.0
    for i in range(len(y)):
        lp += f * bp
        bp += -fq * bp - f * lp + (x[i] + x_ * coefficient)
        x_ = x[i]
        if mode == 'bp':
            y[i] = fq * bp
        elif mode == 'lp':
            y[i] = f * lp
        elif mode == 'hp':
            y[i] = x_ - lp * f - bp * fq
    return y


def hp_level(sample_rate, frequency):
    # the last band's pass band, as built (filter_bank.py's gain), from 1.3 x its corner to 19 kHz
    w = frequency / (sample_rate * 0.5)
    z, p, k = scipy.signal.cheby1(4, 0.25, w, 'highpass', output='zpk')
    x = np.eye(4096, 1).ravel() * 21 * w
    for q in sorted([q for q in p if q.imag > 0], key=lambda q: -np.angle(q)):
        f, fq = pole_pair_to_f_fq(np.array([q, np.conj(q)]))
        x = modified_chamberlin(f, fq, modified_chamberlin(f, fq, x, 'hp'), 'hp')
    h = np.abs(np.fft.rfft(x))
    hz = np.arange(len(h)) / 4096.0 * sample_rate
    return h[(hz >= 1.3 * frequency) & (hz <= 19000)].mean()


def filter_bank(sample_rate):
    IR_SIZE = 2048
    num_bands = 20
    interval = 2 ** (1 / 3.0)
    first_frequency = 110 / interval
    frequencies = first_frequency * (interval ** np.arange(0, num_bands))
    # the groups: a band goes to the most decimated group whose Nyquist it stays under 0.35 of (filter_bank.py: 13 /
    # 6 / 1 at 96 kHz, written out there)
    sample_rates = []
    for f in frequencies:
        for decimation in (12, 3, 1):
            if f <= 0.35 * sample_rate / decimation / 2 or decimation == 1:
                break
        sample_rates.append(sample_rate // decimation if sample_rate % decimation == 0 else sample_rate / decimation)
    sample_rates[-1] = sample_rate          # the last band, the high pass: never decimated
    filters = []
    for index, (frequency, sr) in enumerate(zip(frequencies, sample_rates)):
        frequency = frequency / (sr * 0.5)
        if index == 0:
            z, p, k = scipy.signal.cheby1(4, 0.5, frequency, 'lowpass', output='zpk')
            svf_mode, gain = 'lp', 1.0
        elif index == num_bands - 1:
            z, p, k = scipy.signal.cheby1(4, 0.25, frequency, 'highpass', output='zpk')
            svf_mode, gain = 'hp', 21 * frequency
        else:
            w = [frequency / (interval ** 0.5), frequency * (interval ** 0.5)]
            z, p, k = scipy.signal.butter(2, w, 'bandpass', output='zpk')
            svf_mode, gain = 'bp', 0.25
        out = np.eye(IR_SIZE, 1).ravel() * gain
        coefficients = [0, 0, 0]
        # the poles as two conjugate pairs, the higher one first (the order the script's scipy gave them for most
        # bands; two cascaded sections give the same response either way). Today's scipy lists them otherwise
        upper = sorted([q for q in p if q.imag > 0], key=lambda q: -np.angle(q))
        pairs = [np.array([q, np.conj(q)]) for q in upper]
        for i in range(2):
            f, fq = pole_pair_to_f_fq(pairs[i])
            out = modified_chamberlin(f, fq, out, svf_mode)
            out = modified_chamberlin(f, fq, out, svf_mode)
            coefficients += [f, fq]
        if index == num_bands - 1 and sample_rate != 96000:
            # the high pass's gain: filter_bank.py's 21 x its frequency is a fit at 96 kHz (a pass band ~1.04); at
            # another rate it is set to give the same pass band, measured against the 96 kHz design
            gain *= hp_level(96000, frequencies[-1]) / hp_level(sample_rate, frequencies[-1])
            out = np.eye(IR_SIZE, 1).ravel() * gain
            for i in range(2):
                out = modified_chamberlin(coefficients[3 + 2 * i], coefficients[4 + 2 * i], out, svf_mode)
                out = modified_chamberlin(coefficients[3 + 2 * i], coefficients[4 + 2 * i], out, svf_mode)
        delay = (np.arange(len(out)) * out * out).sum() / (out * out).sum()
        if index == num_bands - 1:
            delay += 4 * sample_rate / 96000.0   # filter_bank.py's empirical 4 samples at 96 kHz, as a time
        coefficients[0] = round(sample_rate / sr)
        coefficients[1] = np.floor(delay)
        coefficients[2] = gain
        filters.append(('%3.0f_%d' % (frequency * 0.5 * sr, round(sr)), coefficients))
    return filters


def midi_to_f_high(sample_rate):
    midi_note = np.arange(0, 256) - 48
    frequency = 440 * 2 ** ((midi_note - 69) / 12.0)
    max_frequency = min(12000, sample_rate / 2)
    frequency[frequency >= max_frequency] = max_frequency
    return frequency / sample_rate


def iir_lpf_poles(cutoff, order, warp=True):
    wp = cutoff * np.pi
    ws = np.pi - wp
    k = np.tan(0.5 * wp) / np.tan(0.5 * ws) if warp else wp / ws
    u = 0.5 * (1 - (1 - k ** 2) ** 0.25) / (1 + (1 - k ** 2) ** 0.25)
    q = u + 2 * u ** 5 + 15 * u ** 9 + 150 * u ** 13
    poles = []
    for i in range((order - 1) // 2):
        w = (i + 1) * np.pi / order
        num = sum(((-1) ** m) * q ** (m * (m + 1)) * np.sin((2 * m + 1) * w) for m in range(0, 7))
        den = sum(((-1) ** m) * q ** (m * m) * np.cos(2 * m * w) for m in range(1, 7))
        l = 2 * q ** 0.25 * num / (1 + 2 * den)
        b = ((1 - k * l ** 2) * (1 - l ** 2 / k)) ** 0.5
        c = 2 * b / (1 + l ** 2)
        poles += [(2 - c) / (2 + c)]
    return poles


def warped_lp_to_ap(lp_poles, band):
    w = np.pi * band
    beta = (np.tan(w[0]) * np.tan(w[1])) ** 0.5
    b = (beta - 1) / (beta + 1)
    ap_poles = []
    for pole in lp_poles:
        pole = np.sqrt(pole)
        conjugates = [-pole, pole] if pole != 0.0 else [pole]
        ap_poles += [(-c + b) / (-c * b + 1) for c in conjugates]
    return -np.sort(ap_poles)


def ap_poles(sample_rate):
    return warped_lp_to_ap(iir_lpf_poles(0.495, 17) + [0.0], np.array([10.0, 20000.0]) / sample_rate)


def src6(taps, passband, sample_rate):   # the 6x oversampling pair: (up, down), each its first half (they mirror)
    h = scipy.signal.remez(taps, [0, passband / (6.0 * sample_rate), 0.5 / 6, 0.5], [1, 0], fs=1.0)
    return 6 * h[:taps // 2], h[:taps // 2]


def src6_code(taps, passband, sample_rate):
    up, down = src6(taps, passband, sample_rate)
    out = []
    for kind, h, gain in (('SRC_UP', up, 6), ('SRC_DOWN', down, 1)):
        out.append('''// MIFM1: for 44.1 kHz (tools/warps_tables.py), with:
// %d * scipy.signal.remez(%d, [0, %.0f / (6 * %d), 0.5 / 6, 0.5], [1, 0], fs=1.0)
template<>
struct SRC_FIR<%s, 6, %d> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
%s
    };
    return h[i];
  }
};
''' % (gain, taps, passband, sample_rate, kind, taps, '\n'.join('    ' + l for l in c_floats(h).split('\n'))))
    return '\n'.join(out)


def c_floats(values, per_line=4):
    s = ['%16.9e' % v for v in values]
    lines = [', '.join(s[i:i + per_line]) for i in range(0, len(s), per_line)]
    return '\n'.join('  ' + l + ',' for l in lines)


def read_array(text, name):
    m = re.search(r'const float ' + re.escape(name) + r'\[\] = \{([^}]*)\}', text)
    return np.array([float(x.strip().rstrip('f')) for x in m.group(1).split(',') if x.strip()])


def check_src(filters):
    worst = 0.0
    for kind, gain in (('SRC_UP', 6), ('SRC_DOWN', 1)):
        m = re.search(r'struct SRC_FIR<' + kind + r', 6, 48> \{.*?const float h\[\] = \{([^}]*)\}', filters, re.S)
        theirs = np.array([float(x) for x in m.group(1).split(',') if x.strip()])
        mine = src6(48, 0.06 * 96000, 96000)[0 if kind == 'SRC_UP' else 1]
        worst = max(worst, np.max(np.abs(mine - theirs)))
    print('96 kHz 6x oversampling from the recipe against sample_rate_conversion_filters.h: %.2g' % worst)
    return worst < 1e-3                  # (remez converges a little differently from 2014's scipy)


def check(text):
    worst = 0.0
    old_names = re.findall(r'const float (fb_\w+)\[\]', text)
    for (name, coefficients), old in zip(filter_bank(96000), old_names):
        mine, theirs = np.array(coefficients, dtype=float), read_array(text, old)
        swapped = np.concatenate([mine[:3], mine[5:7], mine[3:5]])   # the sections in the other order
        worst = max(worst, min(np.max(np.abs(mine - theirs)), np.max(np.abs(swapped - theirs))))
    worst_f = np.max(np.abs(midi_to_f_high(96000) - read_array(text, 'lut_midi_to_f_high')) /
                     read_array(text, 'lut_midi_to_f_high'))
    worst_ap = np.max(np.abs(ap_poles(96000) - read_array(text, 'lut_ap_poles')))
    print('96 kHz from the port against resources.cc: filter bank %.2g, midi_to_f %.2g (relative), ap_poles %.2g' %
          (worst, worst_f, worst_ap))
    return worst < 1e-6 and worst_f < 1e-6 and worst_ap < 1e-6


def rewrite(text, sample_rate):
    old_names = re.findall(r'const float (fb_\w+)\[\]', text)
    for (name, coefficients), old in zip(filter_bank(sample_rate), old_names):
        # the arrays keep their names (resources.h declares them; filter_bank_table lists them): the values are new
        text = re.sub(r'const float ' + old + r'\[\] = \{[^}]*\}',
                      'const float %s[] = {  // MIFM1: %s at %d Hz\n%s\n}' % (old, name, sample_rate,
                                                                          c_floats(coefficients)), text)
    text = re.sub(r'const float lut_midi_to_f_high\[\] = \{[^}]*\}',
                  'const float lut_midi_to_f_high[] = {  // MIFM1: at %d Hz\n%s\n}' %
                  (sample_rate, c_floats(midi_to_f_high(sample_rate))), text)
    text = re.sub(r'const float lut_ap_poles\[\] = \{[^}]*\}',
                  'const float lut_ap_poles[] = {  // MIFM1: at %d Hz\n%s\n}' %
                  (sample_rate, c_floats(ap_poles(sample_rate))), text)
    return text


SRC_TAPS, SRC_PASS = 72, 12000.0


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    root = args[0] if args else os.path.join(os.path.dirname(__file__), '..', 'firmware', 'third_party', 'warps')
    paths = [os.path.join(root, p) for p in ('resources.cc', 'dsp/sample_rate_conversion_filters.h',
                                              'dsp/modulator.h')]
    text, filters, modulator = (open(p).read() for p in paths)
    done = ['MIFM1: at 44100 Hz' in text, 'MIFM1: for 44.1 kHz' in filters, 'MIFM1: 72' in modulator]
    if np is None and ('--check' in sys.argv or not all(done)):
        print('warps_tables: needs numpy and scipy')
        sys.exit(1)
    if '--check' in sys.argv:            # (on the vendored files: resources.cc once rewritten holds 44.1 kHz's)
        ok = check(text) if not done[0] else print('resources.cc already at 44.1 kHz: not checked') or True
        sys.exit(0 if ok and check_src(filters) else 1)
    if all(done):
        print('warps_tables: %s already at 44.1 kHz' % root)
        sys.exit(0)
    if not done[0]:
        if not check(text):
            print('warps_tables: the port does not make the vendored tables at 96 kHz: stopped')
            sys.exit(1)
        open(paths[0], 'w').write(rewrite(text, 44100))
        for name, c in filter_bank(44100):
            print('  band %s: decimated by %d' % (name, c[0]))
    if not done[1]:
        if not check_src(filters):
            print('warps_tables: the recipe does not make the vendored 6x filters at 96 kHz: stopped')
            sys.exit(1)
        end = filters.rindex('}  // namespace warps')
        open(paths[1], 'w').write(filters[:end] + src6_code(SRC_TAPS, SRC_PASS, 44100) + '\n' + filters[end:])
    if not done[2]:
        for kind in ('UP', 'DOWN'):
            line = 'SampleRateConverter<SRC_%s, kOversampling, 48>' % kind
            if line not in modulator:
                print('warps_tables: modulator.h: %s not found' % line)
                sys.exit(1)
            modulator = modulator.replace(line, 'SampleRateConverter<SRC_%s, kOversampling, %d>' % (kind, SRC_TAPS))
        modulator = modulator.replace('  SampleRateConverter<SRC_DOWN, kOversampling, %d> src_down_;' % SRC_TAPS,
                                      '  SampleRateConverter<SRC_DOWN, kOversampling, %d> src_down_;  '
                                      '// MIFM1: 72 taps (were 48), the 44.1 kHz pair' % SRC_TAPS)
        open(paths[2], 'w').write(modulator)
