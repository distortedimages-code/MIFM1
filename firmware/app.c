/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1: the screen and the panel. The sound is mi/engine.cc's (C++, compiled apart): this file edits its project
 * (eng_project) and shows what it plays.
 *
 * The screen is a module's face, dark, its lights in Mutable Instruments' colours (teal, orange, pink, as their
 * panels print them): at the top the module's icon in a lit ring, its name and a line on its state (Plaits shows
 * the model alone, its logo and its name); then its four knobs in a row, as KNOB 1 to 4 sit on the FM-1, each a ring
 * lit from its start (or its middle) to its value, the value in it; at the bottom the four tracks, an LED each lit by
 * its gate (on Tides' and Marbles' pages: their outputs, lit as high as they are).
 *
 *   KNOB 1..4   the page's four parameters        SELECT   the module's other face (1/3, 2/3...)
 *   ALGORITHM   the page's model or mode          PRESETS  the active track, 1 to 4
 *   EDIT        Plaits, the active track          LFO      Tides A, then B
 *   SEQ         the sequencer: Marbles' t, or the track's steps (TRIG, then RANDOM)
 *   ARP         Marbles' X (with the steps: Marbles' t, then X)
 *   HOME        the mixer: the levels, the pans   FX       the reverb (the sends, then its own knobs)
 *   GLO         SEQUENCER, TEMPO, SCALE, ROOT; then the project (K1, REC held: INIT) and the machine. Held with a
 *               knob: ALL, the same parameter on the four tracks (with ALGORITHM: the same model)
 *   ENV         held with a knob: that parameter's random (right new at each trig, left looped, 0 none)
 *   REC         with the steps: the white keys are a page's 16 steps, the first black keys the pages; a step held
 *               and a knob turned: a lock; a step held and SEL: its locks cleared
 *   PLAY        the clock runs or stops           SAVE     held: the project saved (stopped)
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
#define HEAD_H 28                        /* the screen: the band and the header, the module's display, the knobs, */
#define PANEL_Y 28                       /* the tracks */
#define PANEL_H 76
#define KNOB_Y 104
#define KNOB_W 60
#define KNOB_H 100
#define FOOT_Y 204
#define FOOT_H 36

/* the colours: a dark panel, Mutable Instruments' teal, orange and pink as lit LEDs */
#define BG RGB(25, 23, 24)
#define TXT RGB(240, 239, 235)
#define MUTED RGB(154, 151, 152)
#define DIM RGB(107, 104, 105)
#define TRACK RGB(53, 50, 51)            /* a ring's travel, unlit */
#define OFF RGB(58, 55, 56)              /* an LED off */
#define RULE RGB(46, 43, 44)
#define SEL RGB(52, 49, 50)              /* the chosen row */
#define PINK RGB(255, 74, 125)
#define TEAL RGB(24, 201, 212)
#define ORANGE RGB(255, 178, 46)

/* ------------------------------------------------------------------ state --- */
static int data_ok;                      /* the resource region holds this build's tables */
static int halted;                       /* app_panic: the engine stopped until the next frame */
static int engine_ok;
static int cpu2_ok;
static int save_hold, dirty;
static project_t scratch;                /* a project read or made before it replaces the live one */

/* the screen's own pages, after the engine's: a track's RANDOM page (its steps' second face), the project and the
 * machine (GLO's second face) */
#define VP_RND(t) (ENG_NPAGES + (t))
#define VP_SYS (ENG_NPAGES + ENG_TRACKS)
enum { PT_RND = NPT, PT_SYS };
static const uint8_t RND_TP[4] = {TP_PAN, TP_DECAY, TP_NOTE, TP_RATCHET};   /* the RANDOM page's knobs */

static struct {
    int page;                            /* the page shown: the engine's (engine.h), or VP_RND, VP_SYS */
    int prev_page;                       /* the page before GLO */
    int matrix;                          /* the MATRIX page, over the page */
    int track;                           /* the active track */
    int grid;                            /* REC, the steps on: the white keys are the steps */
    int spage;                           /* the steps' page on the keys (16 each) */
    int held_step, step_new, step_edit;  /* a step held (-1: none), made by this press, locked meanwhile */
    int used[NB];                        /* a knob turned while that button was held: no tap when it is let go */
    int mod;                             /* MOD mode */
    int msel;                            /* MATRIX: the cable chosen (0..7 the track's, 8..15 the global ones) */
    int init_sel;                        /* GLO: the project REC held loads (0 new, 1 the demo, 2 the saved one) */
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
static int ptype(int page) { return page >= VP_SYS ? PT_SYS : page >= VP_RND(0) ? PT_RND : eng_page_type(page); }
static int ptrack(int page)              /* the track a page belongs to, -1: none */
{
    return page >= VP_SYS ? -1 : page >= VP_RND(0) ? page - VP_RND(0) : eng_page_track(page);
}

static int module_pages(int page, int *f)   /* its module's pages (its faces), in order; how many */
{
    int t = ptrack(page);
    switch (ptype(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3:
        f[0] = PG_TRACK(t, 0);
        f[1] = PG_TRACK(t, 1);
        f[2] = PG_TRACK(t, 2);
        return 3;
    case PT_TIDES1: case PT_TIDES2:
        f[0] = page >= PG_TIDES(1, 0) ? PG_TIDES(1, 0) : PG_TIDES(0, 0);
        f[1] = f[0] + 1;
        return 2;
    case PT_MARB_T1: case PT_MARB_T2: f[0] = PG_MARBLES(0); f[1] = PG_MARBLES(1); return 2;
    case PT_MARB_X1: case PT_MARB_X2: f[0] = PG_MARBLES(2); f[1] = PG_MARBLES(3); return 2;
    case PT_MIX: case PT_PAN: f[0] = PG_MIX; f[1] = PG_PAN; return 2;
    case PT_FX1: case PT_FX2: f[0] = PG_FX(0); f[1] = PG_FX(1); return 2;
    case PT_STEP: case PT_RND: f[0] = PG_STEP(t); f[1] = VP_RND(t); return 2;
    default: f[0] = PG_GLOBAL; f[1] = VP_SYS; return 2;
    }
}
static int page_count(int page)
{
    int f[3];
    return module_pages(page, f);
}
static int page_face(int page)           /* which face of its module */
{
    int f[3], n = module_pages(page, f);
    for (int i = 0; i < n; i++)
        if (f[i] == page)
            return i;
    return 0;
}
static int page_first(int page)
{
    int f[3];
    module_pages(page, f);
    return f[0];
}
static int page_at(int page, int face)   /* its module's page at face (wrapping) */
{
    int f[3], n = module_pages(page, f);
    return f[((face % n) + n) % n];
}
static int track_page(int page, int t)   /* the same page on track t */
{
    switch (ptype(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: return PG_TRACK(t, page_face(page));
    case PT_STEP: return PG_STEP(t);
    case PT_RND: return VP_RND(t);
    default: return page;
    }
}

/* the parameter a knob turns: its page's, the GLOBAL page's TEMPO is Marbles' RATE, the RANDOM page's are its track's
 * PAN, DECAY, NOTE and RATCHET (their randoms) */
static int knob_param(int page, int k)
{
    if (page == PG_GLOBAL && k == 1)
        return PG_MARBLES(0) * 4;
    if (ptype(page) == PT_RND)
        return eng_track_param(ptrack(page), RND_TP[k]);
    return page * 4 + k;
}

static const char *module_name(int page)
{
    switch (ptype(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: return "Plaits";
    case PT_TIDES1: case PT_TIDES2: return page >= PG_TIDES(1, 0) ? "Tides B" : "Tides A";
    case PT_MARB_T1: case PT_MARB_T2: return "Marbles t";
    case PT_MARB_X1: case PT_MARB_X2: return "Marbles X";
    case PT_MIX: case PT_PAN: return "Mixer";
    case PT_STEP: case PT_RND: return "Steps";
    case PT_GLOBAL: case PT_SYS: return "Global";
    default: return "Reverb";
    }
}

/* the colours of a module's state, as its icons print them: teal, orange, pink */
static uint16_t accent3(int i) { return i <= 0 ? TEAL : i == 1 ? ORANGE : PINK; }

static uint16_t model_accent(int m)      /* Plaits' banks: teal the classic models, pink the noisy and the drums,
                                          * orange the eight of 1.2 */
{
    return m < 8 ? ORANGE : m < 16 ? TEAL : PINK;
}
static uint16_t track_accent(int t) { return model_accent(eng_project()->algo[PG_TRACK(t, 0)]); }

static uint16_t page_accent(int page)
{
    project_t *p = eng_project();
    switch (ptype(page)) {
    case PT_STEP: case PT_RND: return track_accent(ptrack(page));
    case PT_PAN: case PT_GLOBAL: case PT_SYS: return TEAL;
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: return model_accent(p->algo[page_first(page)]);
    case PT_TIDES1: case PT_TIDES2: return accent3(p->algo[page_first(page)]);
    case PT_MARB_T1: case PT_MARB_T2: {
        int m = p->algo[PG_MARBLES(0)];
        return m < 3 ? accent3(m) : TEAL;
    }
    case PT_MARB_X1: case PT_MARB_X2: return accent3((int)(p->val[PG_MARBLES(2) * 4 + 3] + 0.5f));
    case PT_MIX: return TEAL;
    default: return PINK;
    }
}
static uint16_t mod_colour(uint16_t ac) { return ac == TEAL ? PINK : TEAL; }   /* where the cables take a knob */

/* Plaits' models in four letters or so (the tracks' row), its banks */
static const char *const SHORT[24] = {"VCF", "PHASE", "6-OP 1", "6-OP 2", "6-OP 3", "TERRAIN", "STRINGS", "CHIP",
                                      "VA", "SHAPER", "FM", "FORMANT", "HARMO", "TABLE", "CHORDS", "SPEECH",
                                      "SWARM", "NOISE", "PARTICL", "STRING", "MODAL", "KICK", "SNARE", "HI-HAT"};
static const char *const BANK[3] = {"PLAITS 1.2", "PITCHED", "NOISE & DRUMS"};

/* ------------------------------------------------------------------ text --- */
static int iabs(int v) { return v < 0 ? -v : v; }
static int held(int b) { return (int)((ui.btn >> b) & 1u); }   /* a button held now */

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

/* a knob's name as the panels write it: in capitals, Marbles' t left small */
static void label_text(const char *s, char *b, uint32_t max)
{
    uint32_t n = 0;
    for (const char *c = s; *c && n < max - 1; c++, n++) {
        int lone_t = *c == 't' && (c == s || c[-1] == ' ') && (c[1] == 0 || c[1] == ' ');
        b[n] = (char)(*c >= 'a' && *c <= 'z' && !lone_t ? *c - 32 : *c);
    }
    b[n] = 0;
}

/* a parameter's short name, for the matrix: its track or its module, then its own */
static void param_name(int param, char *b, uint32_t max)
{
    int page = param / 4, t = -1;
    char n[4];
    b[0] = 0;
    eng_param_tp(param, &t);
    if (t >= 0) {
        text_cat(b, "T", max);
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
static void project_load(void)            /* the saved project (v0.1's read into this version's), else the demo */
{
    project_t *p = eng_project();
    int got = plat_store_load(OBJ_PROJECT, &scratch, sizeof scratch);
    if (got > 0 && eng_upgrade(&scratch, got))
        *p = scratch;
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
    ui.page = ui.prev_page = PG_TRACK(0, 0);
    ui.touched_k = -1;
    ui.held_step = -1;
}

/* ---------------------------------------------------------- drawing kit --- */
/* shapes with soft edges, on gfx.c's canvas: a pixel's coverage a (0..1) blends c over what is there */
static void blend_px(int32_t x, int32_t y, uint16_t c, float a)
{
    uint16_t *px;
    if (a <= 0.0f)
        return;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1)
        return;
    px = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 0.99f) {
        *px = swap16(c);
    } else {
        uint32_t k = (uint32_t)(a * 256.0f), bg = swap16(*px);
        uint32_t r = ((uint32_t)(c >> 11) * k + (bg >> 11) * (256u - k)) >> 8;
        uint32_t g = ((uint32_t)((c >> 5) & 63u) * k + ((bg >> 5) & 63u) * (256u - k)) >> 8;
        uint32_t b = ((uint32_t)(c & 31u) * k + (bg & 31u) * (256u - k)) >> 8;
        *px = swap16((uint16_t)(r << 11 | g << 5 | b));
    }
}

static uint16_t tint(uint16_t c, uint16_t bg, int a)   /* a / 8 of c over bg */
{
    uint32_t r = ((uint32_t)(c >> 11) * (uint32_t)a + (uint32_t)(bg >> 11) * (uint32_t)(8 - a)) / 8u;
    uint32_t g = ((uint32_t)((c >> 5) & 63u) * (uint32_t)a + (uint32_t)((bg >> 5) & 63u) * (uint32_t)(8 - a)) / 8u;
    uint32_t b = ((uint32_t)(c & 31u) * (uint32_t)a + (uint32_t)(bg & 31u) * (uint32_t)(8 - a)) / 8u;
    return (uint16_t)(r << 11 | g << 5 | b);
}

#define BOX_OF(cx, cy, r)                                                                                         \
    int xa = (int)fm_floorf((cx) - (r) - 1.0f), xb = (int)fm_floorf((cx) + (r) + 2.0f);                        \
    int ya = (int)fm_floorf((cy) - (r) - 1.0f), yb = (int)fm_floorf((cy) + (r) + 2.0f)

static void disc(float cx, float cy, float r, uint16_t c)
{
    BOX_OF(cx, cy, r);
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            float dx = (float)x - cx, dy = (float)y - cy;
            blend_px(x, y, c, r + 0.5f - fm_sqrtf(dx * dx + dy * dy));
        }
}

static void ring(float cx, float cy, float r, float w, uint16_t c)   /* r the outer edge, w its width */
{
    BOX_OF(cx, cy, r);
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            float dx = (float)x - cx, dy = (float)y - cy, d = fm_sqrtf(dx * dx + dy * dy);
            blend_px(x, y, c, fm_minf(fm_minf(r + 0.5f - d, d - (r - w) + 0.5f), 1.0f));
        }
}

static void seg(float x0, float y0, float x1, float y1, float w, uint16_t c)   /* a stroke w wide, round ends */
{
    float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy, h = w * 0.5f;
    int xa = (int)fm_floorf(fm_minf(x0, x1) - h - 1.0f), xb = (int)fm_floorf(fm_maxf(x0, x1) + h + 2.0f);
    int ya = (int)fm_floorf(fm_minf(y0, y1) - h - 1.0f), yb = (int)fm_floorf(fm_maxf(y0, y1) + h + 2.0f);
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            float px = (float)x - x0, py = (float)y - y0;
            float t = l2 > 0.0f ? fm_clampf((px * vx + py * vy) / l2, 0.0f, 1.0f) : 0.0f;
            float dx = px - t * vx, dy = py - t * vy;
            blend_px(x, y, c, h + 0.5f - fm_sqrtf(dx * dx + dy * dy));
        }
}

static float atan2_deg(float y, float x) /* degrees, to a few hundredths */
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), mx = fm_maxf(ax, ay), a, s, r;
    if (mx <= 0.0f)
        return 0.0f;
    a = fm_minf(ax, ay) / mx;
    s = a * a;
    r = (((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a) * 57.2957795f;
    if (ay > ax)
        r = 90.0f - r;
    if (x < 0.0f)
        r = 180.0f - r;
    return y < 0.0f ? -r : r;
}

/* a knob's travel: 270 degrees from the bottom left, clockwise; u 0..1 along it (a direction, y up) */
static float travel_u(float dx, float dy)
{
    float a = atan2_deg(dy, dx);
    if (a < -90.0f)
        a += 360.0f;
    return (225.0f - a) / 270.0f;
}
static void travel_xy(float cx, float cy, float u, float r, float *x, float *y)
{
    float a = (225.0f - 270.0f * u) * (3.14159265f / 180.0f);
    *x = cx + r * fm_cosf(a);
    *y = cy - r * fm_sinf(a);
}

static void arc(float cx, float cy, float r0, float r1, float u0, float u1, uint16_t c)   /* u0..u1, r0..r1 */
{
    BOX_OF(cx, cy, r1);
    if (u1 <= u0)
        return;
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            float dx = (float)x - cx, dy = cy - (float)y, d = fm_sqrtf(dx * dx + dy * dy);
            float cov = fm_minf(r1 + 0.5f - d, d - r0 + 0.5f), u, e;
            if (cov <= 0.0f)
                continue;
            u = travel_u(dx, dy);
            e = fm_minf(u - u0, u1 - u) * 4.712389f * d + 0.5f;   /* its ends, soft too */
            if (e > 0.0f)
                blend_px(x, y, c, fm_minf(cov, 1.0f) * fm_minf(e, 1.0f));
        }
}

