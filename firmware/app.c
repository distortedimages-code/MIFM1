/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1: the screen and the panel. The sound is mi/engine.cc's (C++, compiled apart): this file edits its project
 * (eng_project) and shows what it plays.
 *
 * The screen is a module's face: its name and its own display at the top (Plaits' bank of LEDs, Tides' four
 * outputs, Marbles' gates and voltages, the mixer's meters, the reverb's tail), then its four knobs in a row, as
 * KNOB 1 to 4 sit on the FM-1, then the four tracks.
 *
 *   KNOB 1..4   the page's four parameters        SELECT   the module's other face (1/3, 2/3...)
 *   ALGORITHM   the page's model or mode          PRESETS  the active track, 1 to 4
 *   EDIT        Plaits, the active track          LFO      Tides A, then B
 *   SEQ         Marbles, rhythm (t)               ARP      Marbles, notes (X, Y)
 *   HOME        the mixer (the levels)            FX       the reverb (the sends, then its own knobs)
 *   GLO         the load of both cores, the tables, the version
 *   PLAY        Marbles' clock runs or stops      SAVE     held: the project saved (stopped)
 *   SEL         MOD mode: hold a source key, turn a knob: a cable. Held: the track's cables (MATRIX)
 *   keys        play the active track (MOD mode: the 15 sources, then Tides A and B fanned over the 4 knobs)
 *   OCT- OCT+   the keyboard's octave
 *
 * Part of the unity build (base/device/unity.c, base/host/plat_host.c), after the platform: gfx.c is in scope.
 * The modules' biggest tables are in the resource region (app.h), which the installer writes after the firmware:
 * until it holds this build's, the engine does not run (it would read erased flash) and the screen says so. */
#include "fastmath.h"
#include "mi/engine.h"

const void *mi_res_where(void);          /* mi/res_symbols.cc: where the moved tables are (FM-1: the region) */
#ifdef PLAT_HOST
int mi_res_check(const uint8_t *payload, uint32_t len);   /* mi/res_symbols.cc: the region's file against the arrays */
#endif

#define LONG_MS 600u                     /* a long press */
#define SHOW_MS 1500u                    /* a message's time */
#define HEAD_H 22                        /* the screen: the header, the module's display, the knobs, the tracks */
#define PANEL_Y 22
#define PANEL_H 82
#define KNOB_Y 104
#define KNOB_W 60
#define KNOB_H 100
#define FOOT_Y 204
#define FOOT_H 36

/* the modules' colours: their LEDs (Plaits' three banks, Tides' outputs, Marbles' sections) */
#define C_GREEN RGB(70, 214, 110)
#define C_RED RGB(240, 76, 64)
#define C_ORANGE RGB(250, 152, 40)
#define C_YELLOW RGB(238, 214, 72)
#define C_BLUE RGB(86, 170, 255)
#define C_KNOB RGB(46, 46, 52)

/* ------------------------------------------------------------------ state --- */
static int data_ok;                      /* the resource region holds this build's tables */
static int halted;                       /* app_panic: the engine stopped until the next frame */
static int engine_ok;
static int cpu2_ok;
static int save_hold, dirty;

static struct {
    int page;                            /* the page shown (engine.h: 23 of them) */
    int matrix, global;                  /* the MATRIX page, the GLO page, over the page */
    int track;                           /* the active track */
    int mod;                             /* MOD mode */
    int msel;                            /* MATRIX: the cable chosen (0..7 the track's, 8..15 the global ones) */
    int octave;                          /* the keyboard's */
    uint32_t keys, btn;
    uint32_t down_at[NB];
    int long_done[NB];
    uint32_t touched_at[4];
    int touched_k;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t drawn_at, panel_at;
    int full;                            /* everything again */
    /* what each part of the screen showed, to draw only what changed */
    int32_t knob_shown[4][4];
    int32_t head_shown[5], foot_shown[6], panel_shown[3];
} ui;

/* ------------------------------------------------------------ the pages --- */
static int page_face(int page)           /* which face of its module */
{
    switch (eng_page_type(page)) {
    case PT_PLAITS2: case PT_TIDES2: case PT_MARB_T2: case PT_MARB_X2: case PT_FX2: return 1;
    case PT_PLAITS3: return 2;
    default: return 0;
    }
}
static int page_count(int page)          /* its module's faces */
{
    int pt = eng_page_type(page);
    return pt <= PT_PLAITS3 ? 3 : pt == PT_MIX ? 1 : 2;
}
static int page_first(int page) { return page - page_face(page); }

