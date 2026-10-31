/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1: the screen and the panel. The sound is mi/engine.cc's (C++, compiled apart): this file edits its project
 * (eng_project) and shows what it plays.
 *
 * The screen is a module's face, drawn the way Mutable Instruments drew theirs (each module's panel artwork is in
 * pichenettes/eurorack, in its hardware_design/panel): the light grey aluminium, the woven band across the top, black
 * legends in capitals, the pink and teal icons (and Tides 2's orange), white knobs with their "- +" when they go
 * both ways, dotted lines for what is patched, the outputs in their dark grey box. From the top: the module's name,
 * its own display (Plaits' models and LEDs, Tides' modes and outputs, Marbles' t and X, the mixer's meters, Clouds'
 * reverb), its four knobs in a row as KNOB 1 to 4 sit on the FM-1, then the four tracks as four outputs.
 *
 *   KNOB 1..4   the page's four parameters        SELECT   the module's other face (1/3, 2/3...)
 *   ALGORITHM   the page's model or mode          PRESETS  the active track, 1 to 4
 *   EDIT        Plaits, the active track          LFO      Tides A, then B
 *   SEQ         Marbles, rhythm (t)               ARP      Marbles, notes (X, Y)
 *   HOME        the mixer (the levels)            FX       the reverb (the sends, then its own knobs)
 *   GLO         the load of both cores, the tables; there K1 picks a project and REC held loads it (INIT: four
 *               Plaits, nothing patched; the demo patch; the saved project)
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
#define HEAD_H 28                        /* the screen: the band and the header, the module's display, the knobs, */
#define PANEL_Y 28                       /* the tracks */
#define PANEL_H 76
#define KNOB_Y 104
#define KNOB_W 60
#define KNOB_H 100
#define FOOT_Y 204
#define FOOT_H 36

/* Mutable Instruments' colours, as their panels' artwork has them */
#define MI_PANEL RGB(226, 226, 222)      /* the aluminium */
#define MI_INK RGB(35, 31, 32)           /* the legends (#231f20) */
#define MI_MUTED RGB(116, 113, 114)
#define MI_RULE RGB(189, 188, 188)       /* the band, the rules (#bdbcbc) */
#define MI_FAINT RGB(207, 206, 203)      /* the engravings, an LED off */
#define MI_BOX RGB(101, 98, 99)          /* the outputs' box (#656263) */
#define MI_BOX_LO RGB(76, 73, 74)        /* in it: an LED off */
#define MI_BOX_HI RGB(130, 127, 128)     /* in it: a floor, the active track */
#define BOX_TEXT RGB(240, 240, 236)
#define BOX_DIM RGB(178, 176, 176)
#define MI_CAP RGB(249, 249, 247)        /* a knob */
#define MI_CAP_EDGE RGB(156, 154, 153)
#define MI_CAP_TOP RGB(233, 233, 230)
#define MI_SHADOW RGB(196, 196, 191)
#define MI_PINK RGB(219, 40, 93)         /* #db285d */
#define MI_TEAL RGB(3, 163, 172)         /* #03a3ac */
#define MI_ORANGE RGB(230, 160, 18)      /* #e6a012, Tides 2's third colour */
#define LED_GREEN RGB(80, 222, 100)      /* the LEDs themselves */
#define LED_RED RGB(246, 62, 52)
#define LED_ORANGE RGB(255, 172, 30)

/* ------------------------------------------------------------------ state --- */
static int data_ok;                      /* the resource region holds this build's tables */
static int halted;                       /* app_panic: the engine stopped until the next frame */
static int engine_ok;
static int cpu2_ok;
static int save_hold, dirty;
static project_t scratch;                /* a project read or made before it replaces the live one */

static struct {
    int page;                            /* the page shown (engine.h: 23 of them) */
    int matrix, global;                  /* the MATRIX page, the GLO page, over the page */
    int track;                           /* the active track */
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

/* the colours of a module's state, as its icons print them: teal, orange, pink */
static uint16_t accent3(int i) { return i <= 0 ? MI_TEAL : i == 1 ? MI_ORANGE : MI_PINK; }

static uint16_t model_accent(int m)      /* Plaits' banks: teal the classic models, pink the noisy and the drums,
                                          * orange the eight of 1.2 (which have no printed icons) */
{
    return m < 8 ? MI_ORANGE : m < 16 ? MI_TEAL : MI_PINK;
}
static uint16_t track_accent(int t) { return model_accent(eng_project()->algo[PG_TRACK(t, 0)]); }

static uint16_t page_accent(int page)
{
    project_t *p = eng_project();
    switch (eng_page_type(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: return model_accent(p->algo[page_first(page)]);
    case PT_TIDES1: case PT_TIDES2: return accent3(p->algo[page_first(page)]);
    case PT_MARB_T1: case PT_MARB_T2: {
        int m = p->algo[PG_MARBLES(0)];
        return m < 3 ? accent3(m) : MI_TEAL;
    }
    case PT_MARB_X1: case PT_MARB_X2: return accent3((int)(p->val[PG_MARBLES(2) * 4 + 3] + 0.5f));
    case PT_MIX: return MI_TEAL;
    default: return MI_PINK;
    }
}

/* Clouds' labels: the reverb's knobs named in pink and teal pills, the last one plain with its dot (BLEND's) */
static uint16_t knob_accent(int page, int k)
{
    if (page == PG_FX(0))
        return k & 1 ? MI_TEAL : MI_PINK;
    if (page == PG_FX(1))
        return k < 2 ? MI_PINK : MI_TEAL;
    return page_accent(page);
}
static int knob_pill(int page, int k)    /* 0 a plain label, 1 a pill, 2 plain with a dot */
{
    if (page == PG_FX(0))
        return 1;
    if (page == PG_FX(1))
        return k < 3 ? 1 : 2;
    return 0;
}
static int knob_big(int page, int k)     /* the panels' big knobs: Plaits' FREQUENCY and HARMONICS, Tides' FREQUENCY
                                          * and SHAPE, Marbles' RATE, SPREAD and DEJA VU */
{
    switch (eng_page_type(page)) {
    case PT_PLAITS1: case PT_TIDES1: return k < 2;
    case PT_MARB_T1: return k == 0 || k == 3;
    case PT_MARB_X1: return k == 0;
    default: return 0;
    }
}
static int knob_star(int page, int k)    /* Marbles' RATE and SPREAD sit in an eight-pointed star */
{
    int pt = eng_page_type(page);
    return k == 0 && (pt == PT_MARB_T1 || pt == PT_MARB_X1);
}

/* ------------------------------------------------------------------ text --- */
static int iabs(int v) { return v < 0 ? -v : v; }

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
    project_t *p = eng_project();
    if (plat_store_load(OBJ_PROJECT, &scratch, sizeof scratch) == (int)sizeof scratch && eng_check(&scratch))
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
    ui.page = PG_TRACK(0, 0);
    ui.touched_k = -1;
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

static void rrect(int x, int y, int w, int h, int r, uint16_t c)   /* the outputs' box: a rectangle, round corners */
{
    float fr = (float)r;
    cv_rect(x + r, y, w - 2 * r, h, c);
    cv_rect(x, y + r, w, h - 2 * r, c);
    disc((float)x + fr - 0.5f, (float)y + fr - 0.5f, fr, c);
    disc((float)(x + w) - fr - 0.5f, (float)y + fr - 0.5f, fr, c);
    disc((float)x + fr - 0.5f, (float)(y + h) - fr - 0.5f, fr, c);
    disc((float)(x + w) - fr - 0.5f, (float)(y + h) - fr - 0.5f, fr, c);
}

static void dotted(int x0, int y0, int x1, int y1, uint16_t c)   /* the panels' dotted lines: round dots */
{
    int dx = x1 - x0, dy = y1 - y0, l = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
    if (!l) {
        disc((float)x0, (float)y0, 1.1f, c);
        return;
    }
    for (int i = 0; i <= l; i += 4)
        disc((float)(x0 + dx * i / l), (float)(y0 + dy * i / l), 1.1f, c);
}

/* the woven band across the top of the panels: chevrons toward a cross, away from a diamond */
static void band(int y, uint16_t c)
{
    cv_rect(0, y, 240, 1, c);
    cv_rect(0, y + 6, 240, 1, c);
    for (int x0 = -10; x0 < 240; x0 += 56)
        for (int r = 0; r < 5; r++) {
            int o = 2 - iabs(r - 2);
            for (int i = 0; i < 5; i++) {
                cv_rect(x0 + i * 4 + o, y + 1 + r, 2, 1, c);              /* > > > > > */
                cv_rect(x0 + 28 + i * 4 + 2 - o, y + 1 + r, 2, 1, c);     /* < < < < < */
            }
            cv_rect(x0 + 21 + r, y + 1 + r, 2, 1, c);                     /* X */
            cv_rect(x0 + 25 - r, y + 1 + r, 2, 1, c);
            cv_rect(x0 + 51 - o, y + 1 + r, 1, 1, c);                     /* a diamond */
            cv_rect(x0 + 52 + o, y + 1 + r, 1, 1, c);
        }
}

/* the icons, 7 x 7: Plaits' models (the printed sixteen), Tides' ramps, Marbles' t models and X modes, the projects */
enum { IC_VA, IC_SHAPE, IC_FM, IC_GRAIN, IC_ADD, IC_TABLE, IC_CHORD, IC_SPEECH,
       IC_SWARM, IC_NOISE, IC_DUST, IC_STRING, IC_MODAL, IC_KICK, IC_SNARE, IC_HAT,
       IC_AD, IC_LOOP, IC_AR, IC_COIN, IC_CLUSTER, IC_DRUMS, IC_SAME, IC_BUMP, IC_TILT,
       IC_NEW, IC_DEMO, IC_SAVED, NICON };
static const char ICONS[NICON][7][8] = {
    {".......", ".###...", ".#.#...", ".#.#.#.", "...#.#.", "...###.", "......."},   /* virtual analog */
    {".......", ".......", ".##....", "#..#..#", "....##.", ".......", "......."},   /* waveshaping */
    {"####...", "#..#...", "#.####.", "####.#.", "..#..#.", "..####.", "......."},   /* FM */
    {".......", "...#...", "..#.#..", "..#.#..", ".#...#.", "#.....#", "......."},   /* grain */
    {".......", ".#.....", ".#.#...", ".#.#.#.", ".#.#.#.", "#######", "......."},   /* additive */
    {".......", ".##.##.", ".##.##.", ".......", ".##.##.", ".##.##.", "......."},   /* wavetable */
    {".......", "#####..", ".......", ".#####.", ".......", "..#####", "......."},   /* chords */
    {"#######", "#.....#", "#.....#", "#######", ".#.....", "#......", "......."},   /* speech */
    {".......", "...##..", ".##..#.", "#.....#", "#######", ".......", "......."},   /* swarm */
    {".......", "..#...#", "#.#.#.#", "#.###.#", "#######", ".......", "......."},   /* noise */
    {"#...#..", "..#...#", "#...#..", "..#...#", "#...#..", ".......", "......."},   /* particles */
    {"#.#.#.#", "#.#.#.#", "#.#.#.#", "#.#.#.#", "#.#.#.#", ".......", "......."},   /* string */
    {".#####.", "#.....#", "#.###.#", "#.#.#.#", "#.###.#", "#.....#", ".#####."},   /* modal */
    {".......", "..###..", ".#...#.", ".#.#.#.", ".#...#.", "..###..", "......."},   /* kick */
    {".......", ".#####.", "#.....#", "#######", "#.....#", ".#####.", "......."},   /* snare */
    {"...#...", "..###..", "#######", "...#...", "...#...", "..###..", "......."},   /* hi-hat */
    {".......", "...#...", "..#.#..", ".#...#.", "#.....#", ".......", "......."},   /* AD */
    {".......", ".......", "..#...#", ".#.#.#.", "#...#..", ".......", "......."},   /* looping */
    {".......", "..###..", ".#...#.", "#.....#", "#.....#", ".......", "......."},   /* AR */
    {"#.....#", ".#...#.", "..#.#..", "...#...", "...#...", "...#...", "......."},   /* coin toss */
    {".##....", ".##.##.", "....##.", ".##....", ".##....", ".......", "......."},   /* clusters */
    {".#.#.#.", ".......", ".#.#.#.", ".......", ".#.#.#.", ".......", "......."},   /* drums */
    {".......", ".#.#.#.", ".#.#.#.", ".#.#.#.", ".#.#.#.", ".......", "......."},   /* X: identical */
    {".......", "...#...", ".#.#.#.", ".#.#.#.", ".#.#.#.", ".......", "......."},   /* X: bump */
    {".......", ".....#.", "...#.#.", ".#.#.#.", ".#.#.#.", ".......", "......."},   /* X: tilt */
    {"...#...", "...#...", "#######", "...#...", "...#...", ".......", "......."},   /* a new project */
    {"..#....", "..##...", "..###..", "..##...", "..#....", ".......", "......."},   /* the demo */
    {"...#...", "...#...", ".#.#.#.", "..###..", "...#...", "#.....#", "#######"},   /* the saved one */
};

static void icon(float cx, float cy, float r, uint16_t bg, int which)   /* a printed icon: a disc, a white glyph */
{
    int x0 = (int)cx - 3, y0 = (int)cy - 3;
    disc(cx, cy, r, bg);
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 7; x++)
            if (ICONS[which][y][x] == '#')
                cv_rect(x0 + x, y0 + y, 1, 1, C_WHITE);
}

static int pill_h(const felucca_font_t *f) { return f == &FONT_T ? 13 : 15; }
static int text_pill(int cx, int y, const felucca_font_t *f, const char *s, uint16_t bg, uint16_t fg)
{                                        /* a label in a pill, centred on cx; its width */
    int h = pill_h(f), tw = text_w(f, s), w = tw + h - 3, x = cx - w / 2;
    pill(x, y, w, h, bg);
    cv_text(x + (w - tw) / 2, y + (h - f->h) / 2 + 1, f, s, fg);
    return w;
}

static void lens(float cx, float cy, float r, uint16_t lit, int on, uint16_t off)   /* an LED */
{
    disc(cx, cy, r, on ? lit : off);
    if (on)
        disc(cx - r * 0.3f, cy - r * 0.3f, r * 0.35f, tint(C_WHITE, lit, 5));
}

static void jack(float cx, float cy)     /* a socket: its nut, its hole */
{
    disc(cx, cy, 9.0f, MI_RULE);
    disc(cx, cy, 7.8f, RGB(216, 216, 212));
    disc(cx, cy, 4.8f, MI_INK);
}

static void italic_t(float x, float y, uint16_t c)   /* Marbles' t, a serif italic 20 px tall */
{
    seg(x + 8.0f, y + 1.0f, x + 4.0f, y + 16.5f, 2.6f, c);
    seg(x + 1.5f, y + 6.0f, x + 11.5f, y + 6.0f, 1.6f, c);
    seg(x + 4.0f, y + 16.5f, x + 5.6f, y + 19.0f, 2.2f, c);
    seg(x + 5.6f, y + 19.0f, x + 10.5f, y + 15.8f, 1.6f, c);
}
static void italic_X(float x, float y, uint16_t c)   /* and its X */
{
    seg(x + 5.0f, y + 1.0f, x + 13.5f, y + 19.0f, 2.8f, c);
    seg(x + 17.0f, y + 1.0f, x + 2.0f, y + 19.0f, 1.4f, c);
    seg(x + 1.5f, y + 1.0f, x + 8.5f, y + 1.0f, 1.3f, c);
    seg(x + 13.5f, y + 1.0f, x + 19.5f, y + 1.0f, 1.3f, c);
    seg(x - 1.0f, y + 19.0f, x + 5.0f, y + 19.0f, 1.3f, c);
    seg(x + 10.0f, y + 19.0f, x + 17.0f, y + 19.0f, 1.3f, c);
}

static void star8(float cx, float cy, float r, uint16_t c)   /* Marbles' eight-pointed star (two squares) */
{
    float s = r * 0.70710678f;
    BOX_OF(cx, cy, r);
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            float dx = fm_fabsf((float)x - cx), dy = fm_fabsf((float)y - cy);
            float a = s - (dx + dy) * 0.70710678f, b = s - fm_maxf(dx, dy);
            blend_px(x, y, c, fm_maxf(a, b) + 0.5f);
        }
}

static void mandala(float cx, float cy, float r, uint16_t c)   /* Clouds' engraved discs, simplified */
{
    ring(cx, cy, r, 1.2f, c);
    ring(cx, cy, r * 0.82f, 1.0f, c);
    for (int i = 0; i < 12; i++) {
        float a = (float)i * 0.5235988f;
        ring(cx + r * 0.91f * fm_cosf(a), cy + r * 0.91f * fm_sinf(a), r * 0.08f, 1.0f, c);
    }
    ring(cx, cy, r * 0.58f, 1.0f, c);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 0.7853982f + 0.3926991f;
        ring(cx + r * 0.42f * fm_cosf(a), cy + r * 0.42f * fm_sinf(a), r * 0.14f, 1.0f, c);
    }
    ring(cx, cy, r * 0.22f, 1.0f, c);
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
    int tr = eng_page_track(ui.page), x;
    uint16_t ac = page_accent(ui.page);
    if (load < 0 || plat_ms() - load_at >= 250u) {
        load = load_now();
        load_at = plat_ms();
    }
    int32_t s[5] = {ui.page | ui.matrix << 8 | ui.global << 9 | ui.mod << 10, load, eng_playing() | dirty << 1,
                    ui.track | track_accent(ui.track) << 4, ac};
    if (!ui.full && !memcmp(s, ui.head_shown, sizeof s))
        return;
    memcpy(ui.head_shown, s, sizeof s);
    cv_begin(240, HEAD_H, MI_PANEL);
    band(0, ui.mod ? MI_PINK : ui.matrix ? MI_TEAL : MI_RULE);
    t[0] = 0;
    text_cat(t, ui.global ? "Global" : ui.matrix ? "Matrix" : module_name(ui.page), sizeof t);
    x = cv_text(6, 10, &FONT_B, t, MI_INK);
    if (!ui.global && !ui.matrix && page_count(ui.page) > 1) {   /* the faces: an LED each, the one shown lit */
        for (int f = 0; f < page_count(ui.page); f++) {
            float fx = (float)(x + 7 + f * 10);
            if (f == page_face(ui.page))
                disc(fx, 18.0f, 3.2f, ac);
            else
                ring(fx, 18.0f, 3.2f, 1.2f, MI_RULE);
        }
        x += 7 + page_count(ui.page) * 10;
    }
    if (ui.mod)
        text_pill(x + 22, 11, &FONT_T, "MOD", MI_PINK, C_WHITE);
    if (tr >= 0 || ui.matrix) {          /* the track, in its model's colour */
        t[0] = 'T';
        t[1] = (char)('1' + ui.track);
        t[2] = 0;
        text_pill(150, 10, &FONT_XS, t, track_accent(ui.track), C_WHITE);
    }
    disc(177.0f, 17.5f, 7.5f, eng_playing() ? MI_TEAL : MI_RULE);   /* PLAY, as an icon */
    for (int y = -4; y <= 4; y++)
        cv_rect(175, 17 + y, 7 - (iabs(y) * 7 + 2) / 4, 1, C_WHITE);
    t[0] = 0;
    int_text(load, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "%", sizeof t);
    x = 234 - text_w(&FONT_XS, t);
    cv_text(x, 10, &FONT_XS, t, load >= 80 ? MI_PINK : load >= 60 ? MI_ORANGE : MI_MUTED);
    if (dirty)                           /* not saved */
        disc((float)(x - 7), 17.5f, 2.4f, MI_PINK);
    cv_blit(0, 0);
}

