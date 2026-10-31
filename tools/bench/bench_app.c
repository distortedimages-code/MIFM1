/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1 v0.0, the bench. Each scene plays one Mutable Instruments module (or a busy mix over both cores) and the
 * render is timed block by block; REC runs every scene in turn and fills the results page, to photograph.
 *
 *   SELECT   the scene (LIVE page) or the list (RESULTS)    PLAY  sound on / off (the work goes on, silent)
 *   REC      the automatic pass, all scenes (REC again: stop) HOME  the page: LIVE, RESULTS, MEMORY
 *   SEL      the busy scenes on two cores or one             K1    the list, too
 *
 * Part of the unity build (base/device/unity.c, base/host/plat_host.c), after the platform: gfx.c is in scope.
 * The modules and their timing are C++ (mi/, compiled apart at -O2): this file only calls mi/scenes.h.
 *
 * The modules' biggest tables are in the resource region (app.h), which the installer writes after the firmware: until
 * it holds this build's, no scene plays (they would read erased flash) and the screen says to install again. */
#include "fastmath.h"
#include "mi/scenes.h"
const void *mi_res_where(void);          /* mi/res_symbols.cc: where the moved tables are (FM-1: the region) */
#ifdef PLAT_HOST
int mi_res_check(const uint8_t *payload, uint32_t len);   /* mi/res_symbols.cc: the region's file against the arrays */
#endif

#define BLOCK_US ((float)APP_BLOCK * 1000000.0f / 44100.0f)   /* 2902 us: a block's time, the 100 % */
#define AUTO_MS 3000u                    /* each scene's time in the automatic pass */
#define MAX_SCENES 64
#define LINES 16                         /* 16 lines of 15 px: the screen */
#define LINE_H 15

/* ---------------------------------------------------------------- results --- */
typedef struct {
    uint8_t done;                        /* 1 measured, 2 not enough RAM */
    uint8_t core1;                       /* the second part ran on the second core (most blocks) */
    uint16_t c0, c0_pk, c1, c1_pk, tot, tot_pk;   /* tenths of a percent of the block */
    uint16_t xruns;                      /* the platform's late blocks meanwhile */
} result_t;
static struct {                          /* the results, as saved (OBJ_RESULTS) and in a backup (tools/read_results.py) */
    uint32_t magic;
    uint16_t count, size;                /* the scenes; a result's bytes */
    result_t r[MAX_SCENES];
    char name[MAX_SCENES][16];           /* each scene's short name: the file says what it holds */
} saved;
static result_t *const res = saved.r;
#define SAVED_MAGIC 0x3152494Du          /* "MIR1" */
static int save_pending, save_hold;      /* results to save; a backup streaming (no saving meanwhile) */

static int cpu2_ok;                      /* the second core started (plat_cpu2_set) */
static int data_ok;                      /* the resource region holds this build's tables */
static int halted;                       /* app_panic: the scene stopped until app_ui_init */
static int ui_automatic(void);
static void text_cat(char *d, const char *s, uint32_t max);
static volatile int listen = 1;          /* the render's sound goes out */

/* ------------------------------------------------------------------ audio --- */
static float out_l[APP_BLOCK], out_r[APP_BLOCK];

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float g = listen ? (float)master_q12 * (8300000.0f / 4096.0f) : 0.0f;
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    sc_render(out_l, out_r, n);
    for (i = 0; i < n; i++) {
        float l = fm_clampf(out_l[i], -1.0f, 1.0f), r = fm_clampf(out_r[i], -1.0f, 1.0f);
        lr[2 * i] = (int32_t)(l * g);
        lr[2 * i + 1] = (int32_t)(r * g);
    }
}

/* the tables in the region, where the moved symbols point (on the FM-1), the same bytes (on a computer) */
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

