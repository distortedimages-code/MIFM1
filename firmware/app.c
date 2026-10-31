/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1 v0.1: the screen and the panel. The sound is mi/engine.cc's (C++, compiled apart): this file edits its
 * project (eng_project) and shows what it plays.
 *
 *   KNOB 1..4   the page's four parameters        SELECT   the module's other face (1/3, 2/3...)
 *   ALGORITHM   the page's model or mode          PRESETS  the active track, 1 to 4
 *   EDIT        Plaits, the active track          LFO      Tides A, then B
 *   SEQ         Marbles, rhythm (T)               ARP      Marbles, notes (X, Y)
 *   HOME        the mixer                         GLO      the load of both cores, the tables, the version
 *   PLAY        Marbles' clock runs or stops      SAVE     held: the project saved (stopped)
 *   SEL         MOD mode: hold a source key, turn a knob: a cable. Held: the track's cables (MATRICE)
 *   keys        play the active track (MOD mode: the 16 sources, then Tides A and B fanned over the 4 knobs)
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
#define CELL_W 120
#define CELL_H 78
#define AREA_Y 52                        /* the knobs' cells, under the header and the model's line */
#define FOOT_Y 208

/* ------------------------------------------------------------------ state --- */
static int data_ok;                      /* the resource region holds this build's tables */
static int halted;                       /* app_panic: the engine stopped until the next frame */
static int engine_ok;
static int cpu2_ok;
static int save_hold, dirty;

static struct {
    int page;                            /* the page shown (engine.h: 22 of them) */
    int matrix, global;                  /* the MATRICE page, the GLO page, over the page */
    int track;                           /* the active track */
    int mod;                             /* MOD mode */
    int msel;                            /* MATRICE: the cable chosen (0..7 the track's, 8..15 the global ones) */
    int octave;                          /* the keyboard's */
    uint32_t keys, btn;
    uint32_t down_at[NB];
    int long_done[NB];
    uint32_t touched_at[4];
    int touched_k;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t drawn_at;
    int full;                            /* everything again */
    /* what each part of the screen showed, to draw only what changed */
    int16_t cell_shown[4][4];            /* knob, value, cables' hash, highlight */
    int head_shown[4], foot_shown[6], algo_shown[2];
} ui;

static int page_count(int page)          /* the faces of the module page belongs to: Plaits 3, the others 2 */
{
    return eng_page_type(page) <= PT_PLAITS3 ? 3 : 2;
}
static int page_face(int page)
{
    int pt = eng_page_type(page);
    switch (pt) {
    case PT_PLAITS1: case PT_TIDES1: case PT_MARB_T1: case PT_MARB_X1: case PT_MIX1: return 0;
    case PT_PLAITS2: case PT_TIDES2: case PT_MARB_T2: case PT_MARB_X2: case PT_MIX2: return 1;
    default: return 2;
    }
}
static int page_first(int page) { return page - page_face(page); }

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

