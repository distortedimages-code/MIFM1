/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1's own exports in the browser emulator (app.h APP_WEB; fm1-base's web/emu/emu_web.c includes this file): the
 * FM-1's load as the engine's model has it (mi/engine.cc PLAT_WEB, mi/web_costs.h), for the line under the panel
 * (web/emu/app_worklet.js sends it to the page). i: 0 core 0, 1 core 1, 2 the worst total of the last second, 3 the
 * preparation; tenths of % of the block */
__attribute__((used, visibility("default"))) uint32_t web_fm1_load(uint32_t i)
{
    return i == 0 ? eng_load(0) : i == 1 ? eng_load(1) : i == 2 ? eng_peak() : eng_prep_load();
}

/* the page's "Essayer": kind 0 a model (Plaits', an FM drum's: its kit with it; PADS held by another track: that
 * track), 1 a filter (its model), on the track playing, its page shown, as ALGORITHM would */
__attribute__((used, visibility("default"))) void web_try(uint32_t kind, uint32_t i)
{
    int t = ui.track;
    if (kind == 0 && i < ENG_MODELS) {
        if (!eng_model_free(t, (int)i))  /* (PADS, once a project: the track holding it) */
            for (int u = 0; u < ENG_TRACKS; u++)
                if (u != t && eng_project()->algo[PG_TRACK(u, 0)] == i) {
                    set_track(u);
                    t = u;
                    break;
                }
        eng_set_model(t, (int)i);
        go_page(PG_TRACK(t, 0));
    } else if (kind == 1 && i < MF_MODELS) {
        eng_project()->algo[PG_FILT(t, 0)] = (uint8_t)i;
        go_page(VP_FLT1(t));
    } else {
        return;
    }
    dirty = 1;
    ui.full = ui.body_panel = 1;
    ui.anim = 1;                         /* (its name written again) */
    ui.anim_at = plat_ms();
}

/* ---- (0.16.25) the page's examples ("the lab"): a second copy of this program in a worklet of its own
 * (web/emu/app_worklet.js, mifm1-lab), never booted: the engine alone (one core, as the tests run it), a small
 * project an example, made from the demo groove; up to four knobs, each on a parameter of that project (or TRAUMA's,
 * the random dose's), their texts the machine's own ---- */
enum { LAB_LADDER, LAB_MS20, LAB_SEM, LAB_VOX, LAB_TAURUS, LAB_STATIC, LAB_KICK, LAB_TRAUMA, LAB_DICE, LAB_WARPS,
       LAB_RATCH, LAB_PLOCK, LAB_EUCLID, LAB_CHOIR, LAB_VOCODER, LAB_METAL, LAB_SDS, LAB_N };
       /* (0.16.30: the P-locks' kick, the Euclidean rhythm; 0.16.44: CHOIR, VOCODER, METAL; 0.16.49: the SDS) */
#define LAB_P_TRAUMA (-1)
#define LAB_P_DICE (-2)
#define LAB_P_RVMODE (-3)                /* (0.16.44) the reverb's algorithm: METAL or SHIMMER */
#define LAB_RATCHES 10
static int lab_on, lab_n, lab_p[4], lab_direct, lab_lane;   /* (direct: a filter on a saw, the engine idle) */
static float lab_ph;                     /* (the saw's phase) */
static float lab_ks[12];                 /* (0.16.45) the filter's knobs as heard: glided toward the knobs' (~10 ms) */
static int lab_ks_on;
static float lab_l[512], lab_r[512];
static char lab_txt[32];
static int lab_ratch[LAB_RATCHES];       /* RATCH's travel here: one step's hits, then hits over steps */