static void pill(int x, int y, int w, int h, uint16_t c)
{
    float r = (float)h * 0.5f;
    if (w < h)
        w = h;
    disc((float)x + r - 0.5f, (float)y + r - 0.5f, r, c);
    disc((float)(x + w) - r - 0.5f, (float)y + r - 0.5f, r, c);
    cv_rect(x + h / 2, y, w - 2 * (h / 2), h, c);
}

static int text_pill(int cx, int y, const felucca_font_t *f, const char *s, uint16_t bg, uint16_t fg)
{                                        /* a label in a pill, centred on cx; its width */
    int h = f == &FONT_T ? 13 : 15, tw = text_w(f, s), w = tw + h - 3, x = cx - w / 2;
    pill(x, y, w, h, bg);
    cv_text(x + (w - tw) / 2, y + (h - f->h) / 2 + 1, f, s, fg);
    return w;
}

/* light: a lit thing glows, as an LED behind a panel would (two wider, darker passes under it) */
static void glow_arc(float cx, float cy, float r0, float r1, float u0, float u1, uint16_t c)
{
    arc(cx, cy, r0 - 2.6f, r1 + 2.6f, u0, u1, tint(c, BG, 2));
    arc(cx, cy, r0 - 1.3f, r1 + 1.3f, u0, u1, tint(c, BG, 4));
    arc(cx, cy, r0, r1, u0, u1, c);
}
static void glow_disc(float cx, float cy, float r, uint16_t c)
{
    disc(cx, cy, r + 2.8f, tint(c, BG, 2));
    disc(cx, cy, r + 1.4f, tint(c, BG, 4));
    disc(cx, cy, r, c);
}
static void led(float cx, float cy, float r, uint16_t c, int lvl)   /* an LED: lvl 0 off .. 8 full */
{
    uint16_t col;
    if (lvl <= 0) {
        disc(cx, cy, r, OFF);
        return;
    }
    col = lvl >= 8 ? c : tint(c, BG, lvl);
    if (lvl >= 5)
        glow_disc(cx, cy, r, col);
    else
        disc(cx, cy, r, col);
}

/* the icons, as strokes in a 32-unit box: Plaits' 24 models, Tides' ramps, Marbles' t models and X modes, the mixer,
 * the reverb, the global page, the projects. L a line, C a circle, D a dot, R a rectangle (x y w h) */
typedef struct {
    char op;
    int8_t a, b, c, d;
} vop_t;
enum { IC_MODEL = 0, IC_RAMP = 24, IC_TMODEL = 27, IC_XMODE = 34, IC_MIX = 37, IC_REV, IC_GLO, IC_NEW, IC_DEMO,
       IC_SAVED, IC_STEPS, IC_DICE, NICON };