/* the results across power-off */
static void results_load(void)          /* another build's (other scenes) or none: no results */
{
    int i, n = sc_count();
    if (plat_store_load(OBJ_RESULTS, &saved, sizeof saved) != (int)sizeof saved || saved.magic != SAVED_MAGIC ||
        saved.count != n || saved.size != sizeof(result_t))
        for (i = 0; i < MAX_SCENES; i++)
            res[i].done = 0;
}
static void results_save(void)
{
    int i;
    if (save_hold) {
        save_pending = 1;
        return;
    }
    save_pending = 0;
    saved.magic = SAVED_MAGIC;
    saved.count = (uint16_t)sc_count();
    saved.size = sizeof(result_t);
    for (i = 0; i < MAX_SCENES; i++) {
        saved.name[i][0] = 0;
        if (i < saved.count)
            text_cat(saved.name[i], sc_short(i), sizeof saved.name[i]);
    }
    plat_store_save(OBJ_RESULTS, &saved, sizeof saved);
}
int app_backup_playing(void) { return ui_automatic(); }
void app_backup_flush(void)
{
    if (save_pending)
        results_save();
}
void app_backup_hold(int on) { save_hold = on; }
void app_backup_restored(void) { results_load(); }

void app_panic(void)                     /* an update, a restore or the tables' transfer: nothing reads the flash */
{
    sc_stop();
    halted = 1;
}

void app_init(void)
{
    cpu2_ok = plat_cpu2_set(1) == 0;     /* the one time it may start: here, at power-on */
    data_ok = data_check();
    results_load();
    if (data_ok)
        sc_start(0);
}

/* --------------------------------------------------------------- the text --- */
static struct {
    char t[LINES][44];
    uint16_t c[LINES];
    char r[LINES][16];                   /* a field on the right */
    uint16_t rc[LINES];
} scr;

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