/* the module's own display -------------------------------------------------------------------------------- */

static void panel_plaits_bank(int model)  /* the model's name; Plaits' column of LEDs between its two rows of icons,
                                           * laid on its side */
{
    const char *name = eng_algo_name(PG_TRACK(ui.track, 0), model);
    const felucca_font_t *f = text_w(&FONT_M, name) <= 232 ? &FONT_M : &FONT_B;
    uint16_t led = model < 8 ? LED_ORANGE : model < 16 ? LED_GREEN : LED_RED;
    cv_text(120 - text_w(f, name) / 2, f == &FONT_M ? 0 : 4, f, name, MI_INK);
    for (int i = 0; i < 8; i++) {
        float x = (float)(22 + i * 28);
        int on = model % 8 == i;
        icon(x, 35.0f, 6.5f, MI_TEAL, IC_VA + i);
        icon(x, 63.0f, 6.5f, MI_PINK, IC_SWARM + i);
        if (!on)
            ring(x, 49.0f, 3.6f, 1.0f, MI_RULE);
        lens(x, 49.0f, on ? 3.6f : 2.6f, led, on, MI_FAINT);
        if (on && model >= 8)
            ring(x, model < 16 ? 35.0f : 63.0f, 9.3f, 1.4f, MI_INK);
        else if (on)
            ring(x, 49.0f, 6.2f, 1.3f, MI_INK);
    }
}

