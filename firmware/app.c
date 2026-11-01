/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1: the screen and the panel. The sound is mi/engine.cc's (C++, compiled apart): this file edits its project
 * (eng_project) and shows what it plays.
 *
 * The screen is a module's face, dark, its lights in Mutable Instruments' colours (teal, orange, pink, as their
 * panels print them): at the top the module's icon in a lit ring, its name and a line on its state (Plaits shows
 * the model alone, its logo and its name); then its four knobs in a row, as KNOB 1 to 4 sit on the FM-1, each a ring
 * lit from its start (or its middle) to its value, the value in it; at the bottom the four tracks, an LED each lit by
 * its gate (on Tides' pages: its outputs, lit as high as they are; on the steps' and the effects' pages: the steps).
 * A module's faces slide in from the side SELECT turns to.
 *
 *   KNOB 1..4   the page's four parameters        SELECT   the module's other face (1/3, 2/3...)
 *   ALGORITHM   the page's model or mode (the delay: its time)   PRESETS  the active track, 1 to 4
 *   EDIT        Plaits, the active track          LFO      Tides A, then B
 *   SEQ         the lane's steps (a track's: NOTE, PROB, RATCHET, LENGTH, then its RANDOM; an effect's: DECAY,
 *               PROB, DEPTH, LENGTH)
 *   ARP         Endless, the track's: REC records (a key a note, SEL a rest), REC again plays it, a key transposes
 *               it; REC held clears it
 *   HOME        the mixer: the levels, the pans
 *   FX          the effects, one after the other: the delay, the reverb, Warps; SELECT their faces (their knobs,
 *               the tracks' sends, their steps)
 *   GLO         TEMPO, SCALE, ROOT, SWING; then the project (K1, REC held: INIT) and the machine. Held with a knob:
 *               ALL, the same parameter on the four tracks (with ALGORITHM: the same model), the screen yellow while
 *               GLO is held; for 4 s after, GLO undoes it as the next bar starts (again: it stays)
 *   ENV         the track's envelope (PLAITS 2/3: DECAY, LPG COLOUR, the internal envelope's TIMBRE and MORPH).
 *               Held with a knob: that parameter's random (right new at each trig, left looped, 0 none)
 *   REC         KEY mode (the keys play the track) or STEP mode: the white keys a page's 16 steps, the first seven
 *               black keys the lanes (the four tracks, the delay, the reverb, Warps), OCT- OCT+ the pages; a step
 *               held and a knob turned: a lock (the lane's knobs); a step held and SEL: its locks cleared. Held: the
 *               lane erased (with GLO: all of them)
 *   PLAY        the clock runs or stops           SAVE     held: the project saved (stopped)
 *   SEL         MOD mode: hold a source key, turn a knob: a cable. Held: the track's cables (MATRIX)
 *   keys        play the active track (MOD mode: Tides A's and B's outputs, then each fanned over the 4 knobs)
 *   OCT- OCT+   the keyboard's octave (STEP mode: the steps' page)
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
#define BG RGB(0, 0, 0)                  /* black: the LCD's darkest */
#define TXT RGB(240, 239, 235)
#define MUTED RGB(154, 151, 152)
#define DIM RGB(107, 104, 105)
#define TRACK RGB(60, 58, 59)            /* a ring's travel, unlit (1 px) */
#define OFF RGB(48, 46, 47)              /* an LED off */
#define RULE RGB(38, 36, 37)
#define SEL RGB(46, 44, 45)              /* the chosen row */
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
    int grid;                            /* STEP mode (REC): the white keys are the steps */
    int lane;                            /* the lane the steps show: a track, the delay, the reverb, Warps */
    int spage;                           /* the steps' page on the keys (16 each) */
    int fx_page;                         /* the effect's page FX goes back to */
    int body_panel, body_knobs;          /* the module's display, the knobs: again (an animation) */
    uint32_t slide_at;                   /* the knobs of another face sliding in: since when, from which side (+1 the
                                          * right, -1 the left), the page they push out */
    int slide_dir, slide_from;
    uint32_t anim_at;                    /* a page changed: its names written again, letters at random first */
    int anim;
    int held_step, step_new, step_edit;  /* a step held (-1: none), made by this press, locked meanwhile */
    int erec;                            /* the track Endless records (-1: none) */
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

