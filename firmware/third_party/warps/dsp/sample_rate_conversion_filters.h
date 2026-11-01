// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Sample rate conversion filters.

#ifndef WARPS_DSP_SAMPLE_RATE_CONVERSION_FILTERS_H_
#define WARPS_DSP_SAMPLE_RATE_CONVERSION_FILTERS_H_

namespace warps {

// Generated with:
// 6 * scipy.signal.remez(48, [0, 0.060000 / 6, 0.5 / 6, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_UP, 6, 48> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
       4.357278576e-04, -2.297029461e-03, -4.703810602e-03, -8.774604727e-03,
      -1.433899145e-02, -2.112793398e-02, -2.853108802e-02, -3.552868193e-02,
      -4.069862931e-02, -4.228981313e-02, -3.836519645e-02, -2.700780696e-02,
      -6.569014106e-03,  2.407089704e-02,  6.526452513e-02,  1.164165703e-01,
       1.758932961e-01,  2.410483237e-01,  3.083744498e-01,  3.737697127e-01,
       4.328923682e-01,  4.815728403e-01,  5.162355916e-01,  5.342582974e-01,
    };
    return h[i];
  }
};

// Generated with:
// 1 * scipy.signal.remez(48, [0, 0.060000 / 6, 0.5 / 6, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_DOWN, 6, 48> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
       7.262130960e-05, -3.828382434e-04, -7.839684337e-04, -1.462434121e-03,
      -2.389831909e-03, -3.521322331e-03, -4.755181337e-03, -5.921446989e-03,
      -6.783104885e-03, -7.048302188e-03, -6.394199409e-03, -4.501301159e-03,
      -1.094835684e-03,  4.011816173e-03,  1.087742085e-02,  1.940276171e-02,
       2.931554935e-02,  4.017472062e-02,  5.139574163e-02,  6.229495212e-02,
       7.214872804e-02,  8.026214006e-02,  8.603926526e-02,  8.904304957e-02,
    };
    return h[i];
  }
};

// Generated with:
// 4 * scipy.signal.remez(48, [0, 0.105000 / 4, 0.5 / 4, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_UP, 4, 48> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
      -6.014371929e-04, -1.116027480e-03, -1.547569918e-03, -1.288608084e-03,
       2.786886230e-04,  3.529342828e-03,  8.203156385e-03,  1.308970614e-02,
       1.600199910e-02,  1.419074690e-02,  5.231038872e-03, -1.177915684e-02,
      -3.506738553e-02, -5.953252182e-02, -7.699933415e-02, -7.757902368e-02,
      -5.198496872e-02,  5.703716839e-03,  9.559598586e-02,  2.106660616e-01,
       3.371310483e-01,  4.566603688e-01,  5.500087786e-01,  6.012053946e-01,
    };
    return h[i];
  }
};

// Generated with:
// 1 * scipy.signal.remez(48, [0, 0.105000 / 4, 0.5 / 4, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_DOWN, 4, 48> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
      -1.503592982e-04, -2.790068700e-04, -3.868924795e-04, -3.221520211e-04,
       6.967215575e-05,  8.823357070e-04,  2.050789096e-03,  3.272426536e-03,
       4.000499774e-03,  3.547686724e-03,  1.307759718e-03, -2.944789209e-03,
      -8.766846381e-03, -1.488313045e-02, -1.924983354e-02, -1.939475592e-02,
      -1.299624218e-02,  1.425929210e-03,  2.389899647e-02,  5.266651541e-02,
       8.428276207e-02,  1.141650922e-01,  1.375021946e-01,  1.503013486e-01,
    };
    return h[i];
  }
};

// Generated with:
// 3 * scipy.signal.remez(36, [0, 0.050000 / 3, 0.5 / 3, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_UP, 3, 36> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
       2.111177486e-04,  9.399136027e-04,  2.516356933e-03,  4.847507152e-03,
       6.912087023e-03,  6.524576194e-03,  8.579855461e-04, -1.203466052e-02,
      -3.103696515e-02, -5.013495031e-02, -5.827142630e-02, -4.183809689e-02,
       1.038391226e-02,  1.014554664e-01,  2.222529437e-01,  3.515426263e-01,
       4.610075226e-01,  5.238640837e-01,
    };
    return h[i];
  }
};

// Generated with:
// 1 * scipy.signal.remez(36, [0, 0.050000 / 3, 0.5 / 3, 0.5], [1, 0])
template<>
struct SRC_FIR<SRC_DOWN, 3, 36> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
       7.037258286e-05,  3.133045342e-04,  8.387856444e-04,  1.615835717e-03,
       2.304029008e-03,  2.174858731e-03,  2.859951820e-04, -4.011553507e-03,
      -1.034565505e-02, -1.671165010e-02, -1.942380877e-02, -1.394603230e-02,
       3.461304086e-03,  3.381848881e-02,  7.408431457e-02,  1.171808754e-01,
       1.536691742e-01,  1.746213612e-01,
    };
    return h[i];
  }
};