static int lab_mparam(int t, int i) { return PG_TRACK(t, 0) * 4 + 1 + i; }   /* a machine's knob i */
static float lab_value(int param, float u);
static int lab_tp(int param)             /* a track parameter's tp (a lock's) */
{
    int lane;
    return eng_param_tp(param, &lane);
}
static void lab_euclid_set(int fills, int rot)   /* (0.16.30) T1's Euclidean rhythm over 16, its trigs written again */
{
    project_t *p = eng_project();
    p->val[PG_EUC(0) * 4] = (float)fills;
    p->val[PG_EUC(0) * 4 + 1] = (float)rot;
    for (int s = 0; s < 16; s++)
        eng_trig_set(0, s, eng_euclid(16, fills, rot, s));
}
static void lab_only(project_t *p, int keep, float kick)   /* that track alone, the kick under it (or not) */
{
    for (int t = 0; t < ENG_TRACKS; t++)
        p->val[PG_MIX * 4 + t] = t == keep ? 0.9f : t == 0 ? kick : 0.0f;
    for (int g = 0; g <= ENG_TRACKS; g++)    /* (the LFOs' cables off: the knobs heard as they are) */
        for (int c = 0; c < ENG_CABLES; c++)
            p->cable[g][c].depth = 0;
}
static void lab_unduck(project_t *p)    /* (the kick silent: nothing ducked by its trigs) */
{
    for (int k = 1; k < 4; k++)
        p->val[PG_DUCK * 4 + k] = 0.0f;
}
static void lab_k(int n, int p0, int p1, int p2, int p3)
{
    lab_n = n, lab_p[0] = p0, lab_p[1] = p1, lab_p[2] = p2, lab_p[3] = p3;
}