static void hex_text(uint32_t v, char *b)     /* 8 hex digits */
{
    int i;
    for (i = 7; i >= 0; i--, v >>= 4)
        b[i] = "0123456789ABCDEF"[v & 15u];
    b[8] = 0;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* why the tables are missing, in two lines: the region's header as the flash holds it (device/res.c: "RES1", the
 * payload's length, its CRC-32), what the platform made of it (plat_res), where the moved symbols point */
static void data_why(char *a, char *b)
{
    uint32_t len = 0, magic, hlen, hcrc;
    const uint8_t *h = plat_data(MIFM1_RES_BASE), *p = plat_res(&len);
    char v[12];
    a[0] = b[0] = 0;
    if (!h) {
        text_cat(a, "ZONE HORS DONNEES", 40);
        return;
    }
    magic = rd32(h), hlen = rd32(h + 4), hcrc = rd32(h + 8);
    if (magic != 0x31534552u) {          /* "RES1" */
        text_cat(a, magic == 0xFFFFFFFFu ? "ZONE VIDE (NON ECRITE)" : "ZONE: AUTRE CONTENU ", 40);
        if (magic != 0xFFFFFFFFu) {
            hex_text(magic, v);
            text_cat(a, v, 40);
        }
        return;
    }
    text_cat(a, "ZONE ECRITE ", 40);
    int_text((int32_t)hlen, v);
    text_cat(a, v, 40);
    text_cat(a, " O CRC ", 40);
    hex_text(hcrc, v);
    text_cat(a, v, 40);
    if (!p) {
        text_cat(b, hlen == MIFM1_RES_LEN ? "REFUSEE: CONTENU DIFFERENT" : "REFUSEE: AUTRE TAILLE", 40);
        return;
    }
    text_cat(b, "LUE ", 40);
    hex_text((uint32_t)(uintptr_t)p, v);
    text_cat(b, v, 40);
    text_cat(b, " SYMB ", 40);
    hex_text((uint32_t)(uintptr_t)mi_res_where(), v);
    text_cat(b, v, 40);
}

static void tenths_text(uint32_t v, char *b)   /* 123 -> "12.3" */
{
    char t[12];
    int_text((int32_t)(v / 10u), b);
    t[0] = '.';
    t[1] = (char)('0' + v % 10u);
    t[2] = 0;
    text_cat(b, t, 12);
}

static void line(int i, const char *s, uint16_t c)
{
    scr.t[i][0] = 0;
    text_cat(scr.t[i], s, sizeof scr.t[i]);
    scr.c[i] = c;
    scr.r[i][0] = 0;
}

static void line_right(int i, const char *s, uint16_t c)
{
    scr.r[i][0] = 0;
    text_cat(scr.r[i], s, sizeof scr.r[i]);
    scr.rc[i] = c;
}

static void draw_lines(void)
{
    int strip, k;
    for (strip = 0; strip * 3 < LINES; strip++) {
        int rows = LINES - strip * 3 < 3 ? LINES - strip * 3 : 3;
        cv_begin(240, (uint32_t)(rows * LINE_H), C_BLACK);
        for (k = 0; k < rows; k++) {
            int i = strip * 3 + k;
            cv_text(4, k * LINE_H, &FONT_XS, scr.t[i], scr.c[i]);
            if (scr.r[i][0])
                cv_text(236 - text_w(&FONT_XS, scr.r[i]), k * LINE_H, &FONT_XS, scr.r[i], scr.rc[i]);
        }
        cv_blit(0, (uint32_t)(strip * 3 * LINE_H));
    }
}

/* ------------------------------------------------------------------- the UI --- */
enum { PAGE_LIVE, PAGE_RESULTS, PAGE_MEMORY, NPAGES };
static struct {
    int page;
    int scroll;                          /* the results' first row */
    int automatic;                       /* the pass is running: the scene it is on, from 1 (0: off) */
    uint32_t auto_t;                     /* when the scene started */
    uint32_t xruns0;                     /* the platform's count at the scene's start */
    uint32_t btn;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t drawn_at;
    int redraw;
} ui;

static int ui_automatic(void) { return ui.automatic != 0; }

void app_say(const char *a, const char *b)
{
    ui.msg[0][0] = ui.msg[1][0] = 0;
    text_cat(ui.msg[0], a ? a : "", sizeof ui.msg[0]);
    text_cat(ui.msg[1], b ? b : "", sizeof ui.msg[1]);
    ui.msg_until = plat_ms() + 1500u;
    ui.redraw = 1;
}

static uint16_t pct10(uint32_t ticks)    /* a time in ticks as tenths of a percent of the block */
{
    float p = (float)ticks / (float)PLAT_TICKS_PER_US / BLOCK_US * 1000.0f;
    return (uint16_t)(p > 65000.0f ? 65000.0f : p + 0.5f);
}

static void take_result(int i)           /* the current scene's numbers into the results */
{
    sc_stats_t s;
    result_t *r;
    if (i < 0 || i >= MAX_SCENES)
        return;
    r = &res[i];
    sc_stats(&s);
    if (!s.blocks) {
        r->done = sc_need(i) > sc_arena() ? 2 : 0;
        return;
    }
    r->done = 1;
    r->c0 = pct10(s.c0_sum / s.blocks);
    r->c0_pk = pct10(s.c0_peak);
    r->c1 = pct10(s.c1_sum / s.blocks);
    r->c1_pk = pct10(s.c1_peak);
    r->tot = pct10(s.tot_sum / s.blocks);
    r->tot_pk = pct10(s.tot_peak);
    r->core1 = s.c1_jobs * 2u > s.blocks;
    r->xruns = (uint16_t)(plat_xruns() - ui.xruns0);
}

static int go(int i)                     /* scene i (another one: the first): 0 = it plays */
{
    int rc;
    if (!data_ok) {
        sc_stop();
        app_say("TABLES ABSENTES", "REINSTALLER");
        return -1;
    }
    if (i < 0 || i >= sc_count() || i >= MAX_SCENES)
        i = 0;
    rc = sc_start(i);
    if (rc != 0)
        res[i].done = 2;                 /* (more RAM than the arena) */
    ui.xruns0 = plat_xruns();
    ui.redraw = 1;
    return rc;
}

static void auto_step(void)
{
    int n = sc_count();
    if (!ui.automatic || plat_ms() - ui.auto_t < AUTO_MS)
        return;
    take_result(ui.automatic - 1);
    if (ui.automatic >= n) {             /* done: the results */
        ui.automatic = 0;
        ui.page = PAGE_RESULTS;
        ui.scroll = 0;
        results_save();
        app_say("BANC TERMINE", "PHOTOGRAPHIE");
        return;
    }
    for (;;) {                           /* the next scene; one that cannot play (no RAM) costs no time */
        ui.automatic++;
        ui.auto_t = plat_ms();
        if (go(ui.automatic - 1) == 0)
            return;
        if (ui.automatic >= n) {
            ui.auto_t -= AUTO_MS;        /* the last one: done at the next frame */
            return;
        }
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), k;
    int32_t e;
    int n = sc_count();
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_PLAY) {
            listen = !listen;
            app_say("SON", listen ? "OUI" : "NON");
        } else if (k == B_REC) {
            if (ui.automatic) {
                ui.automatic = 0;
                app_say("BANC", "ARRETE");
            } else if (!data_ok) {
                app_say("TABLES ABSENTES", "REINSTALLER");
            } else {
                int i;
                for (i = 0; i < MAX_SCENES; i++)
                    res[i].done = 0;
                ui.automatic = 1;
                ui.auto_t = plat_ms();
                ui.page = PAGE_LIVE;
                go(0);
                app_say("BANC", "AUTOMATIQUE");
            }
        } else if (k == B_HOME) {
            ui.page = (ui.page + 1) % NPAGES;
        } else if (k == B_SEL) {
            static int one;
            one = !one;
            sc_core1(!one);
            sc_stats_reset();
            app_say("COEURS", one ? "UN SEUL" : "DEUX");
        }
        ui.redraw = 1;
    }
    ui.btn = btn;
    e = plat_enc(EN_SELECT) + plat_enc(EN_K1);
    if (e) {
        if (ui.page == PAGE_LIVE && !ui.automatic) {
            int i = sc_current() + (int)e;
            take_result(sc_current());
            i = (i % n + n) % n;
            go(i);
        } else if (ui.page == PAGE_RESULTS) {
            ui.scroll = (int)clamp(ui.scroll + e * 3, 0, n > LINES - 2 ? n - (LINES - 2) : 0);
        }
        ui.redraw = 1;
    }
    plat_glow(1);
    plat_leds((listen ? 1u << B_PLAY : 0u) | (ui.automatic ? 1u << B_REC : 0u) | (cpu2_ok ? 1u << B_SEL : 0u), 0u);
}