static void panel_plaits_lpg(int t, uint16_t ac)   /* the low-pass gate's response: DECAY's tail, COLOUR's tone */
{
    int pg = PG_TRACK(t, 1), y0 = 68, h = 48;
    float decay = eng_final(pg * 4 + 0), colour = eng_final(pg * 4 + 1);
    float k = 0.002f + 0.12f * (1.0f - decay) * (1.0f - decay) * (1.0f - decay);   /* the fall a pixel */
    float px = 0.0f, py = (float)y0;
    uint16_t fill = tint(ac, MI_PANEL, 2 + (int)(colour * 4.0f));
    cv_text(6, 1, &FONT_B, "LOW-PASS GATE", MI_INK);
    cv_text(234 - text_w(&FONT_XS, colour < 0.5f ? "VCFA" : "VCA"), 2, &FONT_XS, colour < 0.5f ? "VCFA" : "VCA",
            MI_MUTED);
    cv_rect(10, y0 + 2, 220, 1, MI_RULE);
    for (int x = 0; x < 206; x++) {
        float env = x < 4 ? (float)x / 4.0f : fm_exp2f(-(float)(x - 4) * k * 1.4427f);
        float y = (float)y0 - env * (float)h;
        if (!(x % 3) && y < (float)y0 - 2.0f)
            cv_rect(16 + x, (int)y + 3, 1, y0 - (int)y - 2, fill);
        if (x)
            seg(16.0f + px, py, 16.0f + (float)x, y, 2.0f, MI_INK);
        px = (float)x;
        py = y;
    }
}

