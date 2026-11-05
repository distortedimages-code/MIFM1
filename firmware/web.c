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
       LAB_RATCH, LAB_N };
#define LAB_P_TRAUMA (-1)
#define LAB_P_DICE (-2)
#define LAB_RATCHES 10
static int lab_on, lab_n, lab_p[4], lab_direct, lab_lane;   /* (direct: a filter on a saw, the engine idle) */
static float lab_ph;                     /* (the saw's phase) */
static float lab_l[512], lab_r[512];
static char lab_txt[32];
static int lab_ratch[LAB_RATCHES];       /* RATCH's travel here: one step's hits, then hits over steps */

static int lab_mparam(int t, int i) { return PG_TRACK(t, 0) * 4 + 1 + i; }   /* a machine's knob i */
static void lab_only(project_t *p, int keep, float kick)   /* that track alone, the kick under it (or not) */
{
    for (int t = 0; t < ENG_TRACKS; t++)
        p->val[PG_MIX * 4 + t] = t == keep ? 0.9f : t == 0 ? kick : 0.0f;
    for (int g = 0; g <= ENG_TRACKS; g++)    /* (the LFOs' cables off: the knobs heard as they are) */
        for (int c = 0; c < ENG_CABLES; c++)
            p->cable[g][c].depth = 0;
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
    } else if (id == LAB_TAURUS) {       /* the bass's riff on TAURUS, the filter off (its own ladder) */
        eng_set_model(1, eng_model_find(MK_TAURUS, 0));
        p->algo[PG_FILT(1, 0)] = MF_OFF;
        lab_only(p, 1, 0.5f);
        lab_lane = 1;
        lab_k(3, lab_mparam(1, 3), lab_mparam(1, 4), lab_mparam(1, 5), 0);
    } else if (id == LAB_STATIC) {       /* STATIC: sub hits and high ones, an octave up, its SPACE into the reverb */
        eng_set_model(1, eng_model_find(MK_STATIC, 0));
        p->algo[PG_FILT(1, 0)] = MF_OFF;
        lab_only(p, 1, 0.0f);
        p->val[PG_TRACK(1, 2) * 4 + 3] = 1.0f;
        eng_steps_clear(1);
        static const int8_t S[16] = {-24, 0, 0, 24, 0, 0, 19, 0, -24, 0, 0, 24, 0, 27, -24, 0};
        for (int s = 0; s < 16; s++)
            if (S[s]) {
                eng_trig_set(1, s, 1);
                eng_lock_set(1, s, TP_NOTE, (float)S[s]);
            }
        p->val[lab_mparam(1, 5)] = 0.45f;   /* (its DECAY: 80 ms, the pings heard) */
        lab_lane = 1;
        lab_k(4, lab_mparam(1, 0), lab_mparam(1, 1), lab_mparam(1, 3), lab_mparam(1, 6));
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
    } else if (id == LAB_RATCH) {        /* the hats on four off-beats, their ratchets */
        for (int s = 0; s < 16; s++)
            eng_trig_set(2, s, s % 4 == 2);
        p->val[PG_STEP(2) * 4 + 2] = 3.0f;
        lab_lane = 2;
        lab_k(3, PG_STEP(2) * 4 + 2, PG_CURVE * 4 + 2, PG_EUC(2) * 4 + 2, 0);
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
    const char *s = param == LAB_P_TRAUMA ? "Trauma" : param == LAB_P_DICE ? "Dose" : eng_param_name(param);
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
    if (param == LAB_P_TRAUMA || param == LAB_P_DICE) {
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
__attribute__((used, visibility("default"))) void lab_render(uint32_t n)
{
    if (n > 512)
        n = 512;
    if (!lab_direct) {
        eng_render(lab_l, lab_r, n);
        return;
    }
    /* the saw (A1, polyBLEP: no alias heard), through the filter as its knobs have it */
    project_t *p = eng_project();
    float k[12];
    for (int i = 0; i < 12; i++)
        k[i] = p->val[PG_FILT(0, i / 4) * 4 + i % 4];
    const int model = p->algo[PG_FILT(0, 0)], type = (int)(k[MF_TYPE] + 0.5f);
    mf_set(0, model, type < 0 ? 0 : type > 2 ? 2 : type, k, 33.0f);
    const float dt = 55.0f / 44100.0f;
    for (uint32_t i = 0; i < n; i++) {
        float x = 2.0f * lab_ph - 1.0f, t;
        if (lab_ph < dt) t = lab_ph / dt, x -= t + t - t * t - 1.0f;
        else if (lab_ph > 1.0f - dt) t = (lab_ph - 1.0f) / dt, x -= t * t + t + t + 1.0f;
        lab_l[i] = lab_on ? 0.45f * x : 0.0f;
        lab_ph += dt;
        if (lab_ph >= 1.0f) lab_ph -= 1.0f;
    }
    mf_process(0, lab_l, n);
    static const float G[4] = {0.45f, 0.23f, 0.18f, 0.36f};   /* (each as loud as the loops: LADDER, MS-20, SEM, VOX) */
    const float g = G[(unsigned)(model - MF_LADDER) & 3];
    for (uint32_t i = 0; i < n; i++) {
        float y = lab_l[i] * g;
        y = y > 1.2f ? 1.2f : y < -1.2f ? -1.2f : y;
        lab_l[i] = lab_r[i] = y - 0.13f * y * y * y;
    }
}