#define IOPS 10
static const vop_t ICONS[NICON][IOPS] = {
    {{'L', 4, 24, 12, 8}, {'L', 12, 8, 12, 24}, {'L', 12, 24, 20, 8}, {'L', 20, 8, 20, 24}, {'L', 20, 24, 28, 16}},
    {{'L', 4, 16, 8, 7}, {'L', 8, 7, 12, 7}, {'L', 12, 7, 20, 25}, {'L', 20, 25, 24, 25}, {'L', 24, 25, 28, 16}},
    {{'D', 9, 9, 2, 0}, {'D', 16, 9, 2, 0}, {'D', 23, 9, 2, 0}, {'D', 9, 23, 2, 0}, {'D', 16, 23, 2, 0}, {'D', 23, 23, 2, 0},
     {'L', 9, 12, 9, 20}},
    {{'D', 9, 9, 2, 0}, {'D', 16, 9, 2, 0}, {'D', 23, 9, 2, 0}, {'D', 9, 23, 2, 0}, {'D', 16, 23, 2, 0}, {'D', 23, 23, 2, 0},
     {'L', 16, 12, 16, 20}},
    {{'D', 9, 9, 2, 0}, {'D', 16, 9, 2, 0}, {'D', 23, 9, 2, 0}, {'D', 9, 23, 2, 0}, {'D', 16, 23, 2, 0}, {'D', 23, 23, 2, 0},
     {'L', 23, 12, 23, 20}},
    {{'C', 16, 16, 11, 0}, {'C', 18, 14, 6, 0}, {'D', 19, 13, 2, 0}},
    {{'L', 5, 10, 27, 10}, {'L', 5, 16, 27, 16}, {'L', 5, 22, 27, 22}, {'L', 22, 5, 10, 27}},
    {{'L', 4, 24, 4, 10}, {'L', 4, 10, 12, 10}, {'L', 12, 10, 12, 24}, {'L', 12, 24, 20, 24}, {'L', 20, 24, 20, 10},
     {'L', 20, 10, 28, 10}},
    {{'L', 4, 24, 12, 8}, {'L', 12, 8, 12, 24}, {'L', 12, 24, 18, 24}, {'L', 18, 24, 18, 8}, {'L', 18, 8, 26, 8},
     {'L', 26, 8, 26, 24}},
    {{'L', 4, 16, 7, 9}, {'L', 7, 9, 10, 7}, {'L', 10, 7, 13, 9}, {'L', 13, 9, 19, 23}, {'L', 19, 23, 22, 25},
     {'L', 22, 25, 25, 23}, {'L', 25, 23, 28, 16}},
    {{'C', 12, 16, 7, 0}, {'C', 21, 16, 7, 0}},
    {{'L', 4, 24, 10, 22}, {'L', 10, 22, 13, 12}, {'L', 13, 12, 16, 7}, {'L', 16, 7, 19, 12}, {'L', 19, 12, 22, 22},
     {'L', 22, 22, 28, 24}},
    {{'L', 6, 26, 6, 6}, {'L', 11, 26, 11, 11}, {'L', 16, 26, 16, 15}, {'L', 21, 26, 21, 19}, {'L', 26, 26, 26, 22}},
    {{'R', 5, 5, 9, 9}, {'R', 18, 5, 9, 9}, {'R', 5, 18, 9, 9}, {'R', 18, 18, 9, 9}},
    {{'L', 5, 9, 19, 9}, {'L', 9, 16, 23, 16}, {'L', 13, 23, 27, 23}},
    {{'R', 5, 6, 22, 14}, {'L', 10, 20, 8, 27}, {'L', 8, 27, 16, 20}},
    {{'C', 10, 18, 5, 0}, {'C', 17, 13, 6, 0}, {'C', 23, 19, 5, 0}},
    {{'L', 4, 18, 7, 10}, {'L', 7, 10, 10, 23}, {'L', 10, 23, 13, 8}, {'L', 13, 8, 16, 21}, {'L', 16, 21, 19, 11},
     {'L', 19, 11, 22, 24}, {'L', 22, 24, 25, 12}, {'L', 25, 12, 28, 17}},
    {{'D', 8, 9, 2, 0}, {'D', 20, 7, 2, 0}, {'D', 14, 15, 2, 0}, {'D', 25, 17, 2, 0}, {'D', 9, 23, 2, 0}, {'D', 19, 25, 2, 0}},
    {{'L', 8, 5, 8, 27}, {'L', 16, 5, 20, 16}, {'L', 20, 16, 16, 27}, {'L', 24, 5, 24, 27}},
    {{'C', 16, 16, 11, 0}, {'C', 16, 16, 6, 0}, {'D', 16, 16, 2, 0}},
    {{'C', 16, 16, 9, 0}, {'D', 16, 16, 3, 0}},
    {{'R', 6, 11, 20, 12}, {'L', 6, 15, 26, 15}, {'L', 9, 4, 15, 9}, {'L', 23, 4, 17, 9}},
    {{'L', 5, 13, 16, 8}, {'L', 16, 8, 27, 13}, {'L', 5, 16, 16, 20}, {'L', 16, 20, 27, 16}, {'L', 16, 20, 16, 28}},
    {{'L', 5, 24, 16, 8}, {'L', 16, 8, 27, 24}},                                                    /* AD */
    {{'L', 4, 22, 9, 10}, {'L', 9, 10, 14, 22}, {'L', 14, 22, 19, 10}, {'L', 19, 10, 24, 22}, {'L', 24, 22, 28, 15}},
    {{'L', 4, 24, 10, 9}, {'L', 10, 9, 22, 9}, {'L', 22, 9, 28, 24}},                               /* AR */
    {{'L', 16, 27, 16, 16}, {'L', 16, 16, 8, 6}, {'L', 16, 16, 24, 6}},                             /* coin toss */
    {{'D', 8, 10, 2, 0}, {'D', 12, 13, 2, 0}, {'D', 9, 16, 2, 0}, {'D', 21, 18, 2, 0}, {'D', 25, 21, 2, 0}, {'D', 20, 23, 2, 0}},
    {{'D', 9, 9, 2, 0}, {'D', 16, 9, 2, 0}, {'D', 23, 9, 2, 0}, {'D', 9, 16, 2, 0}, {'D', 16, 16, 2, 0}, {'D', 23, 16, 2, 0},
     {'D', 9, 23, 2, 0}, {'D', 16, 23, 2, 0}, {'D', 23, 23, 2, 0}},                                           /* drums */
    {{'L', 8, 26, 8, 12}, {'L', 16, 26, 16, 6}, {'L', 24, 26, 24, 18}},                             /* independent */
    {{'L', 6, 16, 26, 16}, {'D', 16, 9, 2, 0}, {'D', 16, 23, 2, 0}},                                      /* divider */
    {{'C', 8, 16, 4, 0}, {'C', 16, 16, 4, 0}, {'D', 24, 16, 4, 0}},                                          /* three states */
    {{'C', 9, 16, 5, 0}, {'C', 23, 16, 5, 0}, {'L', 14, 13, 18, 13}, {'L', 14, 19, 18, 19}},              /* Markov */
    {{'L', 9, 26, 9, 10}, {'L', 16, 26, 16, 10}, {'L', 23, 26, 23, 10}},                            /* X: identical */
    {{'L', 9, 26, 9, 17}, {'L', 16, 26, 16, 7}, {'L', 23, 26, 23, 17}},                             /* bump */
    {{'L', 9, 26, 9, 20}, {'L', 16, 26, 16, 14}, {'L', 23, 26, 23, 7}},                             /* tilt */
    {{'L', 8, 6, 8, 26}, {'L', 16, 6, 16, 26}, {'L', 24, 6, 24, 26}, {'L', 5, 20, 11, 20}, {'L', 13, 10, 19, 10},
     {'L', 21, 16, 27, 16}},                                                                         /* mixer */
    {{'D', 7, 16, 2, 0}, {'L', 11, 10, 13, 16}, {'L', 13, 16, 11, 22}, {'L', 16, 7, 19, 16}, {'L', 19, 16, 16, 25},
     {'L', 21, 4, 25, 16}, {'L', 25, 16, 21, 28}},                                                   /* reverb */
    {{'L', 5, 9, 27, 9}, {'D', 11, 9, 3, 0}, {'L', 5, 16, 27, 16}, {'D', 21, 16, 3, 0}, {'L', 5, 23, 27, 23},
     {'D', 14, 23, 3, 0}},                                                                              /* global */
    {{'L', 16, 6, 16, 26}, {'L', 6, 16, 26, 16}},                                                   /* new */
    {{'L', 10, 6, 10, 26}, {'L', 10, 6, 26, 16}, {'L', 26, 16, 10, 26}},                            /* the demo */
    {{'L', 16, 4, 16, 20}, {'L', 9, 13, 16, 20}, {'L', 16, 20, 23, 13}, {'L', 6, 27, 26, 27}},      /* the saved one */
    {{'R', 3, 10, 5, 12}, {'R', 10, 10, 5, 12}, {'R', 17, 10, 5, 12}, {'R', 24, 10, 5, 12}, {'D', 5, 16, 1, 0},
     {'D', 19, 16, 1, 0}},                                                                           /* the steps */
    {{'R', 5, 5, 22, 22}, {'D', 11, 11, 2, 0}, {'D', 16, 16, 2, 0}, {'D', 21, 21, 2, 0}},           /* random */
};