static void panel_plaits_route(int t, uint16_t ac)   /* where the track's gate and note come from: TRIG, V/OCT, OUT */
{
    project_t *p = eng_project();
    char n[8];
    int pg = PG_TRACK(t, 2), on = (int)((eng_gates() >> t) & 1u);
    static const char *const L[2] = {"TRIG", "V/OCT"};
    for (int j = 0; j < 2; j++) {
        int cx = 34 + j * 72;
        const char *src = eng_pdef(pg * 4 + j)->names[(int)(p->val[pg * 4 + j] + 0.5f)];
        cv_text(cx - text_w(&FONT_XS, L[j]) / 2, 0, &FONT_XS, L[j], MI_INK);
        jack((float)cx, 28.0f);
        dotted(cx, 40, cx, 52, MI_INK);
        char u[12];
        label_text(src, u, sizeof u);
        text_pill(cx, 56, &FONT_XS, u, ac, C_WHITE);
    }
    rrect(150, 0, 88, 75, 6, MI_BOX);    /* the outputs' box: the note played */
    cv_text(160, 3, &FONT_XS, "OUT", BOX_TEXT);
    lens(226.0f, 11.0f, 4.0f, LED_GREEN, on, MI_BOX_LO);
    note_text(eng_note(t), n);
    cv_text(194 - text_w(&FONT_M, n) / 2, 30, &FONT_M, n, on ? C_WHITE : BOX_DIM);
}

static void trace(int src, int x0, int w, int y0, int h, int bipolar, uint16_t c)
{                                        /* a source's last 1.4 s in w px, its middle (or its foot) at y0 */
    int py = 0;
    for (int j = 0; j < w; j++) {
        int v = eng_hist(src, j * (ENG_HIST - 1) / (w - 1));
        int y = bipolar ? y0 - v * (h / 2) / 127 : y0 - v * h / 127;
        if (j)
            cv_line(x0 + j - 1, py, x0 + j, y, c);
        py = y;
    }
}

