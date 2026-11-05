// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 on a computer: the engine (mi/engine.cc) plays its default patch, as the FM-1 would at power-on, and is
// checked: every track sounds, the steps fall on the tempo, the sources stay in range, no NaN, the keyboard plays a
// stopped machine, a project's values are written as the screen writes them, the effects' lanes open and close them.
// WAVs in the folder given.
//   build/native build/wav [seconds]
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
extern "C" {
#include "plat.h"
}
#include "mi/engine.h"
#include "mi/bridge.h"
#include "mi/res_tables.h"
#include "Soundengine/DSPAudio/SlopeEg2.h"
extern "C" int mi_res_check(const uint8_t* payload, uint32_t len);

extern "C" {
uint32_t plat_ticks24(void) {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<uint32_t>(static_cast<uint64_t>(t.tv_sec) * 24000000u + t.tv_nsec / 1000u * 24u);
}
static int cpu2_sim = 1;                 /* the second core, as fm1-base's simulator has it: a job runs at once, in order
                                          * (MIFM1_ONE_CORE=1: none; MIFM1_LAZY=1: it runs when waited for, a late core.
                                          * The sound is the same, but for the order the voices draw from Plaits' one
                                          * random generator in: it follows the costs measured) */
int plat_cpu2_on(void) { return cpu2_sim; }
static void (*lazy_fn)(uint32_t);
static uint32_t lazy_arg;
static int lazy = 0;
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t a) {
  if (!cpu2_sim) return 0;
  if (lazy) { lazy_fn = fn; lazy_arg = a; return 1; }
  fn(a);
  return 1;
}
int plat_cpu2_wait(void) { if (lazy && lazy_fn) { void (*f)(uint32_t) = lazy_fn; lazy_fn = 0; f(lazy_arg); } return 0; }
int plat_cpu2_busy(void) { return 0; }
void plat_cpu2_sync(void) {}
}

static void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

struct Wav {
  FILE* f = nullptr;
  uint32_t frames = 0;
  bool open(const char* path, uint32_t total) {
    f = fopen(path, "wb");
    if (!f) return false;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + total * 4); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, total * 4);
    return true;
  }
  void add(const float* l, const float* r, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
      put16(f, static_cast<uint16_t>(static_cast<int16_t>(std::fmax(-1.0f, std::fmin(1.0f, l[i])) * 32767.0f)));
      put16(f, static_cast<uint16_t>(static_cast<int16_t>(std::fmax(-1.0f, std::fmin(1.0f, r[i])) * 32767.0f)));
    }
  }
  void close() { if (f) fclose(f); }
};

// the locks a project had before 0.16.7 (384; 380 now): a project's of now into an old layout's, the last free
#define OLD_LOCKS 384
static void old_locks(plock_t* o, const plock_t* q) {
  for (int i = 0; i < OLD_LOCKS; ++i) o[i] = i < ENG_PLOCKS ? q[i] : plock_t{0xFF, 0, 0, 0};
}

// a project of now, written as 0.7 to 0.15 kept theirs (versions 15 to 23: 41 parameters a lane, no SPEED, DIRECTION or
// CURVE, 3776 bytes), its version as the test set it: what eng_upgrade reads from an older FM-1 or a backup
struct V23 {
  uint32_t magic;
  uint16_t version, size;
  float val[ENG_NPARAMS];
  uint8_t algo[ENG_NPAGES];
  uint8_t out_mode, warps_hq, theme, dice, dice_seed;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][41];
  uint16_t seed[ENG_LANES];
  plock_t lock[OLD_LOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
  uint8_t rseed[ENG_LANES][41];
  uint8_t trauma;
};
static_assert(sizeof(V23) == PROJECT_SIZE_22, "0.15's layout");
static project_t* as_v23(project_t* q) {
  static V23 o;
  memset(&o, 0, sizeof o);
  o.magic = q->magic, o.version = q->version, o.size = PROJECT_SIZE_22;
  memcpy(o.val, q->val, sizeof o.val);
  memcpy(o.algo, q->algo, sizeof o.algo);
  o.out_mode = q->out_mode, o.warps_hq = q->warps_hq, o.theme = q->theme, o.dice = q->dice, o.dice_seed = q->dice_seed;
  memcpy(o.cable, q->cable, sizeof o.cable);
  memcpy(o.trig, q->trig, sizeof o.trig);
  for (int l = 0; l < ENG_LANES; ++l)
    for (int i = 0; i < 41; ++i) o.rnd[l][i] = q->rnd[l][i], o.rseed[l][i] = q->rseed[l][i];
  memcpy(o.seed, q->seed, sizeof o.seed);
  old_locks(o.lock, q->lock);
  memcpy(o.endless, q->endless, sizeof o.endless);
  memcpy(o.endless_len, q->endless_len, sizeof o.endless_len);
  memcpy(o.endless_key, q->endless_key, sizeof o.endless_key);
  o.trauma = q->trauma;
  memset(q, 0xA5, sizeof *q);
  memcpy(q, &o, sizeof o);
  return q;
}