static void vicon(float cx, float cy, float s, float w, uint16_t c, int which, int glow)
{                                        /* an icon s px a unit, its strokes w wide, glowing or not */
    for (int pass = glow ? 0 : 2; pass < 3; pass++) {
        float ww = w + (pass == 0 ? 4.0f : pass == 1 ? 2.0f : 0.0f);
        uint16_t col = pass == 0 ? tint(c, BG, 2) : pass == 1 ? tint(c, BG, 4) : c;
        for (int i = 0; i < IOPS && ICONS[which][i].op; i++) {
            const vop_t *o = &ICONS[which][i];
            float x0 = cx + ((float)o->a - 16.0f) * s, y0 = cy + ((float)o->b - 16.0f) * s;
            float x1 = cx + ((float)o->c - 16.0f) * s, y1 = cy + ((float)o->d - 16.0f) * s;
            switch (o->op) {
            case 'L': seg(x0, y0, x1, y1, ww, col); break;
            case 'C': ring(x0, y0, (float)o->c * s + ww * 0.5f, ww, col); break;
            case 'D': disc(x0, y0, (float)o->c * s + (ww - w) * 0.5f, col); break;
            default: {
                float xr = x0 + (float)o->c * s, yr = y0 + (float)o->d * s;
                seg(x0, y0, xr, y0, ww, col);
                seg(xr, y0, xr, yr, ww, col);
                seg(xr, yr, x0, yr, ww, col);
                seg(x0, yr, x0, y0, ww, col);
            }
            }
        }
    }
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

static void draw_header(void)
{
    static int load = -1;
    static uint32_t load_at;
    char t[32], v[12];
    int tr = ptrack(ui.page), x, mode = ui.held_step >= 0 ? 5 : held(B_ENV) ? 4 : held(B_GLO) ? 3 : ui.grid ? 2
                                                                                                          : ui.mod;
    uint16_t ac = page_accent(ui.page);
    if (load < 0 || plat_ms() - load_at >= 250u) {
        load = load_now();
        load_at = plat_ms();
    }
    int32_t s[5] = {ui.page | ui.matrix << 8 | mode << 10 | (ui.held_step & 127) << 13, load,
                    eng_playing() | dirty << 1, ui.track | track_accent(ui.track) << 4, ac};
    if (!ui.full && !memcmp(s, ui.head_shown, sizeof s))
        return;
    memcpy(ui.head_shown, s, sizeof s);
    cv_begin(240, HEAD_H, BG);
    label_text(ui.matrix ? "Matrix" : module_name(ui.page), t, sizeof t);
    x = cv_text(10, 6, &FONT_B, t, TXT);
    if (!ui.matrix && page_count(ui.page) > 1) {   /* the faces: an LED each, the one shown lit */
        for (int f = 0; f < page_count(ui.page); f++)
            led((float)(x + 8 + f * 11), 14.0f, 3.0f, ac, f == page_face(ui.page) ? 8 : 0);
        x += 8 + page_count(ui.page) * 11;
    }
    if (mode == 5) {                     /* what the knobs do now: a step's locks, randoms, all the tracks, ... */
        int_text(ui.held_step + 1, v);
        t[0] = 0;
        text_cat(t, "STEP ", sizeof t);
        text_cat(t, v, sizeof t);
        text_pill(x + 26, 7, &FONT_T, t, C_WHITE, BG);
    } else if (mode) {
        static const char *const M[5] = {"", "MOD", "REC", "ALL", "RND"};
        text_pill(x + 20, 7, &FONT_T, M[mode], mode == 3 ? TEAL : mode == 4 ? ORANGE : PINK, mode == 4 ? BG : C_WHITE);
    }
    t[0] = 0;
    int_text(load, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "%", sizeof t);
    x = 232 - text_w(&FONT_XS, t);
    cv_text(x, 6, &FONT_XS, t, load >= 80 ? PINK : load >= 60 ? ORANGE : DIM);
    if (dirty)                           /* not saved */
        disc((float)(x - 6), 14.0f, 2.2f, PINK);
    x -= 26;
    if (eng_playing())                   /* PLAY: a triangle, lit */
        disc((float)x + 4.0f, 14.0f, 9.0f, tint(TEAL, BG, 1));
    for (int y = -6; y <= 6; y++)
        cv_rect(x, 14 + y, 11 - (iabs(y) * 11 + 3) / 6, 1, eng_playing() ? TEAL : DIM);
    if (tr >= 0 || ui.matrix) {          /* the track, ringed in its model's colour */
        uint16_t tc = track_accent(ui.track);
        t[0] = 'T';
        t[1] = (char)('1' + ui.track);
        t[2] = 0;
        pill(x - 34, 6, 26, 16, tc);
        pill(x - 32, 8, 22, 12, BG);
        cv_text(x - 21 - text_w(&FONT_XS, t) / 2, 7, &FONT_XS, t, tc);
    }
    cv_blit(0, 0);
}

/* the module's display: its icon in a lit ring, its name, a line on its state ---------------------------------- */
#define HX 41.0f
#define HY 35.0f
static void hero_icon(int which, uint16_t c)
{
    ring(HX, HY, 30.5f, 8.0f, tint(c, BG, 2));
    ring(HX, HY, 29.0f, 5.0f, tint(c, BG, 4));
    ring(HX, HY, 27.5f, 3.0f, c);
    vicon(HX, HY, 1.05f, 2.8f, c, which, 1);
}

static int hero_title(const char *name)  /* the name: one line of L, two cut at a space, else M; the y under it */
{
    char a[24], b[24];
    int n = 0;
    if (text_w(&FONT_L, name) <= 150) {
        cv_text(84, 6, &FONT_L, name, C_WHITE);
        return 44;
    }
    while (name[n])
        n++;
    for (int i = n - 1; i > 0; i--) {
        if (name[i] != ' ' || i >= (int)sizeof a)
            continue;
        for (int j = 0; j < i; j++)
            a[j] = name[j];
        a[i] = 0;
        b[0] = 0;
        text_cat(b, name + i + 1, sizeof b);
        if (text_w(&FONT_L, a) <= 150 && text_w(&FONT_L, b) <= 150) {
            cv_text(84, -4, &FONT_L, a, C_WHITE);
            cv_text(84, 22, &FONT_L, b, C_WHITE);
            return 55;
        }
    }
    cv_text(84, 12, &FONT_M, name, C_WHITE);
    return 44;
}

static void hero_line(int y, const char *s)   /* the line under the name: its parts ('|') apart, a dot between */
{
    char part[40];
    int x = 85, n = 0;
    for (;; s++) {
        if (*s && *s != '|' && n < (int)sizeof part - 1) {
            part[n++] = *s;
            continue;
        }
        part[n] = 0;
        x = cv_text(x, y, &FONT_XS, part, MUTED);
        n = 0;
        if (!*s)
            break;
        disc((float)x + 5.0f, (float)y + 8.0f, 1.4f, DIM);
        x += 11;
    }
}

static void panel_plaits(int t, int face, uint16_t ac)
{
    project_t *p = eng_project();
    int m = p->algo[PG_TRACK(t, 0)];
    char s[40], n[8];
    hero_icon(IC_MODEL + m, ac);
    if (face == 0) {                     /* the model */
        int y = hero_title(eng_algo_name(PG_TRACK(t, 0), m));
        s[0] = 0;
        text_cat(s, BANK[m / 8], sizeof s);
        text_cat(s, "|", sizeof s);
        int_text(m + 1, n);
        text_cat(s, n, sizeof s);
        text_cat(s, " / 24", sizeof s);
        hero_line(y, s);
    } else if (face == 1) {              /* the low-pass gate: DECAY's tail, COLOUR's tone */
        int pg = PG_TRACK(t, 1), y0 = 68, h = 48;
        float decay = eng_final(pg * 4 + 0), colour = eng_final(pg * 4 + 1);
        float k = (0.002f + 0.12f * (1.0f - decay) * (1.0f - decay) * (1.0f - decay)) * 1.4f;
        float px = 0.0f, py = (float)y0;
        uint16_t fill = tint(ac, BG, 1 + (int)(colour * 2.0f));
        cv_text(84, 0, &FONT_XS, "LOW-PASS GATE", MUTED);
        cv_text(232 - text_w(&FONT_XS, colour < 0.5f ? "VCFA" : "VCA"), 0, &FONT_XS,
                colour < 0.5f ? "VCFA" : "VCA", DIM);
        for (int x = 0; x < 146; x++) {
            float env = x < 3 ? (float)x / 3.0f : fm_exp2f(-(float)(x - 3) * k * 1.4427f);
            float y = (float)y0 - env * (float)h;
            if (!(x % 3) && y < (float)y0 - 2.0f)
                cv_rect(86 + x, (int)y + 3, 1, y0 - (int)y - 2, fill);
            if (x) {
                seg(86.0f + px, py, 86.0f + (float)x, y, 5.0f, tint(ac, BG, 2));
                seg(86.0f + px, py, 86.0f + (float)x, y, 2.0f, ac);
            }
            px = (float)x;
            py = y;
        }
    } else {                             /* the gate and the note: where they come from, the note now */
        int pg = PG_TRACK(t, 2), on = (int)((eng_gates() >> t) & 1u), x;
        note_text(eng_note(t), n);
        x = cv_text(84, 6, &FONT_L, n, on ? C_WHITE : MUTED);
        led((float)(x + 12), 22.0f, 5.0f, ac, on ? 8 : 0);
        s[0] = 0;
        text_cat(s, "GATE ", sizeof s);
        text_cat(s, eng_pdef(pg * 4)->names[(int)(p->val[pg * 4] + 0.5f)], sizeof s);
        text_cat(s, "|NOTE ", sizeof s);
        text_cat(s, eng_pdef(pg * 4 + 1)->names[(int)(p->val[pg * 4 + 1] + 0.5f)], sizeof s);
        label_text(s, s, sizeof s);
        hero_line(44, s);
    }
}

static void enum_cat(char *s, int param, uint32_t max)   /* a choice's name, in capitals */
{
    char u[24];
    project_t *p = eng_project();
    const eng_pdef_t *d = eng_pdef(param);
    label_text(d->names[(int)(p->val[param] + 0.5f)], u, sizeof u);
    text_cat(s, u, max);
}

static void panel_tides(int tide)
{
    project_t *p = eng_project();
    int ramp = p->algo[PG_TIDES(tide, 0)], pg = PG_TIDES(tide, 1);
    static const char *const R[3] = {"X1/8", "X1", "X8"};
    char s[40];
    hero_icon(IC_RAMP + ramp, accent3(ramp));
    int y = hero_title(eng_algo_name(PG_TIDES(tide, 0), ramp));
    s[0] = 0;
    text_cat(s, R[(int)(p->val[pg * 4 + 2] + 0.5f) % 3], sizeof s);
    text_cat(s, "|", sizeof s);
    enum_cat(s, pg * 4 + 1, sizeof s);
    text_cat(s, "|", sizeof s);
    enum_cat(s, pg * 4 + 3, sizeof s);
    hero_line(y, s);
}

static void panel_marbles(int x)
{
    project_t *p = eng_project();
    char s[40], n[8];
    if (!x) {                            /* t: its model, the loop's length and range, DEJA VU */
        int m = p->algo[PG_MARBLES(0)];
        hero_icon(IC_TMODEL + m, m < 3 ? accent3(m) : TEAL);
        int y = hero_title(eng_algo_name(PG_MARBLES(0), m));
        s[0] = 0;
        text_cat(s, "LENGTH ", sizeof s);
        int_text((int32_t)(p->val[PG_MARBLES(1) * 4] + 0.5f), n);
        text_cat(s, n, sizeof s);
        text_cat(s, "|", sizeof s);
        enum_cat(s, PG_MARBLES(1) * 4 + 1, sizeof s);
        if (p->val[PG_MARBLES(1) * 4 + 2] > 0.5f)
            text_cat(s, "|DEJA VU", sizeof s);
        hero_line(y, s);
    } else {                             /* X: its scale, its mode, its range */
        int xm = (int)(p->val[PG_MARBLES(2) * 4 + 3] + 0.5f);
        hero_icon(IC_XMODE + xm, accent3(xm));
        int y = hero_title(eng_algo_name(PG_MARBLES(2), p->algo[PG_MARBLES(2)]));
        s[0] = 0;
        enum_cat(s, PG_MARBLES(2) * 4 + 3, sizeof s);
        text_cat(s, "|", sizeof s);
        enum_cat(s, PG_MARBLES(3) * 4, sizeof s);
        hero_line(y, s);
    }
}

static int meter_lit(int t) { return (int)(fm_clampf(eng_meter(t) * 1.25f, 0.0f, 1.0f) * 7.0f + 0.5f); }

static void panel_mixer(int pan)
{
    hero_icon(IC_MIX, TEAL);
    hero_line(hero_title(pan ? "PAN" : "MIXER"), pan ? "4 TRACKS|ON A TRIG: ITS LOCK, ITS RANDOM" : "4 TRACKS");
    for (int t = 0; t < ENG_TRACKS; t++) {   /* a column of LEDs a track, in its colour */
        int lit = meter_lit(t), x = 180 + t * 15;
        char n[2] = {(char)('1' + t), 0};
        for (int k = 0; k < 7; k++) {
            uint16_t c = k == 6 ? PINK : track_accent(t);
            cv_rect(x, 56 - k * 7, 9, 5, k < lit ? c : OFF);
        }
        cv_text(x + 4 - text_w(&FONT_T, n) / 2, 63, &FONT_T, n, DIM);
    }
}

static void panel_steps(int t)            /* a track's steps: how many, how many trigs, the page on the keys */
{
    char s[48], n[8];
    int len = (int)(eng_project()->val[PG_STEP(t) * 4 + 3] + 0.5f), trigs = 0;
    for (int i = 0; i < len; i++)
        trigs += eng_trig(t, i);
    hero_icon(IC_STEPS, track_accent(t));
    s[0] = 0;
    text_cat(s, "TRACK ", sizeof s);
    int_text(t + 1, n);
    text_cat(s, n, sizeof s);
    int y = hero_title(s);
    s[0] = 0;
    if (!eng_steps_mode()) {
        text_cat(s, "PLAYED BY MARBLES|GLO: STEPS", sizeof s);
    } else {
        int_text(len, n);
        text_cat(s, n, sizeof s);
        text_cat(s, " STEPS|", sizeof s);
        int_text(trigs, n);
        text_cat(s, n, sizeof s);
        text_cat(s, " TRIGS|PAGE ", sizeof s);
        int_text(ui.spage + 1, n);
        text_cat(s, n, sizeof s);
    }
    hero_line(y, s);
}

static void panel_random(int t)
{
    hero_icon(IC_DICE, track_accent(t));
    hero_line(hero_title("RANDOM"), "RIGHT: NEW|LEFT: LOOPED|0: NONE");
}

static void panel_global(void)           /* the sequencer, the scale */
{
    project_t *p = eng_project();
    char s[40], v[16];
    hero_icon(IC_GLO, TEAL);
    int y = hero_title(eng_steps_mode() ? "STEPS" : "MARBLES");
    s[0] = 0;
    if ((int)(p->val[PG_GLOBAL * 4 + 2] + 0.5f) == 0) {
        text_cat(s, "NO SCALE", sizeof s);
    } else {
        eng_param_text(PG_GLOBAL * 4 + 3, p->val[PG_GLOBAL * 4 + 3], v, sizeof v);
        text_cat(s, v, sizeof s);
        text_cat(s, " ", sizeof s);
        eng_param_text(PG_GLOBAL * 4 + 2, p->val[PG_GLOBAL * 4 + 2], v, sizeof v);
        label_text(v, v, sizeof v);
        text_cat(s, v, sizeof s);
    }
    text_cat(s, "|", sizeof s);
    eng_param_text(PG_MARBLES(0) * 4, eng_final(PG_MARBLES(0) * 4), v, sizeof v);
    text_cat(s, v, sizeof s);
    hero_line(y, s);
}

static void panel_reverb(void)
{
    hero_icon(IC_REV, PINK);
    hero_line(hero_title("REVERB"), ui.page == PG_FX(0) ? "FROM CLOUDS|SENDS" : "FROM CLOUDS");
}

static void reverb_meter(int force)      /* the return's meter, alone: the rest is not drawn again for it */
{
    static int shown = -1;
    int m = (int)(fm_clampf(eng_meter(ENG_TRACKS) * 2.0f, 0.0f, 1.0f) * 52.0f);
    if (!force && m == shown)
        return;
    shown = m;
    cv_begin(6, 52, TRACK);
    cv_rect(0, 52 - m, 6, m, PINK);
    cv_blit(226, PANEL_Y + 12);
}

static void draw_panel(void)
{
    project_t *p = eng_project();
    int pt = ptype(ui.page), tr = ptrack(ui.page);
    int32_t s[3] = {ui.page, pt <= PT_FX2 && eng_algo_count(page_first(ui.page)) ? p->algo[page_first(ui.page)] : -1, 0};
    switch (pt) {                        /* what the display shows, to draw it again only when it changes */
    case PT_PLAITS2: s[2] = (int)(eng_final(ui.page * 4) * 200.0f) | (int)(eng_final(ui.page * 4 + 1) * 8.0f) << 8; break;
    case PT_PLAITS3: s[2] = eng_note(tr) | (int)((eng_gates() >> tr) & 1u) << 8 | (int)p->val[ui.page * 4] << 9 |
                            (int)p->val[ui.page * 4 + 1] << 14; break;
    case PT_TIDES1: case PT_TIDES2: {
        int pg = PG_TIDES(ui.page >= PG_TIDES(1, 0), 1);
        s[2] = (int)p->val[pg * 4 + 1] | (int)p->val[pg * 4 + 2] << 4 | (int)p->val[pg * 4 + 3] << 8;
        break;
    }
    case PT_MARB_T1: case PT_MARB_T2:
        s[2] = (int)p->val[PG_MARBLES(1) * 4] | (int)p->val[PG_MARBLES(1) * 4 + 1] << 5 |
               (p->val[PG_MARBLES(1) * 4 + 2] > 0.5f) << 8;
        break;
    case PT_MARB_X1: case PT_MARB_X2:
        s[2] = (int)(p->val[PG_MARBLES(2) * 4 + 3] + 0.5f) | (int)p->val[PG_MARBLES(3) * 4] << 4;
        break;
    case PT_STEP:
        for (int i = 0; i < ENG_STEPS / 8; i++)
            s[2] = s[2] * 31 + p->trig[tr][i];
        s[2] = s[2] * 7 + ui.spage + 5 * eng_steps_mode() + 11 * (int)p->val[PG_STEP(tr) * 4 + 3];
        break;
    case PT_GLOBAL:
        s[2] = (int)p->val[PG_GLOBAL * 4] | (int)p->val[PG_GLOBAL * 4 + 2] << 2 | (int)p->val[PG_GLOBAL * 4 + 3] << 6 |
               (int)(eng_final(PG_MARBLES(0) * 4) * 1000.0f) << 10;
        break;
    case PT_MIX: case PT_PAN:
        for (int t = 0; t < ENG_TRACKS; t++)
            s[2] |= meter_lit(t) << (t * 3) | (int)(p->algo[PG_TRACK(t, 0)] / 8) << (12 + t * 2);
        break;
    default: break;
    }
    if (!ui.full && !memcmp(s, ui.panel_shown, sizeof s)) {
        if (pt == PT_FX1 || pt == PT_FX2)
            reverb_meter(0);
        return;
    }
    memcpy(ui.panel_shown, s, sizeof s);
    cv_begin(240, PANEL_H, BG);
    switch (pt) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: panel_plaits(tr, page_face(ui.page), page_accent(ui.page)); break;
    case PT_TIDES1: case PT_TIDES2: panel_tides(ui.page >= PG_TIDES(1, 0)); break;
    case PT_MARB_T1: case PT_MARB_T2: panel_marbles(0); break;
    case PT_MARB_X1: case PT_MARB_X2: panel_marbles(1); break;
    case PT_MIX: case PT_PAN: panel_mixer(pt == PT_PAN); break;
    case PT_STEP: panel_steps(tr); break;
    case PT_RND: panel_random(tr); break;
    case PT_GLOBAL: panel_global(); break;
    default: panel_reverb(); break;
    }
    cv_blit(0, PANEL_Y);
    if (pt == PT_FX1 || pt == PT_FX2)
        reverb_meter(1);
}