__attribute__((used, visibility("default"))) uint32_t lab_setup(uint32_t id)
{
    static int inited;
    if (!inited) {
        if (eng_init() != 0)
            return 0;
        static const uint8_t R[LAB_RATCHES][2] = {{1, 1}, {2, 1}, {3, 1}, {4, 1}, {6, 1}, {8, 1},
                                                  {3, 2}, {5, 4}, {7, 4}, {9, 8}};
        for (int i = 0; i < LAB_RATCHES; i++)
            lab_ratch[i] = R[i][1] == 1 ? R[i][0] : eng_ratch_join(R[i][0], R[i][1]);
        inited = 1;
    }
    project_t *p = eng_project();
    eng_play(0);
    eng_groove(p);
    lab_direct = 0, lab_lane = -1;
    if (id <= LAB_VOX) {                 /* a low saw (A1) through that filter alone: the filter module, the engine idle */
        static int mf_ready;
        if (!mf_ready)
            mf_init(), mf_ready = 1;
        mf_reset(0);
        lab_ks_on = 0;
        static const float F[12] = {0.45f, 0.4f, 0.0f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, MF_LP, 0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 12; k++)
            p->val[PG_FILT(0, k / 4) * 4 + k % 4] = F[k];
        p->algo[PG_FILT(0, 0)] = (uint8_t)(MF_LADDER + id);
        lab_direct = 1;
        if (id == LAB_SEM || id == LAB_VOX)  /* (CUTOFF, RESO; DRIVE, or TYPE: SEM's LP BP HP, VOX's voices) */
            lab_k(3, PG_FILT(0, 0) * 4 + 0, PG_FILT(0, 0) * 4 + 1, PG_FILT(0, 2) * 4 + 0, 0);
        else
            lab_k(3, PG_FILT(0, 0) * 4 + 0, PG_FILT(0, 0) * 4 + 1, PG_FILT(0, 1) * 4 + 0, 0);
        return (uint32_t)lab_n;
    } else if (id == LAB_TAURUS) {       /* (0.16.45) EBM: the kick on every beat, the snare on two and four, TAURUS
                                          * galloping between the kicks on the sixteenths (F minor, its octave jumps),
                                          * the Minimoog's saw pushed into its ladder, short notes; the filter off */
        static const int8_t B[16] = {0, 5, 5, 17, 0, 5, 5, 17, 0, 5, 5, 17, 0, 1, 1, 15};   /* (over C2; 0: none) */
        static const float K[8] = {5.5f / 9.0f, 0.35f, 0.7f, 0.33f, 0.45f, 0.8f, 0.3f, 0.0f};   /* (Saw, Hot 40 %) */
        eng_set_model(1, eng_model_find(MK_TAURUS, 0));
        eng_set_model(2, eng_model_find(MK_DRUM, 1));
        p->algo[PG_FILT(1, 0)] = p->algo[PG_FILT(2, 0)] = MF_OFF;
        lab_only(p, 1, 0.9f);
        p->val[PG_MIX * 4 + 1] = 0.85f, p->val[PG_MIX * 4 + 2] = 0.7f;
        p->val[PG_PAN * 4 + 2] = 0.0f;
        for (int i = 0; i < 8; i++) {
            const int q = lab_mparam(1, i);
            p->val[q] = eng_pdef(q)->kind == K_BIP ? 2.0f * K[i] - 1.0f : K[i];
        }
        p->val[PG_TRACK(1, 2) * 4 + 1] = 0.4f;   /* (LENGTH: short) */
        p->val[PG_TRACK(1, 2) * 4 + 3] = -1.0f;  /* (OCTAVE -1: C2) */
        p->val[PG_DUCK * 4 + 1] = 0.2f, p->val[PG_DUCK * 4 + 2] = p->val[PG_DUCK * 4 + 3] = 0.0f;
        p->val[eng_track_param(1, TP_SEND)] = p->val[eng_track_param(1, TP_DSEND)] = 0.0f;
        p->val[eng_track_param(2, TP_SEND)] = 0.25f, p->val[eng_track_param(2, TP_DSEND)] = 0.0f;
        p->rnd[1][TP_RATCHET] = 0, p->rnd[2][5] = 0, p->rnd[2][TP_PAN] = 0;
        p->val[PG_STEP(2) * 4 + 1] = 1.0f;
        for (int t = 0; t < 3; t++)
            eng_steps_clear(t);
        for (int st = 0; st < 16; st++) {
            eng_trig_set(0, st, st % 4 == 0);
            eng_trig_set(2, st, st == 4 || st == 12);
            if (B[st]) {
                eng_trig_set(1, st, 1);
                eng_lock_set(1, st, TP_NOTE, (float)B[st]);
            }
        }
        lab_lane = 1;
        lab_k(4, lab_mparam(1, 3), lab_mparam(1, 4), lab_mparam(1, 5), lab_mparam(1, 6));
    } else if (id == LAB_STATIC) {       /* STATIC on three tracks, ARC/01's three lanes (after its RELAY ROOM): BODY's
                                          * rare sub hits (43 Hz), ARC's copper ring (830 Hz by a modulator at 113 Hz),
                                          * SPARK's sparks (S/H, a band pass at 2.8 kHz), each at 35 % */
        static const int8_t B[16] = {-7, 0, 0, 0, 0, 0, 0, 0, 0, 0, -5, 0, 0, 0, 0, 0},
                            A[16] = {0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 27, 0, 0},
                            S[16] = {0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1};
        const int8_t *seq[3] = {B, A, S};
        for (int t = 1; t < ENG_TRACKS; t++) {
            float k[8];
            eng_set_model(t, eng_model_find(MK_STATIC, 0));
            static_mode_defaults(t - 1, k);
            if (t == 1)
                k[7] = 0.716f;           /* (BODY: 600 ms) */
            if (t == 2)
                k[1] = 0.6227f, k[2] = 0.365f;   /* (ARC: 113 Hz, 2 rad) */
            for (int i = 0; i < 8; i++) {
                const int q = lab_mparam(t, i);
                p->val[q] = eng_pdef(q)->kind == K_BIP ? 2.0f * k[i] - 1.0f : k[i];
            }
            p->algo[PG_FILT(t, 0)] = MF_OFF;
            p->val[PG_TRACK(t, 2) * 4 + 3] = 0.0f;   /* (OCTAVE 0) */
            p->val[eng_track_param(t, TP_SEND)] = p->val[eng_track_param(t, TP_DSEND)] = 0.0f;   /* (dry: no space) */
            eng_steps_clear(t);
            for (int st = 0; st < 16; st++)
                if (seq[t - 1][st]) {
                    eng_trig_set(t, st, 1);
                    if (t < 3)
                        eng_lock_set(t, st, TP_NOTE, (float)seq[t - 1][st]);
                    else if (st != 6)    /* (step 6 always: its long gate, DENSITY's room) */
                        eng_lock_set(t, st, TP_PROB, 0.35f);
                }
        }
        eng_step_tie_set(3, 6, 2);       /* (a spark's gate over three steps: DENSITY's room) */
        lab_only(p, 1, 0.0f);
        p->val[PG_MIX * 4 + 2] = 0.5f, p->val[PG_MIX * 4 + 3] = 0.8f;
        lab_lane = 3;
        lab_k(4, lab_mparam(2, 2), lab_mparam(2, 3), lab_mparam(3, 5), lab_mparam(1, 1));
    } else if (id == LAB_KICK) {         /* the kick alone, a beat and a push */
        lab_only(p, 0, 0.9f);
        eng_trig_set(0, 14, 1);
        lab_lane = 0;
        lab_k(3, lab_mparam(0, 0), lab_mparam(0, 1), lab_mparam(0, 2), 0);
    } else if (id == LAB_TRAUMA) {       /* the groove; TRAUMA */
        lab_lane = 0;                    /* (its screen shows the kick's steps: the beat it bends) */
        lab_k(1, LAB_P_TRAUMA, 0, 0, 0);
    } else if (id == LAB_DICE) {         /* the groove; the random dose */
        lab_lane = 0;
        lab_k(1, LAB_P_DICE, 0, 0, 0);
    } else if (id == LAB_WARPS) {        /* the stabs (its carrier) by the bass (its modulator) */
        static const float W[8] = {(float)WC_T4, 1.0f, 0.4f, 0.8f, 0.5f, 0.6f, 0.6f, (float)WM_WARPS};
        for (int k = 0; k < 8; k++)
            p->val[PG_WARPS(k / 4) * 4 + k % 4] = W[k];
        lab_lane = 3;                    /* (the stabs' steps) */
        lab_k(4, PG_WARPS(0) * 4 + 2, PG_WARPS(1) * 4 + 0, PG_WARPS(0) * 4 + 3, PG_WARPS(1) * 4 + 3);
    } else if (id == LAB_RATCH) {        /* the hats alone on four off-beats, their ratchets */
        lab_only(p, 2, 0.0f);
        for (int s = 0; s < 16; s++)
            eng_trig_set(2, s, s % 4 == 2);
        p->val[PG_STEP(2) * 4 + 2] = 3.0f;
        eng_climb_opt_set(p, 2, CL_ON, CLON_RATCH);   /* (0.16.60: CLIMB on its hits, as drawn; its LIMIT 12) */
        lab_lane = 2;
        lab_k(3, PG_STEP(2) * 4 + 2, PG_CURVE * 4 + 2, PG_EUC(2) * 4 + 2, 0);
    } else if (id == LAB_PLOCK) {        /* (0.16.30) a kick alone, seven trigs, three of them locked */
        lab_only(p, 0, 0.9f);
        eng_steps_clear(0);
        static const int8_t T[7] = {0, 4, 7, 8, 10, 12, 14};
        for (int i = 0; i < 7; i++)
            eng_trig_set(0, T[i], 1);
        eng_lock_set(0, 7, TP_NOTE, 7.0f);
        eng_lock_set(0, 10, TP_NOTE, -5.0f);
        eng_lock_set(0, 14, TP_NOTE, 12.0f);
        eng_lock_set(0, 12, lab_tp(lab_mparam(0, 2)), lab_value(lab_mparam(0, 2), 0.85f));
        lab_lane = 0;
        lab_k(1, lab_mparam(0, 2), 0, 0, 0);
    } else if (id == LAB_CHOIR) {        /* (0.16.44) CHOIR on T2 alone, the soprano on a slow line in F minor, a step
                                          * an eighth (SPEED x1/2), each note held into the next: she glides */
        static const int8_t N[7][3] = {{0, 0, 2}, {3, 1, 0}, {4, 0, 1}, {6, -4, 1}, {8, -2, 2}, {11, 0, 0}, {12, -7, 3}};
        eng_set_model(1, eng_model_find(MK_CHOIR, 0));   /* (step, note over C3 in the scale, the steps it ties) */
        p->algo[PG_FILT(1, 0)] = MF_OFF;
        lab_only(p, 1, 0.0f);
        lab_unduck(p);
        p->val[PG_TRACK(1, 2) * 4 + 1] = 1.0f;   /* (LENGTH: the gate to the next note) */
        p->val[PG_TRACK(1, 2) * 4 + 3] = 0.0f;   /* (OCTAVE 0: C5 sung on C3) */
        p->val[eng_track_param(1, TP_SEND)] = 0.35f, p->val[eng_track_param(1, TP_DSEND)] = 0.0f;
        p->speed[1] = -2;
        p->rnd[1][TP_RATCHET] = 0;       /* (the groove's bass ratchets now and then: not a held note) */
        eng_steps_clear(1);
        for (int i = 0; i < 7; i++) {
            eng_trig_set(1, N[i][0], 1);
            eng_lock_set(1, N[i][0], TP_NOTE, (float)N[i][1]);
            if (N[i][2])
                eng_step_tie_set(1, N[i][0], N[i][2]);
        }
        lab_lane = 1;
        lab_k(4, lab_mparam(1, 0), lab_mparam(1, 1), lab_mparam(1, 2), lab_mparam(1, 3));
    } else if (id == LAB_VOCODER) {      /* (0.16.44) T3 SPEECH, its words, silent; T4 the VOCODER (MOD T3) on Lament, a
                                          * chord a bar; the kick under them */
        static const float W[8] = {0.1f, 0.6f, 0.35f, 0.85f, 0.2f, 0.7f, 0.45f, 0.95f};   /* (MORPH: its words) */
        eng_set_model(2, eng_model_find(MK_PLAITS, 15));
        eng_set_model(3, eng_model_find(MK_VOCODER, 0));
        p->algo[PG_FILT(2, 0)] = p->algo[PG_FILT(3, 0)] = MF_OFF;
        lab_only(p, 3, 0.5f);
        eng_steps_clear(2);
        p->val[PG_STEP(2) * 4 + 1] = 1.0f;   /* (every word: the groove leaves 10 % of the hats out) */
        for (int st = 0; st < 16; st += 2) {
            eng_trig_set(2, st, 1);
            eng_lock_set(2, st, lab_tp(lab_mparam(2, 2)), W[st / 2]);
        }
        p->val[PG_TRACK(3, 0) * 4 + 0] = 9.0f / 27.0f;   /* (PLAY: Lament) */
        p->val[lab_mparam(3, 0)] = 0.7f;                  /* (MOD: T3) */
        p->val[PG_TRACK(3, 2) * 4 + 1] = 1.0f;
        p->val[eng_track_param(3, TP_SEND)] = 0.3f, p->val[eng_track_param(3, TP_DSEND)] = 0.0f;
        eng_steps_clear(3);
        eng_trig_set(3, 0, 1);
        eng_step_tie_set(3, 0, 15);
        lab_lane = 2;
        lab_k(4, lab_mparam(3, 1), lab_mparam(3, 2), lab_mparam(3, 6), lab_mparam(3, 7));
    } else if (id == LAB_METAL) {        /* (0.16.44) the stabs alone into METAL, much of them sent */
        p->algo[PG_REVERB] = RV_METAL;
        p->algo[PG_FILT(3, 0)] = MF_OFF;     /* (the stabs open, an octave up: the lines' ring heard) */
        p->val[PG_TRACK(3, 2) * 4 + 3] = 1.0f;
        lab_only(p, 3, 0.0f);
        lab_unduck(p);
        p->val[PG_REVERB * 4 + 0] = 0.55f, p->val[PG_REVERB * 4 + 1] = 0.6f;   /* (DECAY ~4 s, SIZE) */
        p->val[PG_REVERB * 4 + 2] = 0.7f, p->val[PG_REVERB * 4 + 3] = 1.0f;    /* (TONE toward the metal, RETURN) */
        p->val[eng_track_param(3, TP_SEND)] = 0.85f, p->val[eng_track_param(3, TP_DSEND)] = 0.0f;
        lab_lane = 3;
        lab_k(4, PG_REVERB * 4 + 0, PG_REVERB * 4 + 1, PG_REVERB * 4 + 2, LAB_P_RVMODE);
    } else if (id == LAB_SDS) {          /* (0.16.49) the SDS on three tracks, as the SDS 8's channels: the kick (1, 9,
                                          * 11), the snare on two and four (a ghost note struck softly: HIT locked),
                                          * the toms' fill falling down the bar's end, a little of the reverb */
        static const int8_t K[16] = {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0},
                            S[16] = {0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0},
                            T[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 3, -4};   /* (a tom: its note + 1) */
        const int8_t *seq[3] = {K, S, T};
        for (int t = 0; t < 3; t++) {
            eng_set_model(t, eng_model_find(MK_SDS, t));
            p->algo[PG_FILT(t, 0)] = MF_OFF;
            for (int tp = 0; tp < ENG_NTP; tp++)
                p->rnd[t][tp] = 0;       /* (the groove's randoms: none, every hit as written) */
            p->val[PG_STEP(t) * 4 + 1] = 1.0f;
            p->val[PG_PAN * 4 + t] = t == 2 ? -0.2f : 0.0f;
            p->val[eng_track_param(t, TP_SEND)] = t ? 0.22f : 0.0f;
            p->val[eng_track_param(t, TP_DSEND)] = 0.0f;
            eng_steps_clear(t);
            for (int st = 0; st < 16; st++)
                if (seq[t][st]) {
                    eng_trig_set(t, st, 1);
                    if (t == 2)
                        eng_lock_set(t, st, TP_NOTE, (float)(seq[t][st] - 1));
                }
        }
        eng_lock_set(1, 7, lab_tp(lab_mparam(1, 5)), lab_value(lab_mparam(1, 5), 0.25f));   /* (the ghost note) */
        lab_only(p, 1, 0.9f);
        lab_unduck(p);
        p->val[PG_MIX * 4 + 1] = 0.8f, p->val[PG_MIX * 4 + 2] = 0.85f;
        lab_lane = 2;
        lab_k(4, lab_mparam(2, 7), lab_mparam(2, 3), lab_mparam(1, 1), lab_mparam(0, 2));
    } else if (id == LAB_EUCLID) {       /* (0.16.30) a percussion alone, its Euclidean rhythm (5 over 16) */
        eng_set_model(0, eng_model_find(MK_DRUM, 5));
        p->algo[PG_FILT(0, 0)] = MF_OFF;
        lab_only(p, 0, 0.9f);
        eng_steps_clear(0);
        lab_lane = 0;
        lab_k(0, 0, 0, 0, 0);
        lab_euclid_set(5, 0);
    } else {
        return 0;
    }
    eng_fresh();
    if (lab_on)
        eng_play(1);
    return (uint32_t)lab_n;
}