#define MAXFACES 5
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
    case PT_MIX: case PT_PAN: case PT_DUCK: case PT_COMP: case PT_OTT:   /* the mixer: the levels, the pans, the
                                                                          * duck, the master bus (its two modes) */
        f[0] = PG_MIX, f[1] = PG_PAN, f[2] = PG_DUCK, f[3] = PG_COMP, f[4] = PG_OTT;
        return 5;
    case PT_STEP: case PT_RND: f[0] = PG_STEP(t); f[1] = VP_RND(t); return 2;
    case PT_ARP: f[0] = PG_ARP(t); return 1;
    case PT_DELAY1: case PT_DELAY2: case PT_DSEND: case PT_LANE: case PT_REVERB: case PT_RSEND:
    case PT_WARPS1: case PT_WARPS2: {    /* an effect: its knobs, the tracks' sends, its steps */
        int l = eng_page_lane(page);
        if (l == LANE_DELAY) {
            f[0] = PG_DELAY(0), f[1] = PG_DELAY(1), f[2] = PG_DSEND, f[3] = PG_STEP(LANE_DELAY);
            return 4;
        }
        if (l == LANE_REVERB) {
            f[0] = PG_REVERB, f[1] = PG_RSEND, f[2] = PG_STEP(LANE_REVERB);
            return 3;
        }
        f[0] = PG_WARPS(0), f[1] = PG_WARPS(1), f[2] = PG_STEP(LANE_WARPS);
        return 3;
    }
    default: f[0] = PG_GLOBAL; f[1] = VP_SYS; return 2;
    }
}
static int page_count(int page)
{
    int f[MAXFACES];
    return module_pages(page, f);
}
static int page_face(int page)           /* which face of its module */
{
    int f[MAXFACES], n = module_pages(page, f);
    for (int i = 0; i < n; i++)
        if (f[i] == page)
            return i;
    return 0;
}
static int page_first(int page)
{
    int f[MAXFACES];
    module_pages(page, f);
    return f[0];
}
static int page_at(int page, int face)   /* its module's page at face (wrapping) */
{
    int f[MAXFACES], n = module_pages(page, f);
    return f[((face % n) + n) % n];
}
static int track_page(int page, int t)   /* the same page on track t */
{
    switch (ptype(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: return PG_TRACK(t, page_face(page));
    case PT_STEP: return PG_STEP(t);
    case PT_ARP: return PG_ARP(t);
    case PT_RND: return VP_RND(t);
    default: return page;
    }
}

/* the lane a page's knobs belong to (its steps lock them), -1: the tracks' (the mixer, the sends) or none */
static int page_lane(int page)
{
    int t = ptrack(page);
    if (t >= 0)
        return t;
    if (page >= ENG_NPAGES || page == PG_DSEND || page == PG_RSEND)
        return -1;
    int l = eng_page_lane(page);
    return l >= ENG_TRACKS ? l : -1;
}

/* the parameter a knob turns: its page's, the RANDOM page's are its track's PAN, DECAY, NOTE and RATCHET (their
 * randoms) */
static int knob_param(int page, int k)
{
    if (ptype(page) == PT_RND)
        return eng_track_param(ptrack(page), RND_TP[k]);
    return page * 4 + k;
}

static const char *module_name(int page)
{
    switch (ptype(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: return "Plaits";
    case PT_TIDES1: case PT_TIDES2: return page >= PG_TIDES(1, 0) ? "Tides B" : "Tides A";
    case PT_MIX: case PT_PAN: case PT_DUCK: case PT_COMP: case PT_OTT: return "Mixer";
    case PT_STEP: case PT_RND: return "Steps";
    case PT_ARP: return "Endless";
    case PT_GLOBAL: case PT_SYS: return "Global";
    default: {
        int l = eng_page_lane(page);
        return l == LANE_DELAY ? "Delay" : l == LANE_REVERB ? "Reverb" : "Warps";
    }
    }
}

static const char *const LANE_SHORT[ENG_LANES] = {"T1", "T2", "T3", "T4", "DL", "RV", "WP"};

/* the colours of a module's state, as its icons print them: teal, orange, pink */
static uint16_t accent3(int i) { return i <= 0 ? TEAL : i == 1 ? ORANGE : PINK; }

static uint16_t model_accent(int m)      /* Plaits' banks: teal the classic models, pink the noisy and the drums,
                                          * orange the eight of 1.2 */
{
    return m < 8 ? ORANGE : m < 16 ? TEAL : PINK;
}
static uint16_t track_accent(int t) { return model_accent(eng_project()->algo[PG_TRACK(t, 0)]); }

static uint16_t warps_accent(void)      /* Warps' algorithm: the blends teal, the ring modulators and the logic
                                          * orange, the frequency shifter pink; off, grey */
{
    int a = eng_warps_algo();
    return a < 0 ? MUTED : a <= 1 ? TEAL : a <= 5 ? ORANGE : PINK;
}

static uint16_t lane_accent(int l)      /* a lane's colour: its track's model's, or its effect's */
{
    return l < ENG_TRACKS ? track_accent(l) : l == LANE_DELAY ? ORANGE : l == LANE_REVERB ? PINK : warps_accent();
}

static uint16_t page_accent(int page)
{
    project_t *p = eng_project();
    switch (ptype(page)) {
    case PT_STEP: case PT_RND: case PT_ARP: return track_accent(ptrack(page));
    case PT_PAN: case PT_GLOBAL: case PT_SYS: case PT_MIX: return TEAL;
    case PT_DUCK: return track_accent(0);    /* T1's: it ducks the others */
    case PT_COMP: return ORANGE;
    case PT_OTT: return PINK;
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: return model_accent(p->algo[page_first(page)]);
    case PT_TIDES1: case PT_TIDES2: return accent3(p->algo[page_first(page)]);
    default: return lane_accent(eng_page_lane(page));
    }
}
static uint16_t mod_colour(uint16_t ac) { return ac == TEAL ? PINK : TEAL; }   /* where the cables take a knob */

/* Plaits' models in four letters or so (the tracks' row), its banks */
static const char *const SHORT[24] = {"VCF", "PD", "6OP1", "6OP2", "6OP3", "TERR", "SMCH", "CHIP",
                                      "VA", "SHPR", "FM", "FRMT", "HARM", "WTBL", "CHRD", "SPCH",
                                      "SWRM", "NOIS", "PART", "STRG", "MODL", "KICK", "SNAR", "HHAT"};
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

static int text_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
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
    if (t >= 0 && t < ENG_TRACKS) {
        text_cat(b, "T", max);
        int_text(t + 1, n);
        text_cat(b, n, max);
    } else if (page < PG_MIX) {
        text_cat(b, page >= PG_TIDES(1, 0) ? "TB" : "TA", max);
    } else {
        int l = eng_page_lane(page);
        text_cat(b, l == LANE_DELAY ? "Dl" : l == LANE_REVERB ? "Rv" : l == LANE_WARPS ? "Wp" : "Gl", max);
    }
    (void)n;
    text_cat(b, " ", max);
    text_cat(b, eng_param_name(param), max);
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
    ui.track = ui.lane = 0;
    ui.page = ui.prev_page = PG_TRACK(0, 0);
    ui.fx_page = PG_DELAY(0);
    ui.touched_k = -1;
    ui.held_step = ui.erec = -1;
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

/* the knobs' rings, their pixels known once (where they are on the travel, how far out): a ring's arcs then cost a
 * pass over a table instead of a square root and an arctangent for each pixel around (the knobs are most of what the
 * screen draws) */
#define KNOB_CX 30.0f                    /* (RX, RY below) */
#define KNOB_CY 45.0f
#define LUT_R0 18.5f
#define LUT_R1 26.0f
#define LUT_MAX 1400
static struct {
    uint8_t x, y;
    uint16_t d;                          /* the distance, 1/256 px */
    int16_t u;                           /* the place on the travel, 1/8192 */
} ring_lut[LUT_MAX];
static int ring_lut_n = -1;

static void ring_lut_init(void)
{
    ring_lut_n = 0;
    for (int y = 0; y < 100; y++)
        for (int x = 0; x < 60; x++) {
            float dx = (float)x - KNOB_CX, dy = KNOB_CY - (float)y, d = fm_sqrtf(dx * dx + dy * dy);
            if (d < LUT_R0 - 0.6f || d > LUT_R1 + 0.6f || ring_lut_n >= LUT_MAX)
                continue;
            ring_lut[ring_lut_n].x = (uint8_t)x;
            ring_lut[ring_lut_n].y = (uint8_t)y;
            ring_lut[ring_lut_n].d = (uint16_t)(d * 256.0f + 0.5f);
            ring_lut[ring_lut_n].u = (int16_t)(travel_u(dx, dy) * 8192.0f);
            ring_lut_n++;
        }
}

static void arc(float cx, float cy, float r0, float r1, float u0, float u1, uint16_t c)   /* u0..u1, r0..r1 */
{
    BOX_OF(cx, cy, r1);
    if (u1 <= u0)
        return;
    if (cx == KNOB_CX && cy == KNOB_CY && r0 >= LUT_R0 && r1 <= LUT_R1) {   /* a knob's: from the table */
        if (ring_lut_n < 0)
            ring_lut_init();
        int32_t ua = (int32_t)((u0 - 0.02f) * 8192.0f), ub = (int32_t)((u1 + 0.02f) * 8192.0f);
        for (int i = 0; i < ring_lut_n; i++) {
            if (ring_lut[i].u < ua || ring_lut[i].u > ub)
                continue;
            float d = (float)ring_lut[i].d * (1.0f / 256.0f), u = (float)ring_lut[i].u * (1.0f / 8192.0f);
            float cov = fm_minf(r1 + 0.5f - d, d - r0 + 0.5f), e;
            if (cov <= 0.0f)
                continue;
            e = fm_minf(u - u0, u1 - u) * 4.712389f * d + 0.5f;
            if (e > 0.0f)
                blend_px(ring_lut[i].x, ring_lut[i].y, c, fm_minf(cov, 1.0f) * fm_minf(e, 1.0f));
        }
        return;
    }
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

/* light: a lit thing in its colour, flat (no glow: finer, Yoann's choice) */
static void lit_arc(float cx, float cy, float r0, float r1, float u0, float u1, uint16_t c)
{
    arc(cx, cy, r0, r1, u0, u1, c);
}
static void lit_disc(float cx, float cy, float r, uint16_t c) { disc(cx, cy, r, c); }
static void led(float cx, float cy, float r, uint16_t c, int lvl)   /* an LED: lvl 0 off .. 8 full */
{
    uint16_t col;
    if (lvl <= 0) {
        disc(cx, cy, r, OFF);
        return;
    }
    col = lvl >= 8 ? c : tint(c, BG, lvl);
    disc(cx, cy, r, col);
}

/* the icons, as strokes in a 32-unit box: Plaits' 24 models, Tides' ramps, the mixer, the reverb, the global page,
 * the projects, the steps, Warps' algorithms, the delay. L a line, C a circle, D a dot, R a rectangle (x y w h) */
typedef struct {
    char op;
    int8_t a, b, c, d;
} vop_t;
enum { IC_MODEL = 0, IC_RAMP = 24, IC_MIX = 27, IC_REV, IC_GLO, IC_NEW, IC_DEMO, IC_SAVED, IC_STEPS, IC_DICE,
       IC_ENDLESS, IC_WARPS, IC_DELAY = IC_WARPS + 7, IC_DUCK, IC_COMP, IC_OTT, NICON };
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
    {{'C', 10, 16, 6, 0}, {'C', 22, 16, 6, 0}},                                                     /* Endless */
    {{'L', 4, 8, 28, 24}, {'L', 4, 24, 28, 8}},                                                     /* Warps: xfade */
    {{'L', 4, 26, 10, 6}, {'L', 10, 6, 16, 18}, {'L', 16, 18, 22, 6}, {'L', 22, 6, 28, 26}},        /* fold */
    {{'C', 16, 16, 11, 0}, {'L', 10, 10, 22, 22}, {'L', 22, 10, 10, 22}},                           /* diode ring */
    {{'R', 5, 5, 22, 22}, {'L', 10, 10, 22, 22}, {'L', 22, 10, 10, 22}},                            /* digital ring */
    {{'L', 4, 22, 10, 22}, {'L', 10, 22, 10, 10}, {'L', 10, 10, 16, 10}, {'L', 16, 10, 16, 22}, {'L', 16, 22, 22, 22},
     {'L', 22, 22, 22, 10}, {'L', 22, 10, 28, 10}},                                                  /* xor */
    {{'L', 8, 7, 24, 16}, {'L', 24, 16, 8, 25}},                                                    /* comparator */
    {{'C', 9, 16, 4, 0}, {'L', 15, 16, 27, 16}, {'L', 22, 11, 27, 16}, {'L', 27, 16, 22, 21}},      /* freq shifter */
    {{'L', 5, 6, 5, 26}, {'L', 12, 9, 12, 23}, {'L', 19, 12, 19, 20}, {'L', 26, 14, 26, 18}},       /* the delay */
    {{'L', 4, 7, 9, 7}, {'L', 9, 7, 9, 25}, {'L', 9, 25, 14, 15}, {'L', 14, 15, 20, 10}, {'L', 20, 10, 28, 7}},
                                                                                                     /* the duck */
    {{'L', 5, 27, 27, 27}, {'L', 5, 27, 5, 5}, {'L', 5, 27, 16, 14}, {'L', 16, 14, 27, 10}},        /* a knee */
    {{'R', 4, 14, 6, 12}, {'R', 13, 8, 6, 18}, {'R', 22, 12, 6, 14}, {'L', 4, 6, 28, 6}},            /* OTT: 3 bands */
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
/* ALL (GLO held a moment, or with a knob): the screen yellow, everything on it knocked out in black, until GLO is let
 * go. Each canvas goes out through blit: its pixels by their brightness, from yellow (black) to black (white) */
#define YELLOW RGB(255, 214, 0)
static int knock;
/* another face of the module (SELECT): its knobs slide in, pushing the others out (each row drawn shifted by slide_s
 * px, only its part on the screen sent); the module's display stays, its names written again (dec) */
#define SLIDE_MS 250u
#define DECODE_MS 340u
static int32_t slide_s;
static void blit(uint32_t x, uint32_t y)
{
    if (knock) {
        for (uint32_t i = 0, n = cv_w * cv_h; i < n; i++) {
            uint32_t c = swap16(cv_px[i]);
            uint32_t l = ((c >> 11) * 16u + ((c >> 5) & 63u) * 20u + (c & 31u) * 8u) / 8u;   /* 0..250 */
            l = l * 3u / 2u;
            l = l > 255u ? 0u : 255u - l;   /* what is left of the yellow */
            cv_px[i] = swap16((uint16_t)((31u * l / 255u) << 11 | (53u * l / 255u) << 5));
        }
    }
    if (slide_s && y >= KNOB_Y && y < FOOT_Y) {
        int32_t sx = (int32_t)x + slide_s, c0 = sx < 0 ? -sx : 0, c1 = (int32_t)cv_w;
        if (sx + c1 > 240)
            c1 = 240 - sx;
        if (c1 <= c0)
            return;
        uint32_t w = (uint32_t)(c1 - c0);
        if (w != cv_w)                   /* the columns on the screen, packed (each row moves back: in order) */
            for (uint32_t r = 0; r < cv_h; r++)
                for (uint32_t i = 0; i < w; i++)
                    cv_px[r * w + i] = cv_px[r * cv_w + (uint32_t)c0 + i];
        lcd_blit((uint32_t)(sx + c0), y, w, cv_h, cv_px);
        return;
    }
    cv_blit(x, y);
}

/* the names of a page that changed (the module's, the display's title and line) written again: each letter at random
 * a moment, then the right one, left to right. A name that did not change stays. Slots: 0 the module's name, 1 and 2
 * the title's lines, 3 the line under it */
#define NSLOT 4
static float anim_p = 1.0f;              /* how far the names are (1: written) */
static char slot_was[NSLOT][28], slot_from[NSLOT][28];
static uint32_t dec_seed = 0x6D2B79F5u;
static const char *dec(const char *s, int slot)
{
    static char b[NSLOT][40];
    static const char G[] = "ABCDEFGHJKLMNPRSTUVWXYZ0123456789#%/*+";
    int n = 0;
    if (anim_p >= 1.0f || text_eq(s, slot_from[slot])) {
        if (anim_p >= 1.0f) {
            slot_was[slot][0] = 0;
            text_cat(slot_was[slot], s, sizeof slot_was[slot]);
        }
        return s;
    }
    while (s[n])
        n++;
    for (int i = 0; i < n && i < 39; i++) {
        float land = 0.25f + 0.7f * (float)(i + 1) / (float)n;   /* when this letter lands */
        dec_seed = dec_seed * 1664525u + 1013904223u;
        b[slot][i] = anim_p >= land || s[i] == ' ' || s[i] == '|' ? s[i] : G[(dec_seed >> 24) % (sizeof G - 1u)];
    }
    b[slot][n < 39 ? n : 39] = 0;
    return b[slot];
}

static void anim_start(void)             /* a page changed: its names again */
{
    memcpy(slot_from, slot_was, sizeof slot_from);
    ui.anim_at = plat_ms();
    ui.anim = 1;
}

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
    int tr = ptrack(ui.page), x, mode = ui.held_step >= 0 ? 5 : held(B_ENV) ? 4 : held(B_GLO) ? 3
                                       : ui.grid ? 6 : ui.erec >= 0 ? 2 : ui.mod;
    uint16_t ac = page_accent(ui.page);
    if (load < 0 || plat_ms() - load_at >= 250u) {
        load = load_now();
        load_at = plat_ms();
    }
    int32_t s[5] = {ui.page | ui.matrix << 8 | mode << 10 | (ui.held_step & 127) << 13 | ui.lane << 20, load,
                    eng_playing() | dirty << 1, ui.track | lane_accent(ui.grid ? ui.lane : ui.track) << 4, ac};
    if (!ui.full && !ui.anim && !memcmp(s, ui.head_shown, sizeof s))
        return;
    memcpy(ui.head_shown, s, sizeof s);
    cv_begin(240, HEAD_H, BG);
    label_text(ui.matrix ? "Matrix" : module_name(ui.page), t, sizeof t);
    x = cv_text(10, 6, &FONT_B, dec(t, 0), TXT);
    x = 10 + text_w(&FONT_B, t);         /* (its place: the name's, not the letters' on the way) */
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
        static const char *const M[7] = {"", "MOD", "REC", "ALL", "RND", "", "STEP"};
        if (mode == 3 && knock)          /* ALL in yellow and black: white on black here, black on yellow there */
            text_pill(x + 20, 7, &FONT_T, M[mode], C_WHITE, BG);
        else
            text_pill(x + 20, 7, &FONT_T, M[mode], mode == 3 ? TEAL : mode == 4 ? ORANGE : PINK,
                      mode == 4 ? BG : C_WHITE);
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
    for (int y = -6; y <= 6; y++)
        cv_rect(x, 14 + y, 11 - (iabs(y) * 11 + 3) / 6, 1, eng_playing() ? TEAL : DIM);
    if (tr >= 0 || ui.matrix || ui.grid) {   /* the track (STEP mode: the lane), ringed in its colour */
        int l = ui.grid ? ui.lane : ui.track;
        uint16_t tc = lane_accent(l);
        t[0] = 0;
        text_cat(t, LANE_SHORT[l], sizeof t);
        pill(x - 34, 6, 26, 16, tc);
        pill(x - 32, 8, 22, 12, BG);
        cv_text(x - 21 - text_w(&FONT_XS, t) / 2, 7, &FONT_XS, t, tc);
    }
    blit(0, 0);
}

/* the module's display: its icon in a lit ring, its name, a line on its state ---------------------------------- */
#define HX 41.0f
#define HY 35.0f
static void hero_icon(int which, uint16_t c)
{
    ring(HX, HY, 27.0f, 2.0f, c);
    vicon(HX, HY, 1.05f, 2.4f, c, which, 0);
}

static int hero_title(const char *name)  /* the name: one line of L, two cut at a space, else M; the y under it */
{
    char a[24], b[24];
    int n = 0;
    if (text_w(&FONT_L, name) <= 150) {
        cv_text(84, 6, &FONT_L, dec(name, 1), C_WHITE);
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
            cv_text(84, -4, &FONT_L, dec(a, 1), C_WHITE);
            cv_text(84, 22, &FONT_L, dec(b, 2), C_WHITE);
            return 55;
        }
    }
    cv_text(84, 12, &FONT_M, dec(name, 1), C_WHITE);
    return 44;
}

static void hero_line(int y, const char *s)   /* the line under the name: its parts ('|') apart, a dot between */
{
    char part[40];
    int x = 85, n = 0;
    s = dec(s, 3);
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
        int m = p->algo[PG_TRACK(t, 0)];
        if (eng_model_enveloped(m)) {    /* its own envelope: the LPG is out of its way */
            int y = hero_title("ENVELOPE");
            hero_line(y, m >= 19 ? "NO LPG|DECAY: ITS DECAY" : "NO LPG|IN ITS PRESET");
            return;
        }
        cv_text(84, 0, &FONT_XS, dec("LOW-PASS GATE", 1), MUTED);
        cv_text(232 - text_w(&FONT_XS, colour < 0.5f ? "VCFA" : "VCA"), 0, &FONT_XS,
                colour < 0.5f ? "VCFA" : "VCA", DIM);
        int reveal = anim_p >= 1.0f ? 146 : (int)(146.0f * (1.0f - (1.0f - anim_p) * (1.0f - anim_p)));
        for (int x = 0; x < reveal; x++) {   /* (a page changed: the curve drawn in) */
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
    } else {                             /* the note now, the gate's length, the envelope on the pitch */
        int pg = PG_TRACK(t, 2), on = (int)((eng_gates() >> t) & 1u), x;
        char v[12];
        note_text(eng_note(t), n);
        x = cv_text(84, 6, &FONT_L, dec(n, 1), on ? C_WHITE : MUTED);
        x = 84 + text_w(&FONT_L, n);
        led((float)(x + 12), 22.0f, 5.0f, ac, on ? 8 : 0);
        s[0] = 0;
        text_cat(s, "GATE ", sizeof s);
        eng_param_text(pg * 4 + 1, eng_final(pg * 4 + 1), v, sizeof v);
        text_cat(s, v, sizeof s);
        text_cat(s, "|PITCH ENV ", sizeof s);
        eng_param_text(pg * 4, eng_final(pg * 4), v, sizeof v);
        text_cat(s, v, sizeof s);
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

static int meter_lit(int t) { return (int)(fm_clampf(eng_meter(t) * 1.25f, 0.0f, 1.0f) * 7.0f + 0.5f); }

static int bus_mode(void) { return eng_project()->algo[PG_COMP] % 3; }   /* MB_OFF, MB_COMP, MB_OTT */

static void panel_duck(void)             /* the duck: T1's trigs, its decay */
{
    char s[40], v[16];
    hero_icon(IC_DUCK, track_accent(0));
    int y = hero_title("DUCK");
    s[0] = 0;
    text_cat(s, "AT T1'S TRIGS|DECAY ", sizeof s);
    eng_param_text(PG_DUCK * 4, eng_final(PG_DUCK * 4), v, sizeof v);
    text_cat(s, v, sizeof s);
    hero_line(y, s);
}

static void panel_bus(int ott)           /* the master bus: this face's mode, on or not */
{
    static const char *const M[3] = {"MASTER BUS OFF", "COMP ON", "OTT ON"};
    int m = bus_mode(), on = m == (ott ? MB_OTT : MB_COMP);
    char s[48];
    hero_icon(ott ? IC_OTT : IC_COMP, on ? (ott ? PINK : ORANGE) : MUTED);
    int y = hero_title(ott ? "OTT" : "COMP");
    s[0] = 0;
    text_cat(s, on ? M[m] : m == MB_OFF ? M[0] : ott ? "COMP ON" : "OTT ON", sizeof s);
    text_cat(s, on ? "|LIMITER" : "|ALGO: THIS ONE", sizeof s);
    hero_line(y, s);
}

static void panel_mixer(int pan)
{
    hero_icon(IC_MIX, TEAL);
    hero_line(hero_title(pan ? "PAN" : "MIXER"), "4 TRACKS");
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

static void panel_steps(int l)            /* a lane's steps: how many, how many trigs; an effect's: held without */
{
    static const char *const FX[3] = {"DELAY STEPS", "REVERB STEPS", "WARPS STEPS"};
    char s[48], n[8];
    int len = (int)(eng_project()->val[PG_STEP(l) * 4 + 3] + 0.5f), trigs = 0;
    for (int i = 0; i < len; i++)
        trigs += eng_trig(l, i);
    hero_icon(IC_STEPS, lane_accent(l));
    s[0] = 0;
    if (l < ENG_TRACKS) {
        text_cat(s, "TRACK ", sizeof s);
        int_text(l + 1, n);
        text_cat(s, n, sizeof s);
    } else {
        text_cat(s, FX[l - ENG_TRACKS], sizeof s);
    }
    int y = hero_title(s);
    s[0] = 0;
    int_text(len, n);
    text_cat(s, n, sizeof s);
    text_cat(s, " STEPS|", sizeof s);
    if (l >= ENG_TRACKS && !eng_lane_has_trigs(l)) {
        text_cat(s, "NO TRIG: ALWAYS ON", sizeof s);
    } else {
        int_text(trigs, n);
        text_cat(s, n, sizeof s);
        text_cat(s, l < ENG_TRACKS ? " TRIGS" : " TRIGS OPEN IT", sizeof s);
    }
    hero_line(y, s);
}

static void panel_random(int t)
{
    hero_icon(IC_DICE, track_accent(t));
    hero_line(hero_title("RANDOM"), "RIGHT: NEW|LEFT: LOOP");
}

static void panel_endless(int t)         /* Endless: recording, or how many notes, the key's transposition */
{
    project_t *p = eng_project();
    char s[48], n[8];
    int len = p->endless_len[t], notes = 0;
    for (int i = 0; i < len; i++)
        notes += p->endless[t][i] != ENDLESS_REST;
    hero_icon(IC_ENDLESS, ui.erec == t ? PINK : track_accent(t));
    int y = hero_title(ui.erec == t ? "RECORDING" : "ENDLESS");
    s[0] = 0;
    if (ui.erec == t) {
        text_cat(s, "A KEY: A NOTE|SEL: A REST", sizeof s);
    } else if (!len) {
        text_cat(s, "EMPTY|REC: RECORD", sizeof s);
    } else {
        int_text(notes, n);
        text_cat(s, n, sizeof s);
        text_cat(s, " NOTES|", sizeof s);
        int_text(len - notes, n);
        text_cat(s, n, sizeof s);
        text_cat(s, " RESTS|KEY ", sizeof s);
        int_text(p->endless_key[t], n);
        if (p->endless_key[t] > 0)
            text_cat(s, "+", sizeof s);
        text_cat(s, n, sizeof s);
    }
    hero_line(y, s);
}

static void panel_global(void)           /* the tempo, the scale, the swing */
{
    project_t *p = eng_project();
    char s[40], v[16];
    hero_icon(IC_GLO, TEAL);
    eng_param_text(PG_GLOBAL * 4, p->val[PG_GLOBAL * 4], v, sizeof v);
    int y = hero_title(v);
    s[0] = 0;
    if ((int)(p->val[PG_GLOBAL * 4 + 1] + 0.5f) == 0) {
        text_cat(s, "NO SCALE", sizeof s);
    } else {
        eng_param_text(PG_GLOBAL * 4 + 2, p->val[PG_GLOBAL * 4 + 2], v, sizeof v);
        text_cat(s, v, sizeof s);
        text_cat(s, " ", sizeof s);
        eng_param_text(PG_GLOBAL * 4 + 1, p->val[PG_GLOBAL * 4 + 1], v, sizeof v);
        label_text(v, v, sizeof v);
        text_cat(s, v, sizeof s);
    }
    text_cat(s, "|SWING ", sizeof s);
    eng_param_text(PG_GLOBAL * 4 + 3, p->val[PG_GLOBAL * 4 + 3], v, sizeof v);
    text_cat(s, v, sizeof s);
    hero_line(y, s);
}

static void panel_warps(void)            /* Warps: its algorithm, its marks, what it takes and where it goes */
{
    project_t *p = eng_project();
    int a = eng_warps_algo(), car = (int)(p->val[PG_WARPS(1) * 4] + 0.5f), mod = (int)(p->val[PG_WARPS(1) * 4 + 1] + 0.5f);
    float alg = eng_final(PG_WARPS(0) * 4);
    uint16_t ac = warps_accent();
    char s[48], v[16], u[16];
    hero_icon(IC_WARPS + (a < 0 ? clamp((int)(alg * 5.0f + 0.5f), 0, 5) : a), ac);
    int y = hero_title(eng_warps_name(a));
    if (a < 0) {
        hero_line(y, "OFF|A CARRIER ON PAGE 2");
        return;
    }
    s[0] = 0;
    if (a != 6 || car < WC_OSC1) {       /* the carrier: a track, or the oscillator's shape */
        eng_param_text(PG_WARPS(1) * 4, (float)car, v, sizeof v);
        label_text(v, u, sizeof u);
        text_cat(s, u, sizeof s);
        text_cat(s, car >= WC_OSC1 ? " OSC " : " ", sizeof s);
    }
    text_cat(s, a == 6 ? "SHIFTS T" : "BY T", sizeof s);
    v[0] = (char)('1' + mod);
    v[1] = 0;
    text_cat(s, v, sizeof s);
    text_cat(s, "|ON T", sizeof s);
    v[0] = (char)('1' + (car >= WC_T1 && car <= WC_T4 ? car - WC_T1 : mod));
    text_cat(s, v, sizeof s);
    hero_line(y, s);
    if (a == 6)
        return;
    for (int i = 0; i < 6; i++) {        /* the knob's six marks, the cross-modulations; lit as near (they blend) */
        float d = alg * 5.0f - (float)i;
        int lv = (int)((1.0f - (d < 0 ? -d : d)) * 8.0f + 0.5f);
        led((float)(89 + i * 12), (float)(y + 26), 3.0f, ac, lv < 0 ? 0 : lv);
    }
}

static void panel_reverb(void)
{
    hero_icon(IC_REV, PINK);
    hero_line(hero_title("REVERB"), ui.page == PG_RSEND ? "FROM CLOUDS|THE SENDS" : "FROM CLOUDS");
}

static void panel_delay(void)            /* the delay: its time, its mode, its repeats */
{
    project_t *p = eng_project();
    char s[48], v[16];
    hero_icon(IC_DELAY, ORANGE);
    int y = hero_title("DELAY");
    s[0] = 0;
    if (ui.page == PG_DSEND)
        text_cat(s, "THE SENDS|", sizeof s);
    eng_param_text(PG_DELAY(0) * 4, eng_final(PG_DELAY(0) * 4), v, sizeof v);
    text_cat(s, v, sizeof s);
    if (ui.page != PG_DSEND) {
        text_cat(s, "|", sizeof s);
        enum_cat(s, PG_DELAY(1) * 4 + 1, sizeof s);
        text_cat(s, "|FB ", sizeof s);
        eng_param_text(PG_DELAY(0) * 4 + 1, eng_final(PG_DELAY(0) * 4 + 1), v, sizeof v);
        text_cat(s, v, sizeof s);
    }
    (void)p;
    hero_line(y, s);
}

/* a page's meter, alone (the rest is not drawn again for it): an effect's return (the delay's, the reverb's; how open
 * its lane holds it: the row at the bottom), the duck (how deep now), the master bus (the compressor's reduction, from
 * the top; OTT's three bands, each up or down from the middle) */
static void fx_meter(int force)
{
    static int shown = -1;
    int pt = ptype(ui.page), l = ui.page < ENG_NPAGES ? eng_page_lane(ui.page) : -1, m;
    if (pt == PT_DUCK || pt == PT_COMP || pt == PT_OTT) {
        int mode = bus_mode(), h[3] = {0, 0, 0}, key;
        if (pt == PT_DUCK)
            h[0] = (int)(eng_duck() * 52.0f + 0.5f);
        else if (mode == MB_COMP)
            h[0] = (int)(fm_clampf(-eng_gr(0) / 18.0f, 0.0f, 1.0f) * 52.0f + 0.5f);
        else if (mode == MB_OTT)
            for (int b = 0; b < 3; b++)
                h[b] = (int)(fm_clampf(eng_gr(b) / 24.0f, -1.0f, 1.0f) * 26.0f);
        key = (h[0] + 64) | (h[1] + 64) << 7 | (h[2] + 64) << 14 | pt << 21 | mode << 26;
        if (!force && key == shown)
            return;
        shown = key;
        cv_begin(10, 52, BG);
        if (pt == PT_DUCK) {             /* down from the top: how much the tracks are ducked */
            cv_rect(2, 0, 6, 52, TRACK);
            cv_rect(2, 0, 6, h[0], track_accent(0));
        } else if (mode == MB_COMP) {    /* down from the top: the reduction (18 dB the whole bar) */
            cv_rect(2, 0, 6, 52, TRACK);
            cv_rect(2, 0, 6, h[0], ORANGE);
        } else if (mode == MB_OTT) {     /* low, mid, high: up raised, down squashed (24 dB half the bar) */
            for (int b = 0; b < 3; b++) {
                cv_rect(b * 4, 0, 2, 52, TRACK);
                if (h[b] > 0)
                    cv_rect(b * 4, 26 - h[b], 2, h[b], PINK);
                else if (h[b] < 0)
                    cv_rect(b * 4, 26, 2, -h[b], TEAL);
                cv_rect(b * 4, 25, 2, 2, C_WHITE);
            }
        }
        blit(226, PANEL_Y + 12);
        return;
    }
    if (l != LANE_DELAY && l != LANE_REVERB)
        return;
    m = (int)(fm_clampf(eng_meter(l == LANE_DELAY ? ENG_TRACKS + 1 : ENG_TRACKS) * 2.0f, 0.0f, 1.0f) * 52.0f);
    if (!force && (m | l << 8) == shown)
        return;
    shown = m | l << 8;
    cv_begin(6, 52, TRACK);
    cv_rect(0, 52 - m, 6, m, lane_accent(l));
    blit(226, PANEL_Y + 12);
}

static void draw_panel(void)
{
    project_t *p = eng_project();
    int pt = ptype(ui.page), tr = ptrack(ui.page);
    int32_t s[3] = {ui.page, eng_algo_count(page_first(ui.page)) ? p->algo[page_first(ui.page)] : -1, 0};
    int fx = pt == PT_DELAY1 || pt == PT_DELAY2 || pt == PT_DSEND || pt == PT_REVERB || pt == PT_RSEND ||
             pt == PT_WARPS1 || pt == PT_WARPS2 || pt == PT_LANE || pt == PT_DUCK || pt == PT_COMP || pt == PT_OTT;
    if (pt == PT_COMP || pt == PT_OTT)
        s[1] = bus_mode();
    if (pt == PT_DUCK)
        s[2] = (int)(p->val[PG_DUCK * 4] * 1000.0f);
    switch (pt) {                        /* what the display shows, to draw it again only when it changes */
    case PT_PLAITS2: s[2] = (int)(eng_final(ui.page * 4) * 200.0f) | (int)(eng_final(ui.page * 4 + 1) * 8.0f) << 8; break;
    case PT_PLAITS3: s[2] = eng_note(tr) | (int)((eng_gates() >> tr) & 1u) << 8 |
                            (int)(eng_final(ui.page * 4) * 50.0f + 50.0f) << 9 | (int)(eng_final(ui.page * 4 + 1) * 100.0f) << 16;
        break;
    case PT_TIDES1: case PT_TIDES2: {
        int pg = PG_TIDES(ui.page >= PG_TIDES(1, 0), 1);
        s[2] = (int)p->val[pg * 4 + 1] | (int)p->val[pg * 4 + 2] << 4 | (int)p->val[pg * 4 + 3] << 8;
        break;
    }
    case PT_STEP: case PT_LANE: {
        int l = eng_page_lane(ui.page);
        for (int i = 0; i < ENG_STEPS / 8; i++)
            s[2] = s[2] * 31 + p->trig[l][i];
        s[2] = s[2] * 7 + 11 * (int)p->val[PG_STEP(l) * 4 + 3] + (l == LANE_WARPS ? eng_warps_algo() * 3 : 0);
        break;
    }
    case PT_DELAY1: case PT_DELAY2: case PT_DSEND:
        s[2] = (int)p->val[PG_DELAY(0) * 4] | (int)p->val[PG_DELAY(1) * 4 + 1] << 5 |
               (int)(eng_final(PG_DELAY(0) * 4 + 1) * 100.0f) << 6;
        break;
    case PT_ARP:
        s[2] = p->endless_len[tr] | (p->endless_key[tr] + 64) << 7 | (ui.erec == tr) << 14;
        for (int i = 0; i < p->endless_len[tr]; i++)
            s[2] = s[2] * 31 + (p->endless[tr][i] == ENDLESS_REST);
        break;
    case PT_GLOBAL:
        s[2] = (int)p->val[PG_GLOBAL * 4 + 1] | (int)p->val[PG_GLOBAL * 4 + 2] << 4 |
               (int)(p->val[PG_GLOBAL * 4 + 3] * 100.0f) << 8 | (int)(p->val[PG_GLOBAL * 4] * 1000.0f) << 16;
        break;
    case PT_MIX: case PT_PAN:
        for (int t = 0; t < ENG_TRACKS; t++)
            s[2] |= meter_lit(t) << (t * 3) | (int)(p->algo[PG_TRACK(t, 0)] / 8) << (12 + t * 2);
        break;
    case PT_WARPS1: case PT_WARPS2:
        s[2] = (int)(eng_final(PG_WARPS(0) * 4) * 64.0f) | (int)p->val[PG_WARPS(1) * 4] << 7 |
               (int)p->val[PG_WARPS(1) * 4 + 1] << 11 | (int)p->val[PG_WARPS(1) * 4 + 3] << 13;
        break;
    default: break;
    }
    if (!ui.full && !ui.body_panel && !memcmp(s, ui.panel_shown, sizeof s)) {
        if (fx)
            fx_meter(0);
        return;
    }
    memcpy(ui.panel_shown, s, sizeof s);
    cv_begin(240, PANEL_H, BG);
    switch (pt) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: panel_plaits(tr, page_face(ui.page), page_accent(ui.page)); break;
    case PT_TIDES1: case PT_TIDES2: panel_tides(ui.page >= PG_TIDES(1, 0)); break;
    case PT_MIX: case PT_PAN: panel_mixer(pt == PT_PAN); break;
    case PT_DUCK: panel_duck(); break;
    case PT_COMP: case PT_OTT: panel_bus(pt == PT_OTT); break;
    case PT_STEP: case PT_LANE: panel_steps(eng_page_lane(ui.page)); break;
    case PT_RND: panel_random(tr); break;
    case PT_ARP: panel_endless(tr); break;
    case PT_GLOBAL: panel_global(); break;
    case PT_WARPS1: case PT_WARPS2: panel_warps(); break;
    case PT_DELAY1: case PT_DELAY2: case PT_DSEND: panel_delay(); break;
    default: panel_reverb(); break;
    }
    blit(0, PANEL_Y);
    if (fx)
        fx_meter(1);
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
#define R0 22.5f                         /* a fine ring: 2 px lit, its travel 1 px */
#define R1 24.5f
static void ring_end(float u, uint16_t c, int glow)   /* a lit travel's round end */
{
    float x, y;
    travel_xy(RX, RY, u, (R0 + R1) * 0.5f, &x, &y);
    if (glow)
        lit_disc(x, y, (R1 - R0) * 0.5f, c);
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
    if (text_w(&FONT_B, a) > 40) {       /* "120 BPM", "0.89 Hz": the number in the ring, the unit under it */
        for (int i = 0; a[i]; i++)
            if (a[i] == ' ')
                sp = i;
        if (sp > 0) {
            text_cat(under, a + sp + 1, max);
            a[sp] = 0;
        }
    }
    const felucca_font_t *f = text_w(&FONT_B, a) <= 40 ? &FONT_B : text_w(&FONT_XS, a) <= 42 ? &FONT_XS : &FONT_T;
    if (text_w(f, a) > 44) {             /* still too wide: under the ring */
        under[0] = 0;
        text_cat(under, a, max);
        return;
    }
    cv_text(30 - text_w(f, a) / 2, (int)RY - f->h / 2 - 1, f, a, c);
}

/* what a knob shows: its value (and where the cables take it), or a held step's lock, or a random's depth */
enum { KV_VALUE, KV_LOCK, KV_RND };
static int knob_view(int page, int param, int *t, int *tp)
{
    *tp = eng_param_tp(param, t);
    if (ptype(page) == PT_RND)
        return KV_RND;
    if (*tp < 0 || !eng_tp_lockable(*t, *tp))
        return KV_VALUE;
    if (held(B_ENV))
        return KV_RND;
    if (ui.held_step >= 0 && *t == ui.lane)
        return KV_LOCK;
    return KV_VALUE;
}

/* a knob that does nothing now, drawn grey (it still turns): the LPG's colour on a model with its own envelope; an
 * effect lane's DECAY and DEPTH while it has no trig (the effect held open) */
static int knob_idle(int param)
{
    int page = param / 4, k = param % 4;
    if (page < 12 && page % 3 == 1 && k == 1)
        return eng_model_enveloped(eng_project()->algo[page - 1]);
    if (page == PG_COMP || page == PG_OTT)
        return bus_mode() != (page == PG_OTT ? MB_OTT : MB_COMP);
    if (ptype(page) == PT_LANE && k != 3)
        return !eng_lane_has_trigs(eng_page_lane(page));
    return 0;
}

/* a choice's name in the ring, short (a word, or three or four letters) */
static const char *ring_short(const char *s)
{
    static const char *const M[][2] = {
        {"PING-PONG", "PING"}, {"FORWARD", "FWD"}, {"BACKWARD", "BWD"}, {"RANDOM", "RND"}, {"AMPLITUDE", "AMP"},
        {"FREQUENCY", "FREQ"}, {"MEDIUM", "MED"}, {"WARPS", "XMOD"}, {"FREQ SHIFT", "SHIFT"}, {"TRIANGLE", "TRI"},
        {"MAJOR", "MAJ"}, {"MINOR", "MIN"}, {"DORIAN", "DOR"}, {"PHRYGIAN", "PHR"}, {"LYDIAN", "LYD"},
        {"MIXOLYDIAN", "MIX"}, {"LOCRIAN", "LOC"}, {"HARM. MINOR", "HMIN"}, {"MEL. MINOR", "MMIN"},
        {"MAJ. PENTA", "PMAJ"}, {"MIN. PENTA", "PMIN"}, {"WHOLE TONE", "WHOLE"}, {"HIRAJOSHI", "HIRA"},
        {"DIGITAL", "DIGI"}, {"COMPAR", "COMP"}};
    for (uint32_t i = 0; i < sizeof M / sizeof M[0]; i++)
        if (text_eq(s, M[i][0]))
            return M[i][1];
    return s;
}

/* a knob's name: the module's, short (fm1's FONT_S, 52 px at most: the knobs apart); on the pages of the four
 * tracks' (the levels, the pans, the sends) the track, in its colour */
static int track_knobs(int page)
{
    int pt = ptype(page);
    return pt == PT_MIX || pt == PT_PAN || pt == PT_RSEND || pt == PT_DSEND || pt == PT_DUCK;
}

static void draw_knob_at(int page, int k, int force)
{
    static const char *const RND_NAME[4] = {"Pan", "Decay", "Tune", "Ratch"};
    project_t *p = eng_project();
    int param = knob_param(page, k), t, tp, view = knob_view(page, param, &t, &tp), locked = 0, rnd = 0;
    const eng_pdef_t *d = eng_pdef(param);
    float v = p->val[param], f = eng_final(param), u, uf;
    char txt[32], c[32], lab[24], under[32];
    int hash = 0, hot = page == ui.page && ui.touched_k == k && plat_ms() - ui.touched_at[k] < 1200u, ncab = 0;
    int bip = d->kind == K_BIP || (d->kind == K_INT && d->lo < 0);
    uint16_t ac = page_accent(page), mc = mod_colour(ac), vc;
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
    int idle = view == KV_VALUE && knob_idle(param);
    if (idle)
        ac = mc = DIM;
    vc = view == KV_RND ? (rnd < 0 ? mc : ac) : locked ? mc : ac;
    int tk = track_knobs(page) && view == KV_VALUE && !(ptype(page) == PT_DUCK && k == 0);
    int act = tk && k == (ui.grid && ui.lane < ENG_TRACKS ? ui.lane : ui.track);   /* the track played: knocked out */
    if (tk)
        lab[0] = 0, text_cat(lab, LANE_SHORT[k], sizeof lab);
    else
        label_text(ptype(page) == PT_RND ? RND_NAME[k] : eng_param_name(param), lab, sizeof lab);
    int32_t s[4] = {(int32_t)(view == KV_RND ? (float)(rnd + 100) : knob_u(param, v) * 270.0f) |
                    (int32_t)(knob_u(param, f) * 270.0f) << 10 | view << 20 | locked << 22,
                    text_hash(txt) * 31 + text_hash(lab), hash, hot | ui.mod << 1 | (int32_t)ac << 2 | act << 18};
    if (!force && !ui.full && !ui.body_knobs && !memcmp(s, ui.knob_shown[k], sizeof s))
        return;
    if (page == ui.page)
        memcpy(ui.knob_shown[k], s, sizeof s);
    if (slide_s && (k * KNOB_W + slide_s >= 240 || k * KNOB_W + KNOB_W + slide_s <= 0))
        return;                          /* sliding, not on the screen */
    cv_begin(KNOB_W, KNOB_H, BG);
    {                                    /* its name (touched: white, underlined) */
        uint16_t lc = hot ? C_WHITE : view != KV_VALUE ? vc : tk ? track_accent(k) : MUTED;
        const felucca_font_t *lf = text_w(&FONT_S, lab) <= 58 ? &FONT_S : &FONT_XS;
        int w = text_w(lf, lab);
        if (act) {                       /* the active track: its name knocked out of a block of its colour */
            cv_rect(30 - w / 2 - 5, 0, w + 10, 17, track_accent(k));
            lc = BG;
        }
        cv_text(30 - w / 2, 0, lf, lab, lc);
        if (hot)
            cv_rect(30 - w / 2, 17, w, 1, ac);
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
                lit_arc(RX, RY, R0, R1, a, b, ac);
            else
                arc(RX, RY, R0 + 0.5f, R1 - 0.5f, a, b, TRACK);
        }
        label_text(txt, nm, sizeof nm);
        ring_text(ring_short(nm), idle ? DIM : C_WHITE, under, sizeof under);
    } else {
        float lo = bip ? fm_minf(u, 0.5f) : 0.0f, hi = bip ? fm_maxf(u, 0.5f) : u;
        arc(RX, RY, R0 + 0.5f, R1 - 0.5f, 0.0f, 1.0f, TRACK);
        if (hi - lo > 0.004f) {
            lit_arc(RX, RY, R0, R1, lo, hi, vc);
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
                arc(RX, RY, R0 - 3.0f, R0 - 1.6f, q, fm_minf(q + 0.025f, b), mc);
            float x, y;
            travel_xy(RX, RY, uf, (R0 + R1) * 0.5f, &x, &y);
            lit_disc(x, y, 2.0f, mc);
        }
        ring_text(txt, ncab ? mc : (view == KV_RND && !rnd) || idle ? DIM : C_WHITE, under, sizeof under);
    }
    if (ncab)                            /* under the ring: the cables, a lock, a random, else a unit, MOD's socket */
        cv_text(30 - text_w(&FONT_XS, c) / 2, 78, text_w(&FONT_XS, c) <= 58 ? &FONT_XS : &FONT_T, c, mc);
    else if (under[0])
        cv_text(30 - text_w(&FONT_XS, under) / 2, 78, &FONT_XS, under, view != KV_VALUE ? vc : MUTED);
    else if (ui.mod && eng_param_mod(param))
        ring(30.0f, 85.0f, 4.5f, 1.4f, DIM);
    blit((uint32_t)(k * KNOB_W), KNOB_Y);
}
static void draw_knob(int k) { draw_knob_at(ui.page, k, 0); }

/* lists: the matrix, the global page, the missing tables --------------------------------------------------- */
static void draw_lines(int y, int h, const char *const *lines, const uint16_t *colours, int n, int sel)
{
    for (int i = 0; i < n; i++) {
        cv_begin(240, (uint32_t)h, BG);
        if (i == sel)
            pill(4, 1, 232, h - 2, SEL);
        cv_text(10, (h - 15) / 2, &FONT_XS, lines[i], i == sel ? C_WHITE : colours[i]);
        blit(0, (uint32_t)(y + i * h));
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
    if (!ui.full && !ui.body_panel && h == shown)
        return;
    shown = h;
    cv_begin(240, 24, BG);
    cv_text(10, 0, &FONT_XS, g < ENG_TRACKS ? "THE TRACK'S CABLES" : "GLOBAL CABLES (TIDES, THE EFFECTS)", TXT);
    cv_text(10, 12, &FONT_T, "K1 CABLE   K2 SOURCE   K3 TARGET   K4 DEPTH", DIM);
    blit(0, PANEL_Y);
    draw_lines(PANEL_Y + 24, 19, lines, col, 8, ui.msel - first);
}

static void draw_projects(void)          /* the projects REC held loads, in the module's display */
{
    static int shown = -1;
    static const char *const P[3] = {"New: 4 Plaits, nothing patched", "The demo patch", "The saved project"};
    if (ui.full || ui.body_panel || shown != ui.init_sel) {
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
        blit(0, PANEL_Y);
    }
}

static void draw_machine(int force)      /* the machine, where the knobs are */
{
    static uint32_t at;
    char v[24];
    if (!force && !ui.full && !ui.body_knobs && plat_ms() - at < 400u)
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
            l = "Core 0";
            int_text((int32_t)((eng_load(0) + 5u) / 10u), v);
            text_cat(v, "%", sizeof v);
            break;
        case 1:
            l = "Core 1";
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
        case 3: {                        /* what each costs: the four tracks (while they play), Warps, the effects
                                          * and the master bus (the cores share them by it) */
            char n[8];
            n[0] = 0;
            l = "T1 T2 T3 T4 Warps FX";
            for (int t = 0; t <= ENG_TRACKS + 1; t++) {
                if (t == ENG_TRACKS && !eng_task_load(t))
                    n[0] = '-', n[1] = 0;         /* Warps off */
                else
                    int_text((int32_t)((eng_task_load(t) + 5u) / 10u), n);
                text_cat(v, n, sizeof v);
                text_cat(v, t <= ENG_TRACKS ? " " : "%", sizeof v);
            }
            break;
        }
        case 4: {                        /* the output (K2): stereo; a headset with a mic (its 4-pole plug in the
                                          * 3-pole jack: its earpieces in series between left and right, they get
                                          * left minus right, so the right inverted gives them the whole mix); mono */
            static const char *const O[3] = {"Stereo", "Mic headset", "Mono"};
            l = "Output (K2)";
            text_cat(v, O[eng_project()->out_mode % 3], sizeof v);
            c = eng_project()->out_mode ? ORANGE : TXT;
            break;
        }
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
            blit(0, (uint32_t)(KNOB_Y + (i < 3 ? 0 : 49)));
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

/* the row at the bottom: the four tracks (an LED lit by its gate or its trig, in its model's colour); on Tides' pages
 * its outputs, lit as much as they are high; the steps of the lane in STEP mode, and on the steps' and the effects'
 * pages theirs */
static int foot_lane(void)               /* the lane whose steps the row shows, -1 none */
{
    int pt = ptype(ui.page);
    if (ui.grid)
        return ui.lane;
    if (pt == PT_STEP || pt == PT_RND)
        return ptrack(ui.page);
    if (ui.page < ENG_NPAGES && eng_page_lane(ui.page) >= ENG_TRACKS)
        return eng_page_lane(ui.page);
    return -1;
}

static int foot_mode(void)
{
    if (ui.matrix || ui.page == VP_SYS)
        return 0;
    if (ui.erec >= 0 || (ptype(ui.page) == PT_ARP && !ui.grid))
        return 6;
    if (foot_lane() >= 0)
        return 5;
    switch (ptype(ui.page)) {
    case PT_TIDES1: case PT_TIDES2: return ui.page >= PG_TIDES(1, 0) ? 2 : 1;
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
    uint16_t ac = lane_accent(t);
    if (t >= ENG_TRACKS && !eng_lane_has_trigs(t) && ui.held_step < 0) {
        /* an effect without a trig: held open, always on. The row in its colour, HOLD knocked out of it */
        cv_rect(9, 8, 15 * 13 + 11, 18, ac);
        cv_text(9 + (15 * 13 + 11 - text_w(&FONT_B, "HOLD")) / 2, 9, &FONT_B, "HOLD", BG);
        for (int i = 0; i < 16; i += 4)
            cv_rect(9 + i * 13, 28, 11, 1, DIM);   /* the beats */
        cv_rect(231, 8, 3, 18, C_WHITE);
        return;
    }
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
    if (t >= ENG_TRACKS) {               /* an effect: how open its lane holds it */
        int o = (int)(eng_lane_open(t) * 18.0f + 0.5f);
        cv_rect(231, 8, 3, 18, TRACK);
        cv_rect(231, 26 - o, 3, o, C_WHITE);
    }
}

static void foot_endless(int t)          /* Endless's notes, 16 at a time: a bar as high as the note, a rest a dot */
{
    project_t *p = eng_project();
    int len = p->endless_len[t], pos = eng_endless_pos(t), first, lo = 127, hi = 0;
    uint16_t ac = ui.erec == t ? PINK : track_accent(t);
    if (!len) {
        cv_text(10, 11, &FONT_XS, ui.erec == t ? "PLAY THE NOTES, ONE BY ONE" : "NO NOTES YET", DIM);
        return;
    }
    first = ui.erec == t ? (len > 16 ? len - 16 : 0) : pos >= 0 ? pos / 16 * 16 : 0;
    for (int i = 0; i < len; i++)
        if (p->endless[t][i] != ENDLESS_REST) {
            lo = p->endless[t][i] < lo ? p->endless[t][i] : lo;
            hi = p->endless[t][i] > hi ? p->endless[t][i] : hi;
        }
    for (int i = 0; i < 16 && first + i < len; i++) {
        int x = 9 + i * 13, nt = p->endless[t][first + i], on = first + i == pos;
        if (nt == ENDLESS_REST) {
            disc((float)x + 5.0f, 24.0f, 1.6f, on ? C_WHITE : DIM);
            continue;
        }
        int h = 5 + (hi > lo ? (nt - lo) * 15 / (hi - lo) : 8);
        cv_rect(x, 27 - h, 11, h, on ? C_WHITE : ac);
    }
}

/* the presses held for an action: how long (an action that loses something, a second), what it will do (the
 * footer counts it down; "": nothing to show) */
static int erase_lane(void)              /* the lane REC held erases: STEP mode's, or the steps' page's */
{
    return ui.grid ? ui.lane : ui.page < ENG_NPAGES ? eng_page_lane(ui.page) : ptrack(ui.page);
}

static uint32_t hold_ms(int b, char *label, uint32_t max)
{
    static const char *const LOAD[3] = {"LOAD NEW", "LOAD DEMO", "LOAD SAVED"};
    static const char *const FX[3] = {"ERASE DELAY STEPS", "ERASE REVERB STEPS", "ERASE WARPS STEPS"};
    char l[24];
    uint32_t ms = LONG_MS;
    int pt = ptype(ui.page);
    l[0] = 0;
    if (b == B_SEL && ui.held_step < 0 && !ui.matrix) {
        text_cat(l, "MATRIX", sizeof l);
    } else if (b == B_SAVE) {
        text_cat(l, eng_playing() ? "SAVE: STOP FIRST" : "SAVE PROJECT", sizeof l);
        ms = 1000u;
    } else if (b == B_REC && ui.page == VP_SYS) {
        text_cat(l, LOAD[ui.init_sel % 3], sizeof l);
        ms = 1000u;
    } else if (b == B_REC && pt == PT_ARP && ui.erec < 0 && !ui.grid) {
        text_cat(l, "ERASE ENDLESS", sizeof l);
        ms = 1000u;
    } else if (b == B_REC && ui.erec < 0 && (ui.grid || pt == PT_STEP || pt == PT_RND || pt == PT_LANE)) {
        int e = erase_lane();             /* the lane erased: its steps, locks, randoms (a track: its cables); GLO
                                           * held: all of them */
        if (held(B_GLO)) {
            text_cat(l, "ERASE ALL", sizeof l);
        } else if (e >= ENG_TRACKS) {
            text_cat(l, FX[e - ENG_TRACKS], sizeof l);
        } else {
            char n[2] = {(char)('1' + e), 0};
            text_cat(l, "ERASE TRACK ", sizeof l);
            text_cat(l, n, sizeof l);
        }
        ms = 1000u;
    }
    if (label) {
        label[0] = 0;
        text_cat(label, l, max);
    }
    return ms;
}

static int hold_now(char *label, uint32_t max, uint32_t *done)   /* a press held for an action: which button, its
                                                                   * label, how far (0..1000), -1 none */
{
    static const uint8_t B[3] = {B_REC, B_SAVE, B_SEL};
    uint32_t now = plat_ms();
    for (int i = 0; i < 3; i++) {
        int b = B[i];
        if (!held(b) || ui.long_done[b] || ui.used[b])
            continue;
        uint32_t ms = hold_ms(b, label, max), t = now - ui.down_at[b];
        if (!label[0] || t < 120u)       /* a tap shows nothing */
            continue;
        *done = t >= ms ? 1000u : t * 1000u / ms;
        return b;
    }
    return -1;
}

/* ALL keeps what it changed. For a few seconds after GLO is let go, GLO again puts the knobs, models and randoms back
 * as they were before it, as the next bar starts (the engine does it in time); GLO once more before that bar: it
 * stays. A pattern cleared with GLO held is not part of it */
#define ALL_UNDO_MS 4000u
static struct {
    float val[ENG_NPARAMS];
    uint8_t algo[ENG_NPAGES];
    int8_t rnd[ENG_TRACKS][ENG_NTP];
    int dirty;
    uint32_t until;                      /* the time left to undo it (0: none) */
    int press;                           /* this GLO press undoes (or keeps): no ALL, no page */
    int waiting;                         /* undone at the next bar */
} all;

static void all_down(void)               /* GLO pressed */
{
    project_t *p = eng_project();
    all.press = 0;
    if (all.until && (int32_t)(all.until - plat_ms()) > 0) {   /* in time: back as the next bar starts */
        eng_back_at_bar(all.val, all.algo, &all.rnd[0][0]);
        all.until = 0;
        all.waiting = 1;
        all.press = 1;
    } else if (all.waiting && eng_back_pending()) {             /* again before that bar: it stays */
        eng_back_cancel();
        all.waiting = 0;
        all.press = 1;
        app_say("ALL", "STAYS");
    } else {                             /* a new ALL, maybe: what it would change */
        memcpy(all.val, p->val, sizeof all.val);
        memcpy(all.algo, p->algo, sizeof all.algo);
        memcpy(all.rnd, p->rnd, sizeof all.rnd);
        all.dirty = dirty;
        all.until = 0;
    }
    if (all.press)
        ui.used[B_GLO] = 1;
}

static void all_up(void)                 /* GLO let go after an ALL: the time to undo it starts */
{
    project_t *p = eng_project();
    if (all.press) {
        all.press = 0;
        return;
    }
    if (memcmp(all.val, p->val, sizeof all.val) || memcmp(all.algo, p->algo, sizeof all.algo) ||
        memcmp(all.rnd, p->rnd, sizeof all.rnd))
        all.until = plat_ms() + ALL_UNDO_MS;
}

static void all_poll(void)               /* (each frame) the undo done at its bar; the time to undo it run out */
{
    if (all.waiting && !eng_back_pending()) {
        all.waiting = 0;
        dirty = all.dirty;
        app_say("ALL", "UNDONE");
        ui.full = 1;
    }
    if (all.until && (int32_t)(all.until - plat_ms()) <= 0)
        all.until = 0;
}

static int all_state(uint32_t *left)     /* 1: the time to undo it, left (ms); 2: waiting for the bar; 0 */
{
    if (all.waiting)
        return 2;
    if (all.until) {
        int32_t l = (int32_t)(all.until - plat_ms());
        if (l > 0) {
            *left = (uint32_t)l;
            return 1;
        }
    }
    return 0;
}

/* a countdown: five squares, the last one shrinking away each fifth of the time (left: 0..5000) */
static void squares(int32_t left, uint16_t c)
{
    for (int i = 0; i < 5; i++) {
        int32_t f = left - i * 1000, w;
        cv_rect(162 + i * 14, 11, 10, 10, f >= 1000 ? c : TRACK);
        if (f > 0 && f < 1000 && (w = (f * 10 + 500) / 1000) > 0)
            cv_rect(162 + i * 14 + (10 - w) / 2, 11 + (10 - w) / 2, w, w, c);
    }
}

static void draw_footer(void)
{
    static const char *const OUT[2][4] = {{"A1", "A2", "A3", "A4"}, {"B1", "B2", "B3", "B4"}};
    project_t *p = eng_project();
    char t[40], n[8];
    uint32_t now = plat_ms();
    int msg = now < ui.msg_until, mode = foot_mode(), lv[4], lh = 0;
    uint32_t left = 0, hdone = 0;
    int und = all_state(&left);
    char hl[32];
    int hb = hold_now(hl, sizeof hl, &hdone);
    uint16_t ac = page_accent(ui.page);
    for (int i = 0; i < 4; i++) {        /* each LED's level, 0..8 */
        int src = mode == 1 ? SRC_A1 + i : SRC_B1 + i, v = mode && mode < 3 ? eng_hist(src, ENG_HIST - 1) : 0;
        lv[i] = mode && mode < 3 ? (iabs(v) * 8 + 63) / 127 : track_lit(i) * 8;
        lh = lh * 9 + lv[i];
    }
    if (mode == 6) {                     /* Endless: its notes, the one playing */
        lh = eng_endless_pos(ui.track) + 1 + (ui.erec >= 0) * 100 + p->endless_len[ui.track] * 1000;
        for (int i = 0; i < p->endless_len[ui.track]; i++)
            lh = lh * 31 + p->endless[ui.track][i];
    }
    int fl = foot_lane();
    if (mode == 5) {                     /* the steps shown: their trigs, locks, the one playing, the one held */
        lh = eng_step_now(fl) | ui.held_step << 7 | ui.spage << 14 | fl << 17;
        for (int i = 0; i < 16; i++)
            lh = lh * 3 + eng_trig(fl, ui.spage * 16 + i) + (eng_step_locked(fl, ui.spage * 16 + i) ? 2 : 0);
        lh = lh * 67 + (int)p->val[PG_STEP(fl) * 4 + 3];
        if (fl >= ENG_TRACKS)
            lh = lh * 19 + (int)(eng_lane_open(fl) * 18.0f + 0.5f);
    }
    int32_t hsq = 5000 - (int32_t)hdone * 5, usq = (int32_t)(left * 5000u / ALL_UNDO_MS);
    int32_t s[6] = {lh, mode | ui.mod << 4 | msg << 5 | und << 6 | (hb + 1) << 8 |
                        ((hb >= 0 ? hsq : und == 1 ? usq : 0) / 50) << 12,
                    (int32_t)(track_accent(0) ^ track_accent(1) << 1 ^
                    track_accent(2) << 2 ^ track_accent(3) << 3) ^ ui.track << 20 ^ (int32_t)ac << 4,
                    eng_note(0) | eng_note(1) << 8 | eng_note(2) << 16,
                    p->algo[PG_TRACK(0, 0)] | p->algo[PG_TRACK(1, 0)] << 5 | p->algo[PG_TRACK(2, 0)] << 10 |
                    p->algo[PG_TRACK(3, 0)] << 15, msg ? text_hash(ui.msg[0]) : 0};
    if (!ui.full && !memcmp(s, ui.foot_shown, sizeof s))
        return;
    memcpy(ui.foot_shown, s, sizeof s);
    cv_begin(240, FOOT_H, BG);
    cv_rect(8, 2, 224, 1, RULE);
    if (hb >= 0) {                       /* a press held: what it will do, the time left in five squares */
        cv_text(10, 8, &FONT_B, hl, C_WHITE);
        squares(hsq, hb == B_SEL ? TEAL : PINK);
    } else if (msg) {
        t[0] = 0;
        text_cat(t, ui.msg[0], sizeof t);
        text_cat(t, "  ", sizeof t);
        text_cat(t, ui.msg[1], sizeof t);
        cv_text(120 - text_w(&FONT_B, t) / 2, 11, &FONT_B, t, C_WHITE);
    } else if (und) {                    /* ALL: the time to undo it, or waiting for the bar */
        if (und == 1) {
            cv_text(10, 8, &FONT_B, "GLO: UNDO ALL", YELLOW);
            squares(usq, YELLOW);
        } else {
            cv_text(120 - text_w(&FONT_B, "UNDONE AT THE BAR") / 2, 8, &FONT_B, "UNDONE AT THE BAR", YELLOW);
        }
    } else if (ui.mod) {
        cv_text(10, 6, &FONT_XS, "MOD: HOLD A SOURCE KEY, TURN A KNOB", C_WHITE);
        cv_text(10, 21, &FONT_T, "KEYS 1-4 TIDES A  5-8 B  9, 10: A, B ON THE 4 KNOBS", MUTED);
    } else if (mode == 6) {
        foot_endless(ui.erec >= 0 ? ui.erec : ui.track);
    } else if (mode == 5) {
        foot_steps(fl);
    } else {
        for (int i = 0; i < 4; i++) {    /* a cell under each knob (60 px), the LED and the name from its left */
            int x = i * 60 + 8, act = mode ? lv[i] > 0 : i == ui.track;
            t[0] = 0;
            if (mode) {
                text_cat(t, OUT[mode - 1][i], sizeof t);
                led((float)(x + 5), 19.0f, 3.6f, ac, lv[i]);
            } else {
                n[0] = (char)('1' + i);
                n[1] = ' ';
                n[2] = 0;
                text_cat(t, n, sizeof t);
                text_cat(t, SHORT[p->algo[PG_TRACK(i, 0)] % 24], sizeof t);
                if (act) {               /* the track played: knocked out of a block of its colour */
                    uint16_t tc = track_accent(i);
                    cv_rect(x - 4, 7, 52, 25, tc);
                    disc((float)(x + 5), 19.0f, 3.6f, lv[i] ? BG : tint(BG, tc, 3));
                    cv_text(x + 13, 11, &FONT_XS, t, BG);
                    continue;
                }
                led((float)(x + 5), 19.0f, 3.6f, track_accent(i), lv[i]);
            }
            cv_text(x + 13, 11, &FONT_XS, t, act ? C_WHITE : DIM);
        }
    }
    blit(0, FOOT_Y);
}

static void poll_keys(void);             /* (input, below): the keys read between the screen's parts too */

/* the knobs' row of a page (the machine's on GLO's second face), sh px aside: sliding, the page it pushes out too */
static void draw_row1(int page, int force)
{
    if (page == VP_SYS) {
        draw_machine(force);
        return;
    }
    for (int k = 0; k < 4; k++) {
        poll_keys();
        draw_knob_at(page, k, force);
    }
}
static void draw_row(int page, int32_t sh)
{
    if (sh) {
        slide_s = sh - ui.slide_dir * 240;
        draw_row1(ui.slide_from, 1);
        slide_s = sh;
        draw_row1(page, 1);
        slide_s = 0;
    } else {
        draw_row1(page, 0);
    }
}

static void draw(void)
{
    int k = held(B_GLO) && !all.press && (ui.used[B_GLO] || plat_ms() - ui.down_at[B_GLO] >= 250u);
    all_poll();
    if (k != knock) {                    /* ALL: in yellow and black while GLO is held, then as it was */
        knock = k;
        ui.full = 1;
    }
    if (ui.full)
        lcd_fill(0, 0, 240, 240, knock ? YELLOW : BG);
    int32_t sh = 0;
    anim_p = 1.0f;
    if (ui.anim) {                       /* the names written again */
        uint32_t t = plat_ms() - ui.anim_at;
        if (t >= DECODE_MS)
            ui.anim = 0;                 /* the last frame: written */
        else
            anim_p = (float)t / (float)DECODE_MS;
        ui.body_panel = 1;
    }
    if (ui.slide_dir) {                  /* the knobs of another face: quick, then slowing (an ease out) */
        uint32_t t = plat_ms() - ui.slide_at;
        if (t >= SLIDE_MS) {
            ui.slide_dir = 0;            /* the last frame: in place */
        } else {
            float u = 1.0f - (float)t / (float)SLIDE_MS;
            sh = (int32_t)((float)ui.slide_dir * 240.0f * u * u * u);
        }
        ui.body_knobs = 1;
    }
    draw_header();
    poll_keys();
    if (!data_ok || !engine_ok) {
        draw_missing();
    } else if (ui.page == VP_SYS) {
        draw_projects();
        draw_row(VP_SYS, sh);
    } else if (ui.matrix) {
        draw_matrix();
    } else {
        draw_panel();
        draw_row(ui.page, sh);
    }
    poll_keys();
    draw_footer();
    ui.full = 0;
    ui.body_panel = ui.body_knobs = 0;
}

/* ---------------------------------------------------------------- input --- */
static int lane_len(int l) { return (int)(eng_project()->val[PG_STEP(l) * 4 + 3] + 0.5f); }

static void set_lane(int l)              /* the steps shown: their page kept if the lane has it */
{
    ui.lane = l;
    if (ui.spage * 16 >= lane_len(l))
        ui.spage = 0;
}

/* a page: another face of the module shown slides in (dir: from the right +1, the left -1; 0: by the faces' order);
 * another module, at once. The lane follows the page whose knobs a lane's steps lock */
static void go_page_dir(int page, int dir)
{
    int g = ptype(page) == PT_GLOBAL || ptype(page) == PT_SYS, l = page_lane(page);
    if (g && ptype(ui.page) != PT_GLOBAL && ptype(ui.page) != PT_SYS)
        ui.prev_page = ui.page;          /* GLO again goes back there */
    if (!ui.matrix && page != ui.page && page_first(page) == page_first(ui.page) && data_ok && engine_ok) {
        ui.slide_dir = dir ? dir : page_face(page) > page_face(ui.page) ? 1 : -1;
        ui.slide_from = ui.page;
        ui.slide_at = plat_ms();
        ui.head_shown[0] = -1;
        ui.foot_shown[0] = -1;
    } else {
        ui.slide_dir = 0;
        ui.full = 1;
    }
    if (page != ui.page || ui.matrix)
        anim_start();
    if (l >= 0 && l != ui.lane)
        set_lane(l);
    if (page < ENG_NPAGES && eng_page_lane(page) >= ENG_TRACKS)
        ui.fx_page = page;               /* FX goes back to it */
    ui.page = page;
    ui.touched_k = -1;
    ui.matrix = 0;
}
static void go_page(int page) { go_page_dir(page, 0); }

/* GLO, REC held: a new project (four Plaits, nothing patched), the demo patch, or the saved project again. The
 * transport stops; nothing is saved until SAVE is held */
static void project_init(int which)
{
    uint8_t out = eng_project()->out_mode;   /* the output's setting stays (it is the FM-1's, not the music's) */
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
    eng_project()->out_mode = out;
    ui.track = 0;
    set_lane(0);
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
    if (tp < 0 || t >= ENG_TRACKS)
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
    if (tp < 0 || !eng_tp_lockable(t, tp)) {
        app_say("RANDOM:", "A LANE'S KNOBS");
        return;
    }
    for (int i = 0; i < ENG_LANES; i++)
        if (i == t || (held(B_GLO) && t < ENG_TRACKS && i < ENG_TRACKS)) {
            int r = p->rnd[i][tp], n = clamp(r + e * (e > 1 || e < -1 ? 5 : 2), -100, 100);
            if ((r > 0 && n < 0) || (r < 0 && n > 0))
                n = 0;                   /* a stop at 0 on the way through */
            p->rnd[i][tp] = (int8_t)n;
        }
    dirty = 1;
}

static void edit_lock(int param, int32_t e)   /* a held step: its lock on the lane's parameter */
{
    project_t *p = eng_project();
    int t, tp = eng_param_tp(param, &t);
    float v;
    if (tp < 0 || !eng_tp_lockable(t, tp) || t != ui.lane) {
        app_say("LOCK:", "THIS LANE'S KNOBS");
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
    return tp >= 0 && t < ENG_TRACKS ? t : ENG_TRACKS;
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

static int held_source(void)             /* MOD mode: the source key held (1..8), 9 / 10 the fans, 0 none */
{
    for (int k = 0; k < 10; k++)
        if (ui.keys & (1u << k))
            return k + 1;
    return 0;
}

static int key_source(int key)           /* the first eight keys: A1-A4, B1-B4 */
{
    return key >= 1 && key <= 8 ? SRC_A1 + key - 1 : SRC_NONE;
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
    case B_SEQ:                          /* the lane's steps (a track's: then its RANDOM face) */
        if (here && (pt == PT_STEP || pt == PT_RND))
            go_page(next);
        else
            go_page(PG_STEP(ui.lane));
        break;
    case B_ARP:                          /* Endless, the active track's */
        go_page(PG_ARP(ui.track));
        break;
    case B_HOME:
        go_page(here && (pt == PT_MIX || pt == PT_PAN || pt == PT_DUCK || pt == PT_COMP || pt == PT_OTT) ? next : PG_MIX);
        break;
    case B_FX: {                         /* the effect shown last; on an effect: the next one (delay, reverb, Warps) */
        int l = ui.page < ENG_NPAGES ? eng_page_lane(ui.page) : -1;
        if (here && l >= ENG_TRACKS)
            go_page(l == LANE_DELAY ? PG_REVERB : l == LANE_REVERB ? PG_WARPS(0) : PG_DELAY(0));
        else
            go_page(ui.fx_page);
        break;
    }
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
    case B_OCTDN: case B_OCTUP:          /* the keyboard's octave; STEP mode: the steps' page */
        if (ui.grid)
            ui.spage = clamp(ui.spage + (b == B_OCTUP ? 1 : -1), 0, (lane_len(ui.lane) - 1) / 16);
        else
            ui.octave = clamp(ui.octave + (b == B_OCTUP ? 1 : -1), -3, 3);
        break;
    case B_GLO:                          /* ALL, or its undo */
        all_down();
        ui.full = 1;
        break;
    case B_SEL: case B_SAVE: case B_REC: case B_ENV:
        ui.full = 1;                     /* on release (a tap), held long, or held with a knob */
        break;
    default:
        module_button(b);
        break;
    }
}

static void cables_erase(int g)          /* a track's cables (g = ENG_TRACKS: the global ones) */
{
    project_t *p = eng_project();
    for (int c = 0; c < ENG_CABLES; c++) {
        p->cable[g][c].src = SRC_NONE;
        p->cable[g][c].dst = 0;
        p->cable[g][c].depth = 0;
    }
}

static void lane_erase(int l)            /* its pattern, its randoms; a track's cables too */
{
    project_t *p = eng_project();
    eng_steps_clear(l);
    for (int i = 0; i < ENG_NTP; i++)
        p->rnd[l][i] = 0;
    if (l < ENG_TRACKS)
        cables_erase(l);
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
        if (ui.page == VP_SYS) {
            project_init(ui.init_sel);
        } else if (ptype(ui.page) == PT_ARP && ui.erec < 0 && !ui.grid) {   /* Endless: its notes cleared */
            eng_endless_clear(ui.track);
            dirty = 1;
            app_say("ENDLESS", "CLEARED");
        } else if (ui.erec < 0 && (ui.grid || ptype(ui.page) == PT_STEP || ptype(ui.page) == PT_RND ||
                                   ptype(ui.page) == PT_LANE)) {
            /* the steps: the lane erased (its trigs, locks, randoms; a track's cables), with GLO held all of them
             * and the global cables (the effects' knobs stay) */
            static const char *const FX[3] = {"DELAY STEPS", "REVERB STEPS", "WARPS STEPS"};
            char n[16];
            int l = erase_lane();
            if (held(B_GLO)) {
                for (int i = 0; i < ENG_LANES; i++)
                    lane_erase(i);
                cables_erase(ENG_TRACKS);
                ui.used[B_GLO] = 1;
                all.press = 1;                /* (not an ALL to undo) */
                app_say("ALL", "ERASED");
            } else if (l >= 0) {
                lane_erase(l);
                n[0] = 0;
                if (l < ENG_TRACKS) {
                    text_cat(n, "TRACK ", sizeof n);
                    n[6] = (char)('1' + l);
                    n[7] = 0;
                } else {
                    text_cat(n, FX[l - ENG_TRACKS], sizeof n);
                }
                app_say(n, "ERASED");
            }
            ui.held_step = -1;
            dirty = 1;
            ui.full = 1;
        } else if (!ui.grid && ui.erec < 0) {
            app_say("INIT:", "GLO, SELECT, REC HELD");
        }
    }
}

static void button_up(int b)
{
    int pt = ptype(ui.page);
    switch (b) {
    case B_SEL:
        if (ui.long_done[b])
            break;
        if (ui.erec >= 0) {              /* Endless recording: a rest */
            if (eng_endless_add(ui.erec, ENDLESS_REST) != 0)
                app_say("ENDLESS", "64 STEPS, FULL");
            dirty = 1;
        } else if (ui.held_step >= 0) {  /* a step held: its locks cleared */
            eng_lock_clear(ui.lane, ui.held_step);
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
    case B_REC:                          /* KEY or STEP mode; on Endless' page it records, then plays */
        if (ui.long_done[b])
            break;
        if (ui.erec >= 0) {              /* Endless: recorded, it plays */
            char n[8];
            eng_endless_rec(ui.erec, 0);
            int_text(eng_project()->endless_len[ui.erec], n);
            app_say(eng_project()->endless_len[ui.erec] ? "ENDLESS PLAYS" : "ENDLESS", eng_project()->endless_len[ui.erec] ? n : "EMPTY");
            ui.erec = -1;
            ui.full = 1;
            break;
        }
        if (pt == PT_ARP && !ui.grid) {  /* Endless: recording from nothing */
            eng_endless_clear(ui.track);
            eng_endless_rec(ui.track, 1);
            ui.erec = ui.track;
            ui.grid = ui.mod = 0;
            ui.held_step = -1;
            dirty = 1;
            ui.full = 1;
            break;
        }
        ui.grid = !ui.grid;              /* STEP mode: the lane the page's knobs belong to, else the last one */
        if (ui.grid && page_lane(ui.page) >= 0)
            set_lane(page_lane(ui.page));
        ui.mod = 0;
        ui.held_step = -1;
        ui.full = 1;
        break;
    case B_GLO:                          /* a tap: the global page, and back; held with a knob: ALL, undone here */
        if (!ui.used[b])
            go_page(pt == PT_GLOBAL || pt == PT_SYS ? ui.prev_page : PG_GLOBAL);
        else
            all_up();
        ui.full = 1;
        break;
    case B_ENV:                          /* a tap: the track's envelope (PLAITS 2/3); held with a knob: its random */
        if (!ui.used[b])
            go_page(PG_TRACK(ui.track, 1));
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

static void set_track(int t)             /* the active track: the keys' notes stop on the old one */
{
    if (t == ui.track)
        return;
    for (uint32_t k = 0; k < NKEYS; k++)
        if (ui.keys & (1u << k) && !ui.grid && !ui.mod)
            eng_key(ui.track, 53 + (int)k + 12 * ui.octave, 0);
    if (ui.erec >= 0) {                  /* recording Endless: it stops there */
        eng_endless_rec(ui.erec, 0);
        ui.erec = -1;
    }
    ui.track = t;
    ui.held_step = -1;
}

/* STEP mode, a black key: a lane. A track: its sound (or its steps), unless the page is one of the tracks' (the mixer,
 * the sends); an effect: its knobs (or its steps) */
static void select_lane(int l)
{
    int pl = page_lane(ui.page), pt = ptype(ui.page), steps = pt == PT_STEP || pt == PT_RND || pt == PT_LANE;
    ui.held_step = -1;
    if (l < ENG_TRACKS) {
        set_track(l);
        if (steps)
            go_page(PG_STEP(l));
        else if (pl >= ENG_TRACKS)
            go_page(PG_TRACK(l, 0));
        else if (track_page(ui.page, l) != ui.page)
            ui.page = track_page(ui.page, l), ui.full = 1, anim_start();
    } else if (pl != l) {
        go_page(steps ? PG_STEP(l) : l == LANE_DELAY ? PG_DELAY(0) : l == LANE_REVERB ? PG_REVERB : PG_WARPS(0));
    }
    set_lane(l);
    ui.full = 1;
}

/* the keys: the active track's notes; MOD's sources; STEP mode: the white keys are the 16 steps of a page (held: its
 * locks on the knobs), the first seven black keys the lanes */
static void grid_key(int k, int down)
{
    static const uint8_t W[16] = PLAT_WHITE_KEYS, Bk[11] = PLAT_BLACK_KEYS;
    int w = -1, bl = -1, len = lane_len(ui.lane), ln = ui.lane;
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
            ui.step_new = !eng_trig(ln, st);
            if (ui.step_new) {
                eng_trig_set(ln, st, 1);
                dirty = 1;
            }
        } else if (st == ui.held_step) { /* let go: a trig that was there, not locked meanwhile, goes (its locks too) */
            if (!ui.step_new && !ui.step_edit) {
                eng_trig_set(ln, st, 0);
                eng_lock_clear(ln, st);
                dirty = 1;
            }
            ui.held_step = -1;
        }
    } else if (bl >= 0 && bl < ENG_LANES && down) {
        select_lane(bl);
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
        int note = 53 + (int)k + 12 * ui.octave;
        if (ui.grid) {
            grid_key((int)k, down);
        } else if (ui.erec >= 0) {       /* Endless recording: a note a key, heard */
            if (down && eng_endless_add(ui.erec, note) != 0)
                app_say("ENDLESS", "64 STEPS, FULL");
            eng_key(ui.erec, note, down);
            dirty = 1;
        } else if (eng_endless_on(ui.track) && eng_playing()) {   /* Endless playing: a key transposes it */
            if (down) {
                eng_endless_key(ui.track, note);
                dirty = 1;
            }
        } else {
            eng_key(ui.track, note, down);
        }
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
        } else if ((btn & bit) && !ui.long_done[k] && now - ui.down_at[k] >= hold_ms((int)k, 0, 0)) {
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
            set_track(t);
            if (track_page(ui.page, t) != ui.page)
                anim_start();
            ui.page = track_page(ui.page, t);
            if (ui.lane < ENG_TRACKS)
                set_lane(t);
            ui.full = 1;
        }
    }
    e = plat_enc(EN_SELECT);
    if (e && !ui.matrix && page_count(ui.page) > 1)
        go_page_dir(page_at(ui.page, page_face(ui.page) + (e > 0 ? 1 : -1)), e > 0 ? 1 : -1);
    e = plat_enc(EN_ALGO);
    if (e && !ui.matrix && (ptype(ui.page) == PT_WARPS1 || ptype(ui.page) == PT_WARPS2)) {
        /* Warps: ALGORITHM from mark to mark (its six cross-modulations, a fifth of the knob apart) */
        float *v = &eng_project()->val[PG_WARPS(0) * 4];
        int i;
        if (e > 0)
            for (i = 0; i < 5 && (float)i * 0.2f <= *v + 0.001f; i++)
                ;
        else
            for (i = 5; i > 0 && (float)i * 0.2f >= *v - 0.001f; i--)
                ;
        *v = (float)i * 0.2f;
        dirty = 1;
    } else if (e && !ui.matrix && (ptype(ui.page) == PT_DELAY1 || ptype(ui.page) == PT_DELAY2 ||
                                   ptype(ui.page) == PT_DSEND)) {
        edit_param(PG_DELAY(0) * 4, e > 0 ? 1 : -1);   /* the delay: its time, a division at a time */
        ui.touched_at[0] = now;
        ui.touched_k = ui.page == PG_DELAY(0) ? 0 : -1;
    } else if (e && !ui.matrix && (ptype(ui.page) == PT_COMP || ptype(ui.page) == PT_OTT)) {
        int m = clamp(bus_mode() + (e > 0 ? 1 : -1), MB_OFF, MB_OTT);   /* the master bus: off, COMP, OTT (its face) */
        eng_project()->algo[PG_COMP] = (uint8_t)m;
        dirty = 1;
        if (m != MB_OFF && ui.page != (m == MB_OTT ? PG_OTT : PG_COMP))
            go_page_dir(m == MB_OTT ? PG_OTT : PG_COMP, e > 0 ? 1 : -1);
        ui.full = ui.slide_dir ? ui.full : 1;
    } else if (e && !ui.matrix && ui.page < ENG_NPAGES) {
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
        if (ui.page == VP_SYS) {         /* K1 the project REC held loads, K2 the output */
            if (i == 0)
                ui.init_sel = clamp(ui.init_sel + (e > 0 ? 1 : -1), 0, 2);
            if (i == 1) {
                eng_project()->out_mode = (uint8_t)clamp(eng_project()->out_mode + (e > 0 ? 1 : -1), 0, 2);
                dirty = 1;
                ui.full = 1;
            }
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
        } else if (h >= 1 && h <= 8) {
            cable_turn(key_source(h), param, e);
        } else if (h == 9 || h == 10) {  /* the fans: A n (or B n) on knob n */
            cable_turn((h == 9 ? SRC_A1 : SRC_B1) + i, param, e);
        } else if (!held(B_GLO) || !edit_all(param, e)) {
            edit_param(param, e);
        }
        ui.touched_at[i] = now;
        ui.touched_k = i;
    }
    /* lights: the page's button, PLAY, SEL in MOD mode, REC with the steps; the keys: the steps (the one playing
     * blinks), or held, or the sources on this page */
    {
        static const uint8_t PAGE_BTN[PT_SYS + 1] = {
            [PT_PLAITS1] = B_EDIT, [PT_PLAITS2] = B_ENV, [PT_PLAITS3] = B_EDIT, [PT_TIDES1] = B_LFO, [PT_TIDES2] = B_LFO,
            [PT_MIX] = B_HOME, [PT_PAN] = B_HOME, [PT_REVERB] = B_FX, [PT_RSEND] = B_FX, [PT_DELAY1] = B_FX,
            [PT_DELAY2] = B_FX, [PT_DSEND] = B_FX, [PT_STEP] = B_SEQ, [PT_LANE] = B_FX, [PT_GLOBAL] = B_GLO,
            [PT_ARP] = B_ARP, [PT_WARPS1] = B_FX, [PT_WARPS2] = B_FX, [PT_DUCK] = B_HOME, [PT_COMP] = B_HOME,
            [PT_OTT] = B_HOME, [PT_RND] = B_SEQ, [PT_SYS] = B_GLO};
        static const uint8_t W[16] = PLAT_WHITE_KEYS, Bk[11] = PLAT_BLACK_KEYS;
        uint32_t bb = 0, kk = ui.keys;
        int pt = ptype(ui.page);
        if (!ui.matrix)
            bb |= 1u << PAGE_BTN[pt];
        if (ui.mod || ui.matrix)
            bb |= 1u << B_SEL;
        if (ui.grid || ui.page == VP_SYS || (ui.erec >= 0 && (now / 250u) & 1u))   /* recording Endless: it blinks */
            bb |= 1u << B_REC;
        if (eng_playing())
            bb |= LED_PLAY_GREEN;
        if (dirty)
            bb |= 1u << B_SAVE;
        if (ui.grid) {                   /* the lane's steps, its key; OCT- OCT+ lit when there are pages that way */
            int len = lane_len(ui.lane), st = eng_step_now(ui.lane);
            kk = 0;
            for (int i = 0; i < 16; i++)
                if (eng_trig(ui.lane, ui.spage * 16 + i))
                    kk |= 1u << W[i];
            if (st >= ui.spage * 16 && st < ui.spage * 16 + 16)
                kk ^= 1u << W[st - ui.spage * 16];
            kk |= 1u << Bk[ui.lane];
            if (ui.spage > 0)
                bb |= 1u << B_OCTDN;
            if ((ui.spage + 1) * 16 < len)
                bb |= 1u << B_OCTUP;
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
    ui.held_step = ui.erec = -1;
    ui.prev_page = PG_TRACK(0, 0);
    ui.fx_page = PG_DELAY(0);
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
    if (ui.full || ui.slide_dir || ui.anim || now - ui.drawn_at >= 20u) {   /* 50 frames a second at most (a slide: all it can):
                                                                   * what changed */
        ui.drawn_at = now;
        draw();
    }
}