/* the knobs: a ring each, lit from its start (or its middle) to its value ---------------------------------------- */

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

#define RX 30.0f                         /* a knob's ring in its 60 x 100 cell */
#define RY 45.0f
#define R0 19.0f
#define R1 25.0f
static void ring_end(float u, uint16_t c, int glow)   /* a lit travel's round end */
{
    float x, y;
    travel_xy(RX, RY, u, (R0 + R1) * 0.5f, &x, &y);
    if (glow)
        glow_disc(x, y, (R1 - R0) * 0.5f, c);
    else
        disc(x, y, (R1 - R0) * 0.5f, c);
}

/* the value in the ring when it fits (B, else XS, else T), its unit under it; else all of it under the ring */
static void ring_text(const char *t, uint16_t c, char *under, uint32_t max)
{
    char a[24];
    int n = 0, sp = -1;
    for (const char *q = t; *q && n < (int)sizeof a - 1; q++) {   /* "1 / bar": "1/bar" */
        if (*q == ' ' && (q[1] == '/' || (n && a[n - 1] == '/')))
            continue;
        a[n++] = *q;
    }
    a[n] = 0;
    if (text_w(&FONT_B, a) > 36) {       /* "120 BPM", "0.89 Hz": the number in the ring, the unit under it */
        for (int i = 0; a[i]; i++)
            if (a[i] == ' ')
                sp = i;
        if (sp > 0) {
            text_cat(under, a + sp + 1, max);
            a[sp] = 0;
        }
    }
    const felucca_font_t *f = text_w(&FONT_B, a) <= 36 ? &FONT_B : text_w(&FONT_XS, a) <= 38 ? &FONT_XS : &FONT_T;
    if (text_w(f, a) > 40) {             /* still too wide: under the ring */
        under[0] = 0;
        text_cat(under, a, max);
        return;
    }
    cv_text(30 - text_w(f, a) / 2, (int)RY - f->h / 2 - 1, f, a, c);
}

/* what a knob shows: its value (and where the cables take it), or a held step's lock, or a random's depth */
enum { KV_VALUE, KV_LOCK, KV_RND };
static int knob_view(int param, int *t, int *tp)
{
    *tp = eng_param_tp(param, t);
    if (ptype(ui.page) == PT_RND)
        return KV_RND;
    if (*tp < 0 || !eng_tp_lockable(*tp))
        return KV_VALUE;
    if (held(B_ENV))
        return KV_RND;
    if (ui.held_step >= 0 && *t == ui.track)
        return KV_LOCK;
    return KV_VALUE;
}