static float lab_value(int param, float u)   /* a knob's travel (0..1) as its parameter's value */
{
    const eng_pdef_t *d = eng_pdef(param);
    if (param == PG_STEP(2) * 4 + 2) {   /* (RATCH: its list) */
        int i = (int)(u * (LAB_RATCHES - 1) + 0.5f);
        return (float)lab_ratch[i < 0 ? 0 : i >= LAB_RATCHES ? LAB_RATCHES - 1 : i];
    }
    if (d->kind == K_BIP)
        return 2.0f * u - 1.0f;
    if (d->kind == K_INT || d->kind == K_ENUM)
        return (float)(int)(d->lo + u * (float)(d->hi - d->lo) + 0.5f);
    return u;
}
static float lab_travel(int param)       /* where a knob is now, 0..1 */
{
    project_t *p = eng_project();
    if (param == LAB_P_TRAUMA)
        return p->trauma / 100.0f;
    if (param == LAB_P_DICE)
        return p->dice / 100.0f;
    if (param == LAB_P_RVMODE)
        return p->algo[PG_REVERB] == RV_SHIMMER ? 1.0f : 0.0f;
    const eng_pdef_t *d = eng_pdef(param);
    float v = p->val[param];
    if (param == PG_STEP(2) * 4 + 2) {
        for (int i = 0; i < LAB_RATCHES; i++)
            if ((float)lab_ratch[i] == v)
                return (float)i / (LAB_RATCHES - 1);
        return 0.0f;
    }
    if (d->kind == K_BIP)
        return (v + 1.0f) * 0.5f;
    if (d->kind == K_INT || d->kind == K_ENUM)
        return d->hi > d->lo ? (v - d->lo) / (float)(d->hi - d->lo) : 0.0f;
    return v;
}