static void mode_icons(int x0, int sel, int first, int n)   /* a row of printed icons, the chosen one ringed */
{
    for (int i = 0; i < n; i++) {
        float x = (float)(x0 + i * 21);
        icon(x, 11.0f, 7.8f, i == sel ? accent3(i) : tint(accent3(i), MI_PANEL, 3), first + i);
        if (i == sel)
            ring(x, 11.0f, 10.6f, 1.4f, MI_INK);
    }
}

static void panel_tides(int tide)
{
    project_t *p = eng_project();
    int ramp = p->algo[PG_TIDES(tide, 0)], src0 = tide ? SRC_B1 : SRC_A1;
    int range = (int)(p->val[PG_TIDES(tide, 1) * 4 + 2] + 0.5f);
    static const char *const R[3] = {"/8", "X1", "X8"};
    mode_icons(12, ramp, IC_AD, 3);      /* the ramp: AD, looping, AR */
    cv_text(70, 3, &FONT_B, eng_algo_name(PG_TIDES(tide, 0), ramp), MI_INK);
    for (int i = 0; i < 3; i++) {        /* the range, as Tides 2's three pills */
        float x = (float)(184 + i * 21);
        disc(x, 11.0f, 7.8f, i == range ? accent3(i) : tint(accent3(i), MI_PANEL, 3));
        cv_text((int)x - text_w(&FONT_T, R[i]) / 2, 5, &FONT_T, R[i], C_WHITE);
        if (i == range)
            ring(x, 11.0f, 10.6f, 1.4f, MI_INK);
    }
    rrect(2, 24, 236, 51, 6, MI_BOX);    /* the four outputs, in their box */
    for (int c = 0; c < 4; c++) {
        int x0 = 4 + c * 59, v = eng_hist(src0 + c, ENG_HIST - 1), mid = ramp == 1 ? 55 : 69;
        char n[2] = {(char)('1' + c), 0};
        lens((float)(x0 + 10), 32.0f, 3.4f, v > 8 ? LED_GREEN : LED_RED, v > 8 || v < -8, MI_BOX_LO);
        cv_text(x0 + 18, 25, &FONT_XS, n, BOX_TEXT);
        cv_rect(x0 + 4, mid, 50, 1, MI_BOX_HI);
        trace(src0 + c, x0 + 4, 50, mid, ramp == 1 ? 26 : 28, ramp == 1, BOX_TEXT);
    }
}

static void panel_marbles_t(void)
{
    project_t *p = eng_project();
    int m = p->algo[PG_MARBLES(0)];
    italic_t(5.0f, 1.0f, MI_INK);
    mode_icons(34, m, IC_COIN, 3);       /* coin toss, clusters, drums (the others: their name alone) */
    cv_text(92, 3, &FONT_B, eng_algo_name(PG_MARBLES(0), m), MI_INK);
    rrect(2, 24, 236, 51, 6, MI_BOX);
    for (int c = 0; c < 3; c++) {        /* t1 t2 t3, their LEDs and the gates of the last 1.4 s */
        int y = 27 + c * 16, on = eng_hist(SRC_T1 + c, ENG_HIST - 1) > 64;
        char n[3] = {'t', (char)('1' + c), 0};
        lens(11.0f, (float)(y + 7), 3.4f, LED_GREEN, on, MI_BOX_LO);
        cv_text(19, y, &FONT_XS, n, BOX_TEXT);
        for (int i = 0; i < ENG_HIST; i++) {
            int x = 38 + i * 194 / ENG_HIST;
            if (eng_hist(SRC_T1 + c, i) > 64)
                cv_rect(x, y + 3, 2, 9, BOX_TEXT);
            else if (!(i & 7))
                cv_rect(x, y + 7, 1, 2, MI_BOX_HI);
        }
    }
}

static void panel_marbles_x(void)
{
    project_t *p = eng_project();
    int xm = (int)(p->val[PG_MARBLES(2) * 4 + 3] + 0.5f);
    static const char *const N[4] = {"X1", "X2", "X3", "Y"};
    mode_icons(12, xm, IC_SAME, 3);      /* identical, bump, tilt */
    cv_text(70, 3, &FONT_B, eng_algo_name(PG_MARBLES(2), p->algo[PG_MARBLES(2)]), MI_INK);
    italic_X(214.0f, 1.0f, MI_INK);
    rrect(2, 24, 236, 51, 6, MI_BOX);
    for (int c = 0; c < 4; c++) {        /* the voltages, stepped as they are drawn */
        int y = 26 + c * 12, py = 0;
        uint16_t col = c == 3 ? tint(MI_PINK, C_WHITE, 4) : BOX_TEXT;
        cv_text(9, y - 1, &FONT_T, N[c], BOX_DIM);
        for (int i = 0; i < ENG_HIST; i++) {
            int v = eng_hist(SRC_X1 + c, i), x = 30 + i * 200 / ENG_HIST, yy = y + 10 - v * 9 / 127;
            if (i && yy != py)
                cv_rect(x, py < yy ? py : yy, 1, iabs(yy - py) + 1, MI_BOX_HI);
            cv_rect(x, yy, 2, 1, col);
            py = yy;
        }
    }
}

static void panel_mixer(void)
{
    rrect(2, 0, 236, 75, 6, MI_BOX);
    for (int t = 0; t < ENG_TRACKS; t++) {   /* a column of LEDs over each level knob */
        int cx = t * KNOB_W + 30, lit = (int)(fm_clampf(eng_meter(t) * 1.25f, 0.0f, 1.0f) * 7.0f + 0.5f);
        char n[2] = {(char)('1' + t), 0};
        cv_text(cx - 12, 1, &FONT_XS, n, BOX_TEXT);
        disc((float)(cx + 6), 9.0f, 2.4f, track_accent(t));
        for (int k = 0; k < 7; k++)
            lens((float)cx, (float)(67 - k * 8), 3.0f, k == 6 ? LED_RED : k >= 4 ? LED_ORANGE : LED_GREEN, k < lit,
                 MI_BOX_LO);
    }
}