static void draw_knob(int k)
{
    static const char *const RND_NAME[4] = {"Pan", "Decay", "Tune", "Ratchet"};
    project_t *p = eng_project();
    int param = knob_param(ui.page, k), t, tp, view = knob_view(param, &t, &tp), locked = 0, rnd = 0;
    const eng_pdef_t *d = eng_pdef(param);
    float v = p->val[param], f = eng_final(param), u, uf;
    char txt[32], c[32], lab[24], under[32];
    int hash = 0, hot = ui.touched_k == k && plat_ms() - ui.touched_at[k] < 1200u, ncab = 0;
    int bip = d->kind == K_BIP || (d->kind == K_INT && d->lo < 0);
    uint16_t ac = page_accent(ui.page), mc = mod_colour(ac), vc;
    under[0] = 0;
    if (view == KV_RND) {                /* the random's depth: right new at each trig, left a loop */
        rnd = p->rnd[t][tp];
        v = f = (float)rnd * 0.01f;
        bip = 1;
        txt[0] = 0;
        if (!rnd) {
            text_cat(txt, "OFF", sizeof txt);
        } else {
            int_text(rnd < 0 ? -rnd : rnd, txt);
            text_cat(under, rnd > 0 ? "RANDOM" : "LOOP", sizeof under);
        }
    } else if (view == KV_LOCK) {        /* the held step: its lock, else the track's value */
        locked = eng_lock(t, ui.held_step, tp, &v);
        f = v;
        eng_param_text(param, v, txt, sizeof txt);
        if (locked)
            text_cat(under, "LOCK", sizeof under);
    } else {
        ncab = cables_on(param, c, sizeof c, &hash);
        eng_param_text(param, ncab ? f : v, txt, sizeof txt);
    }
    vc = view == KV_RND ? (rnd < 0 ? mc : ac) : locked ? mc : ac;
    int32_t s[4] = {(int32_t)(view == KV_RND ? (float)(rnd + 100) : knob_u(param, v) * 270.0f) |
                    (int32_t)(knob_u(param, f) * 270.0f) << 10 | view << 20 | locked << 22,
                    text_hash(txt), hash, hot | ui.mod << 1 | (int32_t)ac << 2};
    if (!ui.full && !memcmp(s, ui.knob_shown[k], sizeof s))
        return;
    memcpy(ui.knob_shown[k], s, sizeof s);
    cv_begin(KNOB_W, KNOB_H, BG);
    label_text(ptype(ui.page) == PT_RND ? RND_NAME[k] : d->name, lab, sizeof lab);   /* touched: white, underlined */
    {
        const felucca_font_t *lf = text_w(&FONT_XS, lab) <= 58 ? &FONT_XS : &FONT_T;
        int w = text_w(lf, lab);
        cv_text(30 - w / 2, lf == &FONT_XS ? 0 : 2, lf, lab, hot ? C_WHITE : view != KV_VALUE ? vc : MUTED);
        if (hot)
            cv_rect(30 - w / 2, 15, w, 2, ac);
    }
    u = view == KV_RND ? ((float)rnd + 100.0f) / 200.0f : fm_clampf(knob_u(param, v), 0.0f, 1.0f);
    uf = view == KV_RND ? u : fm_clampf(knob_u(param, f), 0.0f, 1.0f);
    if (d->kind == K_ENUM && view == KV_VALUE) {   /* a choice: the ring in as many parts, the chosen one lit */
        int n = d->hi - d->lo + 1, i = (int)(v + 0.5f) - d->lo;
        float gap = n > 6 ? 0.008f : 0.014f;
        char nm[24];
        for (int j = 0; j < n; j++) {
            float a = (float)j / (float)n + gap, b = (float)(j + 1) / (float)n - gap;
            if (j == i)
                glow_arc(RX, RY, R0, R1, a, b, ac);
            else
                arc(RX, RY, R0, R1, a, b, TRACK);
        }
        label_text(txt, nm, sizeof nm);
        ring_text(nm, C_WHITE, under, sizeof under);
    } else {
        float lo = bip ? fm_minf(u, 0.5f) : 0.0f, hi = bip ? fm_maxf(u, 0.5f) : u;
        arc(RX, RY, R0, R1, 0.0f, 1.0f, TRACK);
        ring_end(0.0f, TRACK, 0);
        ring_end(1.0f, TRACK, 0);
        if (hi - lo > 0.004f) {
            glow_arc(RX, RY, R0, R1, lo, hi, vc);
            ring_end(lo, vc, 1);
            ring_end(hi, vc, 1);
        } else {
            ring_end(u, view == KV_RND ? DIM : vc, view != KV_RND);
        }
        if (bip) {                       /* an attenuverter's - + (a random's: LOOP - + RANDOM) */
            seg(3.0f, 67.0f, 8.0f, 67.0f, 1.4f, DIM);
            seg(52.0f, 67.0f, 57.0f, 67.0f, 1.4f, DIM);
            seg(54.5f, 64.5f, 54.5f, 69.5f, 1.4f, DIM);
        }
        if (ncab) {                      /* where the cables take it: a dotted path, a lit dot */
            float a = fm_minf(u, uf), b = fm_maxf(u, uf);
            for (float q = a; q < b; q += 0.05f)
                arc(RX, RY, R0 + 2.0f, R1 - 2.0f, q, fm_minf(q + 0.025f, b), mc);
            float x, y;
            travel_xy(RX, RY, uf, (R0 + R1) * 0.5f, &x, &y);
            glow_disc(x, y, 3.0f, mc);
        }
        ring_text(txt, ncab ? mc : view == KV_RND && !rnd ? DIM : C_WHITE, under, sizeof under);
    }
    if (ncab)                            /* under the ring: the cables, a lock, a random, else a unit, MOD's socket */
        cv_text(30 - text_w(&FONT_XS, c) / 2, 78, text_w(&FONT_XS, c) <= 58 ? &FONT_XS : &FONT_T, c, mc);
    else if (under[0])
        cv_text(30 - text_w(&FONT_XS, under) / 2, 78, &FONT_XS, under, view != KV_VALUE ? vc : MUTED);
    else if (ui.mod && eng_param_mod(param))
        ring(30.0f, 85.0f, 4.5f, 1.4f, DIM);
    cv_blit((uint32_t)(k * KNOB_W), KNOB_Y);
}

/* lists: the matrix, the global page, the missing tables --------------------------------------------------- */
static void draw_lines(int y, int h, const char *const *lines, const uint16_t *colours, int n, int sel)
{
    for (int i = 0; i < n; i++) {
        cv_begin(240, (uint32_t)h, BG);
        if (i == sel)
            pill(4, 1, 232, h - 2, SEL);
        cv_text(10, (h - 15) / 2, &FONT_XS, lines[i], i == sel ? C_WHITE : colours[i]);
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
            col[i] = DIM;
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
            col[i] = TXT;
        }
        lines[i] = rows[i];
    }
    h = h * 17 + ui.msel + g * 1000;
    if (!ui.full && h == shown)
        return;
    shown = h;
    cv_begin(240, 24, BG);
    cv_text(10, 0, &FONT_XS, g < ENG_TRACKS ? "THE TRACK'S CABLES" : "GLOBAL CABLES (TIDES, MARBLES, REVERB)", TXT);
    cv_text(10, 12, &FONT_T, "K1 CABLE   K2 SOURCE   K3 TARGET   K4 DEPTH", DIM);
    cv_blit(0, PANEL_Y);
    draw_lines(PANEL_Y + 24, 19, lines, col, 8, ui.msel - first);
}

static void draw_global(void)
{
    static uint32_t at;
    static int shown = -1;
    static const char *const P[3] = {"New: 4 Plaits, nothing patched", "The demo patch", "The saved project"};
    char v[24];
    if (ui.full || shown != ui.init_sel) {   /* the projects REC held loads, in the module's display */
        shown = ui.init_sel;
        cv_begin(240, PANEL_H, BG);
        cv_text(10, 0, &FONT_B, "PROJECT", TXT);
        cv_text(232 - text_w(&FONT_T, "K1 CHOOSE, REC HELD LOADS"), 3, &FONT_T, "K1 CHOOSE, REC HELD LOADS", DIM);
        for (int i = 0; i < 3; i++) {
            int y = 20 + i * 18, sel = i == ui.init_sel;
            if (sel)
                pill(4, y, 232, 17, SEL);
            disc(16.0f, (float)y + 8.0f, 6.5f, accent3(i));
            vicon(16.0f, (float)y + 8.0f, 0.34f, 1.5f, BG, IC_NEW + i, 0);
            cv_text(30, y + 1, &FONT_XS, P[i], sel ? C_WHITE : TXT);
        }
        cv_blit(0, PANEL_Y);
    }
    if (!ui.full && plat_ms() - at < 400u)
        return;
    at = plat_ms();
    for (int i = 0; i < 6; i++) {        /* the machine, where the knobs are: two canvases of three rows (a 240 x 100
                                          * one is more than gfx.c's canvas holds) */
        const char *l = "";
        uint16_t c = TXT;
        int y = i < 3 ? 4 + i * 15 : (i - 3) * 15;   /* a row every 15 px: rows 4..48, then 49..99 */
        if (i % 3 == 0) {
            cv_begin(240, i ? 51 : 49, BG);
            if (!i)
                cv_rect(8, 1, 224, 1, RULE);
        }
        v[0] = 0;
        switch (i) {
        case 0:
            l = "Core 0: tracks 1-2, Marbles, Tides";
            int_text((int32_t)((eng_load(0) + 5u) / 10u), v);
            text_cat(v, "%", sizeof v);
            break;
        case 1:
            l = "Core 1: tracks 3-4";
            if (eng_core1_running()) {
                int_text((int32_t)((eng_load(1) + 5u) / 10u), v);
                text_cat(v, "%", sizeof v);
            } else {
                text_cat(v, cpu2_ok ? "paused" : "off: all on core 0", sizeof v);
                c = ORANGE;
            }
            break;
        case 2:
            l = "Peak over a second";
            int_text((int32_t)((eng_peak() + 5u) / 10u), v);
            text_cat(v, "%", sizeof v);
            if (eng_peak() >= 900u)
                c = PINK;
            break;
        case 3:
            l = "Platform (audio)";
            int_text((int32_t)plat_cpu_pct(), v);
            text_cat(v, "%", sizeof v);
            break;
        case 4:
            l = "Core 1's stack";
            int_text((int32_t)plat_cpu2_stack(), v);
            text_cat(v, " / 2816 B", sizeof v);
            break;
        default:
            l = dirty ? "MIFM1 " APP_VERSION ", not saved" : "MIFM1 " APP_VERSION;
            text_cat(v, data_ok ? "tables OK" : "tables MISSING", sizeof v);
            if (!data_ok)
                c = PINK;
            break;
        }
        cv_text(10, y, &FONT_XS, l, MUTED);
        cv_text(230 - text_w(&FONT_XS, v), y, &FONT_XS, v, c);
        if (i % 3 == 2)
            cv_blit(0, (uint32_t)(KNOB_Y + (i < 3 ? 0 : 49)));
    }
}

static void draw_missing(void)
{
    static const char *const L[8] = {"", "MODULE TABLES MISSING", "", "The installer writes them after",
                                     "the firmware: install again from", "the page, the FM-1 stopped.", "", ""};
    uint16_t col[8];
    if (!ui.full)
        return;
    for (int i = 0; i < 8; i++)
        col[i] = i == 1 ? PINK : TXT;
    draw_lines(PANEL_Y + 4, 19, L, col, 8, -1);
}

/* the row at the bottom: the four tracks (an LED lit by its gate or its trig, in its model's colour); on Tides' and
 * Marbles' pages their outputs, lit as much as they are high; the steps of the active track when they play it and
 * REC is on (or on their pages) */
static int foot_mode(void)
{
    if (ui.matrix || ui.page == VP_SYS)
        return 0;
    if (eng_steps_mode() && (ui.grid || ptype(ui.page) == PT_STEP || ptype(ui.page) == PT_RND))
        return 5;
    switch (ptype(ui.page)) {
    case PT_TIDES1: case PT_TIDES2: return ui.page >= PG_TIDES(1, 0) ? 2 : 1;
    case PT_MARB_T1: case PT_MARB_T2: return 3;
    case PT_MARB_X1: case PT_MARB_X2: return 4;
    default: return 0;
    }
}

static int track_lit(int t)              /* a track's LED: its gate, or a trig in the last 60 ms (a ratchet's too) */
{
    static uint32_t seen[ENG_TRACKS], until[ENG_TRACKS];
    uint32_t r = eng_rises(t), now = plat_ms();
    if (r != seen[t]) {
        seen[t] = r;
        until[t] = now + 60u;
    }
    return (int)((eng_gates() >> t) & 1u) || (int32_t)(until[t] - now) > 0;
}