static const char *page_title(int page, char *b)
{
    static const char *const T[NPT] = {"PLAITS 1/3", "PLAITS 2/3", "PLAITS 3/3", "TIDES 1/2", "TIDES 2/2",
                                       "MARBLES T 1/2", "MARBLES T 2/2", "MARBLES X 1/2", "MARBLES X 2/2",
                                       "MIXER 1/2", "MIXER 2/2"};
    int pt = eng_page_type(page);
    b[0] = 0;
    text_cat(b, T[pt], 24);
    if (pt == PT_TIDES1 || pt == PT_TIDES2) {   /* "TIDES A 1/2" */
        b[0] = 0;
        text_cat(b, "TIDES ", 24);
        text_cat(b, page >= PG_TIDES(1, 0) ? "B " : "A ", 24);
        text_cat(b, pt == PT_TIDES1 ? "1/2" : "2/2", 24);
    }
    return b;
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
    } else if (page < PG_MIX(0)) {
        text_cat(b, "MB", max);
    } else {
        text_cat(b, "MX", max);
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

static int load_now(void)                /* the worse core, in % */
{
    uint32_t a = eng_load(0), b = eng_load(1);
    return (int)(((a > b ? a : b) + 5u) / 10u);
}

static void draw_header(void)
{
    char t[32], v[12];
    int load = load_now();
    int s[4] = {ui.page | ui.matrix << 8 | ui.global << 9 | ui.mod << 10, load, eng_playing() | dirty << 1, ui.track};
    if (!ui.full && !memcmp(s, ui.head_shown, sizeof s))
        return;
    memcpy(ui.head_shown, s, sizeof s);
    cv_begin(240, 24, C_BLACK);
    t[0] = 0;
    if (ui.global)
        text_cat(t, "GLOBAL", sizeof t);
    else if (ui.matrix)
        text_cat(t, "MATRICE", sizeof t);
    else
        page_title(ui.page, t);
    cv_text(6, 4, &FONT_B, t, ui.mod ? C_WHITE : C_HI);
    t[0] = 0;
    if (eng_page_track(ui.page) >= 0 || ui.matrix) {
        text_cat(t, "PISTE ", sizeof t);
        int_text(ui.track + 1, v);
        text_cat(t, v, sizeof t);
    }
    cv_text(150 - text_w(&FONT_XS, t) / 2, 5, &FONT_XS, t, C_GRAY);
    t[0] = 0;
    int_text(load, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "%", sizeof t);
    cv_text(234 - text_w(&FONT_XS, t), 5, &FONT_XS, t, load >= 80 ? C_WHITE : load >= 60 ? C_HI : C_GRAY);
    if (eng_playing())                   /* a play triangle */
        for (int y = 0; y < 10; y++)
            cv_rect(198 - 18 + 0, 7 + y, y < 5 ? y + 1 : 10 - y, 1, C_HI);
    if (dirty)
        cv_rect(2, 2, 2, 2, C_AMB);
    if (ui.mod)
        cv_rect(0, 22, 240, 2, C_WHITE);
    else
        cv_rect(0, 23, 240, 1, C_LINE);
    cv_blit(0, 0);
}

static void draw_algo(void)
{
    project_t *p = eng_project();
    char t[32];
    int first = page_first(ui.page), n = eng_algo_count(first);
    int s[2] = {ui.page | ui.matrix << 8 | ui.global << 9, n ? p->algo[first] : -1};
    if (!ui.full && !memcmp(s, ui.algo_shown, sizeof s))
        return;
    memcpy(ui.algo_shown, s, sizeof s);
    cv_begin(240, 28, C_BLACK);
    t[0] = 0;
    if (ui.global || ui.matrix) {
        text_cat(t, ui.global ? "CHARGE ET TABLES" : "CABLES DE LA PISTE", sizeof t);
        cv_text(120 - text_w(&FONT_B, t) / 2, 6, &FONT_B, t, C_GRAY);
    } else if (n) {
        text_cat(t, eng_algo_name(first, p->algo[first]), sizeof t);
        const felucca_font_t *f = text_w(&FONT_M, t) <= 232 ? &FONT_M : &FONT_B;
        cv_text(120 - text_w(f, t) / 2, f == &FONT_M ? 2 : 6, f, t, C_WHITE);
    } else {
        int pt = eng_page_type(ui.page);
        text_cat(t, pt == PT_MIX1 || pt == PT_MIX2 ? "PISTES 1 A 4" : "", sizeof t);
        cv_text(120 - text_w(&FONT_B, t) / 2, 6, &FONT_B, t, C_GRAY);
    }
    cv_blit(0, 24);
}

/* the cables on a parameter: their count and a small hash (what changed), their names in b */
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
            if (b && n <= 3) {
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
    if (hash)
        *hash = h;
    return n;
}

static int bar_x(float v, int kind)      /* a value's place on a 108-pixel bar */
{
    float u = kind == K_BIP ? (v + 1.0f) * 0.5f : v;
    return (int)(fm_clampf(u, 0.0f, 1.0f) * 107.0f + 0.5f);
}

static void draw_cell(int k)
{
    project_t *p = eng_project();
    int param = ui.page * 4 + k;
    const eng_pdef_t *d = eng_pdef(param);
    float v = p->val[param], f = eng_final(param);
    char t[32], c[32];
    int hash = 0, hot = plat_ms() - ui.touched_at[k] < 1200u && ui.touched_k == k;
    int ncab = cables_on(param, c, sizeof c, &hash);
    int16_t s[4] = {(int16_t)(v * 1000.0f), (int16_t)(f * 1000.0f), (int16_t)hash, (int16_t)(hot | ui.mod << 1)};
    if (!ui.full && !memcmp(s, ui.cell_shown[k], sizeof s))
        return;
    memcpy(ui.cell_shown[k], s, sizeof s);
    cv_begin(CELL_W, CELL_H, C_BLACK);
    if (hot)
        cv_rect(0, 0, CELL_W, CELL_H, C_LINE);
    cv_text(6, 3, &FONT_XS, d->name, hot ? C_WHITE : C_GRAY);
    eng_param_text(param, ncab ? f : v, t, sizeof t);
    {
        const felucca_font_t *ft = text_w(&FONT_M, t) <= 108 ? &FONT_M : &FONT_B;
        int up = 1;                          /* FONT_M has capitals only */
        for (const char *q = t; *q; q++)
            if (*q >= 'a' && *q <= 'z')
                up = 0;
        if (!up)
            ft = &FONT_B;
        cv_text(6, ft == &FONT_M ? 22 : 28, ft, t, ncab ? C_HI : C_WHITE);
    }
    if (d->kind == K_UNI || d->kind == K_BIP) {
        int x0 = 6, y0 = 52, xv = bar_x(v, d->kind), xf = bar_x(f, d->kind);
        cv_rect(x0, y0, 108, 6, C_LINE);
        if (d->kind == K_BIP) {
            int xc = 54;
            if (xv >= xc)
                cv_rect(x0 + xc, y0, xv - xc + 1, 6, C_DIM);
            else
                cv_rect(x0 + xv, y0, xc - xv + 1, 6, C_DIM);
            cv_rect(x0 + xc, y0 - 2, 1, 10, C_GRAY);
        } else {
            cv_rect(x0, y0, xv + 1, 6, C_DIM);
        }
        if (ncab)
            cv_rect(x0 + xf - 1, y0 - 3, 3, 12, C_HI);
    } else {
        int lo = d->lo, hi = d->hi, i = (int)(v + (v < 0 ? -0.5f : 0.5f)), w = hi > lo ? 108 / (hi - lo + 1) : 108;
        for (int j = lo; j <= hi && w >= 3; j++)
            cv_rect(6 + (j - lo) * w, 54, w - 2, 3, j == i ? C_HI : C_LINE);
    }
    if (ncab)
        cv_text(6, 62, &FONT_T, c, C_AMB);
    else if (ui.mod && eng_param_mod(param))
        cv_text(6, 62, &FONT_T, "LIBRE", C_LINE);
    cv_blit((uint32_t)((k % 2) * CELL_W), (uint32_t)(AREA_Y + (k / 2) * CELL_H));
}

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
        text_cat(rows[i], first + i + 1 < 10 ? "   " : "  ", 40);
        if (!k->depth || k->src == SRC_NONE) {
            text_cat(rows[i], "-", 40);
            col[i] = C_DIM;
        } else {
            text_cat(rows[i], eng_src_name(k->src), 40);
            text_cat(rows[i], " > ", 40);
            param_name(k->dst, n, sizeof n);
            text_cat(rows[i], n, 40);
            text_cat(rows[i], "  ", 40);
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
    {
        char t[40];
        cv_begin(240, 18, C_BLACK);
        t[0] = 0;
        text_cat(t, g < ENG_TRACKS ? "K1 CABLE  K2 SOURCE  K3 CIBLE  K4 DOSE" : "CABLES GLOBAUX (TIDES, MARBLES, MIX)",
                 sizeof t);
        cv_text(6, 2, &FONT_T, t, C_GRAY);
        cv_blit(0, AREA_Y);
    }
    draw_lines(AREA_Y + 18, 17, lines, col, 8, ui.msel - first);
}

static void draw_global(void)
{
    static uint32_t at;
    char rows[8][40], v[12];
    const char *lines[8];
    uint16_t col[8];
    if (!ui.full && plat_ms() - at < 400u)
        return;
    at = plat_ms();
    for (int i = 0; i < 8; i++) {
        rows[i][0] = 0;
        lines[i] = rows[i];
        col[i] = C_GRAY;
    }
    text_cat(rows[0], "COEUR 0 (PISTES 1-2, MARBLES, TIDES) ", 40);
    int_text((int32_t)((eng_load(0) + 5u) / 10u), v);
    text_cat(rows[0], v, 40);
    text_cat(rows[0], "%", 40);
    text_cat(rows[1], "COEUR 1 (PISTES 3-4) ", 40);
    if (eng_core1_running()) {
        int_text((int32_t)((eng_load(1) + 5u) / 10u), v);
        text_cat(rows[1], v, 40);
        text_cat(rows[1], "%", 40);
    } else {
        text_cat(rows[1], cpu2_ok ? "EN PAUSE" : "ABSENT: TOUT SUR LE 0", 40);
        col[1] = C_AMB;
    }
    text_cat(rows[2], "PIC SUR 1 SECONDE ", 40);
    int_text((int32_t)((eng_peak() + 5u) / 10u), v);
    text_cat(rows[2], v, 40);
    text_cat(rows[2], "%", 40);
    col[0] = C_HI;
    col[2] = eng_peak() >= 900u ? C_WHITE : C_GRAY;
    text_cat(rows[3], "PLATEFORME (AUDIO) ", 40);
    int_text((int32_t)plat_cpu_pct(), v);
    text_cat(rows[3], v, 40);
    text_cat(rows[3], "%", 40);
    text_cat(rows[4], data_ok ? "TABLES DES MODULES: OK" : "TABLES DES MODULES ABSENTES", 40);
    col[4] = data_ok ? C_GRAY : C_WHITE;
    text_cat(rows[5], "PILE DU COEUR 1 ", 40);
    int_text((int32_t)plat_cpu2_stack(), v);
    text_cat(rows[5], v, 40);
    text_cat(rows[5], " / 2816 O", 40);
    text_cat(rows[6], "MIFM1 ", 40);
    text_cat(rows[6], APP_VERSION, 40);
    text_cat(rows[6], dirty ? "  (NON SAUVE)" : "", 40);
    text_cat(rows[7], "SAVE TENU: SAUVER (A L'ARRET)", 40);
    col[7] = C_DIM;
    draw_lines(AREA_Y, 19, lines, col, 8, -1);
}

static void draw_missing(void)
{
    static const char *const L[8] = {"", "TABLES DES MODULES ABSENTES", "", "L'INSTALLEUR LES ECRIT APRES",
                                     "LE FIRMWARE: REINSTALLER DEPUIS", "LA PAGE, FM-1 A L'ARRET", "", ""};
    uint16_t col[8];
    if (!ui.full)
        return;
    for (int i = 0; i < 8; i++)
        col[i] = i == 1 ? C_WHITE : C_GRAY;
    draw_lines(AREA_Y, 19, L, col, 8, -1);
}

static void draw_footer(void)
{
    char t[40], n[8];
    uint32_t now = plat_ms(), g = eng_gates();
    int msg = now < ui.msg_until;
    int s[6] = {(int)g, msg, ui.track | ui.mod << 4, eng_note(0) | eng_note(1) << 8 | eng_note(2) << 16,
                eng_note(3) | (int)(ui.keys & 0xFFFFu) << 8, ui.octave};
    if (!ui.full && !memcmp(s, ui.foot_shown, sizeof s))
        return;
    memcpy(ui.foot_shown, s, sizeof s);
    cv_begin(240, 32, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    if (msg) {
        t[0] = 0;
        text_cat(t, ui.msg[0], sizeof t);
        text_cat(t, "  ", sizeof t);
        text_cat(t, ui.msg[1], sizeof t);
        cv_text(120 - text_w(&FONT_B, t) / 2, 8, &FONT_B, t, C_WHITE);
    } else if (ui.mod) {
        cv_text(6, 2, &FONT_T, "MOD: TIENS UNE SOURCE, TOURNE UN POTARD", C_WHITE);
        cv_text(6, 16, &FONT_T, "TOUCHES 1-4 A  5-8 B  9-12 X Y  13-15 T", C_GRAY);
    } else {
        for (int tr = 0; tr < ENG_TRACKS; tr++) {
            int x = tr * 60;
            int on = (g >> tr) & 1u;
            n[0] = (char)('1' + tr);
            n[1] = 0;
            cv_rect(x + 4, 6, 20, 20, on ? C_HI : C_LINE);
            cv_text(x + 10, 9, &FONT_XS, n, on ? C_BLACK : tr == ui.track ? C_WHITE : C_GRAY);
            note_text(eng_note(tr), t);
            cv_text(x + 28, 9, &FONT_XS, t, tr == ui.track ? C_WHITE : C_GRAY);
            if (tr == ui.track)
                cv_rect(x + 4, 28, 52, 2, C_WHITE);
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
        draw_algo();
        draw_global();
    } else if (ui.matrix) {
        draw_algo();
        draw_matrix();
    } else {
        draw_algo();
        for (int k = 0; k < 4; k++)
            draw_cell(k);
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
    if (param / 4 == PG_MIX(0) || param / 4 == PG_MIX(1))
        return param % 4;                /* a track's level and pan: that track's cables */
    return ENG_TRACKS;
}

/* MOD mode: source s on parameter param, its depth moved by e */
static void cable_turn(int s, int param, int32_t e)
{
    project_t *p = eng_project();
    int g = cable_group(param), free = -1;
    cable_t *k = 0;
    if (!eng_param_mod(param)) {
        app_say("PAS MODULABLE", "");
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
            app_say("8 CABLES MAX", g < ENG_TRACKS ? "SUR CETTE PISTE" : "GLOBAUX");
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
        go_page(here && pt == PT_MIX1 ? PG_MIX(1) : PG_MIX(0));
        break;
    case B_GLO:
        ui.global = !ui.global;
        ui.matrix = 0;
        ui.full = 1;
        break;
    case B_FX:
        app_say("CLOUDS ET WARPS", "EN V0.2");
        break;
    case B_ENV:
        app_say("SCENES", "EN V0.3");
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
        ui.full = 0;
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
            app_say("STOP D'ABORD", "PUIS SAVE");
        else if (project_save() == 0)
            app_say("PROJET", "SAUVE");
        else
            app_say("SAUVEGARDE", "IMPOSSIBLE");
    }
}

static void button_up(int b)
{
    if (b == B_SEL && !ui.long_done[b]) {
        if (ui.matrix) {
            ui.matrix = 0;
            ui.full = 1;
        } else {
            ui.mod = !ui.mod;
            ui.full = 1;
        }
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
                for (int i = 0; i < ENG_TRACKS; i++)   /* the keyboard's notes stop on the old track */
                    for (k = 0; k < NKEYS; k++)
                        if (ui.keys & (1u << k))
                            eng_key(ui.track, 53 + (int)k + 12 * ui.octave, 0);
                ui.track = t;
                if (eng_page_track(ui.page) >= 0)
                    ui.page = PG_TRACK(t, page_face(ui.page));
                ui.full = 1;
            }
        }
        e = plat_enc(EN_SELECT);
        if (e && !ui.matrix && !ui.global) {
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
            if (ui.global)
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
        if (!ui.global && !ui.matrix)
            bb |= 1u << (pt <= PT_PLAITS3 ? B_EDIT : pt <= PT_TIDES2 ? B_LFO : pt <= PT_MARB_T2 ? B_SEQ
                         : pt <= PT_MARB_X2 ? B_ARP : B_HOME);
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
    if (ui.full || now - ui.drawn_at >= 33u) {
        ui.drawn_at = now;
        draw();
    }
}