// a project of now, written as 0.16.0 to 0.16.6 kept theirs (versions 24 to 26: 42 parameters a lane, 384 locks, no
// CLIMB; 3820 bytes, 0.16.0's 3804 without Endless' options), its version as the test set it
struct V26 {
  uint32_t magic;
  uint16_t version, size;
  float val[ENG_NPARAMS];
  uint8_t algo[ENG_NPAGES];
  uint8_t out_mode, warps_hq, theme, dice, dice_seed;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][42];
  uint16_t seed[ENG_LANES];
  plock_t lock[OLD_LOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
  uint8_t rseed[ENG_LANES][42];
  uint8_t trauma;
  int8_t speed[ENG_LANES];
  uint8_t dir[ENG_LANES];
  uint8_t endless_opt[ENG_TRACKS][4];
};
static_assert(sizeof(V26) == PROJECT_SIZE_26 && sizeof(V26) <= sizeof(project_t), "0.16.6's layout");
static int as_v26(project_t* q, int version) {   // (its bytes: what eng_upgrade is given)
  static V26 o;
  memset(&o, 0, sizeof o);
  o.magic = q->magic, o.version = static_cast<uint16_t>(version);
  o.size = static_cast<uint16_t>(version == 24 ? PROJECT_SIZE_24 : PROJECT_SIZE_26);
  memcpy(o.val, q->val, sizeof o.val);
  memcpy(o.algo, q->algo, sizeof o.algo);
  o.out_mode = q->out_mode, o.warps_hq = q->warps_hq, o.theme = q->theme, o.dice = q->dice, o.dice_seed = q->dice_seed;
  memcpy(o.cable, q->cable, sizeof o.cable);
  memcpy(o.trig, q->trig, sizeof o.trig);
  for (int l = 0; l < ENG_LANES; ++l)
    for (int i = 0; i < 42; ++i) o.rnd[l][i] = q->rnd[l][i], o.rseed[l][i] = q->rseed[l][i];
  memcpy(o.seed, q->seed, sizeof o.seed);
  old_locks(o.lock, q->lock);
  memcpy(o.endless, q->endless, sizeof o.endless);
  memcpy(o.endless_len, q->endless_len, sizeof o.endless_len);
  memcpy(o.endless_key, q->endless_key, sizeof o.endless_key);
  o.trauma = q->trauma;
  memcpy(o.speed, q->speed, sizeof o.speed);
  memcpy(o.dir, q->dir, sizeof o.dir);
  memcpy(o.endless_opt, q->endless_opt, sizeof o.endless_opt);
  memset(q, 0xA5, sizeof *q);
  memcpy(q, &o, sizeof o);
  return o.size;
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  if (getenv("MIFM1_ONE_CORE")) cpu2_sim = 0;
  if (getenv("MIFM1_LAZY")) lazy = 1;
  float seconds = argc > 2 ? static_cast<float>(atof(argv[2])) : 16.0f;
  const uint32_t n = 128, blocks = static_cast<uint32_t>(seconds * 44100.0f / n);
  int failures = 0;
  {  // the resource region's file (firmware/res/mi_tables.bin, what the FM-1 reads) against the arrays it replaces
    FILE* rf = fopen("firmware/res/mi_tables.bin", "rb");
    static uint8_t region[MIFM1_RES_LEN + 1];
    size_t got = rf ? fread(region, 1, sizeof region, rf) : 0;
    if (rf) fclose(rf);
    int k = mi_res_check(region, static_cast<uint32_t>(got));
    printf("resource region: %u bytes, %d tables: %s\n", static_cast<unsigned>(got), MIFM1_RES_TABLES,
           k == -1 ? "the same bytes as the arrays" : "DIFFERENT (run tools/split_tables.py)");
    if (k != -1) ++failures;
  }
  if (eng_init() != 0) {
    printf("FAIL: the engine does not fit its memory\n");
    return 1;
  }
  printf("project: %u bytes\n", static_cast<unsigned>(sizeof(project_t)));

  // 1. the default patch, playing
  char path[512];
  snprintf(path, sizeof path, "%s/default.wav", dir);
  Wav w;
  if (!w.open(path, blocks * n)) { perror(path); return 1; }
  eng_play(1);
  float l[128], r[128];
  uint32_t rises[ENG_TRACKS] = {0}, prev = 0;
  double energy = 0.0;
  float peak = 0.0f, smin[NSRC], smax[NSRC];
  for (int s = 0; s < NSRC; ++s) smin[s] = 1e9f, smax[s] = -1e9f;
  int bad = 0, notes_lo = 999, notes_hi = -999;
  double t0 = static_cast<double>(clock());
  for (uint32_t b = 0; b < blocks; ++b) {
    eng_render(l, r, n);
    for (uint32_t i = 0; i < n; ++i) {
      if (!std::isfinite(l[i]) || !std::isfinite(r[i])) bad = 1;
      energy += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
      peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
    }
    uint32_t g = eng_gates();
    for (int t = 0; t < ENG_TRACKS; ++t) {
      if ((g & (1u << t)) && !(prev & (1u << t))) {
        ++rises[t];
        int note = eng_note(t);
        if (note < notes_lo) notes_lo = note;
        if (note > notes_hi) notes_hi = note;
      }
    }
    prev = g;
    for (int s = 1; s < NSRC; ++s) {
      smin[s] = std::fmin(smin[s], eng_src(s));
      smax[s] = std::fmax(smax[s], eng_src(s));
    }
    w.add(l, r, n);
  }
  w.close();
  double us = (static_cast<double>(clock()) - t0) / CLOCKS_PER_SEC * 1e6 / blocks;
  double rms = std::sqrt(energy / (2.0 * blocks * n));
  printf("default patch, %.0f s: rms %.3f, peak %.3f, %.1f us a block on this computer, notes %d..%d%s\n", seconds,
         rms, peak, us, notes_lo, notes_hi, bad ? ", NaN" : "");
  printf("gates per track:");
  for (int t = 0; t < ENG_TRACKS; ++t) printf(" %u", rises[t]);
  printf("  (track 1: %0.f at 120 BPM expected, a beat each and a ratchet a bar)\n", seconds * 2.5f);
  if (bad || rms < 0.01) ++failures;
  if (rises[0] < seconds * 2.5f * 0.95f || rises[0] > seconds * 2.5f * 1.05f + 1) {
    printf("FAIL: track 1 does not play its steps at 120 BPM\n");
    ++failures;
  }
  for (int t = 1; t < ENG_TRACKS; ++t) {
    if (!rises[t]) {
      printf("FAIL: track %d never plays\n", t + 1);
      ++failures;
    }
  }
  printf("sources (min..max):");
  for (int s = 1; s < NSRC; ++s) printf(" %s %.2f..%.2f", eng_src_name(s), smin[s], smax[s]);
  printf("\n");
  for (int s = SRC_L1; s <= SRC_L3; ++s) {
    if (smax[s] - smin[s] < 0.05f) {
      printf("FAIL: %s does not move\n", eng_src_name(s));
      ++failures;
    }
  }

  // 2. stopped: the keyboard plays track 2
  eng_play(0);
  for (int b = 0; b < 200; ++b) eng_render(l, r, n);   // the tails die away
  eng_key(1, 60, 1);
  double ke = 0.0;
  for (int b = 0; b < 100; ++b) {
    eng_render(l, r, n);
    for (uint32_t i = 0; i < n; ++i) ke += static_cast<double>(l[i]) * l[i];
  }
  eng_key(1, 60, 0);
  int kn = eng_note(1), koct = static_cast<int>(std::lround(eng_project()->val[PG_TRACK(1, 2) * 4 + 3]));
  printf("keyboard, stopped: rms %.3f, note %d (OCTAVE %d)\n", std::sqrt(ke / (100.0 * n)), kn, koct);
  if (ke < 1e-3 || kn != 60 + 12 * koct) {
    printf("FAIL: the keyboard does not play track 2\n");
    ++failures;
  }
  {                                     // the track's OCTAVE moves the keyboard's notes too (as TRANSP does)
    float* oct = &eng_project()->val[PG_TRACK(1, 2) * 4 + 3];
    float was = *oct;
    *oct = 1.0f;
    eng_key(1, 60, 1);
    for (int b = 0; b < 4; ++b) eng_render(l, r, n);
    int up = eng_note(1);
    eng_key(1, 60, 0);
    *oct = -2.0f;
    eng_key(1, 60, 1);
    for (int b = 0; b < 4; ++b) eng_render(l, r, n);
    int down = eng_note(1);
    eng_key(1, 60, 0);
    *oct = was;
    for (int b = 0; b < 200; ++b) eng_render(l, r, n);
    printf("keyboard, OCTAVE +1: note %d, -2: note %d\n", up, down);
    if (up != 72 || down != 36) {
      printf("FAIL: the track's OCTAVE does not move the keyboard\n");
      ++failures;
    }
  }

  // 3. every model on track 1, a second each
  project_t* p = eng_project();
  for (int m = 0; m < eng_algo_count(PG_TRACK(0, 0)); ++m) {
    p->algo[PG_TRACK(0, 0)] = static_cast<uint8_t>(m);
    eng_key(0, 48, 1);
    double e = 0.0;
    int nan = 0;
    for (int b = 0; b < 344; ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        if (!std::isfinite(l[i])) nan = 1;
        e += static_cast<double>(l[i]) * l[i];
      }
      if (b == 200) eng_key(0, 48, 0);
    }
    if (nan || e < 1e-4) {
      printf("FAIL: model %s: %s\n", eng_algo_name(PG_TRACK(0, 0), m), nan ? "NaN" : "silent");
      ++failures;
    }
  }
  printf("track 1, the %d models: played\n", eng_algo_count(PG_TRACK(0, 0)));

  // 4. the values as the screen writes them
  char t[24];
  eng_param_text(PG_GLOBAL * 4 + 0, p->val[PG_GLOBAL * 4 + 0], t, sizeof t);
  printf("TEMPO %s", t);
  if (strcmp(t, "120 BPM")) ++failures, printf(" (FAIL: 120 BPM expected)");
  eng_param_text(PG_TRACK(0, 0) * 4 + 0, 0.5f, t, sizeof t);
  printf(", FREQUENCY +0.5 -> %s", t);
  if (strcmp(t, "+12.0")) ++failures, printf(" (FAIL: +12.0 expected)");
  eng_param_text(PG_LFO(1) * 4 + 1, p->val[PG_LFO(1) * 4 + 1], t, sizeof t);
  printf(", LFO 2 (synced) %s", t);
  if (strcmp(t, "1 / bar")) ++failures, printf(" (FAIL: 1 / bar expected)");
  p->val[PG_LFO(0) * 4 + 2] = static_cast<float>(LM_FREE);
  eng_param_text(PG_LFO(0) * 4 + 1, 0.0f, t, sizeof t);
  printf(", LFO 1 at 0: %s\n", t);
  if (strcmp(t, "0.02 Hz")) ++failures, printf(" (FAIL: 0.02 Hz expected)\n");
  {
    static const struct { int param; float v; const char* want; } kText[] = {
        {PG_DELAY(0) * 4 + 0, 7.0f, "1/8D"},    {PG_DELAY(0) * 4 + 2, -0.2f, "LP 20"}, {PG_DELAY(0) * 4 + 2, 0.0f, "Flat"},
        {PG_DELAY(1) * 4 + 1, 1.0f, "Ping-pong"}, {PG_GLOBAL * 4 + 3, 1.0f, "75%"},  {PG_GLOBAL * 4 + 3, 0.0f, "50%"},
        {PG_STEP(LANE_DELAY) * 4 + 0, 0.0f, "10 ms"}, {PG_STEP(LANE_REVERB) * 4 + 0, 1.0f, "5.1 s"},
        {PG_TRACK(0, 2) * 4 + 1, 1.0f, "95%"},   {PG_LFO(0) * 4 + 2, 5.0f, "T4"}, {PG_LFO(2) * 4 + 3, 0.0f, "Off"},
        {PG_WARPS(0) * 4 + 3, 1.0f, "100%"},     {PG_WARPS(1) * 4 + 3, 1.0f, "+ Dry"}};
    for (const auto& k : kText) {
      eng_param_text(k.param, k.v, t, sizeof t);
      if (strcmp(t, k.want)) ++failures, printf("FAIL: %s at %.2f written %s, %s expected\n", eng_pdef(k.param)->name, k.v, t, k.want);
    }
    printf("the delay's, the swing's, the lanes' values: written as expected\n");
  }

  // 5. INIT: four tracks on the FM, nothing patched, no steps; a step on each, each track plays
  eng_blank(p);
  int cables = 0, models = 0, steps = 0;
  for (int l = 0; l < ENG_LANES; ++l) steps += eng_lane_has_trigs(l);
  for (int tr = 0; tr < ENG_TRACKS; ++tr) eng_trig_set(tr, tr * 4, 1);
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) cables += p->cable[g][c].depth != 0;
  for (int tr = 0; tr < ENG_TRACKS; ++tr) models += p->algo[PG_TRACK(tr, 0)] == eng_model_find(MK_FM, 0);
  snprintf(path, sizeof path, "%s/init.wav", dir);
  Wav wi;
  const uint32_t iblocks = 8u * 44100u / n;
  if (!wi.open(path, iblocks * n)) { perror(path); return 1; }
  eng_play(0);
  eng_play(1);
  uint32_t irises[ENG_TRACKS] = {0};
  prev = 0;
  double ie = 0.0;
  int inan = 0;
  for (uint32_t b = 0; b < iblocks; ++b) {
    eng_render(l, r, n);
    for (uint32_t i = 0; i < n; ++i) {
      if (!std::isfinite(l[i])) inan = 1;
      ie += static_cast<double>(l[i]) * l[i];
    }
    uint32_t g = eng_gates();
    for (int tr = 0; tr < ENG_TRACKS; ++tr)
      if ((g & (1u << tr)) && !(prev & (1u << tr))) ++irises[tr];
    prev = g;
    wi.add(l, r, n);
  }
  wi.close();
  printf("init: %d cables, %d lanes with steps, %d tracks on the FM (%s), a step each: rms %.3f, gates %u %u %u %u\n",
         cables, steps, models, eng_algo_name(PG_TRACK(0, 0), eng_model_find(MK_FM, 0)), std::sqrt(ie / (iblocks * n)), irises[0], irises[1],
         irises[2], irises[3]);
  if (cables || steps || models != ENG_TRACKS || inan || ie < 1e-3) ++failures, printf("FAIL: the INIT project\n");
  for (int tr = 0; tr < ENG_TRACKS; ++tr)
    if (!irises[tr]) ++failures, printf("FAIL: INIT, track %d never plays\n", tr + 1);

  // 6. the step sequencer: the demo's patterns at 120 BPM (a sixteenth 125 ms), 8 s = 4 bars
  eng_play(0);
  eng_defaults(p);
  auto run = [&](int seconds, uint32_t* rises, int* lo, int* hi, uint16_t* inscale) {
    uint32_t pv = 0, bl = static_cast<uint32_t>(seconds) * 44100u / n;
    for (int tr = 0; tr < ENG_TRACKS; ++tr) rises[tr] = 0, lo[tr] = 999, hi[tr] = -999;
    *inscale = 1;
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);   // stopped: the tails die away
    eng_play(1);
    for (uint32_t b = 0; b < bl; ++b) {
      eng_render(l, r, n);
      uint32_t g = eng_gates();
      for (int tr = 0; tr < ENG_TRACKS; ++tr)
        if ((g & (1u << tr)) && !(pv & (1u << tr))) {
          ++rises[tr];
          int nt = eng_note(tr);
          if (nt < lo[tr]) lo[tr] = nt;
          if (nt > hi[tr]) hi[tr] = nt;
          if (eng_quantize(nt) != nt) *inscale = 0;
        }
      pv = g;
    }
    eng_play(0);
  };
  uint32_t sr[ENG_TRACKS];
  int slo[ENG_TRACKS], shi[ENG_TRACKS];
  uint16_t ok_scale;
  run(8, sr, slo, shi, &ok_scale);
  printf("steps, 4 bars: trigs %u %u %u %u (20 20 12 24 expected: track 1 has a ratchet x2 a bar), track 2 notes %d..%d\n",
         sr[0], sr[1], sr[2], sr[3], slo[1], shi[1]);
  static const uint32_t kExpect[4] = {20, 20, 12, 24};
  for (int tr = 0; tr < ENG_TRACKS; ++tr)
    if (sr[tr] + 1 < kExpect[tr] || sr[tr] > kExpect[tr] + 1) ++failures, printf("FAIL: steps, track %d\n", tr + 1);
  if (shi[1] - slo[1] < 12) ++failures, printf("FAIL: track 2's lock (an octave up on step 15) not played\n");

  // 7. probability, ratchets, a random tune kept in the scale
  p->val[PG_STEP(2) * 4 + 1] = 0.0f;                   // track 3 never
  p->val[PG_STEP(0) * 4 + 2] = 3.0f;                   // track 1: three a trig (its locked step: two)
  p->rnd[3][TP_NOTE] = 100;                             // track 4: any note an octave around, new at each trig
  p->val[PG_GLOBAL * 4 + 1] = 11.0f;                    // minor pentatonic
  run(8, sr, slo, shi, &ok_scale);
  printf("prob 0 on track 3: %u trigs; ratchets x3 on track 1: %u (44 expected); random tune on track 4: %d..%d, %s\n",
         sr[2], sr[0], slo[3], shi[3], ok_scale ? "all in the scale" : "OUT OF THE SCALE");
  if (sr[2]) ++failures, printf("FAIL: probability 0 played\n");
  if (sr[0] + 2 < 44 || sr[0] > 45) ++failures, printf("FAIL: ratchets\n");
  if (!ok_scale || shi[3] - slo[3] < 5) ++failures, printf("FAIL: the random tune or the quantizer\n");
  p->rnd[3][TP_NOTE] = -100;                            // looped: the same notes each bar
  run(4, sr, slo, shi, &ok_scale);
  int a0 = slo[3], a1 = shi[3];
  run(4, sr, slo, shi, &ok_scale);
  printf("looped random tune: %d..%d then %d..%d\n", a0, a1, slo[3], shi[3]);
  if (a0 != slo[3] || a1 != shi[3]) ++failures, printf("FAIL: the looped random is not a loop\n");

  // 8. the keyboard: a key pressed while another is held is a new trig
  eng_defaults(p);
  for (int b = 0; b < 400; ++b) eng_render(l, r, n);
  {
    uint32_t r0 = eng_rises(1), kr;
    for (int b = 0; b < 300; ++b) {
      if (b == 10) eng_key(1, 60, 1);
      if (b == 100) eng_key(1, 62, 1);                  // legato
      if (b == 101) eng_key(1, 60, 0);
      if (b == 200) eng_key(1, 62, 0);
      if (b == 250) {                                   // a tap shorter than a block
        eng_key(1, 64, 1);
        eng_key(1, 64, 0);
      }
      eng_render(l, r, n);
    }
    kr = eng_rises(1) - r0;
    printf("keyboard: legato then a short tap: %u trigs (3 expected)\n", kr);
    if (kr != 3) ++failures, printf("FAIL: legato keys\n");
  }

  // 10. Endless: track 1 records C3, a rest, D#3, G3, then loops them on sixteenths; a key transposes it
  eng_defaults(p);
  eng_endless_rec(0, 1);
  eng_endless_add(0, 60);
  eng_endless_add(0, ENDLESS_REST);
  eng_endless_add(0, 63);
  eng_endless_add(0, 67);
  eng_endless_rec(0, 0);
  run(4, sr, slo, shi, &ok_scale);
  int e1lo = slo[0], e1hi = shi[0];
  uint32_t e1 = sr[0];
  eng_endless_key(0, 65);                                // up a fourth
  run(4, sr, slo, shi, &ok_scale);
  printf("endless: %u notes in 2 bars (24 expected), %d..%d; transposed by a key: %d..%d, %s\n", e1, e1lo, e1hi, slo[0],
         shi[0], ok_scale ? "in the scale" : "OUT OF THE SCALE");
  const int eo = 12 * static_cast<int>(std::lround(p->val[PG_TRACK(0, 2) * 4 + 3]));   // (the track's OCTAVE)
  if (e1 + 1 < 24 || e1 > 25 || e1lo != 60 + eo || e1hi != 67 + eo || slo[0] != 65 + eo || shi[0] != 72 + eo ||
      !ok_scale)
    ++failures, printf("FAIL: Endless\n");
  eng_endless_clear(0);

  // 11. no clicks: a smooth voice (HARMONIC, long decay) on every sixteenth, its pan, level and tune random at each
  // trig. A click is a sample whose second difference stands far above its neighbours' (a gain or a pitch that jumps
  // at a block's edge); 0.1.8's mixer made them by the thousand, ~5000 times its neighbours
  {
    eng_play(0);
    eng_blank(p);
    p->algo[PG_TRACK(0, 0)] = static_cast<uint8_t>(eng_model_find(MK_PLAITS, 12));
    p->val[eng_track_param(0, TP_DECAY)] = 0.75f;
    p->val[eng_track_param(0, TP_SEND)] = 0.0f;
    for (int s = 0; s < 16; ++s) eng_trig_set(0, s, 1);
    p->rnd[0][TP_PAN] = 100;
    p->rnd[0][TP_NOTE] = 100;
    p->rnd[0][TP_RATCHET] = 100;
    eng_lock_set(0, 4, TP_LEVEL, 0.5f);                   // a lock on the level, step 5: half
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);
    eng_play(1);
    static float d2[2][44100 * 4];
    float pl = 0, pr = 0, ppl = 0, ppr = 0;
    const int nb = 44100 * 4 / static_cast<int>(n);
    for (int b = 0; b < nb; ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        d2[0][b * n + i] = l[i] - 2.0f * pl + ppl;
        d2[1][b * n + i] = r[i] - 2.0f * pr + ppr;
        ppl = pl, pl = l[i], ppr = pr, pr = r[i];
      }
    }
    eng_play(0);
    double worst = 0;
    for (int c = 0; c < 2; ++c) {
      double acc = 0;
      const int W = 88;                                   // 2 ms around
      for (int i = 2; i < nb * static_cast<int>(n); ++i) {
        acc += std::fabs(d2[c][i]);
        if (i >= W + 2) acc -= std::fabs(d2[c][i - W]);
        if (i < W + 3) continue;
        double q = std::fabs(d2[c][i]) / ((acc - std::fabs(d2[c][i])) / (W - 1) + 1e-5);
        if (std::fabs(d2[c][i]) > 0.003 && q > worst) worst = q;
      }
    }
    printf("random pan, tune and ratchets, a level lock: the worst click %.0f times its neighbours (under 200: none)\n",
           worst);
    if (worst > 200.0) ++failures, printf("FAIL: clicks\n");
    eng_defaults(p);
  }

  // 12. Warps: the demo with track 3 (FM 2-op) as the carrier and track 2 (the bass) as the modulator, through every
  // mark of ALGORITHM (its six cross-modulations) and the frequency shifter; its output on track 3, finite and heard;
  // its cost against the four voices'. Then its oscillator alone (the tracks silent): A4 on LEVEL 1, 440 Hz out
  {
    eng_play(0);
    eng_defaults(p);
    const int nb = 44100 * 2 / static_cast<int>(n);
    auto play = [&](double* rms, double* us) {
      double e = 0;
      int nan = 0;
      for (int b = 0; b < 200; ++b) eng_render(l, r, n);
      double t0 = static_cast<double>(clock());
      for (int b = 0; b < nb; ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i]) || !std::isfinite(r[i])) nan = 1;
          e += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
        }
      }
      *us = (static_cast<double>(clock()) - t0) / CLOCKS_PER_SEC * 1e6 / nb;
      *rms = nan ? -1.0 : std::sqrt(e / (2.0 * nb * n));
    };
    double rms0, us0, rms, us, worst = 0;
    eng_play(1);
    play(&rms0, &us0);
    p->val[PG_WARPS(0) * 4 + 0] = WC_T3;
    p->val[PG_WARPS(0) * 4 + 1] = 1.0f;                  // track 2
    static const float kMarks[6] = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    printf("warps (T3 by T2), rms and the render's time, us a block (off: %.3f, %.0f):", rms0, us0);
    for (int a = 0; a < 7; ++a) {
      p->val[PG_WARPS(0) * 4 + 2] = a < 6 ? kMarks[a] : 0.7f;
      p->val[PG_WARPS(1) * 4 + 3] = static_cast<float>(a == 6 ? WM_SHIFT : WM_WARPS);
      if (a == 6) p->val[PG_WARPS(0) * 4 + 0] = WC_OSC1;   // the shifter on its own oscillator
      play(&rms, &us);
      int alg = eng_warps_algo();
      printf(" %s %.3f %.0f%s", eng_warps_name(alg), rms, us, a == 6 ? "\n" : ",");
      if (us - us0 > worst) worst = us - us0;
      if (rms <= 0.002 || rms > 1.0 || eng_warps_track() != (a == 6 ? 1 : 2) || alg != a)
        ++failures, printf("FAIL: Warps at %d (%s): rms %.3f, on track %d\n", a, eng_warps_name(alg), rms, eng_warps_track() + 1);
    }
    printf("warps: at most %.0f us a block more than without, the four voices and the rest %.0f\n", worst, us0);
    eng_play(0);
    eng_blank(p);                                         // nothing plays: the oscillator alone, through XFADE
    p->val[PG_WARPS(0) * 4 + 0] = WC_OSC1;
    p->val[PG_WARPS(0) * 4 + 1] = 0.0f;
    p->val[PG_WARPS(0) * 4 + 2] = 0.0f;
    p->val[PG_WARPS(1) * 4 + 1] = 57.0f / 60.0f;          // LEVEL 1: A4
    p->val[PG_MIX * 4 + 0] = 1.0f;
    double best = 0;
    for (int side = 0; side < 2; ++side) {                // TIMBRE at either end: the carrier alone, one way
      p->val[PG_WARPS(1) * 4 + 0] = static_cast<float>(side);
      for (int b = 0; b < 400; ++b) eng_render(l, r, n);
      int cross = 0;
      float prev = 0;
      double e = 0;
      for (int b = 0; b < 44100 / static_cast<int>(n); ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if ((l[i] >= 0) != (prev >= 0)) ++cross;
          prev = l[i];
          e += static_cast<double>(l[i]) * l[i];
        }
      }
      double hz = cross / 2.0 / (44100.0 / n * n / 44100.0 * (44100 / n) / (44100.0 / n));
      if (e > best) best = e, hz = cross / 2.0 / ((44100 / n) * n / 44100.0);
      if (side == 1 || e >= best) {
        printf("warps' oscillator, TIMBRE %d: %.1f Hz, rms %.3f\n", side, cross / 2.0 / ((44100 / n) * n / 44100.0),
               std::sqrt(e / ((44100 / n) * n)));
      }
      (void)hz;
    }
    eng_defaults(p);
  }

  // 14. Warps' frequency shifter (at 44.1 kHz, its allpass pairs made for that rate): 1 kHz in, shifted 100 Hz; one
  // side kept, the other under it by 30 dB or more (TIMBRE at either end)
  {
    static uint8_t wm[96 * 1024] __attribute__((aligned(8)));
    if (mw_size() > sizeof wm) {
      ++failures, printf("FAIL: Warps needs %u bytes\n", mw_size());
    } else {
      float pot = 0.5f + 0.5f * (0.4f + std::log2(100.0f / 4.0f) / 15.0f);   // modulator.cc: +100 Hz
      double side[2][2];
      for (int t = 0; t < 2; ++t) {
        mw_init(wm);
        static float c[44100], m[44100], o[44100];
        for (int i = 0; i < 44100; ++i) c[i] = 0.0f, m[i] = 0.4f * std::sin(2.0 * M_PI * 1000.0 * i / 44100.0);
        for (int b = 0; b < 44100 / 128; ++b) {
          mw_set(wm, pot, static_cast<float>(t), 0.0f, 1.0f, 1, 48.0f, 1);
          static float dry[128];
          mw_render(wm, c + b * 128, m + b * 128, m + b * 128, o + b * 128, dry, 128);
        }
        for (int k = 0; k < 2; ++k) {
          double f = k ? 1100.0 : 900.0, re = 0, im = 0;
          for (int i = 11025; i < 44100 / 128 * 128; ++i)
            re += o[i] * std::cos(2.0 * M_PI * f * i / 44100.0), im += o[i] * std::sin(2.0 * M_PI * f * i / 44100.0);
          side[t][k] = 10.0 * std::log10(re * re + im * im + 1e-20);
        }
      }
      printf("warps' frequency shifter, 1 kHz up 100 Hz: TIMBRE 0: 1100 Hz %+.0f dB over 900 Hz; TIMBRE 1: %+.0f dB\n",
             side[0][1] - side[0][0], side[1][1] - side[1][0]);
      double a = side[0][1] - side[0][0], b = side[1][1] - side[1][0];
      if (!((a > 30 && b < -30) || (a < -30 && b > 30))) ++failures, printf("FAIL: the frequency shifter\n");
    }
  }

  // 25. Warps' ECO (its cross-modulations at 3x instead of 6x): each algorithm, a 220 Hz carrier by a 330 Hz
  // modulator, plays as at 6x (what differs, the samples a little apart in time and what folds back from over 66 kHz,
  // under it: -35 dB or so, XOR's bits the most, -18), in less time
  {
    static uint8_t wm[96 * 1024] __attribute__((aligned(8)));
    static float c[44100], m[44100], o[2][44100], dry[128];
    for (int i = 0; i < 44100; ++i)
      c[i] = 0.4f * std::sin(2.0 * M_PI * 220.0 * i / 44100.0), m[i] = 0.4f * std::sin(2.0 * M_PI * 330.0 * i / 44100.0);
    const int nb = 44100 / 128;
    double us[2] = {0, 0}, worst = -200;
    printf("warps ECO against 6x, the difference (dB):");
    for (int a = 0; a < 6; ++a) {
      for (int eco = 0; eco < 2; ++eco) {
        mw_init(wm);
        mw_eco(wm, eco);
        double t0 = static_cast<double>(clock());
        for (int b = 0; b < nb; ++b) {
          mw_set(wm, a / 8.0f, 0.5f, 0.6f, 0.6f, 0, 48.0f, 0);
          mw_render(wm, c + b * 128, m + b * 128, m + b * 128, o[eco] + b * 128, dry, 128);
        }
        us[eco] += (static_cast<double>(clock()) - t0) / CLOCKS_PER_SEC * 1e6 / nb;
      }
      double best = 1e30, ref = 0;
      for (int i = 4410; i < nb * 128 - 8; ++i) ref += static_cast<double>(o[0][i]) * o[0][i];
      for (int lag = -3; lag <= 3; ++lag) {                // (the two pairs of filters delay it a little differently)
        double e = 0;
        for (int i = 4410; i < nb * 128 - 8; ++i) {
          double d = static_cast<double>(o[1][i + lag]) - o[0][i];
          e += d * d;
        }
        if (e < best) best = e;
      }
      double db = 10.0 * std::log10(best / (ref + 1e-20) + 1e-20);
      printf(" %s %.0f", eng_warps_name(a), db);
      if (db > worst) worst = db;
      if (ref <= 1e-6 || db > -15.0) ++failures, printf(" FAIL");
    }
    printf("; its time %.1f us a block, 6x %.1f\n", us[1] / 6, us[0] / 6);
  }

  // 13. 0.1.9's project (version 4, 2644 bytes) read into this version's: all of it, Warps off
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 4;
    q->size = 2644;
    float* val = reinterpret_cast<float*>(buf + 8);
    for (int i = 0; i < 132; ++i) val[i] = 0.25f;
    buf[8 + 528 + PG_TRACK(1, 0)] = 17;                   // track 2 on NOISE, track 1 on STRING (the others on VA VCF:
    buf[8 + 528 + PG_TRACK(0, 0)] = 19;                   // gone: the FM, its second algorithm)
    buf[8 + 528 + 33] = 2;                                // the output: mono
    cable_t* cab = reinterpret_cast<cable_t*>(buf + 572);
    cab[0].src = 2;                                       // (Tides A2 then: LFO 2 now)
    cab[0].dst = 3;
    cab[0].depth = 33;
    buf[2380 + 0] = 60;                                   // Endless, track 1: three notes
    buf[2636 + 0] = 3;
    int ok = eng_upgrade(q, 2644);
    printf("a version 4 project: %s, track 2 model %d, output %d, a cable %s, knob %.2f, Endless %d notes, warps %s\n",
           ok ? "read" : "REFUSED", q->algo[PG_TRACK(1, 0)], q->out_mode, q->cable[0][0].depth == 33 ? "kept" : "LOST",
           q->val[5], q->endless_len[0], q->val[PG_WARPS(0) * 4] == WC_OFF ? "off" : "ON");
    if (!ok || !eng_check(q) || q->algo[PG_TRACK(1, 0)] != eng_model_find(MK_PLAITS, 17) || q->out_mode != 2 || q->cable[0][0].depth != 33 ||
        q->val[5] != 0.25f || q->endless_len[0] != 3 || q->endless[0][0] != 60 || q->val[PG_WARPS(0) * 4] != WC_OFF)
      ++failures, printf("FAIL: the version 4 project\n");
  }

  // 15. clearing a track's pattern: its trigs and locks gone, the other tracks' kept
  {
    eng_defaults(p);
    int before = 0, after = 0, other = 0, locks = 0;
    for (int s = 0; s < 16; ++s) before += eng_trig(1, s), other += eng_trig(0, s);
    eng_steps_clear(1);
    for (int s = 0; s < 16; ++s) after += eng_trig(1, s), other -= eng_trig(0, s), locks += eng_step_locked(1, s);
    printf("clear track 2: %d trigs, then %d (%d locks); track 1 %s\n", before, after, locks, other ? "CHANGED" : "kept");
    if (!before || after || locks || other) ++failures, printf("FAIL: clearing a track\n");
    eng_defaults(p);
  }

  // 16. ALL undone in time: asked 200 blocks into PLAY (120 BPM, a bar 88200 samples, 689 blocks), the knobs come
  // back as the second bar starts, 489 blocks later; stopped, at once
  {
    eng_play(0);
    eng_defaults(p);
    static float was[ENG_NPARAMS];
    static uint8_t was_algo[ENG_NPAGES];
    static int8_t was_rnd[ENG_TRACKS * ENG_NTP];
    for (int i = 0; i < ENG_NPARAMS; ++i) was[i] = p->val[i];
    for (int i = 0; i < ENG_NPAGES; ++i) was_algo[i] = p->algo[i];
    for (int i = 0; i < ENG_TRACKS * ENG_NTP; ++i) was_rnd[i] = p->rnd[i / ENG_NTP][i % ENG_NTP];
    eng_play(1);
    for (int b = 0; b < 200; ++b) eng_render(l, r, n);
    for (int t = 0; t < ENG_TRACKS; ++t) p->val[eng_track_param(t, TP_DECAY)] = 0.05f;   // an ALL
    eng_back_at_bar(was, was_algo, was_rnd);
    int at = -1;
    for (int b = 0; b < 1000 && at < 0; ++b) {
      eng_render(l, r, n);
      if (!eng_back_pending()) at = b;
    }
    int ok = at >= 480 && at <= 495 && p->val[eng_track_param(2, TP_DECAY)] == was[eng_track_param(2, TP_DECAY)];
    eng_play(0);
    p->val[eng_track_param(0, TP_DECAY)] = 0.05f;
    eng_back_at_bar(was, was_algo, was_rnd);
    eng_render(l, r, n);
    int stopped = !eng_back_pending() && p->val[eng_track_param(0, TP_DECAY)] == was[eng_track_param(0, TP_DECAY)];
    printf("ALL undone at the bar: %d blocks after it was asked (489 expected); stopped: %s\n", at,
           stopped ? "at once" : "NOT DONE");
    if (!ok || !stopped) ++failures, printf("FAIL: ALL's undo\n");
    eng_defaults(p);
  }

  // 9. v0.1's project (version 2) is not read any more (0.3 reads 0.1.4's and later)
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 2;
    q->size = 564;
    if (eng_upgrade(q, 564)) ++failures, printf("FAIL: a version 2 project read\n");
  }

  // 17. 0.2's project (version 5, 2680 bytes): Marbles' RATE the tempo, the pages moved (the mixer, the reverb, the
  // steps, the scale, Warps), a lock and a cable kept, a cable from Marbles dropped
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 5;
    q->size = 2680;
    float* val = reinterpret_cast<float*>(buf + 8);
    val[16 * 4 + 0] = 0.7f;                               // Marbles' RATE
    val[20 * 4 + 2] = 0.33f;                              // the mixer: track 3's level
    val[22 * 4 + 0] = 0.8f;                               // the reverb's time
    val[24 * 4 + 3] = 12.0f;                              // track 1's steps: 12
    val[28 * 4 + 2] = 3.0f;                               // the scale: Dorian
    val[34 * 4 + 0] = 2.0f;                               // Warps: T2 the carrier
    uint8_t* algo = buf + 8 + 140 * 4;
    algo[PG_TRACK(2, 0)] = 19;                            // track 3 on STRING
    cable_t* cab = reinterpret_cast<cable_t*>(buf + 8 + 560 + 35 + 3);
    cab[0] = {1, 20 * 4 + 1, 40, 0};                      // Tides A1 -> track 2's level: LFO 1 now
    cab[1] = {12, 2, 50, 0};                              // Marbles' Y -> track 1's TIMBRE: gone
    cab[2] = {5, 3, 30, 0};                               // Tides B1 -> track 1's MORPH: gone (no LFO for it)
    uint8_t* trig = buf + 8 + 560 + 35 + 3 + 160;
    trig[1 * 8 + 0] = 0x11;                               // track 2: steps 1 and 5
    uint8_t* lock = trig + 32 + 72 + 8;
    lock[0] = 1 << 6 | 4;                                 // track 2, step 5: its note
    lock[1] = TP_NOTE;
    lock[2] = 200;
    for (int i = 1; i < OLD_LOCKS; ++i) lock[i * 4 + 1] = 0xFF;
    int ok = eng_upgrade(q, 2680);
    char tx[24];
    eng_param_text(PG_GLOBAL * 4, q->val[PG_GLOBAL * 4], tx, sizeof tx);
    float lv = 0;
    int haslock = 0;
    for (int i = 0; i < ENG_PLOCKS; ++i)
      if (q->lock[i].lane == 1 && q->lock[i].step == 4 && q->lock[i].tp == TP_NOTE) haslock = q->lock[i].v;
    lv = q->val[PG_MIX * 4 + 2];
    printf("a version 5 project: %s, tempo %s, level %.2f, reverb %.2f, length %.0f, scale %.0f, Warps' carrier %.0f, "
           "LFO 2 %s, model %d, cables %d (L%d)/%d/%d, trig %d, lock %d\n",
           ok ? "read" : "REFUSED", tx, lv, q->val[PG_REVERB * 4], q->val[PG_STEP(0) * 4 + 3], q->val[PG_GLOBAL * 4 + 1],
           q->val[PG_WARPS(0) * 4], q->val[PG_LFO(1) * 4 + 2] == LM_SYNC ? "synced" : "NOT NEW", q->algo[PG_TRACK(2, 0)],
           q->cable[0][0].depth, q->cable[0][0].src, q->cable[0][1].depth, q->cable[0][2].depth,
           (q->trig[1][0] >> 4) & 1, haslock);
    if (!ok || !eng_check(q) || q->val[PG_GLOBAL * 4] != 0.7f || lv != 0.33f || q->val[PG_REVERB * 4] != 0.8f ||
        q->val[PG_STEP(0) * 4 + 3] != 12.0f || q->val[PG_GLOBAL * 4 + 1] != 3.0f || q->val[PG_WARPS(0) * 4] != 2.0f ||
        q->val[PG_LFO(1) * 4 + 2] != LM_SYNC || q->algo[PG_TRACK(2, 0)] != eng_model_find(MK_PLAITS, 19) || q->cable[0][0].depth != 40 ||
        q->cable[0][0].src != SRC_L1 || q->cable[0][0].dst != PG_MIX * 4 + 1 || q->cable[0][1].depth != 0 ||
        q->cable[0][2].depth != 0 || haslock != 200)
      ++failures, printf("FAIL: the version 5 project\n");
  }

  // 18. swing: every step of track 1 played, SWING at 75 %: the off-beats late, a sixteenth and a half then half of
  // one apart (at 120 BPM 8269 and 2756 samples, ~65 and ~22 blocks)
  {
    eng_play(0);
    eng_blank(p);
    for (int s = 0; s < 16; ++s) eng_trig_set(0, s, 1);
    p->val[PG_GLOBAL * 4 + 3] = 1.0f;
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);
    uint32_t r0 = eng_rises(0);
    int at[12], k = 0;
    eng_play(1);
    for (int b = 0; b < 2000 && k < 12; ++b) {
      eng_render(l, r, n);
      if (eng_rises(0) != r0) r0 = eng_rises(0), at[k++] = b;
    }
    eng_play(0);
    int lo = 9999, hi = 0;
    for (int i = 2; i + 1 < k; i += 2) {
      int a = at[i + 1] - at[i], b = i + 2 < k ? at[i + 2] - at[i + 1] : -1;
      if (a > hi) hi = a;
      if (b >= 0 && b < lo) lo = b;
      if (b >= 0 && (a < 60 || a > 70 || b < 18 || b > 26)) lo = -1;
    }
    printf("swing 75 %%: the sixteenths %d and %d blocks apart (65 and 22 expected)\n", hi, lo);
    if (k < 12 || lo < 0) ++failures, printf("FAIL: swing\n");
  }

  // 19. a step's LENGTH: a sustained voice (VIRTUAL ANALOG), its gate 5 % then 95 % of the sixteenth (6 and 119 ms)
  {
    eng_blank(p);
    for (int s = 0; s < 16; s += 4) eng_trig_set(0, s, 1);
    int held[2];
    for (int k = 0; k < 2; ++k) {
      p->val[eng_track_param(0, TP_LENGTH)] = static_cast<float>(k);
      for (int b = 0; b < 400; ++b) eng_render(l, r, n);
      eng_play(1);
      held[k] = 0;
      for (int b = 0; b < 689; ++b) {                     // a bar: four notes
        eng_render(l, r, n);
        held[k] += eng_gates() & 1u;
      }
      eng_play(0);
    }
    printf("length 5 %% then 95 %%: the gate held %d then %d blocks a bar (~8 and ~164 expected)\n", held[0], held[1]);
    if (held[0] > 14 || held[1] < 150 || held[1] > 175) ++failures, printf("FAIL: the steps' length\n");
  }

  // 20. the delay: one note (a kick), its echoes a quarter (0.5 s) apart, ping-pong: left at a quarter, right at two
  {
    eng_blank(p);
    p->algo[PG_TRACK(0, 0)] = static_cast<uint8_t>(eng_model_find(MK_DRUM, 0));
    eng_trig_set(0, 0, 1);
    p->val[PG_STEP(0) * 4 + 3] = 64.0f;                   // one note in four bars
    p->val[eng_track_param(0, TP_DSEND)] = 1.0f;
    p->val[eng_track_param(0, TP_PAN)] = 1.0f;           // the kick on the right: the left the echoes alone
    static const float kD[8] = {8.0f, 0.3f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f};   // 1/4, ping-pong, flat, no mod
    for (int k = 0; k < 8; ++k) p->val[PG_DELAY(k / 4) * 4 + k % 4] = kD[k];
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);
    eng_play(1);
    static float L[44100 * 2], R[44100 * 2];
    for (int b = 0; b < 44100 * 2 / 128; ++b) {
      eng_render(l, r, n);
      for (int i = 0; i < 128; ++i) L[b * 128 + i] = l[i], R[b * 128 + i] = r[i];
    }
    eng_play(0);
    int pl = 0, pr = 0;
    pr = 33075;                                           // (the right after the kick)
    for (int i = 1; i < 44100 * 2 / 128 * 128; ++i) {
      if (std::fabs(L[i]) > std::fabs(L[pl])) pl = i;
      if (i > 33075 && std::fabs(R[i]) > std::fabs(R[pr])) pr = i;
    }
    printf("the delay, a quarter ping-pong: its loudest echo at %.3f s left, %.3f s right (0.5 and 1.0 expected)\n",
           pl / 44100.0, pr / 44100.0);
    if (pl < 22050 || pl > 22050 + 2000 || pr < 44100 || pr > 44100 + 2000) ++failures, printf("FAIL: the delay\n");
  }

  // 21. the effects' lanes: without a step the effect stays open (1); a step opens it, DECAY closes it down to
  // 1 - DEPTH; Warps' lane locks its ALGORITHM on a step
  {
    eng_defaults(p);
    eng_steps_clear(LANE_DELAY);
    p->val[PG_WARPS(0) * 4 + 0] = WC_T3;                  // Warps: T3 by T2
    p->val[PG_WARPS(0) * 4 + 2] = 0.0f;                   // CROSSFADE, and DIGITAL RING locked on step 9
    eng_trig_set(LANE_WARPS, 0, 1);
    eng_trig_set(LANE_WARPS, 8, 1);
    eng_lock_set(LANE_WARPS, 8, 2, 0.6f);                 // (its ALGORITHM)
    p->val[PG_STEP(LANE_WARPS) * 4 + 0] = 0.2f;           // ~35 ms
    p->val[PG_STEP(LANE_WARPS) * 4 + 2] = 1.0f;
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);
    eng_play(1);
    float dmin = 2, dmax = -1, wmin = 2, wmax = -1, rmin = 2;
    int algs = 0;
    for (int b = 0; b < 689; ++b) {
      eng_render(l, r, n);
      float d = eng_lane_open(LANE_DELAY), w = eng_lane_open(LANE_WARPS), rv = eng_lane_open(LANE_REVERB);
      dmin = std::fmin(dmin, d), dmax = std::fmax(dmax, d), wmin = std::fmin(wmin, w), wmax = std::fmax(wmax, w);
      rmin = std::fmin(rmin, rv);
      algs |= 1 << eng_warps_algo();
    }
    eng_play(0);
    printf("lanes: the delay without steps open %.2f..%.2f, the reverb %.2f; Warps' %.2f..%.2f, its algorithms %s%s\n",
           dmin, dmax, rmin, wmin, wmax, algs & 1 ? "CROSSFADE" : "", algs & 8 ? " and DIGITAL RING" : "");
    if (dmin < 1.0f || rmin < 1.0f || wmax < 0.95f || wmin > 0.05f || algs != (1 | 8)) ++failures, printf("FAIL: the lanes\n");
    eng_defaults(p);
  }

  // 22. the duck: T1 on every beat, T2 a held note (VIRTUAL ANALOG, its gate the whole step); T2's level just after
  // T1's trigs against just before them: down as deep as T2's DUCK (0.8: -14 dB), none at 0
  {
    double ratio[2];
    for (int k = 0; k < 2; ++k) {
      eng_play(0);
      eng_blank(p);
      p->algo[PG_COMP] = MB_OFF;
      p->algo[PG_TRACK(0, 0)] = static_cast<uint8_t>(eng_model_find(MK_DRUM, 0));
      p->val[eng_track_param(0, TP_LEVEL)] = 0.0f;         // (T1 itself silent: T2 alone heard)
      for (int st = 0; st < 16; st += 4) eng_trig_set(0, st, 1);
      for (int st = 0; st < 16; ++st) eng_trig_set(1, st, 1);
      p->val[eng_track_param(1, TP_LENGTH)] = 1.0f;
      p->val[PG_TRACK(1, 1) * 4 + 0] = 1.0f;               // a long LPG decay: a steady note
      p->val[PG_DUCK * 4 + 0] = 0.3f;                      // ~85 ms
      p->val[eng_track_param(1, TP_DUCK)] = k ? 0.8f : 0.0f;
      for (int b = 0; b < 400; ++b) eng_render(l, r, n);
      eng_play(1);
      double after = 0, before = 0;
      for (int b = 0; b < 689 * 2; ++b) {                  // two bars: a beat every 172 blocks
        eng_render(l, r, n);
        int ph = b % 172;
        double e = 0;
        for (uint32_t i = 0; i < n; ++i) e += static_cast<double>(l[i]) * l[i];
        if (b < 172) continue;
        if (ph >= 3 && ph < 8) after += e;                 // 9..23 ms after the trig
        if (ph >= 160 && ph < 165) before += e;            // the end of the beat: back up
      }
      ratio[k] = 10.0 * std::log10((after + 1e-12) / (before + 1e-12));
      eng_play(0);
    }
    printf("duck: T2 just after T1's trigs %+.1f dB against just before (duck 0.8), %+.1f dB (duck 0)\n", ratio[1],
           ratio[0]);
    if (ratio[1] > -9.0 || std::fabs(ratio[0]) > 3.0) ++failures, printf("FAIL: the duck\n");
  }

  // 23. the master bus: the compressor brings a loud patch down (its reduction seen), OTT keeps it as loud at its
  // depth's middle, every mode's peak under full scale (the limiter: 0.8 at most, the soft limit after)
  {
    double rms[3];
    float pk[3], gr = 0;
    for (int m = 0; m < 3; ++m) {
      eng_play(0);
      eng_defaults(p);
      p->algo[PG_COMP] = static_cast<uint8_t>(m);
      for (int t = 0; t < ENG_TRACKS; ++t) p->val[eng_track_param(t, TP_LEVEL)] = 1.0f;
      p->val[PG_COMP * 4 + 0] = 0.6f;                      // -22 dB, 3:1
      for (int b = 0; b < 400; ++b) eng_render(l, r, n);
      eng_play(1);
      double e = 0;
      pk[m] = 0;
      for (int b = 0; b < 1378; ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          e += static_cast<double>(l[i]) * l[i];
          pk[m] = std::fmax(pk[m], std::fabs(l[i]));
        }
        if (m == MB_COMP) gr = std::fmin(gr, eng_gr(0));
      }
      rms[m] = std::sqrt(e / (1378.0 * n));
      eng_play(0);
    }
    printf("master bus: off rms %.3f peak %.2f; comp (-22 dB, 3:1, +4 dB) rms %.3f peak %.2f, %.1f dB at most; OTT rms %.3f "
           "peak %.2f\n", rms[0], pk[0], rms[1], pk[1], gr, rms[2], pk[2]);
    if (gr > -6.0f || pk[0] > 1.0f || pk[1] > 1.0f || pk[2] > 1.0f || rms[2] < rms[0] * 0.6 || rms[2] > rms[0] * 1.7)
      ++failures, printf("FAIL: the master bus\n");
    eng_defaults(p);
  }

  // 24. 0.3's project (version 6, 2804 bytes): all of it, the duck off, the master bus INIT's
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 6;
    q->size = 2804;
    float* val = reinterpret_cast<float*>(buf + 8);
    for (int i = 0; i < 148; ++i) val[i] = 0.25f;
    uint8_t* algo = buf + 8 + 148 * 4;
    algo[PG_TRACK(3, 0)] = 23;                            // track 4 on HI-HAT
    uint8_t* trig = buf + 8 + 592 + 37 + 3 + 160;
    trig[LANE_DELAY * 8] = 0x05;                          // the delay's lane: steps 1 and 3
    int8_t* rnd = reinterpret_cast<int8_t*>(trig + 56);
    rnd[2 * 19 + TP_DSEND] = -40;                         // track 3's delay send, looped
    int ok = eng_upgrade(q, 2804);
    printf("a version 6 project: %s, track 4 model %d, a knob %.2f, the delay's lane %d%d, a random %d, duck %.2f, bus %d\n",
           ok ? "read" : "REFUSED", q->algo[PG_TRACK(3, 0)], q->val[100], q->trig[LANE_DELAY][0] & 1,
           (q->trig[LANE_DELAY][0] >> 2) & 1, q->rnd[2][TP_DSEND], q->val[PG_DUCK * 4 + 2], q->algo[PG_COMP]);
    if (!ok || !eng_check(q) || q->algo[PG_TRACK(3, 0)] != eng_model_find(MK_DRUM, 3) || q->val[100] != 0.25f || q->trig[LANE_DELAY][0] != 0x05 ||
        q->rnd[2][TP_DSEND] != -40 || q->val[PG_DUCK * 4 + 2] != 0.0f || q->algo[PG_COMP] != MB_COMP)
      ++failures, printf("FAIL: the version 6 project\n");
  }

  // 26. 0.3.7's project (version 7, 2864 bytes): all of it, the filter flat. Then the filter: the demo playing, every
  // track's low pass down (the highs gone: the mix darker), then its high pass up (brighter); the knob's text
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 7;
    q->size = 2864;
    float* val = reinterpret_cast<float*>(buf + 8);
    for (int i = 0; i < 160; ++i) val[i] = 0.25f;
    uint8_t* algo = buf + 8 + 160 * 4;
    algo[PG_COMP] = MB_OTT;
    algo[41] = 1;                                         // (after the 40 pages' choices: out_mode, warps_hq,
    algo[42] = 3;                                         // theme)
    uint8_t* trig = algo + 43 + 160;
    trig[LANE_REVERB * 8] = 0x11;
    int8_t* rnd = reinterpret_cast<int8_t*>(trig + 56);
    rnd[1 * 20 + TP_DUCK] = -30;
    int ok = eng_upgrade(q, 2864);
    printf("a version 7 project: %s, OTT %d, HQ %d, theme %d, the reverb's lane %02x, a random %d, the filter %.2f\n",
           ok ? "read" : "REFUSED", q->algo[PG_COMP], q->warps_hq, q->theme, q->trig[LANE_REVERB][0],
           q->rnd[1][TP_DUCK], q->val[PG_EQ * 4 + 1]);
    if (!ok || !eng_check(q) || q->algo[PG_COMP] != MB_OTT || q->warps_hq != 1 || q->theme != 3 ||
        q->trig[LANE_REVERB][0] != 0x11 || q->rnd[1][TP_DUCK] != -30 || q->val[PG_EQ * 4 + 1] != 0.0f ||
        q->val[PG_OTT * 4 + 3] != 0.25f)
      ++failures, printf("FAIL: the version 7 project\n");

    char a[16], b[16], c[16];
    eng_param_text(PG_EQ * 4, -1.0f, a, sizeof a);
    eng_param_text(PG_EQ * 4 + 1, 1.0f, b, sizeof b);
    eng_param_text(PG_EQ * 4 + 2, 0.0f, c, sizeof c);
    eng_play(0);
    eng_defaults(p);
    double bright[3];
    for (int m = 0; m < 3; ++m) {
      for (int t = 0; t < ENG_TRACKS; ++t) p->val[PG_EQ * 4 + t] = m == 0 ? 0.0f : m == 1 ? -0.8f : 0.6f;
      eng_play(0);
      eng_play(1);
      double e = 0, d = 0;
      float prev = 0;
      int nan = 0;
      for (int k = 0; k < 44100 * 4 / static_cast<int>(n); ++k) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i])) nan = 1;
          e += static_cast<double>(l[i]) * l[i];
          d += static_cast<double>(l[i] - prev) * (l[i] - prev);
          prev = l[i];
        }
      }
      bright[m] = nan ? -1.0 : std::sqrt(d / (e + 1e-20));
    }
    printf("the filter: %s, %s, %s; the mix's brightness flat %.3f, low passes %.3f, high passes %.3f\n", a, b, c,
           bright[0], bright[1], bright[2]);
    if (bright[0] <= 0 || !(bright[1] < bright[0] * 0.6) || !(bright[2] > bright[0] * 1.5))
      ++failures, printf("FAIL: the filter\n");
    eng_play(0);
    eng_defaults(p);
  }

  // 27. the Euclidean rhythms (3 in 8: x..x..x., 5 in 16, turned), 0.3.8's project (version 8, 2888 bytes: all of it,
  // no grit), then the bending's bits on a held note of track 1 (a virtual analog voice, alone; 0.7, the grit's
  // place): BITS near the end (a few values left), at the end (1 bit: this quiet a voice, none), CLOCK at the end (x2
  // the note: a sample held for a long while), each SHAPE and a MASK (finite, within the output's reach); then its
  // glitch, the steps playing: GLITCH full (it takes its slice, then repeats it), BEND and DROP up (still finite)
  {
    char e38[9], e516r[17];
    int n516 = 0;
    for (int i = 0; i < 8; ++i) e38[i] = eng_euclid(8, 3, 0, i) ? 'x' : '.';
    for (int i = 0; i < 16; ++i) {
      e516r[i] = eng_euclid(16, 5, 2, i) ? 'x' : '.';
      n516 += eng_euclid(16, 5, 0, i);
    }
    e38[8] = e516r[16] = 0;
    printf("euclid: 3 in 8 %s, 5 in 16 turned 2 %s\n", e38, e516r);
    if (strcmp(e38, "x..x..x.") != 0 || n516 != 5 || !eng_euclid(16, 5, 2, 2) || eng_euclid(16, 5, 2, 0) ||
        eng_euclid(8, 0, 0, 0) || !eng_euclid(4, 9, 0, 3))
      ++failures, printf("FAIL: the Euclidean rhythms\n");

    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 8;
    q->size = 2888;
    float* val = reinterpret_cast<float*>(buf + 8);
    for (int i = 0; i < 164; ++i) val[i] = 0.25f;
    uint8_t* algo = buf + 8 + 164 * 4;
    algo[43] = 4;                                         // (after the 41 pages' choices: out_mode, warps_hq, theme)
    int8_t* rnd = reinterpret_cast<int8_t*>(algo + 44 + 160 + 56);
    rnd[3 * 21 + TP_EQ] = 25;
    int ok = eng_upgrade(q, 2888);
    printf("a version 8 project: %s, theme %d, the filter %.2f, its random %d, drive %.2f, fills %.0f\n",
           ok ? "read" : "REFUSED", q->theme, q->val[PG_EQ * 4 + 2], q->rnd[3][TP_EQ], q->val[PG_BEND(1) * 4],
           q->val[PG_EUC(2) * 4]);
    if (!ok || !eng_check(q) || q->theme != 4 || q->val[PG_EQ * 4 + 2] != 0.25f || q->rnd[3][TP_EQ] != 25 ||
        q->val[PG_BEND(1) * 4] != 0.0f || q->val[PG_EUC(2) * 4] != 0.0f)
      ++failures, printf("FAIL: the version 8 project\n");

    eng_play(0);
    eng_blank(p);
    for (int t = 1; t < ENG_TRACKS; ++t) p->val[PG_MIX * 4 + t] = 0.0f;
    p->val[PG_TRACK(0, 1) * 4 + 0] = 1.0f;               // a long decay: the note held
    p->algo[PG_COMP] = MB_OFF;                           // (the compressor's gain moves sample by sample, the
    p->val[PG_REVERB * 4 + 3] = 0.0f;                    // effects' tails of the tests before go on)
    p->val[PG_DELAY(1) * 4 + 3] = 0.0f;
    auto measure = [&](int* distinct, int* changes, float* peak) {
      for (int b = 0; b < 300; ++b) eng_render(l, r, n);
      int d = 0, c = 0, seen[128];
      float pk = 0.0f;
      for (int b = 0; b < 20; ++b) {     // (values to 1e-4: what the other tracks' silence leaves is under it)
        eng_render(l, r, n);
        int nd = 0;
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i])) pk = 1e9f;
          pk = std::max(pk, std::fabs(l[i]));
          if (i && std::fabs(l[i] - l[i - 1]) > 1e-4f) ++c;
          int v = static_cast<int>(std::floor(l[i] * 1e4f + 0.5f)), k = 0;
          while (k < nd && seen[k] != v) ++k;
          if (k == nd && nd < 128) seen[nd++] = v;
        }
        d = std::max(d, nd);
      }
      *distinct = d;
      *changes = c / 20;
      *peak = pk;
    };
    eng_key(0, 48, 1);
    int d0, c0, d1, c1, d2, c2, dd, cd;
    float k0, k1, k2, kd[3];
    measure(&d0, &c0, &k0);
    p->val[PG_BEND(0) * 4 + 1] = 0.85f;                  // BITS: 3
    measure(&d1, &c1, &k1);
    float k3;
    p->val[PG_BEND(0) * 4 + 1] = 1.0f;                   // BITS: 1 (-1, 0, 1 left)
    int d3;
    measure(&d3, &cd, &k3);
    p->val[PG_BEND(0) * 4 + 1] = 0.0f;
    p->val[PG_BEND(0) * 4 + 0] = 1.0f;                   // CLOCK: x2 the note (a hold every 168 samples)
    measure(&d2, &c2, &k2);
    p->val[PG_BEND(0) * 4 + 0] = 0.0f;
    float ks[BS_N + 1];
    for (int sh = 1; sh < BS_N; ++sh) {                  // each SHAPE, then a MASK (Sign inv)
      p->val[PG_BEND(0) * 4 + 3] = static_cast<float>(sh);
      measure(&dd, &cd, &ks[sh]);
    }
    p->val[PG_BEND(0) * 4 + 3] = 0.0f;
    p->val[PG_BEND(0) * 4 + 2] = 8.0f;
    measure(&dd, &cd, &ks[0]);
    p->val[PG_BEND(0) * 4 + 2] = 0.0f;
    eng_key(0, 48, 0);
    float kw = 0.0f;
    for (int sh = 0; sh < BS_N; ++sh) kw = std::max(kw, ks[sh]);
    printf("bits: plain %d values a block, %d changes, peak %.2f; 3 bits %d values; 1 bit %d values; clock x2 %d changes;"
           " shapes and a mask peak %.2f\n", d0, c0, k0, d1, d3, c2, kw);
    if (d0 < 64 || d1 > 16 || d3 > 3 || c2 > 4 || k0 <= 0.01f || kw > 2.0f || kw < 0.01f)
      ++failures, printf("FAIL: the bits\n");
    {                                    // 0.6's project (version 14, 3652 bytes): read, its grit gone, the rest kept
      struct v14 {
        uint32_t magic;
        uint16_t version, size;
        float val[64 * 4];
        uint8_t algo[64];
        uint8_t out_mode, warps_hq, theme, dice, dice_seed;
        cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
        uint8_t trig[ENG_LANES][ENG_STEPS / 8];
        int8_t rnd[ENG_LANES][37];
        uint16_t seed[ENG_LANES];
        plock_t lock[OLD_LOCKS];
        uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
        uint8_t endless_len[ENG_TRACKS];
        int8_t endless_key[ENG_TRACKS];
        uint8_t rseed[ENG_LANES][37];
      };
      static_assert(sizeof(v14) == 3652u, "0.6's project");
      static uint8_t b14[sizeof(project_t)];
      for (uint32_t i = 0; i < sizeof b14; ++i) b14[i] = 0;
      v14* o = reinterpret_cast<v14*>(b14);
      o->magic = PROJECT_MAGIC;
      o->version = 14;
      o->size = 3652;
      for (int i = 0; i < 64 * 4; ++i) o->val[i] = 0.5f;
      o->algo[PG_FILT(2, 0)] = MF_SEM;
      o->theme = 5;
      o->rnd[1][TP_CLOCK + 1] = 40;                       // (the grit's SHAPE's random: gone)
      o->rnd[1][TP_CUTOFF] = 30;                          // (the filter's: kept)
      for (int i = 0; i < OLD_LOCKS; ++i) o->lock[i].lane = 0xFF;
      o->lock[0].lane = 2, o->lock[0].step = 3, o->lock[0].tp = TP_CLOCK + 3, o->lock[0].v = 9;   // (gone)
      o->lock[1].lane = 2, o->lock[1].step = 4, o->lock[1].tp = TP_RESO, o->lock[1].v = 7;        // (kept)
      o->cable[0][0].src = SRC_L1, o->cable[0][0].dst = PG_BEND(0) * 4, o->cable[0][0].depth = 50;   // (gone)
      o->cable[0][1].src = SRC_L2, o->cable[0][1].dst = PG_FILT(0, 0) * 4, o->cable[0][1].depth = 40;
      project_t* q14 = reinterpret_cast<project_t*>(b14);
      int ok14 = eng_upgrade(q14, 3652);
      printf("a version 14 project: %s, theme %d, bits %.2f, glitch %.2f size %.0f, filter %d %.2f, randoms %d %d, locks %d %d,"
             " cables %d %d\n", ok14 ? "read" : "REFUSED", q14->theme, q14->val[PG_BEND(1) * 4], q14->val[PG_GLITCH(1) * 4],
             q14->val[PG_GLITCH(1) * 4 + 1], q14->algo[PG_FILT(2, 0)], q14->val[PG_FILT(2, 1) * 4], q14->rnd[1][TP_CLOCK + 1],
             q14->rnd[1][TP_CUTOFF], q14->lock[0].lane, q14->lock[1].lane, q14->cable[0][0].depth, q14->cable[0][1].depth);
      if (!ok14 || !eng_check(q14) || q14->theme != 5 || q14->val[PG_BEND(1) * 4] != 0.0f ||
          q14->val[PG_GLITCH(1) * 4] != 0.0f || q14->val[PG_GLITCH(1) * 4 + 1] != static_cast<float>(GL_16) ||
          q14->algo[PG_FILT(2, 0)] != MF_SEM || q14->val[PG_FILT(2, 1) * 4] != 0.5f || q14->rnd[1][TP_CLOCK + 1] != 0 ||
          q14->rnd[1][TP_CUTOFF] != 30 || q14->lock[0].lane != 0xFF || q14->lock[1].lane != 2 ||
          q14->cable[0][0].depth != 0 || q14->cable[0][1].depth != 40)
        ++failures, printf("FAIL: the version 14 project\n");
    }
    for (int s2 = 0; s2 < 16; ++s2) eng_trig_set(0, s2, 1);   // the glitch: the steps playing
    p->val[PG_GLITCH(0) * 4 + 0] = 1.0f;
    p->val[PG_GLITCH(0) * 4 + 1] = static_cast<float>(GL_16);
    eng_play(1);
    int seen2 = 0, reps = 0;
    float sp = 1.0f, gpk = 0.0f;
    for (int b2 = 0; b2 < 600; ++b2) {
      eng_render(l, r, n);
      seen2 |= 1 << eng_glitch_state(0, &reps, &sp);
      for (uint32_t i = 0; i < n; ++i) gpk = std::isfinite(l[i]) ? std::max(gpk, std::fabs(l[i])) : 1e9f;
    }
    int reps0 = reps;
    p->val[PG_GLITCH(0) * 4 + 2] = 1.0f;                 // BEND: each repeat higher; DROP: accidents
    p->val[PG_GLITCH(0) * 4 + 3] = 1.0f;
    float spmax = 0.0f;                  // (the fastest a repeat went: BEND up, each one faster)
    for (int b2 = 0; b2 < 600; ++b2) {
      eng_render(l, r, n);
      eng_glitch_state(0, &reps, &sp);
      spmax = std::max(spmax, sp);
      for (uint32_t i = 0; i < n; ++i) gpk = std::isfinite(l[i]) ? std::max(gpk, std::fabs(l[i])) : 1e9f;
    }
    sp = spmax;
    eng_play(0);
    printf("glitch: states seen %d (taking %d, repeating %d), %d repeats, peak %.2f, bent speed %.2f\n", seen2,
           (seen2 >> 1) & 1, (seen2 >> 2) & 1, reps0, gpk, sp);
    if (!((seen2 >> 1) & 1) || !((seen2 >> 2) & 1) || reps0 < 1 || gpk > 2.0f || !(sp > 1.0f))
      ++failures, printf("FAIL: the glitch\n");
    eng_defaults(p);
  }

  // 28. 0.3.9's project (version 9, the layout of 0.4): Warps' knobs on their new faces (its lane's lock with them),
  // Tides' cables to the LFOs (A1..A3) or gone; the LFOs (a square synced a beat: two edges a beat); the delay FREE
  // (its time kept: a dotted eighth at 120 BPM, 375 ms); SCALE on: FREQ in the scale too (a semitone up, C# out of C
  // major: the same pitch as none), off: a semitone higher
  {
    struct v9 {                                          // (0.3.9's layout: no random dose)
      uint32_t magic;
      uint16_t version, size;
      float val[52 * 4];
      uint8_t algo[52];
      uint8_t out_mode, warps_hq, theme;
      cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
      uint8_t trig[ENG_LANES][ENG_STEPS / 8];
      int8_t rnd[ENG_LANES][25];
      uint16_t seed[ENG_LANES];
      plock_t lock[OLD_LOCKS];
      uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
      uint8_t endless_len[ENG_TRACKS];
      int8_t endless_key[ENG_TRACKS];
    };
    static uint8_t buf[sizeof(project_t)];
    static v9 old;
    project_t* q = reinterpret_cast<project_t*>(buf);
    v9* o9 = &old;
    eng_blank(q);
    o9->magic = PROJECT_MAGIC;
    o9->version = 9;
    o9->size = sizeof(v9);
    for (int i = 0; i < 52 * 4; ++i) o9->val[i] = q->val[i];
    for (int i = 0; i < 52; ++i) o9->algo[i] = q->algo[i];
    old_locks(o9->lock, q->lock);
    project_t* w = q;                                    // (made as 0.4's, then copied into the old layout)
    static const float kOld[8] = {0.6f, 0.3f, 0.7f, 0.8f, static_cast<float>(WC_T3), 1.0f, 1.0f, 0.0f};
    for (int k = 0; k < 8; ++k) q->val[PG_WARPS(k / 4) * 4 + k % 4] = kOld[k];   // 0.3's: ALGO TIMBRE L1 L2, CAR MOD DRY MODE
    q->lock[0] = {LANE_WARPS, 3, 0, 100};                // its ALGORITHM on step 4
    q->rnd[LANE_WARPS][1] = 20;                          // TIMBRE's random
    q->cable[0][0] = {1, PG_WARPS(0) * 4 + 1, 50, 0};    // Tides A1 -> Warps' TIMBRE
    q->cable[0][1] = {4, 5, 20, 0};                      // Tides A4 -> track 1's HARMONICS: gone
    q->cable[4][0] = {2, 12 * 4 + 0, 30, 0};             // Tides A2 -> Tides A's frequency: gone
    for (int i = 0; i < 52 * 4; ++i) o9->val[i] = w->val[i];
    old_locks(o9->lock, w->lock);
    for (int g = 0; g <= ENG_TRACKS; ++g)
      for (int c = 0; c < ENG_CABLES; ++c) o9->cable[g][c] = w->cable[g][c];
    for (int l = 0; l < ENG_LANES; ++l)
      for (int i = 0; i < 25; ++i) o9->rnd[l][i] = w->rnd[l][i];
    o9->theme = 3;
    o9->trig[2][1] = 0x24;
    o9->endless_key[1] = -5;
    for (uint32_t i = 0; i < sizeof(v9); ++i) buf[i] = reinterpret_cast<uint8_t*>(o9)[i];
    int ok = eng_upgrade(q, sizeof(v9));
    printf("a version 9 project (%d bytes, %d now)", static_cast<int>(sizeof(v9)), static_cast<int>(sizeof(project_t)));
    if (q->theme != 3 || q->trig[2][1] != 0x24 || q->endless_key[1] != -5 || q->dice != 0)
      ++failures, printf(" FAIL: its layout");
    printf("\n");
    printf("a version 9 project: %s, Warps %.0f %.0f %.2f %.2f / %.2f %.2f %.2f %.0f, lock tp %d, random %d, cables %d->%d %d %d\n",
           ok ? "read" : "REFUSED", q->val[PG_WARPS(0) * 4], q->val[PG_WARPS(0) * 4 + 1], q->val[PG_WARPS(0) * 4 + 2],
           q->val[PG_WARPS(0) * 4 + 3], q->val[PG_WARPS(1) * 4], q->val[PG_WARPS(1) * 4 + 1], q->val[PG_WARPS(1) * 4 + 2],
           q->val[PG_WARPS(1) * 4 + 3], q->lock[0].tp, q->rnd[LANE_WARPS][4], q->cable[0][0].src, q->cable[0][0].dst,
           q->cable[0][1].depth, q->cable[4][0].depth);
    if (!ok || !eng_check(q) || q->val[PG_WARPS(0) * 4] != WC_T3 || q->val[PG_WARPS(0) * 4 + 2] != 0.6f ||
        q->val[PG_WARPS(0) * 4 + 3] != 1.0f || q->val[PG_WARPS(1) * 4] != 0.3f || q->val[PG_WARPS(1) * 4 + 2] != 0.8f ||
        q->val[PG_WARPS(1) * 4 + 3] != WM_DRY || q->lock[0].tp != 2 || q->rnd[LANE_WARPS][4] != 20 ||
        q->rnd[LANE_WARPS][1] != 0 || q->cable[0][0].src != SRC_L1 || q->cable[0][0].dst != PG_WARPS(1) * 4 ||
        q->cable[0][1].depth != 0 || q->cable[4][0].depth != 0)
      ++failures, printf("FAIL: the version 9 project\n");

    eng_play(0);                                          // the LFOs
    eng_defaults(p);
    p->val[PG_LFO(0) * 4 + 0] = static_cast<float>(LW_SQUARE);
    p->val[PG_LFO(0) * 4 + 1] = 2.5f / 9.0f;             // 1 / beat
    p->val[PG_LFO(0) * 4 + 2] = static_cast<float>(LM_SYNC);
    p->val[PG_LFO(0) * 4 + 3] = 0.0f;
    p->val[PG_LFO(2) * 4 + 0] = static_cast<float>(LW_SH);
    eng_play(1);
    int flips = 0, sh = 0;
    float last = eng_src(SRC_L1), lsh = eng_src(SRC_L3);
    for (int b = 0; b < 44100 * 4 / static_cast<int>(n); ++b) {
      eng_render(l, r, n);
      if ((eng_src(SRC_L1) > 0) != (last > 0)) ++flips;
      if (eng_src(SRC_L3) != lsh) ++sh;
      last = eng_src(SRC_L1);
      lsh = eng_src(SRC_L3);
    }
    eng_play(0);
    char a[16], b2[16];
    eng_param_text(PG_DELAY(0) * 4, p->val[PG_DELAY(0) * 4], a, sizeof a);
    eng_delay_mode(DL_FREE);
    eng_param_text(PG_DELAY(0) * 4, p->val[PG_DELAY(0) * 4], b2, sizeof b2);
    eng_delay_mode(DL_SYNC);
    float back = p->val[PG_DELAY(0) * 4];
    printf("LFOs: a square a beat %d edges in 4 s (16 expected), S&H %d values; the delay %s, FREE %s, back %.0f\n", flips,
           sh, a, b2, back);
    if (flips < 15 || flips > 17 || sh < 1 || strcmp(b2, "375 ms") || back != 7.0f) ++failures, printf("FAIL: the LFOs or the delay\n");

    double hz[3];
    for (int c = 0; c < 3; ++c) {                         // C3 held; FREQ up a semitone; SCALE: major, then off
      eng_blank(p);
      for (int t = 1; t < ENG_TRACKS; ++t) p->val[PG_MIX * 4 + t] = 0.0f;
      p->val[PG_TRACK(0, 1) * 4 + 0] = 1.0f;
      p->val[PG_TRACK(0, 0) * 4 + 3] = 0.0f;              // (the FM's INDEX 0, MOVE flat: a sine, two crossings a
      p->val[PG_TRACK(0, 1) * 4 + 2] = 0.0f;              // cycle)
      p->val[PG_REVERB * 4 + 3] = 0.0f;
      p->val[PG_DELAY(1) * 4 + 3] = 0.0f;
      p->val[PG_TRACK(0, 0) * 4 + 0] = c ? 1.0f / 24.0f : 0.0f;
      p->val[PG_GLOBAL * 4 + 1] = c == 2 ? 0.0f : 1.0f;
      eng_key(0, 48, 1);
      for (int k = 0; k < 100; ++k) eng_render(l, r, n);
      int cross = 0;
      float pv = 0;
      for (int k = 0; k < 344; ++k) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if ((l[i] >= 0) != (pv >= 0)) ++cross;
          pv = l[i];
        }
      }
      eng_key(0, 48, 0);
      hz[c] = cross * 0.5;
    }
    printf("the scale: C3 %.0f crossings a second, FREQ +1 semitone in C major %.0f, scale off %.0f\n", hz[0], hz[1], hz[2]);
    if (std::fabs(hz[1] - hz[0]) > hz[0] * 0.01 || hz[2] < hz[0] * 1.04 || hz[2] > hz[0] * 1.08)
      ++failures, printf("FAIL: FREQ and the scale\n");
    eng_defaults(p);
  }

  // 29. the random dose: at 0 every trig plays the knobs; at 50 % each trig of each track throws every sound parameter
  // of its own anew (the pitches in whole semitones, the timbres, the gate, the bits and the glitch, the filter, the EQ, the pan:
  // another value at each trig, within a quarter of its span: the knob's travel squared), never what leaves the scale
  // (the pitch envelope, the VA's detune), the levels, the time; the sends now and then, lightly, upwards (a trig in
  // seven at 100 %); the effects' colours anew at each beat, Warps' ALGORITHM from mark to mark; at 100 % its whole
  // span; back to 0: the knobs again at the next trig
  {
    eng_play(0);
    eng_defaults(p);
    for (int g = 0; g <= ENG_TRACKS; ++g)                 // (no cables: the LFOs would move what they patch; no locks,
      for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c].depth = 0;   // no randoms of the knobs: the dose alone)
    for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i].lane = 0xFF;
    for (int l = 0; l < ENG_LANES; ++l)
      for (int tp = 0; tp < ENG_NTP; ++tp) p->rnd[l][tp] = 0;
    p->algo[PG_FILT(1, 0)] = MF_LADDER;                  // (a filter to throw)
    p->val[PG_WARPS(0) * 4 + 2] = 0.4f;                  // (Warps' ALGORITHM on a mark)
    const int fdbk = PG_DELAY(0) * 4 + 1, wa = PG_WARPS(0) * 4 + 2;
    struct Stat { int trigs, wrong, kept_wrong, changed, marks_off, sends, send_wrong, off_scale; float far, fx_lo, fx_hi; };
    auto run = [&](int dose, float bars) {
      Stat st = {0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 1e9f, -1e9f};
      p->dice = static_cast<uint8_t>(dose);
      uint32_t r0[ENG_TRACKS];
      for (int t = 0; t < ENG_TRACKS; ++t) r0[t] = eng_rises(t);
      static float last[ENG_TRACKS][ENG_NTP];
      int seen[ENG_TRACKS] = {0, 0, 0, 0};
      float a = static_cast<float>(dose) * static_cast<float>(dose) * 0.0001f;
      for (uint32_t b = 0; b < static_cast<uint32_t>(bars * 2.0f * 44100.0f / n); ++b) {
        eng_render(l, r, n);
        float fx = eng_final(fdbk);
        st.fx_lo = std::min(st.fx_lo, fx), st.fx_hi = std::max(st.fx_hi, fx);
        float m = eng_final(wa) * 5.0f;
        st.marks_off += std::fabs(m - std::floor(m + 0.5f)) > 1e-4f;
        for (int t = 0; t < ENG_TRACKS; ++t) {
          if (eng_rises(t) == r0[t]) continue;
          r0[t] = eng_rises(t);
          ++st.trigs;
          int diff = 0, sent = 0;
          for (int tp = 0; tp < ENG_NTP; ++tp) {
            int param = eng_track_param(t, tp);
            if (param < 0 || tp == TP_NOTE || tp == TP_PROB || tp == TP_RATCHET || tp == TP_CURVE) continue;   // (0.16.8:
                                                  // the ratchets thrown too, their test 60)
            float now = eng_now(param), dev = now - eng_final(param), sp = eng_dice_span(param);
            if (tp == TP_SEND || tp == TP_DSEND) {        // (now and then, upwards, lightly)
              sent += dev > 1e-6f;
              st.send_wrong += dev < -1e-6f || dev > 0.35f + 1e-4f;
              continue;
            }
            if (tp == TP_PITCHENV) st.off_scale += std::fabs(dev) > 1e-6f;   // (the pitch envelope; FREQ:
            if (tp == 0) st.off_scale += std::fabs(dev * 24.0f - std::floor(dev * 24.0f + 0.5f)) > 1e-3f;   // semitones)
            if (tp == TP_LEVEL && dev <= 1e-6f && dev >= -0.35f - 1e-4f) continue;   // (0.16.8: a ratchet's hits
                                                  // softer with CURVE to the right, the dose throwing it: as set)
            if (sp == 0.0f) {
              st.kept_wrong += std::fabs(dev) > 1e-6f;   // (the sends, the levels, the time: the knobs)
              continue;
            }
            if (std::fabs(dev) > a * sp + (tp == 0 ? 1.0f / 48.0f : 0.0f) + 1e-4f) ++st.wrong;
            st.far = std::max(st.far, std::fabs(dev) / sp);
            diff += seen[t] && std::fabs(now - last[t][tp]) > 1e-6f;
            last[t][tp] = now;
          }
          if (dose && seen[t] && !diff) {                 // (a ratchet's hit, the same trig's: 0.16.8's dose adds
            --st.trigs;                                   // them; not a trig)
            continue;
          }
          st.changed += diff >= 8;                        // (another sound: most of its parameters elsewhere)
          st.sends += sent > 0;
          seen[t] = 1;
        }
      }
      return st;
    };
    eng_fresh();
    eng_play(1);
    Stat s0 = run(0, 2.0f), s50 = run(50, 4.0f), s100 = run(100, 4.0f);
    run(0, 0.5f);                                        // (back to 0: the knobs again from the next trig)
    Stat back = run(0, 2.0f);
    float k = p->val[fdbk];
    printf("the random dose: at 0 %d trigs, %d moved; at 50 %% %d trigs, %d of them another sound, as far as %.2f of "
           "the span, %d past it, %d kept parameters moved, sends on %d, the delay's FEEDBACK %.2f..%.2f (its knob "
           "%.2f), Warps off its marks %d blocks; at 100 %% as far as %.2f (%d past it, %d kept moved), sends on %d trigs "
           "of %d; off the scale %d; back at 0 %d moved\n", s0.trigs, s0.wrong + s0.kept_wrong + s0.sends + static_cast<int>(s0.far > 0.0f),
           s50.trigs, s50.changed, s50.far, s50.wrong, s50.kept_wrong, s50.sends, s50.fx_lo, s50.fx_hi, k,
           s50.marks_off + s100.marks_off, s100.far, s100.wrong, s100.kept_wrong, s100.sends, s100.trigs, s50.off_scale + s100.off_scale,
           back.wrong + back.kept_wrong + back.sends + static_cast<int>(back.far > 0.0f));
    if (s0.trigs < 10 || s0.far > 0.0f || s0.kept_wrong || s0.sends || s0.fx_hi - s0.fx_lo > 1e-6f || s50.trigs < 20 ||
        s50.changed < s50.trigs - 8 || s50.far > 0.25f + 1e-3f || s50.far < 0.15f || s50.wrong || s50.kept_wrong ||
        s50.fx_hi - s50.fx_lo < 0.01f || s50.fx_hi > k + 0.05f + 1e-4f || s50.fx_lo < k - 0.05f - 1e-4f ||
        s50.marks_off + s100.marks_off || s100.far < 0.6f || s100.wrong || s100.kept_wrong ||
        s100.sends < s100.trigs / 20 || s100.sends > s100.trigs * 3 / 10 || s50.sends > s50.trigs / 8 ||
        s50.send_wrong + s100.send_wrong || s50.off_scale + s100.off_scale || back.far > 0.0f || back.kept_wrong ||
        back.sends)
      ++failures, printf("FAIL: the random dose\n");
    eng_play(0);
    eng_defaults(p);
  }

  // 30. STOP twice: the kick ringing on a decay held at full (an LFO on it), the delay and the reverb full of it; cut,
  // all silent within three blocks (the fade, the modules made new); then a trig plays again
  {
    eng_play(0);
    eng_defaults(p);
    p->algo[PG_TRACK(0, 0)] = static_cast<uint8_t>(eng_model_find(MK_DRUM, 0));                         // the kick
    p->val[PG_TRACK(0, 0) * 4 + 3] = 1.0f;                // its decay (MORPH) at full
    p->val[PG_TRACK(0, 1) * 4 + 0] = 1.0f;                // and Plaits' DECAY
    p->val[PG_RSEND * 4 + 0] = 1.0f;
    p->val[PG_DSEND * 4 + 0] = 1.0f;
    p->val[PG_MIX * 4 + 0] = 1.0f;
    eng_play(1);
    for (int b = 0; b < 44100 / static_cast<int>(n); ++b) eng_render(l, r, n);
    eng_play(0);
    float ring = 0.0f;
    for (int b = 0; b < 44100 / 4 / static_cast<int>(n); ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) ring = std::max(ring, std::fabs(l[i]));
    }
    eng_cut();
    float faded = 0.0f, after = 0.0f;
    for (int b = 0; b < 3; ++b) eng_render(l, r, n);
    int pending = eng_cut_pending();
    for (int b = 0; b < 44100 / 2 / static_cast<int>(n); ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) after = std::max(after, std::max(std::fabs(l[i]), std::fabs(r[i])));
    }
    (void)faded;
    eng_key(0, 48, 1);                                    // a key: the kick again
    float back = 0.0f;
    for (int b = 0; b < 40; ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) back = std::max(back, std::fabs(l[i]));
    }
    eng_key(0, 48, 0);
    printf("STOP twice: %.3f stopped (the kick ringing, its echoes), %.6f after the cut, %.3f at the next key\n", ring,
           after, back);
    if (ring < 0.01f || after > 1e-6f || pending || back < 0.01f)
      ++failures, printf("FAIL: STOP twice\n");
    eng_defaults(p);
  }

  // 31. Warps on its own oscillator (a saw, CROSSFADE: the saw heard over T2): playing, it sounds; stopped, it fades
  // out (~30 ms) and Warps stops running: what is left is T2's own tail, as loud as with Warps off; a key held, it
  // sounds again; PLAY again, it is back
  {
    eng_play(0);
    eng_defaults(p);
    for (int t = 0; t < ENG_TRACKS; ++t) p->val[PG_MIX * 4 + t] = t == 1 ? 1.0f : 0.0f;   // T2 alone
    for (int t = 0; t < ENG_TRACKS; ++t) p->val[PG_RSEND * 4 + t] = p->val[PG_DSEND * 4 + t] = 0.0f;
    p->val[PG_WARPS(0) * 4 + 1] = 1.0f;                   // by T2 (its output in T2's place)
    p->val[PG_WARPS(0) * 4 + 2] = 0.0f;                   // CROSSFADE
    p->val[PG_WARPS(0) * 4 + 3] = 1.0f;
    p->val[PG_WARPS(1) * 4 + 3] = static_cast<float>(WM_WARPS);
    eng_steps_clear(LANE_WARPS);
    auto loud = [&](int blocks) {
      float m = 0.0f;
      for (int b = 0; b < blocks; ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) m = std::max(m, std::fabs(l[i]));
      }
      return m;
    };
    float play_pk[2], stop_pk[2];
    uint32_t wl = 1;
    for (int k = 0; k < 2; ++k) {                         // Warps off, then on its saw: the same from a cut
      p->val[PG_WARPS(0) * 4] = k ? WC_OSC3 : WC_OFF;
      eng_cut();
      loud(3);
      eng_play(1);
      play_pk[k] = loud(300);
      eng_play(0);
      loud(40);                                           // (the fade)
      stop_pk[k] = loud(100);
      if (k) wl = eng_task_load(4);
    }
    eng_key(1, 48, 1);
    float key_pk = loud(40);
    eng_key(1, 48, 0);
    eng_play(1);
    float back_pk = loud(100);
    eng_play(0);
    printf("Warps' saw: playing %.3f (off %.3f), stopped %.6f (off %.6f, its load %u), a key held %.3f, playing again "
           "%.3f\n", play_pk[1], play_pk[0], stop_pk[1], stop_pk[0], wl, key_pk, back_pk);
    if (play_pk[1] < 0.01f || stop_pk[1] > stop_pk[0] * 1.2f + 1e-3f || wl != 0 || key_pk < 0.01f ||
        back_pk < 0.01f)
      ++failures, printf("FAIL: Warps' oscillator stopped\n");
    eng_defaults(p);
  }

  // 32. the stress test: it plays (every track, Warps, OTT), no NaN, nothing stuck; its weight beside the demo's on
  // this computer (the FM-1's figures are on its machine page); on the FM drums too, lighter
  {
    auto weigh = [&](int stress, float* pk, int* sounding) {
      eng_play(0);
      if (stress == 2) eng_stress_fm(p);
      else if (stress) eng_stress(p);
      else eng_defaults(p);
      eng_fresh();                                        // (as the screen does: the tests before's locks gone)
      eng_cut();
      for (int b = 0; b < 3; ++b) eng_render(l, r, n);
      eng_play(1);
      for (int b = 0; b < 200; ++b) eng_render(l, r, n);
      int nan = 0;
      float m = 0.0f, tm[ENG_TRACKS] = {0, 0, 0, 0};
      clock_t c0 = clock();
      for (int b = 0; b < 44100 * 4 / static_cast<int>(n); ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i]) || !std::isfinite(r[i])) ++nan;
          m = std::max(m, std::fabs(l[i]));
        }
        for (int t = 0; t < ENG_TRACKS; ++t) tm[t] = std::max(tm[t], eng_meter(t));
      }
      double secs = static_cast<double>(clock() - c0) / CLOCKS_PER_SEC;
      int s2 = 0;
      for (int t = 0; t < ENG_TRACKS; ++t) s2 += tm[t] > 0.01f;
      *pk = nan ? -1.0f : m;
      *sounding = s2;
      eng_play(0);
      return secs;
    };
    float pk_demo, pk_stress;
    int snd_demo, snd_stress;
    double t_demo = weigh(0, &pk_demo, &snd_demo);
    double t_stress = weigh(1, &pk_stress, &snd_stress);
    int warps = eng_warps_algo(), bus = p->algo[PG_COMP], cables = 0;
    for (int g = 0; g <= ENG_TRACKS; ++g)
      for (int c = 0; c < ENG_CABLES; ++c) cables += p->cable[g][c].depth != 0;
    printf("the stress test: peak %.3f, %d tracks sounding, Warps %d, bus %d, %d cables; %.2fx the demo's time here\n",
           pk_stress, snd_stress, warps, bus, cables, t_stress / (t_demo > 0.0 ? t_demo : 1.0));
    if (pk_stress < 0.05f || pk_stress > 1.0f || snd_stress != ENG_TRACKS || warps != 2 || bus != MB_OTT || cables != 20)
      ++failures, printf("FAIL: the stress test\n");
    float pk_fm;
    int snd_fm;
    double t_fm = weigh(2, &pk_fm, &snd_fm);
    printf("the stress test on the FM drums: peak %.3f, %d tracks sounding; %.2fx the stress test's time here\n", pk_fm,
           snd_fm, t_fm / (t_stress > 0.0 ? t_stress : 1.0));
    if (pk_fm < 0.05f || pk_fm > 1.0f || snd_fm != ENG_TRACKS || t_fm > t_stress)
      ++failures, printf("FAIL: the stress test on the FM drums\n");
    eng_defaults(p);
  }

  // 33. the delay never runs away: FEEDBACK at full (a loop), the repeats held as they are (a burst, then 6 s: as loud
  // as at 1 s); the resonance and the drive up, a low pass on its peak: what would grow is held at its ceiling
  {
    static uint8_t dmem[200000] __attribute__((aligned(16)));
    if (mdl_size() > sizeof dmem) {
      ++failures, printf("FAIL: the delay's memory\n");
    } else {
      static float in[128], ol[128], orr[128];
      float lv[3][2];
      for (int c = 0; c < 3; ++c) {
        mdl_init(dmem);
        if (c == 0) mdl_set(dmem, 11025.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f);   // the loop, clean
        else if (c == 1) mdl_set(dmem, 11025.0f, 1.0f, -0.3f, 1.0f, 1.0f, 1, 0.3f);   // all up, ping-pong
        else mdl_set(dmem, 11025.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0.0f);   // half: dies away
        for (int b = 0; b < 44100 * 7 / 128; ++b) {
          for (int i = 0; i < 128; ++i) {
            int k = b * 128 + i;
            in[i] = k < 2000 ? 0.3f * std::sin(static_cast<float>(k) * 0.0627f) : 0.0f;   // a 440 Hz burst
          }
          mdl_process(dmem, in, ol, orr, 128);
          float pk = 0.0f;
          for (int i = 0; i < 128; ++i) pk = std::max(pk, std::max(std::fabs(ol[i]), std::fabs(orr[i])));
          int sec = (b * 128) / 44100;
          if (b == 0) lv[c][0] = lv[c][1] = 0.0f;
          if (sec == 1) lv[c][0] = std::max(lv[c][0], pk);
          if (sec == 6) lv[c][1] = std::max(lv[c][1], pk);
        }
      }
      printf("the delay: the loop %.3f at 1 s, %.3f at 6 s; all up %.3f, %.3f; half feedback %.3f, %.5f\n", lv[0][0],
             lv[0][1], lv[1][0], lv[1][1], lv[2][0], lv[2][1]);
      if (lv[0][1] < lv[0][0] * 0.7f || lv[0][1] > lv[0][0] * 1.3f || lv[1][1] > 0.6f || lv[1][1] < 0.05f ||
          lv[2][1] > lv[2][0] * 0.05f)
        ++failures, printf("FAIL: the delay's feedback\n");
    }
  }

  // 34. the FM drums (the LXR-02's voices): a model chosen on a track brings its kit to its knobs, which name its
  // parameters and write them as the LXR-02 does; each of the four sounds, triggered by the steps, its own pitch at
  // a step's note at octave 0 (the drum's kit: G1, 49 Hz); STOP twice cuts them; the stress test on them plays and
  // costs less here than on Plaits
  {
    eng_play(0);
    eng_defaults(p);
    eng_fresh();
    static const char* const kFirst[4] = {"FM", "Noise", "Mod 1", "Mod 1"};
    int names_ok = 1, kit_ok = 1, sound_ok = 1;
    float pk[4];
    char tx[24];
    for (int m = 0; m < LX_MODELS; ++m) {
      eng_set_model(0, eng_model_find(MK_LXR, m));
      float k[LX_KNOBS];
      lx_defaults(m, k);
      for (int i = 0; i < LX_KNOBS; ++i) {
        int param = PG_TRACK(0, 0) * 4 + 1 + i;
        float u = eng_pdef(param)->kind == K_BIP ? (p->val[param] + 1.0f) * 0.5f : p->val[param];
        if (std::fabs(u - k[i]) > 1e-6f || eng_lx_knob(param) != i) kit_ok = 0;
      }
      if (std::strcmp(eng_param_name(PG_TRACK(0, 0) * 4 + 1), kFirst[m]) != 0 || eng_lx_knob(PG_TRACK(0, 0) * 4) >= 0 ||
          eng_lx_knob(PG_TRACK(0, 2) * 4 + 1) >= 0)
        names_ok = 0;
      eng_cut();
      for (int b = 0; b < 3; ++b) eng_render(l, r, n);
      int nan = 0;
      pk[m] = 0.0f;
      for (int b = 0; b < 200; ++b) {
        if (b % 40 == 0) eng_key(0, 48, 1);
        if (b % 40 == 10) eng_key(0, 48, 0);
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i])) nan = 1;
          pk[m] = std::max(pk[m], std::fabs(l[i]));
        }
      }
      if (nan || pk[m] < 0.02f) sound_ok = 0;
    }
    eng_set_model(0, eng_model_find(MK_LXR, 0));                           // the drum: its Wave written, its Decay 0..127
    eng_param_text(PG_TRACK(0, 1) * 4 + 1, p->val[PG_TRACK(0, 1) * 4 + 1], tx, sizeof tx);
    int text_ok = std::strcmp(tx, "Sine") == 0;
    eng_param_text(PG_TRACK(0, 1) * 4 + 0, 21.0f / 127.0f, tx, sizeof tx);
    text_ok &= std::strcmp(tx, "21") == 0;
    // a detent: the next of its 128 values (DECAY 21 to 22; on a bipolar knob, P.AMT 44 to 41), a wave's next
    float st = eng_lx_step(PG_TRACK(0, 1) * 4 + 0, p->val[PG_TRACK(0, 1) * 4 + 0], 1);
    eng_param_text(PG_TRACK(0, 1) * 4 + 0, st, tx, sizeof tx);
    text_ok &= std::strcmp(tx, "22") == 0;
    st = eng_lx_step(PG_TRACK(0, 2) * 4 + 0, p->val[PG_TRACK(0, 2) * 4 + 0], -3);
    eng_param_text(PG_TRACK(0, 2) * 4 + 0, st, tx, sizeof tx);
    text_ok &= std::strcmp(tx, "41") == 0;
    st = eng_lx_step(PG_TRACK(0, 1) * 4 + 1, p->val[PG_TRACK(0, 1) * 4 + 1], 1);
    eng_param_text(PG_TRACK(0, 1) * 4 + 1, st, tx, sizeof tx);
    text_ok &= std::strcmp(tx, "Tri") == 0;
    for (int x = 0; x < 128; ++x) {                       // every value of a bipolar knob reachable, written as itself
      float v = -1.0f;
      for (int i = 0; i < x; ++i) v = eng_lx_step(PG_TRACK(0, 2) * 4 + 0, v, 1);
      eng_param_text(PG_TRACK(0, 2) * 4 + 0, v, tx, sizeof tx);
      if (std::atoi(tx) != x) text_ok = 0;
    }
    // its pitch: a sine at the kit's coarse 31 (49 Hz) once the pitch envelope has gone (no click, no drive)
    p->val[PG_TRACK(0, 0) * 4 + 1] = 0.0f;                // FM 0
    p->val[PG_TRACK(0, 1) * 4 + 0] = 0.9f;                // a long decay
    p->val[PG_TRACK(0, 1) * 4 + 2] = -1.0f;               // Click 0
    p->val[PG_TRACK(0, 1) * 4 + 3] = -1.0f;               // Drive 0
    p->val[PG_TRACK(0, 2) * 4 + 0] = -1.0f;               // the pitch envelope 0
    p->val[PG_TRACK(0, 0) * 4 + 0] = 0.0f;                // FREQ 0
    eng_cut();
    for (int b = 0; b < 3; ++b) eng_render(l, r, n);
    eng_key(0, 48, 1);
    int cross = 0;
    float last = 0.0f;
    for (int b = 0; b < 344 * 2; ++b) {                   // 2 s: the crossings of the second one
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        if (b >= 344 && last < 0.0f && l[i] >= 0.0f) ++cross;
        last = l[i];
      }
    }
    eng_key(0, 48, 0);
    // STOP twice: silent
    eng_cut();
    for (int b = 0; b < 3; ++b) eng_render(l, r, n);
    float after = 0.0f;
    for (int b = 0; b < 20; ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) after = std::max(after, std::fabs(l[i]));
    }
    printf("FM drums: kits %s, names %s, text %s, peaks %.2f %.2f %.2f %.2f, the drum at 48: %d Hz (49), cut %.5f\n",
           kit_ok ? "ok" : "WRONG", names_ok ? "ok" : "WRONG", text_ok ? "ok" : "WRONG", pk[0], pk[1], pk[2], pk[3],
           cross, after);
    if (!kit_ok || !names_ok || !text_ok || !sound_ok || cross < 47 || cross > 51 || after > 1e-4f)
      ++failures, printf("FAIL: the FM drums\n");
    eng_defaults(p);
  }

  // 35. the FM drums' amplitude envelope keeps the LXR-02's double exactly (lxr_port.cc): its times, its slopes and
  // its curve over whole envelopes, each step against the same computed in double here
  {
    long steps = 0, wrong = 0;
    for (int d = 0; d < 128; ++d) {
      for (int w = 0; w < 2; ++w) {                       // slopeEg2_calcTime, attack and decay
        double amount = w ? (2 * 0.999 / (1.0 - 0.999)) : (2 * 0.99 / (1.0 - 0.99));
        const double val = d / 127.0;
        const float ret0 = (1.0 - ((1.0 + amount) * val / (1.0 + amount * std::fabs(val)))) * 2.f;
        float want = ret0 > 1.f ? 1.f : ret0;
        SlopeEg2 eg;
        slopeEg2_init(&eg);
        slopeEg2_setAttack(&eg, static_cast<uint8_t>(d), 0);
        slopeEg2_setDecay(&eg, static_cast<uint8_t>(d), 0);
        ++steps;
        if ((w ? eg.decay : eg.attack) != want) ++wrong;
      }
    }
    for (int s = 0; s < 128; s += 3)
      for (int dc = 0; dc < 128; dc += 13)
        for (int at = 0; at < 128; at += 31) {
          SlopeEg2 eg;
          slopeEg2_init(&eg);
          slopeEg2_setSlope(&eg, static_cast<uint8_t>(s));
          slopeEg2_setAttack(&eg, static_cast<uint8_t>(at), 0);
          slopeEg2_setDecay(&eg, static_cast<uint8_t>(dc), 0);
          double amount = ((s / 127.f) - 0.5f) * 1.999f, inv = -amount;
          double sl = 2 * amount / (1 - amount), isl = 2 * inv / (1 - inv), v = 0, A = eg.attack, D = eg.decay;
          int st = 1;
          slopeEg2_trigger(&eg);
          for (int b = 0; b < 3000 && st; ++b) {
            double out;
            if (st == 1) {
              v += A;
              if (v >= 1.0) v = 1.0, st = 2;
              out = (1.0 + isl) * v / (1.0 + isl * std::fabs(v));
            } else if (v > 0) {
              v -= D;
              out = (1.0 + sl) * v / (1.0 + sl * std::fabs(v));
            } else {
              out = 0, st = 0;
            }
            ++steps;
            if (slopeEg2_calc(&eg) != static_cast<float>(out)) ++wrong;
          }
        }
    printf("FM drums' envelope: %ld steps, %ld not the double's\n", steps, wrong);
    if (wrong) ++failures, printf("FAIL: the FM drums' envelope\n");
  }

  // 36. 0.4's, 0.5's and 0.5.6's projects (versions 10 and 11, 3104 bytes; 12 with its draws): read, as they were,
  // their filters off (and ready), their draws kept (12) or 0
  {
    struct V12 {                                         // (engine.cc's project_v12)
      uint32_t magic;
      uint16_t version, size;
      float val[52 * 4];
      uint8_t algo[52];
      uint8_t out_mode, warps_hq, theme, dice, dice_seed;
      cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
      uint8_t trig[ENG_LANES][ENG_STEPS / 8];
      int8_t rnd[ENG_LANES][25];
      uint16_t seed[ENG_LANES];
      plock_t lock[OLD_LOCKS];
      uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
      uint8_t endless_len[ENG_TRACKS];
      int8_t endless_key[ENG_TRACKS];
      uint8_t rseed[ENG_LANES][25];
    };
    for (int v = 10; v <= 12; ++v) {
      static project_t cur, q;
      static V12 o;
      eng_defaults(&cur);
      memset(&o, 0, sizeof o);
      o.magic = PROJECT_MAGIC;
      o.version = static_cast<uint16_t>(v);
      o.size = static_cast<uint16_t>(v == 12 ? sizeof o : 3104);
      for (int i = 0; i < 52 * 4; ++i) o.val[i] = cur.val[i];
      for (int i = 0; i < 52; ++i) o.algo[i] = cur.algo[i];
      o.algo[PG_TRACK(2, 0)] = 7;
      o.dice = 33;
      for (int l = 0; l < ENG_LANES; ++l) {
        for (int i = 0; i < 25; ++i) o.rnd[l][i] = cur.rnd[l][i], o.rseed[l][i] = static_cast<uint8_t>(v == 12 ? 5 : 99);
        for (int i = 0; i < ENG_STEPS / 8; ++i) o.trig[l][i] = cur.trig[l][i];
        o.seed[l] = cur.seed[l];
      }
      old_locks(o.lock, cur.lock);
      o.lock[0] = plock_t{2, 5, TP_NOTE, 200};
      memcpy(&q, &o, sizeof o);
      int bytes = v == 12 ? static_cast<int>(sizeof o) : 3104;
      int ok = eng_upgrade(&q, bytes) && eng_check(&q) && q.algo[PG_TRACK(2, 0)] == eng_model_find(MK_SQ1, 0) && q.dice == 33 &&
               q.rnd[3][TP_NOTE] == cur.rnd[3][TP_NOTE] && q.rseed[1][3] == (v == 12 ? 5 : 0) && !q.rseed[1][30] &&
               q.lock[0].lane == 2 && q.lock[0].tp == TP_NOTE && q.algo[PG_FILT(1, 0)] == MF_OFF &&
               q.val[PG_FILT(1, 0) * 4] > 0.5f && q.val[PG_MIX * 4 + 1] == cur.val[PG_MIX * 4 + 1];
      printf("a version %d project: %s\n", v, ok ? "read" : "REFUSED");
      if (!ok) ++failures, printf("FAIL: a version %d project\n", v);
    }
    struct V13 {                                         // (engine.cc's project_v13: 0.5.8's two filter pages a track)
      uint32_t magic;
      uint16_t version, size;
      float val[60 * 4];
      uint8_t algo[60];
      uint8_t out_mode, warps_hq, theme, dice, dice_seed;
      cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
      uint8_t trig[ENG_LANES][ENG_STEPS / 8];
      int8_t rnd[ENG_LANES][33];
      uint16_t seed[ENG_LANES];
      plock_t lock[OLD_LOCKS];
      uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
      uint8_t endless_len[ENG_TRACKS];
      int8_t endless_key[ENG_TRACKS];
      uint8_t rseed[ENG_LANES][33];
    };
    {                                                    // its filters: T2 a SEM high pass (its NOISE knob then on
      static project_t cur, q;                           // its second page), T3 a ladder band pass driven, a cable on
      static V13 o;                                      // T2's RESO
      eng_defaults(&cur);
      memset(&o, 0, sizeof o);
      o.magic = PROJECT_MAGIC;
      o.version = 13;
      o.size = sizeof o;
      for (int i = 0; i < 52 * 4; ++i) o.val[i] = cur.val[i];
      for (int i = 0; i < 52; ++i) o.algo[i] = cur.algo[i];
      for (int l = 0; l < ENG_LANES; ++l) {
        for (int i = 0; i < 33; ++i) o.rnd[l][i] = cur.rnd[l][i], o.rseed[l][i] = 7;
        for (int i = 0; i < ENG_STEPS / 8; ++i) o.trig[l][i] = cur.trig[l][i];
        o.seed[l] = cur.seed[l];
      }
      old_locks(o.lock, cur.lock);
      for (int t = 0; t < ENG_TRACKS; ++t) o.val[(52 + 2 * t) * 4] = 0.7f, o.val[(52 + 2 * t) * 4 + 3] = 0.4f;
      o.algo[52 + 2] = MF_SEM, o.algo[52 + 3] = MF_HP;
      o.val[(52 + 2) * 4] = 0.3f, o.val[(52 + 3) * 4] = 0.5f, o.val[(52 + 3) * 4 + 1] = 0.6f;
      o.algo[52 + 4] = MF_LADDER, o.algo[52 + 5] = MF_BP;
      o.val[(52 + 5) * 4] = 0.8f;
      o.cable[1][0] = cable_t{SRC_L1, static_cast<uint8_t>((52 + 2) * 4 + 1), 50, 0};
      memcpy(&q, &o, sizeof o);
      int ok = eng_upgrade(&q, sizeof o) && eng_check(&q) && q.algo[PG_FILT(1, 0)] == MF_SEM &&
               q.val[PG_FILT(1, 2) * 4] == MF_HP && std::fabs(q.val[PG_FILT(1, 2) * 4 + 1] - 0.47f) < 0.001f &&
               q.val[PG_FILT(1, 0) * 4] == 0.3f && q.val[PG_FILT(1, 1) * 4] == 0.0f && q.val[PG_FILT(1, 1) * 4 + 1] == 0.6f &&
               q.algo[PG_FILT(2, 0)] == MF_LADDER && q.val[PG_FILT(2, 2) * 4] == MF_BP && q.val[PG_FILT(2, 1) * 4] == 0.8f &&
               q.val[PG_FILT(2, 2) * 4 + 1] == 0.0f && q.algo[PG_FILT(0, 0)] == MF_OFF && q.val[PG_FILT(3, 0) * 4 + 3] == 0.4f &&
               q.cable[1][0].dst == PG_FILT(1, 0) * 4 + 1 && q.rseed[2][TP_NOTE] == 7 && q.rnd[2][TP_FTYPE] == 0;
      printf("a version 13 project (0.5.8's filters): %s\n", ok ? "read, its filters in their new places" : "REFUSED");
      if (!ok) ++failures, printf("FAIL: a version 13 project\n");
    }
  }

  // 39. a looped random brought back to 0 draws another loop: T4's looped tune, two passes the same; its knob back to
  // 0 and to the loop again (eng_rnd_redraw, as the screen does): another tune, itself a loop; T3's own loop as it was
  {
    auto tune = [&](int t, int* out, int nmax) {         // the notes of track t's next nmax trigs
      int got = 0;
      uint32_t r0 = eng_rises(t);
      while (got < nmax) {
        eng_render(l, r, n);
        if (eng_rises(t) != r0) {
          r0 = eng_rises(t);
          out[got++] = eng_note(t);
        }
      }
    };
    eng_play(0);
    eng_defaults(p);
    for (int t = 0; t < ENG_TRACKS; ++t) p->val[PG_STEP(t) * 4 + 3] = 16.0f;
    p->rnd[3][TP_NOTE] = -100;
    p->rnd[2][TP_NOTE] = -100;
    for (int t = 2; t < 4; ++t) p->rnd[t][TP_RATCHET] = 0;
    eng_fresh();
    eng_cut();
    for (int b = 0; b < 3; ++b) eng_render(l, r, n);
    eng_play(1);
    int c = 0;                                           // (T4's trigs a pass)
    for (int s2 = 0; s2 < 16; ++s2) c += eng_trig(3, s2);
    int a1[64], a2[64], b1[64], t3a[64], t3b[64];
    tune(3, a1, c);
    tune(3, a2, c);
    eng_play(0);
    eng_cut();
    for (int b = 0; b < 3; ++b) eng_render(l, r, n);
    eng_play(1);
    int c3 = 0;
    for (int s2 = 0; s2 < 16; ++s2) c3 += eng_trig(2, s2);
    tune(2, t3a, c3);
    p->rnd[3][TP_NOTE] = 0;                              // back to 0, then the loop side again
    eng_rnd_redraw(p, 3, TP_NOTE);
    p->rnd[3][TP_NOTE] = -100;
    eng_play(0);
    eng_cut();
    for (int b = 0; b < 3; ++b) eng_render(l, r, n);
    eng_play(1);
    tune(3, b1, c);
    eng_play(0);
    eng_cut();
    for (int b = 0; b < 3; ++b) eng_render(l, r, n);
    eng_play(1);
    tune(2, t3b, c3);
    int same = 1, differ = 0, t3same = 1;
    for (int i = 0; i < c; ++i) {
      if (a1[i] != a2[i]) same = 0;
      if (a1[i] != b1[i]) differ = 1;
    }
    for (int i = 0; i < c3; ++i)
      if (t3a[i] != t3b[i]) t3same = 0;
    printf("a looped random back to 0: T4's loop %s, then %s; T3's %s (%d and %d trigs a pass)\n",
           same ? "the same each pass" : "NOT A LOOP", differ ? "another" : "THE SAME", t3same ? "kept" : "CHANGED", c,
           c3);
    if (c < 3 || !same || !differ || !t3same) ++failures, printf("FAIL: a looped random's new draw\n");
    eng_play(0);
  }

  // 38. the random dose's fills: none up to 75 %; at 80 % on the last beat of every fourth bar, one track; at 100 % on
  // the last two beats of every bar, on every track that plays steps; a fill's trigs heard (more gates than at 75 %)
  {
    struct Run { uint32_t fills[ENG_TRACKS], rises; int bad_pos, bars_with, tracks_max, early; };
    auto run_dose = [&](int dose, Run* o) {
      eng_play(0);
      eng_defaults(p);
      for (int t = 0; t < ENG_TRACKS; ++t) p->val[PG_STEP(t) * 4 + 3] = 16.0f;   // (the bar: T1's step)
      p->dice = static_cast<uint8_t>(dose);
      p->dice_seed = 7;
      eng_fresh();
      eng_cut();
      for (int b = 0; b < 3; ++b) eng_render(l, r, n);
      uint32_t f0[ENG_TRACKS], r0 = 0;
      for (int t = 0; t < ENG_TRACKS; ++t) f0[t] = eng_fill_trigs(t), r0 += eng_rises(t);
      eng_play(1);
      int bar = 0, last = -1, minpos = 16, bar_tracks = 0, bar_has = 0;
      uint32_t prev[ENG_TRACKS];
      for (int t = 0; t < ENG_TRACKS; ++t) prev[t] = f0[t];
      *o = Run{{0, 0, 0, 0}, 0, 0, 0, 0, 0};
      while (bar < 16) {
        eng_render(l, r, n);
        int pos = eng_step_now(0);
        if (pos < last) {               // a new bar
          if (bar_has) ++o->bars_with;
          o->tracks_max = std::max(o->tracks_max, bar_tracks);
          bar_tracks = bar_has = 0;
          ++bar;
        }
        last = pos;
        for (int t = 0; t < ENG_TRACKS; ++t) {
          uint32_t f = eng_fill_trigs(t);
          if (f != prev[t]) {
            if (!(bar_has & (1 << t))) ++bar_tracks;
            bar_has |= 1 << t;
            minpos = std::min(minpos, pos);
            if (dose < 90 && bar % 4 != 3) ++o->early;   // (80 %: only every fourth bar)
            prev[t] = f;
          }
        }
      }
      for (int t = 0; t < ENG_TRACKS; ++t) o->fills[t] = eng_fill_trigs(t) - f0[t];
      for (int t = 0; t < ENG_TRACKS; ++t) o->rises += eng_rises(t);
      o->rises -= r0;
      o->bad_pos = minpos;
      eng_play(0);
    };
    Run d0, d75, d80, d100;
    run_dose(0, &d0);
    run_dose(75, &d75);
    run_dose(80, &d80);
    run_dose(100, &d100);
    uint32_t s0 = 0, s75 = 0, s80 = 0, s100 = 0;
    int t100 = 0;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      s0 += d0.fills[t], s75 += d75.fills[t], s80 += d80.fills[t], s100 += d100.fills[t];
      t100 += d100.fills[t] > 0;
    }
    int stepping = 0;
    for (int t = 0; t < ENG_TRACKS; ++t) stepping += eng_lane_has_trigs(t);
    printf("the dose's fills over 16 bars: 0 %%: %u, 75 %%: %u, 80 %%: %u (%d bars, %d track at most, from the "
           "sixteenth %d), 100 %%: %u (%d bars, on %d tracks of %d, from the sixteenth %d); gates %u at 75 %%, %u at "
           "100 %%\n", s0, s75, s80, d80.bars_with, d80.tracks_max, d80.bad_pos, s100, d100.bars_with, t100, stepping,
           d100.bad_pos, d75.rises, d100.rises);
    if (s0 || s75 || !s80 || d80.bars_with > 4 || d80.tracks_max != 1 || d80.bad_pos < 12 || d80.early ||
        d100.bars_with < 15 || t100 != stepping || d100.bad_pos < 8 || d100.rises <= d75.rises)
      ++failures, printf("FAIL: the dose's fills\n");
  }

  // 40. the tracks' filters (mi_filter.cc), each model and type: low pass keeps the lows and cuts the highs, high pass
  // the reverse, band pass its middle; pinged in silence, it rings; driven anyhow (any knobs, FM, noise, pings), never
  // a NaN, never past 2; its envelope opens the cutoff at a trig; OFF leaves the sound as it is. Its time here
  {
    auto tone_db = [&](int m, int ty, float hz, float cut) {   // a sine's level through it, dB
      mf_init();
      float k[MF_KNOBS] = {cut, 0.2f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f}, buf[128];
      double e = 0.0, ph = 0.0;
      for (int b = 0; b < 200; ++b) {
        mf_set(0, m, ty, k, 60.0f);
        for (int i = 0; i < 128; ++i, ph += 6.283185307 * hz / 44100.0) buf[i] = 0.1f * static_cast<float>(std::sin(ph));
        mf_process(0, buf, 128);
        if (b >= 100)
          for (int i = 0; i < 128; ++i) e += static_cast<double>(buf[i]) * buf[i];
      }
      return 10.0 * std::log10(e / (100.0 * 128.0) / 0.005 + 1e-12);
    };
    int ok = 1;
    for (int m = MF_LADDER; m < MF_VOX; ++m) {           // (VOX: formants, not a pass band)
      float c = 0.62f;                                   // (~1.3 kHz)
      double lp_lo = tone_db(m, MF_LP, 150.0f, c), lp_hi = tone_db(m, MF_LP, 9000.0f, c);
      double hp_lo = tone_db(m, MF_HP, 150.0f, c), hp_hi = tone_db(m, MF_HP, 9000.0f, c);
      double bp_lo = tone_db(m, MF_BP, 100.0f, c), bp_mid = tone_db(m, MF_BP, 1300.0f, c), bp_hi = tone_db(m, MF_BP, 12000.0f, c);
      printf("filter %s: LP %.0f / %.0f dB, HP %.0f / %.0f, BP %.0f %.0f %.0f (150 Hz / 9 kHz; 100, 1.3k, 12k)\n",
             mf_model_name(m), lp_lo, lp_hi, hp_lo, hp_hi, bp_lo, bp_mid, bp_hi);
      // (the ladder's passband: the resonance takes some, as a Moog's; MS-20's high pass: 6 dB, one pole and its loop)
      if (lp_lo < -6.0 || lp_hi > lp_lo - 20.0 || hp_hi < -3.0 || hp_lo > hp_hi - (m == MF_MS20 ? 12.0 : 20.0) ||
          bp_mid < bp_lo + 10.0 ||
          bp_mid < bp_hi + 10.0)
        ok = 0;
    }
    double vox_e = tone_db(MF_VOX, MF_MID, 700.0f, 0.0f), vox_i = tone_db(MF_VOX, MF_MID, 700.0f, 0.5f);
    printf("filter VOX: 700 Hz through A %.0f dB, through I %.0f dB (its first formant there, then not)\n", vox_e, vox_i);
    if (vox_e < vox_i + 6.0) ok = 0;
    for (int m = MF_LADDER; m < MF_MODELS; ++m) {        // pinged in silence
      mf_init();
      float k[MF_KNOBS] = {0.6f, 0.8f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f}, buf[128], pk = 0.0f;
      mf_set(0, m, 0, k, 60.0f);
      mf_trigger(0, 10);
      for (int b = 0; b < 20; ++b) {
        mf_set(0, m, 0, k, 60.0f);
        for (int i = 0; i < 128; ++i) buf[i] = 0.0f;
        mf_process(0, buf, 128);
        for (int i = 0; i < 128; ++i) pk = std::max(pk, std::fabs(buf[i]));
      }
      printf("filter %s pinged: peak %.2f\n", mf_model_name(m), pk);
      if (pk < 0.05f) ok = 0;
    }
    uint32_t rs = 12345u;
    auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return static_cast<float>(rs >> 8) / 16777216.0f; };
    int nan = 0;
    float worst = 0.0f;
    for (int trial = 0; trial < 240; ++trial) {          // driven anyhow
      int m = MF_LADDER + trial % 4, ty = (trial / 4) % 3;
      float k[MF_KNOBS] = {rnd(), rnd(), rnd() * 2.0f - 1.0f, rnd(), rnd(), rnd(), rnd() * 2.0f - 1.0f, rnd(), 0.0f,
                           rnd(), rnd() * 2.0f - 1.0f, rnd()}, buf[128];   // (its noise, colour, attack too)
      mf_init();
      for (int b = 0; b < 120; ++b) {
        mf_set(0, m, ty, k, 24.0f + rnd() * 72.0f);
        if (b % 15 == 0) mf_trigger(0, static_cast<uint32_t>(rnd() * 127.0f));
        for (int i = 0; i < 128; ++i) buf[i] = (rnd() * 2.0f - 1.0f) * (b % 40 < 20 ? 1.0f : 3.0f);
        mf_process(0, buf, 128);
        for (int i = 0; i < 128; ++i) {
          if (!std::isfinite(buf[i])) ++nan;
          worst = std::max(worst, std::fabs(buf[i]));
        }
      }
    }
    printf("filters driven anyhow (240 settings, FM, pings, loud noise): %d NaN, peak %.2f\n", nan, worst);
    if (nan || worst > 2.001f) ok = 0;
    {                                                    // the CPU guard: the FM's coefficients every 2nd, 4th sample,
      float lv[4][4];                                    // then the FM off: finite, near the level (a self-FM is
      int gnan = 0;                                      // chaotic: not the same samples)
      for (int m = MF_LADDER; m < MF_MODELS; ++m)
        for (int g = 0; g < 4; ++g) {
          mf_init();
          eng_guard_test(0, g);
          float k[MF_KNOBS] = {0.5f, 0.5f, 0.0f, 0.5f, 0.3f, 0.0f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
          float ph = 0.0f;
          double e = 0.0;
          for (int b = 0; b < 344; ++b) {
            float buf[128];
            mf_set(0, m, MF_LP, k, 60.0f);
            for (int i = 0; i < 128; ++i) {   // a saw at 110 Hz
              ph += 110.0f / 44100.0f;
              ph -= ph >= 1.0f ? 1.0f : 0.0f;
              buf[i] = 0.5f * (2.0f * ph - 1.0f);
            }
            mf_process(0, buf, 128);
            for (int i = 0; i < 128; ++i) {
              if (!std::isfinite(buf[i])) gnan = 1;
              if (b >= 40) e += buf[i] * buf[i];
            }
          }
          lv[m - 1][g] = static_cast<float>(10.0 * std::log10(e / (304.0 * 128.0) + 1e-12));
        }
      eng_guard_test(0, 0);
      float lvd = 0.0f;
      for (int m = 0; m < 4; ++m)
        for (int g = 1; g < 4; ++g) lvd = std::max(lvd, std::fabs(lv[m][g] - lv[m][0]));
      printf("the CPU guard on the filters' FM (strong, on a saw): its levels %.1f dB from the FM's at most%s\n", lvd,
             gnan ? ", NaN" : "");
      if (gnan || lvd > 6.0f) ok = 0;
    }
    {                                                    // the envelope opens it at a trig
      mf_init();
      float k[MF_KNOBS] = {0.3f, 0.2f, 1.0f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f}, buf[128] = {0};
      mf_set(0, MF_SEM, MF_LP, k, 60.0f);
      mf_process(0, buf, 128);
      float before = mf_cutoff_hz(0);
      mf_set(0, MF_SEM, MF_LP, k, 60.0f);
      mf_trigger(0, 0);
      mf_process(0, buf, 128);
      float after = mf_cutoff_hz(0);
      printf("filter envelope: cutoff %.0f Hz, at a trig %.0f Hz\n", before, after);
      if (after < before * 8.0f) ok = 0;
    }
    {                                                    // STOP: a self-oscillation, a ring, the noise, silent at
      float k[MF_KNOBS] = {0.6f, 1.0f, 0.5f, 0.2f, 1.0f, 1.0f, 0.3f, 0.0f, 0.0f, 0.6f, 0.0f, 0.0f}, buf[128];   // once;
                                                                                    // the noise is its envelope's
      float before = 0.0f, after = 0.0f, woke = 0.0f, quiet_noise = 0.0f;
      for (int m = MF_LADDER; m < MF_MODELS; ++m) {
        mf_init();
        mf_set(0, m, MF_LP, k, 60.0f);
        mf_trigger(0, 0);
        for (int b = 0; b < 400; ++b) {                  // pinged, then ringing (LADDER, MS-20: their own song)
          mf_set(0, m, MF_LP, k, 60.0f);
          for (int i = 0; i < 128; ++i) buf[i] = 0.0f;
          mf_process(0, buf, 128);
          if (b > 380)
            for (int i = 0; i < 128; ++i) quiet_noise = std::max(quiet_noise, m >= MF_SEM ? std::fabs(buf[i]) : 0.0f);
          if (b == 10)
            for (int i = 0; i < 128; ++i) before = std::max(before, std::fabs(buf[i]));
        }
        mf_stop(0);
        for (int b = 0; b < 40; ++b) {
          mf_set(0, m, MF_LP, k, 60.0f);
          for (int i = 0; i < 128; ++i) buf[i] = 0.0f;
          mf_process(0, buf, 128);
          if (b >= 9)
            for (int i = 0; i < 128; ++i) after = std::max(after, std::fabs(buf[i]));
        }
        mf_trigger(0, 0);                                // a trig wakes it
        mf_set(0, m, MF_LP, k, 60.0f);
        for (int i = 0; i < 128; ++i) buf[i] = 0.0f;
        mf_process(0, buf, 128);
        for (int i = 0; i < 128; ++i) woke = std::max(woke, std::fabs(buf[i]));
      }
      printf("filters at STOP: %.3f ringing, %.5f 26 ms after, %.3f at the next trig; SEM's and VOX's noise long after a "
             "trig %.5f (DECAY 50 ms: 1.1 s after)\n", before, after, woke, quiet_noise);
      if (after > 0.0f || woke < 0.01f || before < 0.05f || quiet_noise > 0.001f) ok = 0;
    }
    {                                                    // OFF: the sound as it is
      mf_init();
      float k[MF_KNOBS] = {0.1f, 1.0f, 1.0f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f}, buf[128], ref[128];
      for (int i = 0; i < 128; ++i) buf[i] = ref[i] = rnd() - 0.5f;
      mf_set(0, MF_OFF, MF_LP, k, 60.0f);
      mf_trigger(0, 3);
      mf_process(0, buf, 128);
      if (memcmp(buf, ref, sizeof buf)) ok = 0, printf("FAIL: the filter OFF changed the sound\n");
    }
    printf("filters' time here, a block:");
    for (int m = MF_LADDER; m < MF_MODELS; ++m)
      for (int fm = 0; fm < 2; ++fm) {
        mf_init();
        float k[MF_KNOBS] = {0.6f, 0.5f, 0.5f, 0.5f, 0.3f, 0.0f, fm ? 0.5f : 0.0f, 0.0f}, buf[128];
        clock_t c0 = clock();
        for (int b = 0; b < 20000; ++b) {
          mf_set(0, m, MF_LP, k, 60.0f);
          for (int i = 0; i < 128; ++i) buf[i] = rnd() - 0.5f;
          mf_process(0, buf, 128);
        }
        printf(" %s%s %.1f us", mf_model_name(m), fm ? "+FM" : "",
               static_cast<double>(clock() - c0) / CLOCKS_PER_SEC * 1e6 / 20000.0);
      }
    printf("\n");
    if (!ok) ++failures, printf("FAIL: the filters\n");
    mf_init();
    eng_cut();
  }

  // 41. the demo groove (a first power-on's, the browser emulator's): its four tracks playing, its filters on two of
  // them (the bass's cutoff moved by its LFO), no NaN, its peak under full scale
  {
    eng_play(0);
    eng_groove(p);
    eng_fresh();
    eng_cut();
    for (int b = 0; b < 4; ++b) eng_render(l, r, n);
    eng_play(1);
    uint32_t r0[ENG_TRACKS];
    for (int t = 0; t < ENG_TRACKS; ++t) r0[t] = eng_rises(t);
    float pk = 0.0f, lo = 1e9f, hi = 0.0f;
    int nan = 0;
    for (uint32_t b = 0; b < static_cast<uint32_t>(8.0f * 44100.0f / n); ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        if (!std::isfinite(l[i]) || !std::isfinite(r[i])) nan = 1;
        pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
      }
      if (b % 64 == 0 && !(eng_gates() & 2u)) lo = std::min(lo, eng_filter_hz(1)), hi = std::max(hi, eng_filter_hz(1));
    }
    printf("the demo groove: %u %u %u %u trigs in 8 s, peak %.2f, the bass's cutoff between notes %.0f..%.0f Hz\n",
           eng_rises(0) - r0[0], eng_rises(1) - r0[1], eng_rises(2) - r0[2], eng_rises(3) - r0[3], pk, lo, hi);
    int ok = !nan && pk < 1.0f && pk > 0.2f && hi > lo * 1.3f;
    for (int t = 0; t < ENG_TRACKS; ++t) ok = ok && eng_rises(t) - r0[t] >= 8;
    if (!ok) ++failures, printf("FAIL: the demo groove\n");
    eng_play(0);
    eng_cut();
  }

  // 37. the USB audio input's 10 channels (eng_usb_frames): silence until the host has recorded a block, then the
  // four tracks and the effects' return, each sounding, their sum the master before its bus (to the 16-bit steps,
  // where none of them is clipped); on the demo patch and on the stress test (Warps in a track's place, OTT)
  {
    extern float eng_test_prebus[2][128];
    const float k = 8300000.0f / 256.0f;
    for (int stress = 0; stress < 2; ++stress) {
      eng_play(0);
      if (stress) eng_stress(p);
      else eng_defaults(p);
      eng_fresh();
      eng_cut();
      for (int b = 0; b < 8; ++b) eng_render(l, r, n);   // (no request: none made)
      eng_play(1);
      eng_render(l, r, n);
      const int16_t* u = eng_usb_frames(n, 1);
      int first_silent = 1;
      for (uint32_t i = 0; i < n * ENG_USB_CH; ++i)
        if (u[i]) first_silent = 0;
      float pk[ENG_USB_CH / 2] = {0, 0, 0, 0, 0}, err = 0.0f;
      long checked = 0;
      for (int b = 0; b < 44100 * 4 / static_cast<int>(n); ++b) {
        eng_render(l, r, n);
        u = eng_usb_frames(n, 1);
        if (b < 2) continue;                              // (the request reaches the next block's mix)
        for (uint32_t i = 0; i < n; ++i) {
          const int16_t* f = u + i * ENG_USB_CH;
          float s[2] = {0.0f, 0.0f};
          int clip = 0;
          for (int c = 0; c < ENG_USB_CH; ++c) {
            s[c & 1] += static_cast<float>(f[c]) / k;
            if (f[c] == 32767 || f[c] == -32768) clip = 1;
            pk[c / 2] = std::max(pk[c / 2], std::fabs(static_cast<float>(f[c]) / k));
          }
          if (clip) continue;
          for (int c = 0; c < 2; ++c) err = std::max(err, std::fabs(s[c] - eng_test_prebus[c][i]));
          ++checked;
        }
      }
      int ok = first_silent && err < 6.0f / k && checked > 44100 * 3;
      for (int c = 0; c < ENG_USB_CH / 2; ++c)
        if (pk[c] < 0.01f) ok = 0;
      printf("USB, 10 channels (%s): first block %s, peaks %.2f %.2f %.2f %.2f, effects %.2f; the sum against the "
             "master before its bus: %.1f steps at most (%ld frames)\n", stress ? "stress test" : "demo",
             first_silent ? "silent" : "NOT SILENT", pk[0], pk[1], pk[2], pk[3], pk[4], err * k, checked);
      if (!ok) ++failures, printf("FAIL: the USB channels\n");
    }
    eng_play(0);
  }

  // 42. ZERO (mi_zero.cc, 0.8.1): on a track, its name, its knobs (named, its defaults, written as it writes them), its
  // sound through the engine; the voice itself: INDEX 0 a pure sine on the note (its harmonics under -90 dB); deep
  // through-zero FM on a ratio of 1 still a tone on the note (each period the same, its first harmonic the strongest
  // band); a saw modulator high up, its alias under the half-band; BEAT a round pulse of the level at its rate; VARY to
  // the left the same sixteen sounds again, sample for sample (not the same each trig), to the right others; any
  // setting finite and bounded
  {
    eng_play(0);
    eng_defaults(p);
    eng_fresh();
    eng_set_model(0, eng_model_find(MK_ZERO, 0));
    float kd[8];
    zero_defaults(kd);
    int setup_ok = std::strcmp(eng_algo_name(PG_TRACK(0, 0), eng_model_find(MK_ZERO, 0)), "ZERO") == 0;
    for (int i = 0; i < 8; ++i) {
      int param = PG_TRACK(0, 0) * 4 + 1 + i;
      float u = eng_pdef(param)->kind == K_BIP ? (p->val[param] + 1.0f) * 0.5f : p->val[param];
      if (std::fabs(u - kd[i]) > 1e-6f || std::strcmp(eng_param_name(param), zero_knob_name(i)) != 0) setup_ok = 0;
    }
    char tx[24], tx2[24], tx3[24], tx4[24];
    eng_param_text(PG_TRACK(0, 0) * 4 + 2, p->val[PG_TRACK(0, 0) * 4 + 2], tx, sizeof tx);   // RATIO x1
    float st = eng_lx_step(PG_TRACK(0, 0) * 4 + 2, p->val[PG_TRACK(0, 0) * 4 + 2], 3);         // three detents on: x3
    eng_param_text(PG_TRACK(0, 0) * 4 + 2, st, tx2, sizeof tx2);
    eng_param_text(PG_TRACK(0, 0) * 4 + 1, 1.0f, tx3, sizeof tx3);                             // INDEX at the end
    eng_param_text(PG_TRACK(0, 1) * 4 + 1, p->val[PG_TRACK(0, 1) * 4 + 1], tx4, sizeof tx4);   // BEAT .72Hz
    setup_ok &= std::strcmp(tx, "x1") == 0 && std::strcmp(tx2, "x3") == 0 && std::strcmp(tx3, "5.00") == 0 &&
                std::strcmp(tx4, ".72Hz") == 0;
    eng_cut();
    for (int b = 0; b < 3; ++b) eng_render(l, r, n);
    float pk_eng = 0.0f;
    int nan = 0;
    for (int b = 0; b < 400; ++b) {
      if (b % 50 == 0) eng_key(0, 36, 1);
      if (b % 50 == 20) eng_key(0, 36, 0);
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        if (!std::isfinite(l[i])) nan = 1;
        pk_eng = std::max(pk_eng, std::fabs(l[i]));
      }
    }
    eng_key(0, 36, 0);
    setup_ok &= !nan && pk_eng > 0.05f;
    eng_defaults(p);
    eng_cut();

    static float buf[44100];
    auto level = [&](double hz) {        // a Hann-windowed Goertzel: the level at hz, against a full sine
      double w = 2.0 * M_PI * hz / 44100.0, c = 2.0 * std::cos(w), s1 = 0.0, s2 = 0.0;
      for (uint32_t i = 0; i < 44100; ++i) {
        double s0 = buf[i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * i / 44100.0)) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
      }
      return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (44100.0 * 0.25);
    };
    auto held = [&](float note, const float* k, int skip_s) {   // a held note: its second second (or later)
      zero_init();
      zero_set(0, note, k);
      zero_gate(0, 1);
      for (int s = 0; s < skip_s; ++s)
        for (uint32_t i = 0; i < 44100; i += 49) zero_render(0, &buf[i], i + 49 > 44100 ? 44100 - i : 49);
      for (uint32_t i = 0; i < 44100; i += 49) zero_render(0, &buf[i], i + 49 > 44100 ? 44100 - i : 49);
    };
    // INDEX 0, BEAT off, VARY in the middle, held: a pure sine on C3
    float k0[8] = {0.0f, 2.5f / 11.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f};
    held(48.0f, k0, 1);
    int zc = 0;
    for (uint32_t i = 1; i < 44100; ++i) zc += buf[i - 1] < 0.0f && buf[i] >= 0.0f;
    double f0 = 130.8128, a1 = level(f0), hmax = 0.0;
    for (int h = 2; h <= 9; ++h) hmax = std::max(hmax, level(f0 * h));
    double thd = 20.0 * std::log10(hmax / a1 + 1e-12);
    int pure_ok = zc >= 130 && zc <= 132 && a1 > 0.5 && thd < -90.0;
    // through zero: INDEX 3, a sine modulator at x1, on 110.25 Hz (400 samples a period): each period the same; its
    // first harmonic the strongest of its bands, nothing between them
    float kz[8] = {std::sqrt(3.0f / 5.0f), 2.5f / 11.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f};
    float nz = 69.0f + 12.0f * std::log2(110.25f / 440.0f);
    held(nz, kz, 1);
    float per = 0.0f, pk_z = 0.0f;
    for (uint32_t i = 0; i + 400 < 44100; ++i) per = std::max(per, std::fabs(buf[i + 400] - buf[i]));
    for (uint32_t i = 0; i < 44100; ++i) pk_z = std::max(pk_z, std::fabs(buf[i]));
    double hz_band[6], between = 0.0;
    for (int h = 1; h <= 6; ++h) hz_band[h - 1] = level(110.25 * h), between = std::max(between, level(110.25 * h + 55.0));
    double strongest = 0.0;
    for (int h = 0; h < 6; ++h) strongest = std::max(strongest, hz_band[h]);
    int tz_ok = per < 0.005f * pk_z && 20.0 * std::log10(between / strongest + 1e-12) < -60.0;
    // the alias: a saw modulator at x3, INDEX 3, on C6 (1046.5 Hz): the level between the harmonics (aliases land there)
    float ka[8] = {std::sqrt(3.0f / 5.0f), 5.5f / 11.0f, 1.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f};
    held(84.0f, ka, 1);
    double tot = 0.0, alias = 0.0;
    for (int h = 1; h <= 19; ++h) tot += std::pow(level(1046.502 * h), 2.0);
    for (double hz = 100.0; hz < 20000.0; hz += 23.0) {   // (between the harmonics, 40 Hz off them)
      double off = std::fmod(hz, 1046.502);
      if (off > 40.0 && off < 1006.5) alias = std::max(alias, level(hz));
    }
    double alias_db = 20.0 * std::log10(alias / std::sqrt(tot) + 1e-12);
    // BEAT 0.5 (2 Hz), PULSE to the level, INDEX 0: the level pulses twice a second, roundly (no corner)
    float kb[8] = {0.0f, 2.5f / 11.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f};
    held(48.0f, kb, 1);
    float env_lo = 9.0f, env_hi = 0.0f, envs[100];
    for (uint32_t i = 0; i < 100; ++i) {   // the level every 10 ms
      float e = 0.0f;
      for (uint32_t j = 0; j < 441; ++j) e = std::max(e, std::fabs(buf[i * 441 + j]));
      envs[i] = e;
      env_lo = std::min(env_lo, e), env_hi = std::max(env_hi, e);
    }
    int peaks = 0;                       // (its rises through the middle: one a pulse)
    for (int i = 1; i < 100; ++i) peaks += envs[i - 1] < 0.5f * (env_lo + env_hi) && envs[i] >= 0.5f * (env_lo + env_hi);
    int beat_ok = peaks >= 2 && peaks <= 3 && env_hi > env_lo * 2.5f;
    // VARY: 32 short trigs, each after silence; to the left the 16 after the 16 first the same
    auto trigs = [&](float vary, float* sig, float* rms) {
      float kk[8] = {0.6f, 2.5f / 11.0f, 0.6f, 0.15f, 0.4f, 0.8f, vary, 0.6f};
      static float tb[32][8192];
      zero_init();
      for (int tr = 0; tr < 32; ++tr) {
        zero_set(0, 43.0f, kk);
        zero_gate(0, 1);
        for (uint32_t i = 0; i < 8192; i += 32) {
          if (i == 2048) zero_gate(0, 0);
          zero_render(0, &tb[tr][i], 32);
        }
        float tmp[32];
        for (int b = 0; b < 6000; ++b) zero_render(0, tmp, 32);   // (4.4 s: silent again)
        double e = 0.0;
        for (int i = 0; i < 8192; ++i) e += static_cast<double>(tb[tr][i]) * tb[tr][i];
        rms[tr] = static_cast<float>(std::sqrt(e / 8192.0));
      }
      float diff = 0.0f;
      for (int tr = 16; tr < 32; ++tr)
        for (int i = 0; i < 8192; ++i) diff = std::max(diff, std::fabs(tb[tr][i] - tb[tr - 16][i]));
      *sig = diff;
    };
    float d_locked, d_free, rms_l[32], rms_f[32];
    trigs(0.0f, &d_locked, rms_l);       // (VARY all to the left: a loop)
    trigs(1.0f, &d_free, rms_f);         // (all to the right: new each trig)
    float lo = 1e9f, hi = 0.0f;
    for (int tr = 0; tr < 16; ++tr) lo = std::min(lo, rms_l[tr]), hi = std::max(hi, rms_l[tr]);
    int loop_ok = d_locked == 0.0f && d_free > 0.05f && hi > lo * 1.1f;
    // anything: 300 settings drawn, a trig each, its half second
    uint32_t sd = 12345u;
    auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return static_cast<float>(sd >> 8) / 16777216.0f; };
    float pk_any = 0.0f;
    int nan_any = 0;
    zero_init();
    for (int s = 0; s < 300; ++s) {
      float kk[8];
      for (int i = 0; i < 8; ++i) kk[i] = rnd() < 0.15f ? (rnd() < 0.5f ? 0.0f : 1.0f) : rnd();
      zero_set(s & 3, 12.0f + 100.0f * rnd(), kk);
      zero_gate(s & 3, 1);
      for (int b = 0; b < 690; ++b) {
        if (b == 300) zero_gate(s & 3, 0);
        float tmp[32];
        zero_render(s & 3, tmp, 32);
        for (int i = 0; i < 32; ++i) {
          if (!std::isfinite(tmp[i])) nan_any = 1;
          pk_any = std::max(pk_any, std::fabs(tmp[i]));
        }
      }
    }
    zero_init();
    printf("ZERO: %s (%s, %s, INDEX %s, BEAT %s), through the engine peak %.2f; INDEX 0: %d cycles a second (C3), its "
           "harmonics %.0f dB; through zero (INDEX 3): a period %.4f apart, between its bands %.0f dB; a saw at x3 on C6: "
           "alias %.0f dB; BEAT 2 Hz: %d pulses a second, %.1f dB deep; VARY to the left: %.6f apart (each trig's level "
           "%.2f..%.2f), to the right: %.2f apart; 300 settings: peak %.2f%s\n",
           setup_ok ? "ok" : "WRONG", tx, tx2, tx3, tx4, pk_eng, zc, thd, per, 20.0 * std::log10(between / strongest + 1e-12),
           alias_db, peaks, 20.0 * std::log10(env_hi / env_lo), d_locked, lo, hi, d_free, pk_any, nan_any ? ", NaN" : "");
    if (!setup_ok || !pure_ok || !tz_ok || alias_db > -60.0 || !beat_ok || !loop_ok || nan_any || pk_any > 1.2f)
      ++failures, printf("FAIL: ZERO\n");
  }

  // 43. the dose (0.8.3) moves what is on, never turns on what costs once on: T1's filter (SEM) with FM 0, DRIVE 0,
  // NOISE 0, its bits and glitch off: at 100 %, 64 trigs, they stay at 0; T2's with FM, DRIVE and NOISE up: moved
  {
    eng_play(0);
    eng_blank(p);
    eng_fresh();
    for (int t = 0; t < 2; ++t) {
      p->algo[PG_FILT(t, 0)] = MF_SEM;
      for (int s2 = 0; s2 < 16; ++s2) eng_trig_set(t, s2, 1);
    }
    int pf = eng_track_param(1, TP_FM), pd = eng_track_param(1, TP_FDRIVE), pn = eng_track_param(1, TP_NOISE);
    p->val[pf] = 0.3f, p->val[pd] = 0.4f, p->val[pn] = 0.3f;
    p->dice = 100;
    p->dice_seed = 3;
    static const int kOff[8] = {TP_FDRIVE, TP_FM, TP_NOISE, TP_CLOCK, TP_CRUSH, TP_MASK, TP_BSHAPE, TP_GLITCH};
    float on_t1 = 0.0f, moved_t2 = 0.0f;
    eng_play(1);
    for (int b = 0; b < 344 * 8; ++b) {
      eng_render(l, r, n);
      for (int i = 0; i < 8; ++i) on_t1 = std::max(on_t1, std::fabs(eng_now(eng_track_param(0, kOff[i]))));
      moved_t2 = std::max(moved_t2, std::fabs(eng_now(pf) - 0.3f) + std::fabs(eng_now(pd) - 0.4f));
    }
    eng_play(0);
    p->dice = 0;
    printf("the dose at 100 %%: what is off stays off (%.3f at most), what is on moves (%.2f)\n", on_t1, moved_t2);
    if (on_t1 > 0.0f || moved_t2 < 0.05f) ++failures, printf("FAIL: the dose's costly parts\n");
    eng_defaults(p);
    eng_fresh();
  }

  // 44. the dose leaves Warps' own oscillator's note (LEVEL 1, its carrier the oscillator) where it is (0.8.6), and
  // still moves LEVEL 1 when the carrier is a track (there a level)
  {
    eng_play(0);
    eng_blank(p);
    eng_fresh();
    const int car = PG_WARPS(0) * 4 + 0, lv1 = PG_WARPS(1) * 4 + 1;
    float dev[2];
    for (int c = 0; c < 2; ++c) {
      p->val[car] = static_cast<float>(c ? WC_T1 : WC_OSC1);
      p->val[lv1] = 0.5f;
      p->dice = 100;
      p->dice_seed = 5;
      dev[c] = 0.0f;
      eng_play(1);
      for (int b = 0; b < 344 * 4; ++b) {
        eng_render(l, r, n);
        dev[c] = std::max(dev[c], std::fabs(eng_final(lv1) - 0.5f));
      }
      eng_play(0);
      p->dice = 0;
    }
    printf("the dose and Warps' LEVEL 1: its oscillator's note %.3f off at most, a track's level %.2f\n", dev[0], dev[1]);
    if (dev[0] > 0.0f || dev[1] < 0.05f) ++failures, printf("FAIL: the dose on Warps' oscillator\n");
    eng_defaults(p);
    eng_fresh();
  }

  // 45. the drums (mi_drum.cc, 0.9): on a track, their kit (knobs named, written as they are), each sounding; the
  // engine itself: KICK's pitch, at C3 its kit's 50 Hz once its sweep has gone; CLAP's bursts, three before its tail;
  // a retrig over a ringing TOM without a step; any setting finite and bounded
  {
    eng_play(0);
    eng_defaults(p);
    eng_fresh();
    int kit_ok = 1, sound_ok = 1;
    float pk[DR_MODELS];
    for (int m = 0; m < DR_MODELS; ++m) {
      eng_set_model(0, eng_model_find(MK_DRUM, m));
      float kd[8];
      drum_defaults(m, kd);
      for (int i = 0; i < 8; ++i) {
        int param = PG_TRACK(0, 0) * 4 + 1 + i;
        float u = eng_pdef(param)->kind == K_BIP ? (p->val[param] + 1.0f) * 0.5f : p->val[param];
        if (std::fabs(u - kd[i]) > 1e-6f || std::strcmp(eng_param_name(param), drum_knob_name(i)) != 0) kit_ok = 0;
      }
      if (std::strcmp(eng_algo_name(PG_TRACK(0, 0), eng_model_find(MK_DRUM, m)), drum_model_name(m)) != 0) kit_ok = 0;
      eng_cut();
      for (int b = 0; b < 3; ++b) eng_render(l, r, n);
      pk[m] = 0.0f;
      for (int b = 0; b < 200; ++b) {
        if (b % 50 == 0) eng_key(0, 48, 1);
        if (b % 50 == 10) eng_key(0, 48, 0);
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i])) sound_ok = 0;
          pk[m] = std::max(pk[m], std::fabs(l[i]));
        }
      }
      if (pk[m] < 0.02f) sound_ok = 0;
    }
    char tx[24];
    eng_set_model(0, eng_model_find(MK_DRUM, 0));
    eng_param_text(PG_TRACK(0, 0) * 4 + 1, p->val[PG_TRACK(0, 0) * 4 + 1], tx, sizeof tx);   // KICK's WAVE: Sine
    kit_ok &= std::strcmp(tx, "Sine") == 0;
    eng_defaults(p);
    eng_cut();
    // the engine: KICK at C3, held a second, its zero crossings from 0.4 s on (its sweep long gone)
    static float buf[44128];
    float k[8];
    drum_init();
    drum_defaults(0, k);
    k[2] = 1.0f;                                         // (a long decay)
    drum_set(0, 0, 48.0f, k);
    drum_gate(0, 1);
    for (uint32_t i = 0; i < 44100; i += 32) drum_render(0, &buf[i], 32);
    int zc = 0;
    for (uint32_t i = 17640 + 1; i < 44100; ++i) zc += buf[i - 1] < 0.0f && buf[i] >= 0.0f;
    float hz = static_cast<float>(zc) / 0.6f;
    // CLAP: its noise's bursts (the level a millisecond, rising past half the hit's peak from under a fifth): three
    // bursts, then the tail
    drum_init();
    drum_defaults(2, k);
    k[5] = 1.0f;                                         // (the noise alone)
    drum_set(0, 2, 48.0f, k);
    drum_gate(0, 1);
    for (uint32_t i = 0; i < 4416; i += 32) drum_render(0, &buf[i], 32);
    float env[100], top = 0.0f;
    for (int w = 0; w < 100; ++w) {
      double e = 0.0;
      for (int j = 0; j < 44; ++j) e += buf[w * 44 + j] * buf[w * 44 + j];
      env[w] = static_cast<float>(std::sqrt(e / 44.0));
      top = std::max(top, env[w]);
    }
    int rises = 0, low = 1;
    for (int w = 0; w < 100; ++w) {
      if (low && env[w] > 0.5f * top) ++rises, low = 0;
      else if (env[w] < 0.2f * top) low = 1;
    }
    // TOM ringing, retriggered: its largest step around the retrig, against a fresh hit's first ones
    drum_init();
    drum_defaults(4, k);
    drum_set(0, 4, 48.0f, k);
    drum_gate(0, 1);
    for (uint32_t i = 0; i < 4416; i += 32) drum_render(0, &buf[i], 32);
    float typ = 0.0f, step = 0.0f;
    for (uint32_t i = 1; i < 60; ++i) typ = std::max(typ, std::fabs(buf[i] - buf[i - 1]));
    drum_gate(0, 0);
    drum_gate(0, 1);
    for (uint32_t i = 4416; i < 8832; i += 32) drum_render(0, &buf[i], 32);
    for (uint32_t i = 4380; i < 4480; ++i) step = std::max(step, std::fabs(buf[i] - buf[i - 1]));
    // anything: 300 settings, a trig each
    uint32_t sd = 777u;
    auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return static_cast<float>(sd >> 8) / 16777216.0f; };
    float pk_any = 0.0f;
    int nan_any = 0;
    drum_init();
    for (int s2 = 0; s2 < 300; ++s2) {
      for (int i = 0; i < 8; ++i) k[i] = rnd() < 0.15f ? (rnd() < 0.5f ? 0.0f : 1.0f) : rnd();
      drum_set(s2 & 3, s2 % DR_MODELS, 12.0f + 100.0f * rnd(), k);
      drum_gate(s2 & 3, 1);
      for (int b = 0; b < 300; ++b) {
        float tmp[32];
        if (b == 20) drum_gate(s2 & 3, 0);
        drum_render(s2 & 3, tmp, 32);
        for (int i = 0; i < 32; ++i) {
          if (!std::isfinite(tmp[i])) nan_any = 1;
          pk_any = std::max(pk_any, std::fabs(tmp[i]));
        }
      }
    }
    drum_init();
    printf("the drums: kits %s, sounding %s (peaks %.2f %.2f %.2f %.2f %.2f %.2f); KICK at C3 %.1f Hz; CLAP's rises %d; "
           "a retrig over a TOM: a step of %.3f (a fresh hit's %.3f); 300 settings: peak %.2f%s\n", kit_ok ? "ok" : "WRONG",
           sound_ok ? "ok" : "NO", pk[0], pk[1], pk[2], pk[3], pk[4], pk[5], hz, rises, step, typ, pk_any,
           nan_any ? ", NaN" : "");
    if (!kit_ok || !sound_ok || std::fabs(hz - 50.0f) > 1.5f || rises < 3 || rises > 5 || step > 1.5f * typ + 0.02f ||
        nan_any || pk_any > 1.5f)
      ++failures, printf("FAIL: the drums\n");
  }

  // 46. the FM (mi_fm4.cc, 0.10.1): on a track, its knobs (named, written as they are), each algorithm sounding; the
  // engine: the eighth algorithm's first operator alone (INDEX 0) at the note, a sine; FEEDBACK's saw and square (their
  // harmonics), clean (nothing between the harmonics above -55 dB) up to C6; SYNC and PW clean; a chord on RATIO; a
  // held note held, let go; a decaying one over; a retrig without a step; any setting finite and bounded. Projects of
  // 0.9 and 0.10.0 (versions 16, 17): their models renumbered, a model gone the FM on its nearest algorithm
  {
    eng_play(0);
    eng_defaults(p);
    eng_fresh();
    int kit_ok = 1, sound_ok = 1;
    float pk[8];
    eng_set_model(0, eng_model_find(MK_FM, 0));
    {
      float kd[8];
      fm4_defaults(kd);
      for (int i = 0; i < 8; ++i) {
        int param = PG_TRACK(0, 0) * 4 + 1 + i;
        float u = eng_pdef(param)->kind == K_BIP ? (p->val[param] + 1.0f) * 0.5f : p->val[param];
        if (std::fabs(u - kd[i]) > 1e-6f || std::strcmp(eng_param_name(param), fm4_knob_name(i)) != 0) kit_ok = 0;
      }
      if (std::strcmp(eng_algo_name(PG_TRACK(0, 0), eng_model_find(MK_FM, 0)), "FM") != 0) kit_ok = 0;
    }
    for (int m = 0; m < 8; ++m) {
      p->val[PG_TRACK(0, 0) * 4 + 1] = (static_cast<float>(m) + 0.5f) / 8.0f;   // ALGO
      if (fm4_algo((static_cast<float>(m) + 0.5f) / 8.0f) != m) kit_ok = 0;
      eng_cut();
      for (int b = 0; b < 3; ++b) eng_render(l, r, n);
      if (eng_fm_algo(0) != m) kit_ok = 0;
      pk[m] = 0.0f;
      for (int b = 0; b < 400; ++b) {
        if (b % 100 == 0) eng_key(0, 48, 1);
        if (b % 100 == 60) eng_key(0, 48, 0);
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i])) sound_ok = 0;
          pk[m] = std::max(pk[m], std::fabs(l[i]));
        }
      }
      if (pk[m] < 0.02f) sound_ok = 0;
    }
    char tx[24];
    struct { int k; float u; const char* want; } kTx[] = {
        {0, 2.5f / 8.0f, "3"}, {1, 0.5f / 30.0f, "1:1:1:1"}, {1, 11.5f / 30.0f, "1:1:.5:1"}, {1, 16.5f / 30.0f, "Tine"},
        {1, 22.5f / 30.0f, "Major"}, {1, 27.5f / 30.0f, "Min7"}, {3, 1.0f, "Hold"}, {6, 0.875f, "Saw"},
        {6, 0.125f, "Square"}, {6, 0.5f, "Sine"}, {7, 0.5f, "Off"}, {7, 0.75f, "50% SYNC"}, {7, 0.25f, "50% PW"},
        {4, 0.0f, "Off"}, {4, 0.5f, "50%"}, {5, 7.5f / 15.0f, "Flat"}, {5, 9.5f / 15.0f, "Pluck"},
        {5, 5.5f / 15.0f, "Swell"}, {5, 0.0f, "Breath"}, {5, 1.0f, "Loop"}, {5, 13.5f / 15.0f, "Rebound"}};
    for (const auto& x : kTx) {
      fm4_knob_text(x.k, x.u, tx, sizeof tx);
      if (std::strcmp(tx, x.want)) kit_ok = 0, printf("FM: knob %d at %.3f written %s, %s expected\n", x.k, x.u, tx, x.want);
    }
    kit_ok &= fm4_chord(22.5f / 30.0f) && !fm4_chord(21.5f / 30.0f);
    eng_defaults(p);
    eng_cut();
    // the engine itself: a note held, its samples
    static float buf[88200];
    float k[8];
    auto play = [&](int m, float note, const float* kk, uint32_t frames, uint32_t gate_off) {
      (void)m;
      fm4_init();
      fm4_set(0, note, kk);
      fm4_gate(0, 1);
      for (uint32_t i = 0; i < frames; i += 32) {
        if (i == gate_off) fm4_gate(0, 0);
        fm4_render(0, &buf[i], 32);
      }
    };
    auto level = [&](uint32_t from, uint32_t len, double hz) {   // a Hann-windowed DFT bin's level
      double re = 0.0, im = 0.0, w = 2.0 * 3.14159265358979 * hz / 44100.0;
      for (uint32_t i = 0; i < len; ++i) {
        double h = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / len), v = buf[from + i] * h;
        re += v * std::cos(w * i);
        im -= v * std::sin(w * i);
      }
      return std::sqrt(re * re + im * im) / (0.25 * len);
    };
    auto db = [](double a, double ref) { return 20.0 * std::log10(a / ref + 1e-12); };
    auto between = [&](double f0) {      // the loudest between the harmonics (10 Hz apart, under 16 kHz) against f0's
      double ref = level(8192, 32768, f0), worst = 0.0;
      for (double f = 40.0; f < 16000.0; f += 10.0) {
        double h = f / f0, d = std::fabs(h - std::floor(h + 0.5)) * f0;
        if (d < 25.0) continue;
        worst = std::max(worst, level(8192, 32768, f));
      }
      return db(worst, ref);
    };
    auto knobs = [&](int m, float index, float fb, float mut) {   // (algorithm m+1, unison, no detune, held, MOVE flat)
      fm4_defaults(k);
      k[0] = (static_cast<float>(m) + 0.5f) / 8.0f, k[1] = 0.5f / 30.0f, k[2] = index, k[3] = 1.0f, k[4] = 0.0f;
      k[5] = 0.5f, k[6] = fb, k[7] = mut;
    };
    // ORGAN alone: a sine at the note (C3), its zero crossings
    knobs(7, 0.0f, 0.5f, 0.5f);
    play(7, 48.0f, k, 44128, 1u << 30);
    int zc = 0;
    for (uint32_t i = 8821; i < 44100; ++i) zc += buf[i - 1] < 0.0f && buf[i] >= 0.0f;
    float hz = static_cast<float>(zc) / 0.8f;
    double sine2 = db(level(8192, 32768, 261.63), level(8192, 32768, 130.81));
    // FEEDBACK: the saw (C3), the square, clean up to C6
    knobs(7, 0.0f, 0.875f, 0.5f);
    play(7, 48.0f, k, 44128, 1u << 30);
    double f3 = 130.81, saw2 = db(level(8192, 32768, 2 * f3), level(8192, 32768, f3));
    double saw3 = db(level(8192, 32768, 3 * f3), level(8192, 32768, f3));
    knobs(7, 0.0f, 0.125f, 0.5f);
    play(7, 48.0f, k, 44128, 1u << 30);
    double sq2 = db(level(8192, 32768, 2 * f3), level(8192, 32768, f3));
    double sq3 = db(level(8192, 32768, 3 * f3), level(8192, 32768, f3));
    double f6 = 1046.50;
    knobs(7, 0.0f, 0.875f, 0.5f);
    play(7, 84.0f, k, 44128, 1u << 30);
    double saw_alias = between(f6);
    knobs(7, 0.0f, 0.125f, 0.5f);
    play(7, 84.0f, k, 44128, 1u << 30);
    double sq_alias = between(f6);
    // SYNC (+75 %: x2.8) and PW (-100 %) on C5
    double f5 = 523.25;
    knobs(7, 0.0f, 0.5f, 0.875f);
    play(7, 72.0f, k, 44128, 1u << 30);
    double sync_alias = between(f5);
    knobs(7, 0.0f, 0.5f, 0.0f);
    play(7, 72.0f, k, 44128, 1u << 30);
    double pw_alias = between(f5);
    // a major chord (RATIO, the sixth algorithm: three heard; INDEX 0): its root, its third, its fifth; a fourth, none
    knobs(5, 0.0f, 0.5f, 0.5f);
    k[1] = 22.5f / 30.0f;
    play(5, 48.0f, k, 44128, 1u << 30);
    double c_root = level(8192, 32768, 130.81), c3rd = db(level(8192, 32768, 164.81), c_root);
    double c5th = db(level(8192, 32768, 196.0), c_root), c4th = db(level(8192, 32768, 174.61), c_root);
    // a note held (DECAY at Hold) half a second, let go: its level held, then gone; a tine (its decay) over in 20 s
    fm4_defaults(k);
    k[3] = 1.0f, k[4] = 0.0f, k[5] = 0.5f;               // (held; MOTION off, MOVE flat: its timbre steady too)
    play(1, 60.0f, k, 44128, 22048);
    auto rms = [&](uint32_t a, uint32_t len) {
      double e = 0.0;
      for (uint32_t i = a; i < a + len; ++i) e += buf[i] * buf[i];
      return std::sqrt(e / len);
    };
    double held_a = rms(8000, 4000), held_b = rms(18000, 4000), after = rms(40000, 4000);
    fm4_defaults(k);
    k[0] = 4.5f / 8.0f, k[1] = 16.5f / 30.0f;            // (two pairs, a tine)
    fm4_init();
    fm4_set(0, 60.0f, k);
    fm4_gate(0, 1);
    float tail = 1.0f;
    for (int b = 0; b < 44100 * 20 / 32; ++b) {
      fm4_render(0, buf, 32);
      if (b > 44100 * 19 / 32)
        for (int i = 0; i < 32; ++i) tail = i == 0 && b == 44100 * 19 / 32 + 1 ? std::fabs(buf[i]) : std::max(tail, std::fabs(buf[i]));
    }
    // a retrig over a held note: its largest step around it, against the note's own
    fm4_defaults(k);
    k[3] = 1.0f;
    fm4_init();
    fm4_set(0, 48.0f, k);
    fm4_gate(0, 1);
    for (uint32_t i = 0; i < 8832; i += 32) fm4_render(0, &buf[i], 32);
    float typ = 0.0f, step = 0.0f;
    for (uint32_t i = 4001; i < 8832; ++i) typ = std::max(typ, std::fabs(buf[i] - buf[i - 1]));
    fm4_gate(0, 0);
    fm4_gate(0, 1);
    for (uint32_t i = 8832; i < 13248; i += 32) fm4_render(0, &buf[i], 32);
    for (uint32_t i = 8800; i < 9000; ++i) step = std::max(step, std::fabs(buf[i] - buf[i - 1]));
    // anything: 300 settings, a trig each
    uint32_t sd = 4242u;
    auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return static_cast<float>(sd >> 8) / 16777216.0f; };
    float pk_any = 0.0f;
    int nan_any = 0;
    fm4_init();
    for (int s2 = 0; s2 < 300; ++s2) {
      for (int i = 0; i < 8; ++i) k[i] = rnd() < 0.15f ? (rnd() < 0.5f ? 0.0f : 1.0f) : rnd();
      fm4_set(s2 & 3, 12.0f + 100.0f * rnd(), k);
      fm4_gate(s2 & 3, 1);
      for (int b = 0; b < 300; ++b) {
        float tmp[32];
        if (b == 150) fm4_gate(s2 & 3, 0);
        fm4_render(s2 & 3, tmp, 32);
        for (int i = 0; i < 32; ++i) {
          if (!std::isfinite(tmp[i])) nan_any = 1;
          pk_any = std::max(pk_any, std::fabs(tmp[i]));
        }
      }
    }
    // SHAPE: each held two seconds (the second algorithm, MOTION off): its brightness (the differences' level over the
    // signal's, 100 ms windows, 50 apart) steady in the middle, moving elsewhere; Loop's again and again
    int shapes_ok = 1, loops = 0;
    double sh_lo = 1e9, sh_hi = 0.0;
    for (int sh = 0; sh < 15; ++sh) {
      fm4_defaults(k);
      k[3] = 1.0f, k[4] = 0.0f, k[5] = (static_cast<float>(sh) + 0.5f) / 15.0f;
      play(1, 45.0f, k, 88192, 1u << 30);   // (A2: 110 Hz, 11 cycles a window)
      double bmin = 1e9, bmax = 0.0, prev = -1.0;
      int rising = 0, peaks = 0;
      for (uint32_t w = 4410; w + 4410 <= 88192; w += 2205) {   // (from 0.1 s: the coupling capacitor settled)
        double ex = 0.0, ed = 0.0;
        for (uint32_t i = w; i < w + 4410; ++i) ex += buf[i] * buf[i], ed += (buf[i] - buf[i - 1]) * (buf[i] - buf[i - 1]);
        double b = std::sqrt(ed / (ex + 1e-12));
        bmin = std::min(bmin, b), bmax = std::max(bmax, b);
        if (prev >= 0.0) {
          if (b > prev * 1.05) rising = 1;
          else if (b < prev * 0.95 && rising) ++peaks, rising = 0;
        }
        prev = b;
      }
      double ratio = bmax / bmin;
      if (sh == 7) shapes_ok &= ratio < 1.02;
      else shapes_ok &= ratio > 1.08, sh_lo = std::min(sh_lo, ratio), sh_hi = std::max(sh_hi, ratio);
      if (sh == 14) loops = peaks;
    }
    shapes_ok &= loops >= 3;
    fm4_init();
    // a project of 0.11.0 (version 19): the FM's MOVE where SHAPE is: a swell (-50 %), a pluck (+30 %), a strong one
    // (+90 %), none; a lock (-60 %): their nearest shapes
    static project_t q19;
    eng_defaults(&q19);
    q19.version = 19;
    const float kMove[4] = {-0.5f, 0.3f, 0.9f, 0.0f};
    for (int t = 0; t < ENG_TRACKS; ++t) {
      q19.algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(eng_model_find(MK_FM, 0));
      q19.val[PG_TRACK(t, 1) * 4 + 2] = kMove[t];
    }
    for (int i = 0; i < ENG_PLOCKS; ++i) q19.lock[i].lane = 0xFF;
    q19.lock[0].lane = 1, q19.lock[0].step = 3, q19.lock[0].tp = 6, q19.lock[0].v = 51;   // (-60 %)
    int v19 = eng_upgrade(as_v23(&q19), PROJECT_SIZE_22) && eng_check(&q19);
    const int kWant[4] = {-2, 2, 3, 0};
    for (int t = 0; t < ENG_TRACKS; ++t) v19 &= fm4_shape((q19.val[PG_TRACK(t, 1) * 4 + 2] + 1.0f) * 0.5f) == kWant[t];
    v19 &= fm4_shape(static_cast<float>(q19.lock[0].v) / 255.0f) == -2;
    // a project of 0.9 (version 16): VIRTUAL ANALOG (19 then) the FM on its own knobs, its second algorithm; CHIPTUNE
    // (18) still Plaits', ZERO (10), MODAL (31). One of 0.10.0 (version 17): BASS (11) the FM's first algorithm, PAD
    // (16) its sixth, CHIPTUNE (19), MODAL (26)
    static project_t q16;
    float kl[8];
    fm4_defaults(kl);
    const int fm = eng_model_find(MK_FM, 0);
    eng_defaults(&q16);
    q16.version = 16;
    const uint8_t kOld[4] = {19, 18, 10, 31};
    for (int t = 0; t < ENG_TRACKS; ++t) q16.algo[PG_TRACK(t, 0)] = kOld[t];
    q16.val[PG_TRACK(0, 0) * 4 + 3] = 0.123f;
    int up_ok = eng_upgrade(as_v23(&q16), PROJECT_SIZE_22) && eng_check(&q16) && q16.algo[PG_TRACK(0, 0)] == fm &&
                q16.algo[PG_TRACK(1, 0)] == eng_model_find(MK_SQ1, 0) &&
                q16.algo[PG_TRACK(2, 0)] == eng_model_find(MK_ZERO, 0) && q16.algo[PG_TRACK(3, 0)] == eng_model_find(MK_PLAITS, 20);
    up_ok &= std::fabs(q16.val[PG_TRACK(0, 0) * 4 + 3] - kl[2]) < 1e-6f &&
             std::fabs(q16.val[PG_TRACK(0, 0) * 4 + 1] - 1.5f / 8.0f) < 1e-6f;
    eng_defaults(&q16);
    q16.version = 17;
    const uint8_t kOld17[4] = {11, 16, 19, 26};
    for (int t = 0; t < ENG_TRACKS; ++t) q16.algo[PG_TRACK(t, 0)] = kOld17[t];
    up_ok &= eng_upgrade(as_v23(&q16), PROJECT_SIZE_22) && eng_check(&q16) && q16.algo[PG_TRACK(0, 0)] == fm &&
             q16.algo[PG_TRACK(1, 0)] == fm && q16.algo[PG_TRACK(2, 0)] == eng_model_find(MK_SQ1, 0) &&
             q16.algo[PG_TRACK(3, 0)] == eng_model_find(MK_PLAITS, 20) &&
             std::fabs(q16.val[PG_TRACK(0, 0) * 4 + 1] - 0.5f / 8.0f) < 1e-6f &&
             std::fabs(q16.val[PG_TRACK(1, 0) * 4 + 1] - 5.5f / 8.0f) < 1e-6f;
    printf("the FM: knobs %s, its algorithms sounding %s (peaks %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f); C3 alone %.1f Hz, its octave "
           "%.0f dB; the saw %.0f %.0f dB, the square %.0f %.0f dB; on C6 between the harmonics: saw %.0f dB, square %.0f;"
           " on C5: SYNC %.0f, PW %.0f; a major chord: third %.0f, fifth %.0f, fourth %.0f dB; held %.3f %.3f, let go "
           "%.5f; a tine over: %.6f; a retrig: a step of %.3f (the note's %.3f); 300 settings: peak %.2f%s; versions 16, 17 %s; "
           "SHAPE %s (its brightness moving x%.2f to x%.2f, Loop's %d times), MOVE read as SHAPE %s\n",
           kit_ok ? "ok" : "WRONG", sound_ok ? "ok" : "NO", pk[0], pk[1], pk[2], pk[3], pk[4], pk[5], pk[6], pk[7], hz,
           sine2, saw2, saw3, sq2, sq3, saw_alias, sq_alias, sync_alias, pw_alias, c3rd, c5th, c4th, held_a, held_b, after,
           tail, step, typ, pk_any, nan_any ? ", NaN" : "", up_ok ? "read" : "WRONG", shapes_ok ? "ok" : "WRONG", sh_lo,
           sh_hi, loops, v19 ? "ok" : "WRONG");
    if (!shapes_ok || !v19 || !kit_ok || !sound_ok || std::fabs(hz - 130.81f) > 1.5f || sine2 > -80.0 || saw2 < -8.0 || saw2 > -4.0 ||
        saw3 < -14.0 || saw3 > -8.0 || sq2 > -40.0 || sq3 < -14.0 || sq3 > -8.0 || saw_alias > -55.0 || sq_alias > -55.0 ||
        sync_alias > -55.0 || pw_alias > -55.0 || c3rd < -12.0 || c5th < -12.0 || c4th > -40.0 || held_a < 0.1 ||
        std::fabs(held_b / held_a - 1.0) > 0.2 || after > 1e-3 || tail != 0.0f || step > 1.5f * typ + 0.02f ||
        nan_any || pk_any > 1.25f || !up_ok)
      ++failures, printf("FAIL: the FM\n");
  }

  // 47. PADS (mi_pads.cc, 0.11): its knobs; once a project; the chords (Pachelbel's in C major: their roots and
  // thirds; la Folia's major dominant in A minor, its raised third in the chord's scale); the chord's scale (the avoid
  // note out); SCALE Prog through the engine: at each of PADS' trigs the next chord, another track's notes in its
  // scale; the pad sounding, any setting finite and bounded
  {
    eng_play(0);
    eng_defaults(p);
    eng_fresh();
    const int pads = eng_model_find(MK_PADS, 0);
    int ok = pads >= 0;
    eng_set_model(3, pads);
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[3][c].depth = 0;   // (0.16.12: the demo's L3 on T4's TIMBRE is
                                                                    // COLOR here, its nine shares narrower: off)
    float kd[8];
    pads_defaults(kd);
    for (int i = 0; i < 8; ++i) {
      int param = PG_TRACK(3, 0) * 4 + 1 + i;
      float u = eng_pdef(param)->kind == K_BIP ? (p->val[param] + 1.0f) * 0.5f : p->val[param];
      if (std::fabs(u - kd[i]) > 1e-6f || std::strcmp(eng_param_name(param), pads_knob_name(i)) != 0) ok = 0;
    }
    int once = !eng_model_free(1, pads) && eng_model_free(3, pads) && eng_model_free(1, eng_model_find(MK_FM, 0));
    // the chords
    int ch[6], raised, roots_ok = 1, thirds_ok = 1;
    const int kRoot[8] = {0, 7, 9, 4, 5, 0, 5, 7}, kThird[8] = {4, 4, 3, 3, 4, 4, 4, 4};
    const float kTriad = 1.5f / 9.0f;     // (0.16.12: COLOR's nine, TRIAD the second; Pachelbel the fifth of 26)
    for (int i = 0; i < 8; ++i) {         // Pachelbel, C major (0xAB5)
      int nn = pads_chord(5, i, 0, 0xAB5, kTriad, ch, &raised);
      roots_ok &= nn == 3 && (ch[0] % 12) == kRoot[i];
      thirds_ok &= ch[1] - ch[0] == kThird[i];
    }
    int nf = pads_chord(7, 1, 0, 0x5AD, kTriad, ch, &raised);   // la Folia's V in A minor (over A): E major, G#
    uint16_t fm = pads_chord_scale(ch, nf, 0x5AD, raised);
    int folia = nf == 3 && ch[0] % 12 == 7 && ch[1] - ch[0] == 4 && raised == 11 && ((fm >> 11) & 1) && !((fm >> 10) & 1);
    int nc = pads_chord(5, 0, 0, 0xAB5, kTriad, ch, &raised);   // C major in C major: F out (over E)
    uint16_t cm = pads_chord_scale(ch, nc, 0xAB5, raised);
    int avoid = cm == 0xAB5 - (1u << 5);
    // SCALE Prog through the engine: PADS on track 4, Pachelbel, a trig each beat
    for (int st = 0; st < ENG_STEPS; ++st) eng_trig_set(3, st, st % 4 == 0 && st < 16);
    p->val[PG_TRACK(3, 0) * 4 + 1] = 3.5f / 17.0f;
    p->val[PG_GLOBAL * 4 + 1] = 15.0f;
    p->val[PG_GLOBAL * 4 + 2] = 0.0f;
    eng_play(1);
    int pos0 = -2, q0 = 0, q1 = 0, pos1 = -2;
    float pk = 0.0f;
    int nan = 0;
    for (int b = 0; b < 2000; ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        if (!std::isfinite(l[i])) nan = 1;
        pk = std::max(pk, std::fabs(l[i]));
      }
      if (eng_pads_pos() == 0 && pos0 == -2) pos0 = 0, q0 = eng_quantize(65);   // C: F to E
      if (eng_pads_pos() == 1 && pos1 == -2) pos1 = 1, q1 = eng_quantize(60);   // G: C to B
    }
    char line[48];
    eng_pads_line(line, sizeof line);
    eng_play(0);
    // the pad alone: 300 settings, a chord each
    uint32_t sd = 99u;
    auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return static_cast<float>(sd >> 8) / 16777216.0f; };
    float pk_any = 0.0f, k[8];
    int nan_any = 0;
    pads_init();
    for (int s2 = 0; s2 < 300; ++s2) {
      for (int i = 0; i < 8; ++i) k[i] = rnd() < 0.15f ? (rnd() < 0.5f ? 0.0f : 1.0f) : rnd();
      int prog = static_cast<int>(rnd() * static_cast<float>(pads_progs()));
      int cn = pads_chord(prog, s2, s2 % 7, 0x5AD, k[1], ch, &raised);
      pads_set(24.0f + 60.0f * rnd(), k, ch, cn, s2 % 12);
      pads_gate(1);
      for (int b = 0; b < 200; ++b) {
        float tmp[32];
        if (b == 100) pads_gate(0);
        pads_render(tmp, 32);
        for (int i = 0; i < 32; ++i) {
          if (!std::isfinite(tmp[i])) nan_any = 1;
          pk_any = std::max(pk_any, std::fabs(tmp[i]));
        }
      }
    }
    // held 40 s, dark (the sines most): its level steady (each voice's sine a rotation, its level held)
    pads_init();
    pads_defaults(k);
    k[3] = 1.0f;
    k[4] = 0.0f;
    k[5] = 0.0f;
    k[6] = 0.5f;
    {
      int cn = pads_chord(1, 0, 0, 0x5AD, k[1], ch, &raised);
      pads_set(57.0f, k, ch, cn, 9);
    }
    pads_gate(1);
    double e_early = 0.0, e_late = 0.0;
    for (int b = 0; b < 40 * 44100 / 32; ++b) {
      float tmp[32];
      pads_render(tmp, 32);
      for (int i = 0; i < 32; ++i) {
        if (b >= 3 * 44100 / 32 && b < 4 * 44100 / 32) e_early += tmp[i] * tmp[i];
        if (b >= 39 * 44100 / 32) e_late += tmp[i] * tmp[i];
      }
    }
    const double held = 10.0 * std::log10((e_late + 1e-12) / (e_early + 1e-12));
    pads_init();
    p->val[PG_GLOBAL * 4 + 1] = 2.0f;
    printf("PADS: knobs %s, once a project %s; Pachelbel's roots %s, thirds %s; la Folia's V %s; C's scale %s (0x%03X); "
           "SCALE Prog: the first chord F->%d, the second C->%d (%s); peak %.2f%s; 300 settings: peak %.2f%s; held 40 s: "
           "%+.2f dB\n",
           ok ? "ok" : "WRONG", once ? "ok" : "WRONG", roots_ok ? "ok" : "WRONG", thirds_ok ? "ok" : "WRONG",
           folia ? "major" : "WRONG", avoid ? "ok" : "WRONG", cm, q0, q1, line, pk, nan ? ", NaN" : "", pk_any,
           nan_any ? ", NaN" : "", held);
    if (!ok || !once || !roots_ok || !thirds_ok || !folia || !avoid || q0 != 64 || q1 != 59 || pk < 0.02f || nan ||
        nan_any || pk_any > 1.2f || std::fabs(held) > 0.1 || e_early < 1.0)
      ++failures, printf("FAIL: PADS\n");
  }

  // 48. POLY (mi_poly.cc, 0.12): its knobs; its chords (a chord memory's Min9; the scale's triad and seventh in A minor
  // on F, a seventh; PADS' chord through the engine, Root following it); its voices (a voice a trig, the last ringing
  // on under it; UNISON four; a chord three); held and let go; its alias (saw, pulse, the sub, up to C6); any setting
  // finite and bounded; a project of 0.11.1 (version 20) renumbered
  {
    eng_play(0);
    eng_defaults(p);
    eng_fresh();
    const int poly_m = eng_model_find(MK_POLY, 0);
    int ok = poly_m >= 0 && eng_model_free(1, poly_m) && eng_model_free(2, poly_m);
    eng_set_model(2, poly_m);
    float kd[8];
    poly_defaults(kd);
    for (int i = 0; i < 8; ++i) {
      int param = PG_TRACK(2, 0) * 4 + 1 + i;
      float u = eng_pdef(param)->kind == K_BIP ? (p->val[param] + 1.0f) * 0.5f : p->val[param];
      if (std::fabs(u - kd[i]) > 1e-6f || std::strcmp(eng_param_name(param), poly_knob_name(i)) != 0) ok = 0;
    }
    char tx[24];
    struct { int k; float u; const char* want; } kTx[] = {
        {0, 0.0f, "Saw"}, {0, 0.5f, "Square"}, {6, 0.25f, "Mono"}, {6, 0.75f, "Poly"},
        {3, 1.0f, "Hold"}, {7, 0.25f, "50% VINTAGE"},
        {7, 0.75f, "50% UNISON"}, {5, 0.25f, "50% SWELL"}, {4, 0.0f, "Off"}, {4, 1.0f, "-2 25%"}};
    for (const auto& x : kTx) {
      poly_knob_text(x.k, x.u, tx, sizeof tx);
      if (std::strcmp(tx, x.want)) ok = 0, printf("POLY: knob %d at %.3f written %s, %s expected\n", x.k, x.u, tx, x.want);
    }
    // the chords
    float nn[4];
    int n9 = poly_notes(14.5f / 15.0f, 48.0f, nullptr, 0, 0, 0, nn);
    int chords_ok = n9 == 4 && nn[0] == 48.0f && nn[1] == 51.0f && nn[2] == 58.0f && nn[3] == 62.0f;
    int nt = poly_notes(6.5f / 15.0f, 53.0f, nullptr, 0, 0x5AD, 9, nn);   // F in A minor: F A C
    chords_ok &= nt == 3 && nn[0] == 53.0f && nn[1] == 57.0f && nn[2] == 60.0f;
    int n7 = poly_notes(5.5f / 15.0f, 55.0f, nullptr, 0, 0x5AD, 9, nn);   // G: G B D F
    chords_ok &= n7 == 4 && nn[0] == 55.0f && nn[1] == 59.0f && nn[2] == 62.0f && nn[3] == 65.0f;
    const int pe[3] = {7, 11, 14};       // PADS' E major in A minor (its raised third), Root under C4: E3
    int nr = poly_notes(0.0f, 60.0f, pe, 3, 0x5AD, 9, nn);
    chords_ok &= nr == 1 && nn[0] == 52.0f;
    int ntp = poly_notes(6.5f / 15.0f, 60.0f, pe, 3, 0x5AD, 9, nn);
    chords_ok &= ntp == 3 && nn[0] == 52.0f && nn[1] == 56.0f && nn[2] == 59.0f;
    // the voices
    float k[8], buf[32];
    auto blocks = [&](int nb) { for (int b = 0; b < nb; ++b) poly_render(0, buf, 32); };
    auto trig = [&](const float* kk, float note, float chord = 7.5f / 15.0f) {   // (0.16.15: its chords PROPHET's)
      float v4[4];
      int c = poly_notes(chord, note, nullptr, 0, 0, 0, v4);
      poly_set(0, v4, c, kk);
      poly_gate(0, 1);
      blocks(20);
      poly_gate(0, 0);
      blocks(20);
    };
    poly_init();
    poly_defaults(k);
    k[3] = 0.8f;                        // (a long fall: 5 s)
    trig(k, 48.0f);
    trig(k, 55.0f);
    int two = poly_voices(0);
    k[6] = 0.25f;                       // (0.16.15) MODE Mono: one voice, the next trig its
    poly_init();
    trig(k, 48.0f);
    trig(k, 55.0f);
    int one = poly_voices(0);
    k[6] = 0.75f;
    k[7] = 0.9f;                        // UNISON
    poly_init();
    trig(k, 48.0f);
    int four = poly_voices(0);
    k[7] = 0.35f;                       // a major chord
    poly_init();
    trig(k, 48.0f, 11.5f / 15.0f);
    int three = poly_voices(0);
    // held, let go
    poly_init();
    poly_defaults(k);
    k[3] = 1.0f;
    {
      float v1 = 48.0f;
      poly_set(0, &v1, 1, k);
    }
    poly_gate(0, 1);
    double held_a = 0.0, held_b = 0.0, after = 0.0;
    for (int b = 0; b < 1400; ++b) {
      if (b == 700) poly_gate(0, 0);
      poly_render(0, buf, 32);
      for (int i = 0; i < 32; ++i) {
        if (b >= 100 && b < 200) held_a = std::max(held_a, (double)std::fabs(buf[i]));
        if (b >= 600 && b < 700) held_b = std::max(held_b, (double)std::fabs(buf[i]));
        if (b >= 1300) after = std::max(after, (double)std::fabs(buf[i]));
      }
    }
    // the alias: between the harmonics, under 16 kHz, against the fundamental
    static float ab[44160];
    auto level = [&](uint32_t from, uint32_t len, double hz) {
      double re = 0.0, im = 0.0, w = 2.0 * 3.14159265358979 * hz / 44100.0;
      for (uint32_t i = 0; i < len; ++i) {
        double h = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / len), v = ab[from + i] * h;
        re += v * std::cos(w * i);
        im -= v * std::sin(w * i);
      }
      return std::sqrt(re * re + im * im) / (0.25 * len);
    };
    double alias = -200.0;
    const float kW[3] = {0.0f, 0.5f, 0.7f};
    for (int c = 0; c < 4; ++c) {
      poly_init();
      poly_defaults(k);
      k[0] = kW[c < 3 ? c : 0], k[1] = 1.0f, k[2] = 0.0f, k[3] = 1.0f, k[4] = c == 3 ? 0.5f : 0.0f, k[5] = 0.5f;
      k[7] = 0.5f;
      float v1 = 84.0f;
      poly_set(0, &v1, 1, k);
      poly_gate(0, 1);
      for (uint32_t i = 0; i < 44128; i += 32) poly_render(0, &ab[i], 32);
      double f0 = 1046.5 / (c == 3 ? 2.0 : 1.0), ref = level(8192, 32768, f0), worst = 0.0;
      for (double f = 40.0; f < 16000.0; f += 10.0) {
        double h = f / f0, d = std::fabs(h - std::floor(h + 0.5)) * f0;
        if (d >= 25.0) worst = std::max(worst, level(8192, 32768, f));
      }
      alias = std::max(alias, 20.0 * std::log10(worst / ref + 1e-12));
    }
    // anything: 300 settings, a trig each, a chord or not
    uint32_t sd = 777u;
    auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return static_cast<float>(sd >> 8) / 16777216.0f; };
    float pk_any = 0.0f;
    int nan_any = 0;
    poly_init();
    for (int s2 = 0; s2 < 300; ++s2) {
      for (int i = 0; i < 8; ++i) k[i] = rnd() < 0.15f ? (rnd() < 0.5f ? 0.0f : 1.0f) : rnd();
      float v4[4];
      int c = poly_notes(k[6], 24.0f + 84.0f * rnd(), nullptr, 0, 0x5AD, 9, v4);
      poly_set(s2 & 3, v4, c, k);
      poly_gate(s2 & 3, 1);
      for (int b = 0; b < 200; ++b) {
        if (b == 100) poly_gate(s2 & 3, 0);
        poly_render(s2 & 3, buf, 32);
        for (int i = 0; i < 32; ++i) {
          if (!std::isfinite(buf[i])) nan_any = 1;
          pk_any = std::max(pk_any, std::fabs(buf[i]));
        }
      }
    }
    poly_init();
    // through the engine: POLY on track 3 following PADS on track 4 (Root)
    eng_defaults(p);
    eng_fresh();
    eng_set_model(3, eng_model_find(MK_PADS, 0));
    eng_set_model(2, poly_m);
    p->val[PG_TRACK(2, 1) * 4 + 3] = 2.0f * 0.5f / 15.0f - 1.0f;   // CHORD: Root
    for (int st = 0; st < ENG_STEPS; ++st) eng_trig_set(3, st, st % 4 == 0), eng_trig_set(2, st, st % 2 == 0);
    p->val[PG_TRACK(3, 0) * 4 + 1] = 3.5f / 17.0f;   // Pachelbel
    eng_play(1);
    float epk = 0.0f;
    int enan = 0;
    for (int b = 0; b < 1500; ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        if (!std::isfinite(l[i])) enan = 1;
        epk = std::max(epk, std::fabs(l[i]));
      }
    }
    eng_play(0);
    // a project of 0.11.1 (version 20): CHIPTUNE (13 then), PADS (12), MODAL (20), the FM (11)
    static project_t q20;
    eng_defaults(&q20);
    q20.version = 20;
    const uint8_t kOld20[4] = {13, 12, 20, 11};
    for (int t = 0; t < ENG_TRACKS; ++t) q20.algo[PG_TRACK(t, 0)] = kOld20[t];
    int v20 = eng_upgrade(as_v23(&q20), PROJECT_SIZE_22) && eng_check(&q20) &&
              q20.algo[PG_TRACK(0, 0)] == eng_model_find(MK_SQ1, 0) &&
              q20.algo[PG_TRACK(1, 0)] == eng_model_find(MK_PADS, 0) &&
              q20.algo[PG_TRACK(2, 0)] == eng_model_find(MK_PLAITS, 20) &&
              q20.algo[PG_TRACK(3, 0)] == eng_model_find(MK_FM, 0);
    printf("POLY: knobs %s; chords %s; voices: a trig each %d, Mono %d, unison %d, a major chord %d; held %.2f %.2f, let go "
           "%.5f; its alias at C6 %.0f dB; 300 settings: peak %.2f%s; after PADS: peak %.2f%s; version 20 %s\n",
           ok ? "ok" : "WRONG", chords_ok ? "ok" : "WRONG", two, one, four, three, held_a, held_b, after, alias, pk_any,
           nan_any ? ", NaN" : "", epk, enan ? ", NaN" : "", v20 ? "read" : "WRONG");
    if (!ok || !chords_ok || two != 2 || one != 1 || four != 4 || three != 3 || held_a < 0.1 || std::fabs(held_b / held_a - 1.0) > 0.2 ||
        after > 1e-3 || alias > -55.0 || nan_any || pk_any > 1.2f || enan || epk < 0.02f || !v20)
      ++failures, printf("FAIL: POLY\n");
  }

  // 49. PROPHET (mi_prophet.cc, 0.13): its knobs; its voices (a trig each, the last ringing on; a Min7 four); held and
  // let go; its alias (its oscillators' settings, up to C6; the sync's step); POLY-MOD (the sweep moves the sync's
  // timbre, B's FM the bell's); any setting finite and bounded. The machines' state shared on a track: the FM, POLY,
  // PROPHET, ZERO, a drum one after another on the same track through the engine, sounding, finite. A project of 0.12
  // (version 21) renumbered
  {
    eng_play(0);
    eng_defaults(p);
    eng_fresh();
    const int pro_m = eng_model_find(MK_PROPHET, 0);
    int ok = pro_m >= 0;
    eng_set_model(1, pro_m);
    float kd[8];
    pro_defaults(kd);
    for (int i = 0; i < 8; ++i) {
      int param = PG_TRACK(1, 0) * 4 + 1 + i;
      float u = eng_pdef(param)->kind == K_BIP ? (p->val[param] + 1.0f) * 0.5f : p->val[param];
      if (std::fabs(u - kd[i]) > 1e-6f || std::strcmp(eng_param_name(param), pro_knob_name(i)) != 0) ok = 0;
    }
    char tx[24];
    struct { int k; float u; const char* want; } kTx[] = {
        {0, 0.0f, "Saws"}, {0, 8.5f / 13.0f, "Sync"}, {0, 1.0f, "Noise"}, {7, 13.5f / 15.0f, "Min7"},
        {7, 7.5f / 15.0f, "Off"}, {6, 0.25f, "50% SWEEP"}, {6, 0.75f, "50% FM"}, {4, 0.0f, "Clean"}, {3, 1.0f, "Hold"}};
    for (const auto& x : kTx) {
      pro_knob_text(x.k, x.u, tx, sizeof tx);
      if (std::strcmp(tx, x.want)) ok = 0, printf("PROPHET: knob %d at %.3f written %s, %s expected\n", x.k, x.u, tx, x.want);
    }
    float k[8], buf[32];
    auto blocks = [&](int nb) { for (int b = 0; b < nb; ++b) pro_render(0, buf, 32); };
    auto trig = [&](const float* kk, float note) {
      float v4[4];
      int c = poly_notes(kk[7], note, nullptr, 0, 0, 0, v4);
      pro_set(0, v4, c, kk);
      pro_gate(0, 1);
      blocks(20);
      pro_gate(0, 0);
      blocks(20);
    };
    pro_init();
    pro_defaults(k);
    k[3] = 0.8f;
    trig(k, 48.0f);
    trig(k, 55.0f);
    int two = pro_voices(0);
    k[7] = 13.5f / 15.0f;
    pro_init();
    trig(k, 48.0f);
    int four = pro_voices(0);
    // held, let go
    pro_init();
    pro_defaults(k);
    k[3] = 1.0f, k[5] = 0.5f;
    {
      float v1 = 48.0f;
      pro_set(0, &v1, 1, k);
    }
    pro_gate(0, 1);
    double held_a = 0.0, held_b = 0.0, after = 0.0;
    for (int b = 0; b < 1400; ++b) {
      if (b == 700) pro_gate(0, 0);
      pro_render(0, buf, 32);
      for (int i = 0; i < 32; ++i) {
        if (b >= 100 && b < 200) held_a = std::max(held_a, (double)std::fabs(buf[i]));
        if (b >= 600 && b < 700) held_b = std::max(held_b, (double)std::fabs(buf[i]));
        if (b >= 1300) after = std::max(after, (double)std::fabs(buf[i]));
      }
    }
    // the alias: between the harmonics (the fundamental's), on C6
    static float ab[44160];
    auto level = [&](uint32_t from, uint32_t len, double hz) {
      double re = 0.0, im = 0.0, w = 2.0 * 3.14159265358979 * hz / 44100.0;
      for (uint32_t i = 0; i < len; ++i) {
        double h = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / len), v = ab[from + i] * h;
        re += v * std::cos(w * i);
        im -= v * std::sin(w * i);
      }
      return std::sqrt(re * re + im * im) / (0.25 * len);
    };
    auto render_one = [&](const float* kk, float note) {
      pro_init();
      pro_set(0, &note, 1, kk);
      pro_gate(0, 1);
      for (uint32_t i = 0; i < 44128; i += 32) pro_render(0, &ab[i], 32);
    };
    double alias = -200.0;
    const int kProg[8] = {0, 1, 2, 4, 5, 6, 7, 8};   // (Saws .. Sync: their fundamental the note's, the sub's half)
    for (int c = 0; c < 8; ++c) {
      pro_defaults(k);
      k[0] = (kProg[c] + 0.5f) / 13.0f, k[1] = 1.0f, k[2] = 0.0f, k[3] = 1.0f, k[4] = 0.0f, k[5] = 0.5f;
      render_one(k, 84.0f);
      double f0 = 1046.5 / (kProg[c] == 5 ? 2.0 : 1.0), ref = level(8192, 32768, f0), worst = 0.0;
      for (double f = 40.0; f < 16000.0; f += 10.0) {
        double h = f / f0, d = std::fabs(h - std::floor(h + 0.5)) * f0;
        if (d >= 25.0) worst = std::max(worst, level(8192, 32768, f));
      }
      alias = std::max(alias, 20.0 * std::log10(worst / ref + 1e-12));
    }
    // POLY-MOD: the sweep (Sync, the filter's envelope on A: its timbre at the trig and a second later); FM (Bell)
    auto bright = [&](uint32_t from, uint32_t len) {
      double ex = 0.0, ed = 0.0;
      for (uint32_t i = from; i < from + len; ++i) ex += ab[i] * ab[i], ed += (ab[i] - ab[i - 1]) * (ab[i] - ab[i - 1]);
      return std::sqrt(ed / (ex + 1e-12));
    };
    pro_defaults(k);
    k[0] = 8.5f / 13.0f, k[1] = 1.0f, k[2] = 0.0f, k[3] = 0.7f, k[5] = 0.9f, k[6] = 0.1f;
    render_one(k, 48.0f);
    double sweep = bright(441, 2205) / bright(33075, 2205);
    pro_defaults(k);
    k[0] = 10.5f / 13.0f, k[1] = 1.0f, k[2] = 0.0f, k[3] = 1.0f, k[5] = 0.5f, k[6] = 0.5f;
    render_one(k, 60.0f);
    static float plain[4410];
    for (int i = 0; i < 4410; ++i) plain[i] = ab[8820 + i];
    k[6] = 0.9f;
    render_one(k, 60.0f);
    double fd = 0.0, fe = 0.0;           // (FM: how much it moved the wave, against its level)
    for (int i = 0; i < 4410; ++i) fd += (ab[8820 + i] - plain[i]) * (ab[8820 + i] - plain[i]), fe += plain[i] * plain[i];
    double fmb = fd / (fe + 1e-12);
    // anything: 300 settings
    uint32_t sd = 4321u;
    auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return static_cast<float>(sd >> 8) / 16777216.0f; };
    float pk_any = 0.0f;
    int nan_any = 0;
    pro_init();
    for (int s2 = 0; s2 < 300; ++s2) {
      for (int i = 0; i < 8; ++i) k[i] = rnd() < 0.15f ? (rnd() < 0.5f ? 0.0f : 1.0f) : rnd();
      float v4[4];
      int c = poly_notes(k[7], 24.0f + 84.0f * rnd(), nullptr, 0, 0x5AD, 9, v4);
      pro_set(s2 & 3, v4, c, k);
      pro_gate(s2 & 3, 1);
      for (int b = 0; b < 200; ++b) {
        if (b == 100) pro_gate(s2 & 3, 0);
        pro_render(s2 & 3, buf, 32);
        for (int i = 0; i < 32; ++i) {
          if (!std::isfinite(buf[i])) nan_any = 1;
          pk_any = std::max(pk_any, std::fabs(buf[i]));
        }
      }
    }
    pro_init();
    // the machines' state shared: one track through the FM, POLY, PROPHET, ZERO, a drum, the FM again
    eng_defaults(p);
    eng_fresh();
    for (int st = 0; st < ENG_STEPS; ++st) eng_trig_set(0, st, st % 2 == 0);
    const int kSeq[6] = {eng_model_find(MK_FM, 0), eng_model_find(MK_POLY, 0), pro_m, eng_model_find(MK_ZERO, 0),
                         eng_model_find(MK_DRUM, 0), eng_model_find(MK_FM, 0)};
    eng_play(1);
    int shared_ok = 1;
    for (int m = 0; m < 6; ++m) {
      eng_set_model(0, kSeq[m]);
      float spk = 0.0f;
      for (int b = 0; b < 300; ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i])) shared_ok = 0;
          spk = std::max(spk, std::fabs(l[i]));
        }
      }
      if (spk < 0.02f || spk > 1.5f) shared_ok = 0, printf("PROPHET: the shared state, model %d: peak %.3f\n", kSeq[m], spk);
    }
    eng_play(0);
    // a project of 0.12 (version 21): CHIPTUNE (14 then), POLY (13), MODAL (21), PADS (12)
    static project_t q21;
    eng_defaults(&q21);
    q21.version = 21;
    const uint8_t kOld21[4] = {14, 13, 21, 12};
    for (int t = 0; t < ENG_TRACKS; ++t) q21.algo[PG_TRACK(t, 0)] = kOld21[t];
    int v21 = eng_upgrade(as_v23(&q21), PROJECT_SIZE_22) && eng_check(&q21) &&
              q21.algo[PG_TRACK(0, 0)] == eng_model_find(MK_SQ1, 0) &&
              q21.algo[PG_TRACK(1, 0)] == eng_model_find(MK_POLY, 0) &&
              q21.algo[PG_TRACK(2, 0)] == eng_model_find(MK_PLAITS, 20) &&
              q21.algo[PG_TRACK(3, 0)] == eng_model_find(MK_PADS, 0);
    printf("PROPHET: knobs %s; voices: a trig each %d, a Min7 %d; held %.2f %.2f, let go %.5f; its alias at C6 %.0f dB; "
           "the sweep: brighter x%.2f at the trig; FM: %.2f of it moved; 300 settings: peak %.2f%s; the state shared %s; "
           "version 21 %s\n",
           ok ? "ok" : "WRONG", two, four, held_a, held_b, after, alias, sweep, fmb, pk_any, nan_any ? ", NaN" : "",
           shared_ok ? "ok" : "WRONG", v21 ? "read" : "WRONG");
    if (!ok || two != 2 || four != 4 || held_a < 0.1 || std::fabs(held_b / held_a - 1.0) > 0.2 || after > 1e-3 ||
        alias > -55.0 || sweep < 1.3 || fmb < 0.05 || nan_any || pk_any > 1.2f || !shared_ok || !v21)
      ++failures, printf("FAIL: PROPHET\n");
  }

  {                                     // 50. TRAUMA (0.14): at 0 nothing; up the knob more harmonics on the same note
    // (the fundamental the strongest partial, its pitch kept), the level about kept, nothing past the edge; the
    // filters' FM at most 10 % of its depth, none under 40 %; a 0.13 project's padding read as TRAUMA at 0
    project_t* p = eng_project();
    const uint32_t n = 128;
    static float l[128], r[128], out[22050];
    auto play = [&](int dose, int note) {   // track 1 alone, the keyboard's note held: half a second after 0.2 s
      eng_defaults(p);
      eng_fresh();
      eng_play(0);
      eng_set_model(0, eng_model_find(MK_FM, 0));
      p->trauma = static_cast<uint8_t>(dose);
      for (int b = 0; b < 300; ++b) eng_render(l, r, n);
      eng_key(0, note, 1);
      for (int b = 0; b < 69; ++b) eng_render(l, r, n);
      for (int b = 0; b < 172; ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) out[b * n + i] = l[i];
      }
      eng_key(0, note, 0);
      return 440.0 * std::pow(2.0, (eng_note(0) - 69) / 12.0);
    };
    auto at = [&](double f) {           // a partial's level (a Hann window over the half second)
      double re = 0.0, im = 0.0;
      const int len = 172 * 128;
      for (int i = 0; i < len; ++i) {
        double w = 0.5 - 0.5 * std::cos(6.283185307 * i / len), ph = 6.283185307 * f * i / 44100.0;
        re += w * out[i] * std::cos(ph), im += w * out[i] * std::sin(ph);
      }
      return std::sqrt(re * re + im * im);
    };
    struct Got { double f0, fund, side, harm, rms, pk; int nan; };
    auto measure = [&](int dose) {
      Got g{};
      g.f0 = play(dose, 45);
      g.fund = at(g.f0);
      g.side = std::max(at(g.f0 * 1.0594631), at(g.f0 / 1.0594631));
      for (int h = 2; h <= 9; ++h) g.harm += at(g.f0 * h);
      double e = 0.0;
      for (int i = 0; i < 172 * 128; ++i) {
        if (!std::isfinite(out[i])) g.nan = 1;
        e += static_cast<double>(out[i]) * out[i], g.pk = std::max(g.pk, static_cast<double>(std::fabs(out[i])));
      }
      g.rms = std::sqrt(e / (172.0 * 128.0));
      return g;
    };
    Got g0 = measure(0), g50 = measure(50), g100 = measure(100);
    double h0 = g0.harm / (g0.fund + 1e-12), h50 = g50.harm / (g50.fund + 1e-12), h100 = g100.harm / (g100.fund + 1e-12);
    int ok = !g0.nan && !g50.nan && !g100.nan && g100.f0 == g0.f0 && h50 > h0 * 1.3 && h100 > h50 * 1.2 &&
             g100.fund > 3.0 * g100.side && g50.fund > 3.0 * g50.side && g100.pk < 1.0 && g100.rms > 0.6 * g0.rms &&
             g100.rms < 1.6 * g0.rms && g50.rms > 0.6 * g0.rms && g50.rms < 1.6 * g0.rms;
    // the filters' FM: LADDER on track 1, its FM at 0, the steps playing; the trigs' FM read as each plays
    float fm_max[3] = {0.0f, 0.0f, 0.0f};
    const int kDose[3] = {30, 70, 100};
    for (int c = 0; c < 3; ++c) {
      eng_defaults(p);
      eng_fresh();
      eng_set_model(0, eng_model_find(MK_FM, 0));
      p->algo[PG_FILT(0, 0)] = MF_LADDER;
      int pf = eng_track_param(0, TP_FM);
      p->val[pf] = 0.0f;
      for (int st = 0; st < ENG_STEPS; ++st) eng_trig_set(0, st, 1);
      p->trauma = static_cast<uint8_t>(kDose[c]);
      eng_play(1);
      for (int b = 0; b < 3000; ++b) {
        eng_render(l, r, n);
        fm_max[c] = std::max(fm_max[c], std::fabs(eng_now(pf)));
      }
      eng_play(0);
    }
    const float kTop = std::log(1.0f + 0.1f * 53.59815f) * 0.25f;   // (the knob for 10 % of the depth)
    ok &= fm_max[0] == 0.0f && fm_max[1] > 0.05f && fm_max[1] < kTop * 0.75f && fm_max[2] > kTop * 0.6f &&
          fm_max[2] <= kTop + 1e-3f;
    // a 0.13 project (version 22): what was its padding read as TRAUMA at 0
    static project_t q22;
    eng_defaults(&q22);
    q22.version = 22;
    q22.trauma = 0xA5;
    int v22 = eng_upgrade(as_v23(&q22), PROJECT_SIZE_22) && eng_check(&q22) && q22.trauma == 0 && q22.version == PROJECT_VERSION;
    printf("TRAUMA: harmonics against the note 0 %.3f, 50 %% %.3f, 100 %% %.3f; the note %.1f Hz %s, over its "
           "neighbours x%.0f; rms %.3f %.3f %.3f, peak at 100 %% %.2f; the filter's FM at 30 %% %.3f, 70 %% %.3f, "
           "100 %% %.3f (at most %.3f); version 22 %s\n",
           h0, h50, h100, g100.f0, g100.f0 == g0.f0 ? "kept" : "MOVED", g100.fund / (g100.side + 1e-12), g0.rms,
           g50.rms, g100.rms, g100.pk, fm_max[0], fm_max[1], fm_max[2], kTop, v22 ? "read" : "WRONG");
    if (!ok || !v22) ++failures, printf("FAIL: TRAUMA\n");
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 51. the presets (0.15.1): a track's sound read and written back, onto any
    // track (its randoms too); PADS refused where another track holds it; each factory sound plays a note: sound, no
    // NaN, nothing past the edge, its length and brightness as its name says
    project_t* p = eng_project();
    const uint32_t n = 128;
    static float l[128], r[128], out[44100 * 2];
    eng_sound_t a, b;
    eng_blank(p);
    eng_fresh();
    eng_set_model(0, eng_model_find(MK_POLY, 0));
    p->algo[PG_FILT(0, 0)] = MF_MS20;
    p->val[PG_FILT(0, 0) * 4 + 1] = 0.77f;
    p->val[PG_BEND(0) * 4 + 1] = 0.4f;
    p->rnd[0][TP_CUTOFF] = 40;
    p->rseed[0][TP_CUTOFF] = 9;
    eng_sound_get(0, &a);
    int rt = eng_sound_check(&a, sizeof a) && eng_sound_set(3, &a) == 0;
    eng_sound_get(3, &b);
    rt &= !memcmp(a.val, b.val, sizeof a.val) && !memcmp(a.algo, b.algo, sizeof a.algo) &&
          !memcmp(a.rnd, b.rnd, sizeof a.rnd) && !memcmp(a.rseed, b.rseed, sizeof a.rseed);
    rt &= p->algo[PG_TRACK(3, 0)] == eng_model_find(MK_POLY, 0) && p->algo[PG_FILT(3, 0)] == MF_MS20 &&
          p->val[PG_FILT(3, 0) * 4 + 1] == 0.77f && p->val[PG_BEND(3) * 4 + 1] == 0.4f && p->rnd[3][TP_CUTOFF] == 40 &&
          p->rseed[3][TP_CUTOFF] == 9 && p->val[PG_MIX * 4 + 3] == 0.75f;   // (its level, its mixing: as they were)
    eng_set_model(1, eng_model_find(MK_PADS, 0));
    eng_factory(12, &b);                // (DARK PAD)
    int pads = b.algo[0] == eng_model_find(MK_PADS, 0) && eng_sound_set(2, &b) == -2 && eng_sound_set(1, &b) == 0;
    a.magic = 0;
    int refused = eng_sound_set(0, &a) == -1;
    int bad = 0, names = 1;
    for (int i = 0; i < ENG_FACTORY; ++i) {
      eng_blank(p);
      eng_fresh();
      eng_play(0);
      if (eng_factory(i, &a) != 0 || !eng_sound_check(&a, sizeof a) || eng_sound_set(0, &a) != 0) { ++bad; continue; }
      names &= strlen(eng_factory_name(i)) > 0 && !strcmp(a.name, eng_factory_name(i));
      for (int bl = 0; bl < 40; ++bl) eng_render(l, r, n);
      eng_key(0, 48, 1);                // (as a step plays it: C3, its NOTE 0)
      const int len = 44100 * 2 / 128;
      double e = 0.0, pk = 0.0;
      int nan = 0, zc = 0, last = -1;
      for (int bl = 0; bl < len; ++bl) {
        if (bl == 40) eng_key(0, 48, 0);   // (a sixteenth's at 120 BPM held, about)
        eng_render(l, r, n);
        for (uint32_t k = 0; k < n; ++k) out[bl * n + k] = 0.5f * (l[k] + r[k]);
      }
      double env = 0.0;
      for (int k = 0; k < len * 128; ++k) {
        float x = out[k];
        if (!std::isfinite(x)) nan = 1;
        pk = std::max(pk, static_cast<double>(std::fabs(x)));
        if (k < 22050) e += static_cast<double>(x) * x, zc += (x >= 0.0f) != (out[k > 0 ? k - 1 : 0] >= 0.0f);
      }
      for (int k = 0; k < len * 128; ++k) {   // (the length: where the level stays under -40 dB of the peak)
        env = std::max(static_cast<double>(std::fabs(out[k])), env * 0.9995);
        if (env > pk * 0.01) last = k;
      }
      double rms = std::sqrt(e / 22050.0);
      printf("  A%-2d %-10s %-9s rms %.3f peak %.2f, %4.0f ms, %5.0f crossings a second%s\n", i + 1, a.name,
             eng_algo_name(PG_TRACK(0, 0), a.algo[0]), rms, pk, last * 1000.0 / 44100.0, zc * 2.0, nan ? ", NaN" : "");
      if (nan || rms < 0.003 || pk > 1.2) ++bad;
    }
    printf("presets: a sound read and written onto another track %s; PADS %s; a broken one %s; %d factory sounds "
           "play%s\n", rt ? "the same" : "WRONG", pads ? "once a project" : "WRONG", refused ? "refused" : "TAKEN",
           ENG_FACTORY - bad, names ? "" : ", their names WRONG");
    if (!rt || !pads || !refused || bad || !names) ++failures, printf("FAIL: presets\n");
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 52. PADS held (0.15.2): DECAY at Hold, one trig a bar: its gate up the whole
    // bar (to the next trig), not GATE's share of a step; DECAY under Hold: GATE's share as before; Endless the same,
    // over its rests
    project_t* p = eng_project();
    static float l[128], r[128];
    auto bar_up = [&](float decay, int endless) {   // the share of the first bar the gate is up
      eng_blank(p);
      eng_fresh();
      eng_set_model(0, eng_model_find(MK_PADS, 0));
      p->val[PG_TRACK(0, 1) * 4 + 0] = decay;
      p->val[PG_GLOBAL * 4 + 0] = 0.60206f;   // (120 BPM: a bar 2 s)
      if (endless) {
        p->endless[0][0] = 60;
        for (int i = 1; i < 4; ++i) p->endless[0][i] = ENDLESS_REST;
        p->endless_len[0] = 4;
        p->val[PG_ARP(0) * 4 + 0] = 5.0f;     // (quarters: four a bar, one note then three rests)
      } else {
        eng_trig_set(0, 0, 1);
      }
      eng_play(1);
      int up = 0, all = 0;
      for (int b = 0; b < 689; ++b) {   // (2 s)
        eng_render(l, r, 128);
        if (b >= 4) up += (eng_gates() & 1u) != 0, ++all;
      }
      eng_play(0);
      return static_cast<double>(up) / all;
    };
    double held = bar_up(1.0f, 0), timed = bar_up(0.8f, 0), eheld = bar_up(1.0f, 1), etimed = bar_up(0.8f, 1);
    printf("PADS held: its gate up %.0f %% of the bar at Hold (one trig), %.0f %% under it; Endless %.0f %%, %.0f %%\n",
           held * 100.0, timed * 100.0, eheld * 100.0, etimed * 100.0);
    if (held < 0.9 || timed > 0.06 || eheld < 0.9 || etimed > 0.3) ++failures, printf("FAIL: PADS held\n");
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 53. the lanes' SPEED and DIRECTION, RATCH's tuplets, CURVE (0.16): a lane's
    // trigs as many as its speed says over a bar; its steps in each direction; 3:4 three hits evenly over four
    // sixteenths; CURVE's gaps closer and closer (or apart); the pattern's length at its lanes' speeds; a 0.15 project
    // read: its locks of RATCH the same, its randoms where they were, x1 and forward
    project_t* p = eng_project();
    static float l[128], r[128];
    auto setup = [&]() {
      eng_blank(p);
      eng_fresh();
      eng_set_model(0, eng_model_find(MK_DRUM, 0));
      p->val[PG_GLOBAL * 4 + 0] = 0.60206f;   // (120 BPM: a sixteenth 5512.5 samples, a bar 2 s)
    };
    auto rises = [&](int blocks, int* at, int max) {   // the gate's rises on T1, their blocks
      int n = 0, last = 0;
      eng_play(1);
      for (int b = 0; b < blocks; ++b) {
        eng_render(l, r, 128);
        int g = (eng_gates() & 1u) != 0;
        if (g && !last && n < max) at[n++] = b;
        else if (g && !last) ++n;
        last = g;
      }
      eng_play(0);
      return n;
    };
    static int at[64];
    const int kSp[4] = {-2, -1, 0, 2};  // x1/2, x3/4, x1, x2
    const int kWant[4] = {8, 12, 16, 32};
    int speed_ok = 1, counts[4];
    for (int c = 0; c < 4; ++c) {
      setup();
      for (int st = 0; st < 16; ++st) eng_trig_set(0, st, 1);
      p->speed[0] = static_cast<int8_t>(kSp[c]);
      counts[c] = rises(689 - 4, at, 64);   // (a bar, the last frames left out)
      speed_ok &= counts[c] == kWant[c];
    }
    // the directions: the steps a lane of 4 goes through
    const int kDir[6][8] = {{0, 1, 2, 3, 0, 1, 2, 3}, {3, 2, 1, 0, 3, 2, 1, 0}, {0, 1, 2, 3, 2, 1, 0, 1},
                            {0, -1, -1, -1, -1, -1, -1, -1}, {0, -2, -2, -2, -2, -2, -2, -2}, {0, 1, 2, 1, 2, 3, 2, 3}};
    int dir_ok = 1;
    for (int d = 0; d < DIR_N; ++d) {
      int seen[8], k = 0;               // the lane's step at each sixteenth's middle
      setup();
      p->val[PG_STEP(0) * 4 + 3] = 4.0f;
      p->dir[0] = static_cast<uint8_t>(d);
      eng_play(1);
      k = 0;
      for (int b = 0; b < 8 * 43 + 30 && k < 8; ++b) {
        eng_render(l, r, 128);
        if (b % 43 == 21) seen[k++] = eng_step_now(0);   // (5512 samples a sixteenth: 43.07 blocks)
      }
      eng_play(0);
      for (int i = 0; i < 8; ++i) {
        int w = kDir[d][i];
        if (w >= 0) dir_ok &= seen[i] == w;
        else if (w == -1) dir_ok &= seen[i] >= 0 && seen[i] < 4;
        else if (i) dir_ok &= seen[i] >= 0 && seen[i] < 4 && ((seen[i] - seen[i - 1] + 4) % 4 == 1 || (seen[i - 1] - seen[i] + 4) % 4 == 1);
      }
      if (!dir_ok) {
        printf("  direction %s:", eng_dir_name(d));
        for (int i = 0; i < 8; ++i) printf(" %d", seen[i]);
        printf("\n");
      }
    }
    // RATCH 3 over SPAN 4 (0.16.4): one trig, three hits over four sixteenths, evenly
    setup();
    eng_trig_set(0, 0, 1);
    p->val[PG_STEP(0) * 4 + 2] = static_cast<float>(eng_ratch_join(3, 4));
    char txt[16], tup[16];
    eng_param_text(PG_STEP(0) * 4 + 2, p->val[PG_STEP(0) * 4 + 2], tup, sizeof tup);
    int n34 = rises(4 * 43 + 10, at, 64);
    double g1 = at[1] - at[0], g2 = at[2] - at[1];
    int tup_ok = n34 == 3 && std::fabs(g1 - 4.0 * 43.07 / 3.0) < 3.0 && std::fabs(g2 - g1) < 2.0 && !strcmp(tup, "x3");
    // CURVE: eight hits over a step at x1/4 (half a second), to the right closer and closer, to the left apart
    auto curve_gaps = [&](float c, double* first, double* last) {
      setup();
      eng_trig_set(0, 0, 1);
      p->speed[0] = -3;                 // (x1/4)
      p->val[PG_STEP(0) * 4 + 2] = 8.0f;
      p->val[PG_CURVE * 4 + 0] = c;
      int k = rises(4 * 43 + 10, at, 64), mono = 1;
      for (int i = 2; i < k && i < 8; ++i) mono &= c > 0 ? at[i] - at[i - 1] <= at[i - 1] - at[i - 2] : at[i] - at[i - 1] >= at[i - 1] - at[i - 2];
      *first = at[1] - at[0];
      *last = at[k - 1] - at[k - 2];
      return k == 8 && mono;
    };
    double f1, l1, f2, l2;
    int curve_ok = curve_gaps(1.0f, &f1, &l1) && curve_gaps(-1.0f, &f2, &l2) && l1 < f1 * 0.2 && l2 > f2 * 5.0;
    eng_param_text(PG_CURVE * 4 + 0, 0.4f, txt, sizeof txt);
    curve_ok &= !strcmp(txt, "40% Fast");
    // the pattern's length: 16 at x1/2 is 32 sixteenths; there and back over 16, 30
    setup();
    p->speed[0] = -2;
    int len_half = eng_pattern_len();
    p->speed[0] = 0;
    p->dir[1] = DIR_PINGPONG;
    int len_pp = eng_pattern_len();
    // a 0.15 project (version 23): RATCH locked to 2 (its travel 1..8 then), a random on GDROP, the last parameter then
    setup();
    eng_trig_set(0, 3, 1);
    eng_lock_set(0, 3, TP_RATCHET, 2.0f);   // (as 0.16 writes it: re-written below as 0.15 did)
    for (int i = 0; i < ENG_PLOCKS; ++i)
      if (p->lock[i].lane == 0 && p->lock[i].tp == TP_RATCHET) p->lock[i].v = static_cast<uint8_t>(255.0f / 7.0f + 0.5f);
    p->rnd[2][TP_GDROP] = 33;
    p->rseed[2][TP_GDROP] = 7;
    p->version = 23;
    static project_t q;
    q = *p;
    int up = eng_upgrade(as_v23(&q), PROJECT_SIZE_22) && eng_check(&q);
    float lv = -1.0f;
    for (int i = 0; i < ENG_PLOCKS; ++i)
      if (q.lock[i].lane == 0 && q.lock[i].tp == TP_RATCHET) lv = static_cast<float>(eng_ratch_hits(q.lock[i].v + 1, 0));
    up &= std::fabs(lv - 2.0f) < 0.1f && q.rnd[2][TP_GDROP] == 33 && q.rseed[2][TP_GDROP] == 7 && !q.rnd[2][TP_CURVE] &&
          !q.speed[0] && !q.dir[0] && q.val[PG_CURVE * 4] == 0.0f;
    printf("the lanes (0.16): a bar of sixteenths at x1/2 x3/4 x1 x2: %d %d %d %d trigs; the directions %s; 3 over 4 (\"%s\"): "
           "%d hits, %.0f and %.0f blocks apart; CURVE: gaps %.0f to %.0f blocks to the right, %.0f to %.0f to the left; "
           "the pattern %d sixteenths at x1/2, %d there and back; a 0.15 project %s\n",
           counts[0], counts[1], counts[2], counts[3], dir_ok ? "ok" : "WRONG", tup, n34, g1, g2, f1, l1, f2,
           l2, len_half, len_pp, up ? "read, its RATCH locks the same" : "WRONG");
    if (!speed_ok || !dir_ok || !tup_ok || !curve_ok || len_half != 32 || len_pp != 30 || !up) {
      printf("FAIL: the lanes (speed %d dir %d tuplet %d curve %d len %d %d upgrade %d)\n", speed_ok, dir_ok, tup_ok,
             curve_ok, len_half, len_pp, up);
      ++failures;
    }
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 54. Endless' options (0.16.2): STRIDE (one note in so many), REST (a rest
    // every so many steps, the notes waiting), HOLES (rests at random), LEAP (an octave up at random); a 0.16.0 project
    // read, its options none
    project_t* p = eng_project();
    static float l[128], r[128];
    auto play = [&](int stride, int rest, int holes, int leap, int len, int* got) {   // the notes of 10 steps (-1 a rest)
      eng_blank(p);
      eng_fresh();
      eng_set_model(0, eng_model_find(MK_FM, 0));
      p->val[PG_GLOBAL * 4 + 0] = 0.60206f;   // (120 BPM)
      p->val[PG_GLOBAL * 4 + 1] = 0.0f;       // (SCALE Off: the notes as recorded, a semitone apart)
      for (int i = 0; i < len; ++i) p->endless[0][i] = static_cast<uint8_t>(60 + i);
      p->endless_len[0] = static_cast<uint8_t>(len);
      p->val[PG_ARP(0) * 4 + 0] = 2.0f;       // (sixteenths)
      eng_endless_opt_set(p, 0, EO_STRIDE, stride);
      eng_endless_opt_set(p, 0, EO_REST, rest);
      eng_endless_opt_set(p, 0, EO_HOLES, holes);
      eng_endless_opt_set(p, 0, EO_LEAP, leap);
      eng_play(1);
      int k = 0;
      for (int b = 0; b < 10 * 43 + 20 && k < 10; ++b) {
        eng_render(l, r, 128);
        if (b % 43 == 10) got[k++] = (eng_gates() & 1u) ? eng_note(0) - 48 - 12 : -1;   // (C3 +12: the Endless note's)
      }
      eng_play(0);
    };
    int a[10], b2[10], c[10], d[10], e[10];
    play(2, 0, 0, 0, 5, a);             // 0 2 4 1 3 0 2 4 1 3
    play(1, 3, 0, 0, 4, b2);            // 0 1 - 2 3 - 0 1 - 2
    play(1, 0, 100, 0, 4, c);           // all rests
    play(1, 0, 0, 100, 4, d);           // each an octave up
    play(1, 0, 0, 0, 4, e);             // as before: 0 1 2 3 0 1 ...
    const int kA[10] = {0, 2, 4, 1, 3, 0, 2, 4, 1, 3}, kB[10] = {0, 1, -1, 2, 3, -1, 0, 1, -1, 2};
    int ok = 1, rests = 1, leaps = 1, plain = 1;
    for (int i = 0; i < 10; ++i) {
      ok &= a[i] == kA[i] + 0 && b2[i] == kB[i];
      rests &= c[i] == -1;
      leaps &= d[i] == i % 4 + 12;
      plain &= e[i] == i % 4;
    }
    // a 0.16.0 project (version 24, 3804 bytes): read, Endless' options none (its padding byte left alone)
    static project_t q;
    eng_defaults(&q);
    memset(q.endless_opt, 0xA5, sizeof q.endless_opt);
    int up = eng_upgrade(&q, as_v26(&q, 24)) && eng_check(&q);
    for (int t = 0; t < ENG_TRACKS; ++t)
      for (int i = 0; i < 4; ++i) up &= q.endless_opt[t][i] == 0;
    printf("Endless' options: STRIDE 2 over 5 %d %d %d %d %d %d; REST 3 %d %d %d %d %d %d; HOLES 100 %s; LEAP 100 %s; "
           "none: as before %s; a 0.16.0 project %s\n", a[0], a[1], a[2], a[3], a[4], a[5], b2[0], b2[1], b2[2], b2[3],
           b2[4], b2[5], rests ? "all rests" : "WRONG", leaps ? "an octave up" : "WRONG", plain ? "ok" : "WRONG",
           up ? "read" : "WRONG");
    if (!ok || !rests || !leaps || !plain || !up) {
      printf("  plain:");
      for (int i = 0; i < 10; ++i) printf(" %d", e[i]);
      printf("  leap:");
      for (int i = 0; i < 10; ++i) printf(" %d", d[i]);
      printf("\n");
      ++failures, printf("FAIL: Endless' options\n");
    }
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 55. RATCH over SPAN (0.16.4): 4 hits over 1, 2 and 16 steps; a 0.16.2 project's
    // tuplets read as hits over a span (3:4 as 3 over 4), its locks too
    project_t* p = eng_project();
    static float l[128], r[128];
    static int at[64];
    auto hits = [&](int h, int sp, int blocks) {
      eng_blank(p);
      eng_fresh();
      eng_set_model(0, eng_model_find(MK_DRUM, 0));
      p->val[PG_GLOBAL * 4 + 0] = 0.60206f;
      p->val[PG_STEP(0) * 4 + 3] = 64.0f;   // (a lane long enough: the one trig alone)
      eng_trig_set(0, 0, 1);
      p->val[PG_STEP(0) * 4 + 2] = static_cast<float>(eng_ratch_join(h, sp));
      int n = 0, last = 0;
      eng_play(1);
      for (int b = 0; b < blocks; ++b) {
        eng_render(l, r, 128);
        int g = (eng_gates() & 1u) != 0;
        if (g && !last && n < 64) at[n++] = b;
        last = g;
      }
      eng_play(0);
      return n;
    };
    int n1 = hits(4, 1, 60), s1 = at[3] - at[0];
    int n2 = hits(4, 2, 100), s2 = at[3] - at[0];
    int n16 = hits(4, 16, 16 * 43 + 20), s16 = at[3] - at[0];
    int span_ok = n1 == 4 && n2 == 4 && n16 == 4 && s2 > s1 * 3 / 2 && s16 > 16 * 43 * 3 / 5;
    static project_t q;
    eng_defaults(&q);
    q.val[PG_STEP(1) * 4 + 2] = 10.0f;    // (0.16.2's 3:4)
    for (int i = 0; i < ENG_PLOCKS; ++i) q.lock[i].lane = 0xFF;
    q.lock[0] = plock_t{2, 4, TP_RATCHET, static_cast<uint8_t>(8.0f * 255.0f / 15.0f + 0.5f)};   // (its 3:2, 9 then)
    int sp1 = 0, sp2 = 0;
    int up = eng_upgrade(&q, as_v26(&q, 25)) && eng_check(&q) &&
             eng_ratch_hits(static_cast<int>(q.val[PG_STEP(1) * 4 + 2] + 0.5f), &sp1) == 3 && sp1 == 4 &&
             eng_ratch_hits(q.lock[0].v + 1, &sp2) == 3 && sp2 == 2;
    printf("RATCH over SPAN: 4 hits over 1, 2, 16 steps: %d %d %d, first to last %d %d %d blocks; a 0.16.2 project %s\n",
           n1, n2, n16, s1, s2, s16, up ? "read (3:4 as 3 over 4, a lock's 3:2 as 3 over 2)" : "WRONG");
    if (!span_ok || !up) ++failures, printf("FAIL: RATCH over SPAN\n");
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 56. POLY's voices' level (0.16.5): a note ringing on under the next one, then
    // gone: the one playing's level glides (0.6 / sqrt(those sounding)), no step; the same in PROPHET (VINTAGE Clean).
    // Their sound made smooth (a low note held, its filter low, no envelope on it, no resonance): a step shows as a
    // spike of the wave's bend (its second difference), against its own (0.16.4: x5000 and x4100; the glide's corners,
    // a block each, x14 and x11)
    project_t* p = eng_project();
    static float l[128], r[128], wav[1400 * 128];
    auto smooth = [&](int kind) {
      eng_blank(p);
      eng_fresh();
      eng_set_model(0, eng_model_find(kind, 0));
      const float kPoly[8] = {0.0f, 0.25f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f};          // (DECAY right: held)
      const float kPro[8] = {0.5f / 13.0f, 0.25f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f};   // (VINTAGE Clean)
      for (int i = 0; i < 8; ++i) {
        int prm = PG_TRACK(0, 0) * 4 + 1 + i;
        float u = kind == MK_POLY ? kPoly[i] : kPro[i];
        p->val[prm] = eng_pdef(prm)->kind == K_BIP ? 2.0f * u - 1.0f : u;
      }
      p->algo[PG_COMP] = MB_OFF;        // (the bus off: the track alone)
    };
    auto bend = [&](int a, int b) {     // (the most the wave bends in samples a..b)
      double m = 0;
      for (int i = a; i < b; ++i) m = std::max(m, static_cast<double>(std::fabs(wav[i] - 2.0f * wav[i - 1] + wav[i - 2])));
      return m;
    };
    auto worst = [&](int kind) {
      smooth(kind);
      int voices[1400];
      for (int b = 0; b < 40; ++b) eng_render(l, r, 128);
      eng_key(0, 36, 1);
      for (int b = 0; b < 1400; ++b) {
        if (b == 30) eng_key(0, 36, 0), eng_key(0, 31, 1);   // (the second note over the first's release)
        eng_render(l, r, 128);
        for (int k = 0; k < 128; ++k) wav[b * 128 + k] = l[k];
        voices[b] = kind == MK_POLY ? poly_voices(0) : pro_voices(0);
      }
      eng_key(0, 31, 0);
      int gone = -1;
      for (int b = 60; b < 1400 && gone < 0; ++b)
        if (voices[b] < voices[b - 1]) gone = b;
      if (gone < 0 || gone > 1300) return -1.0;
      double typ = std::max(bend((gone - 40) * 128, (gone - 4) * 128), bend((gone + 20) * 128, (gone + 60) * 128));
      return bend((gone - 2) * 128, (gone + 8) * 128) / (typ + 1e-12);
    };
    double wp = worst(MK_POLY), wx = worst(MK_PROPHET);
    printf("the voices' level: when a voice ringing under the next one goes, the next one bends at most x%.2f its usual "
           "(POLY), x%.2f (PROPHET)\n", wp, wx);
    if (wp < 0 || wp > 50.0 || wx < 0 || wx > 50.0) ++failures, printf("FAIL: the voices' level\n");

    // 57. the mixer's filter taken out or in (0.16.5): from 23 Hz's high pass to flat, from flat to 16 kHz's low
    // pass, from a low pass to a high pass at once (a lock), from flat to a high pass at once: from the one to the
    // other over a block, no step (0.16.4: x7600, x8500, x200, x4500; now x3.3, x3.4, and x31, x28: the cutoff's glide
    // after, its coefficients a block each, as with the knob turned)
    auto eq_worst = [&](float from, float to) {
      smooth(MK_POLY);
      p->val[PG_EQ * 4] = from;
      eng_key(0, 36, 1);
      for (int b = 0; b < 200; ++b) {
        if (b == 120) p->val[PG_EQ * 4] = to;
        eng_render(l, r, 128);
        for (int k = 0; k < 128; ++k) wav[b * 128 + k] = l[k];
      }
      eng_key(0, 36, 0);
      double typ = std::max(bend(60 * 128, 118 * 128), bend(140 * 128, 198 * 128));
      return bend(119 * 128, 132 * 128) / (typ + 1e-12);
    };
    double e1 = eq_worst(0.035f, 0.0f), e2 = eq_worst(0.0f, -0.0237f), e3 = eq_worst(-0.3f, 0.3f), e4 = eq_worst(0.0f, 0.4f);
    printf("the mixer's filter: in or out, the wave bends at most x%.2f its usual (23 Hz HP to flat), x%.2f (flat to "
           "16 kHz LP), x%.2f (LP to HP), x%.2f (flat to HP)\n", e1, e2, e3, e4);
    if (e1 > 10.0 || e2 > 10.0 || e3 > 50.0 || e4 > 50.0) ++failures, printf("FAIL: the mixer's filter in or out\n");
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 58. SPEED turned while playing (0.16.6): the lane goes on at once at its new
    // pace (0.16..0.16.5 took its steps from the grid since PLAY: back from x2 to x1 it waited as long as it had played,
    // down to x1/2 the same); its steps on from where they were
    project_t* p = eng_project();
    static float l[128], r[128];
    eng_blank(p);
    eng_fresh();
    eng_set_model(0, eng_model_find(MK_DRUM, 0));
    p->val[PG_GLOBAL * 4 + 0] = 0.60206f;   // (120 BPM: a bar 689 blocks)
    for (int st = 0; st < 16; ++st) eng_trig_set(0, st, 1);
    int last = 0;
    auto bar = [&](int blocks) {        // the gate's rises on T1 over so many blocks
      int n = 0;
      for (int b = 0; b < blocks; ++b) {
        eng_render(l, r, 128);
        int g = (eng_gates() & 1u) != 0;
        n += g && !last;
        last = g;
      }
      return n;
    };
    const int kFrom[4] = {2, 0, 0, 4}, kTo[4] = {0, -2, -4, -2};   // x2 to x1, x1 to x1/2, x1 to x1/8, x4 to x1/2
    const int kWant[4] = {16, 8, 2, 8};
    int got[4], ok = 1;
    for (int c = 0; c < 4; ++c) {
      p->speed[0] = static_cast<int8_t>(kFrom[c]);
      last = 0;
      eng_play(1);
      bar(2 * 689);
      p->speed[0] = static_cast<int8_t>(kTo[c]);
      bar(20);                          // (the change's own step: in or out)
      got[c] = bar(689);
      eng_play(0);
      ok &= std::abs(got[c] - kWant[c]) <= 1;
    }
    // its steps on from where they were: a lane of 16 at x2 for 20 steps (on its fifth), then x1: its next the sixth
    p->speed[0] = 2;
    eng_play(1);
    int seen = -1, before = -1;
    for (int b = 0; b < 10 * 43 + 30; ++b) eng_render(l, r, 128);   // (10 sixteenths: 20 of its steps, plus a bit)
    before = eng_step_now(0);
    p->speed[0] = 0;
    for (int b = 0; b < 43; ++b) eng_render(l, r, 128);
    seen = eng_step_now(0);
    eng_play(0);
    ok &= seen == (before + 1) % 16;
    printf("SPEED turned while playing: a bar after x2 to x1 %d trigs, x1 to x1/2 %d, x1 to x1/8 %d, x4 to x1/2 %d; on "
           "from step %d to %d\n", got[0], got[1], got[2], got[3], before, seen);
    if (!ok) ++failures, printf("FAIL: SPEED turned while playing\n");
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 59. CLIMB (0.16.7): a step's trig higher each time it plays, back to its note
    // after TIMES; BOUNCE; on the ratchets' hits; both; in the scale's notes with SCALE on; a lock on one step alone;
    // a 0.16.6 project read (CLIMB off, its locks and randoms where they were)
    project_t* p = eng_project();
    static float l[128], r[128];
    auto notes = [&](int blocks, int* out, int max) {   // the notes T1's gate rises on
      int n = 0, last = 0;
      eng_play(1);
      for (int b = 0; b < blocks; ++b) {
        eng_render(l, r, 128);
        int g = (eng_gates() & 1u) != 0;
        if (g && !last && n < max) out[n++] = eng_note(0);
        last = g;
      }
      eng_play(0);
      return n;
    };
    auto setup = [&](int climb, int times, int on, int shape, int scale) {
      eng_blank(p);
      eng_fresh();
      p->val[PG_GLOBAL * 4 + 0] = 0.60206f;   // (120 BPM: a sixteenth 43 blocks)
      p->val[PG_GLOBAL * 4 + 1] = static_cast<float>(scale);
      p->val[PG_GLOBAL * 4 + 2] = 0.0f;       // (C)
      p->val[PG_STEP(0) * 4 + 3] = 4.0f;      // (a trig on its first step, every four sixteenths)
      eng_trig_set(0, 0, 1);
      p->val[PG_EUC(0) * 4 + 2] = static_cast<float>(climb);
      eng_climb_opt_set(p, 0, CL_TIMES, times);
      eng_climb_opt_set(p, 0, CL_ON, on);
      eng_climb_opt_set(p, 0, CL_SHAPE, shape);
    };
    static int got[32];
    auto same = [&](const int* want, int n, int base) {
      for (int i = 0; i < n; ++i)
        if (got[i] - base != want[i]) return 0;
      return 1;
    };
    auto show = [&](const char* what, int n, int base) {
      printf("  %s:", what);
      for (int i = 0; i < n; ++i) printf(" %+d", got[i] - base);
      printf("\n");
    };
    int ok = 1;
    setup(2, 3, CLON_TRIG, CLSH_RAMP, 0);
    notes(7 * 4 * 43 + 20, got, 32);
    const int kRamp[7] = {0, 2, 4, 0, 2, 4, 0};
    int ramp = same(kRamp, 7, 48);
    if (!ramp) show("ramp", 7, 48);
    setup(2, 3, CLON_TRIG, CLSH_BOUNCE, 0);
    notes(7 * 4 * 43 + 20, got, 32);
    const int kBounce[7] = {0, 2, 4, 2, 0, 2, 4};
    int bounce = same(kBounce, 7, 48);
    if (!bounce) show("bounce", 7, 48);
    setup(-1, 4, CLON_RATCH, CLSH_RAMP, 0);
    p->val[PG_STEP(0) * 4 + 2] = static_cast<float>(eng_ratch_join(4, 1));
    notes(2 * 4 * 43 + 20, got, 32);
    const int kRatch[8] = {0, -1, -2, -3, 0, -1, -2, -3};
    int ratch = same(kRatch, 8, 48);
    if (!ratch) show("ratch", 8, 48);
    setup(1, 3, CLON_BOTH, CLSH_RAMP, 0);
    p->val[PG_STEP(0) * 4 + 2] = static_cast<float>(eng_ratch_join(2, 1));
    notes(3 * 4 * 43 + 20, got, 32);
    const int kBoth[6] = {0, 1, 2, 0, 1, 2};
    int both = same(kBoth, 6, 48);
    if (!both) show("both", 6, 48);
    setup(1, 4, CLON_TRIG, CLSH_RAMP, 2);   // (C minor: C D Eb F)
    notes(4 * 4 * 43 + 20, got, 32);
    const int kScale[4] = {0, 2, 3, 5};
    int scale = same(kScale, 4, 48);
    if (!scale) show("scale", 4, 48);
    setup(0, 2, CLON_TRIG, CLSH_RAMP, 0);   // (CLIMB 0 on the track; step 3 locked to +5: it alone climbs)
    eng_trig_set(0, 2, 1);
    eng_lock_set(0, 2, TP_CLIMB, 5.0f);
    notes(3 * 4 * 43 + 20, got, 32);
    const int kLock[6] = {0, 0, 0, 5, 0, 0};
    int lock = same(kLock, 6, 48);
    if (!lock) show("lock", 6, 48);
    // a 0.16.6 project: its slot's junk not read as CLIMB, its randoms and locks where they were
    static project_t q;
    eng_defaults(&q);
    q.val[PG_EUC(1) * 4 + 2] = 7.0f;
    q.rnd[2][TP_CURVE] = 44;
    q.rseed[3][TP_GDROP] = 9;
    for (int i = 0; i < ENG_PLOCKS; ++i) q.lock[i].lane = 0xFF;
    q.lock[5] = plock_t{1, 9, TP_NOTE, 77};
    int bytes = as_v26(&q, 26);
    reinterpret_cast<V26*>(&q)->lock[383] = plock_t{2, 1, TP_PROB, 33};   // (in a slot 0.16.7 has no more)
    int up = eng_upgrade(&q, bytes) && eng_check(&q) && q.val[PG_EUC(1) * 4 + 2] == 0.0f && q.rnd[2][TP_CURVE] == 44 &&
             q.rseed[3][TP_GDROP] == 9 && q.rnd[2][TP_CLIMB] == 0 && eng_climb_opt(1, CL_TIMES) == 4;
    int found = 0;
    for (int i = 0; i < ENG_PLOCKS; ++i)
      found += (q.lock[i].lane == 1 && q.lock[i].step == 9 && q.lock[i].tp == TP_NOTE && q.lock[i].v == 77) +
               (q.lock[i].lane == 2 && q.lock[i].step == 1 && q.lock[i].tp == TP_PROB && q.lock[i].v == 33);
    up &= found == 2;
    ok = ramp && bounce && ratch && both && scale && lock && up;
    printf("CLIMB: +2 three times %s, bouncing %s; -1 on four ratchets %s; both %s; in C minor %s; a lock alone %s; a "
           "0.16.6 project %s\n", ramp ? "ok" : "WRONG", bounce ? "ok" : "WRONG", ratch ? "ok" : "WRONG",
           both ? "ok" : "WRONG", scale ? "ok" : "WRONG", lock ? "ok" : "WRONG", up ? "read" : "WRONG");
    if (!ok) ++failures, printf("FAIL: CLIMB\n");
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 60. the random dose on the ratchets (0.16.8, 0.16.9): none added below 50 %,
    // the sequence's left alone below 30 %; at 60 % trigs without ratchets get some now and then, trigs with some lose
    // them now and then; at 90 % some rolls climb
    project_t* p = eng_project();
    static float l[128], r[128];
    static int per[80], notes[80][8];
    auto run = [&](int dose, int ratch) {   // 4 bars of 16 trigs on T1: the rises in each sixteenth, their notes
      eng_blank(p);
      eng_fresh();
      eng_set_model(0, eng_model_find(MK_DRUM, 0));
      p->val[PG_GLOBAL * 4 + 0] = 0.60206f;   // (120 BPM)
      p->val[PG_GLOBAL * 4 + 1] = 0.0f;
      for (int st = 0; st < 16; ++st) eng_trig_set(0, st, 1);
      p->val[PG_STEP(0) * 4 + 2] = static_cast<float>(ratch);
      p->dice = static_cast<uint8_t>(dose);
      for (int i = 0; i < 80; ++i) per[i] = 0;
      int n = 0, last = 0;
      eng_play(1);
      for (int b = 0; b < 4 * 689 - 4; ++b) {
        eng_render(l, r, 128);
        int g = (eng_gates() & 1u) != 0;
        if (g && !last) {
          int six = static_cast<int>(static_cast<double>(b + 1) / 43.066);   // (the block the trig falls in)
          if (six < 80) {
            if (per[six] < 8) notes[six][per[six]] = eng_note(0);
            ++per[six];
          }
          ++n;
        }
        last = g;
      }
      eng_play(0);
      return n;
    };
    int low = run(45, 1), base3 = run(25, 3);
    int add = run(60, 1), more = 0;
    for (int i = 0; i < 64; ++i) more += per[i] >= 2;
    int drop = run(60, 3), singles = 0;
    for (int i = 0; i < 64; ++i) singles += per[i] == 1;
    run(90, 3);
    int climbs = 0;
    for (int i = 0; i < 64; ++i) {
      int k = per[i] > 8 ? 8 : per[i];
      for (int j = 1; j < k; ++j)
        if (notes[i][j] != notes[i][0]) { ++climbs; break; }
    }
    int ok = low == 64 && base3 == 192 && add > 64 && more >= 3 && more <= 24 && singles >= 2 && drop != 192 &&
             climbs >= 1;
    printf("the dose on the ratchets: at 45 %% %d rises over 64 trigs; at 60 %% %d (%d sixteenths with ratchets added); "
           "x3 at 25 %% %d, at 60 %% %d (%d with theirs gone); at 90 %% %d rolls climbing\n", low, add, more, base3, drop,
           singles, climbs);
    if (!ok) ++failures, printf("FAIL: the dose on the ratchets\n");
    p->dice = 0;
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 61. the random dose leaving trigs out (0.16.10): none below 50 %; at 70 %
    // some sixteenths silent, never the bar's first
    project_t* p = eng_project();
    static float l[128], r[128];
    static int per[80];
    auto run = [&](int dose) {          // 4 bars of 16 trigs on T1: the sixteenths with no rise
      eng_blank(p);
      eng_fresh();
      eng_set_model(0, eng_model_find(MK_DRUM, 0));
      p->val[PG_GLOBAL * 4 + 0] = 0.60206f;
      for (int st = 0; st < 16; ++st) eng_trig_set(0, st, 1);
      p->dice = static_cast<uint8_t>(dose);
      for (int i = 0; i < 80; ++i) per[i] = 0;
      int last = 0;
      eng_play(1);
      for (int b = 0; b < 4 * 689 - 4; ++b) {
        eng_render(l, r, 128);
        int g = (eng_gates() & 1u) != 0;
        if (g && !last) {
          int six = static_cast<int>(static_cast<double>(b + 1) / 43.066);   // (the block the trig falls in)
          if (six < 80) ++per[six];
        }
        last = g;
      }
      eng_play(0);
      int silent = 0;
      for (int i = 0; i < 64; ++i) silent += !per[i];
      return silent;
    };
    int at45 = run(45), at70 = run(70), firsts = 1;
    for (int b = 0; b < 4; ++b) firsts &= per[b * 16] > 0;
    int at100 = run(100);
    for (int b = 0; b < 4; ++b) firsts &= per[b * 16] > 0;
    printf("the dose leaving trigs out: silent sixteenths of 64 at 45 %% %d, at 70 %% %d, at 100 %% %d; the bars' first "
           "steps %s\n", at45, at70, at100, firsts ? "all played" : "LEFT OUT");
    if (at45 || at70 < 2 || at70 > 20 || at100 > 28 || !firsts) ++failures, printf("FAIL: the dose leaving trigs out\n");
    p->dice = 0;
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 62. Endless played by a key held, the sequencer stopped (0.16.11): its notes
    // at its rate from its first, transposed by the key; nothing once let go; nothing without a key
    project_t* p = eng_project();
    static float l[128], r[128];
    eng_blank(p);
    eng_fresh();
    eng_set_model(0, eng_model_find(MK_DRUM, 0));
    p->val[PG_GLOBAL * 4 + 0] = 0.60206f;   // (120 BPM: a sixteenth 43 blocks)
    p->val[PG_GLOBAL * 4 + 1] = 0.0f;
    eng_endless_add(0, 60);
    eng_endless_add(0, 63);
    eng_endless_add(0, 67);
    eng_play(0);
    int got[16], n = 0, last = 0;
    auto listen = [&](int blocks) {
      int c = 0;
      for (int b = 0; b < blocks; ++b) {
        eng_render(l, r, 128);
        int g = (eng_gates() & 1u) != 0;
        if (g && !last) {
          if (n < 16) got[n++] = eng_note(0);
          ++c;
        }
        last = g;
      }
      return c;
    };
    int idle = listen(200);              // (stopped, no key: nothing)
    eng_endless_key(0, 65);              // (a key: F, its first note C: +5)
    eng_endless_free(0, 1);
    int held = listen(6 * 43 + 20);
    eng_endless_free(-1, 0);
    listen(30);
    int after = listen(200);
    const int kWant[6] = {65, 68, 72, 65, 68, 72};
    int ok = !idle && held == 7 && !after, pos = eng_endless_pos(0);
    for (int i = 0; i < 6 && ok; ++i) ok &= got[i] == kWant[i];
    printf("Endless by a key, stopped: %d rises without a key; held %d (%d %d %d %d %d %d); let go %d after%s\n", idle,
           held, got[0], got[1], got[2], got[3], got[4], got[5], after, pos < 0 ? "" : ", STILL PLAYING");
    if (!ok || pos >= 0) ++failures, printf("FAIL: Endless by a key, stopped\n");
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 63. the harmony (0.16.12): PADS' progressions and colours of then where a
    // project of then finds them; the new chords (off the scale, suspended, the cliché's line, POWER, the eleventh);
    // the phrygian dominant; FLIP minor one bar, dorian the next
    static const char* const kOldProg[17] = {"Manual", "Tintin", "Mirror", "Pachelbel", "Folia", "Lament", "Fifths",
                                             "Gymnopedie", "Prelude", "Plagal", "Dorian", "Dub", "Hymn", "Phrygian",
                                             "Epic", "Lento", "Static"};
    static const char* const kOldColor[4] = {"Triad", "7th", "9th", "11th"};
    char nm[24];
    int kept = 1;
    for (int i = 0; i < 17; ++i) {
      pads_knob_text(0, (static_cast<float>(i) + 0.5f) / 17.0f, nm, sizeof nm);
      if (strcmp(nm, kOldProg[i])) kept = 0, printf("  PROG %d of then: now %s\n", i, nm);
    }
    for (int i = 0; i < 4; ++i) {
      pads_knob_text(1, (static_cast<float>(i) + 0.5f) / 4.0f, nm, sizeof nm);
      if (strcmp(nm, kOldColor[i])) kept = 0, printf("  COLOR %d of then: now %s\n", i, nm);
    }
    auto prog_at = [&](const char* name) {   // (PROG's middle of that progression's share)
      for (int i = 0; i < pads_progs(); ++i) {
        float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(pads_progs());
        pads_knob_text(0, u, nm, sizeof nm);
        if (!strcmp(nm, name)) return pads_prog(u);
      }
      return -1;
    };
    auto color_at = [&](const char* name) {
      for (int i = 0; i < 9; ++i) {
        float u = (static_cast<float>(i) + 0.5f) / 9.0f;
        pads_knob_text(1, u, nm, sizeof nm);
        if (!strcmp(nm, name)) return u;
      }
      return -1.0f;
    };
    auto chord_is = [&](int prog, int pos, const char* color, const int* want, int wn) {
      int out[8], raised, n = pads_chord(prog, pos, 0, 0x5ADu, color_at(color), out, &raised);   // (A minor's: 0 A)
      if (n != wn) return 0;
      for (int i = 0; i < n; ++i)
        if (out[i] != want[i]) return 0;
      return 1;
    };
    const int kFlat2[3] = {1, 5, 8}, kCliche[4] = {0, 3, 7, 11}, kSus2[3] = {0, 2, 7}, kPower[3] = {0, 7, 12};
    const int k11[5] = {0, 7, 10, 14, 17}, kTritone[3] = {6, 10, 13}, kHover[3] = {0, 2, 7};
    int chords = chord_is(prog_at("Flat II"), 1, "Triad", kFlat2, 3) && chord_is(prog_at("Descent"), 1, "Triad", kCliche, 4) &&
                 chord_is(0, 0, "Sus2", kSus2, 3) && chord_is(0, 0, "Power", kPower, 3) && chord_is(0, 0, "11th", k11, 5) &&
                 chord_is(prog_at("Tritone"), 1, "Triad", kTritone, 3) && chord_is(prog_at("Hover"), 0, "Triad", kHover, 3);
    project_t* p = eng_project();
    static float l[128], r[128];
    eng_blank(p);
    eng_fresh();
    p->val[PG_GLOBAL * 4 + 0] = 0.60206f;
    p->val[PG_GLOBAL * 4 + 2] = 0.0f;   // (C)
    p->val[PG_GLOBAL * 4 + 1] = 16.0f;  // (the phrygian dominant: C Db E F G Ab Bb)
    eng_render(l, r, 128);
    eng_param_text(PG_GLOBAL * 4 + 1, 16.0f, nm, sizeof nm);
    int pd = !strcmp(nm, "Phryg. dom") && eng_quantize(62) == 61 && eng_quantize(64) == 64 && eng_quantize(66) == 65;
    p->val[PG_GLOBAL * 4 + 1] = 17.0f;  // FLIP
    eng_render(l, r, 128);
    int bar1 = eng_quantize(69);        // (stopped: minor, A to Ab)
    eng_play(1);
    for (int b = 0; b < 689 + 200; ++b) eng_render(l, r, 128);   // (into the second bar: dorian, A)
    int bar2 = eng_quantize(69);
    eng_play(0);
    int flip = bar1 == 68 && bar2 == 69;
    // the chords' names, from their notes: Descent in A minor, a trig a beat on PADS (T4)
    eng_blank(p);
    eng_fresh();
    p->val[PG_GLOBAL * 4 + 0] = 0.60206f;
    p->val[PG_GLOBAL * 4 + 1] = 2.0f;   // (minor)
    p->val[PG_GLOBAL * 4 + 2] = 9.0f;   // (A)
    eng_set_model(3, eng_model_find(MK_PADS, 0));
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[3][c].depth = 0;
    for (int i = 0; i < pads_progs(); ++i) {
      float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(pads_progs());
      pads_knob_text(0, u, nm, sizeof nm);
      if (!strcmp(nm, "Descent")) p->val[PG_TRACK(3, 0) * 4 + 1] = u;
    }
    for (int st = 0; st < 16; ++st) eng_trig_set(3, st, st % 4 == 0);
    static char names[4][16];
    eng_play(1);
    for (int b = 0, seen = -1; b < 689; ++b) {
      eng_render(l, r, 128);
      int pos = eng_pads_pos();
      if (pos >= 0 && pos < 4 && pos != seen && b % 43 == 20) {
        char line[48];
        eng_pads_line(line, sizeof line);
        const char* sp = line;
        for (const char* q = line; *q; ++q)
          if (*q == ' ') sp = q + 1;
        strncpy(names[pos], sp, 15);
        seen = pos;
      }
    }
    eng_play(0);
    int named = !strcmp(names[0], "Am") && !strcmp(names[1], "AmMaj7") && !strcmp(names[2], "Am7") && !strcmp(names[3], "Am6");
    if (!named) printf("  Descent named: %s %s %s %s\n", names[0], names[1], names[2], names[3]);
    flip &= named;
    printf("the harmony: PROG and COLOR of then %s; the new chords %s; the phrygian dominant %s; FLIP A as %d then %d\n",
           kept ? "where they were" : "MOVED", chords ? "ok" : "WRONG", pd ? "ok" : "WRONG", bar1, bar2);
    if (!kept || !chords || !pd || !flip) ++failures, printf("FAIL: the harmony\n");
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 64. Endless' ARP (0.16.13): the keys held, by pitch or as played, over
    // octaves; +2-1 and CONVERGE; stopped by a key, let go: silent, latched: on; PADS' chord, playing
    project_t* p = eng_project();
    static float l[128], r[128];
    int got[24], n = 0, last = 0;
    auto listen = [&](int blocks) {
      int c = 0;
      for (int b = 0; b < blocks; ++b) {
        eng_render(l, r, 128);
        int g = (eng_gates() & 1u) != 0;
        if (g && !last) {
          if (n < 24) got[n++] = eng_note(0);
          ++c;
        }
        last = g;
      }
      return c;
    };
    auto setup = [&](int order, int oct, int dir, int latch) {
      eng_blank(p);
      eng_fresh();
      eng_set_model(0, eng_model_find(MK_DRUM, 0));
      p->val[PG_GLOBAL * 4 + 0] = 0.60206f;
      p->val[PG_GLOBAL * 4 + 1] = 0.0f;
      p->val[PG_ARP(0) * 4 + 2] = static_cast<float>(dir);
      eng_arp_opt_set(p, 0, AO_MODE, AM_KEYS);
      eng_arp_opt_set(p, 0, AO_OCT, oct);
      eng_arp_opt_set(p, 0, AO_ORDER, order);
      eng_arp_opt_set(p, 0, AO_LATCH, latch);
      eng_arp_release_all();
      eng_play(0);
      eng_render(l, r, 128);
      n = last = 0;
      eng_arp_key(0, 64, 1), eng_arp_key(0, 60, 1), eng_arp_key(0, 67, 1);
      eng_endless_free(0, 1);
    };
    auto same = [&](const int* want, int k) {
      for (int i = 0; i < k; ++i)
        if (got[i] != want[i]) return 0;
      return n >= k ? 1 : 0;
    };
    setup(0, 2, 0, 0);
    listen(6 * 43 + 10);
    const int kPitch[6] = {60, 64, 67, 72, 76, 79};
    int pitch = same(kPitch, 6);
    setup(1, 2, 0, 0);
    listen(6 * 43 + 10);
    const int kPlayed[6] = {64, 60, 67, 76, 72, 79};
    int played = same(kPlayed, 6);
    setup(0, 1, 4, 0);
    listen(6 * 43 + 10);
    const int kTwoOne[6] = {60, 67, 64, 60, 67, 64};
    int twoone = same(kTwoOne, 6);
    setup(0, 2, 5, 0);
    listen(6 * 43 + 10);
    const int kConv[6] = {60, 79, 64, 76, 67, 72};
    int conv = same(kConv, 6);
    setup(0, 1, 0, 0);                  // (let go: silent)
    listen(3 * 43);
    eng_arp_key(0, 64, 0), eng_arp_key(0, 60, 0), eng_arp_key(0, 67, 0);
    eng_arp_release_all();
    eng_endless_free(-1, 0);
    listen(30);
    int gone = listen(200) == 0;
    setup(0, 1, 0, 1);                  // (latched: on once let go)
    listen(2 * 43);
    eng_arp_key(0, 64, 0), eng_arp_key(0, 60, 0), eng_arp_key(0, 67, 0);
    eng_arp_release_all();
    eng_endless_free(-1, 0);
    int latched = listen(4 * 43 + 10) >= 3;
    eng_arp_opt_set(p, 0, AO_LATCH, 0);  // (LATCH off: let go)
    listen(30);
    int unlatched = listen(200) == 0;
    // PADS' chord, playing: Static in C major (C E G) on T4, T1 walking it
    eng_blank(p);
    eng_fresh();
    p->val[PG_GLOBAL * 4 + 0] = 0.60206f;
    p->val[PG_GLOBAL * 4 + 1] = 1.0f;
    p->val[PG_GLOBAL * 4 + 2] = 0.0f;
    eng_set_model(0, eng_model_find(MK_DRUM, 0));
    eng_set_model(3, eng_model_find(MK_PADS, 0));
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[3][c].depth = 0;
    p->val[PG_TRACK(3, 0) * 4 + 1] = 26.5f / 27.0f;   // (Static, the last)
    eng_trig_set(3, 0, 1);
    eng_arp_opt_set(p, 0, AO_MODE, AM_CHORD);
    eng_arp_opt_set(p, 0, AO_OCT, 1);
    n = last = 0;
    eng_play(1);
    listen(6 * 43 + 20);
    eng_play(0);
    const int kChord[6] = {48, 52, 55, 48, 52, 55};
    int chord = same(kChord, 6);
    int ok = pitch && played && twoone && conv && gone && latched && unlatched && chord;
    printf("the ARP: by pitch %s, as played %s, +2-1 %s, CONVERGE %s; let go %s, latched %s, unlatched %s; PADS' chord %s"
           " (%d %d %d %d)\n", pitch ? "ok" : "WRONG", played ? "ok" : "WRONG", twoone ? "ok" : "WRONG",
           conv ? "ok" : "WRONG", gone ? "silent" : "STILL", latched ? "on" : "OFF", unlatched ? "silent" : "STILL",
           chord ? "ok" : "WRONG", got[0], got[1], got[2], got[3]);
    if (!ok) ++failures, printf("FAIL: the ARP\n");
    eng_arp_opt_set(p, 0, AO_MODE, AM_LOOP);
    eng_defaults(p);
    eng_fresh();
  }

  {                                     // 65. SQ-1 (0.16.14): in CHIPTUNE's place (FORMANT gone); its knobs, its kit;
    // every program sounding, finite, bounded, silent after its release; SWEEP moving its Transwave (a pluck's
    // brighter at the trig, a pad's later), INDEX its place; the levels of its programs close; 300 settings bounded;
    // through the engine one track after another machine and back (Plaits' memory lent and given back: SPEECH,
    // MODAL sounding after it); a project of 0.16.13 (version 27) and one of 0.16.6 (26) renumbered, CHIPTUNE and
    // FORMANT SQ-1 from its kit (their locks on its knobs gone); a preset of before (its version 1) the same
    project_t* p = eng_project();
    const uint32_t n = 128;
    static float l[128], r[128];
    eng_play(0);
    eng_defaults(p);
    eng_fresh();
    const int sq_m = eng_model_find(MK_SQ1, 0);
    int ok = sq_m == 15 && ENG_MODELS == 22 && eng_model_find(MK_PLAITS, 7) < 0 && eng_model_find(MK_PLAITS, 11) < 0 &&
             eng_model_bank(sq_m) == 2 && eng_model_own(sq_m) && sq_progs() == 24;
    eng_set_model(0, sq_m);
    float kd[8];
    sq_defaults(kd);
    for (int i = 0; i < 8; ++i) {
      int param = PG_TRACK(0, 0) * 4 + 1 + i;
      float u = eng_pdef(param)->kind == K_BIP ? (p->val[param] + 1.0f) * 0.5f : p->val[param];
      if (std::fabs(u - kd[i]) > 1e-6f || std::strcmp(eng_param_name(param), sq_knob_name(i)) != 0) ok = 0;
    }
    char tx[24];
    const float kMetal = 6.5f / 24.0f, kPiano = 3.5f / 24.0f;
    struct { int k; float u, w; const char* want; } kTx[] = {
        {0, 0.0f, 0.0f, "Res Bass"}, {0, kPiano, 0.0f, "E.Piano"}, {0, 1.0f, 0.0f, "Pluck"}, {2, 0.5f, 0.0f, "0%"},
        {2, 1.0f, 0.0f, "+100%"}, {2, 0.0f, 0.0f, "-100%"}, {4, 0.6f, 0.0f, "Open"}, {4, 0.0f, 0.0f, "100% Dark"},
        {4, 1.0f, 0.0f, "100% Thin"}, {5, 0.0f, 0.0f, "Perc"}, {5, 1.0f, 0.0f, "Pad"}, {6, 0.0f, 0.0f, "Off"},
        {6, 0.5f, 0.0f, "100% 2nd"}, {6, 1.0f, 0.0f, "100% 3rd"}, {7, 0.0f, 0.0f, "Dry"}, {7, 0.25f, 0.0f, "50% Chorus"},
        {7, 1.0f, kMetal, "100% +Comb"}, {7, 0.75f, kPiano, "50% +Phaser"}, {7, 1.0f, 0.0f, "100% +Drive"},
        {3, 0.25f, 0.0f, "25%"}};
    for (const auto& x : kTx) {
      sq_knob_text(x.k, x.u, x.w, tx, sizeof tx);
      if (std::strcmp(tx, x.want)) ok = 0, printf("SQ-1: knob %d at %.3f written %s, %s expected\n", x.k, x.u, tx, x.want);
    }
    if (std::strcmp(sq_family(kMetal), "Metallic") || std::strcmp(sq_family(1.0f), "Percussive")) ok = 0;
    // every program: C3 held half a second, let go
    static float ab[44100 * 4];
    auto play = [&](const float* kk, float note, int held_blocks, int total_blocks) {
      sq_reset(0);
      sq_set(0, note, kk);
      sq_gate(0, 1);
      for (int b = 0; b < total_blocks; ++b) {
        if (b == held_blocks) sq_gate(0, 0);
        sq_render(0, &ab[b * n], n);
      }
    };
    auto bright = [&](uint32_t from, uint32_t len) {   // (its brightness: the slope's energy against the level's)
      double ex = 0.0, ed = 0.0;
      for (uint32_t i = from; i < from + len; ++i) ex += ab[i] * ab[i], ed += (ab[i] - ab[i - 1]) * (ab[i] - ab[i - 1]);
      return std::sqrt(ed / (ex + 1e-12));
    };
    auto rms = [&](uint32_t from, uint32_t len) {
      double e = 0.0;
      for (uint32_t i = from; i < from + len; ++i) e += ab[i] * ab[i];
      return std::sqrt(e / len);
    };
    int sounds = 0, finite = 1, quiet = 1, moves = 0;
    float pk_all = 0.0f;
    double lo = 1e9, hi = 0.0;
    for (int pr = 0; pr < 24; ++pr) {
      float k[8];
      sq_defaults(k);
      k[0] = (pr + 0.5f) / 24.0f;
      k[1] = 0.25f, k[2] = 0.85f, k[3] = 0.0f, k[5] = 0.45f, k[6] = 0.3f, k[7] = 0.0f;
      play(k, 48.0f, 172, 1300);         // (half a second held, then three and a half)
      float pk = 0.0f;
      for (uint32_t i = 0; i < 1300 * n; ++i) {
        if (!std::isfinite(ab[i])) finite = 0;
        pk = std::max(pk, std::fabs(ab[i]));
      }
      double tail = 0.0;
      for (uint32_t i = 1200 * n; i < 1300 * n; ++i) tail = std::max(tail, (double)std::fabs(ab[i]));
      double lv = rms(2205, 8820), b0 = bright(441, 1764), b1 = bright(13230, 4410);
      sounds += lv > 0.01 && pk < 1.0f;
      quiet &= tail < 1e-4;
      if (tail >= 1e-4) printf("SQ-1: program %d still %.5f after its release\n", pr, tail);
      lo = std::min(lo, lv), hi = std::max(hi, lv);
      pk_all = std::max(pk_all, pk);
      static const int kRise[24] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0};
      double ratio = kRise[pr] ? b1 / b0 : b0 / b1;
      moves += ratio > 1.08;
      if (getenv("SQ1_VERBOSE"))
        printf("  SQ-1 %2d %-9s rms %.3f peak %.2f, brightness %.2f then %.2f (x%.2f)\n", pr, sq_prog_name(k[0]), lv, pk,
               b0, b1, ratio);
    }
    // INDEX: Res Bass, no sweep, at its start and at its end
    float k[8];
    sq_defaults(k);
    k[0] = 0.5f / 24.0f, k[2] = 0.5f, k[3] = 0.0f, k[6] = 0.0f, k[7] = 0.0f, k[5] = 0.6f, k[4] = 0.6f;
    k[1] = 0.0f;
    play(k, 48.0f, 400, 400);
    double i0 = bright(8820, 8820);
    k[1] = 1.0f;
    play(k, 48.0f, 400, 400);
    double i1 = bright(8820, 8820);
    // a Transwave steps: the cycle read changes only at its wrap (the place on the screen one of 32)
    float cyc[48];
    int of = 0, at = sq_cycle(0, cyc, 48, &of);
    int steps = at >= 0 && at % 32 == 31 && of == 32;
    // anything: 300 settings
    uint32_t sd = 9876u;
    auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return static_cast<float>(sd >> 8) / 16777216.0f; };
    float pk_any = 0.0f;
    int nan_any = 0;
    sq_reset(0);
    for (int s2 = 0; s2 < 300; ++s2) {
      for (int i = 0; i < 8; ++i) k[i] = rnd() < 0.15f ? (rnd() < 0.5f ? 0.0f : 1.0f) : rnd();
      sq_set(0, 24.0f + 84.0f * rnd(), k);
      sq_gate(0, 1);
      for (int b = 0; b < 60; ++b) {
        if (b == 30) sq_gate(0, 0);
        sq_render(0, ab, 32);
        for (int i = 0; i < 32; ++i) {
          if (!std::isfinite(ab[i])) nan_any = 1;
          pk_any = std::max(pk_any, std::fabs(ab[i]));
        }
      }
    }
    // through the engine: SQ-1, SPEECH, SQ-1, MODAL, SQ-1 on track 1 (Plaits' memory lent and back)
    eng_defaults(p);
    eng_fresh();
    for (int st = 0; st < ENG_STEPS; ++st) eng_trig_set(0, st, st % 2 == 0);
    const int kSeq[5] = {sq_m, eng_model_find(MK_PLAITS, 15), sq_m, eng_model_find(MK_PLAITS, 20), sq_m};
    eng_play(1);
    int shared_ok = 1;
    for (int m = 0; m < 5; ++m) {
      eng_set_model(0, kSeq[m]);
      float spk = 0.0f;
      for (int b = 0; b < 300; ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i])) shared_ok = 0;
          spk = std::max(spk, std::fabs(l[i]));
        }
      }
      if (spk < 0.02f || spk > 1.5f) shared_ok = 0, printf("SQ-1: through the engine, model %d: peak %.3f\n", kSeq[m], spk);
    }
    eng_play(0);
    // a project of 0.16.13 (version 27): CHIPTUNE (15) with a lock on HARMONICS and one on NOTE, FORMANT (16),
    // SPEECH (17), MODAL (22); one of 0.16.6 (26): SWARM (18), POLY (13), STRING (21), CHIPTUNE (15)
    static project_t q;
    eng_defaults(&q);
    q.version = 27;
    const uint8_t kOld27[4] = {15, 16, 17, 22};
    for (int t = 0; t < ENG_TRACKS; ++t) q.algo[PG_TRACK(t, 0)] = kOld27[t];
    for (int i = 0; i < ENG_PLOCKS; ++i) q.lock[i].lane = 0xFF;
    q.lock[0] = plock_t{0, 3, 1, 200};
    q.lock[1] = plock_t{0, 4, TP_NOTE, 100};
    q.val[PG_TRACK(0, 0) * 4 + 1] = 0.9f;   // (HARMONICS: SQ-1's WAVE then its kit's)
    int v27 = eng_upgrade(&q, sizeof q) && eng_check(&q) && q.algo[PG_TRACK(0, 0)] == sq_m &&
              q.algo[PG_TRACK(1, 0)] == sq_m && q.algo[PG_TRACK(2, 0)] == eng_model_find(MK_PLAITS, 15) &&
              q.algo[PG_TRACK(3, 0)] == eng_model_find(MK_PLAITS, 20) && q.lock[0].lane == 0xFF &&
              q.lock[1].lane == 0 && q.lock[1].tp == TP_NOTE && std::fabs(q.val[PG_TRACK(0, 0) * 4 + 1] - kd[0]) < 1e-6f;
    eng_defaults(&q);
    const uint8_t kOld26[4] = {18, 13, 21, 15};
    for (int t = 0; t < ENG_TRACKS; ++t) q.algo[PG_TRACK(t, 0)] = kOld26[t];
    int bytes26 = as_v26(&q, 26);
    int v26 = eng_upgrade(&q, bytes26) && eng_check(&q) && q.algo[PG_TRACK(0, 0)] == eng_model_find(MK_PLAITS, 16) &&
              q.algo[PG_TRACK(1, 0)] == eng_model_find(MK_POLY, 0) &&
              q.algo[PG_TRACK(2, 0)] == eng_model_find(MK_PLAITS, 19) && q.algo[PG_TRACK(3, 0)] == sq_m;
    // a preset of before: CHIPTUNE's (15) SQ-1's kit, STRING's (21) STRING
    eng_defaults(p);
    eng_fresh();
    eng_sound_t a;
    eng_set_model(1, eng_model_find(MK_PLAITS, 19));
    eng_sound_get(1, &a);
    a.version = 1;
    a.algo[0] = 15;
    a.val[1] = 0.95f;
    int snd = eng_sound_check(&a, sizeof a) && eng_sound_set(2, &a) == 0 && p->algo[PG_TRACK(2, 0)] == sq_m &&
              std::fabs(p->val[PG_TRACK(2, 0) * 4 + 1] - kd[0]) < 1e-6f;
    a.algo[0] = 21;
    snd &= eng_sound_set(3, &a) == 0 && p->algo[PG_TRACK(3, 0)] == eng_model_find(MK_PLAITS, 19);
    eng_defaults(p);
    eng_fresh();
    printf("SQ-1: knobs %s; its 24 programs sounding %d, finite %s, after their release %s, peak %.2f, levels %.3f .. "
           "%.3f; SWEEP moving %d of 24; INDEX brighter x%.2f; its Transwave stepping %s; 300 settings: peak %.2f%s; "
           "through the engine %s; versions 27, 26 %s %s; a preset of before %s\n",
           ok ? "ok" : "WRONG", sounds, finite ? "yes" : "NO", quiet ? "silent" : "STILL", pk_all, lo, hi, moves,
           i1 / i0, steps ? "ok" : "WRONG", pk_any, nan_any ? ", NaN" : "", shared_ok ? "ok" : "WRONG",
           v27 ? "read" : "WRONG", v26 ? "read" : "WRONG", snd ? "read" : "WRONG");
    if (!ok || sounds != 24 || !finite || !quiet || pk_all > 1.0f || hi / lo > 2.0 || moves < 22 || i1 / i0 < 2.0 ||
        !steps || pk_any > 1.2f || nan_any || !shared_ok || !v27 || !v26 || !snd)
      ++failures, printf("FAIL: SQ-1\n");
  }

  {                                     // 66. the keys (0.16.15): a voice of one plays the newest key held, back to
    // the one before when it is let go (a new trig); POLY in MODE Poly, PROPHET with CHORD Off and SQ-1 play the keys
    // held, a voice each, the one let go falling (DECAY at Hold), the steps silent meanwhile; MODE Mono one voice; a
    // trig's length over the steps after it (its own lock), the gate that long
    project_t* p = eng_project();
    static float l[128], r[128];
    auto blocks = [&](int nb) { for (int b = 0; b < nb; ++b) eng_render(l, r, 128); };
    auto knob = [&](int t, int i, float u) {
      int param = PG_TRACK(t, 0) * 4 + 1 + i;
      p->val[param] = eng_pdef(param)->kind == K_BIP ? 2.0f * u - 1.0f : u;
    };
    eng_play(0);
    eng_defaults(p);
    eng_fresh();
    for (int l2 = 0; l2 < ENG_LANES; ++l2)
      for (int st = 0; st < ENG_STEPS; ++st) eng_trig_set(l2, st, 0);
    // a voice of one (the FM): 60, 64 over it, 64 let go: 60 again, a new trig
    eng_set_model(0, eng_model_find(MK_FM, 0));
    eng_key(0, 60, 1);
    blocks(10);
    int n1 = eng_note(0);
    eng_key(0, 64, 1);
    blocks(10);
    int n2 = eng_note(0);
    uint32_t r0 = eng_rises(0);
    eng_key(0, 64, 0);
    blocks(10);
    int n3 = eng_note(0), back = eng_rises(0) > r0 && (eng_gates() & 1u);
    eng_key(0, 60, 0);
    blocks(20);
    int mono = n2 > n1 && n3 == n1 && back && !(eng_gates() & 1u);   // (64 in the project's scale)
    // the keys' chords: POLY (Poly, DECAY Hold), PROPHET (CHORD Off, DECAY Hold), SQ-1
    auto silent = [&]() {               // (a voice resting keeps its count: the track's sound tells)
      float pk = 0.0f;
      for (int b = 0; b < 20; ++b) {
        eng_render(l, r, 128);
        pk = std::max(pk, eng_meter(0));
      }
      return pk < 1e-3f;
    };
    auto chord = [&](int kind, int* held3, int* after1, int* none) {
      eng_set_model(0, eng_model_find(kind, 0));
      if (kind != MK_SQ1) knob(0, 3, 1.0f);
      eng_key(0, 60, 1);
      blocks(2);
      eng_key(0, 64, 1);
      blocks(2);
      eng_key(0, 67, 1);
      blocks(20);
      auto voices = [&]() { return kind == MK_POLY ? poly_voices(0) : kind == MK_PROPHET ? pro_voices(0) : sq_voices(0); };
      *held3 = voices();
      eng_key(0, 64, 0);
      blocks(700);
      *after1 = voices();
      eng_key(0, 60, 0);
      eng_key(0, 67, 0);
      blocks(1500);
      *none = silent() ? 0 : voices();
    };
    int ph3, ph1, ph0, xh3, xh1, xh0, sh3, sh1, sh0;
    chord(MK_POLY, &ph3, &ph1, &ph0);
    chord(MK_PROPHET, &xh3, &xh1, &xh0);
    chord(MK_SQ1, &sh3, &sh1, &sh0);
    int chords = ph3 == 3 && ph1 == 2 && ph0 == 0 && xh3 == 3 && xh1 == 2 && xh0 == 0 && sh3 == 3 && sh1 == 2 && sh0 == 0;
    // POLY in MODE Mono: one voice, the newest key
    eng_set_model(0, eng_model_find(MK_POLY, 0));
    knob(0, 3, 1.0f);
    knob(0, 6, 0.25f);
    eng_key(0, 60, 1);
    blocks(2);
    const int p60 = eng_note(0);
    eng_key(0, 64, 1);
    blocks(250);
    int pmono = poly_voices(0), pnote = eng_note(0);
    eng_key(0, 64, 0);
    blocks(10);
    int pback = pnote > p60 && eng_note(0) == p60;
    eng_key_release_all();
    blocks(600);
    const int mono_v = pmono, mono_quiet = silent();
    pmono = mono_v == 1 && pback && mono_quiet;
    if (!pmono) printf("the keys: POLY Mono: %d voices, back %d, then silent %d\n", mono_v, pback, mono_quiet);
    // a trig's length: the FM on step 0, its gate's blocks as it is, then held over 3 steps more
    eng_defaults(p);
    eng_fresh();
    for (int l2 = 0; l2 < ENG_LANES; ++l2)
      for (int st = 0; st < ENG_STEPS; ++st) eng_trig_set(l2, st, 0);
    eng_set_model(0, eng_model_find(MK_FM, 0));
    eng_trig_set(0, 0, 1);
    auto gate_blocks = [&]() {
      eng_play(1);
      int on = 0;
      for (int b = 0; b < 300; ++b) {
        eng_render(l, r, 128);
        on += (eng_gates() & 1u) != 0;
      }
      eng_play(0);
      blocks(50);
      return on;
    };
    int g1 = gate_blocks();
    int set = eng_step_tie_set(0, 0, 3) == 0 && eng_step_tie(0, 0) == 3;
    int g4 = gate_blocks();
    uint8_t ties[ENG_STEPS];
    eng_step_ties(0, ties);
    set &= ties[0] == 3 && ties[1] == 0;
    eng_lock_clear(0, 0);
    set &= eng_step_tie(0, 0) == 0;
    const float bpm = 30.0f * std::pow(2.0f, p->val[PG_GLOBAL * 4] * 3.321928f);
    const float six = 44100.0f * 60.0f / (4.0f * bpm) / 128.0f;   // (a sixteenth's blocks)
    int tie_ok = set && g4 > g1 + static_cast<int>(2.5f * six) && g4 < g1 + static_cast<int>(3.5f * six);
    eng_defaults(p);
    eng_fresh();
    printf("the keys: one voice %s (%d %d %d, back with a trig %s); the chords held 3, 64 let go, all: POLY %d %d %d, "
           "PROPHET %d %d %d, SQ-1 %d %d %d; POLY Mono %s; a trig's length: %d blocks, held over 3 steps more %d %s\n",
           mono ? "ok" : "WRONG", n1, n2, n3, back ? "yes" : "NO", ph3, ph1, ph0, xh3, xh1, xh0, sh3, sh1, sh0,
           pmono ? "ok" : "WRONG", g1, g4, tie_ok ? "ok" : "WRONG");
    if (!mono || !chords || !pmono || !tie_ok) ++failures, printf("FAIL: the keys\n");
  }

  printf(failures ? "%d FAILURES\n" : "all good\n", failures);
  return failures ? 1 : 0;
}