static void panel_reverb(void)          /* Clouds' disc, and the tail its reverb would leave: TIME long, DAMPING dark */
{
    float time = eng_final(PG_FX(1) * 4 + 0), damp = eng_final(PG_FX(1) * 4 + 1);
    float ret = eng_final(PG_FX(1) * 4 + 3), k = 0.06f - 0.054f * time;
    mandala(37.0f, 38.0f, 33.0f, MI_FAINT);
    cv_text(80, 0, &FONT_XS, "from Clouds, sends post-fader", MI_MUTED);
    for (int i = 0; i < 36; i++) {
        float a = fm_exp2f(-(float)i * k * 4.0f * 1.4427f) * (0.4f + 0.6f * ret);
        int h = (int)(a * 50.0f);
        uint16_t c = i & 1 ? MI_TEAL : MI_PINK;
        if ((float)i / 36.0f > 1.0f - damp * 0.8f)
            c = tint(c, MI_PANEL, 3);
        cv_rect(82 + i * 4, 72 - h, 2, h, c);
    }
}

static void reverb_meter(int force)      /* the return's meter, alone: the disc is not drawn again for it */
{
    static int shown = -1;
    int m = (int)(fm_clampf(eng_meter(ENG_TRACKS) * 2.0f, 0.0f, 1.0f) * 54.0f);
    if (!force && m == shown)
        return;
    shown = m;
    cv_begin(5, 54, MI_FAINT);
    cv_rect(0, 54 - m, 5, m, MI_PINK);
    cv_blit(230, PANEL_Y + 18);
}