__attribute__((used, visibility("default"))) uint32_t lab_name(uint32_t i)
{
    if (i >= (uint32_t)lab_n)
        return 0;
    const int param = lab_p[i];
    const char *s = param == LAB_P_TRAUMA ? "Trauma" : param == LAB_P_DICE ? "Dose" : param == LAB_P_RVMODE ? "Mode"
                                                                                       : eng_param_name(param);
    int k = 0;
    while (s[k] && k < (int)sizeof lab_txt - 1)
        lab_txt[k] = s[k], k++;
    lab_txt[k] = 0;
    return (uint32_t)(uintptr_t)lab_txt;
}
__attribute__((used, visibility("default"))) float lab_get(uint32_t i)
{
    return i < (uint32_t)lab_n ? lab_travel(lab_p[i]) : 0.0f;
}
__attribute__((used, visibility("default"))) uint32_t lab_set(uint32_t i, float u)   /* its text, as the screen's */
{
    if (i >= (uint32_t)lab_n)
        return 0;
    project_t *p = eng_project();
    const int param = lab_p[i];
    u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
    if (param == LAB_P_RVMODE) {         /* (METAL or SHIMMER: the same lines, the octave fed back or not) */
        const int sh = u >= 0.5f;
        p->algo[PG_REVERB] = (uint8_t)(sh ? RV_SHIMMER : RV_METAL);
        const char *t = sh ? "Shimmer" : "Metal";
        int k = 0;
        while (t[k])
            lab_txt[k] = t[k], k++;
        lab_txt[k] = 0;
    } else if (param == LAB_P_TRAUMA || param == LAB_P_DICE) {
        const int pc = (int)(u * 100.0f + 0.5f);
        if (param == LAB_P_TRAUMA) {
            p->trauma = (uint8_t)pc;
        } else {
            if (!p->dice && pc)          /* (from 0, another throw: the screen's way) */
                p->dice_seed++;
            p->dice = (uint8_t)pc;
        }
        int k = 0;
        if (pc >= 100)
            lab_txt[k++] = '1';
        if (pc >= 10)
            lab_txt[k++] = (char)('0' + pc / 10 % 10);
        lab_txt[k++] = (char)('0' + pc % 10);
        lab_txt[k++] = '%';
        lab_txt[k] = 0;
    } else {
        const float v = lab_value(param, u);
        p->val[param] = v;
        eng_param_text(param, v, lab_txt, sizeof lab_txt);
    }
    return (uint32_t)(uintptr_t)lab_txt;
}
__attribute__((used, visibility("default"))) void lab_play(uint32_t on)
{
    lab_on = on != 0;
    if (!lab_direct)
        eng_play(lab_on);
}
/* what the module's screen shows: the filter's cutoff now (Hz, 0: none); the lane it shows, its step playing (-1:
 * none) and its trigs (bit s: step s of 16) */