static void foot_steps(int t)            /* the steps of a page: a trig lit, a lock dotted, the playing one ringed */
{
    project_t *p = eng_project();
    int len = (int)(p->val[PG_STEP(t) * 4 + 3] + 0.5f), now = eng_step_now(t);
    uint16_t ac = track_accent(t);
    for (int i = 0; i < 16; i++) {
        int st = ui.spage * 16 + i, x = 9 + i * 13;
        if (st >= len) {
            cv_rect(x + 4, 16, 3, 3, RULE);
            continue;
        }
        if (st == ui.held_step)
            cv_rect(x, 8, 11, 18, C_WHITE);
        else if (eng_trig(t, st))
            cv_rect(x, 8, 11, 18, st == now ? C_WHITE : ac);
        else
            cv_rect(x, 8, 11, 18, st == now ? MUTED : TRACK);
        if (eng_trig(t, st) && st == now)
            cv_rect(x + 2, 10, 7, 14, ac);
        if (eng_step_locked(t, st))
            disc((float)x + 5.0f, 22.0f, 1.6f, BG);
        if (!(i & 3))
            cv_rect(x, 28, 11, 1, DIM);  /* the beats */
    }
    for (int j = 0; j < 4; j++)          /* the pages */
        if (j * 16 < len)
            cv_rect(222, 9 + j * 5, 6, 3, j == ui.spage ? TXT : DIM);
}

static void draw_footer(void)
{
    static const char *const OUT[4][4] = {{"A1", "A2", "A3", "A4"}, {"B1", "B2", "B3", "B4"},
                                          {"t1", "t2", "t3", "Y"}, {"X1", "X2", "X3", "Y"}};
    project_t *p = eng_project();
    char t[40], n[8];
    uint32_t now = plat_ms();
    int msg = now < ui.msg_until, mode = foot_mode(), lv[4], lh = 0;
    uint16_t ac = page_accent(ui.page);
    for (int i = 0; i < 4; i++) {        /* each LED's level, 0..8 */
        int src = mode == 1 ? SRC_A1 + i : mode == 2 ? SRC_B1 + i : mode == 3 ? (i < 3 ? SRC_T1 + i : SRC_Y)
                : SRC_X1 + i, v = mode && mode < 5 ? eng_hist(src, ENG_HIST - 1) : 0;
        lv[i] = mode && mode < 5 ? (iabs(v) * 8 + 63) / 127 : track_lit(i) * 8;
        lh = lh * 9 + lv[i];
    }
    if (mode == 5) {                     /* the steps shown: their trigs, locks, the one playing, the one held */
        lh = eng_step_now(ui.track) | ui.held_step << 7 | ui.spage << 14;
        for (int i = 0; i < 16; i++)
            lh = lh * 3 + eng_trig(ui.track, ui.spage * 16 + i) + (eng_step_locked(ui.track, ui.spage * 16 + i) ? 2 : 0);
        lh = lh * 67 + (int)p->val[PG_STEP(ui.track) * 4 + 3];
    }
    int32_t s[6] = {lh, mode | ui.mod << 4 | msg << 5, (int32_t)(track_accent(0) ^ track_accent(1) << 1 ^
                    track_accent(2) << 2 ^ track_accent(3) << 3) ^ ui.track << 20 ^ (int32_t)ac << 4,
                    eng_note(0) | eng_note(1) << 8 | eng_note(2) << 16,
                    p->algo[PG_TRACK(0, 0)] | p->algo[PG_TRACK(1, 0)] << 5 | p->algo[PG_TRACK(2, 0)] << 10 |
                    p->algo[PG_TRACK(3, 0)] << 15, msg ? text_hash(ui.msg[0]) : 0};
    if (!ui.full && !memcmp(s, ui.foot_shown, sizeof s))
        return;
    memcpy(ui.foot_shown, s, sizeof s);
    cv_begin(240, FOOT_H, BG);
    cv_rect(8, 2, 224, 1, RULE);
    if (msg) {
        t[0] = 0;
        text_cat(t, ui.msg[0], sizeof t);
        text_cat(t, "  ", sizeof t);
        text_cat(t, ui.msg[1], sizeof t);
        cv_text(120 - text_w(&FONT_B, t) / 2, 11, &FONT_B, t, C_WHITE);
    } else if (ui.mod) {
        cv_text(10, 6, &FONT_XS, "MOD: HOLD A SOURCE KEY, TURN A KNOB", C_WHITE);
        cv_text(10, 21, &FONT_T, "KEYS 1-4 A  5-8 B  9-11 X  12 Y  13-15 T", MUTED);
    } else if (mode == 5) {
        foot_steps(ui.track);
    } else {
        for (int i = 0; i < 4; i++) {
            int x = 10 + i * 57, act = mode ? lv[i] > 0 : i == ui.track;
            t[0] = 0;
            if (mode) {
                text_cat(t, OUT[mode - 1][i], sizeof t);
                led((float)(x + 4), 19.0f, 3.6f, ac, lv[i]);
            } else {
                n[0] = (char)('1' + i);
                n[1] = ' ';
                n[2] = 0;
                text_cat(t, n, sizeof t);
                text_cat(t, SHORT[p->algo[PG_TRACK(i, 0)] % 24], sizeof t);
                led((float)(x + 4), 19.0f, 3.6f, track_accent(i), lv[i]);
            }
            cv_text(x + 12, 11, text_w(&FONT_XS, t) <= 42 ? &FONT_XS : &FONT_T, t, act ? C_WHITE : DIM);
        }
    }
    cv_blit(0, FOOT_Y);
}

static void poll_keys(void);             /* (input, below): the keys read between the screen's parts too */

static void draw(void)
{
    if (ui.full)
        lcd_fill(0, 0, 240, 240, BG);
    draw_header();
    poll_keys();
    if (!data_ok || !engine_ok) {
        draw_missing();
    } else if (ui.page == VP_SYS) {
        draw_global();
    } else if (ui.matrix) {
        draw_matrix();
    } else {
        draw_panel();
        for (int k = 0; k < 4; k++) {
            poll_keys();
            draw_knob(k);
        }
    }
    poll_keys();
    draw_footer();
    ui.full = 0;
}

/* ---------------------------------------------------------------- input --- */
static void go_page(int page)
{
    int g = ptype(page) == PT_GLOBAL || ptype(page) == PT_SYS;
    if (g && ptype(ui.page) != PT_GLOBAL && ptype(ui.page) != PT_SYS)
        ui.prev_page = ui.page;          /* GLO again goes back there */
    ui.page = page;
    ui.touched_k = -1;
    ui.matrix = 0;
    ui.full = 1;
}

/* GLO, REC held: a new project (four Plaits, nothing patched), the demo patch, or the saved project again. The
 * transport stops; nothing is saved until SAVE is held */
static void project_init(int which)
{
    eng_play(0);
    if (which == 2) {
        project_load();
        app_say("SAVED PROJECT", "LOADED");
    } else {
        if (which == 0)
            eng_blank(&scratch);
        else
            eng_defaults(&scratch);
        *eng_project() = scratch;
        dirty = 1;
        app_say(which == 0 ? "NEW PROJECT" : "DEMO PATCH", which == 0 ? "4 PLAITS" : "LOADED");
    }
    ui.track = 0;
    go_page(PG_TRACK(0, 0));
}

/* a value moved e detents: 1 % a detent (3 fast) over 0..1, 2 % over -1..1, one for an integer or a choice */
static float step_value(int param, float v, int32_t e)
{
    const eng_pdef_t *d = eng_pdef(param);
    int32_t a = e > 1 || e < -1 ? e * 3 : e;
    if (d->kind == K_UNI)
        return fm_clampf(v + 0.01f * (float)a, 0.0f, 1.0f);
    if (d->kind == K_BIP)
        return fm_clampf(v + 0.02f * (float)a, -1.0f, 1.0f);
    return (float)clamp((int32_t)(v + (v < 0 ? -0.5f : 0.5f)) + e, d->lo, d->hi);
}

static void edit_param(int param, int32_t e)
{
    project_t *p = eng_project();
    p->val[param] = step_value(param, p->val[param], e);
    dirty = 1;
}

static int edit_all(int param, int32_t e)   /* GLO held: a track's parameter moved on the four tracks */
{
    int t, tp = eng_param_tp(param, &t);
    if (tp < 0)
        return 0;
    for (int i = 0; i < ENG_TRACKS; i++)
        edit_param(eng_track_param(i, tp), e);
    return 1;
}

/* ENV held (or the RANDOM page): a track parameter's random, -100 (a loop) .. 100 (new at each trig), 0 none: the
 * knob's own value again. GLO held too: the four tracks' */
static void edit_rnd(int param, int32_t e)
{
    project_t *p = eng_project();
    int t, tp = eng_param_tp(param, &t);
    if (tp < 0 || !eng_tp_lockable(tp)) {
        app_say("RANDOM:", "A TRACK'S KNOBS");
        return;
    }
    for (int i = 0; i < ENG_TRACKS; i++)
        if (i == t || held(B_GLO)) {
            int r = p->rnd[i][tp], n = clamp(r + e * (e > 1 || e < -1 ? 5 : 2), -100, 100);
            if ((r > 0 && n < 0) || (r < 0 && n > 0))
                n = 0;                   /* a stop at 0 on the way through */
            p->rnd[i][tp] = (int8_t)n;
        }
    dirty = 1;
}

static void edit_lock(int param, int32_t e)   /* a held step: its lock on the active track's parameter */
{
    project_t *p = eng_project();
    int t, tp = eng_param_tp(param, &t);
    float v;
    if (tp < 0 || !eng_tp_lockable(tp) || t != ui.track) {
        app_say("LOCK:", "THIS TRACK'S KNOBS");
        return;
    }
    if (!eng_lock(t, ui.held_step, tp, &v))
        v = p->val[param];
    if (eng_lock_set(t, ui.held_step, tp, step_value(param, v, e)) != 0)
        app_say("LOCKS", "ALL USED");
    ui.step_edit = 1;
    dirty = 1;
}