static void draw_panel(void)
{
    project_t *p = eng_project();
    int pt = eng_page_type(ui.page), live = 0, tr = eng_page_track(ui.page);
    uint32_t now = plat_ms();
    uint16_t ac = page_accent(ui.page);
    int32_t s[3] = {ui.page, eng_algo_count(page_first(ui.page)) ? p->algo[page_first(ui.page)] : -1, 0};
    switch (pt) {
    case PT_PLAITS2: s[2] = (int)(eng_final(ui.page * 4) * 200.0f) | (int)(eng_final(ui.page * 4 + 1) * 8.0f) << 8; break;
    case PT_PLAITS3: s[2] = eng_note(tr) | (int)((eng_gates() >> tr) & 1u) << 8 | (int)p->val[ui.page * 4] << 9 |
                            (int)p->val[ui.page * 4 + 1] << 14; break;
    case PT_TIDES1: case PT_TIDES2:
        s[2] = (int)p->val[PG_TIDES(ui.page >= PG_TIDES(1, 0), 1) * 4 + 2];
        live = 1;
        break;
    case PT_MARB_X1: case PT_MARB_X2: s[2] = (int)(p->val[PG_MARBLES(2) * 4 + 3] + 0.5f); live = 1; break;
    case PT_MARB_T1: case PT_MARB_T2: live = 1; break;
    case PT_MIX:                         /* the meters: drawn again when a LED changes */
        for (int t = 0; t < ENG_TRACKS; t++)
            s[2] |= (int)(fm_clampf(eng_meter(t) * 1.25f, 0.0f, 1.0f) * 7.0f + 0.5f) << (t * 3);
        s[2] |= (int)(p->algo[PG_TRACK(0, 0)] / 8) << 12 | (int)(p->algo[PG_TRACK(1, 0)] / 8) << 14 |
                (int)(p->algo[PG_TRACK(2, 0)] / 8) << 16 | (int)(p->algo[PG_TRACK(3, 0)] / 8) << 18;
        break;
    case PT_FX1: case PT_FX2:            /* the tail's knobs (the meter: reverb_meter) */
        for (int i = 0; i < 4; i++)
            s[2] = s[2] * 41 + (int)(eng_final(PG_FX(1) * 4 + i) * 40.0f);
        break;
    default: break;
    }
    if (!ui.full && !memcmp(s, ui.panel_shown, sizeof s) && (!live || now - ui.panel_at < 80u)) {
        if (pt == PT_FX1 || pt == PT_FX2)
            reverb_meter(0);
        return;
    }
    memcpy(ui.panel_shown, s, sizeof s);
    ui.panel_at = now;
    cv_begin(240, PANEL_H, MI_PANEL);
    switch (pt) {
    case PT_PLAITS1: panel_plaits_bank(p->algo[page_first(ui.page)]); break;
    case PT_PLAITS2: panel_plaits_lpg(tr, ac); break;
    case PT_PLAITS3: panel_plaits_route(tr, ac); break;
    case PT_TIDES1: case PT_TIDES2: panel_tides(ui.page >= PG_TIDES(1, 0)); break;
    case PT_MARB_T1: case PT_MARB_T2: panel_marbles_t(); break;
    case PT_MARB_X1: case PT_MARB_X2: panel_marbles_x(); break;
    case PT_MIX: panel_mixer(); break;
    default: panel_reverb(); break;
    }
    cv_blit(0, PANEL_Y);
    if (pt == PT_FX1 || pt == PT_FX2)
        reverb_meter(1);
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

#define DIAL_X 30.0f
#define DIAL_Y 43.0f
static void draw_dial(float u, float uf, int bip, int has_mod, uint16_t ac, int big, int star)
{
    float rc = big ? 13.5f : 11.0f, r0 = rc + 3.6f, r1 = rc + 6.2f, x, y;
    u = fm_clampf(u, 0.0f, 1.0f);
    uf = fm_clampf(uf, 0.0f, 1.0f);
    if (star) {
        star8(DIAL_X, DIAL_Y, 25.0f, MI_FAINT);
        star8(DIAL_X, DIAL_Y, 22.6f, MI_PANEL);
        star8(DIAL_X, DIAL_Y, 20.5f, tint(MI_FAINT, MI_PANEL, 4));
    }
    for (int i = 0; i <= 10; i++) {      /* the scale: eleven dots */
        travel_xy(DIAL_X, DIAL_Y, (float)i * 0.1f, (r0 + r1) * 0.5f, &x, &y);
        disc(x, y, 0.9f, MI_RULE);
    }
    arc(DIAL_X, DIAL_Y, r0, r1, bip ? fm_minf(u, 0.5f) : 0.0f, bip ? fm_maxf(u, 0.5f) : u, ac);
    if (bip) {                           /* an attenuverter's marks: - +, and its middle */
        float my = DIAL_Y - r1 + 1.0f;
        seg(DIAL_X - r1 - 3.5f, my, DIAL_X - r1 + 1.0f, my, 1.4f, MI_INK);
        seg(DIAL_X + r1 - 1.0f, my, DIAL_X + r1 + 3.5f, my, 1.4f, MI_INK);
        seg(DIAL_X + r1 + 1.25f, my - 2.25f, DIAL_X + r1 + 1.25f, my + 2.25f, 1.4f, MI_INK);
        seg(DIAL_X, DIAL_Y - r1 - 4.5f, DIAL_X, DIAL_Y - r1 - 0.5f, 2.2f, MI_INK);
    }
    disc(DIAL_X + 0.8f, DIAL_Y + 1.8f, rc + 0.4f, MI_SHADOW);   /* the knob: its shadow, its skirt, its cap, its line */
    disc(DIAL_X, DIAL_Y, rc, MI_CAP_EDGE);
    disc(DIAL_X, DIAL_Y, rc - 1.2f, MI_CAP);
    ring(DIAL_X, DIAL_Y, rc - 3.0f, 1.0f, MI_CAP_TOP);
    {
        float x0, y0;
        travel_xy(DIAL_X, DIAL_Y, u, rc * 0.25f, &x0, &y0);
        travel_xy(DIAL_X, DIAL_Y, u, rc - 2.0f, &x, &y);
        seg(x0, y0, x, y, 2.2f, MI_INK);
    }
    if (has_mod) {                       /* where the cables take it */
        travel_xy(DIAL_X, DIAL_Y, uf, r1 + 3.2f, &x, &y);
        disc(x, y, 2.6f, MI_PANEL);
        disc(x, y, 2.0f, ac);
    }
}

static void draw_knob(int k)
{
    project_t *p = eng_project();
    int param = ui.page * 4 + k;
    const eng_pdef_t *d = eng_pdef(param);
    float v = p->val[param], f = eng_final(param);
    char t[32], c[32], lab[24];
    int hash = 0, hot = ui.touched_k == k && plat_ms() - ui.touched_at[k] < 1200u;
    int ncab = cables_on(param, c, sizeof c, &hash), lp = knob_pill(ui.page, k), big = knob_big(ui.page, k);
    uint16_t ac = knob_accent(ui.page, k);
    eng_param_text(param, ncab ? f : v, t, sizeof t);
    int32_t s[4] = {(int32_t)(knob_u(param, v) * 270.0f) | (int32_t)(knob_u(param, f) * 270.0f) << 10,
                    text_hash(t), hash, hot | ui.mod << 1 | (int32_t)ac << 2 | big << 18 | lp << 19};
    if (!ui.full && !memcmp(s, ui.knob_shown[k], sizeof s))
        return;
    memcpy(ui.knob_shown[k], s, sizeof s);
    cv_begin(KNOB_W, KNOB_H, MI_PANEL);
    label_text(d->name, lab, sizeof lab);  /* the name as the panel prints it (touched: in black) */
    {
        const felucca_font_t *lf = text_w(&FONT_XS, lab) <= (hot || lp == 1 ? 46 : 58) ? &FONT_XS : &FONT_T;
        if (hot || lp == 1) {
            text_pill(30, 1, lf, lab, hot ? MI_INK : ac, C_WHITE);
        } else {
            int w = text_w(lf, lab), x = 30 - w / 2 - (lp == 2 ? 4 : 0);
            cv_text(x, lf == &FONT_XS ? 1 : 3, lf, lab, MI_INK);
            if (lp == 2)
                disc((float)(x + w + 5), 9.0f, 2.6f, ac);
        }
    }
    if (d->kind == K_ENUM) {             /* a choice: its position in a pill, the choices as LEDs */
        int n = d->hi - d->lo + 1, i = (int)(v + 0.5f) - d->lo;
        float sp = n <= 5 ? 11.0f : 4.8f, r = n <= 5 ? 3.0f : 1.7f;
        char u[24];
        label_text(t, u, sizeof u);
        text_pill(30, 30, text_w(&FONT_XS, u) <= 44 ? &FONT_XS : &FONT_T, u, ac, C_WHITE);
        for (int j = 0; j < n; j++) {
            float x = 30.0f - (float)(n - 1) * sp * 0.5f + (float)j * sp;
            if (j == i)
                disc(x, 56.0f, r + 0.6f, MI_INK);
            else
                ring(x, 56.0f, r, 1.0f, MI_RULE);
        }
    } else {
        draw_dial(knob_u(param, v), knob_u(param, f), d->kind == K_BIP || (d->kind == K_INT && d->lo < 0), ncab, ac,
                  big, knob_star(ui.page, k));
        cv_text(30 - text_w(&FONT_XS, t) / 2, 66, &FONT_XS, t, ncab ? ac : MI_INK);
    }
    if (ncab) {                          /* the cables: their sources in a pill, a dotted line up to the knob */
        int w = text_pill(30, 84, &FONT_T, c, MI_BOX, C_WHITE);
        int xl = 30 - w / 2 - 2, xr = (int)(DIAL_X - (big ? 13.5f : 11.0f) - 8.0f);
        if (d->kind != K_ENUM) {
            dotted(xl, 90, 3, 90, MI_INK);
            dotted(3, 86, 3, (int)DIAL_Y, MI_INK);
            dotted(7, (int)DIAL_Y, xr, (int)DIAL_Y, MI_INK);
        }
    } else if (ui.mod && eng_param_mod(param)) {   /* MOD mode: a socket free for a cable */
        ring(30.0f, 90.0f, 5.5f, 1.5f, MI_MUTED);
        disc(30.0f, 90.0f, 2.0f, MI_MUTED);
    }
    cv_blit((uint32_t)(k * KNOB_W), KNOB_Y);
}

/* lists: the matrix, the global page, the missing tables --------------------------------------------------- */
static void draw_lines(int y, int h, const char *const *lines, const uint16_t *colours, int n, int sel)
{
    for (int i = 0; i < n; i++) {
        cv_begin(240, (uint32_t)h, MI_PANEL);
        if (i == sel)
            pill(2, 1, 236, h - 2, MI_BOX);
        cv_text(8, (h - 15) / 2, &FONT_XS, lines[i], i == sel ? C_WHITE : colours[i]);
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
            col[i] = MI_MUTED;
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
            col[i] = MI_INK;
        }
        lines[i] = rows[i];
    }
    h = h * 17 + ui.msel + g * 1000;
    if (!ui.full && h == shown)
        return;
    shown = h;
    cv_begin(240, 24, MI_PANEL);
    cv_text(8, 0, &FONT_XS, g < ENG_TRACKS ? "The track's cables" : "Global cables (Tides, Marbles, reverb)", MI_INK);
    cv_text(8, 12, &FONT_T, "K1 CABLE   K2 SOURCE   K3 TARGET   K4 DEPTH", MI_MUTED);
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
        cv_begin(240, PANEL_H, MI_PANEL);
        cv_text(8, 1, &FONT_B, "PROJECT", MI_INK);
        cv_text(232 - text_w(&FONT_T, "K1 CHOOSE, REC HELD LOADS"), 4, &FONT_T, "K1 CHOOSE, REC HELD LOADS",
                MI_MUTED);
        for (int i = 0; i < 3; i++) {
            int y = 20 + i * 18, sel = i == ui.init_sel;
            if (sel)
                pill(2, y, 236, 17, MI_BOX);
            icon(14.0f, (float)y + 8.0f, 6.5f, accent3(i), IC_NEW + i);
            cv_text(28, y + 1, &FONT_XS, P[i], sel ? C_WHITE : MI_INK);
        }
        cv_blit(0, PANEL_Y);
    }
    if (!ui.full && plat_ms() - at < 400u)
        return;
    at = plat_ms();
    for (int i = 0; i < 6; i++) {        /* the machine, where the knobs are: two canvases of three rows (a 240 x 100
                                          * one is more than gfx.c's canvas holds) */
        const char *l = "";
        uint16_t c = MI_INK;
        int y = i < 3 ? 4 + i * 15 : (i - 3) * 15;   /* a row every 15 px: rows 4..48, then 49..99 */
        if (i % 3 == 0) {
            cv_begin(240, i ? 51 : 49, MI_PANEL);
            if (!i)
                cv_rect(8, 1, 224, 1, MI_RULE);
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
                c = MI_ORANGE;
            }
            break;
        case 2:
            l = "Peak over a second";
            int_text((int32_t)((eng_peak() + 5u) / 10u), v);
            text_cat(v, "%", sizeof v);
            if (eng_peak() >= 900u)
                c = MI_PINK;
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
                c = MI_PINK;
            break;
        }
        cv_text(8, y, &FONT_XS, l, MI_MUTED);
        cv_text(232 - text_w(&FONT_XS, v), y, &FONT_XS, v, c);
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
        col[i] = i == 1 ? MI_PINK : MI_INK;
    draw_lines(PANEL_Y + 4, 19, L, col, 8, -1);
}