__attribute__((used, visibility("default"))) float lab_meter(void) { return lab_direct ? mf_cutoff_hz(0) : 0.0f; }
__attribute__((used, visibility("default"))) int32_t lab_step(void)
{
    return lab_lane < 0 || !lab_on || lab_direct ? -1 : eng_step_now(lab_lane);
}
__attribute__((used, visibility("default"))) uint32_t lab_trigs(void)
{
    uint32_t m = 0;
    if (lab_lane >= 0)
        for (int s = 0; s < 16; s++)
            m |= (uint32_t)(eng_trig(lab_lane, s) != 0) << s;
    return m;
}
__attribute__((used, visibility("default"))) uint32_t lab_out_l(void) { return (uint32_t)(uintptr_t)lab_l; }
__attribute__((used, visibility("default"))) uint32_t lab_out_r(void) { return (uint32_t)(uintptr_t)lab_r; }

/* (0.16.30) what the page's second copy (the one that draws, on the page itself: it never plays) asks: a knob's text
 * at a travel without moving it; the filter's impulse response as its knobs have it (a small one: its curve, its
 * nonlinearities asleep); the example from its start, rendered (a frozen hit); the P-locks; the Euclidean rhythm */
static float lab_buf[32768];
__attribute__((used, visibility("default"))) uint32_t lab_buf_at(void) { return (uint32_t)(uintptr_t)lab_buf; }
__attribute__((used, visibility("default"))) uint32_t lab_text(uint32_t i, float u)
{
    if (i >= (uint32_t)lab_n || lab_p[i] < 0)
        return 0;
    u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
    eng_param_text(lab_p[i], lab_value(lab_p[i], u), lab_txt, sizeof lab_txt);
    return (uint32_t)(uintptr_t)lab_txt;
}
__attribute__((used, visibility("default"))) uint32_t lab_ir(uint32_t n)
{
    if (!lab_direct)
        return 0;
    if (n > 32768)
        n = 32768;
    project_t *p = eng_project();
    float k[12];
    for (int i = 0; i < 12; i++)
        k[i] = p->val[PG_FILT(0, i / 4) * 4 + i % 4];
    const int model = p->algo[PG_FILT(0, 0)], type = (int)(k[MF_TYPE] + 0.5f), ty = type < 0 ? 0 : type > 2 ? 2 : type;
    mf_reset(1);
    static float blk[128];
    for (int b = 0; b < 64; b++) {       /* (its cutoff settled where its knobs put it, in silence) */
        mf_set(1, model, ty, k, 33.0f);
        for (int i = 0; i < 128; i++)
            blk[i] = 0.0f;
        mf_process(1, blk, 128);
    }
    for (uint32_t at = 0; at < n; at += 128) {
        mf_set(1, model, ty, k, 33.0f);
        for (int i = 0; i < 128; i++)
            blk[i] = at == 0 && i == 0 ? 0.001f : 0.0f;
        mf_process(1, blk, 128);
        for (int i = 0; i < 128 && at + (uint32_t)i < n; i++)
            lab_buf[at + i] = blk[i] * 1000.0f;
    }
    return n;
}
__attribute__((used, visibility("default"))) uint32_t lab_snap(uint32_t n)
{
    if (lab_direct)
        return 0;
    if (n > 32768)
        n = 32768;
    eng_cut();                           /* (the last one's tails gone, the effects' too: a block faded, a block that */
    for (int i = 0; i < 2; i++)          /*  makes the modules new; then the start, its first trig heard) */
        eng_render(lab_l, lab_r, 128);
    eng_play(1);
    for (uint32_t at = 0; at < n; at += 128) {
        eng_render(lab_l, lab_r, 128);
        for (int i = 0; i < 128 && at + (uint32_t)i < n; i++)
            lab_buf[at + i] = 0.5f * (lab_l[i] + lab_r[i]);
    }
    eng_play(0);
    return n;
}
__attribute__((used, visibility("default"))) void lab_lock(uint32_t step, uint32_t which, float v)
{                                        /* which 0: NOTE (semitones); 1: the knob's (its travel, 0..1) */
    if (lab_lane < 0 || step >= 16)
        return;
    const int param = lab_mparam(lab_lane, 2);
    if (which == 0)
        eng_lock_set(lab_lane, (int)step, TP_NOTE, v);
    else
        eng_lock_set(lab_lane, (int)step, lab_tp(param), lab_value(param, v));
}
__attribute__((used, visibility("default"))) void lab_unlock(uint32_t step, uint32_t which)
{
    if (lab_lane < 0 || step >= 16)
        return;
    const int tps[2] = {TP_NOTE, lab_tp(lab_mparam(lab_lane, 2))};
    float keep = 0.0f;
    const int other = tps[which ? 0 : 1], had = eng_lock(lab_lane, (int)step, other, &keep);
    eng_lock_clear(lab_lane, (int)step);
    if (had)
        eng_lock_set(lab_lane, (int)step, other, keep);
}
__attribute__((used, visibility("default"))) void lab_euclid(uint32_t fills, uint32_t rot) { lab_euclid_set((int)fills, (int)rot); }
__attribute__((used, visibility("default"))) void lab_render(uint32_t n)
{
    if (n > 512)
        n = 512;
    if (!lab_direct) {
        eng_render(lab_l, lab_r, n);
        return;
    }
    /* the saw (A1, polyBLEP: no alias heard), through the filter as its knobs have it; (0.16.45) its knobs glided,
     * 16 samples at a time (a finger's drag comes in steps, ~60 a second: RESO, DRIVE jumping crackled) */
    project_t *p = eng_project();
    float k[12];
    for (int i = 0; i < 12; i++)
        k[i] = p->val[PG_FILT(0, i / 4) * 4 + i % 4];
    if (!lab_ks_on) {
        for (int i = 0; i < 12; i++)
            lab_ks[i] = k[i];
        lab_ks_on = 1;
    }
    const int model = p->algo[PG_FILT(0, 0)], type = (int)(k[MF_TYPE] + 0.5f);
    const float dt = 55.0f / 44100.0f;
    for (uint32_t at = 0; at < n; at += 16) {
        const uint32_t m = n - at < 16 ? n - at : 16;
        for (int i = 0; i < 12; i++)     /* (16 samples: 1 - e^(-16 / 441), ~10 ms) */
            lab_ks[i] += (k[i] - lab_ks[i]) * 0.0356f;
        lab_ks[MF_TYPE] = k[MF_TYPE];
        mf_set(0, model, type < 0 ? 0 : type > 2 ? 2 : type, lab_ks, 33.0f);
        for (uint32_t i = at; i < at + m; i++) {
            float x = 2.0f * lab_ph - 1.0f, t;
            if (lab_ph < dt) t = lab_ph / dt, x -= t + t - t * t - 1.0f;
            else if (lab_ph > 1.0f - dt) t = (lab_ph - 1.0f) / dt, x -= t * t + t + t + 1.0f;
            lab_l[i] = lab_on ? 0.45f * x : 0.0f;
            lab_ph += dt;
            if (lab_ph >= 1.0f) lab_ph -= 1.0f;
        }
        mf_process(0, lab_l + at, m);
    }
    static const float G[4] = {0.45f, 0.23f, 0.18f, 0.36f};   /* (each as loud as the loops: LADDER, MS-20, SEM, VOX) */
    const float g = G[(unsigned)(model - MF_LADDER) & 3];
    for (uint32_t i = 0; i < n; i++) {
        float y = lab_l[i] * g;
        y = y > 1.2f ? 1.2f : y < -1.2f ? -1.2f : y;
        lab_l[i] = lab_r[i] = y - 0.13f * y * y * y;
    }
}