static const char *module_name(int page)
{
    switch (eng_page_type(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: return "Plaits";
    case PT_TIDES1: case PT_TIDES2: return page >= PG_TIDES(1, 0) ? "Tides B" : "Tides A";
    case PT_MARB_T1: case PT_MARB_T2: return "Marbles t";
    case PT_MARB_X1: case PT_MARB_X2: return "Marbles X";
    case PT_MIX: return "Mixer";
    default: return "Reverb";
    }
}

static uint16_t module_colour(int page)
{
    project_t *p = eng_project();
    switch (eng_page_type(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: {
        int m = p->algo[page_first(page)];   /* Plaits' banks: the 1.2 models, the pitched, the noisy and drums */
        return m < 8 ? C_ORANGE : m < 16 ? C_GREEN : C_RED;
    }
    case PT_TIDES1: case PT_TIDES2: return C_GREEN;
    case PT_MARB_T1: case PT_MARB_T2: return C_GREEN;
    case PT_MARB_X1: case PT_MARB_X2: return C_YELLOW;
    case PT_MIX: return C_HI;
    default: return C_BLUE;
    }
}

/* ------------------------------------------------------------------ text --- */
static void text_cat(char *d, const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (*d && n < max - 1) {
        d++;
        n++;
    }
    while (*s && n < max - 1) {
        *d++ = *s++;
        n++;
    }
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void note_text(int note, char *b)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[8];
    if (note < 0)
        note = 0;
    b[0] = 0;
    text_cat(b, N[note % 12], 8);
    int_text(note / 12 - 1, o);
    text_cat(b, o, 8);
}

static int text_hash(const char *s)
{
    int h = 0;
    while (*s)
        h = h * 31 + *s++;
    return h;
}

/* a parameter's short name, for the matrix: its module, then its own */
static void param_name(int param, char *b, uint32_t max)
{
    int page = param / 4, t = eng_page_track(page);
    char n[4];
    b[0] = 0;
    if (t >= 0) {
        text_cat(b, "P", max);
        int_text(t + 1, n);
        text_cat(b, n, max);
    } else if (page < PG_MARBLES(0)) {
        text_cat(b, page >= PG_TIDES(1, 0) ? "TB" : "TA", max);
    } else if (page < PG_MIX) {
        text_cat(b, "Mb", max);
    } else if (page == PG_MIX) {
        text_cat(b, "Mix", max);
    } else {
        text_cat(b, "Rv", max);
    }
    text_cat(b, " ", max);
    text_cat(b, eng_pdef(param)->name, max);
}

/* --------------------------------------------------------------- tables --- */
static int data_check(void)
{
    uint32_t len = 0;
    const uint8_t *p = plat_res(&len);
    if (!p || len != MIFM1_RES_LEN)
        return 0;
#ifdef PLAT_HOST
    return mi_res_check(p, len) == -1;
#else
    return p == (const uint8_t *)mi_res_where();
#endif
}

/* ---------------------------------------------------------------- store --- */
static void project_load(void)
{
    static project_t tmp;
    project_t *p = eng_project();
    if (plat_store_load(OBJ_PROJECT, &tmp, sizeof tmp) == (int)sizeof tmp && eng_check(&tmp))
        *p = tmp;
    else
        eng_defaults(p);
    dirty = 0;
}

static int project_save(void)
{
    if (save_hold)
        return -1;
    if (plat_store_save(OBJ_PROJECT, eng_project(), sizeof(project_t)) != 0)
        return -1;
    dirty = 0;
    return 0;
}

int app_backup_playing(void) { return eng_playing(); }
void app_backup_flush(void)
{
    if (dirty && !eng_playing())
        project_save();
}
void app_backup_hold(int on) { save_hold = on; }
void app_backup_restored(void)
{
    project_load();
    ui.full = 1;
}

/* ---------------------------------------------------------------- audio --- */
static float out_l[APP_BLOCK], out_r[APP_BLOCK];

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float g = (float)master_q12 * (8300000.0f / 4096.0f);
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    if (halted || !data_ok || !engine_ok) {
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    eng_render(out_l, out_r, n);
    for (i = 0; i < n; i++) {
        lr[2 * i] = (int32_t)(fm_clampf(out_l[i], -1.0f, 1.0f) * g);
        lr[2 * i + 1] = (int32_t)(fm_clampf(out_r[i], -1.0f, 1.0f) * g);
    }
}

void app_panic(void)                     /* an update, a restore or the tables' transfer: nothing reads the flash */
{
    halted = 1;
}

void app_init(void)
{
    cpu2_ok = plat_cpu2_set(1) == 0;     /* the one time it may start: here, at power-on */
    data_ok = data_check();
    project_load();
    engine_ok = data_ok && eng_init() == 0;   /* (not before the tables: the modules' set-up may read them) */
    ui.track = 0;
    ui.page = PG_TRACK(0, 0);
    ui.touched_k = -1;
}

/* -------------------------------------------------------------- drawing --- */
void app_say(const char *a, const char *b)
{
    ui.msg[0][0] = ui.msg[1][0] = 0;
    text_cat(ui.msg[0], a ? a : "", sizeof ui.msg[0]);
    text_cat(ui.msg[1], b ? b : "", sizeof ui.msg[1]);
    ui.msg_until = plat_ms() + SHOW_MS;
    ui.foot_shown[0] = -1;
}

static int load_now(void)                /* the busier core, in % */
{
    uint32_t a = eng_load(0), b = eng_load(1);
    return (int)(((a > b ? a : b) + 5u) / 10u);
}

static void fill_circle(int cx, int cy, int r, uint16_t c)
{
    for (int y = -r; y <= r; y++) {
        int w = (int)fm_sqrtf((float)(r * r - y * y) + 0.5f);
        cv_rect(cx - w, cy + y, 2 * w + 1, 1, c);
    }
}

static void draw_header(void)
{
    static int load = -1;
    static uint32_t load_at;
    char t[32], v[12];
    int tr = eng_page_track(ui.page);
    if (load < 0 || plat_ms() - load_at >= 250u) {
        load = load_now();
        load_at = plat_ms();
    }
    int32_t s[5] = {ui.page | ui.matrix << 8 | ui.global << 9 | ui.mod << 10, load, eng_playing() | dirty << 1,
                    ui.track, module_colour(ui.page)};
    if (!ui.full && !memcmp(s, ui.head_shown, sizeof s))
        return;
    memcpy(ui.head_shown, s, sizeof s);
    cv_begin(240, HEAD_H, C_BLACK);
    t[0] = 0;
    text_cat(t, ui.global ? "Global" : ui.matrix ? "Matrix" : module_name(ui.page), sizeof t);
    uint16_t mc = ui.global || ui.matrix ? C_HI : module_colour(ui.page);
    int x = cv_text(6, 3, &FONT_B, t, mc);
    if (!ui.global && !ui.matrix && page_count(ui.page) > 1) {   /* the faces: a dot each, the one shown filled */
        for (int f = 0; f < page_count(ui.page); f++) {
            int fx = x + 4 + f * 10;
            if (f == page_face(ui.page))
                cv_rect(fx, 9, 6, 6, mc);
            else {
                cv_rect(fx, 9, 6, 1, C_DIM);
                cv_rect(fx, 14, 6, 1, C_DIM);
                cv_rect(fx, 9, 1, 6, C_DIM);
                cv_rect(fx + 5, 9, 1, 6, C_DIM);
            }
        }
    }
    if (tr >= 0 || ui.matrix) {          /* the track, as a badge */
        cv_rect(138, 3, 24, 16, C_LINE);
        t[0] = 'T';
        t[1] = (char)('1' + ui.track);
        t[2] = 0;
        cv_text(150 - text_w(&FONT_XS, t) / 2, 3, &FONT_XS, t, C_WHITE);
    }
    if (eng_playing())                   /* a play triangle */
        for (int y = 0; y < 11; y++)
            cv_rect(172, 6 + y, y < 6 ? y + 1 : 11 - y, 1, C_GREEN);
    t[0] = 0;
    int_text(load, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "%", sizeof t);
    cv_text(234 - text_w(&FONT_XS, t), 3, &FONT_XS, t, load >= 80 ? C_RED : load >= 60 ? C_ORANGE : C_GRAY);
    if (dirty)
        cv_rect(1, 1, 3, 3, C_ORANGE);
    cv_rect(0, HEAD_H - 1, 240, 1, ui.mod ? C_WHITE : C_LINE);
    cv_blit(0, 0);
}

/* the module's own display -------------------------------------------------------------------------------- */

static void panel_plaits_bank(int model, uint16_t mc)   /* the model's name, and Plaits' LEDs: 3 banks of 8 */
{
    const char *name = eng_algo_name(PG_TRACK(ui.track, 0), model);
    const felucca_font_t *f = text_w(&FONT_M, name) <= 232 ? &FONT_M : &FONT_B;
    cv_text(120 - text_w(f, name) / 2, f == &FONT_M ? 2 : 6, f, name, C_WHITE);
    for (int b = 0; b < 3; b++) {
        uint16_t c = b == 0 ? C_ORANGE : b == 1 ? C_GREEN : C_RED;
        for (int i = 0; i < 8; i++) {
            int x = 52 + i * 17, y = 36 + b * 15, on = model == b * 8 + i;
            if (on) {
                cv_rect(x - 2, y - 2, 15, 12, C_LINE);
                cv_rect(x, y, 11, 8, c);
            } else {
                cv_rect(x, y, 11, 8, C_KNOB);
                cv_rect(x + 4, y + 3, 3, 2, b == model / 8 ? c : C_DIM);
            }
        }
    }
    (void)mc;
}

static void panel_plaits_lpg(int t, uint16_t mc)    /* the low-pass gate's response: DECAY's tail, COLOUR's tone */
{
    int pg = PG_TRACK(t, 1);
    float decay = eng_final(pg * 4 + 0), colour = eng_final(pg * 4 + 1);
    float k = 0.002f + 0.12f * (1.0f - decay) * (1.0f - decay) * (1.0f - decay);   /* the fall a pixel */
    int y0 = 74, h = 56, px = -1, py = 0;
    cv_text(6, 2, &FONT_XS, "Low-pass gate", C_GRAY);
    cv_rect(20, y0, 210, 1, C_LINE);
    for (int x = 0; x < 200; x++) {
        float env = x < 4 ? (float)x / 4.0f : fm_exp2f(-(float)(x - 4) * k * 1.4427f);
        int y = y0 - (int)(env * (float)h);
        uint16_t c = colour > 0.66f ? C_HI : colour > 0.33f ? mc : C_ORANGE;
        if (px >= 0)
            cv_line(24 + px, py, 24 + x, y, c);
        if (!(x & 3))
            cv_rect(24 + x, y + 1, 1, y0 - y - 1, C_KNOB);
        px = x;
        py = y;
    }
}

static void panel_plaits_route(int t, uint16_t mc) /* where the track's gate and note come from */
{
    project_t *p = eng_project();
    char a[32], n[8];
    int pg = PG_TRACK(t, 2);
    a[0] = 0;
    text_cat(a, "Gate  <  ", sizeof a);
    text_cat(a, eng_pdef(pg * 4)->names[(int)(p->val[pg * 4] + 0.5f)], sizeof a);
    cv_text(20, 10, &FONT_B, a, C_WHITE);
    a[0] = 0;
    text_cat(a, "Note  <  ", sizeof a);
    text_cat(a, eng_pdef(pg * 4 + 1)->names[(int)(p->val[pg * 4 + 1] + 0.5f)], sizeof a);
    cv_text(20, 34, &FONT_B, a, C_WHITE);
    note_text(eng_note(t), n);
    cv_text(234 - text_w(&FONT_M, n), 24, &FONT_M, n, (eng_gates() >> t) & 1u ? mc : C_GRAY);
    cv_text(20, 60, &FONT_XS, "Transpose and octave on the knobs", C_DIM);
}

static void trace(int src, int x0, int y0, int h, int bipolar, uint16_t pos, uint16_t neg)
{                                        /* a source's last 1.4 s in a band h tall, its middle (or its foot) at y0 */
    int py = 0;
    for (int i = 0; i < ENG_HIST; i++) {
        int v = eng_hist(src, i);
        int y = bipolar ? y0 - v * (h / 2) / 127 : y0 - v * h / 127;
        uint16_t c = v >= 0 ? pos : neg;
        if (i)
            cv_line(x0 + i - 1, py, x0 + i, y, c);
        py = y;
    }
}

static void panel_tides(int tide, uint16_t mc)
{
    project_t *p = eng_project();
    int ramp = p->algo[PG_TIDES(tide, 0)], src0 = tide ? SRC_B1 : SRC_A1;
    static const char *const R[3] = {"AD", "Looping", "AR"};
    for (int c = 0; c < 4; c++) {        /* the four outputs, as Tides' four LEDs and their voltages */
        int y = 4 + c * 19, mid = ramp == 1 ? y + 9 : y + 17;
        char n[2] = {(char)('1' + c), 0};
        int v = eng_hist(src0 + c, ENG_HIST - 1);
        cv_rect(6, y + 4, 9, 9, v > 8 ? C_GREEN : v < -8 ? C_RED : C_KNOB);
        cv_text(20, y + 1, &FONT_XS, n, C_GRAY);
        cv_rect(34, mid, ENG_HIST, 1, C_LINE);
        trace(src0 + c, 34, mid, 16, ramp == 1, C_GREEN, C_RED);
    }
    for (int i = 0; i < 3; i++) {        /* the ramp modes, Tides' mode LEDs */
        int y = 10 + i * 22, on = ramp == i;
        fill_circle(170, y + 7, 4, on ? mc : C_KNOB);
        cv_text(180, y, &FONT_XS, R[i], on ? C_WHITE : C_DIM);
    }
}

static void panel_marbles_t(uint16_t mc)
{
    project_t *p = eng_project();
    const char *m = eng_algo_name(PG_MARBLES(0), p->algo[PG_MARBLES(0)]);
    cv_text(6, 1, &FONT_B, m, C_WHITE);
    for (int c = 0; c < 3; c++) {        /* t1 t2 t3: their gates, as Marbles' LEDs and a strip of the last steps */
        int y = 24 + c * 19;
        char n[3] = {'t', (char)('1' + c), 0};
        int on = eng_hist(SRC_T1 + c, ENG_HIST - 1) > 64;
        cv_rect(6, y + 4, 9, 9, on ? mc : C_KNOB);
        cv_text(20, y + 1, &FONT_XS, n, C_GRAY);
        for (int i = 0; i < ENG_HIST; i++)
            if (eng_hist(SRC_T1 + c, i) > 64)
                cv_rect(40 + i * 3 / 2, y + 4, 2, 10, c == 1 ? C_HI : mc);
            else if (!(i & 7))
                cv_rect(40 + i * 3 / 2, y + 8, 1, 2, C_LINE);
    }
}

static void panel_marbles_x(uint16_t mc)
{
    project_t *p = eng_project();
    const char *s = eng_algo_name(PG_MARBLES(2), p->algo[PG_MARBLES(2)]);
    static const char *const N[4] = {"X1", "X2", "X3", "Y"};
    cv_text(6, 1, &FONT_B, s, C_WHITE);
    for (int c = 0; c < 4; c++) {        /* the voltages, stepped as they are drawn */
        int y = 24 + c * 14;
        cv_text(6, y - 2, &FONT_XS, N[c], C_GRAY);
        cv_rect(30, y + 12, 200, 1, C_LINE);
        int py = 0;
        for (int i = 0; i < ENG_HIST; i++) {
            int v = eng_hist(SRC_X1 + c, i), yy = y + 12 - v * 11 / 127;
            if (i && yy != py)
                cv_rect(30 + (i * 5) / 3, py < yy ? py : yy, 1, (py < yy ? yy - py : py - yy) + 1, C_DIM);
            cv_rect(30 + (i * 5) / 3, yy, 2, 1, c == 3 ? C_HI : mc);
            py = yy;
        }
    }
}

static void panel_mixer(void)
{
    for (int t = 0; t < ENG_TRACKS; t++) {   /* a meter under each level knob */
        int x = t * KNOB_W + 22, h = 66;
        float m = eng_meter(t);
        int lit = (int)(fm_clampf(m * 1.25f, 0.0f, 1.0f) * (float)(h / 4));
        for (int k = 0; k < h / 4; k++) {
            uint16_t c = k >= lit ? C_KNOB : k > h / 4 - 3 ? C_RED : k > h / 4 - 6 ? C_ORANGE : C_GREEN;
            cv_rect(x, 72 - k * 4, 16, 3, c);
        }
    }
}

static void panel_reverb(void)          /* the tail as Clouds' reverb would leave it: TIME long, DAMPING dark */
{
    float time = eng_final(PG_FX(1) * 4 + 0), damp = eng_final(PG_FX(1) * 4 + 1);
    float ret = eng_final(PG_FX(1) * 4 + 3), m = eng_meter(ENG_TRACKS);
    float k = 0.06f - 0.054f * time;
    cv_text(6, 1, &FONT_XS, "Clouds reverb, sends post-fader", C_GRAY);
    for (int i = 0; i < 52; i++) {
        float a = fm_exp2f(-(float)i * k * 4.0f * 1.4427f) * (0.4f + 0.6f * ret);
        int h = (int)(a * 50.0f);
        uint16_t c = (float)i / 52.0f > 1.0f - damp * 0.8f ? C_LINE : i & 1 ? C_BLUE : C_HI;
        cv_rect(10 + i * 4, 74 - h, 2, h, c);
    }
    cv_rect(230, 74 - (int)(fm_clampf(m * 2.0f, 0.0f, 1.0f) * 56.0f), 4,
            (int)(fm_clampf(m * 2.0f, 0.0f, 1.0f) * 56.0f), C_BLUE);
}

static void draw_panel(void)
{
    project_t *p = eng_project();
    int pt = eng_page_type(ui.page), live = 0, tr = eng_page_track(ui.page);
    uint32_t now = plat_ms();
    uint16_t mc = module_colour(ui.page);
    int32_t s[3] = {ui.page, eng_algo_count(page_first(ui.page)) ? p->algo[page_first(ui.page)] : -1, 0};
    switch (pt) {
    case PT_PLAITS2: s[2] = (int)(eng_final(ui.page * 4) * 200.0f) | (int)(eng_final(ui.page * 4 + 1) * 3.0f) << 8; break;
    case PT_PLAITS3: s[2] = eng_note(tr) | (int)((eng_gates() >> tr) & 1u) << 8 | (int)p->val[ui.page * 4] << 9 |
                            (int)p->val[ui.page * 4 + 1] << 14; break;
    case PT_TIDES1: case PT_TIDES2: case PT_MARB_T1: case PT_MARB_T2: case PT_MARB_X1: case PT_MARB_X2:
    case PT_MIX: case PT_FX1: case PT_FX2: live = 1; break;
    default: break;
    }
    if (!ui.full && !memcmp(s, ui.panel_shown, sizeof s) && (!live || now - ui.panel_at < 80u))
        return;
    memcpy(ui.panel_shown, s, sizeof s);
    ui.panel_at = now;
    cv_begin(240, PANEL_H, C_BLACK);
    switch (pt) {
    case PT_PLAITS1: panel_plaits_bank(p->algo[page_first(ui.page)], mc); break;
    case PT_PLAITS2: panel_plaits_lpg(tr, mc); break;
    case PT_PLAITS3: panel_plaits_route(tr, mc); break;
    case PT_TIDES1: case PT_TIDES2: panel_tides(ui.page >= PG_TIDES(1, 0), mc); break;
    case PT_MARB_T1: case PT_MARB_T2: panel_marbles_t(mc); break;
    case PT_MARB_X1: case PT_MARB_X2: panel_marbles_x(mc); break;
    case PT_MIX: panel_mixer(); break;
    default: panel_reverb(); break;
    }
    cv_blit(0, PANEL_Y);
}

/* the knobs ------------------------------------------------------------------------------------------------ */

/* the cables on a parameter: their count, a hash (what changed), their names in b */
static int cables_on(int param, char *b, uint32_t max, int *hash)
{
    project_t *p = eng_project();
    int n = 0, h = 0;
    if (b)
        b[0] = 0;
    for (int g = 0; g <= ENG_TRACKS; g++)
        for (int c = 0; c < ENG_CABLES; c++) {
            const cable_t *k = &p->cable[g][c];
            if (!k->depth || k->src == SRC_NONE || k->dst != param)
                continue;
            n++;
            h = h * 31 + k->src * 211 + k->depth;
            if (b && n <= 2) {
                char d[8];
                if (b[0])
                    text_cat(b, " ", max);
                text_cat(b, eng_src_name(k->src), max);
                int_text(k->depth, d);
                if (k->depth > 0)
                    text_cat(b, "+", max);
                text_cat(b, d, max);
            }
        }
    if (b && n > 2)
        text_cat(b, "..", max);
    if (hash)
        *hash = h;
    return n;
}

static float knob_u(int param, float v)  /* a value's place on its knob, 0..1 */
{
    const eng_pdef_t *d = eng_pdef(param);
    if (d->kind == K_BIP)
        return (v + 1.0f) * 0.5f;
    if (d->kind == K_UNI)
        return v;
    return d->hi > d->lo ? (v - (float)d->lo) / (float)(d->hi - d->lo) : 0.0f;
}

#define DIAL_X 30
#define DIAL_Y 44
static void dial_xy(float u, float r, int *x, int *y)   /* 0..1 over 270 degrees, from bottom left, clockwise */
{
    float a = (225.0f - 270.0f * u) * (3.14159265f / 180.0f);
    *x = DIAL_X + (int)(r * fm_cosf(a) + (r * fm_cosf(a) >= 0 ? 0.5f : -0.5f));
    *y = DIAL_Y - (int)(r * fm_sinf(a) + (r * fm_sinf(a) >= 0 ? 0.5f : -0.5f));
}

static void draw_dial(float u, float uf, int bip, int has_mod, uint16_t mc)
{
    float lo = bip ? (u < 0.5f ? u : 0.5f) : 0.0f, hi = bip ? (u > 0.5f ? u : 0.5f) : u;
    fill_circle(DIAL_X, DIAL_Y, 15, C_KNOB);           /* the knob */
    for (int i = 0; i <= 180; i++) {                   /* its scale: lit from the start (the middle) to the value */
        float s = (float)i / 180.0f;
        uint16_t c = s >= lo - 0.003f && s <= hi + 0.003f ? mc : C_LINE;
        for (int r = 18; r <= 21; r++) {
            int x, y;
            dial_xy(s, (float)r, &x, &y);
            cv_rect(x, y, 1, 1, c);
        }
    }
    {
        int x, y;                                      /* its pointer */
        dial_xy(u, 13.0f, &x, &y);
        cv_line(DIAL_X, DIAL_Y, x, y, C_WHITE);
        cv_line(DIAL_X + 1, DIAL_Y, x + 1, y, C_WHITE);
    }
    if (has_mod) {                                     /* where the cables take it */
        int x, y;
        dial_xy(uf, 25.0f, &x, &y);
        cv_rect(x - 1, y - 1, 3, 3, C_WHITE);
    }
}

static void draw_knob(int k)
{
    project_t *p = eng_project();
    int param = ui.page * 4 + k;
    const eng_pdef_t *d = eng_pdef(param);
    float v = p->val[param], f = eng_final(param);
    char t[32], c[32];
    int hash = 0, hot = ui.touched_k == k && plat_ms() - ui.touched_at[k] < 1200u;
    int ncab = cables_on(param, c, sizeof c, &hash);
    uint16_t mc = module_colour(ui.page);
    eng_param_text(param, ncab ? f : v, t, sizeof t);
    int32_t s[4] = {(int32_t)(knob_u(param, v) * 270.0f) | (int32_t)(knob_u(param, f) * 270.0f) << 10,
                    text_hash(t), hash, hot | ui.mod << 1 | mc << 2};
    if (!ui.full && !memcmp(s, ui.knob_shown[k], sizeof s))
        return;
    memcpy(ui.knob_shown[k], s, sizeof s);
    cv_begin(KNOB_W, KNOB_H, C_BLACK);
    cv_text(30 - text_w(&FONT_XS, d->name) / 2, 1, &FONT_XS, d->name, hot ? C_WHITE : C_GRAY);
    if (d->kind == K_ENUM) {                           /* a switch: its position written, its choices as dots */
        int n = d->hi - d->lo + 1, i = (int)(v + 0.5f) - d->lo;
        cv_rect(4, 30, 52, 26, C_KNOB);
        const felucca_font_t *ft = text_w(&FONT_XS, t) <= 50 ? &FONT_XS : &FONT_T;
        cv_text(30 - text_w(ft, t) / 2, ft == &FONT_XS ? 35 : 37, ft, t, C_WHITE);
        int w = n > 8 ? 4 : 6;
        for (int j = 0; j < n; j++)
            cv_rect(30 - n * w / 2 + j * w, 62, w - 2, 3, j == i ? mc : C_LINE);
    } else {
        draw_dial(knob_u(param, v), knob_u(param, f), d->kind == K_BIP || (d->kind == K_INT && d->lo < 0), ncab, mc);
        cv_text(30 - text_w(&FONT_XS, t) / 2, 68, &FONT_XS, t, ncab ? mc : C_WHITE);
    }
    if (ncab)
        cv_text(30 - text_w(&FONT_T, c) / 2, 86, &FONT_T, c, C_AMB);
    else if (ui.mod && eng_param_mod(param))
        cv_text(30 - text_w(&FONT_T, "FREE") / 2, 86, &FONT_T, "FREE", C_LINE);
    if (k)
        cv_rect(0, 8, 1, KNOB_H - 16, C_LINE);
    cv_blit((uint32_t)(k * KNOB_W), KNOB_Y);
}

/* lists: the matrix, the global page, the missing tables --------------------------------------------------- */
static void draw_lines(int y, int h, const char *const *lines, const uint16_t *colours, int n, int sel)
{
    for (int i = 0; i < n; i++) {
        cv_begin(240, (uint32_t)h, i == sel ? C_LINE : C_BLACK);
        cv_text(6, (h - 15) / 2, &FONT_XS, lines[i], colours[i]);
        cv_blit(0, (uint32_t)(y + i * h));
    }
}

static void draw_matrix(void)
{
    static int shown = -1;
    project_t *p = eng_project();
    char rows[8][40];
    const char *lines[8];
    uint16_t col[8];
    int g = ui.msel < 8 ? ui.track : ENG_TRACKS, first = ui.msel < 8 ? 0 : 8, h = 0;
    for (int i = 0; i < 8; i++) {
        const cable_t *k = &p->cable[g][i];
        char n[24], d[8];
        h = h * 33 + k->src * 7 + k->dst * 131 + k->depth;
        rows[i][0] = 0;
        int_text(first + i + 1, d);
        text_cat(rows[i], d, 40);
        text_cat(rows[i], first + i + 1 < 10 ? "    " : "  ", 40);
        if (!k->depth || k->src == SRC_NONE) {
            text_cat(rows[i], "-", 40);
            col[i] = C_DIM;
        } else {
            text_cat(rows[i], eng_src_name(k->src), 40);
            text_cat(rows[i], "  >  ", 40);
            param_name(k->dst, n, sizeof n);
            text_cat(rows[i], n, 40);
            text_cat(rows[i], "   ", 40);
            int_text(k->depth, d);
            if (k->depth > 0)
                text_cat(rows[i], "+", 40);
            text_cat(rows[i], d, 40);
            col[i] = C_HI;
        }
        if (first + i == ui.msel)
            col[i] = C_WHITE;
        lines[i] = rows[i];
    }
    h = h * 17 + ui.msel + g * 1000;
    if (!ui.full && h == shown)
        return;
    shown = h;
    cv_begin(240, 24, C_BLACK);
    cv_text(6, 1, &FONT_XS, g < ENG_TRACKS ? "The track's cables" : "Global cables (Tides, Marbles, reverb)", C_GRAY);
    cv_text(6, 13, &FONT_T, "K1 CABLE   K2 SOURCE   K3 TARGET   K4 DEPTH", C_DIM);
    cv_blit(0, PANEL_Y);
    draw_lines(PANEL_Y + 24, 19, lines, col, 8, ui.msel - first);
    cv_begin(240, FOOT_Y - (PANEL_Y + 24 + 8 * 19), C_BLACK);
    cv_blit(0, PANEL_Y + 24 + 8 * 19);
}

static void draw_global(void)
{
    static uint32_t at;
    char rows[9][40], v[12];
    const char *lines[9];
    uint16_t col[9];
    if (!ui.full && plat_ms() - at < 400u)
        return;
    at = plat_ms();
    for (int i = 0; i < 9; i++) {
        rows[i][0] = 0;
        lines[i] = rows[i];
        col[i] = C_GRAY;
    }
    text_cat(rows[0], "Core 0: tracks 1-2, Marbles, Tides  ", 40);
    int_text((int32_t)((eng_load(0) + 5u) / 10u), v);
    text_cat(rows[0], v, 40);
    text_cat(rows[0], "%", 40);
    col[0] = C_HI;
    text_cat(rows[1], "Core 1: tracks 3-4  ", 40);
    if (eng_core1_running()) {
        int_text((int32_t)((eng_load(1) + 5u) / 10u), v);
        text_cat(rows[1], v, 40);
        text_cat(rows[1], "%", 40);
        col[1] = C_HI;
    } else {
        text_cat(rows[1], cpu2_ok ? "paused" : "off: all on core 0", 40);
        col[1] = C_ORANGE;
    }
    text_cat(rows[2], "Peak over a second  ", 40);
    int_text((int32_t)((eng_peak() + 5u) / 10u), v);
    text_cat(rows[2], v, 40);
    text_cat(rows[2], "%", 40);
    col[2] = eng_peak() >= 900u ? C_RED : C_GRAY;
    text_cat(rows[3], "Platform (audio)  ", 40);
    int_text((int32_t)plat_cpu_pct(), v);
    text_cat(rows[3], v, 40);
    text_cat(rows[3], "%", 40);
    text_cat(rows[4], data_ok ? "Module tables: OK" : "Module tables: MISSING", 40);
    col[4] = data_ok ? C_GRAY : C_RED;
    text_cat(rows[5], "Core 1's stack  ", 40);
    int_text((int32_t)plat_cpu2_stack(), v);
    text_cat(rows[5], v, 40);
    text_cat(rows[5], " / 2816 B", 40);
    text_cat(rows[6], "MIFM1 ", 40);
    text_cat(rows[6], APP_VERSION, 40);
    text_cat(rows[6], dirty ? "  (not saved)" : "", 40);
    text_cat(rows[7], "SAVE held: save (stopped)", 40);
    col[7] = C_DIM;
    draw_lines(PANEL_Y + 4, 19, lines, col, 8, -1);
}

static void draw_missing(void)
{
    static const char *const L[8] = {"", "MODULE TABLES MISSING", "", "The installer writes them after",
                                     "the firmware: install again from", "the page, the FM-1 stopped.", "", ""};
    uint16_t col[8];
    if (!ui.full)
        return;
    for (int i = 0; i < 8; i++)
        col[i] = i == 1 ? C_RED : C_GRAY;
    draw_lines(PANEL_Y + 4, 19, L, col, 8, -1);
}

static void draw_footer(void)
{
    char t[40], n[8];
    uint32_t now = plat_ms(), g = eng_gates();
    int msg = now < ui.msg_until;
    int32_t s[6] = {(int32_t)g, msg, ui.track | ui.mod << 4, eng_note(0) | eng_note(1) << 8 | eng_note(2) << 16,
                    eng_note(3) | (int32_t)(ui.keys & 0xFFFFu) << 8, ui.octave};
    if (!ui.full && !memcmp(s, ui.foot_shown, sizeof s))
        return;
    memcpy(ui.foot_shown, s, sizeof s);
    cv_begin(240, FOOT_H, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    if (msg) {
        t[0] = 0;
        text_cat(t, ui.msg[0], sizeof t);
        text_cat(t, "  ", sizeof t);
        text_cat(t, ui.msg[1], sizeof t);
        cv_text(120 - text_w(&FONT_B, t) / 2, 10, &FONT_B, t, C_WHITE);
    } else if (ui.mod) {
        cv_text(6, 3, &FONT_XS, "MOD: hold a source key, turn a knob", C_WHITE);
        cv_text(6, 19, &FONT_T, "KEYS 1-4 A  5-8 B  9-11 X  12 Y  13-15 T", C_GRAY);
    } else {
        for (int tr = 0; tr < ENG_TRACKS; tr++) {   /* the tracks: a gate LED, the number, the note */
            int x = tr * 60, on = (g >> tr) & 1u, act = tr == ui.track;
            n[0] = (char)('1' + tr);
            n[1] = 0;
            fill_circle(x + 11, 17, 5, on ? C_GREEN : C_KNOB);
            cv_text(x + 21, 9, &FONT_B, n, act ? C_WHITE : C_GRAY);
            note_text(eng_note(tr), t);
            cv_text(x + 31, 10, &FONT_XS, t, act ? C_HI : C_DIM);
            if (act)
                cv_rect(x + 4, 32, 52, 2, C_WHITE);
        }
    }
    cv_blit(0, FOOT_Y);
}

static void draw(void)
{
    if (ui.full)
        lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_header();
    if (!data_ok || !engine_ok) {
        draw_missing();
    } else if (ui.global) {
        draw_global();
    } else if (ui.matrix) {
        draw_matrix();
    } else {
        draw_panel();
        for (int k = 0; k < 4; k++)
            draw_knob(k);
    }
    draw_footer();
    ui.full = 0;
}

/* ---------------------------------------------------------------- input --- */
static void go_page(int page)
{
    ui.page = page;
    ui.touched_k = -1;
    ui.matrix = ui.global = 0;
    ui.full = 1;
}

static void edit_param(int param, int32_t e)
{
    project_t *p = eng_project();
    const eng_pdef_t *d = eng_pdef(param);
    float v = p->val[param];
    int32_t a = e > 1 || e < -1 ? e * 3 : e;    /* a fast turn goes further */
    if (d->kind == K_UNI)
        v = fm_clampf(v + 0.01f * (float)a, 0.0f, 1.0f);
    else if (d->kind == K_BIP)
        v = fm_clampf(v + 0.02f * (float)a, -1.0f, 1.0f);
    else
        v = (float)clamp((int32_t)(v + (v < 0 ? -0.5f : 0.5f)) + e, d->lo, d->hi);
    p->val[param] = v;
    dirty = 1;
}

/* the group a parameter's cables live in: its track's, or the global one */
static int cable_group(int param)
{
    int t = eng_page_track(param / 4);
    if (t >= 0)
        return t;
    if (param / 4 == PG_MIX || param / 4 == PG_FX(0))
        return param % 4;                /* a track's level and send: that track's cables */
    return ENG_TRACKS;
}

/* MOD mode: source s on parameter param, its depth moved by e */
static void cable_turn(int s, int param, int32_t e)
{
    project_t *p = eng_project();
    int g = cable_group(param), free = -1;
    cable_t *k = 0;
    if (!eng_param_mod(param)) {
        app_say("NOT A KNOB", "A CABLE CAN MOVE");
        return;
    }
    for (int c = 0; c < ENG_CABLES; c++) {
        cable_t *q = &p->cable[g][c];
        if (q->depth && q->src == s && q->dst == param)
            k = q;
        else if ((!q->depth || q->src == SRC_NONE) && free < 0)
            free = c;
    }
    if (!k) {
        if (free < 0) {
            app_say("8 CABLES MAX", g < ENG_TRACKS ? "ON THIS TRACK" : "GLOBAL");
            return;
        }
        k = &p->cable[g][free];
        k->src = (uint8_t)s;
        k->dst = (uint8_t)param;
        k->depth = 0;
    }
    int32_t d = clamp((int32_t)k->depth + e * (e > 1 || e < -1 ? 10 : 4), -100, 100);
    if (d == 0) {
        k->src = SRC_NONE;
        k->depth = 0;
    } else {
        k->depth = (int8_t)d;
    }
    dirty = 1;
}

static int held_source(void)             /* MOD mode: the source key held (1..16), 17 / 18 the fans, 0 none */
{
    for (int k = 0; k < 18; k++)
        if (ui.keys & (1u << k))
            return k + 1;
    return 0;
}

static int key_source(int key)           /* the spec's row: 1-4 A1-A4, 5-8 B1-B4, 9-11 X1-X3, 12 Y, 13-15 T1-T3 */
{
    return key >= 1 && key <= 15 ? SRC_A1 + key - 1 : SRC_NONE;
}

/* the module buttons: their first page, and where a second press goes */
static void module_button(int b)
{
    int pt = eng_page_type(ui.page), here = !ui.matrix && !ui.global;
    switch (b) {
    case B_EDIT:
        if (here && pt <= PT_PLAITS3)
            go_page(PG_TRACK(ui.track, (page_face(ui.page) + 1) % 3));
        else
            go_page(PG_TRACK(ui.track, 0));
        break;
    case B_LFO:
        if (here && (pt == PT_TIDES1 || pt == PT_TIDES2))
            go_page(ui.page >= PG_TIDES(1, 0) ? PG_TIDES(0, 0) : PG_TIDES(1, 0));
        else
            go_page(PG_TIDES(0, 0));
        break;
    case B_SEQ:
        go_page(here && pt == PT_MARB_T1 ? PG_MARBLES(1) : PG_MARBLES(0));
        break;
    case B_ARP:
        go_page(here && pt == PT_MARB_X1 ? PG_MARBLES(3) : PG_MARBLES(2));
        break;
    case B_HOME:
        go_page(PG_MIX);
        break;
    case B_FX:
        go_page(here && pt == PT_FX1 ? PG_FX(1) : PG_FX(0));
        break;
    case B_GLO:
        ui.global = !ui.global;
        ui.matrix = 0;
        ui.full = 1;
        break;
    case B_ENV:
        app_say("SCENES", "IN V0.3");
        break;
    default:
        break;
    }
}

static void button_down(int b)
{
    switch (b) {
    case B_PLAY:
        eng_play(!eng_playing());
        break;
    case B_OCTDN:
        ui.octave = clamp(ui.octave - 1, -3, 3);
        break;
    case B_OCTUP:
        ui.octave = clamp(ui.octave + 1, -3, 3);
        break;
    case B_SEL:
    case B_SAVE:
        break;                           /* on release (short) or held (long) */
    default:
        module_button(b);
        break;
    }
}

static void button_long(int b)
{
    if (b == B_SEL) {
        ui.matrix = 1;
        ui.global = 0;
        ui.mod = 0;
        ui.msel = 0;
        ui.full = 1;
    } else if (b == B_SAVE) {
        if (eng_playing())
            app_say("STOP FIRST", "THEN SAVE");
        else if (project_save() == 0)
            app_say("PROJECT", "SAVED");
        else
            app_say("COULD NOT", "SAVE");
    }
}

static void button_up(int b)
{
    if (b == B_SEL && !ui.long_done[b]) {
        if (ui.matrix)
            ui.matrix = 0;
        else
            ui.mod = !ui.mod;
        ui.full = 1;
    }
}

static void matrix_turn(int k, int32_t e)
{
    project_t *p = eng_project();
    int g = ui.msel < 8 ? ui.track : ENG_TRACKS;
    cable_t *c = &p->cable[g][ui.msel % 8];
    if (k == 0) {
        ui.msel = clamp(ui.msel + e, 0, 15);
        return;
    }
    if (k == 1) {                        /* the source: none, then the 15 */
        int s = clamp((int)c->src + e, SRC_NONE, NSRC - 1);
        c->src = (uint8_t)s;
        if (s == SRC_NONE)
            c->depth = 0;
        else if (!c->depth)
            c->depth = 50;
    } else if (k == 2) {                 /* the target: the group's parameters a cable may move */
        int d = c->dst, tries = 0;
        do {
            d = (d + (e > 0 ? 1 : ENG_NPARAMS - 1)) % ENG_NPARAMS;
        } while ((!eng_param_mod(d) || cable_group(d) != g) && ++tries < ENG_NPARAMS);
        c->dst = (uint8_t)d;
    } else {
        c->depth = (int8_t)clamp(c->depth + e * 2, -100, 100);
        if (!c->depth)
            c->src = SRC_NONE;
    }
    dirty = 1;
}

static void input(void)
{
    uint32_t now = plat_ms(), btn = plat_buttons(), keys = plat_keys(), k;
    /* buttons: down, held long, up */
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if ((btn & bit) && !(ui.btn & bit)) {
            ui.down_at[k] = now;
            ui.long_done[k] = 0;
            button_down((int)k);
        } else if ((btn & bit) && !ui.long_done[k] && now - ui.down_at[k] >= LONG_MS) {
            ui.long_done[k] = 1;
            button_long((int)k);
        } else if (!(btn & bit) && (ui.btn & bit)) {
            button_up((int)k);
        }
    }
    ui.btn = btn;
    /* keys: the active track, or the sources in MOD mode */
    if (keys != ui.keys) {
        for (k = 0; k < NKEYS; k++) {
            uint32_t bit = 1u << k;
            int note = 53 + (int)k + 12 * ui.octave;
            if (ui.mod)
                continue;
            if ((keys & bit) && !(ui.keys & bit))
                eng_key(ui.track, note, 1);
            else if (!(keys & bit) && (ui.keys & bit))
                eng_key(ui.track, note, 0);
        }
        ui.keys = keys;
    }
    /* encoders */
    {
        int32_t e = plat_enc(EN_PRESET);
        if (e) {
            int t = clamp(ui.track + e, 0, ENG_TRACKS - 1);
            if (t != ui.track) {
                for (k = 0; k < NKEYS; k++)      /* the keyboard's notes stop on the old track */
                    if (ui.keys & (1u << k))
                        eng_key(ui.track, 53 + (int)k + 12 * ui.octave, 0);
                ui.track = t;
                if (eng_page_track(ui.page) >= 0)
                    ui.page = PG_TRACK(t, page_face(ui.page));
                ui.full = 1;
            }
        }
        e = plat_enc(EN_SELECT);
        if (e && !ui.matrix && !ui.global && page_count(ui.page) > 1) {
            int first = page_first(ui.page), n = page_count(ui.page);
            go_page(first + (page_face(ui.page) + (e > 0 ? 1 : n - 1)) % n);
        }
        e = plat_enc(EN_ALGO);
        if (e && !ui.matrix && !ui.global) {
            project_t *p = eng_project();
            int first = page_first(ui.page), n = eng_algo_count(first);
            if (n) {
                p->algo[first] = (uint8_t)clamp((int32_t)p->algo[first] + e, 0, n - 1);
                dirty = 1;
            }
        }
        for (int i = 0; i < 4; i++) {
            e = plat_enc(EN_K1 + i);
            if (!e || !data_ok || ui.global)
                continue;
            if (ui.matrix) {
                matrix_turn(i, e);
                continue;
            }
            int param = ui.page * 4 + i, h = ui.mod ? held_source() : 0;
            if (h >= 1 && h <= 15)
                cable_turn(key_source(h), param, e);
            else if (h == 17 || h == 18)         /* the fans: A n (or B n) on knob n */
                cable_turn((h == 17 ? SRC_A1 : SRC_B1) + i, param, e);
            else
                edit_param(param, e);
            ui.touched_at[i] = now;
            ui.touched_k = i;
        }
    }
    /* lights: the page's button, PLAY, SEL in MOD mode; the keys: held, or the sources on this page */
    {
        uint32_t bb = 0, kk = ui.keys;
        int pt = eng_page_type(ui.page);
        static const uint8_t PAGE_BTN[NPT] = {B_EDIT, B_EDIT, B_EDIT, B_LFO, B_LFO, B_SEQ, B_SEQ, B_ARP, B_ARP,
                                             B_HOME, B_FX, B_FX};
        if (!ui.global && !ui.matrix)
            bb |= 1u << PAGE_BTN[pt];
        if (ui.global)
            bb |= 1u << B_GLO;
        if (ui.mod || ui.matrix)
            bb |= 1u << B_SEL;
        if (eng_playing())
            bb |= LED_PLAY_GREEN;
        if (dirty)
            bb |= 1u << B_SAVE;
        if (ui.mod) {
            project_t *p = eng_project();
            for (int g = 0; g <= ENG_TRACKS; g++)
                for (int c = 0; c < ENG_CABLES; c++) {
                    const cable_t *q = &p->cable[g][c];
                    if (q->depth && q->src != SRC_NONE && q->dst / 4 == ui.page)
                        kk |= 1u << (q->src - SRC_A1);
                }
        }
        plat_leds(bb, kk);
        plat_glow(1);
    }
}

void app_input_only(void) { input(); }

/* --------------------------------------------------------------- frames --- */
void app_ui_init(void)                   /* at power-on, and after an update, a restore or the tables' transfer */
{
    data_ok = data_check();
    if (!data_ok)
        halted = 1;
    ui.full = 1;
}

#ifdef PLAT_HOST
void app_host_reboot(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched_k = -1;
    halted = save_hold = dirty = 0;
    eng_play(0);
}
#endif

void app_frame(void)
{
    uint32_t now = plat_ms();
    if (halted) {                        /* after app_panic (an update, a restore, the tables' transfer: each done
                                          * before the main loop draws again): the tables checked, the sound back */
        data_ok = data_check();
        if (data_ok) {
            if (!engine_ok)
                engine_ok = eng_init() == 0;
            halted = 0;
        }
        ui.full = 1;
    }
    input();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.foot_shown[0] = -1;
    }
    if (ui.full || now - ui.drawn_at >= 40u) {   /* 25 frames a second at most: what changed */
        ui.drawn_at = now;
        draw();
    }
}