void app_input_only(void) { input(); }

static void header(char *t)
{
    t[0] = 0;
    text_cat(t, "MIFM1 BANC ", 44);
    text_cat(t, APP_VERSION, 44);
}

static void live_row(int i, const char *label, uint32_t avg, uint32_t pk)
{
    char t[24], v[12];
    line(i, label, C_GRAY);
    t[0] = 0;
    tenths_text(avg, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "%  PIC ", sizeof t);
    tenths_text(pk, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "%", sizeof t);
    line_right(i, t, avg >= 900u ? C_WHITE : C_HI);
}

static void page_live(void)
{
    char t[48], v[16];
    sc_stats_t s;
    int cur = sc_current(), n = sc_count();
    sc_stats(&s);
    header(t);
    line(0, t, C_WHITE);
    line_right(0, cpu2_ok ? "2 COEURS" : "1 COEUR", cpu2_ok ? C_HI : C_DIM);
    t[0] = 0;
    text_cat(t, "SCENE ", sizeof t);
    int_text(cur + 1, v);
    text_cat(t, v, sizeof t);
    text_cat(t, " / ", sizeof t);
    int_text(n, v);
    text_cat(t, v, sizeof t);
    line(1, t, C_GRAY);
    if (ui.automatic) {
        uint32_t left = (uint32_t)(n - ui.automatic) * AUTO_MS + AUTO_MS - (plat_ms() - ui.auto_t);
        t[0] = 0;
        text_cat(t, "AUTO, RESTE ", sizeof t);
        int_text((int32_t)(left / 1000u), v);
        text_cat(t, v, sizeof t);
        text_cat(t, " S", sizeof t);
        line_right(1, t, C_WHITE);
    }
    line(2, sc_name(cur), C_WHITE);
    line(3, "", C_GRAY);
    if (!data_ok) {                      /* the region not written yet (or another build's) */
        line(2, "", C_GRAY);
        line(4, "TABLES DES MODULES ABSENTES", C_WHITE);
        line(5, "L'INSTALLEUR LES ECRIT APRES", C_GRAY);
        line(6, "LE FIRMWARE: REINSTALLER DEPUIS", C_GRAY);
        line(7, "LA PAGE, FM-1 A L'ARRET", C_GRAY);
        {
            char a[40], b[40];
            data_why(a, b);
            line(8, a, C_HI);
            line(9, b, C_HI);
        }
    } else if (sc_need(cur) > sc_arena()) {
        line(4, "PAS ASSEZ DE RAM", C_WHITE);
        t[0] = 0;
        int_text((int32_t)(sc_need(cur) / 1024u), v);
        text_cat(t, v, sizeof t);
        text_cat(t, " KO / ", sizeof t);
        int_text((int32_t)(sc_arena() / 1024u), v);
        text_cat(t, v, sizeof t);
        text_cat(t, " KO", sizeof t);
        line(5, t, C_HI);
        for (int i = 6; i < 10; i++)
            line(i, "", C_GRAY);
    } else if (!s.blocks) {
        line(4, "MESURE...", C_GRAY);
        for (int i = 5; i < 10; i++)
            line(i, "", C_GRAY);
    } else {
        live_row(4, "COEUR 0", pct10(s.c0_sum / s.blocks), pct10(s.c0_peak));
        if (sc_has_core1(cur)) {
            live_row(5, s.c1_jobs * 2u > s.blocks ? "COEUR 1" : "PARTIE 2 (C0)", pct10(s.c1_sum / s.blocks),
                     pct10(s.c1_peak));
            live_row(6, "TOTAL", pct10(s.tot_sum / s.blocks), pct10(s.tot_peak));
        } else {
            line(5, "", C_GRAY);
            line(6, "", C_GRAY);
        }
        t[0] = 0;
        int_text((int32_t)s.measured, v);
        text_cat(t, v, sizeof t);
        text_cat(t, " BLOCS  RETARDS ", sizeof t);
        int_text((int32_t)(plat_xruns() - ui.xruns0), v);
        text_cat(t, v, sizeof t);
        line(7, t, C_GRAY);
        t[0] = 0;
        text_cat(t, "RAM ", sizeof t);
        int_text((int32_t)((sc_need(cur) + 1023u) / 1024u), v);
        text_cat(t, v, sizeof t);
        text_cat(t, " / ", sizeof t);
        int_text((int32_t)(sc_arena() / 1024u), v);
        text_cat(t, v, sizeof t);
        text_cat(t, " KO", sizeof t);
        line(8, t, C_GRAY);
        t[0] = 0;
        text_cat(t, "PILE C1 ", sizeof t);
        int_text((int32_t)plat_cpu2_stack(), v);
        text_cat(t, v, sizeof t);
        text_cat(t, " / 2816 O", sizeof t);
        line(9, cpu2_ok ? t : "", C_GRAY);
    }
    t[0] = 0;
    text_cat(t, "CHARGE AUDIO (PLATEFORME) ", sizeof t);
    int_text((int32_t)plat_cpu_pct(), v);
    text_cat(t, v, sizeof t);
    text_cat(t, "%", sizeof t);
    line(10, t, C_DIM);
    line(11, "", C_GRAY);
    line(12, "SELECT  SCENE      PLAY  SON", C_DIM);
    line(13, "REC  BANC AUTOMATIQUE (3 MIN)", C_DIM);
    line(14, "HOME  PAGE    SEL  1 OU 2 COEURS", C_DIM);
    line(15, listen ? "SON: OUI" : "SON: NON", listen ? C_GRAY : C_DIM);
}