/* the group a parameter's cables live in: its track's, or the global one */
static int cable_group(int param)
{
    int t, tp = eng_param_tp(param, &t);
    return tp >= 0 ? t : ENG_TRACKS;
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
    int pt = ptype(ui.page), here = !ui.matrix, next = page_at(ui.page, page_face(ui.page) + 1);
    switch (b) {
    case B_EDIT:
        go_page(here && pt <= PT_PLAITS3 ? next : PG_TRACK(ui.track, 0));
        break;
    case B_LFO:
        if (here && (pt == PT_TIDES1 || pt == PT_TIDES2))
            go_page(ui.page >= PG_TIDES(1, 0) ? PG_TIDES(0, 0) : PG_TIDES(1, 0));
        else
            go_page(PG_TIDES(0, 0));
        break;
    case B_SEQ:                          /* the sequencer: the track's steps, or Marbles' t */
        if (eng_steps_mode())
            go_page(here && (pt == PT_STEP || pt == PT_RND) ? next : PG_STEP(ui.track));
        else
            go_page(here && pt == PT_MARB_T1 ? PG_MARBLES(1) : PG_MARBLES(0));
        break;
    case B_ARP:                          /* Marbles' X (with the steps, Marbles still runs: its t, then its X) */
        if (eng_steps_mode())
            go_page(here && (pt == PT_MARB_T1 || pt == PT_MARB_T2) ? PG_MARBLES(2) : PG_MARBLES(0));
        else
            go_page(here && pt == PT_MARB_X1 ? PG_MARBLES(3) : PG_MARBLES(2));
        break;
    case B_HOME:
        go_page(here && pt == PT_MIX ? PG_PAN : PG_MIX);
        break;
    case B_FX:
        go_page(here && pt == PT_FX1 ? PG_FX(1) : PG_FX(0));
        break;
    default:
        break;
    }
}

static void button_down(int b)
{
    ui.used[b] = 0;
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
    case B_SEL: case B_SAVE: case B_REC: case B_GLO: case B_ENV:
        ui.full = 1;                     /* on release (a tap), held long, or held with a knob */
        break;
    default:
        module_button(b);
        break;
    }
}

static void button_long(int b)
{
    if (b == B_SEL && ui.held_step < 0) {
        ui.matrix = 1;
        ui.mod = 0;
        ui.grid = 0;
        ui.msel = 0;
        ui.full = 1;
    } else if (b == B_SAVE) {
        if (eng_playing())
            app_say("STOP FIRST", "THEN SAVE");
        else if (project_save() == 0)
            app_say("PROJECT", "SAVED");
        else
            app_say("COULD NOT", "SAVE");
    } else if (b == B_REC) {
        if (ui.page == VP_SYS)
            project_init(ui.init_sel);
        else if (!ui.grid)
            app_say("INIT:", "GLO, SELECT, REC HELD");
    }
}

static void button_up(int b)
{
    int pt = ptype(ui.page);
    switch (b) {
    case B_SEL:
        if (ui.long_done[b])
            break;
        if (ui.held_step >= 0) {         /* a step held: its locks cleared */
            eng_lock_clear(ui.track, ui.held_step);
            ui.step_edit = 1;
            dirty = 1;
            app_say("STEP", "LOCKS CLEARED");
        } else if (ui.matrix) {
            ui.matrix = 0;
        } else {
            ui.mod = !ui.mod;
            ui.grid = 0;
        }
        ui.full = 1;
        break;
    case B_REC:                          /* the steps on the white keys */
        if (ui.long_done[b])
            break;
        if (!eng_steps_mode()) {
            app_say("REC:", "SEQUENCER ON STEPS (GLO)");
            break;
        }
        ui.grid = !ui.grid;
        ui.mod = 0;
        ui.held_step = -1;
        ui.full = 1;
        break;
    case B_GLO:                          /* a tap: the global page, and back; held with a knob: ALL */
        if (!ui.used[b])
            go_page(pt == PT_GLOBAL || pt == PT_SYS ? ui.prev_page : PG_GLOBAL);
        ui.full = 1;
        break;
    case B_ENV:                          /* held with a knob: its random */
        if (!ui.used[b])
            app_say("ENV HELD + A KNOB:", "ITS RANDOM");
        ui.full = 1;
        break;
    default:
        break;
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

/* the keys: the active track's notes; MOD's sources; with REC on, the white keys are the 16 steps of a page (held: its
 * locks on the knobs), the first four black keys the pages */
static void grid_key(int k, int down)
{
    static const uint8_t W[16] = PLAT_WHITE_KEYS, Bk[11] = PLAT_BLACK_KEYS;
    int w = -1, bl = -1, len = (int)(eng_project()->val[PG_STEP(ui.track) * 4 + 3] + 0.5f);
    for (int i = 0; i < 16; i++)
        if (W[i] == k)
            w = i;
    for (int i = 0; i < 11; i++)
        if (Bk[i] == k)
            bl = i;
    if (w >= 0) {
        int st = ui.spage * 16 + w;
        if (st >= len)
            return;
        if (down) {
            ui.held_step = st;
            ui.step_edit = 0;
            ui.step_new = !eng_trig(ui.track, st);
            if (ui.step_new) {
                eng_trig_set(ui.track, st, 1);
                dirty = 1;
            }
        } else if (st == ui.held_step) { /* let go: a trig that was there, not locked meanwhile, goes (its locks too) */
            if (!ui.step_new && !ui.step_edit) {
                eng_trig_set(ui.track, st, 0);
                eng_lock_clear(ui.track, st);
                dirty = 1;
            }
            ui.held_step = -1;
        }
    } else if (bl >= 0 && bl < 4 && down && bl * 16 < len) {
        ui.spage = bl;
    }
}

static void poll_keys(void)
{
    uint32_t keys = plat_keys(), k;
    if (keys == ui.keys)
        return;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int down = (keys & bit) && !(ui.keys & bit), up = !(keys & bit) && (ui.keys & bit);
        if ((!down && !up) || ui.mod)    /* MOD: the keys are the sources (held_source) */
            continue;
        if (ui.grid)
            grid_key((int)k, down);
        else
            eng_key(ui.track, 53 + (int)k + 12 * ui.octave, down);
    }
    ui.keys = keys;
}

static void input(void)
{
    uint32_t now = plat_ms(), btn = plat_buttons(), k;
    int32_t e;
    /* buttons: down, held long, up */
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if ((btn & bit) && !(ui.btn & bit)) {
            ui.btn |= bit;
            ui.down_at[k] = now;
            ui.long_done[k] = 0;
            button_down((int)k);
        } else if ((btn & bit) && !ui.long_done[k] && now - ui.down_at[k] >= LONG_MS) {
            ui.long_done[k] = 1;
            button_long((int)k);
        } else if (!(btn & bit) && (ui.btn & bit)) {
            ui.btn &= ~bit;
            button_up((int)k);
        }
    }
    ui.btn = btn;
    poll_keys();
    /* encoders */
    e = plat_enc(EN_PRESET);
    if (e) {
        int t = clamp(ui.track + e, 0, ENG_TRACKS - 1);
        if (t != ui.track) {
            for (k = 0; k < NKEYS; k++)  /* the keyboard's notes stop on the old track */
                if (ui.keys & (1u << k) && !ui.grid && !ui.mod)
                    eng_key(ui.track, 53 + (int)k + 12 * ui.octave, 0);
            ui.track = t;
            ui.held_step = -1;
            ui.page = track_page(ui.page, t);
            ui.full = 1;
        }
    }
    e = plat_enc(EN_SELECT);
    if (e && !ui.matrix && page_count(ui.page) > 1)
        go_page(page_at(ui.page, page_face(ui.page) + (e > 0 ? 1 : -1)));
    e = plat_enc(EN_ALGO);
    if (e && !ui.matrix && ptype(ui.page) <= PT_FX2) {
        project_t *p = eng_project();
        int first = page_first(ui.page), n = eng_algo_count(first);
        if (n) {
            int a = clamp((int32_t)p->algo[first] + e, 0, n - 1);
            if (held(B_GLO) && ptype(ui.page) <= PT_PLAITS3) {   /* GLO held: the four tracks on that model */
                for (int t = 0; t < ENG_TRACKS; t++)
                    p->algo[PG_TRACK(t, 0)] = (uint8_t)a;
                ui.used[B_GLO] = 1;
            } else {
                p->algo[first] = (uint8_t)a;
            }
            dirty = 1;
        }
    }
    for (int i = 0; i < 4; i++) {
        e = plat_enc(EN_K1 + i);
        if (!e || !data_ok)
            continue;
        if (ui.page == VP_SYS) {         /* the project REC held loads */
            if (i == 0)
                ui.init_sel = clamp(ui.init_sel + (e > 0 ? 1 : -1), 0, 2);
            continue;
        }
        if (ui.matrix) {
            matrix_turn(i, e);
            continue;
        }
        int param = knob_param(ui.page, i), h = ui.mod ? held_source() : 0;
        if (held(B_GLO))
            ui.used[B_GLO] = 1;
        if (ptype(ui.page) == PT_RND) {
            edit_rnd(param, e);
        } else if (held(B_ENV)) {
            ui.used[B_ENV] = 1;
            edit_rnd(param, e);
        } else if (ui.held_step >= 0) {
            edit_lock(param, e);
        } else if (h >= 1 && h <= 15) {
            cable_turn(key_source(h), param, e);
        } else if (h == 17 || h == 18) { /* the fans: A n (or B n) on knob n */
            cable_turn((h == 17 ? SRC_A1 : SRC_B1) + i, param, e);
        } else if (!held(B_GLO) || !edit_all(param, e)) {
            edit_param(param, e);
        }
        ui.touched_at[i] = now;
        ui.touched_k = i;
    }
    /* lights: the page's button, PLAY, SEL in MOD mode, REC with the steps; the keys: the steps (the one playing
     * blinks), or held, or the sources on this page */
    {
        static const uint8_t PAGE_BTN[PT_SYS + 1] = {B_EDIT, B_EDIT, B_EDIT, B_LFO, B_LFO, B_SEQ, B_SEQ, B_ARP, B_ARP,
                                                    B_HOME, B_FX, B_FX, B_HOME, B_SEQ, B_GLO, B_SEQ, B_GLO};
        static const uint8_t W[16] = PLAT_WHITE_KEYS, Bk[11] = PLAT_BLACK_KEYS;
        uint32_t bb = 0, kk = ui.keys;
        int pt = ptype(ui.page);
        if (!ui.matrix)
            bb |= 1u << (eng_steps_mode() && (pt == PT_MARB_T1 || pt == PT_MARB_T2) ? B_ARP : PAGE_BTN[pt]);
        if (ui.mod || ui.matrix)
            bb |= 1u << B_SEL;
        if (ui.grid || ui.page == VP_SYS)
            bb |= 1u << B_REC;
        if (eng_playing())
            bb |= LED_PLAY_GREEN;
        if (dirty)
            bb |= 1u << B_SAVE;
        if (ui.grid) {
            int len = (int)(eng_project()->val[PG_STEP(ui.track) * 4 + 3] + 0.5f), st = eng_step_now(ui.track);
            kk = 0;
            for (int i = 0; i < 16; i++)
                if (eng_trig(ui.track, ui.spage * 16 + i))
                    kk |= 1u << W[i];
            if (st >= ui.spage * 16 && st < ui.spage * 16 + 16)
                kk ^= 1u << W[st - ui.spage * 16];
            for (int j = 0; j < 4 && j * 16 < len; j++)
                if (j == ui.spage)
                    kk |= 1u << Bk[j];
        } else if (ui.mod) {
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
    ui.held_step = -1;
    ui.prev_page = PG_TRACK(0, 0);
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