static void draw_footer(void)            /* the tracks, as four outputs in their box */
{
    char t[40], n[8];
    uint32_t now = plat_ms(), g = eng_gates();
    int msg = now < ui.msg_until;
    int32_t s[6] = {(int32_t)g | ui.mod << 8 | msg << 9, (int32_t)(track_accent(0) ^ track_accent(1) << 1 ^
                    track_accent(2) << 2 ^ track_accent(3) << 3), ui.track,
                    eng_note(0) | eng_note(1) << 8 | eng_note(2) << 16, eng_note(3), msg ? text_hash(ui.msg[0]) : 0};
    if (!ui.full && !memcmp(s, ui.foot_shown, sizeof s))
        return;
    memcpy(ui.foot_shown, s, sizeof s);
    cv_begin(240, FOOT_H, MI_PANEL);
    rrect(2, 1, 236, 33, 6, MI_BOX);
    if (msg) {
        t[0] = 0;
        text_cat(t, ui.msg[0], sizeof t);
        text_cat(t, "  ", sizeof t);
        text_cat(t, ui.msg[1], sizeof t);
        cv_text(120 - text_w(&FONT_B, t) / 2, 9, &FONT_B, t, C_WHITE);
    } else if (ui.mod) {
        cv_text(10, 2, &FONT_XS, "MOD: hold a source key, turn a knob", C_WHITE);
        cv_text(10, 18, &FONT_T, "KEYS 1-4 A  5-8 B  9-11 X  12 Y  13-15 T", BOX_DIM);
    } else {
        for (int tr = 0; tr < ENG_TRACKS; tr++) {   /* a gate LED in the track's colour, its number, its note */
            int x = 2 + tr * 59, on = (g >> tr) & 1u, act = tr == ui.track;
            if (act)
                rrect(x + 3, 4, 53, 27, 5, MI_BOX_HI);
            lens((float)(x + 14), 17.5f, 4.4f, track_accent(tr), on, MI_BOX_LO);
            n[0] = (char)('1' + tr);
            n[1] = 0;
            cv_text(x + 24, 9, &FONT_B, n, act ? C_WHITE : BOX_DIM);
            note_text(eng_note(tr), t);
            cv_text(x + 35, 10, &FONT_XS, t, act ? BOX_TEXT : BOX_DIM);
        }
    }
    cv_blit(0, FOOT_Y);
}

static void draw(void)
{
    if (ui.full)
        lcd_fill(0, 0, 240, 240, MI_PANEL);
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
    case B_REC:
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
    } else if (b == B_REC) {
        if (ui.global)
            project_init(ui.init_sel);
        else
            app_say("INIT:", "GLO, THEN REC HELD");
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
            if (!e || !data_ok)
                continue;
            if (ui.global) {             /* GLO: K1 picks the project REC held loads */
                if (i == 0)
                    ui.init_sel = clamp(ui.init_sel + (e > 0 ? 1 : -1), 0, 2);
                continue;
            }
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
    /* lights: the page's button, PLAY, SEL in MOD mode, REC on GLO; the keys: held, or the sources on this page */
    {
        uint32_t bb = 0, kk = ui.keys;
        int pt = eng_page_type(ui.page);
        static const uint8_t PAGE_BTN[NPT] = {B_EDIT, B_EDIT, B_EDIT, B_LFO, B_LFO, B_SEQ, B_SEQ, B_ARP, B_ARP,
                                             B_HOME, B_FX, B_FX};
        if (!ui.global && !ui.matrix)
            bb |= 1u << PAGE_BTN[pt];
        if (ui.global)
            bb |= 1u << B_GLO | 1u << B_REC;
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