static void page_results(void)
{
    char t[48], v[12];
    int n = sc_count(), i, row;
    header(t);
    line(0, t, C_WHITE);
    line_right(0, "MOY  PIC  (C1)", C_GRAY);
    for (row = 1; row < LINES - 1; row++) {
        i = ui.scroll + row - 1;
        if (i >= n) {
            line(row, "", C_GRAY);
            continue;
        }
        line(row, sc_short(i), res[i].done == 1 ? C_HI : C_DIM);
        if (res[i].done == 2) {
            line_right(row, "PAS DE RAM", C_WHITE);
        } else if (res[i].done == 1) {
            uint32_t a = sc_has_core1(i) ? res[i].tot : res[i].c0, p = sc_has_core1(i) ? res[i].tot_pk : res[i].c0_pk;
            t[0] = 0;
            tenths_text(a, v);
            text_cat(t, v, sizeof t);
            text_cat(t, "  ", sizeof t);
            tenths_text(p, v);
            text_cat(t, v, sizeof t);
            if (sc_has_core1(i)) {          /* "*": its second part ran on core 0 (one core) */
                text_cat(t, res[i].core1 ? "  " : "  *", sizeof t);
                tenths_text(res[i].c1, v);
                text_cat(t, v, sizeof t);
            }
            if (res[i].xruns)
                text_cat(t, " !", sizeof t);
            line_right(row, t, p >= 1000u || res[i].xruns ? C_WHITE : C_HI);
        } else {
            line_right(row, "-", C_DIM);
        }
    }
    t[0] = 0;
    text_cat(t, "% D'UN BLOC  ", sizeof t);
    int_text(ui.scroll + 1, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "-", sizeof t);
    int_text(ui.scroll + LINES - 2 < n ? ui.scroll + LINES - 2 : n, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "/", sizeof t);
    int_text(n, v);
    text_cat(t, v, sizeof t);
    line(LINES - 1, t, C_DIM);
}