// MIFM1: for 44.1 kHz (tools/warps_tables.py), with:
// 6 * scipy.signal.remez(72, [0, 12000 / (6 * 44100), 0.5 / 6, 0.5], [1, 0], fs=1.0)
template<>
struct SRC_FIR<SRC_UP, 6, 72> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
       1.097761854e-02,  6.909772188e-03,  7.067540735e-03,  5.435723468e-03,
       1.766704886e-03, -3.759710097e-03, -1.047662110e-02, -1.725446636e-02,
      -2.259846159e-02, -2.490636496e-02, -2.285622018e-02, -1.563569784e-02,
      -3.395026684e-03,  1.267778667e-02,  3.029719902e-02,  4.632197081e-02,
       5.718171337e-02,  5.950207418e-02,  5.079270371e-02,  3.011131244e-02,
      -1.405927630e-03, -4.035808858e-02, -8.117783437e-02, -1.166882099e-01,
      -1.389607974e-01, -1.404494291e-01, -1.151866100e-01, -5.990040201e-02,
       2.517263814e-02,  1.359229775e-01,  2.645215422e-01,  4.001631532e-01,
       5.302467074e-01,  6.419124988e-01,  7.236566026e-01,  7.668444505e-01,
    };
    return h[i];
  }
};

// MIFM1: for 44.1 kHz (tools/warps_tables.py), with:
// 1 * scipy.signal.remez(72, [0, 12000 / (6 * 44100), 0.5 / 6, 0.5], [1, 0], fs=1.0)
template<>
struct SRC_FIR<SRC_DOWN, 6, 72> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
       1.829603090e-03,  1.151628698e-03,  1.177923456e-03,  9.059539113e-04,
       2.944508143e-04, -6.266183496e-04, -1.746103517e-03, -2.875744394e-03,
      -3.766410264e-03, -4.151060826e-03, -3.809370031e-03, -2.605949640e-03,
      -5.658377807e-04,  2.112964445e-03,  5.049533170e-03,  7.720328469e-03,
       9.530285562e-03,  9.917012363e-03,  8.465450619e-03,  5.018552074e-03,
      -2.343212717e-04, -6.726348096e-03, -1.352963906e-02, -1.944803499e-02,
      -2.316013290e-02, -2.340823818e-02, -1.919776833e-02, -9.983400335e-03,
       4.195439690e-03,  2.265382959e-02,  4.408692371e-02,  6.669385887e-02,
       8.837445123e-02,  1.069854165e-01,  1.206094338e-01,  1.278074084e-01,
    };
    return h[i];
  }
};

// MIFM1: ECO, 3x at 44.1 kHz (tools/warps_eco.py), with:
// 3 * scipy.signal.remez(33, [0, 12000 / (3 * 44100), 0.5 / 3, 0.5], [1, 0], fs=1.0)
template<>
struct SRC_FIR<SRC_UP, 3, 33> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
       6.847375034e-03, -6.962151387e-03, -1.660930693e-02, -2.080082585e-02,
      -9.170629041e-03,  1.891275309e-02,  4.833195207e-02,  5.358900511e-02,
       1.578644684e-02, -5.776973597e-02, -1.261369368e-01, -1.293630438e-01,
      -2.138269892e-02,  1.968299772e-01,  4.655577005e-01,  6.875523212e-01,
       7.735521945e-01,
    };
    return h[i];
  }
};

// MIFM1: ECO, 3x at 44.1 kHz (tools/warps_eco.py), with:
// 1 * scipy.signal.remez(33, [0, 12000 / (3 * 44100), 0.5 / 3, 0.5], [1, 0], fs=1.0)
template<>
struct SRC_FIR<SRC_DOWN, 3, 33> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
       2.282458345e-03, -2.320717129e-03, -5.536435644e-03, -6.933608615e-03,
      -3.056876347e-03,  6.304251030e-03,  1.611065069e-02,  1.786300170e-02,
       5.262148948e-03, -1.925657866e-02, -4.204564560e-02, -4.312101461e-02,
      -7.127566308e-03,  6.560999241e-02,  1.551859002e-01,  2.291841071e-01,
       2.578507315e-01,
    };
    return h[i];
  }
};

}  // namespace warps

#endif  // WARPS_DSP_SAMPLE_RATE_CONVERSION_FILTERS_H_