static void mem_row(int i, const char *label, uint32_t bytes)
{
    char t[16], v[12];
    line(i, label, C_GRAY);
    t[0] = 0;
    tenths_text((bytes * 10u + 512u) / 1024u, v);
    text_cat(t, v, sizeof t);
    text_cat(t, " KO", sizeof t);
    line_right(i, t, C_HI);
}

static void page_memory(void)
{
    char t[48];
    header(t);
    line(0, t, C_WHITE);
    line(1, "RAM DE CHAQUE MODULE", C_GRAY);
    mem_row(2, "PLAITS, UNE VOIX", sc_size_plaits());
    mem_row(3, "RINGS (4 VOIX MAX)", sc_size_rings());
    mem_row(4, "  + SA REVERB", sc_size_rings_reverb());
    mem_row(5, "CLOUDS ET SES TAMPONS", sc_size_clouds());
    mem_row(6, "WARPS", sc_size_warps());
    mem_row(7, "TIDES", sc_size_tides());
    mem_row(8, "MARBLES", sc_size_marbles());
    line(9, "", C_GRAY);
    mem_row(10, "ARENE DES SCENES", sc_arena());
    mem_row(11, "TABLES (ZONE RESSOURCES)", MIFM1_RES_LEN);
    if (!data_ok)
        line_right(11, "ABSENTES", C_WHITE);
    line(12, "", C_GRAY);
    line(13, "BLOC AUDIO: 128 ECHANTILLONS,", C_DIM);
    line(14, "2902 MICROSECONDES = 100%", C_DIM);
    line(15, "", C_GRAY);
}

static void draw(void)
{
    if (ui.page == PAGE_RESULTS)
        page_results();
    else if (ui.page == PAGE_MEMORY)
        page_memory();
    else
        page_live();
    if (plat_ms() < ui.msg_until) {      /* a message over the last line */
        char t[48];
        t[0] = 0;
        text_cat(t, ui.msg[0], sizeof t);
        text_cat(t, "  ", sizeof t);
        text_cat(t, ui.msg[1], sizeof t);
        line(LINES - 1, t, C_WHITE);
    }
    draw_lines();
}

#ifdef PLAT_HOST
void app_host_reboot(void)
{
    memset(&saved, 0, sizeof saved);
    memset(&ui, 0, sizeof ui);
    halted = save_pending = save_hold = 0;
    listen = 1;
}
#endif

void app_ui_init(void)                   /* at power-on, and after an update, a restore or the tables' transfer */
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    data_ok = data_check();
    if (!data_ok)
        sc_stop();
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    if (halted) {                        /* after app_panic (an update, a restore, the tables' transfer: each done
                                          * before the main loop draws again): the scene again, from its start */
        halted = 0;
        ui.automatic = 0;
        data_ok = data_check();
        if (data_ok)
            go(sc_current());            /* (none yet, at the first install: the first) */
        ui.redraw = 1;
    }
    input();
    auto_step();
    if (save_pending && !save_hold)
        results_save();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.page == PAGE_LIVE && now - ui.drawn_at >= 300u)   /* the numbers move: four times a second */
        ui.redraw = 1;
    if (ui.redraw) {
        ui.redraw = 0;
        ui.drawn_at = now;
        draw();
    }
}
