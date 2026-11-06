/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1: the screen and the panel. The sound is mi/engine.cc's (C++, compiled apart): this file edits its project
 * (eng_project) and shows what it plays.
 *
 * The screen is a module's face, dark, its lights in three colours (a theme's: GLO's second face, K4; 0.1 to 0.3.6
 * had Mutable Instruments' teal, orange and pink, as their panels print them): at the top the module's icon in a lit ring, its name and a line on its state (Plaits shows
 * the model alone, its logo and its name); then its four knobs in a row, as KNOB 1 to 4 sit on the FM-1, each a ring
 * lit from its start (or its middle) to its value, the value in it; at the bottom the four tracks, an LED each lit by
 * its gate (on the LFOs' pages: the three LFOs, lit as high as they are; on the steps' and the effects' pages: the
 * steps).
 * A module's faces slide in from the side SELECT turns to.
 *
 *   KNOB 1..4   the page's four parameters        SELECT   the module's other face (1/3, 2/3...)
 *   ALGORITHM   the page's model or mode (the delay: SYNC or FREE)   PRESETS  the active track, 1 to 4
 *   EDIT        a tap: the active track's sets (its sound, its bending, its filter). Held: the presets (0.15.1), a
 *               track's sound each, 11 banks of 16 (A the factory's): a black key a bank, a white key that sound onto
 *               the track (heard while held); SAVE held, a white key: saved there; REC held: erased
 *   LFO         the three LFOs, one after the other
 *   SEQ         a tap: the lane's steps (a track's: NOTE, PROB, RATCHET (0.16.4: 1..16 hits over SPAN), LENGTH, then
 *               its RANDOM, its EUCLID; an effect's: DECAY, PROB, DEPTH, LENGTH), last its PLAY face (0.16: SPEED,
 *               DIRECTION, a track's ratchets' SPAN and CURVE, an effect's PROB and LENGTH). Held: the patterns (0.15), 11 banks of 16, each a whole project: a black key
 *               a bank, a white key a pattern (several: an order, looped), from the playing one's end; REC held, a
 *               white key erases one
 *   ARP         Endless, the track's: REC records (a key a note, SEL a rest), REC again plays it, a key transposes
 *               it; REC held clears it. Again: its options (0.16.2: STRIDE, REST, HOLES, LEAP)
 *   HOME        the mixer: the levels, the pans
 *   FX          the effects, one after the other: the delay, the reverb, Warps; SELECT their faces (their knobs,
 *               the tracks' sends, their steps)
 *   GLO         TEMPO, SCALE, ROOT, SWING; then the project (K1, REC held: INIT, the demo, the saved one, the stress
 *               test, which plays at once: the machine's figures under it) and the machine. Held with a knob:
 *               ALL, the same parameter on the four tracks (with ALGORITHM: the same model), the screen yellow while
 *               GLO is held; for 4 s after, GLO undoes it as the next bar starts (again: it stays)
 *   ENV         the track's envelope (PLAITS 2/3: DECAY, LPG COLOUR, the internal envelope's TIMBRE and MORPH).
 *               Held with a knob: that parameter's random (right new at each trig, left looped, 0 none)
 *   REC         KEY mode (the keys play the track) or STEP mode: the white keys a page's 16 steps, the first seven
 *               black keys the lanes (the four tracks, the delay, the reverb, Warps), OCT- OCT+ the pages; a step
 *               held and a knob turned: a lock (the lane's knobs); a step held and SEL: its locks cleared. Held: the
 *               lane erased (with GLO, 3 s: everything as a new project has it but the four sounds, the tempo and
 *               the scale). REC and SEL held: every cable erased (the modulations)
 *   PLAY        the clock runs or stops (twice quickly: stopped, every sound cut)   SAVE  held: the pattern saved
 *               (EDIT held, a white key: a preset)
 *   SEL         MOD mode: hold an LFO's key, turn a knob: a cable, as deep as turned (to the left: upside down).
 *               Held: the track's cables (MATRIX)
 *   keys        play the active track (MOD mode: the first three white keys are the LFOs, blinking at their rates)
 *   OCT- OCT+   the keyboard's octave (STEP mode: the steps' page). OCT- held, SELECT: the random dose, on its own
 *               screen: the sound's knobs (pitches, timbres, filters, sends) thrown aside, more or less, 0 the sound
 *               as set; its last quarter (76..100 %) adds fills at the ends of the bars. OCT+ held, SELECT:
 *               TRAUMA, on its own screen: a dose of saturation (the drives, the filters' FM, Warps), 0 nothing
 *
 * Part of the unity build (base/device/unity.c, base/host/plat_host.c), after the platform: gfx.c is in scope.
 * The modules' biggest tables are in the resource region (app.h), which the installer writes after the firmware:
 * until it holds this build's, the engine does not run (it would read erased flash) and the screen says so. */
#include "fastmath.h"
#include "mi/engine.h"
/* (0.16.31) the screens seen less often drawn small rather than fast (their pixels are gfx.c's, as fast as ever): the
 * machine's, the matrix, the panels of the bits, the glitch, the filter, the envelopes, the LFOs, the mixer, Warps,
 * Endless and the Euclidean rhythms. A few kilobytes of program flash for a little time a frame there */
#define APP_SMALL __attribute__((minsize))
/* (0.16.31) the screen's parts drawn small rather than fast (their pixels are gfx.c's, as fast as ever): 4 KB of
 * program flash, for a few microseconds a frame */
#define APP_SMALL __attribute__((minsize))
#include "mi/bridge.h"                   /* (the filters' models and types: MF_*) */

const void *mi_res_where(void);          /* mi/res_symbols.cc: where the moved tables are (FM-1: the region) */
#ifdef PLAT_HOST
int mi_res_check(const uint8_t *payload, uint32_t len);   /* mi/res_symbols.cc: the region's file against the arrays */
#endif

#define LONG_MS 600u                     /* a long press */
#define SHOW_MS 1500u                    /* a message's time */
#define CUT_MS 400u                      /* PLAY twice within it: every sound cut */
#define HEAD_H 28                        /* the screen: the band and the header, the module's display, the knobs, */
#define PANEL_Y 28                       /* the tracks */
#define PANEL_H 76
#define KNOB_Y 104
#define KNOB_W 60
#define KNOB_H 100
#define FOOT_Y 204
#define FOOT_H 36

/* the colours: a theme (GLO's second face, K4), each a dark panel, its greys, three accents (the module's state as its
 * icons print it: in Mutable Instruments' colours, teal, orange and pink) and the colour ALL fills the screen with */
typedef struct {
    const char *name;
    uint16_t bg, txt, hi, muted, dim;
    uint16_t track, off, rule, sel;      /* a ring's travel unlit (1 px), an LED off, a rule, the chosen row */
    uint16_t a1, a2, a3, knock;
} theme_t;
#define NTHEMES 8
#define THEME_GRAPHITE 7                 /* (the browser emulator's demo: its drawn machine's greys and orange) */
static const theme_t THEMES[NTHEMES] = {
    {"Acid",                             /* black; lime, violet, a red-orange */
     RGB(0, 0, 0), RGB(236, 240, 228), RGB(255, 255, 255), RGB(150, 154, 146), RGB(102, 106, 98),
     RGB(58, 62, 54), RGB(46, 50, 42), RGB(34, 38, 30), RGB(42, 46, 38),
     RGB(184, 255, 46), RGB(160, 112, 255), RGB(255, 84, 46), RGB(200, 255, 0)},
    {"Ember",                            /* a warm black; gold, orange, crimson */
     RGB(18, 10, 6), RGB(244, 230, 207), RGB(255, 247, 232), RGB(170, 148, 126), RGB(118, 100, 82),
     RGB(66, 52, 40), RGB(54, 42, 32), RGB(42, 31, 23), RGB(58, 42, 30),
     RGB(255, 201, 74), RGB(255, 108, 44), RGB(230, 36, 78), RGB(255, 108, 44)},
    {"Neon",                             /* indigo; mint, lavender, pink */
     RGB(11, 8, 32), RGB(236, 232, 255), RGB(255, 255, 255), RGB(154, 147, 191), RGB(106, 99, 144),
     RGB(50, 44, 86), RGB(40, 34, 72), RGB(30, 25, 58), RGB(44, 37, 84),
     RGB(60, 242, 208), RGB(180, 140, 255), RGB(255, 79, 163), RGB(60, 242, 208)},
    {"Ice",                              /* a blue black; cyan, periwinkle, magenta */
     RGB(4, 9, 15), RGB(230, 241, 250), RGB(255, 255, 255), RGB(142, 163, 181), RGB(94, 113, 130),
     RGB(38, 52, 64), RGB(30, 42, 52), RGB(22, 32, 41), RGB(30, 44, 57),
     RGB(69, 224, 255), RGB(124, 140, 255), RGB(255, 79, 216), RGB(69, 224, 255)},
    {"Rust",                             /* a graphite; verdigris, rust, a hazard yellow */
     RGB(10, 12, 12), RGB(232, 228, 218), RGB(250, 247, 240), RGB(150, 148, 140), RGB(100, 98, 92),
     RGB(54, 57, 55), RGB(43, 46, 44), RGB(32, 35, 34), RGB(42, 46, 44),
     RGB(78, 196, 164), RGB(226, 100, 42), RGB(242, 196, 48), RGB(226, 100, 42)},
    {"Mono",                             /* black; white, silver, red */
     RGB(0, 0, 0), RGB(237, 237, 237), RGB(255, 255, 255), RGB(154, 154, 154), RGB(102, 102, 102),
     RGB(58, 58, 58), RGB(46, 46, 46), RGB(36, 36, 36), RGB(46, 46, 46),
     RGB(245, 245, 245), RGB(176, 176, 176), RGB(255, 42, 42), RGB(255, 42, 42)},
    {"Mutable",                          /* 0.1 to 0.3.6: black; teal, orange, pink (the modules' LEDs), ALL yellow */
     RGB(0, 0, 0), RGB(240, 239, 235), RGB(255, 255, 255), RGB(154, 151, 152), RGB(107, 104, 105),
     RGB(60, 58, 59), RGB(48, 46, 47), RGB(38, 36, 37), RGB(46, 44, 45),
     RGB(24, 201, 212), RGB(255, 178, 46), RGB(255, 74, 125), RGB(255, 214, 0)},
    {"Graphite",                         /* greys and one light, orange (Elektron's Machinedrum: a graphite panel, an
                                          * aluminium sleeve): orange, silver, a pale orange */
     RGB(12, 12, 13), RGB(232, 232, 229), RGB(255, 255, 255), RGB(150, 150, 147), RGB(100, 100, 98),
     RGB(56, 56, 58), RGB(44, 44, 46), RGB(34, 34, 36), RGB(46, 46, 48),
     RGB(255, 92, 22), RGB(196, 196, 192), RGB(255, 168, 112), RGB(255, 92, 22)},
};
static const theme_t *th = &THEMES[0];
#define BG (th->bg)
#define TXT (th->txt)
#define HI (th->hi)
#define MUTED (th->muted)
#define DIM (th->dim)
#define TRACK (th->track)
#define OFF (th->off)
#define RULE (th->rule)
#define SEL (th->sel)
#define AC1 (th->a1)                     /* (Mutable's teal) */
#define AC2 (th->a2)                     /* (orange) */
#define AC3 (th->a3)                     /* (pink) */
#define KNOCK (th->knock)

/* ------------------------------------------------------------------ state --- */
static int data_ok;                      /* the resource region holds this build's tables */
static int halted;                       /* app_panic: the engine stopped until the next frame */
static int engine_ok;
static int cpu2_ok;
static int save_hold, dirty;
static project_t scratch PLAT_POOL;      /* a project read or made before it replaces the live one (and the next
                                          * pattern, read before the playing one ends: pat_unstage first); (0.16.1)
                                          * in the pool, out of the RAM */
static int proj_stored;                  /* project_load: the saved project read (else a demo) */
static void pattern_boot(void);          /* (the patterns: further down) */
static void pattern_forget(void);
/* the patterns (0.15): their store, the one playing, the order (their section further down) */
#include "pat_store.h"
static int pat_cur;                      /* the pattern playing (its slot) */
static uint8_t chain[64], chain_new[64]; /* the order playing; the one SEQ is making */
static int chain_n, chain_new_n, chain_pos = -1;   /* chain_pos: its entry playing, -1 not yet */
static int chain_staged = -1, chain_again;          /* the entry next (cued in scratch, or the same again), -1 none */
static uint32_t chain_cycles;            /* eng_cycles seen */
static int pat_bank;                     /* the bank its screen shows */
static int pat_open;                     /* SEQ held: its screen */
static int pat_used;                     /* a key pressed while SEQ held (its tap not SEQ's page then) */
/* the presets (0.15.1): a track's sound each, A the factory's (engine.cc), B to K saved (pat_store.h's keys from
 * PRE_KEY on); their section further down */
static int pre_open;                     /* EDIT held: their screen */
static int pre_used;                     /* a key or a knob used while EDIT held (its tap not EDIT's page then) */
static int pre_bank;                     /* the bank shown: 0 the factory's (A), 1..10 B..K */
static int pre_cur[ENG_TRACKS] = {-1, -1, -1, -1};   /* each track's sound read or saved last (bank x 16 + its key) */
static int pre_key = -1, pre_track = -1; /* the white key held: its sound heard on that track */
static eng_sound_t pre_snd, pre_tmp;

static void pat_name(int slot, char *b)  /* A1 .. K16 */
{
    int n = slot % PAT_PER_BANK + 1;
    b[0] = (char)('A' + slot / PAT_PER_BANK);
    if (n >= 10)
        b[1] = '1', b[2] = (char)('0' + n - 10), b[3] = 0;
    else
        b[1] = (char)('0' + n), b[2] = 0;
}


/* the screen's own pages, after the engine's, four of each (a track's): the RANDOM page (its steps' second face); the
 * sound's second (GATE, TRANSP, OCTAVE, the filter's KEY); the filter's two (TYPE, CUTOFF, RESO, ENV; DRIVE, NOISE,
 * COLOR, FM); the envelopes' second (P.ENV, the filter's ATTACK, DECAY, PING); then the project and the machine (GLO's
 * second face) */
#define VP_RND(t) (ENG_NPAGES + (t))
#define VP_SON2(t) (ENG_NPAGES + 4 + (t))
#define VP_FLT1(t) (ENG_NPAGES + 8 + (t))
#define VP_FLT2(t) (ENG_NPAGES + 12 + (t))
#define VP_ENV2(t) (ENG_NPAGES + 16 + (t))
#define VP_SYS (ENG_NPAGES + 20)
#define VP_PLAY(l) (ENG_NPAGES + 21 + (l))   /* (0.16) a lane's PLAY face: a track's ratchets (0.16.6: its steps'
                                          * second face, RATCH, SPAN, CURVE, then DIRECTION; SPEED on the first, in
                                          * RATCH's place), an effect's (its last) SPEED, DIRECTION, PROB, LENGTH */
#define VP_ARP2(t) (ENG_NPAGES + 28 + (t))   /* (0.16.2) Endless' second face: STRIDE, REST, HOLES, LEAP */
#define VP_CLIMB(t) (ENG_NPAGES + 32 + (t))  /* (0.16.7) a track's steps' third face: CLIMB, NOTES, ON, SHAPE */
#define VP_ARP3(t) (ENG_NPAGES + 36 + (t))   /* (0.16.13) Endless' third face, ARP: MODE, OCTAVES, LATCH, ORDER */
#define VP_SPREAD (ENG_NPAGES + 40)      /* (0.16.19) the mixer's third face, SPREAD: a knob a track (the Euclidean
                                          * pages' fourth place, PG_SPREAD_PARAM) */
enum { PT_RND = NPT, PT_SON2, PT_VFLT1, PT_VFLT2, PT_ENV2, PT_SYS, PT_PLAY, PT_ARP2, PT_CLIMB, PT_ARP3, PT_SPREAD };
/* a track's pages in four sets: EDIT goes from one to the next of the first three (its sound, its bending: the bits
 * and the glitch, its filter),
 * ENV to the envelopes; SELECT through a set's pages; ALGO changes what the page shows (the sound's model, the
 * filter's), where there is a choice */
enum { CAT_NONE, CAT_SOUND, CAT_BEND, CAT_FILTER, CAT_ENV };
static const uint8_t RND_TP[4] = {TP_PAN, TP_DECAY, TP_NOTE, TP_RATCHET};   /* the RANDOM page's knobs */
static const char *const RND_NAME[4] = {"Pan", "Decay", "Tune", "Ratch"};

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
    int ekeys[8], ekn;                   /* (0.16.15) the keys held on Endless, the newest last (its transposition) */
    int cview, cstep, clane;             /* (0.16.16) a step's notes shown (a step held and REC): the step, its lane */
    int erec;                            /* the track Endless records (-1: none) */
    int used[NB];                        /* a knob turned while that button was held: no tap when it is let go */
    int mod;                             /* MOD mode */
    int msel;                            /* MATRIX: the cable chosen (0..7 the track's, 8..15 the global ones) */
    int init_sel;                        /* GLO: the project REC held loads (0 new, 1 the demo, 2 the saved one, 3 the
                                          * stress test, 4 the stress test on the FM drums) */
    int octave;                          /* the keyboard's */
    int dice;                            /* OCT- held (or both), SELECT turned: the random dose, on its own screen */
    uint32_t dice_at;                    /* (its screen came at) */
    int trauma;                          /* OCT+ held, SELECT turned: TRAUMA, on its own screen */
    uint32_t trauma_at;
    int combo;                           /* REC and SEL held together: every cable erased (REC's hold) */
    int lrec, lr_combo, noclick;         /* (0.16.31) live recording (REC and PLAY); REC's release taken; no metronome */
    int lr_s, lr_t;                      /* the step it wrote last, its track, the lane's place then (keys within a */
    float lr_last;                       /* step: a chord) */
    float lr_at[NKEYS];                  /* each key held: the lane's place at its press, its step, its track + 1 */
    int8_t lr_step[NKEYS], lr_trk[NKEYS];
    int oct_was, spage_was;              /* the octave, the steps' page before an OCT press (both: put back) */
    uint32_t play_at;                    /* PLAY's last press (again within CUT_MS: every sound cut) */
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
    int32_t head_shown[6], foot_shown[6], panel_shown[3];
} ui;

/* ------------------------------------------------------------ the pages --- */
static int ptype(int page)
{
    return page >= VP_SPREAD ? PT_SPREAD : page >= VP_ARP3(0) ? PT_ARP3 : page >= VP_CLIMB(0) ? PT_CLIMB : page >= VP_ARP2(0) ? PT_ARP2 : page >= VP_PLAY(0) ? PT_PLAY : page >= VP_SYS ? PT_SYS : page >= ENG_NPAGES ? PT_RND + (page - ENG_NPAGES) / 4
                                                                                        : eng_page_type(page);
}
static int ptrack(int page)              /* the track a page belongs to, -1: none */
{
    if (page >= VP_SPREAD)
        return -1;
    if (page >= VP_ARP3(0))
        return page - VP_ARP3(0);
    if (page >= VP_CLIMB(0))
        return page - VP_CLIMB(0);
    if (page >= VP_ARP2(0))
        return page - VP_ARP2(0);
    if (page >= VP_PLAY(0))
        return page - VP_PLAY(0) < ENG_TRACKS ? page - VP_PLAY(0) : -1;
    return page >= VP_SYS ? -1 : page >= ENG_NPAGES ? (page - ENG_NPAGES) % 4 : eng_page_track(page);
}
static int plane(int page)               /* a lane's page (its steps', its effect's), the lane: -1 none */
{
    if (page >= VP_SPREAD)
        return -1;
    if (page >= VP_ARP3(0))
        return -1;
    if (page >= VP_CLIMB(0))
        return page - VP_CLIMB(0);
    if (page >= VP_ARP2(0))
        return -1;
    if (page >= VP_PLAY(0))
        return page - VP_PLAY(0);
    return page < ENG_NPAGES ? eng_page_lane(page) : -1;
}

static int drum_track(int t)             /* a model with its own eight knobs on it (a drum, an FM drum, ZERO) */
{
    return t >= 0 && t < ENG_TRACKS && eng_model_own(eng_project()->algo[PG_TRACK(t, 0)]);
}
static int track_kind(int t) { return t >= 0 && t < ENG_TRACKS ? eng_model_kind(eng_project()->algo[PG_TRACK(t, 0)]) : -1; }
static int track_cat(int page)           /* the set a track's page is in */
{
    switch (ptype(page)) {
    case PT_PLAITS1: case PT_PLAITS3: case PT_SON2: return CAT_SOUND;
    case PT_PLAITS2: return drum_track(ptrack(page)) ? CAT_SOUND : CAT_ENV;   /* (an FM drum's knobs 4..7) */
    case PT_BEND: case PT_GLITCH: return CAT_BEND;
    case PT_VFLT1: case PT_VFLT2: case PT_FILT1: case PT_FILT2: case PT_FILT3: return CAT_FILTER;
    case PT_ENV2: return CAT_ENV;
    default: return CAT_NONE;
    }
}
static int cat_pages(int cat, int t, int *f)   /* a set's pages on track t, in order; how many */
{
    switch (cat) {
    case CAT_SOUND:
        f[0] = PG_TRACK(t, 0);
        if (drum_track(t)) {
            f[1] = PG_TRACK(t, 1);
            f[2] = VP_SON2(t);
            return 3;
        }
        f[1] = VP_SON2(t);
        return 2;
    case CAT_BEND: f[0] = PG_BEND(t); f[1] = PG_GLITCH(t); return 2;
    case CAT_FILTER: f[0] = VP_FLT1(t); f[1] = VP_FLT2(t); return 2;
    default:
        if (drum_track(t)) {             /* (an FM drum: its own envelopes; the filter's) */
            f[0] = VP_ENV2(t);
            return 1;
        }
        f[0] = PG_TRACK(t, 1);
        f[1] = VP_ENV2(t);
        return 2;
    }
}
static int shown_page(int page)          /* the page the screen shows for one it keeps out of sight (the engine's
                                          * Plaits pitch page, its filter's pages: their knobs are elsewhere) */
{
    int t = ptrack(page);
    switch (ptype(page)) {
    case PT_PLAITS3: return VP_SON2(t);
    case PT_FILT1: case PT_FILT2: case PT_FILT3: return VP_FLT1(t);
    default: return page;
    }
}

static int lfo_of(int page)              /* (0.16.23) the LFO an LFO's page (or its follower's) belongs to: 0..2 */
{
    return clamp(page >= PG_FOLLOW(0) && page < PG_FOLLOW(3) ? page - PG_FOLLOW(0) : page - PG_LFO(0), 0, 2);
}
static int lfo_follows(int i)            /* its WAVE on FOLLOW */
{
    return (int)(eng_project()->val[PG_LFO(i) * 4] + 0.5f) == LW_FOLLOW;
}

#define MAXFACES 7
static int module_pages(int page, int *f)   /* its module's pages (its faces), in order; how many */
{
    int t = ptrack(page);
    switch (ptype(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: case PT_BEND: case PT_GLITCH: case PT_FILT1: case PT_FILT2: case PT_FILT3:
    case PT_SON2: case PT_VFLT1: case PT_VFLT2: case PT_ENV2:
        return cat_pages(track_cat(page), t, f);
    case PT_LFO: case PT_FOLLOW: {       /* the three LFOs; (0.16.23) a follower's input after its LFO */
        int n = 0;
        for (int i = 0; i < 3; i++) {
            f[n++] = PG_LFO(i);
            if (lfo_follows(i))
                f[n++] = PG_FOLLOW(i);
        }
        return n;
    }
    case PT_MIX: case PT_PAN: case PT_SPREAD: case PT_EQ: case PT_DUCK: case PT_COMP: case PT_OTT:   /* the mixer:
                                          * the levels, the pans, the spreads (0.16.19), the filters, the duck, the
                                          * master bus (its two modes) */
        f[0] = PG_MIX, f[1] = PG_PAN, f[2] = VP_SPREAD, f[3] = PG_EQ, f[4] = PG_DUCK, f[5] = PG_COMP, f[6] = PG_OTT;
        return 7;
    case PT_STEP: case PT_RND: case PT_EUC: case PT_PLAY: case PT_CLIMB:
        if (t < 0) {                     /* an effect's Euclidean or PLAY face: its module's */
            int l = plane(page);
            return module_pages(l == LANE_DELAY ? PG_DELAY(0) : l == LANE_REVERB ? PG_REVERB : PG_WARPS(0), f);
        }
        f[0] = PG_STEP(t);               /* (0.16.6: the ratchets second, by the steps; 0.16.7: CLIMB third) */
        f[1] = VP_PLAY(t);
        f[2] = VP_CLIMB(t);
        f[3] = VP_RND(t);
        f[4] = PG_EUC(t);
        return 5;
    case PT_ARP: case PT_ARP2: case PT_ARP3: f[0] = PG_ARP(t); f[1] = VP_ARP2(t); f[2] = VP_ARP3(t); return 3;
    case PT_DELAY1: case PT_DELAY2: case PT_DSEND: case PT_LANE: case PT_REVERB: case PT_RSEND:
    case PT_WARPS1: case PT_WARPS2: {    /* an effect: its knobs, the tracks' sends, its steps */
        int l = eng_page_lane(page);
        if (l == LANE_DELAY) {
            f[0] = PG_DELAY(0), f[1] = PG_DELAY(1), f[2] = PG_DSEND, f[3] = PG_STEP(LANE_DELAY);
            f[4] = PG_EUC(LANE_DELAY), f[5] = VP_PLAY(LANE_DELAY);
            return 6;
        }
        if (l == LANE_REVERB) {
            f[0] = PG_REVERB, f[1] = PG_RSEND, f[2] = PG_STEP(LANE_REVERB), f[3] = PG_EUC(LANE_REVERB);
            f[4] = VP_PLAY(LANE_REVERB);
            return 5;
        }
        f[0] = PG_WARPS(0), f[1] = PG_WARPS(1), f[2] = PG_STEP(LANE_WARPS), f[3] = PG_EUC(LANE_WARPS);
        f[4] = VP_PLAY(LANE_WARPS);
        return 5;
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
/* the page whose ALGORITHM the encoder turns from this one (-1: none): the sound's model on its pages, the filter's on
 * its, a module's own on its first page (the delay's mode, the bus's); not the bending, not the envelopes */
static int algo_page(int page)
{
    int c = track_cat(page);
    if (c == CAT_SOUND)
        return PG_TRACK(ptrack(page), 0);
    if (c == CAT_FILTER)
        return PG_FILT(ptrack(page), 0);
    if (c != CAT_NONE || page >= ENG_NPAGES)
        return -1;
    return eng_algo_count(page_first(page)) ? page_first(page) : -1;
}
static int algo_here(int page)           /* ALGO does something here: the header says so */
{
    int pt = ptype(page);
    return algo_page(page) >= 0 || pt == PT_WARPS1 || pt == PT_WARPS2 || pt == PT_DELAY1 || pt == PT_DELAY2 ||
           pt == PT_DSEND || pt == PT_COMP || pt == PT_OTT;
}

static int page_at(int page, int face)   /* its module's page at face (wrapping) */
{
    int f[MAXFACES], n = module_pages(page, f);
    return f[((face % n) + n) % n];
}
static int track_page(int page, int t)   /* the same page on track t (a track's sets: the same set, the same place in
                                          * it, or its last) */
{
    int c = track_cat(page);
    if (c != CAT_NONE) {
        int f[MAXFACES], n = cat_pages(c, t, f), i = page_face(page);
        return f[i < n ? i : n - 1];
    }
    switch (ptype(page)) {
    case PT_EUC: return ptrack(page) >= 0 ? PG_EUC(t) : page;
    case PT_STEP: return PG_STEP(t);
    case PT_ARP: return PG_ARP(t);
    case PT_ARP2: return VP_ARP2(t);
    case PT_ARP3: return VP_ARP3(t);
    case PT_CLIMB: return VP_CLIMB(t);
    case PT_RND: return VP_RND(t);
    case PT_PLAY: return ptrack(page) >= 0 ? VP_PLAY(t) : page;
    default: return page;
    }
}

/* the lane a page's knobs belong to (its steps lock them), -1: the tracks' (the mixer, the sends) or none */
static int page_lane(int page)
{
    int t = ptrack(page);
    if (t >= 0)
        return t;
    if (ptype(page) == PT_PLAY)
        return plane(page);
    if (page >= ENG_NPAGES || page == PG_DSEND || page == PG_RSEND)
        return -1;
    int l = eng_page_lane(page);
    return l >= ENG_TRACKS ? l : -1;
}

/* the parameter a knob turns: its page's, the RANDOM page's are its track's PAN, DECAY, NOTE and RATCHET (their
 * randoms) */
static int knob_param(int page, int k)
{
    int t = ptrack(page);
    switch (ptype(page)) {
    case PT_SON2: {                      /* GATE, TRANSP, OCTAVE, the filter's KEY (an FM drum: its eighth knob for
                                          * GATE: it has none) */
        static const uint8_t TP[4] = {TP_LENGTH, TP_TRANSPOSE, TP_OCTAVE, TP_KEY};
        return k == 0 && drum_track(t) ? PG_TRACK(t, 2) * 4 : eng_track_param(t, TP[k]);
    }
    case PT_VFLT1: {
        static const uint8_t TP[4] = {TP_FTYPE, TP_CUTOFF, TP_RESO, TP_FENV};
        return eng_track_param(t, TP[k]);
    }
    case PT_VFLT2: {
        static const uint8_t TP[4] = {TP_FDRIVE, TP_NOISE, TP_COLOR, TP_FM};
        return eng_track_param(t, TP[k]);
    }
    case PT_ENV2: {                      /* the pitch envelope (an FM drum: the filter's ENV again), the filter's */
        static const uint8_t TP[4] = {TP_PITCHENV, TP_FATTACK, TP_FDECAY, TP_PING};
        return eng_track_param(t, k == 0 && drum_track(t) ? TP_FENV : TP[k]);
    }
    default: break;
    }
    if (ptype(page) == PT_RND)
        return eng_track_param(ptrack(page), RND_TP[k]);
    if (ptype(page) == PT_ARP2 || ptype(page) == PT_ARP3)   /* (Endless' options, its ARP's: the project's bytes,
                                          * byte_knob; its page's knobs stand for them here) */
        return PG_ARP(ptrack(page)) * 4 + k;
    if (ptype(page) == PT_SPREAD)        /* (0.16.19) SPREAD: each track's, in its Euclidean page's fourth place */
        return PG_SPREAD_PARAM(k);
    if (ptype(page) == PT_CLIMB)         /* (0.16.7) CLIMB, its parameter (in the Euclidean page's third place); the
                                          * others the project's bytes (byte_knob): it stands for them */
        return PG_EUC(ptrack(page)) * 4 + 2;
    if (ptype(page) == PT_PLAY) {        /* (SPEED and DIRECTION are the project's bytes, byte_knob: a parameter
                                          * stands for them here); a track's RATCH and SPAN: RATCH's parameter
                                          * (ratch_field), then CURVE, DIRECTION */
        int l = plane(page);
        if (l < ENG_TRACKS)
            return k == 2 ? PG_CURVE * 4 + l : PG_STEP(l) * 4 + 2;
        return k == 2 ? PG_STEP(l) * 4 + 1 : PG_STEP(l) * 4 + 3;
    }
    if (ptype(page) == PT_EUC && k >= 2)  /* the Euclidean face: then the lane's LENGTH and PROB */
        return PG_STEP(eng_page_lane(page)) * 4 + (k == 2 ? 3 : 1);
    return page * 4 + k;
}

static const char *knob_name(int page, int k, int param)   /* its name: the parameter's, but where the page mixes
                                                             * the sound's with the filter's, the filter's say so */
{
    int t = ptrack(page);
    switch (ptype(page)) {
    case PT_RND: return RND_NAME[k];
    case PT_SON2: return k == 3 ? "F.Key" : eng_param_name(param);
    case PT_ENV2: {
        static const char *const N[4] = {"P.Env", "F.Atk", "F.Decay", "Ping"};
        return k == 0 && drum_track(t) ? "F.Env" : N[k];
    }
    default: return eng_param_name(param);
    }
}

static const char *module_name(int page)
{
    switch (ptype(page)) {
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: case PT_BEND: case PT_GLITCH: case PT_FILT1: case PT_FILT2: case PT_FILT3:
    case PT_SON2: case PT_VFLT1: case PT_VFLT2: case PT_ENV2: {
        int c = track_cat(page);
        return c == CAT_BEND ? "Bend" : c == CAT_FILTER ? "Filter" : c == CAT_ENV ? "Envelope"
                                                                        : track_kind(ptrack(page)) == MK_ZERO ? "Zero"
                                                                        : track_kind(ptrack(page)) == MK_DRUM ? "Drum"
                                                                        : track_kind(ptrack(page)) == MK_LXR ? "FM drum"
                                                                        : track_kind(ptrack(page)) == MK_FM ? "FM"
                                                                        : track_kind(ptrack(page)) == MK_PADS ? "Pads"
                                                                        : track_kind(ptrack(page)) == MK_POLY ? "Poly"
                                                                        : track_kind(ptrack(page)) == MK_PROPHET ? "Rev3"
                                                                        : track_kind(ptrack(page)) == MK_SQ1 ? "Otis"
                                                                        : track_kind(ptrack(page)) == MK_TAURUS ? "Taurus"
                                                                        : track_kind(ptrack(page)) == MK_VOCODER ? "Vocoder"
                                                                        : track_kind(ptrack(page)) == MK_STATIC ? "Static"
                                                                        : "Plaits";
    }
    case PT_LFO: case PT_FOLLOW: {
        static const char *const N[3] = {"LFO 1", "LFO 2", "LFO 3"}, *const I[3] = {"LFO 1 In", "LFO 2 In", "LFO 3 In"};
        return ptype(page) == PT_FOLLOW ? I[lfo_of(page)] : N[lfo_of(page)];
    }
    case PT_MIX: case PT_PAN: case PT_SPREAD: case PT_EQ: case PT_DUCK: case PT_COMP: case PT_OTT: return "Mixer";
    case PT_STEP: case PT_RND: return "Steps";
    case PT_EUC: {
        int l = eng_page_lane(page);
        return l < ENG_TRACKS ? "Steps" : l == LANE_DELAY ? "Delay" : l == LANE_REVERB ? "Reverb" : "Warps";
    }
    case PT_ARP: case PT_ARP2: case PT_ARP3: return "Endless";
    case PT_CLIMB: return "Steps";
    case PT_PLAY: {
        int l = plane(page);
        return l < ENG_TRACKS ? "Steps" : l == LANE_DELAY ? "Delay" : l == LANE_REVERB ? "Reverb" : "Warps";
    }
    case PT_GLOBAL: case PT_SYS: return "Global";
    default: {
        int l = eng_page_lane(page);
        return l == LANE_DELAY ? "Delay" : l == LANE_REVERB ? "Reverb" : "Warps";
    }
    }
}

static const char *const LANE_SHORT[ENG_LANES] = {"T1", "T2", "T3", "T4", "DL", "RV", "WP"};

/* the colours of a module's state, as its icons print them (in Mutable's: teal, orange, pink) */
static uint16_t accent3(int i) { return i <= 0 ? AC1 : i == 1 ? AC2 : AC3; }

static uint16_t model_accent(int m)      /* by bank: orange MIFM1's voices (ZERO, the FM, OTIS...), teal Plaits' speech,
                                          * pink the drums, the noises and the physical models */
{
    int b = eng_model_bank(m);
    return b == 2 ? AC2 : b == 3 ? AC1 : AC3;
}
static uint16_t track_accent(int t) { return model_accent(eng_project()->algo[PG_TRACK(t, 0)]); }

static uint16_t warps_accent(void)      /* Warps' algorithm: the blends teal, the ring modulators and the logic
                                          * orange, the frequency shifter pink; off, grey */
{
    int a = eng_warps_algo();
    return a < 0 ? MUTED : a <= 1 ? AC1 : a <= 5 ? AC2 : AC3;
}

static uint16_t lane_accent(int l)      /* a lane's colour: its track's model's, or its effect's */
{
    return l < ENG_TRACKS ? track_accent(l) : l == LANE_DELAY ? AC2 : l == LANE_REVERB ? AC3 : warps_accent();
}

static uint16_t page_accent(int page)
{
    project_t *p = eng_project();
    switch (ptype(page)) {
    case PT_STEP: case PT_RND: case PT_ARP: case PT_ARP2: case PT_ARP3: case PT_CLIMB: return track_accent(ptrack(page));
    case PT_PAN: case PT_SPREAD: case PT_GLOBAL: case PT_SYS: case PT_MIX: case PT_EQ: return AC1;
    case PT_DUCK: return track_accent(0);    /* T1's: it ducks the others */
    case PT_COMP: return AC2;
    case PT_OTT: return AC3;
    case PT_PLAITS1: case PT_PLAITS2: case PT_PLAITS3: case PT_BEND: case PT_GLITCH: case PT_FILT1: case PT_FILT2: case PT_FILT3:
    case PT_SON2: case PT_VFLT1: case PT_VFLT2: case PT_ENV2:
        return model_accent(p->algo[PG_TRACK(ptrack(page), 0)]);
    case PT_EUC: return lane_accent(eng_page_lane(page));
    case PT_PLAY: return lane_accent(plane(page));
    case PT_LFO: case PT_FOLLOW: return accent3(lfo_of(page));   /* LFO 1, 2, 3: the three colours */
    default: return lane_accent(eng_page_lane(page));
    }
}
static uint16_t mod_colour(uint16_t ac) { return ac == AC1 ? AC3 : AC1; }   /* where the cables take a knob */


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
    } else if (page < PG_MIX || ptype(page) == PT_FOLLOW) {
        n[0] = 'L';
        n[1] = (char)('1' + lfo_of(page));
        n[2] = 0;
        text_cat(b, n, max);
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
static void project_load(void)            /* the saved project (v0.1's read into this version's), else the demo
                                          * groove */
{
    project_t *p = eng_project();
    int got = plat_store_load(OBJ_PROJECT, &scratch, sizeof scratch);
    proj_stored = got > 0 && eng_upgrade(&scratch, got);
    if (proj_stored)
        *p = scratch;
    else
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
        eng_defaults(p);                 /* (the host simulator: 0.1's demo, which its screens' script was written on) */
#else
        eng_groove(p);
#ifdef PLAT_WEB
        p->theme = THEME_GRAPHITE;       /* (the page's machine: greys, its light orange) */
#endif
#endif
    eng_fresh();                         /* (the last project's trigs' locks and randoms gone) */
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
    pattern_forget();
    project_load();
    ui.full = 1;
}

/* ---------------------------------------------------------------- audio --- */
static float eng_l[APP_BLOCK], eng_r[APP_BLOCK];
static int usb_live;                     /* the engine rendered this block: its USB channels are this block's */

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float g = (float)master_q12 * (8300000.0f / 4096.0f);
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    usb_live = 0;
    if (halted || !data_ok || !engine_ok) {
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    eng_render(eng_l, eng_r, n);
    usb_live = 1;
    for (i = 0; i < n; i++) {
        lr[2 * i] = (int32_t)(fm_clampf(eng_l[i], -1.0f, 1.0f) * g);
        lr[2 * i + 1] = (int32_t)(fm_clampf(eng_r[i], -1.0f, 1.0f) * g);
    }
}

/* the USB audio input (the audio interrupt, after app_render, while the host records): 10 channels, T1..T4 and the
 * effects' return, stereo each */
const int16_t *app_uac_frames(uint32_t n)
{
    return eng_usb_frames(n, usb_live);
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
    pattern_boot();
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

static void hoop(float cx, float cy, float r, float w, uint16_t c)   /* r the outer edge, w its width */
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

/* the icons, as strokes in a 32-unit box: the models (as ALGORITHM orders them), the FM's eight algorithms, the LFOs' waves, the mixer, the reverb, the global page,
 * the projects, the steps, Warps' algorithms, the delay. L a line, C a circle, D a dot, R a rectangle (x y w h) */
typedef struct {
    char op;
    int8_t a, b, c, d;
} vop_t;
enum { IC_MODEL = 0, IC_ALG = ENG_MODELS, IC_WAVE = IC_ALG + 8, IC_MIX = IC_WAVE + LW_N, IC_REV, IC_GLO, IC_NEW, IC_DEMO, IC_SAVED, IC_STRESS,
       IC_STEPS, IC_DICE,
       IC_ENDLESS, IC_WARPS, IC_DELAY = IC_WARPS + 7, IC_DUCK, IC_COMP, IC_OTT, IC_EQ, IC_BEND, IC_EUC, IC_SPREAD, NICON };
#define IOPS 10
static const vop_t ICONS[NICON][IOPS] = {
    {{'C', 16, 16, 9, 0}, {'D', 16, 16, 3, 0}},                                                     /* KICK */
    {{'R', 6, 11, 20, 12}, {'L', 6, 15, 26, 15}, {'L', 9, 4, 15, 9}, {'L', 23, 4, 17, 9}},          /* SNARE */
    {{'L', 4, 26, 7, 8}, {'L', 7, 8, 9, 26}, {'L', 9, 26, 12, 10}, {'L', 12, 10, 14, 26}, {'L', 14, 26, 17, 6},
     {'L', 17, 6, 28, 26}},                                                                         /* CLAP: its bursts */
    {{'L', 5, 13, 16, 8}, {'L', 16, 8, 27, 13}, {'L', 5, 16, 16, 20}, {'L', 16, 20, 27, 16}, {'L', 16, 20, 16, 28}}, /* HAT */
    {{'R', 7, 13, 18, 12}, {'L', 7, 17, 25, 17}, {'L', 16, 3, 16, 9}, {'L', 13, 6, 16, 9}, {'L', 19, 6, 16, 9}}, /* TOM */
    {{'L', 11, 7, 21, 7}, {'L', 21, 7, 25, 25}, {'L', 25, 25, 7, 25}, {'L', 7, 25, 11, 7}, {'D', 16, 21, 2, 0}}, /* PERC */
    {{'C', 16, 16, 11, 0}, {'L', 9, 17, 12, 12}, {'L', 12, 12, 16, 20}, {'L', 16, 20, 20, 12},
     {'L', 20, 12, 23, 17}},                                                                        /* FM drum */
    {{'R', 6, 13, 20, 11}, {'L', 6, 17, 26, 17}, {'L', 7, 9, 11, 5}, {'L', 11, 5, 15, 9}, {'L', 15, 9, 19, 5},
     {'L', 19, 5, 23, 9}},                                                                          /* FM snare */
    {{'L', 3, 13, 16, 8}, {'L', 16, 8, 29, 13}, {'L', 3, 13, 29, 13}, {'L', 16, 13, 16, 28},
     {'L', 10, 28, 22, 28}},                                                                        /* FM cymbal */
    {{'L', 5, 11, 27, 11}, {'L', 5, 15, 27, 15}, {'D', 16, 7, 2, 0}, {'L', 16, 15, 16, 28},
     {'L', 10, 22, 22, 22}},                                                                        /* FM hi-hat */
    {{'C', 16, 16, 11, 0}, {'L', 6, 16, 9, 11}, {'L', 9, 11, 12, 11}, {'L', 12, 11, 16, 16}, {'L', 16, 16, 20, 21},
     {'L', 20, 21, 23, 21}, {'L', 23, 21, 26, 16}},                                                 /* ZERO: a sine
                                                                                                     * through zero */
    {{'R', 12, 6, 8, 8}, {'R', 12, 22, 8, 8}, {'L', 16, 14, 16, 22}, {'L', 20, 10, 25, 10}, {'L', 25, 10, 25, 2},
     {'L', 25, 2, 16, 2}, {'L', 16, 2, 16, 6}},                                                     /* FM: an
                                                                                                     * operator on
                                                                                                     * another, its
                                                                                                     * feedback */
    {{'L', 3, 10, 29, 10}, {'L', 3, 16, 29, 16}, {'L', 3, 22, 29, 22}, {'D', 11, 22, 3, 0}, {'D', 11, 16, 3, 0},
     {'D', 11, 10, 3, 0}, {'D', 22, 19, 3, 0}, {'D', 22, 13, 3, 0}, {'D', 22, 7, 3, 0}},             /* PADS: two
                                                                                                     * chords on a
                                                                                                     * staff */
    {{'L', 4, 17, 12, 7}, {'L', 12, 7, 12, 17}, {'L', 12, 17, 20, 7}, {'L', 20, 7, 20, 17}, {'L', 20, 17, 28, 7},
     {'L', 4, 22, 12, 22}, {'L', 12, 22, 12, 27}, {'L', 12, 27, 20, 27}, {'L', 20, 27, 20, 22},
     {'L', 20, 22, 28, 22}},                                                                        /* POLY: a saw
                                                                                                     * over its sub */
    {{'L', 4, 15, 12, 5}, {'L', 12, 5, 12, 15}, {'L', 12, 15, 20, 5}, {'L', 20, 5, 20, 15}, {'L', 20, 15, 28, 5},
     {'L', 4, 27, 10, 17}, {'L', 10, 17, 10, 27}, {'L', 10, 27, 18, 17}, {'L', 18, 17, 18, 27},
     {'L', 18, 27, 26, 17}},                                                                        /* PROPHET: two
                                                                                                     * saws apart */
    {{'L', 2, 27, 6, 21}, {'L', 6, 21, 10, 27}, {'L', 11, 19, 16, 13}, {'L', 16, 13, 16, 19}, {'L', 16, 19, 20, 14},
     {'L', 21, 11, 21, 4}, {'L', 21, 4, 24, 4}, {'L', 24, 4, 24, 11}, {'L', 24, 11, 29, 11}},          /* OTIS: a
                                                                                                     * Transwave's
                                                                                                     * cycles, one
                                                                                                     * after another */
    {{'C', 16, 19, 7, 0}, {'L', 10, 15, 6, 11}, {'L', 6, 11, 5, 5}, {'L', 22, 15, 26, 11}, {'L', 26, 11, 27, 5},
     {'D', 13, 22, 1, 0}, {'D', 19, 22, 1, 0}},                                                     /* TAURUS: the
                                                                                                     * bull */
    {{'R', 12, 4, 8, 13}, {'L', 9, 13, 9, 16}, {'L', 9, 16, 12, 20}, {'L', 12, 20, 20, 20}, {'L', 20, 20, 23, 16},
     {'L', 23, 16, 23, 13}, {'L', 16, 20, 16, 25}, {'L', 4, 28, 8, 25}, {'L', 8, 25, 12, 28}, {'L', 20, 28, 28, 28}},
                                                                                                     /* VOCODER: a
                                                                                                     * microphone, a
                                                                                                     * wave, a line */
    {{'R', 4, 7, 24, 18}, {'D', 8, 11, 1, 0}, {'D', 14, 14, 1, 0}, {'D', 21, 10, 1, 0}, {'D', 11, 19, 1, 0},
     {'D', 18, 20, 1, 0}, {'D', 24, 16, 1, 0}, {'L', 10, 28, 22, 28}, {'L', 16, 25, 16, 28}},       /* STATIC */
    {{'R', 5, 6, 22, 14}, {'L', 10, 20, 8, 27}, {'L', 8, 27, 16, 20}},                              /* SPEECH */
    {{'C', 10, 18, 5, 0}, {'C', 17, 13, 6, 0}, {'C', 23, 19, 5, 0}},                                /* SWARM */
    {{'L', 4, 18, 7, 10}, {'L', 7, 10, 10, 23}, {'L', 10, 23, 13, 8}, {'L', 13, 8, 16, 21}, {'L', 16, 21, 19, 11},
     {'L', 19, 11, 22, 24}, {'L', 22, 24, 25, 12}, {'L', 25, 12, 28, 17}},                          /* NOISE */
    {{'D', 8, 9, 2, 0}, {'D', 20, 7, 2, 0}, {'D', 14, 15, 2, 0}, {'D', 25, 17, 2, 0}, {'D', 9, 23, 2, 0},
     {'D', 19, 25, 2, 0}},                                                                          /* PARTICLES */
    {{'L', 8, 5, 8, 27}, {'L', 16, 5, 20, 16}, {'L', 20, 16, 16, 27}, {'L', 24, 5, 24, 27}},        /* STRING */
    {{'C', 16, 16, 11, 0}, {'C', 16, 16, 6, 0}, {'D', 16, 16, 2, 0}},                               /* MODAL */
    {{'R', 13, 1, 6, 6}, {'R', 13, 9, 6, 6}, {'R', 13, 17, 6, 6}, {'R', 13, 25, 6, 6}, {'L', 16, 7, 16, 9},
     {'L', 16, 15, 16, 17}, {'L', 16, 23, 16, 25}},                                                 /* the FM's
                                                                                                     * algorithms (its
                                                                                                     * hero's): 1 */
    {{'R', 7, 1, 6, 6}, {'R', 19, 1, 6, 6}, {'R', 13, 13, 6, 6}, {'R', 13, 25, 6, 6}, {'L', 10, 7, 15, 13},
     {'L', 22, 7, 17, 13}, {'L', 16, 19, 16, 25}},                                                  /* 2 */
    {{'R', 19, 1, 6, 6}, {'R', 19, 13, 6, 6}, {'R', 7, 13, 6, 6}, {'R', 13, 25, 6, 6}, {'L', 22, 7, 22, 13},
     {'L', 22, 19, 17, 25}, {'L', 10, 19, 15, 25}},                                                 /* 3 */
    {{'R', 7, 1, 6, 6}, {'R', 7, 13, 6, 6}, {'R', 19, 13, 6, 6}, {'R', 13, 25, 6, 6}, {'L', 10, 7, 10, 13},
     {'L', 10, 19, 15, 25}, {'L', 22, 19, 17, 25}},                                                 /* 4 */
    {{'R', 7, 5, 6, 6}, {'R', 7, 21, 6, 6}, {'R', 19, 5, 6, 6}, {'R', 19, 21, 6, 6}, {'L', 10, 11, 10, 21},
     {'L', 22, 11, 22, 21}},                                                                        /* 5 */
    {{'R', 13, 5, 6, 6}, {'R', 3, 21, 6, 6}, {'R', 13, 21, 6, 6}, {'R', 23, 21, 6, 6}, {'L', 15, 11, 7, 21},
     {'L', 16, 11, 16, 21}, {'L', 17, 11, 25, 21}},                                                 /* 6 */
    {{'R', 3, 5, 6, 6}, {'R', 3, 21, 6, 6}, {'R', 13, 21, 6, 6}, {'R', 23, 21, 6, 6}, {'L', 6, 11, 6, 21}}, /* 7 */
    {{'R', 1, 9, 6, 6}, {'R', 9, 9, 6, 6}, {'R', 17, 9, 6, 6}, {'R', 25, 9, 6, 6}, {'L', 4, 24, 28, 24},
     {'L', 4, 15, 4, 24}, {'L', 12, 15, 12, 24}, {'L', 20, 15, 20, 24}, {'L', 28, 15, 28, 24}},     /* 8 */
    {{'L', 4, 16, 7, 10}, {'L', 7, 10, 10, 7}, {'L', 10, 7, 13, 10}, {'L', 13, 10, 16, 16}, {'L', 16, 16, 19, 22},
     {'L', 19, 22, 22, 25}, {'L', 22, 25, 25, 22}, {'L', 25, 22, 28, 16}},                          /* sine */
    {{'L', 4, 16, 10, 7}, {'L', 10, 7, 22, 25}, {'L', 22, 25, 28, 16}},                             /* triangle */
    {{'L', 4, 24, 16, 8}, {'L', 16, 8, 16, 24}, {'L', 16, 24, 28, 8}},                              /* saw (up) */
    {{'L', 4, 8, 16, 24}, {'L', 16, 24, 16, 8}, {'L', 16, 8, 28, 24}},                              /* ramp (down) */
    {{'L', 4, 24, 4, 8}, {'L', 4, 8, 16, 8}, {'L', 16, 8, 16, 24}, {'L', 16, 24, 28, 24}, {'L', 28, 24, 28, 8}},
                                                                                                     /* square */
    {{'L', 4, 18, 10, 18}, {'L', 10, 18, 10, 9}, {'L', 10, 9, 16, 9}, {'L', 16, 9, 16, 23}, {'L', 16, 23, 22, 23},
     {'L', 22, 23, 22, 13}, {'L', 22, 13, 28, 13}},                                                  /* S&H */
    {{'L', 4, 19, 8, 13}, {'L', 8, 13, 12, 10}, {'L', 12, 10, 16, 13}, {'L', 16, 13, 20, 21}, {'L', 20, 21, 24, 22},
     {'L', 24, 22, 28, 15}},                                                                         /* smooth */
    {{'L', 4, 8, 4, 24}, {'L', 4, 24, 9, 24}, {'L', 9, 24, 13, 21}, {'L', 13, 21, 17, 14},
     {'L', 17, 14, 21, 9}, {'L', 21, 9, 28, 8}},                                                     /* pump */
    {{'L', 4, 24, 10, 24}, {'L', 10, 24, 10, 19}, {'L', 10, 19, 16, 19}, {'L', 16, 19, 16, 14},
     {'L', 16, 14, 22, 14}, {'L', 22, 14, 22, 9}, {'L', 22, 9, 28, 9}},                              /* stairs */
    {{'L', 4, 24, 4, 8}, {'L', 4, 8, 9, 24}, {'L', 9, 24, 13, 24}, {'L', 13, 24, 13, 8},
     {'L', 13, 8, 18, 24}, {'L', 18, 24, 22, 24}, {'L', 22, 24, 22, 8}, {'L', 22, 8, 27, 24}},       /* clave */
    {{'L', 4, 25, 6, 12}, {'L', 6, 12, 9, 7}, {'L', 9, 7, 12, 12}, {'L', 12, 12, 14, 25},
     {'L', 14, 25, 17, 15}, {'L', 17, 15, 20, 25}, {'L', 20, 25, 22, 20}, {'L', 22, 20, 24, 25},
     {'L', 24, 25, 26, 22}, {'L', 26, 22, 28, 25}},                                                  /* bounce */
    {{'L', 4, 24, 13, 8}, {'L', 13, 8, 13, 24}, {'L', 13, 24, 22, 8}, {'L', 22, 8, 22, 24},
     {'L', 22, 24, 28, 8}},                                                                          /* rises */
    {{'L', 4, 8, 13, 24}, {'L', 13, 24, 13, 8}, {'L', 13, 8, 22, 24}, {'L', 22, 24, 22, 8},
     {'L', 22, 8, 28, 24}},                                                                          /* falls */
    {{'L', 4, 24, 13, 8}, {'L', 13, 8, 16, 18}, {'L', 16, 18, 22, 11}, {'L', 22, 11, 28, 24}},       /* zigzag */
    {{'L', 16, 16, 11, 8}, {'L', 11, 8, 5, 13}, {'L', 5, 13, 9, 22}, {'L', 9, 22, 16, 16},
     {'L', 16, 16, 23, 10}, {'L', 23, 10, 28, 17}, {'L', 28, 17, 22, 24}, {'L', 22, 24, 16, 16}},    /* chaos */
    {{'L', 6, 13, 6, 19}, {'L', 10, 9, 10, 23}, {'L', 14, 12, 14, 20}, {'L', 18, 15, 18, 17}, {'L', 22, 10, 22, 22},
     {'L', 26, 14, 26, 18}, {'L', 4, 12, 10, 7}, {'L', 10, 7, 18, 13}, {'L', 18, 13, 22, 8}, {'L', 22, 8, 28, 13}},
                                                                                                     /* follow */
    {{'L', 8, 6, 8, 26}, {'L', 16, 6, 16, 26}, {'L', 24, 6, 24, 26}, {'L', 5, 20, 11, 20}, {'L', 13, 10, 19, 10},
     {'L', 21, 16, 27, 16}},                                                                         /* mixer */
    {{'D', 7, 16, 2, 0}, {'L', 11, 10, 13, 16}, {'L', 13, 16, 11, 22}, {'L', 16, 7, 19, 16}, {'L', 19, 16, 16, 25},
     {'L', 21, 4, 25, 16}, {'L', 25, 16, 21, 28}},                                                   /* reverb */
    {{'L', 5, 9, 27, 9}, {'D', 11, 9, 3, 0}, {'L', 5, 16, 27, 16}, {'D', 21, 16, 3, 0}, {'L', 5, 23, 27, 23},
     {'D', 14, 23, 3, 0}},                                                                              /* global */
    {{'L', 16, 6, 16, 26}, {'L', 6, 16, 26, 16}},                                                   /* new */
    {{'L', 10, 6, 10, 26}, {'L', 10, 6, 26, 16}, {'L', 26, 16, 10, 26}},                            /* the demo */
    {{'L', 16, 4, 16, 20}, {'L', 9, 13, 16, 20}, {'L', 16, 20, 23, 13}, {'L', 6, 27, 26, 27}},      /* the saved one */
    {{'L', 19, 3, 9, 18}, {'L', 9, 18, 20, 15}, {'L', 20, 15, 12, 29}},                            /* the stress test */
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
    {{'L', 4, 26, 9, 11}, {'L', 9, 11, 23, 11}, {'L', 23, 11, 28, 26}, {'L', 16, 5, 16, 8}},         /* the filters */
    {{'L', 4, 11, 12, 11}, {'L', 12, 11, 16, 4}, {'L', 16, 4, 16, 11}, {'L', 16, 11, 28, 11},
     {'R', 4, 18, 6, 7}, {'R', 13, 18, 6, 7}, {'D', 25, 21, 2, 0}},                                  /* the bending: a
                                                                                                     * bent line, bits */
    {{'C', 16, 16, 10, 0}, {'D', 16, 6, 3, 0}, {'D', 25, 21, 3, 0}, {'D', 7, 21, 3, 0}},           /* Euclid */
    {{'L', 16, 9, 16, 23}, {'L', 4, 16, 11, 16}, {'L', 4, 16, 8, 12}, {'L', 4, 16, 8, 20}, {'L', 21, 16, 28, 16},
     {'L', 28, 16, 24, 12}, {'L', 28, 16, 24, 20}},                                                  /* SPREAD: the
                                                                                                     * middle, out to
                                                                                                     * both sides */
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
            case 'C': hoop(x0, y0, (float)o->c * s + ww * 0.5f, ww, col); break;
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
/* ALL (GLO held a moment, or with a knob): the screen in the theme's KNOCK colour, everything on it knocked out in
 * black, until GLO is let go. Each canvas goes out through blit: its pixels by their brightness, from KNOCK (black)
 * to black (white) */
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
            l = l > 255u ? 0u : 255u - l;   /* what is left of the colour */
            cv_px[i] = swap16((uint16_t)(((uint32_t)(KNOCK >> 11) * l / 255u) << 11 |
                                         ((uint32_t)((KNOCK >> 5) & 63u) * l / 255u) << 5 |
                                         (uint32_t)(KNOCK & 31u) * l / 255u));
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

/* the header: the module's name, an LED a face (the one shown lit), what the knobs do now (MOD, REC, ALL, RND, a
 * held step); on the right, always in the same place, the keyboard's track: four squares, its own filled in its
 * colour (STEP mode on an effect's lane: that effect), then the clock, the load */
#define SQ_X 135                         /* the squares: 11 px, 3 apart (135..188) */
static void draw_header(void)
{
    static int load = -1;
    static uint32_t load_at;
    char t[32], v[12];
    int x, mode = ui.held_step >= 0 ? 5 : held(B_ENV) ? 4 : held(B_GLO) ? 3 : ui.grid ? 6 : ui.erec >= 0 ? 2 : ui.mod;
    int key = ui.grid ? ui.lane : ui.track; /* what the keyboard plays: a track, or (STEP mode) a lane's steps */
    uint16_t ac = page_accent(ui.page);
    if (load < 0 || plat_ms() - load_at >= 250u) {
        load = load_now();
        load_at = plat_ms();
    }
    int32_t s[6] = {ui.page | ui.matrix << 8 | mode << 10 | (ui.held_step & 127) << 13 | key << 20, load,
                    eng_playing() | dirty << 1 | page_count(ui.page) << 2 | algo_here(ui.page) << 6,
                    (int32_t)lane_accent(key) | ui.track << 16 | ui.grid << 19, ac, 0};
    const char *name = ui.matrix ? "Matrix" : module_name(ui.page);
    for (int i = 0; i < ENG_TRACKS; i++)
        s[5] = s[5] * 31 + (int32_t)track_accent(i);
    for (const char *c = name; *c; c++)  /* (its name too: another model can change it, Plaits to an FM drum or ZERO) */
        s[5] = s[5] * 31 + *c;
    if (!ui.full && !ui.anim && !memcmp(s, ui.head_shown, sizeof s))
        return;
    memcpy(ui.head_shown, s, sizeof s);
    cv_begin(240, HEAD_H, BG);
    label_text(name, t, sizeof t);
    cv_text(10, 6, &FONT_B, dec(t, 0), TXT);
    x = 10 + text_w(&FONT_B, t);         /* (its place: the name's, not the letters' on the way) */
    {
        static const char *const M[7] = {"", "MOD", "REC", "ALL", "RND", "", "STEP"};
        char m[16];
        m[0] = 0;
        if (mode == 5) {                 /* what the knobs do now: a step's locks, randoms, all the tracks, ... */
            int_text(ui.held_step + 1, v);
            text_cat(m, "STEP ", sizeof m);
            text_cat(m, v, sizeof m);
        } else if (mode) {
            text_cat(m, M[mode], sizeof m);
        }
        /* what the encoders do here: SELECT the pages (0.16.6: a bar under the header, a part a face, the one shown
         * lit; before, small LEDs by the name, unseen), ALGO a choice (a ringed A) */
        int faces = !ui.matrix && page_count(ui.page) > 1 ? page_count(ui.page) : 0;
        int alg = !ui.matrix && algo_here(ui.page);
        int pw = mode ? text_w(&FONT_T, m) + 10 : 0, lx = x + (alg ? 18 : 0);
        if (mode && lx + 8 + pw > SQ_X - 4 && alg) {   /* no room for both: the A gives way to the mode */
            alg = 0;
            lx -= 18;
        }
        if (faces) {                     /* the faces: 220 px in as many parts, 4 apart; the one shown lit, taller */
            int cur = page_face(ui.page), gap = 4, w = (220 - gap * (faces - 1)) / faces;
            for (int f = 0; f < faces; f++) {
                int fx = 10 + f * (w + gap);
                if (f == cur)
                    cv_rect(fx, 23, w, 3, ac);
                else
                    cv_rect(fx, 24, w, 2, tint(ac, BG, 3));
            }
        }
        if (alg) {
            float cx = (float)(x + 9);
            hoop(cx, 14.0f, 6.5f, 1.4f, ac);
            cv_text((int)cx - (text_w(&FONT_T, "A") + 1) / 2, 8, &FONT_T, "A", ac);
        }
        if (mode == 3 && knock)          /* ALL in yellow and black: white on black here, black on yellow there */
            text_pill(lx + 8 + pw / 2, 7, &FONT_T, m, HI, BG);
        else if (mode == 5)
            text_pill(lx + 8 + pw / 2, 7, &FONT_T, m, HI, BG);
        else if (mode)
            text_pill(lx + 8 + pw / 2, 7, &FONT_T, m, mode == 3 ? AC1 : mode == 4 ? AC2 : AC3, mode == 4 ? BG : HI);
    }
    if (key >= ENG_TRACKS) {             /* STEP mode on an effect's lane: that effect, in its colour */
        static const char *const FXN[3] = {"DELAY", "REVERB", "WARPS"};
        pill(SQ_X, 8, 53, 13, lane_accent(key));
        cv_text(SQ_X + 27 - text_w(&FONT_T, FXN[key - ENG_TRACKS]) / 2, 8, &FONT_T, FXN[key - ENG_TRACKS], BG);
    } else {
        for (int i = 0; i < ENG_TRACKS; i++) {   /* the four tracks: the keyboard's filled in its colour, the others
                                                  * outlined in theirs, dimmed */
            int sx = SQ_X + i * 14;
            uint16_t c = track_accent(i), d = tint(c, BG, 4);
            char n[2] = {(char)('1' + i), 0};
            if (i == key) {
                cv_rect(sx, 8, 11, 12, c);
            } else {
                cv_rect(sx, 8, 11, 1, d);
                cv_rect(sx, 19, 11, 1, d);
                cv_rect(sx, 8, 1, 12, d);
                cv_rect(sx + 10, 8, 1, 12, d);
            }
            cv_text(sx + 6 - (text_w(&FONT_T, n) + 1) / 2, 8, &FONT_T, n, i == key ? BG : d);
        }
    }
    t[0] = 0;
    int_text(load, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "%", sizeof t);
    x = 232 - text_w(&FONT_XS, t);
    cv_text(x, 6, &FONT_XS, t, load >= 80 ? AC3 : load >= 60 ? AC2 : DIM);
    if (dirty)                           /* not saved */
        disc((float)(x - 5), 14.0f, 2.2f, AC3);
    for (int y = -6; y <= 6; y++)        /* the clock: playing or not */
        cv_rect(195, 14 + y, 11 - (iabs(y) * 11 + 3) / 6, 1, eng_playing() ? AC1 : DIM);
    blit(0, 0);
}

/* the module's display: its icon in a lit ring, its name, a line on its state ---------------------------------- */
#define HX 41.0f
#define HY 35.0f
static void hero_icon(int which, uint16_t c)
{
    hoop(HX, HY, 27.0f, 2.0f, c);
    vicon(HX, HY, 1.05f, 2.4f, c, which, 0);
}

static int hero_title(const char *name)  /* the name: one line of L, two cut at a space, else M; the y under it */
{
    char a[24], b[24];
    int n = 0;
    if (text_w(&FONT_L, name) <= 150) {
        cv_text(84, 6, &FONT_L, dec(name, 1), HI);
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
            cv_text(84, -4, &FONT_L, dec(a, 1), HI);
            cv_text(84, 22, &FONT_L, dec(b, 2), HI);
            return 55;
        }
    }
    cv_text(84, 12, &FONT_M, dec(name, 1), HI);
    return 44;
}

static void hero_line(int y, const char *s)   /* the line under the name: its parts ('|') apart, a dot between; a
                                                 * part that would run off the screen is left out, and the rest (0.16.7:
                                                 * its dot too) */
{
    char part[40];
    int x = 85, n = 0, sep = 0;
    s = dec(s, 3);
    for (;; s++) {
        if (*s && *s != '|' && n < (int)sizeof part - 1) {
            part[n++] = *s;
            continue;
        }
        part[n] = 0;
        if (x + (sep ? 11 : 0) + text_w(&FONT_XS, part) > 238)
            break;
        if (sep) {
            disc((float)x + 5.0f, (float)y + 8.0f, 1.4f, DIM);
            x += 11;
        }
        x = cv_text(x, y, &FONT_XS, part, MUTED);
        n = 0;
        sep = 1;
        if (!*s)
            break;
    }
}

/* the FM's second face: SHAPE drawn, each modulator's depth over the note (three of its units: the note's time
 * constant, or a swell's attack), over INDEX (the dotted line); the first modulator brightest */
static void fm_shape_panel(int t, int pg, uint16_t ac)
{
    char v[16], u[16];
    int mods = eng_fm_shaped(t), k = 0;
    const int x0 = 86, w = 146, ym = 44, hh = 22;
    cv_text(84, 0, &FONT_XS, dec("SHAPE", 1), MUTED);
    eng_param_text(pg * 4 + 2, eng_final(pg * 4 + 2), v, sizeof v);
    label_text(v, u, sizeof u);
    cv_text(232 - text_w(&FONT_XS, u), 0, &FONT_XS, u, DIM);
    for (int x = 0; x < w; x += 5)
        cv_rect(x0 + x, ym, 2, 1, MUTED);
    int reveal = anim_p >= 1.0f ? w : (int)((float)w * (1.0f - (1.0f - anim_p) * (1.0f - anim_p)));
    if (!mods) {                         /* (Flat: INDEX alone) */
        seg((float)x0, (float)ym, (float)(x0 + reveal), (float)ym, 2.0f, ac);
        return;
    }
    for (int op = 0; op < 4; op++) {
        if (!(mods >> op & 1))
            continue;
        uint16_t c = k ? tint(ac, BG, k + 1) : ac;
        float px = 0.0f, py = 0.0f;
        k++;
        for (int x = 0; x <= reveal; x += 2) {
            float y = (float)ym - eng_fm_shape(t, op, (float)x * (3.0f / (float)w)) * (float)hh;
            if (x)
                seg((float)x0 + px, py, (float)(x0 + x), y, 2.0f, c);
            px = (float)x;
            py = y;
        }
    }
}

/* OTIS's first face: in the ring, the cycle its newest voice reads (its Transwave's place as it steps), its name, its
 * program's family, the place */
static void panel_sq1(int t, int m, uint16_t ac)
{
    float y[48];
    char s[40], n[8];
    int of = 32, at = eng_sq_cycle(t, y, 48, &of);
    hoop(HX, HY, 27.0f, 2.0f, ac);
    if (at < 0) {
        vicon(HX, HY, 1.05f, 2.4f, ac, IC_MODEL + m, 0);
    } else {
        int reveal = anim_p >= 1.0f ? 48 : (int)(48.0f * (1.0f - (1.0f - anim_p) * (1.0f - anim_p)));
        for (int i = 1; i < reveal; i++) {   /* (37 px wide, 13 px each way) */
            float x0 = HX - 18.5f + (float)(i - 1) * (37.0f / 47.0f), x1 = HX - 18.5f + (float)i * (37.0f / 47.0f);
            seg(x0, HY - y[i - 1] * 13.0f, x1, HY - y[i] * 13.0f, 4.0f, tint(ac, BG, 2));
            seg(x0, HY - y[i - 1] * 13.0f, x1, HY - y[i] * 13.0f, 2.0f, ac);
        }
    }
    int yl = hero_title(eng_algo_name(PG_TRACK(t, 0), m));
    s[0] = 0;
    text_cat(s, eng_sq_family(t), sizeof s);
    if (at >= 0) {
        text_cat(s, "|", sizeof s);
        int_text(at % 32 + 1, n);
        text_cat(s, n, sizeof s);
        text_cat(s, " / ", sizeof s);
        int_text(of, n);
        text_cat(s, n, sizeof s);
    }
    hero_line(yl, s);
}

static void panel_plaits(int t, int face, uint16_t ac)
{
    project_t *p = eng_project();
    int m = p->algo[PG_TRACK(t, 0)];
    char s[40], n[8];
    int alg = eng_fm_algo(t);            /* (the FM: its algorithm, as it plays) */
    if (face != 2 && track_kind(t) == MK_SQ1) {
        panel_sq1(t, m, ac);
        return;
    }
    hero_icon(alg >= 0 ? IC_ALG + alg : IC_MODEL + m, ac);
    if (face == 0) {                     /* the model (PADS: its progression, its chord) */
        int y = hero_title(eng_algo_name(PG_TRACK(t, 0), m));
        s[0] = 0;
        if (eng_pads_track() == t) {
            eng_pads_line(s, sizeof s);
            hero_line(y, s);
            return;
        }
        text_cat(s, eng_bank_name(eng_model_bank(m)), sizeof s);
        text_cat(s, "|", sizeof s);
        int_text(m + 1, n);
        text_cat(s, n, sizeof s);
        text_cat(s, " / ", sizeof s);
        int_text(ENG_MODELS, n);
        text_cat(s, n, sizeof s);
        hero_line(y, s);
    } else if (face == 1 && alg >= 0) {  /* the FM: its SHAPE */
        fm_shape_panel(t, PG_TRACK(t, 1), ac);
    } else if (face == 1) {              /* the low-pass gate: DECAY's tail, COLOUR's tone */
        int pg = PG_TRACK(t, 1), y0 = 68, h = 48;
        float decay = eng_final(pg * 4 + 0), colour = eng_final(pg * 4 + 1);
        float k = (0.002f + 0.12f * (1.0f - decay) * (1.0f - decay) * (1.0f - decay)) * 1.4f;
        float px = 0.0f, py = (float)y0;
        uint16_t fill = tint(ac, BG, 1 + (int)(colour * 2.0f));
        int m = p->algo[PG_TRACK(t, 0)];
        if (eng_model_enveloped(m)) {    /* its own envelope: the LPG is out of its way */
            int y = hero_title("ENVELOPE");
            hero_line(y, eng_model_own(m) || eng_model_sub(m) >= 19 ? "NO LPG|DECAY: ITS DECAY" : "NO LPG|IN ITS PRESET");
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
    } else {                             /* the note now, the gate's length, its octave and transposition */
        int pg = PG_TRACK(t, 2), on = (int)((eng_gates() >> t) & 1u), x;
        char v[12];
        note_text(eng_note(t), n);
        x = cv_text(84, 6, &FONT_L, dec(n, 1), on ? HI : MUTED);
        x = 84 + text_w(&FONT_L, n);
        led((float)(x + 12), 22.0f, 5.0f, ac, on ? 8 : 0);
        s[0] = 0;
        text_cat(s, "GATE ", sizeof s);
        eng_param_text(pg * 4 + 1, eng_final(pg * 4 + 1), v, sizeof v);
        text_cat(s, v, sizeof s);
        if (eng_model_own(m)) {         /* a drum's, an FM drum's, ZERO's own (its eighth knob): no gate */
            char u[16];
            s[0] = 0;
            label_text(eng_param_name(pg * 4), u, sizeof u);
            text_cat(s, u, sizeof s);
            text_cat(s, " ", sizeof s);
            eng_param_text(pg * 4, eng_final(pg * 4), v, sizeof v);
            text_cat(s, v, sizeof s);
        }
        text_cat(s, "|OCT ", sizeof s);
        eng_param_text(pg * 4 + 3, eng_final(pg * 4 + 3), v, sizeof v);
        text_cat(s, v, sizeof s);
        text_cat(s, "|TR ", sizeof s);
        eng_param_text(pg * 4 + 2, eng_final(pg * 4 + 2), v, sizeof v);
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

/* an LFO: its wave in the ring, what it did these 1.4 s on a scope (lfo_scope, drawn live), its rate, how it runs */
APP_SMALL static void panel_lfo(int i)
{
    project_t *p = eng_project();
    int pg = PG_LFO(i), w = clamp((int)(p->val[pg * 4] + 0.5f), 0, LW_N - 1), m = (int)(p->val[pg * 4 + 2] + 0.5f);
    char s[48], v[16], u[16];
    hero_icon(IC_WAVE + w, accent3(i));
    s[0] = 0;
    eng_param_text(pg * 4 + 1, eng_final(pg * 4 + 1), v, sizeof v);
    label_text(v, u, sizeof u);
    text_cat(s, u, sizeof s);
    if (w == LW_FOLLOW) {                /* (0.16.23) FOLLOW: what it listens to, how */
        int src = clamp((int)(p->val[PG_FOLLOW(i) * 4] + 0.5f), 0, ENG_TRACKS - 1);
        s[0] = 0;
        text_cat(s, m == LF_FOLLOW ? "FOLLOWS T" : m == LF_TRIG ? "OPENS ON T" : "AT EACH TRIG OF T", sizeof s);
        v[0] = (char)('1' + (m >= LF_T1 ? clamp(m - LF_T1, 0, ENG_TRACKS - 1) : src));
        v[1] = 0;
        text_cat(s, v, sizeof s);
        if (m <= LF_TRIG) {
            eng_param_text(PG_FOLLOW(i) * 4 + 2, p->val[PG_FOLLOW(i) * 4 + 2], v, sizeof v);
            label_text(v, u, sizeof u);
            text_cat(s, m == LF_FOLLOW ? "|OVER " : "|AT ", sizeof s);
            text_cat(s, u, sizeof s);
        }
        hero_line(48, s);
        return;
    }
    text_cat(s, m == LM_FREE ? "|FREE" : m == LM_SYNC ? "|ON THE TEMPO" : "|AGAIN AT T", sizeof s);
    if (m >= LM_T1) {
        v[0] = (char)('1' + clamp(m - LM_T1, 0, ENG_TRACKS - 1));
        v[1] = 0;
        text_cat(s, v, sizeof s);
    }
    hero_line(48, s);
}

#define SCOPE_W 148                      /* the LFO's scope, right of its ring */
#define SCOPE_H 42
static void lfo_scope(int force)         /* the LFO these 1.4 s (eng_hist), the newest at the right */
{
    static int32_t shown;
    int i = lfo_of(ui.page), h[ENG_HIST];
    int32_t key = i + 1;
    for (int j = 0; j < ENG_HIST; j++) {
        h[j] = eng_hist(SRC_L1 + i, j);
        key = key * 31 + h[j];
    }
    if (!force && key == shown)
        return;
    shown = key;
    uint16_t ac = accent3(i), glow = tint(ac, BG, 2);
    float px = 0.0f, py = 0.0f;
    cv_begin(SCOPE_W, SCOPE_H, BG);
    for (int x = 0; x < SCOPE_W - 4; x += 4)   /* 0, dotted */
        cv_rect(x, SCOPE_H / 2, 2, 1, RULE);
    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < ENG_HIST; j++) {
            float x = 2.0f + (float)j * (float)(SCOPE_W - 8) / (float)(ENG_HIST - 1);
            float y = (float)(SCOPE_H / 2) - (float)h[j] * (float)(SCOPE_H / 2 - 4) / 127.0f;
            if (j)
                seg(px, py, x, y, pass ? 2.0f : 5.0f, pass ? ac : glow);
            px = x;
            py = y;
        }
    lit_disc(px, py, 3.0f, HI);           /* now */
    blit(84, PANEL_Y + 2);
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

/* (0.16.19) SPREAD: a knob a track, its highs apart on both sides, its lows in the middle. Each track's image, a row
 * each: where its pan sets it, as wide as it is now (a cable, the pan: hard to one side, a point), in its colour */
APP_SMALL static void panel_spread(void)
{
    hero_icon(IC_SPREAD, AC1);
    hero_line(hero_title("SPREAD"), "LOWS MONO");
    const int x0 = 190, w = 44;
    for (int t = 0; t < ENG_TRACKS; t++) {
        int y = 12 + t * 13;
        float pan = fm_clampf(eng_now(PG_PAN * 4 + t), -1.0f, 1.0f), wd = fm_clampf(eng_spread_width(t) / 0.8f, 0.0f, 1.0f);
        float cx = (float)x0 + (float)w * 0.5f * (1.0f + pan), hw = 2.0f + wd * ((float)w * 0.5f - 2.0f);
        int a = (int)(cx - hw + 0.5f), b = (int)(cx + hw + 0.5f);
        uint16_t c = t == ui.track ? track_accent(t) : tint(track_accent(t), BG, 5);
        a = a < x0 ? x0 : a;
        b = b > x0 + w ? x0 + w : b;
        cv_rect(x0, y + 2, w + 1, 1, RULE);
        cv_rect(x0 + w / 2, y - 1, 1, 7, RULE);
        cv_rect(a, y, b - a + 1, 5, c);
    }
}

/* the filters: a knob a track, a low pass to the left, a high pass to the right. Their four responses drawn over
 * 20 Hz..20 kHz (an octave 9.8 px) and 0..-48 dB (a dB a pixel), each in its track's colour, the active one last */
static float eq_db(float v, float hz)
{
    if (v > -0.01f && v < 0.01f)
        return 0.0f;
    float fc = v < 0.0f ? 18000.0f * fm_exp2f(7.17f * v) : 20.0f * fm_exp2f(8.64f * v);
    float r = v < 0.0f ? hz / fc : fc / hz, r8 = r * r * r * r;   /* 24 dB/oct (Butterworth): 1 / (1 + r^8) */
    r8 *= r8;
    return -10.0f * 0.30103f * fm_log2f(1.0f + r8);
}
APP_SMALL static void panel_eq(void)
{
    hero_icon(IC_EQ, AC1);
    hero_line(hero_title("EQ"), "LP|HP|24 DB");
    const int x0 = 136, w = 98, y0 = 10, h = 52;
    for (int o = 0; o <= 3; o++) {       /* 20 Hz, 200 Hz, 2 kHz, 20 kHz */
        int x = x0 + (int)((float)w * (float)o * 3.3219f / 9.9658f + 0.5f);
        cv_rect(x, y0, 1, h, RULE);
    }
    cv_rect(x0, y0, w + 1, 1, RULE);
    int act = ui.track;
    for (int k = 0; k < ENG_TRACKS; k++) {
        int t = (act + 1 + k) % ENG_TRACKS;   /* the active track last: on top */
        float v = eng_final(PG_EQ * 4 + t);
        uint16_t c = t == act ? track_accent(t) : tint(track_accent(t), BG, 4);
        int py = 0;
        for (int i = 0; i <= w; i++) {
            float hz = 20.0f * fm_exp2f(9.9658f * (float)i / (float)w);
            int y = y0 + (int)(fm_clampf(-eq_db(v, hz), 0.0f, (float)h) + 0.5f);
            if (i)
                cv_line(x0 + i - 1, py, x0 + i, y, c);
            if (t == act && i)
                cv_line(x0 + i - 1, py + 1, x0 + i, y + 1, c);
            py = y;
        }
    }
}

/* the filter (PG_FILT): its response as it is now (its cutoff moved by the envelope, FM, the key) in the ring, its
 * model, its type and the cutoff in Hz; OFF: what ALGO does */
static float filt_db(int m, int ty, float w, float q)   /* its response at w (the cutoff's multiple), dB, a sketch */
{
    float w2 = w * w, re = 1.0f - w2, d2 = re * re + w2 / (q * q), a;
    if (m == MF_VOX)
        return 0.0f;
    if (m == MF_LADDER) {                /* four poles: the twelve dB a pair, the resonance on the last */
        float x = 1.0f + w2;
        a = ty == MF_LP ? 1.0f / (x * x) : ty == MF_HP ? w2 * w2 / (x * x) : w2 / (x * x) * 4.0f;
        return 10.0f * fm_log2f(fm_maxf(a, 1e-9f)) * 0.30103f + 20.0f * fm_log2f(fm_maxf(q, 0.5f) * 1.41f) *
               0.30103f * (1.0f / (1.0f + 8.0f * (1.0f - w) * (1.0f - w)));
    }
    a = ty == MF_LP ? 1.0f / d2 : ty == MF_HP ? w2 * w2 / d2 : w2 / (q * q * d2);
    return 10.0f * fm_log2f(fm_maxf(a, 1e-9f)) * 0.30103f;
}

APP_SMALL static void panel_filter(int t, uint16_t ac)
{
    project_t *p = eng_project();
    int m = p->algo[PG_FILT(t, 0)], ty = clamp((int32_t)(p->val[PG_FILT(t, 2) * 4] + 0.5f), 0, MF_TYPES - 1);
    float res = eng_final(PG_FILT(t, 0) * 4 + 1), hz = m ? eng_filter_hz(t) : 1000.0f, env = eng_filter_env(t);
    float q = m == MF_SEM ? 0.5f + 12.0f * res * res * res : m == MF_LADDER ? 0.5f + 6.0f * res * res : 0.6f + 5.0f * res;
    char s[48], v[16];
    hoop(HX, HY, 27.0f, 2.0f, m ? ac : DIM);
    /* the response, 20 Hz .. 20 kHz over the ring's inside, -30 .. +18 dB */
    float px = 0.0f, py = 0.0f;
    for (int i = 0; i <= 36; i++) {
        float x = HX - 18.0f + (float)i, f = 20.0f * fm_exp2f((float)i * (9.97f / 36.0f)), db;
        if (!m) {
            db = 0.0f;
        } else if (m == MF_VOX) {        /* three bumps: the vowel's formants, roughly */
            float o = fm_log2f(f / hz);
            db = -18.0f + 18.0f * fm_maxf(0.0f, 1.0f - fm_fabsf(o) * 2.0f) +
                 14.0f * fm_maxf(0.0f, 1.0f - fm_fabsf(o - 1.1f) * 2.5f) + 10.0f * fm_maxf(0.0f, 1.0f - fm_fabsf(o - 1.9f) * 3.0f);
        } else {
            db = filt_db(m, ty, f / hz, q);
        }
        float y = HY + 2.0f - fm_clampf(db, -30.0f, 18.0f) * 0.5f;
        if (i)
            seg(px, py, x, y, 1.6f, m ? ac : DIM);
        px = x, py = y;
    }
    if (m) {                             /* the cutoff, lit by the envelope */
        float cx = HX - 18.0f + fm_clampf(fm_log2f(hz / 20.0f) * (36.0f / 9.97f), 0.0f, 36.0f);
        disc(cx, HY + 16.0f, 2.0f + 2.0f * env, tint(HI, ac, 4 + (int)(env * 4.0f)));
    }
    int y = hero_title(mf_model_name(m));
    s[0] = 0;
    if (!m) {
        text_cat(s, "ALGO: A MODEL", sizeof s);
    } else {
        static const char *const TY[MF_TYPES] = {"LOW PASS", "BAND PASS", "HIGH PASS"};
        static const char *const VO[MF_TYPES] = {"LOW VOICE", "MID VOICE", "HIGH VOICE"};
        text_cat(s, m == MF_VOX ? VO[ty % MF_TYPES] : TY[ty % MF_TYPES], sizeof s);
        if (m != MF_VOX && ty != MF_BP)  /* (the ladder's four poles, the others' two) */
            text_cat(s, m == MF_LADDER ? " 24" : m == MF_MS20 && ty == MF_HP ? " 6" : " 12", sizeof s);
        text_cat(s, "|", sizeof s);
        if (hz >= 1000.0f) {
            int_text((int32_t)(hz / 100.0f + 0.5f) / 10, v);
            text_cat(s, v, sizeof s);
            text_cat(s, ".", sizeof s);
            int_text((int32_t)(hz / 100.0f + 0.5f) % 10, v);
            text_cat(s, v, sizeof s);
            text_cat(s, "K", sizeof s);
        } else {
            int_text((int32_t)(hz + 0.5f), v);
            text_cat(s, v, sizeof s);
            text_cat(s, " HZ", sizeof s);
        }
    }
    hero_line(y, s);
    if (m && (eng_playing() || eng_filter_load(t))) {   /* its time on its core (the machine page's T1..T4 hold it):
                                                         * the percentage alone, in a small frame beside the name */
        uint32_t ld = eng_filter_load(t);
        int_text((int32_t)(ld / 10u), v);
        if (ld < 100u) {                 /* (under 10 %: a tenth) */
            text_cat(v, ".", sizeof v);
            char d[2] = {(char)('0' + ld % 10u), 0};
            text_cat(v, d, sizeof v);
        }
        text_cat(v, "%", sizeof v);
        int tw = text_w(&FONT_XS, v), w = tw + 12, x = 233 - w;
        if (84 + text_w(&FONT_L, mf_model_name(m)) + 6 <= x) {
            pill(x, 15, w, 15, DIM);
            pill(x + 1, 16, w - 2, 13, BG);
            cv_text(x + 6, 15 + (15 - FONT_XS.h) / 2 + 1, &FONT_XS, v, MUTED);
        }
    }
}

/* the filter's envelope (ENV's second page): its vactrol's rise (ATTACK) and fall (DECAY), the ping its trigs give
 * (a spike), where it is now (the ring); the filter OFF: dim */
APP_SMALL static void panel_env2(int t, uint16_t ac)
{
    project_t *p = eng_project();
    int on = p->algo[PG_FILT(t, 0)] != MF_OFF, y0 = 68, h = 48;
    float at = eng_final(PG_FILT(t, 2) * 4 + 3), dc = eng_final(PG_FILT(t, 0) * 4 + 3);
    float ping = eng_final(PG_FILT(t, 1) * 4 + 1), env = on ? eng_filter_env(t) : 0.0f;
    float rise = 1.0f + 60.0f * at * at, k = 0.004f + 0.16f * (1.0f - dc) * (1.0f - dc) * (1.0f - dc);
    uint16_t c = on ? ac : DIM, fill = tint(c, BG, 2);
    float px = 0.0f, py = (float)y0;
    arc(HX, HY, 22.0f, 27.0f, 0.0f, 1.0f, TRACK);   /* the ring: the envelope now */
    if (env > 0.004f)
        lit_arc(HX, HY, 22.0f, 27.0f, 0.0f, fm_minf(env, 1.0f), c);
    cv_text(HX - text_w(&FONT_XS, "ENV") / 2, HY - 8, &FONT_XS, "ENV", on ? MUTED : DIM);
    cv_text(84, 0, &FONT_XS, dec(on ? "FILTER ENVELOPE" : "FILTER OFF", 1), MUTED);
    if (ping > 0.02f)
        cv_text(232 - text_w(&FONT_XS, "PING"), 0, &FONT_XS, "PING", on ? c : DIM);
    int reveal = anim_p >= 1.0f ? 146 : (int)(146.0f * (1.0f - (1.0f - anim_p) * (1.0f - anim_p)));
    for (int x = 0; x < reveal; x++) {   /* (a page changed: the curve drawn in) */
        float e = (float)x < rise ? 1.0f - fm_exp2f(-(float)x * 4.0f / rise) * (1.0f - (float)x / rise)
                                  : fm_exp2f(-((float)x - rise) * k * 1.4427f);
        float y = (float)y0 - e * (float)h;
        if (!(x % 3) && y < (float)y0 - 2.0f)
            cv_rect(86 + x, (int)y + 3, 1, y0 - (int)y - 2, fill);
        if (x) {
            seg(86.0f + px, py, 86.0f + (float)x, y, 5.0f, tint(c, BG, 2));
            seg(86.0f + px, py, 86.0f + (float)x, y, 2.0f, c);
        }
        px = (float)x;
        py = y;
    }
    if (ping > 0.02f)                    /* the ping: a spike at the trig, as tall as it is */
        seg(87.0f, (float)y0, 87.0f, (float)y0 - (float)h * (0.3f + 0.7f * ping), 2.0f, on ? HI : DIM);
}

/* the bending's bits (CLOCK, BITS, MASK, SHAPE): a cycle of a sine through them, as the engine has them now, and
 * the 8-bit word's switches as Biscuit shows them (the most significant first): lit on, the accent inverted, dim
 * muted */
APP_SMALL static void panel_bend(int t)
{
    char s[48], v[16];
    int pg = PG_BEND(t);
    float ck = eng_final(pg * 4), cr = eng_final(pg * 4 + 1);
    int mask = (int)(eng_final(pg * 4 + 2) + 0.5f), shape = (int)(eng_final(pg * 4 + 3) + 0.5f);
    uint16_t c = track_accent(t);
    hero_icon(IC_BEND, c);
    int y = hero_title("BITS");
    s[0] = 0;
    if (ck < 0.005f && cr < 0.005f && !mask && !shape) {
        text_cat(s, "CLEAN", sizeof s);
    } else {
        eng_param_text(pg * 4, ck, v, sizeof v);
        text_cat(s, ck < 0.005f ? "CLOCK OFF" : v, sizeof s);
        text_cat(s, "|", sizeof s);
        eng_param_text(pg * 4 + 1, cr, v, sizeof v);
        text_cat(s, v, sizeof s);
        text_cat(s, "|", sizeof s);
        eng_param_text(pg * 4 + 2, (float)mask, v, sizeof v);
        text_cat(s, v, sizeof s);
        if (shape) {
            text_cat(s, "|", sizeof s);
            eng_param_text(pg * 4 + 3, (float)shape, v, sizeof v);
            text_cat(s, v, sizeof s);
        }
    }
    char u[48];
    label_text(s, u, sizeof u);
    hero_line(y, u);
    const int x0 = 150, w = 84, yc = 20;
    float wave[85];
    eng_bend_preview(t, wave, w + 1);
    cv_rect(x0, yc, w + 1, 1, RULE);
    int py = yc;
    for (int i = 0; i <= w; i++) {
        int yy = yc - (int)(wave[i] * 15.0f + (wave[i] < 0.0f ? -0.5f : 0.5f));
        if (i) {
            cv_line(x0 + i - 1, py, x0 + i, yy, c);
            cv_line(x0 + i - 1, py + 1, x0 + i, yy + 1, c);
        }
        py = yy;
    }
    uint8_t inv = 0, cut = 0;            /* the switches, under the line */
    eng_bend_mask(t, &inv, &cut);
    for (int b = 7; b >= 0; b--) {
        int x = 85 + (7 - b) * 13, m = 1 << b;
        if (cut & m) {
            cv_rect(x, 62, 10, 1, DIM), cv_rect(x, 70, 10, 1, DIM), cv_rect(x, 62, 1, 9, DIM), cv_rect(x + 9, 62, 1, 9, DIM);
        } else {
            cv_rect(x, 62, 10, 9, (inv & m) ? c : MUTED);
        }
    }
    cv_text(85 + 8 * 13 + 4, 60, &FONT_XS, "MSB  LSB", DIM);
}

/* the bending's glitch (GLITCH, SIZE, BEND, DROP): its chance, then a beat of what it does, the slice repeated its
 * size wide, each repeat lower or higher (BEND), the dropped ones dotted (DROP); lit while it repeats */
APP_SMALL static void panel_glitch(int t)
{
    char s[48], v[16];
    int pg = PG_GLITCH(t), reps = 0;
    float sp = 1.0f;
    float gl = eng_final(pg * 4), bend = eng_final(pg * 4 + 2), drop = eng_final(pg * 4 + 3);
    int size = (int)(eng_final(pg * 4 + 1) + 0.5f), st = eng_glitch_state(t, &reps, &sp);
    uint16_t c = track_accent(t);
    hero_icon(IC_BEND, st == 2 ? HI : c);
    int y = hero_title("GLITCH");
    s[0] = 0;
    if (gl < 0.005f) {
        text_cat(s, "OFF", sizeof s);
    } else {
        eng_param_text(pg * 4, gl, v, sizeof v);
        text_cat(s, v, sizeof s);
        text_cat(s, "|", sizeof s);
        eng_param_text(pg * 4 + 1, (float)size, v, sizeof v);
        text_cat(s, v, sizeof s);
        text_cat(s, "|", sizeof s);
        eng_param_text(pg * 4 + 2, bend, v, sizeof v);
        text_cat(s, v, sizeof s);
    }
    char u[48];
    label_text(s, u, sizeof u);
    hero_line(y, u);
    const int x0 = 85, w = 148, yb = 74;  /* a beat under the line: the slices, each repeat's height its pitch */
    static const int SW[4] = {9, 18, 37, 74};
    int sw = SW[size < 0 ? 0 : size > 3 ? 3 : size];
    float h = 7.0f, mult = fm_exp2f(bend * 0.5f);
    uint32_t r = 0x9E3779B9u;
    cv_rect(x0, yb, w + 1, 1, RULE);
    for (int x = x0, i = 0; x + 2 <= x0 + w; x += sw, i++) {
        r = r * 1664525u + 1013904223u;
        int dropped = i > 0 && (float)(r >> 8) * (1.0f / 16777216.0f) < drop;
        int hh = (int)fm_clampf(h, 2.0f, 14.0f), ww = sw - 2 < 1 ? 1 : sw - 2;
        uint16_t col = gl < 0.005f ? DIM : (st == 2 && i <= reps) ? HI : i == 0 ? c : tint(c, BG, 5);
        if (dropped) {
            for (int k = 0; k < ww; k += 3) cv_rect(x + k, yb - hh, 1, 1, col);
        } else {
            cv_rect(x, yb - hh, ww, hh, col);
        }
        if (i > 0) h *= mult;
    }
}

/* the Euclidean rhythm: FILLS over LENGTH, ROTATE; a ring of the lane's steps, its trigs lit, the one playing ringed */
APP_SMALL static void panel_euclid(int l)
{
    project_t *p = eng_project();
    char s[48], n[8];
    int len = (int)(p->val[PG_STEP(l) * 4 + 3] + 0.5f), fills = (int)(p->val[PG_EUC(l) * 4] + 0.5f);
    int rot = (int)(p->val[PG_EUC(l) * 4 + 1] + 0.5f), now = eng_step_now(l);
    uint16_t c = lane_accent(l);
    hero_icon(IC_EUC, c);
    int y = hero_title("EUCLID");
    s[0] = 0;
    int_text(fills > len ? len : fills, n);
    text_cat(s, n, sizeof s);
    text_cat(s, " IN ", sizeof s);
    int_text(len, n);
    text_cat(s, n, sizeof s);
    text_cat(s, "|TURNED ", sizeof s);
    int_text(len ? rot % len : 0, n);
    text_cat(s, n, sizeof s);
    hero_line(y, s);
    float cx = 208.0f, cy = 36.0f, rr = 23.0f, dr = len > 32 ? 1.3f : len > 16 ? 1.9f : 2.8f;
    for (int i = 0; i < len; i++) {      /* the first step at the top, clockwise */
        float a = 6.2831853f * (float)i / (float)len;
        float x = cx + rr * fm_sinf(a), yy = cy - rr * fm_cosf(a);
        int on = eng_trig(l, i);
        disc(x, yy, dr, on ? c : OFF);
        if (i == now)
            hoop(x, yy, dr + 2.2f, 1.2f, HI);
    }
}

static void panel_bus(int ott)           /* the master bus: this face's mode, on or not */
{
    static const char *const M[3] = {"MASTER BUS OFF", "COMP ON", "OTT ON"};
    int m = bus_mode(), on = m == (ott ? MB_OTT : MB_COMP);
    char s[48];
    hero_icon(ott ? IC_OTT : IC_COMP, on ? (ott ? AC3 : AC2) : MUTED);
    int y = hero_title(ott ? "OTT" : "COMP");
    s[0] = 0;
    text_cat(s, on ? M[m] : m == MB_OFF ? M[0] : ott ? "COMP ON" : "OTT ON", sizeof s);
    text_cat(s, on ? "|LIMITER" : "|ALGO: THIS ONE", sizeof s);
    hero_line(y, s);
}

APP_SMALL static void panel_mixer(int pan)
{
    hero_icon(IC_MIX, AC1);
    hero_line(hero_title(pan ? "PAN" : "MIXER"), "4 TRACKS");
    for (int t = 0; t < ENG_TRACKS; t++) {   /* a column of LEDs a track, in its colour */
        int lit = meter_lit(t), x = 180 + t * 15;
        char n[2] = {(char)('1' + t), 0};
        for (int k = 0; k < 7; k++) {
            uint16_t c = k == 6 ? AC3 : track_accent(t);
            cv_rect(x, 56 - k * 7, 9, 5, k < lit ? c : OFF);
        }
        cv_text(x + 4 - text_w(&FONT_T, n) / 2, 63, &FONT_T, n, DIM);
    }
}

static void panel_steps(int l, int ratch)  /* a lane's steps: how many, how many trigs; an effect's: held without;
                                           * (0.16.6) a track's ratchets' face: its ratchets */
{
    static const char *const FX[3] = {"DELAY STEPS", "REVERB STEPS", "WARPS STEPS"};
    char s[48], n[8];
    int len = (int)(eng_project()->val[PG_STEP(l) * 4 + 3] + 0.5f), trigs = 0;
    for (int i = 0; i < len; i++)
        trigs += eng_trig(l, i);
    hero_icon(IC_STEPS, lane_accent(l));
    s[0] = 0;
    if (ratch) {                         /* RATCHETS: so many hits over so many steps (the track's, not a lock's) */
        int sp, h = eng_ratch_hits((int)(eng_project()->val[PG_STEP(l) * 4 + 2] + 0.5f), &sp);
        int y = hero_title("RATCHETS");
        if (h <= 1) {
            text_cat(s, "ONE HIT A TRIG", sizeof s);
        } else {
            int_text(h, n);
            text_cat(s, n, sizeof s);
            text_cat(s, " HITS OVER ", sizeof s);
            int_text(sp, n);
            text_cat(s, n, sizeof s);
            text_cat(s, sp == 1 ? " STEP" : " STEPS", sizeof s);
        }
        hero_line(y, s);
        return;
    }
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

static void panel_climb(int t)           /* (0.16.7) CLIMB: how far a time (semitones, or the scale's notes), over how
                                          * many notes, on what */
{
    project_t *p = eng_project();
    char s[48], v[16], n[8];
    float c = p->val[PG_EUC(t) * 4 + 2];
    hero_icon(IC_STEPS, track_accent(t));
    int y = hero_title("CLIMB");
    s[0] = 0;
    if (c > -0.5f && c < 0.5f) {
        text_cat(s, "OFF|OR LOCKED ON A STEP", sizeof s);
    } else {
        static const char *const ON[CLON_N] = {"ON TRIGS", "ON HITS", "TRIG+HITS"};
        int i = 0, deg = 0;
        eng_param_text(PG_EUC(t) * 4 + 2, c, v, sizeof v);   /* ("+2 semitones", "+2 degrees": SCALE on) */
        while (v[i] && v[i] != ' ')
            i++;
        deg = v[i] == ' ' && v[i + 1] == 'd';
        v[i] = 0;
        text_cat(s, v, sizeof s);
        text_cat(s, deg ? " DEG|" : " ST|", sizeof s);
        int_text(eng_climb_opt(t, CL_TIMES), n);
        text_cat(s, n, sizeof s);
        text_cat(s, " NOTES|", sizeof s);
        text_cat(s, ON[eng_climb_opt(t, CL_ON)], sizeof s);
    }
    hero_line(y, s);
}

static void panel_random(int t)
{
    hero_icon(IC_DICE, track_accent(t));
    hero_line(hero_title("RANDOM"), "RIGHT: NEW|LEFT: LOOP");
}

APP_SMALL static void panel_endless(int t)         /* Endless: recording, or how many notes, the key's transposition */
{
    project_t *p = eng_project();
    char s[48], n[8];
    int len = p->endless_len[t], notes = 0;
    for (int i = 0; i < len; i++)
        notes += p->endless[t][i] != ENDLESS_REST;
    hero_icon(IC_ENDLESS, ui.erec == t ? AC3 : track_accent(t));
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

static void panel_arp(int t)             /* (0.16.13) Endless' ARP: what it walks, over how many octaves */
{
    char s[48], n[8];
    int m = eng_arp_opt(t, AO_MODE), oct = eng_arp_opt(t, AO_OCT);
    hero_icon(IC_ENDLESS, track_accent(t));
    int y = hero_title("ARP");
    s[0] = 0;
    if (m == AM_LOOP) {
        text_cat(s, "OFF|THE RECORDED NOTES", sizeof s);
    } else {
        text_cat(s, m == AM_KEYS ? "THE KEYS|" : "PADS' CHORD|", sizeof s);
        int_text(oct, n);
        text_cat(s, n, sizeof s);
        text_cat(s, oct > 1 ? " OCTAVES" : " OCTAVE", sizeof s);
        if (m == AM_KEYS && eng_arp_opt(t, AO_LATCH))
            text_cat(s, "|LATCHED", sizeof s);
    }
    hero_line(y, s);
}

static void panel_global(void)           /* the tempo, the scale, the swing */
{
    project_t *p = eng_project();
    char s[40], v[16];
    hero_icon(IC_GLO, AC1);
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

APP_SMALL static void panel_warps(void)            /* Warps: its algorithm, its marks, what it takes and where it goes */
{
    project_t *p = eng_project();
    int a = eng_warps_algo(), car = (int)(p->val[PG_WARPS(0) * 4] + 0.5f), mod = (int)(p->val[PG_WARPS(0) * 4 + 1] + 0.5f);
    float alg = eng_final(PG_WARPS(0) * 4 + 2);
    uint16_t ac = warps_accent();
    char s[48], v[16], u[16];
    hero_icon(IC_WARPS + (a < 0 ? clamp((int)(alg * 5.0f + 0.5f), 0, 5) : a), ac);
    int y = hero_title(eng_warps_name(a));
    if (a < 0) {
        hero_line(y, "OFF|K1: A CARRIER");
        return;
    }
    s[0] = 0;
    if (a != 6 || car < WC_OSC1) {       /* the carrier: a track, or the oscillator's shape */
        eng_param_text(PG_WARPS(0) * 4, (float)car, v, sizeof v);
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
    hero_icon(IC_REV, AC3);
    hero_line(hero_title("REVERB"), ui.page == PG_RSEND ? "FROM CLOUDS|THE SENDS" : "FROM CLOUDS");
}

static void panel_delay(void)            /* the delay: its time, its mode, its repeats */
{
    project_t *p = eng_project();
    char s[48], v[16];
    hero_icon(IC_DELAY, AC2);
    int y = hero_title("DELAY");
    s[0] = 0;
    if (ui.page == PG_DSEND)
        text_cat(s, "THE SENDS|", sizeof s);
    eng_param_text(PG_DELAY(0) * 4, eng_final(PG_DELAY(0) * 4), v, sizeof v);
    label_text(v, v, sizeof v);
    text_cat(s, v, sizeof s);
    if (p->algo[PG_DELAY(0)] == DL_FREE)  /* FREE: in ms, off the tempo */
        text_cat(s, ", FREE", sizeof s);
    if (ui.page != PG_DSEND) {
        text_cat(s, "|", sizeof s);
        enum_cat(s, PG_DELAY(1) * 4 + 1, sizeof s);
    }
    if (ui.page != PG_DSEND && p->algo[PG_DELAY(0)] != DL_FREE) {
        text_cat(s, "|FB ", sizeof s);
        eng_param_text(PG_DELAY(0) * 4 + 1, eng_final(PG_DELAY(0) * 4 + 1), v, sizeof v);
        text_cat(s, v, sizeof s);
    }
    hero_line(y, s);
}

/* a page's meter, alone (the rest is not drawn again for it): an effect's return (the delay's, the reverb's; how open
 * its lane holds it: the row at the bottom), the duck (how deep now), the master bus (the compressor's reduction, from
 * the top; OTT's three bands, each up or down from the middle) */
static void fx_meter(int force)
{
    static int shown = -1;
    int pt = ptype(ui.page), l = plane(ui.page), m;
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
            cv_rect(2, 0, 6, h[0], AC2);
        } else if (mode == MB_OTT) {     /* low, mid, high: up raised, down squashed (24 dB half the bar) */
            for (int b = 0; b < 3; b++) {
                cv_rect(b * 4, 0, 2, 52, TRACK);
                if (h[b] > 0)
                    cv_rect(b * 4, 26 - h[b], 2, h[b], AC3);
                else if (h[b] < 0)
                    cv_rect(b * 4, 26, 2, -h[b], AC1);
                cv_rect(b * 4, 25, 2, 2, HI);
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

static void panel_chord(void);
static void draw_chord_knob(int k, int force);
APP_SMALL static void draw_panel(void)
{
    project_t *p = eng_project();
    if (ui.cview) {                      /* (0.16.16) a step's notes */
        panel_chord();
        return;
    }
    int pt = ptype(ui.page), tr = ptrack(ui.page);
    int32_t s[3] = {ui.page, algo_page(ui.page) >= 0 ? p->algo[algo_page(ui.page)] : -1, 0};
    int fx = pt == PT_DELAY1 || pt == PT_DELAY2 || pt == PT_DSEND || pt == PT_REVERB || pt == PT_RSEND ||
             pt == PT_WARPS1 || pt == PT_WARPS2 || pt == PT_LANE || pt == PT_DUCK || pt == PT_COMP || pt == PT_OTT;
    if (pt == PT_COMP || pt == PT_OTT)
        s[1] = bus_mode();
    if (pt == PT_DUCK)
        s[2] = (int)(p->val[PG_DUCK * 4] * 1000.0f);
    switch (pt) {                        /* what the display shows, to draw it again only when it changes */
    case PT_PLAITS2: s[2] = (int)(eng_final(ui.page * 4) * 200.0f) | (int)(eng_final(ui.page * 4 + 1) * 8.0f) << 8 |
                            (int)(eng_final(ui.page * 4 + 2) * 8.0f + 8.0f) << 12; break;   /* (the FM: SHAPE) */
    case PT_PLAITS3: s[2] = eng_note(tr) | (int)((eng_gates() >> tr) & 1u) << 8 |
                            (int)(eng_final(ui.page * 4) * 50.0f + 50.0f) << 9 | (int)(eng_final(ui.page * 4 + 1) * 100.0f) << 16;
        break;
    case PT_LFO:
        s[2] = (int)p->val[ui.page * 4] | (int)p->val[ui.page * 4 + 2] << 5 |
               (int)(eng_final(ui.page * 4 + 1) * 1000.0f) << 8;
        break;
    case PT_FOLLOW:                      /* (its source, its gain and threshold, its mode) */
        s[2] = (int)p->val[ui.page * 4] | (int)(p->val[ui.page * 4 + 1] * 50.0f + 50.0f) << 2 |
               (int)(p->val[ui.page * 4 + 2] * 60.0f) << 10 | (int)p->val[PG_LFO(lfo_of(ui.page)) * 4 + 2] << 17;
        break;
    case PT_STEP: case PT_LANE: case PT_PLAY: {
        int l = plane(ui.page);
        for (int i = 0; i < ENG_STEPS / 8; i++)
            s[2] = s[2] * 31 + p->trig[l][i];
        s[2] = s[2] * 7 + 11 * (int)p->val[PG_STEP(l) * 4 + 3] + (l == LANE_WARPS ? eng_warps_algo() * 3 : 0);
        s[2] = s[2] * 263 + (pt == PT_PLAY ? 1 + (int)p->val[PG_STEP(l) * 4 + 2] : 0);   /* (the ratchets' face) */
        break;
    }
    case PT_CLIMB:                       /* (CLIMB, its options, the scale: semitones or its notes) */
        s[2] = (int)(p->val[PG_EUC(tr) * 4 + 2] * 2.0f) + 64 + (int)p->endless_opt[tr][3] * 256 +
               (eng_quantize(61) != 61 || eng_quantize(66) != 66) * 65536 + (int)p->val[PG_GLOBAL * 4 + 1] * 131072;
        break;
    case PT_DELAY1: case PT_DELAY2: case PT_DSEND:
        s[2] = (int)p->val[PG_DELAY(0) * 4] | (int)p->val[PG_DELAY(1) * 4 + 1] << 5 |
               (int)(eng_final(PG_DELAY(0) * 4 + 1) * 100.0f) << 6;
        break;
    case PT_ARP3:
        s[2] = p->arp_opt[tr];
        break;
    case PT_ARP:
        s[2] = p->endless_len[tr] | (p->endless_key[tr] + 64) << 7 | (ui.erec == tr) << 14;
        for (int i = 0; i < p->endless_len[tr]; i++)
            s[2] = s[2] * 31 + (p->endless[tr][i] == ENDLESS_REST);
        break;
    case PT_GLOBAL:
        s[2] = (int)p->val[PG_GLOBAL * 4 + 1] | (int)p->val[PG_GLOBAL * 4 + 2] << 5 |   /* (0.16.12: 18 scales) */
               (int)(p->val[PG_GLOBAL * 4 + 3] * 100.0f) << 9 | (int)(p->val[PG_GLOBAL * 4] * 1000.0f) << 16;
        break;
    case PT_BEND:
        for (int k = 0; k < 4; k++)
            s[2] = s[2] * 131 + (int)(eng_final(ui.page * 4 + k) * 100.0f);
        break;
    case PT_GLITCH: {                    /* (its knobs, and the glitch as it plays: taking, repeating, how many) */
        int reps = 0;
        float sp = 1.0f;
        int st = eng_glitch_state(tr, &reps, &sp);
        for (int k = 0; k < 4; k++)
            s[2] = s[2] * 131 + (int)(eng_final(ui.page * 4 + k) * 100.0f);
        s[2] = s[2] * 7 + st + 3 * (reps & 15);
        break;
    }
    case PT_SON2: {
        int pg = PG_TRACK(tr, 2);
        s[2] = eng_note(tr) | (int)((eng_gates() >> tr) & 1u) << 8 | (int)(eng_final(pg * 4) * 50.0f + 50.0f) << 9;
        for (int k = 1; k < 4; k++)
            s[2] = s[2] * 131 + (int)(eng_final(pg * 4 + k) * 100.0f);
        break;
    }
    case PT_ENV2:                        /* the filter's envelope: its knobs, where it is */
        s[2] = p->algo[PG_FILT(tr, 0)] | (int)(eng_filter_env(tr) * 24.0f) << 3;
        for (int k = 0; k < 12; k++)
            s[2] = s[2] * 131 + (int)(eng_final(PG_FILT(tr, k / 4) * 4 + k % 4) * 100.0f);
        break;
    case PT_VFLT1: case PT_VFLT2:        /* its model, type, knobs and the cutoff as it moves (its envelope, FM) */
        s[2] = p->algo[PG_FILT(tr, 0)];
        for (int k = 0; k < 12; k++)
            s[2] = s[2] * 131 + (int)(eng_final(PG_FILT(tr, k / 4) * 4 + k % 4) * 100.0f);
        s[2] = s[2] * 7 + (int)(fm_log2f(fm_maxf(eng_filter_hz(tr), 10.0f)) * 12.0f);
        s[2] = s[2] * 11 + (int)(eng_filter_load(tr) / 5u);
        break;
    case PT_EUC: {
        int l = eng_page_lane(ui.page);
        for (int i = 0; i < ENG_STEPS / 8; i++)
            s[2] = s[2] * 31 + p->trig[l][i];
        s[2] = s[2] * 7 + 11 * (int)p->val[PG_STEP(l) * 4 + 3] + 1001 * (int)p->val[PG_EUC(l) * 4] +
               37 * (int)p->val[PG_EUC(l) * 4 + 1] + 100003 * (eng_playing() ? eng_step_now(l) : -1);
        break;
    }
    case PT_EQ:
        for (int t = 0; t < ENG_TRACKS; t++)
            s[2] = s[2] * 131 + (int)(eng_final(PG_EQ * 4 + t) * 200.0f) + (int)track_accent(t);
        s[2] = s[2] * 7 + ui.track;
        break;
    case PT_SPREAD:                      /* (each track's width now, where it sits, its colour) */
        for (int t = 0; t < ENG_TRACKS; t++)
            s[2] = s[2] * 131 + (int)(eng_spread_width(t) * 60.0f) * 97 + (int)(eng_now(PG_PAN * 4 + t) * 60.0f) +
                   (int)track_accent(t);
        s[2] = s[2] * 7 + ui.track;
        break;
    case PT_MIX: case PT_PAN:
        for (int t = 0; t < ENG_TRACKS; t++)
            s[2] |= meter_lit(t) << (t * 3) | eng_model_bank(p->algo[PG_TRACK(t, 0)]) << (12 + t * 3);
        break;
    case PT_WARPS1: case PT_WARPS2:
        s[2] = (int)(eng_final(PG_WARPS(0) * 4 + 2) * 64.0f) | (int)p->val[PG_WARPS(0) * 4] << 7 |
               (int)p->val[PG_WARPS(0) * 4 + 1] << 11 | (int)p->val[PG_WARPS(1) * 4 + 3] << 13;
        break;
    default: break;
    }
    if ((pt == PT_PLAITS1 || pt == PT_PLAITS2 || pt == PT_PLAITS3 || pt == PT_SON2) && eng_fm_algo(tr) >= 0)
        s[1] = s[1] * 9 + 1 + eng_fm_algo(tr);   /* (the FM: its algorithm's drawing) */
    if ((pt == PT_PLAITS1 || pt == PT_PLAITS2 || pt == PT_PLAITS3) && track_kind(tr) == MK_SQ1)
        s[1] = s[1] * 37 + 2 + eng_sq_cycle(tr, 0, 0, 0);   /* (OTIS: the cycle it reads) */
    if (tr >= 0 && tr == eng_pads_track()) {   /* (PADS: its progression's place, its chord) */
        char pl[40];
        eng_pads_line(pl, sizeof pl);
        s[1] = s[1] * 31 + text_hash(pl);
    }
    if (!ui.full && !ui.body_panel && !memcmp(s, ui.panel_shown, sizeof s)) {
        if (fx)
            fx_meter(0);
        if (pt == PT_LFO || pt == PT_FOLLOW)
            lfo_scope(0);
        return;
    }
    memcpy(ui.panel_shown, s, sizeof s);
    cv_begin(240, PANEL_H, BG);
    switch (pt) {
    case PT_PLAITS1: case PT_PLAITS3: panel_plaits(tr, 0, page_accent(ui.page)); break;
    case PT_PLAITS2:                     /* (an FM drum's knobs: its name; the FM's: its SHAPE) */
        panel_plaits(tr, drum_track(tr) && eng_fm_algo(tr) < 0 ? 0 : 1, page_accent(ui.page));
        break;
    case PT_SON2: panel_plaits(tr, 2, page_accent(ui.page)); break;
    case PT_ENV2: panel_env2(tr, page_accent(ui.page)); break;
    case PT_LFO: case PT_FOLLOW: panel_lfo(lfo_of(ui.page)); break;
    case PT_MIX: case PT_PAN: panel_mixer(pt == PT_PAN); break;
    case PT_SPREAD: panel_spread(); break;
    case PT_EQ: panel_eq(); break;
    case PT_BEND: panel_bend(tr); break;
    case PT_GLITCH: panel_glitch(tr); break;
    case PT_VFLT1: case PT_VFLT2: panel_filter(tr, page_accent(ui.page)); break;
    case PT_EUC: panel_euclid(eng_page_lane(ui.page)); break;
    case PT_DUCK: panel_duck(); break;
    case PT_COMP: case PT_OTT: panel_bus(pt == PT_OTT); break;
    case PT_STEP: case PT_LANE: case PT_PLAY:
        panel_steps(plane(ui.page), pt == PT_PLAY && plane(ui.page) < ENG_TRACKS);
        break;
    case PT_CLIMB: panel_climb(tr); break;
    case PT_RND: panel_random(tr); break;
    case PT_ARP: case PT_ARP2: panel_endless(tr); break;
    case PT_ARP3: panel_arp(tr); break;
    case PT_GLOBAL: panel_global(); break;
    case PT_WARPS1: case PT_WARPS2: panel_warps(); break;
    case PT_DELAY1: case PT_DELAY2: case PT_DSEND: panel_delay(); break;
    default: panel_reverb(); break;
    }
    blit(0, PANEL_Y);
    if (fx)
        fx_meter(1);
    if (pt == PT_LFO || pt == PT_FOLLOW)
        lfo_scope(1);
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
    if (page < 12 && page % 3 == 1 && k == 1)    /* (an FM drum's: one of its own) */
        return eng_model_enveloped(eng_project()->algo[page - 1]) && !eng_model_own(eng_project()->algo[page - 1]);
    if (page == PG_COMP || page == PG_OTT)
        return bus_mode() != (page == PG_OTT ? MB_OTT : MB_COMP);
    if (page >= PG_FILT(0, 0) && page <= PG_FILT(ENG_TRACKS - 1, 2))   /* the filter OFF: its knobs */
        return eng_project()->algo[PG_FILT((page - PG_FILT(0, 0)) / 3, 0)] == MF_OFF;
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
        {"DIGITAL", "DIGI"}, {"COMPAR", "COMP"}, {"2 FWD 1 BACK", "+2-1"}};
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
    return pt == PT_MIX || pt == PT_PAN || pt == PT_SPREAD || pt == PT_RSEND || pt == PT_DSEND || pt == PT_DUCK ||
           pt == PT_EQ;
}

/* (0.16) the knobs on the project's bytes, not parameters (no lock, no random, no cable): a lane's SPEED and DIRECTION
 * (0.16.6: a track's SPEED on its steps' first face, RATCH's place, its DIRECTION last on the ratchets' face; an
 * effect's both first on its PLAY face), Endless' second face's four (0.16.2). A choice (n > 0): a ring in as many
 * parts; a share (n = 0): 0..100 */
static int play_byte(int page, int k)    /* 0 SPEED, 1 DIRECTION, -1 neither */
{
    int pt = ptype(page);
    if (pt == PT_STEP)
        return k == 2 ? 0 : -1;
    if (pt != PT_PLAY)
        return -1;
    if (plane(page) < ENG_TRACKS)
        return k == 3 ? 1 : -1;
    return k < 2 ? k : -1;
}
static int byte_knob(int page, int k)
{
    return play_byte(page, k) >= 0 || ptype(page) == PT_ARP2 || ptype(page) == PT_ARP3 || (ptype(page) == PT_CLIMB && k > 0);
}
static int byte_value(int page, int k, int *n, char *txt, uint32_t max, const char **name)   /* its value (a choice's
                                                                                               * index) */
{
    project_t *p = eng_project();
    int v;
    txt[0] = 0;
    int b = play_byte(page, k);
    if (b >= 0) {
        int l = plane(page);
        *n = b == 0 ? ENG_SPEEDS : DIR_N;
        v = clamp(b == 0 ? p->speed[l] + 4 : p->dir[l], 0, *n - 1);
        if (b == 0)                      /* (x1/2: its x lower case, a multiplier's) */
            text_cat(txt, eng_speed_name(v - 4), max);
        else
            label_text(eng_dir_name(v), txt, max);
        *name = b == 0 ? "Speed" : "Dir";
        return v;
    }
    if (ptype(page) == PT_ARP3) {        /* (0.16.13) the ARP: MODE, OCTAVES, LATCH, ORDER */
        static const char *const N3[4] = {"Mode", "Octaves", "Latch", "Order"};
        int o = eng_arp_opt(ptrack(page), k);
        *name = N3[k];
        if (k == 1) {
            char d[4];
            *n = 4;
            int_text(o, d);
            text_cat(txt, d, max);
            return o - 1;
        }
        *n = k == 0 ? AM_N : 2;
        label_text(k == 0 ? eng_arp_mode_name(o) : k == 2 ? (o ? "On" : "Off") : (o ? "Played" : "Pitch"), txt, max);
        return o;
    }
    if (ptype(page) == PT_CLIMB) {       /* (0.16.7) CLIMB's options: NOTES (2..16), ON, SHAPE */
        int t = ptrack(page), o = eng_climb_opt(t, k - 1);
        char d[8];
        if (k == 1) {
            *n = 15;
            int_text(o, d);
            text_cat(txt, d, max);
            *name = "Notes";
            return o - 2;
        }
        *n = k == 2 ? CLON_N : CLSH_N;
        label_text(k == 2 ? eng_climb_on_name(o) : eng_climb_shape_name(o), txt, max);
        *name = k == 2 ? "On" : "Shape";
        return o;
    }
    static const char *const N[4] = {"Stride", "Rest", "Holes", "Leap"};
    int t = ptrack(page), o = eng_endless_opt(t, k);
    char d[8];
    *name = N[k];
    if (k == EO_STRIDE || k == EO_REST) {   /* one note in so many (the others over), one step in so many a rest */
        *n = k == EO_STRIDE ? 7 : 8;
        v = k == EO_STRIDE ? o - 1 : (o ? o - 1 : 0);
        if (k == EO_STRIDE && o == 1)
            text_cat(txt, "ALL", max);
        else if (k == EO_REST && !o)
            text_cat(txt, "OFF", max);
        else
            int_text(o, d), text_cat(txt, "1/", max), text_cat(txt, d, max);
        return v;
    }
    *n = 0;                              /* HOLES, LEAP: a share */
    int_text(o, d);
    text_cat(txt, d, max);
    text_cat(txt, "%", max);
    return o;
}

static void draw_play_knob(int page, int k, int force)
{
    int n, i;
    const char *name;
    int hot = page == ui.page && ui.touched_k == k && plat_ms() - ui.touched_at[k] < 1200u;
    uint16_t ac = page_accent(page);
    char txt[24], lab[24], under[24];
    under[0] = 0;
    i = byte_value(page, k, &n, txt, sizeof txt, &name);
    label_text(name, lab, sizeof lab);
    int32_t s[4] = {i | k << 8 | 1 << 22, text_hash(txt) * 31 + text_hash(lab), 0x5EED, hot | (int32_t)ac << 2};
    if (!force && !ui.full && !ui.body_knobs && !memcmp(s, ui.knob_shown[k], sizeof s))
        return;
    if (page == ui.page)
        memcpy(ui.knob_shown[k], s, sizeof s);
    if (slide_s && (k * KNOB_W + slide_s >= 240 || k * KNOB_W + KNOB_W + slide_s <= 0))
        return;
    cv_begin(KNOB_W, KNOB_H, BG);
    {
        int w = text_w(&FONT_S, lab);
        cv_text(30 - w / 2, 0, &FONT_S, lab, hot ? HI : MUTED);
        if (hot)
            cv_rect(30 - w / 2, 17, w, 1, ac);
    }
    for (int j = 0; j < n; j++) {        /* (x1 lit apart: the middle of SPEED's travel) */
        float gap = n > 6 ? 0.008f : 0.014f, a = (float)j / (float)n + gap, b = (float)(j + 1) / (float)n - gap;
        if (j == i)
            lit_arc(RX, RY, R0, R1, a, b, ac);
        else
            arc(RX, RY, R0 + 0.5f, R1 - 0.5f, a, b, play_byte(page, k) == 0 && j == 4 ? MUTED : TRACK);
    }
    if (!n) {                            /* a share: lit from 0 */
        float u = (float)i / 100.0f;
        arc(RX, RY, R0 + 0.5f, R1 - 0.5f, 0.0f, 1.0f, TRACK);
        if (u > 0.004f)
            lit_arc(RX, RY, R0, R1, 0.0f, u, ac);
        ring_end(u, ac, 1);
    }
    ring_text(ring_short(txt), i || n ? HI : DIM, under, sizeof under);
    if (under[0])
        cv_text(30 - text_w(&FONT_XS, under) / 2, 78, &FONT_XS, under, MUTED);
    blit((uint32_t)(k * KNOB_W), KNOB_Y);
}

/* (0.16.4) a step's ratchets: RATCH (its hits) and SPAN (the steps they take), both in RATCH's parameter (engine.h
 * eng_ratch_join), each turned and drawn alone: 1 the hits, 2 the span, 0 neither (0.16.6: the two first on the
 * ratchets' face, the steps' second) */
static int ratch_field(int page, int k)
{
    int t = ptrack(page);
    if (t < 0 || ptype(page) != PT_PLAY || k > 1)
        return 0;
    return k + 1;
}

static float ratch_moved(float v, int field, int32_t e)   /* its hits or its span a step on (1..16), the other kept */
{
    int sp, h = eng_ratch_hits((int)(v + 0.5f), &sp), d = e > 0 ? 1 : -1;
    if (field == 1)
        h = clamp(h + d, 1, 16);
    else
        sp = clamp(sp + d, 1, 16);
    return (float)eng_ratch_join(h, sp);
}

static void ratch_turn(int field, int param, int32_t e)
{
    project_t *p = eng_project();
    int t, tp = eng_param_tp(param, &t);
    if (ui.held_step >= 0) {             /* a held step: its lock */
        float v;
        if (t != ui.lane) {
            app_say("LOCK:", "THIS LANE'S KNOBS");
            return;
        }
        if (!eng_lock(t, ui.held_step, tp, &v))
            v = p->val[param];
        if (eng_lock_set(t, ui.held_step, tp, ratch_moved(v, field, e)) != 0)
            app_say("LOCKS", "ALL USED");
        ui.step_edit = 1;
    } else {                             /* the track's (GLO held: the four's) */
        for (int m = 0; m < ENG_TRACKS; m++)
            if (m == t || held(B_GLO)) {
                float *v = &p->val[PG_STEP(m) * 4 + 2];
                *v = ratch_moved(*v, field, e);
            }
        if (held(B_GLO))
            ui.used[B_GLO] = 1;
    }
    dirty = 1;
}

static void draw_ratch_knob(int page, int k, int field, int force)   /* RATCH or SPAN: its value (a lock's), 1..16 */
{
    project_t *p = eng_project();
    int param = knob_param(page, k), t, tp, view = knob_view(page, param, &t, &tp), locked = 0;
    int hot = page == ui.page && ui.touched_k == k && plat_ms() - ui.touched_at[k] < 1200u;
    float v = p->val[param];
    uint16_t ac = page_accent(page), mc = mod_colour(ac), vc;
    char txt[16], lab[16], under[16];
    if (view == KV_LOCK)
        locked = eng_lock(t, ui.held_step, tp, &v);
    int sp, h = eng_ratch_hits((int)(v + 0.5f), &sp), x = field == 1 ? h : sp;
    vc = locked ? mc : ac;
    txt[0] = under[0] = 0;
    if (field == 1)
        text_cat(txt, "x", sizeof txt);
    {
        char d[8];
        int_text(x, d);
        text_cat(txt, d, sizeof txt);
    }
    label_text(field == 1 ? "Ratch" : "Span", lab, sizeof lab);
    text_cat(under, locked ? "LOCK" : field == 2 ? (x == 1 ? "STEP" : "STEPS") : "", sizeof under);
    int32_t s[4] = {x | field << 8 | locked << 10 | view << 12 | 1 << 21, text_hash(txt) * 31 + text_hash(lab), 0x5BA9,
                    hot | (int32_t)ac << 2};
    if (!force && !ui.full && !ui.body_knobs && !memcmp(s, ui.knob_shown[k], sizeof s))
        return;
    if (page == ui.page)
        memcpy(ui.knob_shown[k], s, sizeof s);
    if (slide_s && (k * KNOB_W + slide_s >= 240 || k * KNOB_W + KNOB_W + slide_s <= 0))
        return;
    cv_begin(KNOB_W, KNOB_H, BG);
    {
        int w = text_w(&FONT_S, lab);
        cv_text(30 - w / 2, 0, &FONT_S, lab, hot ? HI : locked ? vc : MUTED);
        if (hot)
            cv_rect(30 - w / 2, 17, w, 1, ac);
    }
    {
        float u = (float)(x - 1) / 15.0f;
        arc(RX, RY, R0 + 0.5f, R1 - 0.5f, 0.0f, 1.0f, TRACK);
        if (u > 0.004f)
            lit_arc(RX, RY, R0, R1, 0.0f, u, vc);
        ring_end(u, vc, 1);
    }
    ring_text(txt, HI, under, sizeof under);
    if (under[0])
        cv_text(30 - text_w(&FONT_XS, under) / 2, 78, &FONT_XS, under, locked ? vc : MUTED);
    blit((uint32_t)(k * KNOB_W), KNOB_Y);
}

static void play_turn(int k, int32_t e)  /* a byte knob turned (byte_knob): the lane's, the track's (GLO held: the
                                          * four tracks') */
{
    project_t *p = eng_project();
    int d = e > 0 ? 1 : -1;
    if (ptype(ui.page) == PT_ARP3) {     /* (0.16.13) the ARP's: a step each, the track's (GLO held: the four's) */
        int t = ptrack(ui.page), v = eng_arp_opt(t, k) + d;
        v = k == 0 ? clamp(v, 0, AM_N - 1) : k == 1 ? clamp(v, 1, 4) : clamp(v, 0, 1);
        for (int m = 0; m < ENG_TRACKS; m++)
            if (m == t || held(B_GLO))
                eng_arp_opt_set(p, m, k, v);
    } else if (ptype(ui.page) == PT_CLIMB) {   /* (0.16.7) CLIMB's options: a step each, the track's (GLO held: the
                                          * four's) */
        int t = ptrack(ui.page), o = k - 1, v = eng_climb_opt(t, o) + d;
        v = o == CL_TIMES ? clamp(v, 2, 16) : clamp(v, 0, (o == CL_ON ? CLON_N : CLSH_N) - 1);
        for (int m = 0; m < ENG_TRACKS; m++)
            if (m == t || held(B_GLO))
                eng_climb_opt_set(p, m, o, v);
    } else if (ptype(ui.page) == PT_ARP2) {   /* Endless' options: a step each (HOLES, LEAP: 2 %, 10 turned fast) */
        int t = ptrack(ui.page), o = eng_endless_opt(t, k), v;
        if (k == EO_STRIDE)
            v = clamp(o + d, 1, 7);
        else if (k == EO_REST)
            v = o == 0 ? (d > 0 ? 2 : 0) : o + d < 2 ? 0 : clamp(o + d, 2, 8);
        else
            v = clamp(o + (int)e * (e > 1 || e < -1 ? 10 : 2), 0, 100);
        for (int m = 0; m < ENG_TRACKS; m++)
            if (m == t || held(B_GLO))
                eng_endless_opt_set(p, m, k, v);
    } else {
        int l = plane(ui.page), b = play_byte(ui.page, k);
        int v = b == 0 ? clamp(p->speed[l] + d, -4, 4) : clamp((int)p->dir[l] + d, 0, DIR_N - 1);
        for (int m = 0; m < ENG_LANES; m++) {
            if (m != l && !(held(B_GLO) && l < ENG_TRACKS && m < ENG_TRACKS))
                continue;
            if (b == 0)
                p->speed[m] = (int8_t)v;
            else
                p->dir[m] = (uint8_t)v;
        }
    }
    if (held(B_GLO))
        ui.used[B_GLO] = 1;
    dirty = 1;
    ui.touched_at[k] = plat_ms();
    ui.touched_k = k;
}

static void draw_knob_at(int page, int k, int force)
{
    project_t *p = eng_project();
    if (ui.cview && page == ui.page) {   /* (0.16.16) a step's notes */
        draw_chord_knob(k, force);
        return;
    }
    if (byte_knob(page, k)) {
        draw_play_knob(page, k, force);
        return;
    }
    if (ratch_field(page, k) && !held(B_ENV)) {   /* RATCH, SPAN (ENV held: their random, as any knob's) */
        draw_ratch_knob(page, k, ratch_field(page, k), force);
        return;
    }
    int param = knob_param(page, k), t, tp, view = knob_view(page, param, &t, &tp), locked = 0, rnd = 0;
    const eng_pdef_t *d = eng_pdef(param);
    float v = p->val[param], f = eng_final(param), u, uf;
    char txt[32], c[32], lab[24], under[32];
    int hash = 0, hot = page == ui.page && ui.touched_k == k && plat_ms() - ui.touched_at[k] < 1200u, ncab = 0;
    int bip = eng_param_centered(param);   /* (0.16.16: a machine's own say) */
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
        if (p->dice && eng_dice_span(param) != 0.0f) {   /* the random dose: where it takes it, the value this trig
                                                          * (a track's) or this beat (an effect's) threw */
            f = eng_now(param);
            if (!ncab) {
                c[0] = 0;
                text_cat(c, "RANDOM", sizeof c);
                ncab = 1;
                hash = 0x5EED;
            }
        }
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
        label_text(knob_name(page, k, param), lab, sizeof lab);
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
        uint16_t lc = hot ? HI : view != KV_VALUE ? vc : tk ? track_accent(k) : MUTED;
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
        ring_text(ring_short(nm), idle ? DIM : HI, under, sizeof under);
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
        ring_text(txt, ncab ? mc : (view == KV_RND && !rnd) || idle ? DIM : HI, under, sizeof under);
    }
    if (ncab)                            /* under the ring: the cables, a lock, a random, else a unit, MOD's socket */
        cv_text(30 - text_w(&FONT_XS, c) / 2, 78, text_w(&FONT_XS, c) <= 58 ? &FONT_XS : &FONT_T, c, mc);
    else if (under[0])
        cv_text(30 - text_w(&FONT_XS, under) / 2, 78, &FONT_XS, under, view != KV_VALUE ? vc : MUTED);
    else if (ui.mod && eng_param_mod(param))
        hoop(30.0f, 85.0f, 4.5f, 1.4f, DIM);
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
        cv_text(10, (h - 15) / 2, &FONT_XS, lines[i], i == sel ? HI : colours[i]);
        blit(0, (uint32_t)(y + i * h));
    }
}

APP_SMALL static void draw_matrix(void)
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
    cv_text(10, 0, &FONT_XS, g < ENG_TRACKS ? "THE TRACK'S CABLES" : "GLOBAL CABLES (THE LFOS, THE EFFECTS)", TXT);
    cv_text(10, 12, &FONT_T, "K1 CABLE   K2 SOURCE   K3 TARGET   K4 DEPTH", DIM);
    blit(0, PANEL_Y);
    draw_lines(PANEL_Y + 24, 19, lines, col, 8, ui.msel - first);
}

APP_SMALL static void draw_projects(void)          /* the projects REC held loads, in the module's display */
{
    static int shown = -1;
    static const char *const P[5] = {"New: 4 FM, nothing patched", "The demo groove", "The saved project",
                                     "Stress test (plays at once)", "Stress test, on the 4 FM drums"};
    if (ui.full || ui.body_panel || shown != ui.init_sel) {
        shown = ui.init_sel;
        cv_begin(240, PANEL_H, BG);
        cv_text(10, 0, &FONT_B, "PROJECT", TXT);
        cv_text(232 - text_w(&FONT_T, "K1 CHOOSE, REC HELD LOADS"), 3, &FONT_T, "K1 CHOOSE, REC HELD LOADS", DIM);
        for (int i = 0; i < 5; i++) {    /* (five rows of 12 px under the title) */
            int y = 15 + i * 12, sel = i == ui.init_sel;
            if (sel)
                pill(4, y, 232, 12, SEL);
            disc(16.0f, (float)y + 6.0f, 5.0f, i < 3 ? accent3(i) : KNOCK);
            vicon(16.0f, (float)y + 6.0f, 0.27f, 1.3f, BG, i < 4 ? IC_NEW + i : IC_STRESS, 0);
            cv_text(30, y - 2, &FONT_XS, P[i], sel ? HI : TXT);
        }
        blit(0, PANEL_Y);
    }
}

APP_SMALL static void draw_machine(int force)      /* the machine, where the knobs are */
{
    static uint32_t at;
    char v[32];
    if (!force && !ui.full && !ui.body_knobs && plat_ms() - at < 400u)
        return;
    at = plat_ms();
    project_t *p = eng_project();
    for (int i = 0; i < 7; i++) {        /* seven rows, 14 px apart, in three canvases (a 240 x 100 one is more than
                                          * gfx.c's canvas holds): 43, 43 and 14 px */
        const char *l = "";
        uint16_t c = TXT;
        int y = i < 3 ? 1 + i * 14 : i < 6 ? (i - 3) * 14 : 0;
        if (i % 3 == 0) {
            cv_begin(240, i < 6 ? 43 : 14, BG);
            if (!i)
                cv_rect(8, 0, 224, 1, RULE);
        }
        v[0] = 0;
        switch (i) {
        case 0: {                        /* the cores' loads */
            char n[8];
            l = "Cores 0 and 1";
            int_text((int32_t)((eng_load(0) + 5u) / 10u), v);
            text_cat(v, "%  ", sizeof v);
            if (eng_core1_running()) {
                int_text((int32_t)((eng_load(1) + 5u) / 10u), n);
                text_cat(v, n, sizeof v);
                text_cat(v, "%", sizeof v);
            } else {
                text_cat(v, cpu2_ok ? "paused" : "off", sizeof v);
                c = AC2;
            }
            text_cat(v, "  prep ", sizeof v);   /* core 0's preparation of a block (the sequencer, the LFOs, the cables) */
            int_text((int32_t)((eng_prep_load() + 5u) / 10u), n);
            text_cat(v, n, sizeof v);
            text_cat(v, "%", sizeof v);
            break;
        }
        case 1:
            l = "Peak over a second";
            v[0] = 0;
            if (eng_guard()) {           /* the CPU guard at work: the filters' FM lightened */
                char n[4] = {'G', (char)('0' + eng_guard()), ' ', 0};
                text_cat(v, n, sizeof v);
            }
            {
                char n[8];
                int_text((int32_t)((eng_peak() + 5u) / 10u), n);
                text_cat(v, n, sizeof v);
            }
            text_cat(v, "%", sizeof v);
            if (eng_peak() >= 900u)
                c = AC3;
            break;
        case 2: {                        /* what each costs: the four tracks (while they play), Warps, the mix, the
                                          * effects and the master bus (the cores share them by it) */
            char n[8];
            l = "T1 T2 T3 T4 Warps FX";
            for (int t = 0; t <= ENG_TRACKS + 1; t++) {
                if (t == ENG_TRACKS && !eng_task_load(t))
                    n[0] = '-', n[1] = 0;         /* Warps off */
                else
                    int_text((int32_t)((eng_task_load(t) + 5u) / 10u), n);
                text_cat(v, n, sizeof v);
                text_cat(v, t <= ENG_TRACKS ? "  " : "%", sizeof v);
            }
            break;
        }
        case 3: {                        /* the output (K2): stereo; a headset with a mic (its 4-pole plug in the
                                          * 3-pole jack: its earpieces in series between left and right, they get
                                          * left minus right, so the right inverted gives them the whole mix); mono */
            static const char *const O[3] = {"Stereo", "Mic headset", "Mono"};
            l = "Output (K2)";
            text_cat(v, O[p->out_mode % 3], sizeof v);
            c = p->out_mode ? AC2 : TXT;
            break;
        }
        case 4:                          /* Warps (K3): ECO, its cross-modulations at 3x (half the work); HQ, 6x */
            l = "Warps (K3)";
            text_cat(v, p->warps_hq ? "HQ, 6x" : "Eco, 3x", sizeof v);
            c = p->warps_hq ? AC2 : TXT;
            break;
        case 5:                          /* the colours (K4) */
            l = "Colours (K4)";
            text_cat(v, th->name, sizeof v);
            break;
        default:
            l = dirty ? "MIFM1 " APP_VERSION ", not saved" : "MIFM1 " APP_VERSION;
            text_cat(v, data_ok ? "tables OK" : "tables MISSING", sizeof v);
            if (!data_ok)
                c = AC3;
            break;
        }
        cv_text(10, y, &FONT_XS, l, MUTED);
        int32_t x = 230 - text_w(&FONT_XS, v);
        cv_text(x, y, &FONT_XS, v, c);
        if (i == 5)                      /* the theme's three colours before its name */
            for (int j = 0; j < 3; j++)
                disc((float)(x - 38 + j * 11), (float)y + 6.5f, 4.0f, j == 0 ? AC1 : j == 1 ? AC2 : AC3);
        if (i % 3 == 2 || i == 6)
            blit(0, (uint32_t)(KNOB_Y + (i / 3) * 43));
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
        col[i] = i == 1 ? AC3 : TXT;
    draw_lines(PANEL_Y + 4, 19, L, col, 8, -1);
}

/* the row at the bottom: the four tracks (an LED lit by its gate or its trig, in its model's colour); on the LFOs'
 * pages the three LFOs, lit as high as they are; the steps of the lane in STEP mode, and on the steps' and the effects'
 * pages theirs. Its rule lit as far as the random dose goes (OCT- held, SELECT), TRAUMA's under it (OCT+ held) */
static int foot_lane(void)               /* the lane whose steps the row shows, -1 none */
{
    int pt = ptype(ui.page);
    if (ui.grid)
        return ui.lane;
    if (pt == PT_PLAY || pt == PT_CLIMB)
        return plane(ui.page);
    if (pt == PT_STEP || pt == PT_RND || pt == PT_EUC)
        return eng_page_lane(ui.page) >= 0 ? eng_page_lane(ui.page) : ptrack(ui.page);
    if (ui.page < ENG_NPAGES && eng_page_lane(ui.page) >= ENG_TRACKS)
        return eng_page_lane(ui.page);
    return -1;
}

static int foot_mode(void)
{
    if (ui.matrix || ui.page == VP_SYS)
        return 0;
    if (ui.erec >= 0 || ((ptype(ui.page) == PT_ARP || ptype(ui.page) == PT_ARP2 || ptype(ui.page) == PT_ARP3) &&
                         !ui.grid))
        return 6;
    if (foot_lane() >= 0)
        return 5;
    switch (ptype(ui.page)) {
    case PT_LFO: case PT_FOLLOW: return 1;
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


/* (0.16.15) the steps of the page shown that the track's trigs hold (their lengths), a bit each */
static __attribute__((noinline)) uint32_t held_steps(int t, int len)
{
    uint8_t tie[ENG_STEPS];
    uint32_t m = 0;
    eng_step_ties(t, tie);
    for (int s0 = 0; s0 < len && s0 < ENG_STEPS; s0++)
        for (int q = s0 + 1; tie[s0] && q <= s0 + tie[s0] && eng_trig(t, s0); q++)
            if ((uint32_t)(q % len - ui.spage * 16) < 16u)
                m |= 1u << (q % len - ui.spage * 16);
    return m;
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
        cv_rect(231, 8, 3, 18, HI);
        return;
    }
    uint8_t chords[ENG_STEPS];
    if (t < ENG_TRACKS)
        eng_step_chords(t, chords);
    for (int i = 0; i < 16; i++) {
        int st = ui.spage * 16 + i, x = 9 + i * 13;
        if (st >= len) {
            cv_rect(x + 4, 16, 3, 3, RULE);
            continue;
        }
        if (st == ui.held_step || (ui.cview && t == ui.clane && st == ui.cstep))
            cv_rect(x, 8, 11, 18, HI);
        else if (eng_trig(t, st))
            cv_rect(x, 8, 11, 18, st == now ? HI : ac);
        else
            cv_rect(x, 8, 11, 18, st == now ? MUTED : TRACK);
        if (eng_trig(t, st) && st == now)
            cv_rect(x + 2, 10, 7, 14, ac);
        if (eng_step_locked(t, st))
            disc((float)x + 5.0f, 22.0f, 1.6f, BG);
        if (t < ENG_TRACKS && chords[st] && eng_trig(t, st))   /* (0.16.16) a chord: a tick a note over the first */
            for (int c = 0; c < chords[st]; c++)
                cv_rect(x + 2, 10 + c * 3, 7, 1, BG);
        if (!(i & 3))
            cv_rect(x, 28, 11, 1, DIM);  /* the beats */
    }
    if (t < ENG_TRACKS) {                /* (0.16.15) a trig held over the steps after it: a bar through them */
        uint32_t m = held_steps(t, len);
        for (int i = 0; i < 16; i++)
            if ((m >> i) & 1u)
                cv_rect(9 + i * 13 - 2, 14, i ? 15 : 13, 6, ui.spage * 16 + i == ui.held_step ? HI : ac);
    }
    for (int j = 0; j < 4; j++)          /* the pages */
        if (j * 16 < len)
            cv_rect(222, 9 + j * 5, 6, 3, j == ui.spage ? TXT : DIM);
    if (t >= ENG_TRACKS) {               /* an effect: how open its lane holds it */
        int o = (int)(eng_lane_open(t) * 18.0f + 0.5f);
        cv_rect(231, 8, 3, 18, TRACK);
        cv_rect(231, 26 - o, 3, o, HI);
    }
}

static int gcd(int a, int b)
{
    while (b) {
        int r = a % b;
        a = b, b = r;
    }
    return a;
}

/* Endless's notes, 16 at a time: a bar as high as the note, a rest a dot; the one playing white. Its options seen
 * (0.16.3): the notes STRIDE never reaches dim; REST's pause and a hole (HOLES) the playing one in a frame, empty; a
 * leap (LEAP) its bar raised, a mark over it */
APP_SMALL static void foot_endless(int t)
{
    project_t *p = eng_project();
    static uint8_t arp[32];
    int am = ui.erec == t ? AM_LOOP : eng_arp_opt(t, AO_MODE);   /* (0.16.13: an ARP's notes as they are now) */
    const uint8_t *src = am == AM_LOOP ? p->endless[t] : arp;
    int len = am == AM_LOOP ? p->endless_len[t] : eng_arp_list(t, arp, 32);
    int pos = eng_endless_pos(t), first, lo = 127, hi = 0, st = eng_endless_state(t);
    int sd = eng_endless_opt(t, EO_STRIDE), dir = (int)p->val[PG_ARP(t) * 4 + 2], g = len ? gcd(sd, len) : 1;
    uint16_t ac = ui.erec == t ? AC3 : track_accent(t);
    if (!len) {
        cv_text(10, 11, &FONT_XS, ui.erec == t ? "PLAY THE NOTES, ONE BY ONE" : am == AM_KEYS ? "HOLD KEYS: THEIR ARPEGGIO" :
                am == AM_CHORD ? "NO PADS: NO CHORD" : "NO NOTES YET", DIM);
        return;
    }
    first = ui.erec == t ? (len > 16 ? len - 16 : 0) : pos >= 0 ? pos / 16 * 16 : 0;
    for (int i = 0; i < len; i++)
        if (src[i] != ENDLESS_REST) {
            lo = src[i] < lo ? src[i] : lo;
            hi = src[i] > hi ? src[i] : hi;
        }
    for (int i = 0; i < 16 && first + i < len; i++) {
        int x = 9 + i * 13, nt = src[first + i], on = first + i == pos, idx = first + i;
        int reach = ui.erec == t || g <= 1 || dir >= 2 || (dir == 1 ? (len - 1 - idx) % g == 0 : idx % g == 0);
        if (nt == ENDLESS_REST) {
            disc((float)x + 5.0f, 24.0f, 1.6f, on ? HI : DIM);
            continue;
        }
        int h = 5 + (hi > lo ? (nt - lo) * 15 / (hi - lo) : 8);
        if (on && (st == ES_PAUSE || st == ES_HOLE)) {   /* (silent this step: a frame) */
            uint16_t fc = st == ES_PAUSE ? MUTED : HI;
            cv_rect(x, 27 - h, 11, 1, fc), cv_rect(x, 26, 11, 1, fc), cv_rect(x, 27 - h, 1, h, fc),
                cv_rect(x + 10, 27 - h, 1, h, fc);
        } else if (on && st == ES_LEAP) {   /* (an octave up: raised, a mark over it) */
            int hh = h + 4 > 22 ? 22 : h + 4;
            cv_rect(x, 27 - hh, 11, hh, HI);
            cv_rect(x + 3, 27 - hh - 3, 5, 2, HI);
        } else {
            cv_rect(x, 27 - h, 11, h, on ? HI : reach ? ac : DIM);
        }
    }
}

/* the presses held for an action: how long (an action that loses something, a second), what it will do (the
 * footer counts it down; "": nothing to show) */
static int erase_lane(void)              /* the lane REC held erases: STEP mode's, or the steps' page's */
{
    return ui.grid ? ui.lane : ui.page < ENG_NPAGES || ptype(ui.page) == PT_PLAY ? plane(ui.page) : ptrack(ui.page);
}

static uint32_t hold_ms(int b, char *label, uint32_t max)
{
    static const char *const LOAD[5] = {"LOAD NEW", "LOAD DEMO", "LOAD SAVED", "LOAD STRESS TEST",
                                        "LOAD STRESS FM"};
    static const char *const FX[3] = {"ERASE DELAY STEPS", "ERASE REVERB STEPS", "ERASE WARPS STEPS"};
    char l[24];
    uint32_t ms = LONG_MS;
    int pt = ptype(ui.page);
    l[0] = 0;
    if (b == B_SEL && held(B_REC)) {      /* REC and SEL: REC's hold */
        ms = 0xFFFFFFFFu;
    } else if (b == B_REC && held(B_SEQ)) {   /* (SEQ held: REC and a white key erase a pattern) */
        ms = 0xFFFFFFFFu;
    } else if ((b == B_REC || b == B_SAVE) && held(B_EDIT)) {   /* (EDIT held: with a white key, a sound erased or
                                          * saved) */
        ms = 0xFFFFFFFFu;
    } else if (b == B_SEQ || b == B_EDIT) {   /* the patterns' screen, the presets' */
        ms = 350u;
    } else if (b == B_REC && held(B_SEL)) {
        text_cat(l, "ERASE ALL CABLES", sizeof l);
        ms = 1500u;
    } else if (b == B_REC && held(B_GLO) && ui.erec < 0 && ui.page != VP_SYS) {
        text_cat(l, "ERASE ALL", sizeof l);   /* all but the sounds, the tempo and the scale (erase_all) */
        ms = 3000u;
    } else if (b == B_SEL && ui.held_step < 0 && !ui.matrix) {
        text_cat(l, "MATRIX", sizeof l);
    } else if (b == B_SAVE) {
        char n[6];
        pat_name(pat_cur, n);
        text_cat(l, eng_playing() ? "SAVE: STOP FIRST" : "SAVE PATTERN ", sizeof l);
        if (!eng_playing())
            text_cat(l, n, sizeof l);
        ms = 1000u;
    } else if (b == B_REC && ui.page == VP_SYS) {
        text_cat(l, LOAD[ui.init_sel % 5], sizeof l);
        ms = 1000u;
    } else if (b == B_REC && (pt == PT_ARP || pt == PT_ARP2) && ui.erec < 0 && !ui.grid) {
        text_cat(l, "ERASE ENDLESS", sizeof l);
        ms = 1000u;
    } else if (b == B_REC && ui.erec < 0 && (ui.grid || pt == PT_STEP || pt == PT_RND || pt == PT_LANE ||
                                             pt == PT_EUC || pt == PT_PLAY || pt == PT_CLIMB)) {
        int e = erase_lane();             /* the lane erased: its steps, locks, randoms (a track: its cables); GLO
                                           * held: all of them */
        if (e >= ENG_TRACKS) {
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
    static const char *const LFO_N[3] = {"LFO 1", "LFO 2", "LFO 3"};
    project_t *p = eng_project();
    char t[64], n[8];
    uint32_t now = plat_ms();
    int msg = now < ui.msg_until, mode = foot_mode(), lv[4], lh = 0;
    uint32_t left = 0, hdone = 0;
    int und = all_state(&left);
    char hl[32];
    int hb = hold_now(hl, sizeof hl, &hdone);
    uint16_t ac = page_accent(ui.page);
    for (int i = 0; i < 4; i++) {        /* each LED's level, 0..8 */
        int v = mode == 1 && i < 3 ? eng_hist(SRC_L1 + i, ENG_HIST - 1) : 0;   /* an LFO: -1 off .. 1 full */
        lv[i] = mode == 1 ? (i < 3 ? ((v + 127) * 8 + 127) / 254 : 0) : track_lit(i) * 8;
        lh = lh * 9 + lv[i];
    }
    if (mode == 6) {                     /* Endless: its notes, the one playing */
        lh = eng_endless_pos(ui.track) + 1 + (ui.erec >= 0) * 100 + p->endless_len[ui.track] * 1000 +
             eng_endless_state(ui.track) * 100000 + eng_endless_opt(ui.track, EO_STRIDE) * 1000000 +
             (int)p->val[PG_ARP(ui.track) * 4 + 2] * 10000000;
        for (int i = 0; i < p->endless_len[ui.track]; i++)
            lh = lh * 31 + p->endless[ui.track][i];
        if (p->arp_opt[ui.track] & 3u) {   /* (0.16.13: an ARP's mode and what it walks now) */
            uint8_t a[32];
            int an = eng_arp_list(ui.track, a, 32);
            lh = lh * 7 + p->arp_opt[ui.track];
            for (int i = 0; i < an; i++)
                lh = lh * 31 + a[i];
        }
    }
    int fl = foot_lane(), sys = ui.page == VP_SYS && !ui.matrix;
    if (sys) {                           /* the machine: the worst block of the last second, what made it */
        lh = 0;
        for (int i = 0; i < 15; i++)
            lh = lh * 31 + (int)eng_peak_part(i);
    }
    if (mode == 5) {                     /* the steps shown: their trigs, locks, the one playing, the one held */
        lh = eng_step_now(fl) | ui.held_step << 7 | ui.spage << 14 | fl << 17;
        for (int i = 0; i < 16; i++)
            lh = lh * 3 + eng_trig(fl, ui.spage * 16 + i) + (eng_step_locked(fl, ui.spage * 16 + i) ? 2 : 0);
        lh = lh * 67 + (int)p->val[PG_STEP(fl) * 4 + 3];
        if (fl < ENG_TRACKS) {           /* (its trigs' lengths, their chords; a step's notes shown) */
            uint8_t tie[ENG_STEPS], ch[ENG_STEPS];
            eng_step_ties(fl, tie);
            eng_step_chords(fl, ch);
            for (int i = 0; i < ENG_STEPS; i++)
                lh = lh * 7 + (tie[i] + 64 * ch[i]) * eng_trig(fl, i);
            lh = lh * 131 + (ui.cview ? ui.cstep + 1 : 0);
        }
        if (fl >= ENG_TRACKS)
            lh = lh * 19 + (int)(eng_lane_open(fl) * 18.0f + 0.5f);
    }
    int32_t hsq = 5000 - (int32_t)hdone * 5, usq = (int32_t)(left * 5000u / ALL_UNDO_MS);
    int32_t s[6] = {lh, mode | ui.mod << 4 | msg << 5 | und << 6 | (hb + 1) << 8 |
                        ((hb >= 0 ? hsq : und == 1 ? usq : 0) / 50) << 12,
                    (int32_t)(track_accent(0) ^ track_accent(1) << 1 ^
                    track_accent(2) << 2 ^ track_accent(3) << 3) ^ ui.track << 20 ^ (int32_t)ac << 4 ^ ui.page << 25,
                    eng_note(0) | eng_note(1) << 8 | eng_note(2) << 16 | p->dice << 24,
                    p->algo[PG_TRACK(0, 0)] | p->algo[PG_TRACK(1, 0)] << 5 | p->algo[PG_TRACK(2, 0)] << 10 |
                    p->algo[PG_TRACK(3, 0)] << 15 | (int32_t)p->trauma << 20, msg ? text_hash(ui.msg[0]) : 0};
    if (!ui.full && !memcmp(s, ui.foot_shown, sizeof s))
        return;
    memcpy(ui.foot_shown, s, sizeof s);
    cv_begin(240, FOOT_H, BG);
    cv_rect(8, 2, 224, 1, RULE);
    if (p->dice)                         /* the random dose */
        cv_rect(8, 1, (224 * p->dice + 50) / 100, 3, AC3);
    if (p->trauma)                       /* TRAUMA, under it */
        cv_rect(8, 4, (224 * p->trauma + 50) / 100, 2, AC2);
    if (hb >= 0) {                       /* a press held: what it will do, the time left in five squares */
        cv_text(10, 8, &FONT_B, hl, HI);
        squares(hsq, hb == B_SEL ? AC1 : AC3);
    } else if (msg) {
        t[0] = 0;
        text_cat(t, ui.msg[0], sizeof t);
        text_cat(t, "  ", sizeof t);
        text_cat(t, ui.msg[1], sizeof t);
        cv_text(120 - text_w(&FONT_B, t) / 2, 11, &FONT_B, t, HI);
    } else if (und) {                    /* ALL: the time to undo it, or waiting for the bar */
        if (und == 1) {
            cv_text(10, 8, &FONT_B, "GLO: UNDO ALL", KNOCK);
            squares(usq, KNOCK);
        } else {
            cv_text(120 - text_w(&FONT_B, "UNDONE AT THE BAR") / 2, 8, &FONT_B, "UNDONE AT THE BAR", KNOCK);
        }
    } else if (sys) {                    /* the worst block of the last second: core 0, core 1, the whole render; each
                                          * task in it (- a voice resting, Warps off); core 0's preparation, its parts */
        static const char *const N[6] = {"T1 ", "  T2 ", "  T3 ", "  T4 ", "  WP ", "  FX "};
        static const char *const PN[5] = {" LFO ", "  CAB ", "  SEQ ", "  LANES ", "  KNOBS "};
        char v[8];
        int all = (int)((eng_peak_part(9) + 5u) / 10u);
        t[0] = 0;
        text_cat(t, "AT THE PEAK ", sizeof t);
        int_text(all, v);
        text_cat(t, v, sizeof t);
        text_cat(t, "%: CORES ", sizeof t);
        int_text((int32_t)((eng_peak_part(7) + 5u) / 10u), v);
        text_cat(t, v, sizeof t);
        text_cat(t, " / ", sizeof t);
        int_text((int32_t)((eng_peak_part(8) + 5u) / 10u), v);
        text_cat(t, v, sizeof t);
        cv_text(10, 3, &FONT_T, t, all >= 90 ? AC3 : HI);
        t[0] = 0;
        for (int i = 0; i < 6; i++) {    /* (* : core 1 played it) */
            uint32_t q = eng_peak_part(i);
            text_cat(t, N[i], sizeof t);
            if (!q && i < 5) {
                text_cat(t, "-", sizeof t);
            } else {
                int_text((int32_t)((q + 5u) / 10u), v);
                text_cat(t, v, sizeof t);
                if ((eng_peak_core1() >> i) & 1u)
                    text_cat(t, "*", sizeof t);
            }
        }
        cv_text(10, 13, &FONT_T, t, MUTED);
        t[0] = 0;
        text_cat(t, "PREP ", sizeof t);
        int_text((int32_t)((eng_peak_part(6) + 5u) / 10u), v);
        text_cat(t, v, sizeof t);
        text_cat(t, ":", sizeof t);
        for (int i = 0; i < 5; i++) {    /* (tenths: they are small) */
            uint32_t q = eng_peak_part(10 + i);
            text_cat(t, PN[i], sizeof t);
            int_text((int32_t)(q / 10u), v);
            text_cat(t, v, sizeof t);
            text_cat(t, ".", sizeof t);
            v[0] = (char)('0' + q % 10u);
            v[1] = 0;
            text_cat(t, v, sizeof t);
        }
        cv_text(10, 23, &FONT_T, t, MUTED);
    } else if (ui.mod) {
        cv_text(10, 6, &FONT_XS, "MOD: HOLD AN LFO'S KEY, TURN A KNOB", HI);
        cv_text(10, 21, &FONT_T, "THE FIRST 3 WHITE KEYS: LFO 1, 2, 3 (BLINKING)", MUTED);
    } else if (mode == 6) {
        foot_endless(ui.erec >= 0 ? ui.erec : ui.track);
    } else if (mode == 5) {
        foot_steps(fl);
    } else {
        for (int i = 0; i < 4; i++) {    /* a cell under each knob (60 px), the LED and the name from its left */
            int x = i * 60 + 8, act = mode ? i == lfo_of(ui.page) : i == ui.track;
            t[0] = 0;
            if (mode) {                  /* the LFOs: each in its colour, the page's in white */
                if (i >= 3)
                    continue;
                text_cat(t, LFO_N[i], sizeof t);
                led((float)(x + 5), 19.0f, 3.6f, accent3(i), lv[i]);
            } else {
                n[0] = (char)('1' + i);
                n[1] = ' ';
                n[2] = 0;
                text_cat(t, n, sizeof t);
                text_cat(t, eng_model_abbr(p->algo[PG_TRACK(i, 0)]), sizeof t);
                if (act) {               /* the track played: knocked out of a block of its colour */
                    uint16_t tc = track_accent(i);
                    cv_rect(x - 4, 7, 52, 25, tc);
                    disc((float)(x + 5), 19.0f, 3.6f, lv[i] ? BG : tint(BG, tc, 3));
                    cv_text(x + 13, 11, &FONT_XS, t, BG);
                    continue;
                }
                led((float)(x + 5), 19.0f, 3.6f, track_accent(i), lv[i]);
            }
            cv_text(x + 13, 11, &FONT_XS, t, act ? HI : DIM);
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

/* ---------------------------------------------------------------- intro --- */
/* the first 1.8 s at power-on: MIFM1's name written in Syne (logo_font.h), its letters at random a moment (signs in
 * the theme's three colours) landing left to right as the pages' names do, a rule drawn out under it in the third
 * colour, then what it is and the version. Any button lets the machine through at once */
#define INTRO_MS 1800u
#define INTRO_NAME_Y 91                  /* the name's cell (its capitals 2 px under it, its baseline 22) */
#define INTRO_RULE_Y (INTRO_NAME_Y + 32)
#define INTRO_LINE_Y (INTRO_RULE_Y + 6)
#include "logo_font.h"
static const char LOGO_SIGNS[] = "#%+0268";   /* (tools/gen_logo_font.py: the font has these, and the name's) */
static const char TEXT_SIGNS[] = "ABCDEFGHJKLMNPRSTUVWXYZ0123456789#%/*+";
static int intro_on;
static uint32_t intro_at;

/* s written from x (its letters where the name's will be), letter i landing at t0 + (t1 - t0) (i + 1) / n: before,
 * a sign of g at random a frame, in an accent; nothing before t0 */
static void intro_text(int32_t x, int32_t y, const felucca_font_t *f, const char *g, const char *s, uint32_t t,
                       uint32_t t0, uint32_t t1, uint16_t c)
{
    int n = 0, ng = 0;
    if (t < t0)
        return;
    while (s[n])
        n++;
    while (g[ng])
        ng++;
    for (int i = 0; i < n; i++) {
        char one[2] = {s[i], 0}, r[2] = {0, 0};
        int32_t w = text_w(f, one);
        uint32_t land = t0 + (t1 - t0) * (uint32_t)(i + 1) / (uint32_t)n;
        if (t >= land || s[i] == ' ') {
            cv_text(x, y, f, one, c);
        } else {                         /* a sign about as wide as the letter (Syne's are wide: a # over an I would
                                          * cover its neighbours) */
            char fit[48];
            int nf = 0;
            uint32_t h = (t / 40u + 1u) * 2654435761u ^ (uint32_t)(i + 1) * 0x9E3779B9u;
            for (int k = 0; k < ng && nf < 47; k++) {
                char o[2] = {g[k], 0};
                if (text_w(f, o) <= w + 12)
                    fit[nf++] = g[k];
            }
            h ^= h >> 15;
            r[0] = nf ? fit[(h >> 8) % (uint32_t)nf] : g[(h >> 8) % (uint32_t)ng];
            cv_text(x + (w - text_w(f, r)) / 2, y, f, r, accent3((int)(h % 3u)));
        }
        x += w;
    }
}

static void intro_center(int32_t y, const felucca_font_t *f, const char *g, const char *s, uint32_t t, uint32_t t0,
                         uint32_t t1, uint16_t c)
{
    intro_text((240 - text_w(f, s)) / 2, y, f, g, s, t, t0, t1, c);
}

static void intro_scene(uint32_t t)
{
    float half = (float)text_w(&FONT_LOGO, "MIFM1") * 0.5f, u = 0.0f;
    if (t > 250u)                        /* the rule: out from the middle, quick then slowing, as wide as the name */
        u = t >= 700u ? 1.0f : 1.0f - (1.0f - (float)(t - 250u) / 450.0f) * (1.0f - (float)(t - 250u) / 450.0f);
    intro_center(INTRO_NAME_Y, &FONT_LOGO, LOGO_SIGNS, "MIFM1", t, 0u, 700u, HI);
    if (u > 0.0f)
        seg(120.0f - half * u, (float)INTRO_RULE_Y, 120.0f + half * u, (float)INTRO_RULE_Y, 2.0f, AC3);
    intro_center(INTRO_LINE_Y, &FONT_XS, TEXT_SIGNS, "4-TRACK SYNTHESIS MACHINE", t, 450u, 1050u, MUTED);
    intro_center(218, &FONT_T, TEXT_SIGNS, APP_VERSION, t, 900u, 1100u, DIM);   /* (a release's number; DEV here) */
}

static void draw_intro(uint32_t t)       /* the screen in three bands of 80 rows, each drawn in screen rows */
{
    for (int b = 0; b < 3; b++) {
        cv_begin(240, 80, BG);
        cv_oy = -80 * b;
        cv_y0 = 0;
        cv_y1 = 80;
        intro_scene(t);
        cv_oy = 0;
        blit(0, (uint32_t)(80 * b));
    }
}

/* --------------------------------------------------------------- random --- */
/* the random dose (OCT- held, SELECT turned): the machine's own screen for it, over everything. Red opening from
 * the middle (a circle, 260 ms; red whatever the theme, 0.14.6); RAN over DOM in Syne ExtraBold (rand_font.h), white,
 * landing left to right through signs at random, then as many of its letters thrown as the dose is high (one at a few
 * %, all six at 100 %), a sign each every 60 ms, all of them again at each detent; the dose, its bar (the fills'
 * quarter marked), and from 76 % the fills: how many tracks, how often, how long */
#include "rand_font.h"
static const char RAND_SIGNS[] = "#%+&?KXZRANDOM";
#define RAND_OPEN_MS 260u
#define RAND_LAND_MS 560u
#define NOISE_RED 0xE8A3u                /* (232, 20, 28): the random dose's screen, TRAUMA's letters */

static uint16_t mix565(uint16_t a, uint16_t b, uint32_t k)   /* a to b, k / 256 of the way */
{
    uint32_t r = ((a >> 11) * (256u - k) + (b >> 11) * k) >> 8;
    uint32_t g = (((a >> 5) & 63u) * (256u - k) + ((b >> 5) & 63u) * k) >> 8;
    uint32_t bl = ((a & 31u) * (256u - k) + (b & 31u) * k) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

static uint32_t rand_hash(uint32_t a, uint32_t b)
{
    uint32_t h = (a + 1u) * 2654435761u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 13;
    return h;
}

static uint32_t rand_turns;              /* the detents turned on it (each throws the letters again) */

/* one of RAN, DOM (row 0, 1), centred at y: each letter landed by `land` (left to right), then thrown while its
 * place is among the first n of the throw's order */
static void rand_word(const char *w, int row, int32_t y, uint32_t t, int n, uint32_t tick, uint16_t c)
{
    static const uint8_t ORDER[6] = {3, 0, 5, 2, 4, 1};   /* the letters thrown first to last (RAN DOM: 0..5) */
    int32_t x = (240 - text_w(&FONT_RAND, w)) / 2;
    for (int i = 0; i < 3; i++) {
        int idx = row * 3 + i, thrown = 0;
        char one[2] = {w[i], 0}, r[2] = {0, 0};
        int32_t lw = text_w(&FONT_RAND, one);
        uint32_t land = 120u + (RAND_LAND_MS - 120u) * (uint32_t)(idx + 1) / 6u;
        for (int k = 0; k < n; k++)
            if (ORDER[k] == idx)
                thrown = 1;
        if (t < 120u)
            ;                            /* (not yet) */
        else if (t >= land && !thrown) {
            cv_text(x, y, &FONT_RAND, one, c);
        } else {                         /* a sign about as wide as the letter */
            char fit[16];
            int nf = 0;
            uint32_t h = rand_hash(tick * 7u + (uint32_t)idx, rand_turns);
            for (int k = 0; RAND_SIGNS[k] && nf < 15; k++) {
                char o[2] = {RAND_SIGNS[k], 0};
                if (RAND_SIGNS[k] != w[i] && text_w(&FONT_RAND, o) <= lw + 10 && text_w(&FONT_RAND, o) >= lw - 24)
                    fit[nf++] = RAND_SIGNS[k];
            }
            r[0] = nf ? fit[(h >> 8) % (uint32_t)nf] : w[i];
            cv_text(x + (lw - text_w(&FONT_RAND, r)) / 2, y, &FONT_RAND, r, c);
        }
        x += lw;
    }
}

static void rand_scene(uint32_t t, int d, uint32_t tick)
{
    uint16_t col = NOISE_RED, dark = mix565(NOISE_RED, 0x0000u, 70u);
    uint16_t soft = mix565(NOISE_RED, 0xFFFFu, 150u), white = 0xFFFFu;
    int n = (d * 6 + 99) / 100, every, beats, tracks;
    char line[48], v[8];
    float rr = 1.0f;
    if (t < RAND_OPEN_MS) {              /* the colour from the middle: quick, then slowing */
        float u = 1.0f - (float)t / (float)RAND_OPEN_MS;
        rr = 1.0f - u * u * u;
    }
    if (rr < 1.0f) {                     /* (outside the circle: the screen's own black) */
        float rad = 172.0f * rr;
        cv_rect(0, 0, 240, 240, BG);
        for (int32_t y = 0; y < 240; y++) {
            float dy = (float)y + 0.5f - 120.0f, q = rad * rad - dy * dy;
            if (q > 0.0f) {
                int32_t dx = (int32_t)fm_sqrtf(q);
                cv_rect(120 - dx, y, 2 * dx, 1, col);
            }
        }
    }
    line[0] = 0;
    text_cat(line, d ? "EVERY TRIG ANOTHER SOUND" : "THE SOUND AS SET", sizeof line);
    if (t >= 200u)
        cv_text((240 - text_w(&FONT_XS, line)) / 2, 18, &FONT_XS, line, soft);
    rand_word("RAN", 0, 46, t, n, tick, white);
    rand_word("DOM", 1, 88, t, n, tick, white);
    if (t >= 300u) {
        int_text(d, v);
        line[0] = 0;
        text_cat(line, v, sizeof line);
        text_cat(line, " %", sizeof line);
        cv_text((240 - text_w(&FONT_L, line)) / 2, 138, &FONT_L, line, white);
        float u = t >= 520u ? 1.0f : (float)(t - 300u) / 220.0f;   /* the bar drawn out */
        int32_t bw = (int32_t)(200.0f * u), fw = (int32_t)(200.0f * u * (float)d / 100.0f);
        cv_rect(20, 184, bw, 6, dark);
        cv_rect(20, 184, fw, 6, white);
        if (u >= 1.0f)
            cv_rect(20 + 150, 180, 2, 14, d > 75 ? white : soft);   /* the fills' quarter */
    }
    if (t >= 420u) {
        eng_fills(d, &every, &beats, &tracks);
        line[0] = 0;
        if (!tracks) {
            text_cat(line, "FILLS FROM 76 %", sizeof line);
        } else {
            int_text(tracks, v);
            text_cat(line, "FILLS  ", sizeof line);
            text_cat(line, v, sizeof line);
            text_cat(line, tracks > 1 ? " TRACKS  " : " TRACK  ", sizeof line);
            text_cat(line, every == 1 ? "EVERY BAR" : every == 2 ? "EVERY 2 BARS" : "EVERY 4 BARS", sizeof line);
        }
        cv_text((240 - text_w(&FONT_XS, line)) / 2, 200, &FONT_XS, line, tracks ? white : soft);
        if (tracks) {
            line[0] = 0;
            text_cat(line, beats > 1 ? "THE LAST 2 BEATS" : "THE LAST BEAT", sizeof line);
            cv_text((240 - text_w(&FONT_XS, line)) / 2, 218, &FONT_XS, line, soft);
        }
    }
}

static void draw_random(void)
{
    static int32_t shown[4] = {-1, -1, -1, -1};
    uint32_t t = plat_ms() - ui.dice_at;
    int d = eng_project()->dice > 100 ? 100 : eng_project()->dice;
    uint32_t tick = d ? plat_ms() / 60u : 0u;
    int32_t s[4] = {t < RAND_LAND_MS + 60u ? (int32_t)t : -2, d, (int32_t)tick, (int32_t)rand_turns};
    if (!ui.full && !memcmp(s, shown, sizeof s))
        return;                          /* (nothing moved) */
    memcpy(shown, s, sizeof s);
    for (int b = 0; b < 3; b++) {        /* in three bands of 80 rows, each drawn in screen rows */
        cv_begin(240, 80, NOISE_RED);
        cv_oy = -80 * b;
        cv_y0 = 0;
        cv_y1 = 80;
        rand_scene(t, d, tick);
        cv_oy = 0;
        blit(0, (uint32_t)(80 * b));
    }
}

static void dice_open(void)              /* the random dose's screen */
{
    if (!ui.dice) {
        ui.dice = 1;
        ui.dice_at = plat_ms();
        ui.full = 1;
    }
}

static void dice_close(void)             /* back to the page, all of it drawn again */
{
    if (ui.dice) {
        ui.dice = 0;
        ui.full = 1;
        ui.head_shown[0] = -1;
        ui.foot_shown[0] = -1;
    }
}

/* ------------------------------------------------------------- patterns --- */
/* The patterns (0.15, pat_store.h): 11 banks (A to K) of 16, each a whole project. SEQ held: their screen; a black
 * key a bank, a white key a pattern. One pressed plays it, looped: from the playing one's end (its longest lane's
 * LENGTH), at once when stopped; several in a row, an order played in a loop (A1 A1 A2 A5: A1 twice, A2, then A5),
 * after Teenage Engineering's Pocket Operators. With REC held, a white key erases one (stopped). SAVE held (stopped):
 * the pattern saved in its slot, and the project as before (the backup's). An empty slot takes the pattern playing as
 * it is (a copy to make another one from); a pattern left without SAVE comes back as it was saved. The machine's
 * settings (the output, Warps' quality, the colours) stay whatever plays */

static void pat_unstage(void)            /* scratch wanted for something else: nothing cued from it */
{
    eng_cue(0);
    chain_staged = -1;
}

static int pat_read(int slot)            /* a saved pattern into scratch (as this version reads it): 0, -1 none, bad */
{
    project_t *p = eng_project();
    int got = pat_load(slot, &scratch, sizeof scratch);
    if (got <= 0 || !eng_upgrade(&scratch, got))
        return -1;
    scratch.out_mode = p->out_mode;      /* (the machine's settings stay) */
    scratch.warps_hq = p->warps_hq;
    scratch.theme = p->theme;
    return 0;
}

static void pat_switch_now(int slot)     /* stopped: that pattern at once (an empty slot: the one playing, copied) */
{
    char was[6], to[6], l1[16], l2[24];
    int lost = dirty && slot != pat_cur, full = pat_full(slot);
    pat_name(pat_cur, was);
    pat_name(slot, to);
    pat_unstage();
    if (full && pat_read(slot) == 0) {
        *eng_project() = scratch;
        eng_fresh();
        dirty = 0;
    } else if (slot != pat_cur) {
        dirty = 1;                       /* (a copy, until SAVE) */
    }
    l1[0] = l2[0] = 0;
    text_cat(l1, "PATTERN ", sizeof l1);
    text_cat(l1, to, sizeof l1);
    if (lost)
        text_cat(l2, was, sizeof l2), text_cat(l2, " NOT SAVED", sizeof l2);
    else if (!full && slot != pat_cur)
        text_cat(l2, "NEW, FROM ", sizeof l2), text_cat(l2, was, sizeof l2);
    else
        text_cat(l2, "LOADED", sizeof l2);
    pat_cur = slot;
    app_say(l1, l2);
    ui.full = 1;
}

static void chain_apply(void)            /* SEQ let go: the order made, from the playing pattern's end */
{
    char n[6], l1[16];
    memcpy(chain, chain_new, (size_t)chain_new_n);
    chain_n = chain_new_n;
    chain_new_n = 0;
    pat_unstage();
    chain_cycles = eng_cycles();
    if (!eng_playing()) {
        pat_switch_now(chain[0]);
        chain_pos = 0;
        return;
    }
    chain_pos = chain[0] == pat_cur ? 0 : -1;
    pat_name(chain[chain_pos < 0 ? 0 : (chain_n > 1 ? 1 : 0)], n);
    l1[0] = 0;
    text_cat(l1, chain_n > 1 ? "ORDER OF " : "NEXT ", sizeof l1);
    if (chain_n > 1) {
        char c[4];
        int_text(chain_n, c);
        text_cat(l1, c, sizeof l1);
    } else {
        text_cat(l1, n, sizeof l1);
    }
    app_say(l1, "AT THE PATTERN'S END");
}

static void chain_service(void)          /* each frame: the order going on, its next pattern read and cued */
{
    uint32_t c;
    if (!chain_n)
        return;
    if (!eng_playing()) {                /* (stopped: the next one read again when it plays) */
        if (chain_staged >= 0)
            pat_unstage();
        chain_cycles = eng_cycles();
        return;
    }
    c = eng_cycles();
    if (c != chain_cycles) {             /* a pattern played to its end: the next one in (or again) */
        chain_cycles = c;
        if (chain_staged >= 0 && (chain_again || !eng_cue_pending())) {
            int slot = chain[chain_staged];
            if (!chain_again)
                dirty = 0;
            else if (slot != pat_cur)
                dirty = 1;               /* (an empty slot: the one playing, copied) */
            chain_pos = chain_staged;
            pat_cur = slot;
            chain_staged = -1;
            ui.full = 1;
        }
    }
    if (chain_staged < 0) {
        int np = chain_pos < 0 ? 0 : (chain_pos + 1) % chain_n, slot = chain[np];
        chain_again = slot == pat_cur || !pat_full(slot) || pat_read(slot) != 0;
        if (!chain_again)
            eng_cue(&scratch);
        chain_staged = np;
    }
}

static void pattern_key(int k, int down) /* SEQ held: a black key a bank, a white key a pattern (REC held: erased) */
{
    static const uint8_t W[16] = PLAT_WHITE_KEYS, Bk[11] = PLAT_BLACK_KEYS;
    if (!down)
        return;
    pat_open = 1;
    pat_used = 1;
    for (int i = 0; i < 11; i++)
        if (Bk[i] == k)
            pat_bank = i, ui.full = 1;
    for (int i = 0; i < 16; i++) {
        int slot;
        if (W[i] != k)
            continue;
        slot = pat_bank * PAT_PER_BANK + i;
        if (held(B_REC)) {               /* erased (stopped: the flash written) */
            char n[6], l1[16];
            ui.long_done[B_REC] = 1;     /* (REC's own tap or hold not taken) */
            pat_name(slot, n);
            l1[0] = 0;
            text_cat(l1, "PATTERN ", sizeof l1);
            text_cat(l1, n, sizeof l1);
            if (eng_playing())
                app_say("ERASE: STOP FIRST", n);
            else if (pat_erase(slot) == 0) {
                if (slot == pat_cur)
                    dirty = 1;           /* (the one playing: as it is, saved nowhere now) */
                app_say(l1, pat_full(slot) ? "NOT ERASED" : "ERASED");
            }
            else
                app_say("COULD NOT ERASE", n);
        } else if (chain_new_n < (int)sizeof chain_new) {
            chain_new[chain_new_n++] = (uint8_t)slot;
        }
        ui.full = 1;
    }
}

static void pattern_seq_up(void)         /* SEQ let go: the order made, if keys were pressed */
{
    if (chain_new_n)
        chain_apply();
    if (pat_open) {
        pat_open = 0;
        ui.full = 1;
        ui.head_shown[0] = -1;
        ui.foot_shown[0] = -1;
    }
}

/* SEQ held: the patterns' screen, over everything. At the top the banks, A to K on the black keys (the one shown lit,
 * those holding patterns brighter); the bank's 16 on the white keys, four rows of four (saved: filled; empty: a
 * frame; the one playing in the first colour, the next one framed in the second, blinking); the order being made or
 * playing; the room left */
static void draw_patterns(void)
{
    static int32_t shown[10];
    char line[48], n[6], v[8];
    uint32_t now = plat_ms();
    int len = eng_pattern_len(), pos = eng_pattern_pos(), next = chain_staged >= 0 ? chain[chain_staged] : -1;
    int msg = now < ui.msg_until;
    int32_t st[10] = {pat_bank, pat_cur, chain_new_n, next, (int32_t)((now / 250u) & 1u), pos, dirty, chain_n,
                      (int32_t)pat_seq, msg ? (int32_t)ui.msg_until : 0};
    if (!ui.full && !memcmp(st, shown, sizeof st))
        return;                          /* (nothing moved) */
    memcpy(shown, st, sizeof st);
    for (int b = 0; b < 3; b++) {
        cv_begin(240, 80, BG);
        cv_oy = -80 * b;
        cv_y0 = 0;
        cv_y1 = 80;
        pat_name(pat_cur, n);
        cv_text(10, 6, &FONT_B, "PATTERNS", HI);
        line[0] = 0;
        text_cat(line, n, sizeof line);
        if (dirty)
            text_cat(line, "*", sizeof line);
        cv_text(230 - text_w(&FONT_M, line), 2, &FONT_M, line, AC1);
        if (pos >= 0 && len > 0)         /* the pattern's place */
            cv_rect(10, 28, 220 * (pos + 1) / len, 2, AC1);
        for (int k = 0; k < PAT_BANKS; k++) {   /* the banks */
            int any = 0, x = 10 + k * 20;
            char c[2] = {(char)('A' + k), 0};
            for (int i = 0; i < PAT_PER_BANK && !any; i++)
                any = pat_full(k * PAT_PER_BANK + i);
            cv_rect(x, 36, 18, 20, k == pat_bank ? AC1 : any ? RULE : BG);
            if (k != pat_bank)
                cv_rect(x, 36, 18, 1, any ? MUTED : RULE), cv_rect(x, 55, 18, 1, any ? MUTED : RULE),
                    cv_rect(x, 36, 1, 20, any ? MUTED : RULE), cv_rect(x + 17, 36, 1, 20, any ? MUTED : RULE);
            cv_text(x + 9 - text_w(&FONT_S, c) / 2, 38, &FONT_S, c, k == pat_bank ? BG : any ? HI : MUTED);
            if (pat_cur / PAT_PER_BANK == k)
                cv_rect(x + 5, 58, 8, 2, AC1);
        }
        for (int i = 0; i < PAT_PER_BANK; i++) {   /* its 16 */
            int slot = pat_bank * PAT_PER_BANK + i, x = 10 + (i % 4) * 56, y = 66 + (i / 4) * 30;
            int full = pat_full(slot), cur = slot == pat_cur, nx = slot == next && slot != pat_cur;
            uint16_t fill = cur ? AC1 : full ? TRACK : BG, ink = cur ? BG : full ? HI : MUTED;
            int_text(i + 1, v);
            cv_rect(x, y, 52, 26, fill);
            if (!cur && (!full || nx)) { /* a frame: empty, or the next one (blinking) */
                uint16_t fc = nx ? ((now / 250u) & 1u ? AC2 : RULE) : RULE;
                cv_rect(x, y, 52, 2, fc), cv_rect(x, y + 24, 52, 2, fc), cv_rect(x, y, 2, 26, fc),
                    cv_rect(x + 50, y, 2, 26, fc);
            }
            cv_text(x + 26 - text_w(&FONT_B, v) / 2, y + 5, &FONT_B, v, ink);
            for (int c = 0; c < chain_new_n; c++)   /* in the order being made: a dot */
                if (chain_new[c] == slot)
                    cv_rect(x + 44, y + 4, 4, 4, AC3);
        }
        line[0] = 0;                     /* the order: being made, or playing */
        {
            const uint8_t *o = chain_new_n ? chain_new : chain;
            int on = chain_new_n ? chain_new_n : chain_n;
            for (int c = 0; c < on && text_w(&FONT_XS, line) < 196; c++) {
                pat_name(o[c], n);
                if (c)
                    text_cat(line, " ", sizeof line);
                text_cat(line, n, sizeof line);
            }
            if (!on)
                text_cat(line, "A KEY: A PATTERN, SEVERAL: AN ORDER", sizeof line);
            cv_text(10, 190, &FONT_XS, line, chain_new_n ? AC3 : on > 1 ? TXT : MUTED);
        }
        line[0] = 0;                     /* a message (0.15.1), or what REC does and the room left */
        if (msg) {
            text_cat(line, ui.msg[0], sizeof line);
            if (ui.msg[1][0])
                text_cat(line, "  ", sizeof line), text_cat(line, ui.msg[1], sizeof line);
        } else {
            int_text((int32_t)(pat_room() / 1024u), v);
            text_cat(line, "REC+KEY ERASES   ", sizeof line);
            text_cat(line, v, sizeof line);
            text_cat(line, " KB FREE", sizeof line);
        }
        cv_text(10, 214, &FONT_T, line, msg ? AC3 : MUTED);
        cv_oy = 0;
        blit(0, (uint32_t)(80 * b));
    }
}

static void pattern_boot(void)           /* power-on: the patterns read; the playing one the pattern saved last, else
                                          * A1 (an older version's saved project becomes A1) */
{
    pat_init();
    pat_cur = pat_last >= 0 ? pat_last : 0;
    if (pat_last < 0 && proj_stored && pat_ok)
        pat_save(0, eng_project(), sizeof(project_t));
}

static void pattern_forget(void)         /* another project read in (a restore, INIT, the demo): no order going on */
{
    pat_unstage();
    chain_n = chain_new_n = 0;
    chain_pos = -1;
}

/* -------------------------------------------------------------- presets --- */
/* The presets (0.15.1): a track's sound (engine.h eng_sound_t: its model, the knobs of its sound, envelope and pitch,
 * its bits, filter and glitch, their randoms; not its level, sends, pan, steps or cables), 11 banks of 16 as the
 * patterns are: A the factory's 16 (engine.cc kFactory), B to K the 160 saved (pat_store.h). EDIT held: their screen,
 * for the active track (PRESETS turns it); a black key a bank, a white key that sound onto the track, heard at once
 * (C3, as a step plays it, while the key is held). SAVE held and a white key: the track's sound saved there (B to K);
 * REC held: one erased. While the clock runs, a save that would make room (a sector erased: the sound stopped a
 * moment) waits for it to stop (pat_quick); a record's few bytes are written at once */
#define PRE_NOTE 48

static int pre_read(int slot, eng_sound_t *s)   /* a preset into s: 0, -1 none or not a sound */
{
    if (slot < 0 || slot >= PAT_BANKS * PAT_PER_BANK)
        return -1;
    if (slot < PAT_PER_BANK)
        return eng_factory(slot, s);
    int got = pat_load(PRE_KEY(slot - PAT_PER_BANK), s, sizeof *s);
    return got == (int)sizeof *s && eng_sound_check(s, got) ? 0 : -1;
}

static int pre_full(int slot) { return slot < PAT_PER_BANK ? slot >= 0 : pat_full(PRE_KEY(slot - PAT_PER_BANK)); }

static int pre_matches(int t, eng_sound_t *s)   /* the track's sound read last, still on its model: in s, 1 */
{
    return pre_cur[t] >= 0 && pre_read(pre_cur[t], s) == 0 && s->algo[0] == eng_project()->algo[PG_TRACK(t, 0)];
}

static void pre_hear_off(void)           /* the sound heard let go */
{
    if (pre_key >= 0 && pre_track >= 0)
        eng_key(pre_track, PRE_NOTE, 0);
    pre_key = pre_track = -1;
}

static void pre_say(const char *what, int slot, const char *b)   /* SOUND A1 ...  */
{
    char n[6], l1[24];
    pat_name(slot, n);
    l1[0] = 0;
    text_cat(l1, what, sizeof l1);
    text_cat(l1, n, sizeof l1);
    app_say(l1, b);
}

static void preset_key(int k, int down)  /* EDIT held: a black key a bank, a white key a sound (SAVE: saved; REC:
                                          * erased) */
{
    static const uint8_t W[16] = PLAT_WHITE_KEYS, Bk[11] = PLAT_BLACK_KEYS;
    int t = ui.track;
    if (!down) {
        if (k == pre_key)
            pre_hear_off();
        return;
    }
    pre_open = 1;
    pre_used = 1;
    ui.full = 1;
    for (int i = 0; i < 11; i++)
        if (Bk[i] == k)
            pre_bank = i;
    for (int i = 0; i < 16; i++) {
        int slot = pre_bank * PAT_PER_BANK + i, e;
        if (W[i] != k)
            continue;
        if (held(B_SAVE) || held(B_REC)) {   /* (their own tap or hold not taken) */
            int sv = held(B_SAVE);
            ui.long_done[sv ? B_SAVE : B_REC] = 1;
            ui.used[sv ? B_SAVE : B_REC] = 1;
            if (slot < PAT_PER_BANK) {
                app_say("A: THE FACTORY'S", sv ? "SAVE IN B TO K" : "KEPT AS IT IS");
                return;
            }
            pat_quick = eng_playing();
            if (sv) {
                eng_sound_get(t, &pre_snd);
                if (pre_matches(t, &pre_tmp))   /* (a sound read, then turned: its name kept) */
                    memcpy(pre_snd.name, pre_tmp.name, sizeof pre_snd.name);
                e = pat_save(PRE_KEY(slot - PAT_PER_BANK), &pre_snd, sizeof pre_snd);
            } else {
                e = pat_erase(PRE_KEY(slot - PAT_PER_BANK));
            }
            pat_quick = 0;
            if (e == 0 && sv)
                pre_cur[t] = slot;
            if (e == 0 && !sv)
                for (int u = 0; u < ENG_TRACKS; u++)
                    if (pre_cur[u] == slot)
                        pre_cur[u] = -1;
            pre_say("SOUND ", slot, e == 0 ? (sv ? "SAVED" : "ERASED") : e == -3 ? "STOP FIRST (ROOM TO MAKE)" :
                                    e == -1 ? "NO ROOM LEFT" : sv ? "NOT SAVED" : "NOT ERASED");
            return;
        }
        if (pre_read(slot, &pre_snd) != 0) {
            pre_say("SOUND ", slot, "EMPTY: SAVE+KEY");
            return;
        }
        e = eng_sound_set(t, &pre_snd);
        if (e == -2) {                   /* PADS: once a project */
            char l2[24], c[2] = {0, 0};
            for (int u = 0; u < ENG_TRACKS; u++)
                if (u != t && eng_model_kind(eng_project()->algo[PG_TRACK(u, 0)]) == MK_PADS)
                    c[0] = (char)('1' + u);
            l2[0] = 0;
            text_cat(l2, "PADS IS ON TRACK ", sizeof l2);
            text_cat(l2, c, sizeof l2);
            app_say(l2, "ONCE A PROJECT");
            return;
        }
        if (e != 0) {
            pre_say("SOUND ", slot, "NOT READ");
            return;
        }
        pre_cur[t] = slot;
        dirty = 1;
        {
            char l1[24], c[2] = {(char)('1' + t), 0};
            l1[0] = 0;
            text_cat(l1, "T", sizeof l1);
            text_cat(l1, c, sizeof l1);
            text_cat(l1, ": ", sizeof l1);
            text_cat(l1, pre_snd.name, sizeof l1);
            app_say(l1, "");
        }
        pre_hear_off();                  /* (heard: C3, as a step plays it) */
        eng_key(t, PRE_NOTE, 1);
        pre_key = k;
        pre_track = t;
    }
}

static void preset_edit_up(void)         /* EDIT let go: the screen closed, the sound heard let go */
{
    pre_hear_off();
    if (pre_open) {
        pre_open = 0;
        ui.full = 1;
        ui.head_shown[0] = -1;
        ui.foot_shown[0] = -1;
    }
}

static void fit_text(char *b, const felucca_font_t *f, int w)   /* cut to w pixels */
{
    int n = 0;                           /* (no strlen on the FM-1: felucca-bsp's libc is memset, memcpy, memcmp) */
    while (b[n])
        n++;
    while (n > 0 && text_w(f, b) > w)
        b[--n] = 0;
}

/* EDIT held: the presets' screen, over everything, as the patterns' is. At the top the track and its sound; the banks
 * A to K on the black keys (A the factory's, its letter in the second colour; those holding sounds brighter); the
 * bank's 16 on the white keys, four rows of four, each its number and its name (the one the track holds in the first
 * colour; empty: a frame); a message, or what the keys do; the room left */
static void draw_presets(void)
{
    static int32_t shown[7];
    char line[48], v[8], nm[16];
    uint32_t now = plat_ms();
    int t = ui.track, cur = pre_matches(t, &pre_tmp) ? pre_cur[t] : -1, msg = now < ui.msg_until;
    int32_t st[7] = {pre_bank, t, cur, msg, (int32_t)pat_seq, (int32_t)eng_project()->algo[PG_TRACK(t, 0)],
                     msg ? (int32_t)ui.msg_until : 0};
    if (!ui.full && !memcmp(st, shown, sizeof st))
        return;                          /* (nothing moved) */
    memcpy(shown, st, sizeof st);
    for (int b = 0; b < 3; b++) {
        cv_begin(240, 80, BG);
        cv_oy = -80 * b;
        cv_y0 = 0;
        cv_y1 = 80;
        cv_text(10, 6, &FONT_B, "PRESETS", HI);
        line[0] = 0;
        text_cat(line, "TRACK ", sizeof line);
        v[0] = (char)('1' + t), v[1] = 0;
        text_cat(line, v, sizeof line);
        cv_text(230 - text_w(&FONT_M, line), 2, &FONT_M, line, AC1);
        for (int k = 0; k < PAT_BANKS; k++) {   /* the banks */
            int any = k == 0, x = 10 + k * 20;
            char c[2] = {(char)('A' + k), 0};
            for (int i = 0; i < PAT_PER_BANK && !any; i++)
                any = pre_full(k * PAT_PER_BANK + i);
            cv_rect(x, 36, 18, 20, k == pre_bank ? AC1 : any ? RULE : BG);
            if (k != pre_bank)
                cv_rect(x, 36, 18, 1, any ? MUTED : RULE), cv_rect(x, 55, 18, 1, any ? MUTED : RULE),
                    cv_rect(x, 36, 1, 20, any ? MUTED : RULE), cv_rect(x + 17, 36, 1, 20, any ? MUTED : RULE);
            cv_text(x + 9 - text_w(&FONT_S, c) / 2, 38, &FONT_S, c,
                    k == pre_bank ? BG : k == 0 ? AC2 : any ? HI : MUTED);
            if (cur >= 0 && cur / PAT_PER_BANK == k)
                cv_rect(x + 5, 58, 8, 2, AC1);
        }
        for (int i = 0; i < PAT_PER_BANK; i++) {   /* its 16 */
            int slot = pre_bank * PAT_PER_BANK + i, x = 10 + (i % 4) * 56, y = 66 + (i / 4) * 30;
            int full = pre_full(slot), on = slot == cur;
            uint16_t fill = on ? AC1 : full ? TRACK : BG, ink = on ? BG : full ? HI : MUTED;
            int_text(i + 1, v);
            cv_rect(x, y, 52, 26, fill);
            if (!on && !full)
                cv_rect(x, y, 52, 2, RULE), cv_rect(x, y + 24, 52, 2, RULE), cv_rect(x, y, 2, 26, RULE),
                    cv_rect(x + 50, y, 2, 26, RULE);
            cv_text(x + 4, y + 1, &FONT_T, v, on ? BG : MUTED);
            nm[0] = 0;
            if (slot < PAT_PER_BANK)
                text_cat(nm, eng_factory_name(slot), sizeof nm);
            else if (full && pre_read(slot, &pre_snd) == 0)
                text_cat(nm, pre_snd.name, sizeof nm);
            fit_text(nm, &FONT_T, 46);
            cv_text(x + 4, y + 12, &FONT_T, nm, ink);
        }
        line[0] = 0;                     /* a message, or the track's sound */
        if (msg) {
            text_cat(line, ui.msg[0], sizeof line);
            if (ui.msg[1][0])
                text_cat(line, "  ", sizeof line), text_cat(line, ui.msg[1], sizeof line);
        } else {
            text_cat(line, eng_algo_name(PG_TRACK(t, 0), eng_project()->algo[PG_TRACK(t, 0)]), sizeof line);
            if (cur >= 0) {
                pat_name(cur, v);
                text_cat(line, "   ", sizeof line), text_cat(line, v, sizeof line);
                text_cat(line, " ", sizeof line), text_cat(line, pre_tmp.name, sizeof line);
            }
        }
        fit_text(line, &FONT_XS, 220);
        cv_text(10, 190, &FONT_XS, line, msg ? AC3 : TXT);
        line[0] = 0;
        int_text((int32_t)(pat_room() / 1024u), v);
        text_cat(line, pre_bank ? "SAVE+KEY SAVES  REC+KEY ERASES  " : "THE FACTORY'S 16   ", sizeof line);
        text_cat(line, v, sizeof line);
        text_cat(line, " KB FREE", sizeof line);
        cv_text(10, 214, &FONT_T, line, MUTED);
        cv_oy = 0;
        blit(0, (uint32_t)(80 * b));
    }
}

/* --------------------------------------------------------------- trauma --- */
/* TRAUMA (OCT+ held, SELECT turned): a dose of saturation (engine.cc: the drives, the filters' FM, Warps, the
 * delay's drive), after Audiotrauma, its own screen over everything, in black and white (0.14.6). White bars coming
 * in from both sides (260 ms), then the label's slogan in Anton, its letters and its lines tight: VOUS ETES LE over
 * BRUIT, as wide (noise_glyphs.h). At 0 it stands still; dosed, its letters are thrown at random as fast as the screen
 * goes (a throw each 50 ms: a quarter of them at a few %, three in five at 100 %), and through them the slogan comes
 * in another language each 1.4 s (0.7 s at 100 %), its letters landing one by one (0.14.8): French, English,
 * Japanese, Czech, German, Italian, Swedish. A letter or a few red at random (one, two from 40 %, three at 80 %);
 * from 80 % a strobe, the screen turned white a frame at a time (60 ms), one in four, then in three, every other one
 * at 100 %. Under it the dose and its gauge; at 100 % the gauge breaks out of its frame to the screen's edge, in red,
 * and its figure runs on past 100 */
#include "noise_glyphs.h"
#define TRAUMA_OPEN_MS 260u
static uint32_t trauma_turns;            /* the detents turned on it (each throws the letters again) */
static int trauma_lang;                  /* the slogan's language now (noise_glyphs.h's order: French first) */
static uint32_t trauma_lang_at;          /* since when */

/* a glyph at x (its pen), the line's cap top at y: its rows' runs, those in this band drawn */
static void noise_glyph(const noise_glyph_t *g, const uint8_t *runs, int32_t x, int32_t y, uint16_t c)
{
    const uint8_t *p = runs + g->off;
    uint32_t r = c >> 11, gg = (c >> 5) & 63u, b = c & 31u;
    int32_t y0 = y + g->y0 + cv_oy;
    for (int32_t row = 0; row < (int32_t)g->h; row++) {
        int32_t py = y0 + row, px = x + g->x0;
        int in = py >= cv_y0;
        if (py >= cv_y1)
            break;                       /* (the rows under the band: none needed) */
        for (int32_t k = 0; k < (int32_t)g->w;) {
            uint32_t v = *p++, a = v >> 4, n = (v & 15u) + 1u;
            for (uint32_t m = 0; in && a && m < n; m++) {
                uint32_t at = (uint32_t)py * cv_w + (uint32_t)(px + (int32_t)m);
                if ((uint32_t)(px + (int32_t)m) >= cv_w)
                    continue;
                if (a == 15u) {
                    cv_px[at] = swap16(c);
                } else {
                    uint32_t bg = swap16(cv_px[at]), br = bg >> 11, bgg = (bg >> 5) & 63u, bb = bg & 31u;
                    cv_px[at] = swap16(((r * a + br * (15u - a)) / 15u) << 11 |
                                       ((gg * a + bgg * (15u - a)) / 15u) << 5 | (b * a + bb * (15u - a)) / 15u);
                }
            }
            k += (int32_t)n;
            px += (int32_t)n;
        }
    }
}

/* a line of the slogan in a language, centred, its cap top at y: each letter its own or, thrown, another of the
 * line's (a kana for a kana); `first` its first letter's number among the slogan's (the red ones' bits in `red`),
 * `landed` how many of them are back from the language's change */
static int noise_line(int li, int lang, int32_t y, uint32_t frame, int d, int first, int landed, uint32_t red,
                      uint16_t ink)
{
    const noise_glyph_t *G = li ? NOISE2_G : NOISE1_G;
    const uint8_t *R = li ? NOISE2_RUNS : NOISE1_RUNS, *ph = li ? NOISE2_PHRASE[lang] : NOISE1_PHRASE[lang];
    int32_t track = li ? NOISE2_TRACK : NOISE1_TRACK, space = li ? NOISE2_SPACE : NOISE1_SPACE, w = 0, x;
    uint32_t nlat = li ? NOISE2_LATIN : NOISE1_LATIN, nall = li ? NOISE2_N : NOISE1_N;
    uint32_t share = d ? 250u + 35u * (uint32_t)d / 10u : 0u;   /* (per mille: the letters thrown) */
    int n = first;
    for (int i = 0; ph[i] != 254u; i++)
        w += (ph[i] == 255u ? space : (int32_t)G[ph[i]].adv) + (i ? track : 0);
    x = (240 - w) / 2;
    for (int i = 0; ph[i] != 254u; i++) {
        if (ph[i] == 255u) {
            x += space + track;
            continue;
        }
        uint32_t gi = ph[i], h = rand_hash((uint32_t)(n * 3 + li), frame * 977u + trauma_turns);
        if (n >= landed || h % 1000u < share)   /* thrown: another of the line's, a kana for a kana */
            gi = G[gi].latin ? (h >> 10) % nlat : nlat + (h >> 10) % (nall - nlat);
        noise_glyph(&G[gi], R, x + ((int32_t)G[ph[i]].adv - (int32_t)G[gi].adv) / 2, y,
                    (red >> n) & 1u ? NOISE_RED : ink);
        x += (int32_t)G[ph[i]].adv + track;
        n++;
    }
    return n;
}

static void trauma_scene(uint32_t t, int d, uint32_t frame, int landed, int strobe)
{
    uint16_t ink = strobe ? 0x0000u : 0xFFFFu, dim = strobe ? 0xBDF7u : 0x4208u;
    uint32_t red = 0, shake = d ? frame * 50u / 90u : 0u;
    char line[16], v[8];
    if (t < TRAUMA_OPEN_MS) {            /* the bars from both sides, quick then slowing, then gone */
        float q = (float)t / (float)TRAUMA_OPEN_MS, r = 1.0f - (1.0f - q) * (1.0f - q) * (1.0f - q);
        for (int32_t j = 0; j < 12; j++) {
            uint32_t h = rand_hash((uint32_t)j, 77u);
            int32_t len = (int32_t)((60.0f + (float)(h % 60u)) * r * (1.0f - q * q));
            cv_rect(0, j * 20 + 4, len, 12, 0xFFFFu);
            cv_rect(240 - len, j * 20 + 4, len, 12, 0xFFFFu);
        }
    }
    for (int k = 0; d && k < 1 + d / 40; k++)   /* the red letters: one, two from 40 %, three at 80 % */
        red |= 1u << (rand_hash((uint32_t)k + 40u, shake * 7u + trauma_turns) % 16u);
    if (t >= 120u) {                     /* the first line's cap top 17 px down, the second's 4 px under its base */
        int n = noise_line(0, trauma_lang, 17, frame, d, 0, landed, red, ink);
        noise_line(1, trauma_lang, 17 + NOISE1_CAP + 4, frame, d, n, landed, red, ink);
    }
    if (t >= 300u) {                     /* the dose, its gauge drawn out; at 100 % both past it */
        int over = d >= 100 && t >= 520u;
        int_text(over ? 100 + (int)(frame * 37u % 900u) : d, v);
        line[0] = 0;
        text_cat(line, v, sizeof line);
        text_cat(line, " %", sizeof line);
        cv_text(232 - text_w(&FONT_L, line), 176, &FONT_L, line, over ? NOISE_RED : ink);
        float uu = t >= 520u ? 1.0f : (float)(t - 300u) / 220.0f;
        int32_t bw = (int32_t)(140.0f * uu), fw = (int32_t)(140.0f * uu * (float)d * 0.01f);
        cv_rect(10, 222, bw, 6, dim);
        cv_rect(10, 222, fw, 6, ink);
        if (over)                        /* out of its frame to the edge, broken where it goes */
            for (int32_t x = 150, k = 0; x < 240; k++) {
                uint32_t h = rand_hash((uint32_t)k + 60u, frame);
                int32_t run = 6 + (int32_t)(h % 18u), gap = (int32_t)((h >> 8) % 5u);
                cv_rect(x, 222 - (int32_t)((h >> 12) % 3u), run, 6, NOISE_RED);
                x += run + gap;
            }
    }
}

static void draw_trauma(void)
{
    static int32_t shown[6] = {-1, -1, -1, -1, -1, -1};
    uint32_t now = plat_ms(), t = now - ui.trauma_at, frame;
    int d = eng_project()->trauma > 100 ? 100 : eng_project()->trauma, landed;
    int strobe = d >= 80 && t >= 520u && (now / 60u) % (d >= 100 ? 2u : d >= 90 ? 3u : 4u) == 0u;
    if (!d) {                            /* at 0: the French, still */
        trauma_lang = 0;
        trauma_lang_at = now - 1000u;
    } else if (now - trauma_lang_at >= 1400u - 7u * (uint32_t)d) {
        trauma_lang = (trauma_lang + 1) % NOISE_LANGS;
        trauma_lang_at = now;
    }
    frame = d ? now / 50u : 0u;
    landed = (int)((now - trauma_lang_at) / 25u);   /* (a letter back each 25 ms after a change) */
    landed = landed > 16 ? 16 : landed;
    int32_t s[6] = {t < 600u ? (int32_t)t : -2, d, (int32_t)frame, (int32_t)trauma_turns, strobe,
                    trauma_lang * 32 + landed};
    if (!ui.full && !memcmp(s, shown, sizeof s))
        return;                          /* (nothing moved) */
    memcpy(shown, s, sizeof s);
    for (int b = 0; b < 3; b++) {        /* in three bands of 80 rows, each drawn in screen rows */
        cv_begin(240, 80, strobe ? 0xFFFFu : 0x0000u);
        cv_oy = -80 * b;
        cv_y0 = 0;
        cv_y1 = 80;
        trauma_scene(t, d, frame, landed, strobe);
        cv_oy = 0;
        blit(0, (uint32_t)(80 * b));
    }
}

static void trauma_open(void)
{
    if (!ui.trauma) {
        ui.trauma = 1;
        ui.trauma_at = plat_ms();
        ui.full = 1;
        trauma_lang = 0;                 /* (the French first, its letters landing) */
        trauma_lang_at = ui.trauma_at;
    }
}

static void trauma_close(void)
{
    if (ui.trauma) {
        ui.trauma = 0;
        ui.full = 1;
        ui.head_shown[0] = -1;
        ui.foot_shown[0] = -1;
    }
}

static void draw(void)
{
    int k = held(B_GLO) && !all.press && (ui.used[B_GLO] || plat_ms() - ui.down_at[B_GLO] >= 250u);
    const theme_t *t = &THEMES[eng_project()->theme % NTHEMES];
    all_poll();
    if (t != th) {                       /* the colours: another theme (K4 on GLO's second face, a project read) */
        th = t;
        ui.full = 1;
    }
    if (k != knock) {                    /* ALL: in yellow and black while GLO is held, then as it was */
        knock = k;
        ui.full = 1;
    }
    if (intro_on) {                      /* power-on: the intro, until its end or a button */
        uint32_t it = plat_ms() - intro_at;
        if (it < INTRO_MS && !ui.btn && data_ok && engine_ok) {
            draw_intro(it);
            return;
        }
        intro_on = 0;
        ui.full = 1;
    }
    if (ui.dice && data_ok && engine_ok) {   /* OCT- held, SELECT: the random dose's own screen, over everything */
        draw_random();
        poll_keys();
        ui.full = 0;
        ui.body_panel = ui.body_knobs = 0;
        return;
    }
    if (pat_open && data_ok && engine_ok) {   /* SEQ held: the patterns' screen, over everything */
        draw_patterns();
        poll_keys();
        ui.full = 0;
        ui.body_panel = ui.body_knobs = 0;
        return;
    }
    if (pre_open && data_ok && engine_ok) {   /* EDIT held: the presets' screen, over everything */
        draw_presets();
        poll_keys();
        ui.full = 0;
        ui.body_panel = ui.body_knobs = 0;
        return;
    }
    if (ui.trauma && data_ok && engine_ok) {   /* OCT+ held, SELECT: TRAUMA's own screen, over everything */
        draw_trauma();
        poll_keys();
        ui.full = 0;
        ui.body_panel = ui.body_knobs = 0;
        return;
    }
    if (ui.full && (!data_ok || !engine_ok))
        lcd_fill(0, 0, 240, 240, BG);    /* (every other screen covers it all, part by part, each in one go: drawn
                                          * again over what was there, never through a blank frame) */
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
    ui.cview = 0;                        /* (another page: a step's notes no longer shown) */
    page = shown_page(page);
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
    if (plane(page) >= ENG_TRACKS)
        ui.fx_page = page;               /* FX goes back to it */
    ui.page = page;
    ui.touched_k = -1;
    ui.matrix = 0;
}
static void go_page(int page) { go_page_dir(page, 0); }

/* GLO, REC held: a new project (four Plaits, nothing patched), the demo groove, or the saved project again. The
 * transport stops; nothing is saved until SAVE is held */
static void project_init(int which)
{
    project_t *p = eng_project();        /* the machine's settings stay (the FM-1's, not the music's): the output,
                                          * Warps' quality, the colours */
    uint8_t out = p->out_mode, hq = p->warps_hq, theme = p->theme;
    eng_play(0);
    pattern_forget();
    if (which == 2) {
        project_load();
        app_say("SAVED PROJECT", "LOADED");
    } else {
        if (which == 0)
            eng_blank(&scratch);
        else if (which == 1)
            eng_groove(&scratch);
        else if (which == 3)
            eng_stress(&scratch);
        else
            eng_stress_fm(&scratch);
        *eng_project() = scratch;
        eng_fresh();
        dirty = 1;
        app_say(which == 0 ? "NEW PROJECT" : which == 1 ? "DEMO GROOVE" : "STRESS TEST",
                which == 0 ? "4 FM" : which == 1 ? "LOADED" : which == 3 ? "PLAYING" : "FM DRUMS PLAYING");
    }
    p->out_mode = out;
    p->warps_hq = hq;
    p->theme = theme;
    ui.track = 0;
    set_lane(0);
    if (which >= 3) {                    /* the stress tests: playing, the machine's figures here, under it */
        eng_play(1);
        ui.full = 1;
        return;
    }
    go_page(PG_TRACK(0, 0));
}

/* a value moved e detents: 1 % a detent (3 fast) over 0..1, 2 % over -1..1, one for an integer or a choice */
static float step_value(int param, float v, int32_t e)
{
    const eng_pdef_t *d = eng_pdef(param);
    int32_t a = e > 1 || e < -1 ? e * 3 : e;
    if (eng_lx_knob(param) >= 0 || eng_voc_play(param))   /* an FM drum's (VOCODER's PLAY): its values, one a detent */
        return eng_lx_step(param, v, (int)a);
    if (d->kind == K_UNI)
        return fm_clampf(v + 0.01f * (float)a, 0.0f, 1.0f);
    if (d->kind == K_BIP)
        return fm_clampf(v + 0.02f * (float)a, -1.0f, 1.0f);
    return (float)clamp((int32_t)(v + (v < 0 ? -0.5f : 0.5f)) + e, d->lo, d->hi);
}

static void edit_param(int param, int32_t e)
{
    project_t *p = eng_project();
    const float was = p->val[param];
    p->val[param] = step_value(param, was, e);
    eng_static_mode(param, was);         /* (STATIC's MODE: the new generator's knobs) */
    dirty = 1;
}

/* the Euclidean face turned: the lane's trigs written again over its LENGTH (FILLS spread as evenly as they go, then
 * turned by ROTATE); its locks stay */
static void euclid_write(int l)
{
    project_t *p = eng_project();
    int len = lane_len(l), fills = (int)(p->val[PG_EUC(l) * 4] + 0.5f), rot = (int)(p->val[PG_EUC(l) * 4 + 1] + 0.5f);
    for (int st = 0; st < len; st++)
        eng_trig_set(l, st, eng_euclid(len, fills, rot, st));
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
 * knob's own value again; back at 0, its loop is drawn again (the loop side then plays a new one). GLO held too: the
 * four tracks' */
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
            if (r && !n)                 /* back at 0: the loop side's next loop another draw */
                eng_rnd_redraw(p, i, tp);
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
    int32_t old = k->depth, d = clamp(old + e * (e > 1 || e < -1 ? 10 : 4), -100, 100);
    if ((old > 0 && d < 0) || (old < 0 && d > 0))   /* through 0: it stops there, the cable gone (+2 went to -2) */
        d = 0;
    if (d == 0) {
        k->src = SRC_NONE;
        k->depth = 0;
    } else {
        k->depth = (int8_t)d;
    }
    dirty = 1;
}

static const uint8_t WHITE[16] = PLAT_WHITE_KEYS;
static int held_source(void)             /* MOD mode: the LFO whose key is held (the first three white keys: F, G, A),
                                          * SRC_NONE none */
{
    for (int i = 0; i < 3; i++)
        if (ui.keys & (1u << WHITE[i]))
            return SRC_L1 + i;
    return SRC_NONE;
}

/* the module buttons: their first page, and where a second press goes */
static void module_button(int b)
{
    int pt = ptype(ui.page), here = !ui.matrix, next = page_at(ui.page, page_face(ui.page) + 1);
    switch (b) {
    case B_EDIT: {                       /* the track's sets: its sound, then its bending, its filter (from anywhere
                                          * else: its sound) */
        int c = here ? track_cat(ui.page) : CAT_NONE, f[MAXFACES];
        cat_pages(c == CAT_SOUND ? CAT_BEND : c == CAT_BEND ? CAT_FILTER : CAT_SOUND, ui.track, f);
        go_page(f[0]);
        (void)next;
        break;
    }
    case B_LFO:                          /* the LFOs: 1, then 2, 3 */
        go_page(here && (pt == PT_LFO || pt == PT_FOLLOW) ? next : PG_LFO(0));
        break;
    case B_SEQ:                          /* the lane's steps (a track's: then its ratchets, its RANDOM face, its
                                          * Euclidean one) */
        if (here && (pt == PT_STEP || pt == PT_RND || pt == PT_CLIMB || ((pt == PT_EUC || pt == PT_PLAY) && ptrack(ui.page) >= 0)))
            go_page(next);
        else
            go_page(PG_STEP(ui.lane));
        break;
    case B_ARP:                          /* Endless, the active track's (again: its second face, 0.16.2) */
        go_page(here && (pt == PT_ARP || pt == PT_ARP2 || pt == PT_ARP3) ? next : PG_ARP(ui.track));
        break;
    case B_HOME:
        go_page(here && (pt == PT_MIX || pt == PT_PAN || pt == PT_SPREAD || pt == PT_EQ || pt == PT_DUCK ||
                         pt == PT_COMP || pt == PT_OTT) ? next : PG_MIX);
        break;
    case B_FX: {                         /* the effect shown last; on an effect: the next one (delay, reverb, Warps) */
        int l = plane(ui.page);
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

static void lrec_set(int on);
static void button_down(int b)
{
    ui.used[b] = 0;
    switch (b) {
    case B_PLAY:                         /* the clock runs or stops; again at once (STOP twice): stopped, every sound
                                          * cut, the effects' tails too */
        if (held(B_GLO)) {               /* (0.16.31) GLO and PLAY: the metronome on or off */
            ui.noclick = !ui.noclick;
            ui.used[B_GLO] = 1;
            eng_click(ui.lrec && !ui.noclick);
            app_say("METRONOME", ui.noclick ? "OFF" : "ON");
            break;
        }
        if (held(B_REC)) {               /* (0.16.31) REC and PLAY: live recording, on or off */
            ui.lr_combo = 1;
            ui.long_done[B_REC] = 1;
            lrec_set(!ui.lrec);
            break;
        }
        if (ui.play_at && plat_ms() - ui.play_at < CUT_MS) {
            eng_cut();
            ui.play_at = 0;
            app_say("ALL SOUND", "CUT");
        } else {
            eng_play(!eng_playing());
            ui.play_at = plat_ms();
        }
        if (ui.lrec && !eng_playing())   /* (stopped: no more recording) */
            lrec_set(0);
        break;
    case B_OCTDN: case B_OCTUP:          /* the keyboard's octave; STEP mode: the steps' page. Both held: SELECT turns
                                          * the random dose (the first one's octave put back) */
        if (held(b == B_OCTUP ? B_OCTDN : B_OCTUP)) {
            if (!ui.dice && !ui.trauma) {
                ui.octave = ui.oct_was;
                ui.spage = ui.spage_was;
            }
            trauma_close();              /* (TRAUMA open, OCT- pressed too: the random dose's, as both held) */
            dice_open();
            break;
        }
        ui.oct_was = ui.octave;
        ui.spage_was = ui.spage;
        if (ui.grid)
            ui.spage = clamp(ui.spage + (b == B_OCTUP ? 1 : -1), 0, (lane_len(ui.lane) - 1) / 16);
        else
            ui.octave = clamp(ui.octave + (b == B_OCTUP ? 1 : -1), -3, 3);
        break;
    case B_GLO:                          /* ALL, or its undo */
        all_down();
        break;
    case B_SEL: case B_REC:              /* both held: every cable erased, on REC's hold from the second press (SEL
                                          * neither opens the matrix nor toggles MOD, REC not STEP) */
        if (held(b == B_SEL ? B_REC : B_SEL)) {
            ui.combo = 1;
            ui.long_done[B_SEL] = 1;
            ui.down_at[B_REC] = plat_ms();
            ui.long_done[B_REC] = 0;
        }
        break;
    case B_SAVE: case B_ENV:
        break;                           /* on release (a tap), held long, or held with a knob */
    case B_SEQ:                          /* a tap: the steps (let go); held: the patterns (0.15) */
        pat_used = 0;
        chain_new_n = 0;
        break;
    case B_EDIT:                         /* a tap: the track's sets (let go); held: the presets (0.15.1) */
        pre_used = 0;
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

static void lane_erase(int l)            /* its pattern, its randoms; a track's cables too; its SPEED x1, forward */
{
    project_t *p = eng_project();
    eng_steps_clear(l);
    p->speed[l] = 0;
    p->dir[l] = DIR_FWD;
    if (l < ENG_TRACKS)                  /* (0.16.7) CLIMB off, its options as new */
        p->val[PG_EUC(l) * 4 + 2] = 0.0f, p->endless_opt[l][3] = 0;
    for (int i = 0; i < ENG_NTP; i++)
        p->rnd[l][i] = 0;
    if (l < ENG_TRACKS)
        cables_erase(l);
}

static void erase_all(void)              /* GLO and REC held 3 s: the project as a new one has it (INIT, eng_blank)
                                          * but for the four tracks' sounds (their models and their three pages: the
                                          * sound, the envelope, the pitch), the tempo, the scale and its root, and
                                          * the machine's settings (the output, Warps' quality, the colours). So: no
                                          * steps, locks, randoms, cables or Endless; the levels as new, the pans in
                                          * the middle, no filter, no send, the bending off, the LFOs, the effects, the
                                          * duck and the bus as new, no swing, the random dose and TRAUMA at 0 */
{
    project_t *p = eng_project();
    pat_unstage();                       /* (scratch: no next pattern in it) */
    eng_blank(&scratch);
    for (int t = 0; t < ENG_TRACKS; t++)
        for (int pg = 0; pg < 3; pg++) {
            for (int k = 0; k < 4; k++)
                scratch.val[PG_TRACK(t, pg) * 4 + k] = p->val[PG_TRACK(t, pg) * 4 + k];
            scratch.algo[PG_TRACK(t, pg)] = p->algo[PG_TRACK(t, pg)];
        }
    for (int k = 0; k < 3; k++)          /* TEMPO, SCALE, ROOT */
        scratch.val[PG_GLOBAL * 4 + k] = p->val[PG_GLOBAL * 4 + k];
    scratch.out_mode = p->out_mode;
    scratch.warps_hq = p->warps_hq;
    scratch.theme = p->theme;
    *p = scratch;
    eng_fresh();                         /* (no trig's lock or random left) */
}

static void button_long(int b)
{
    if (b == B_SEQ) {                    /* the patterns' screen, as long as SEQ is held */
        pat_open = 1;
        ui.full = 1;
        return;
    }
    if (b == B_EDIT) {                   /* the presets' screen, as long as EDIT is held */
        pre_open = 1;
        ui.full = 1;
        return;
    }
    if ((b == B_REC && held(B_SEQ)) || ((b == B_REC || b == B_SAVE) && held(B_EDIT)))
        return;
    if (b == B_REC && held(B_SEL)) {     /* REC and SEL: every cable, the tracks' and the global ones */
        for (int g = 0; g <= ENG_TRACKS; g++)
            cables_erase(g);
        ui.held_step = -1;
        dirty = 1;
        ui.full = 1;
        app_say("ALL CABLES", "ERASED");
    } else if (b == B_REC && held(B_GLO) && ui.erec < 0 && ui.page != VP_SYS) {
        erase_all();
        ui.used[B_GLO] = 1;
        all.press = 1;                   /* (not an ALL to undo) */
        ui.held_step = -1;
        dirty = 1;
        ui.full = 1;
        app_say("ALL", "ERASED");
    } else if (b == B_SEL && held(B_REC)) {
    } else if (b == B_SEL && ui.held_step < 0) {
        ui.matrix = 1;
        ui.mod = 0;
        ui.grid = 0;
        ui.msel = 0;
        ui.full = 1;
    } else if (b == B_SAVE) {            /* the pattern in its slot, and the project (the backup's, the next power-on's) */
        char n[6], l1[16];
        int e;
        pat_name(pat_cur, n);
        l1[0] = 0;
        text_cat(l1, "PATTERN ", sizeof l1);
        text_cat(l1, n, sizeof l1);
        if (eng_playing())
            app_say("STOP FIRST", "THEN SAVE");
        else if (project_save() != 0)
            app_say("COULD NOT", "SAVE");
        else if ((e = pat_save(pat_cur, eng_project(), sizeof(project_t))) == 0)
            app_say(l1, "SAVED");
        else
            app_say(l1, e == -1 ? "NO ROOM LEFT" : "NOT SAVED");
    } else if (b == B_REC) {
        if (ui.page == VP_SYS) {
            project_init(ui.init_sel);
        } else if ((ptype(ui.page) == PT_ARP || ptype(ui.page) == PT_ARP2) && ui.erec < 0 && !ui.grid) {   /* Endless: its
                                          * notes cleared */
            eng_endless_clear(ui.track);
            dirty = 1;
            app_say("ENDLESS", "CLEARED");
        } else if (ui.erec < 0 && (ui.grid || ptype(ui.page) == PT_STEP || ptype(ui.page) == PT_RND ||
                                   ptype(ui.page) == PT_LANE || ptype(ui.page) == PT_EUC || ptype(ui.page) == PT_PLAY ||
                                   ptype(ui.page) == PT_CLIMB)) {
            /* the steps: the lane erased (its trigs, locks, randoms; a track's cables), with GLO held all of them
             * and the global cables (the effects' knobs stay) */
            static const char *const FX[3] = {"DELAY STEPS", "REVERB STEPS", "WARPS STEPS"};
            char n[16];
            int l = erase_lane();
            if (l >= 0) {
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
    case B_SEQ:                          /* a tap: the lane's steps; held: the order made, the screen closed */
        if (!pat_used && !ui.long_done[b])
            module_button(B_SEQ);
        pattern_seq_up();
        break;
    case B_EDIT:                         /* a tap: the track's sets; held: the screen closed */
        if (!pre_used && !ui.long_done[b])
            module_button(B_EDIT);
        preset_edit_up();
        break;
    case B_SEL:
        if (ui.combo && !held(B_REC))
            ui.combo = 0;
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
        if (ui.lr_combo) {               /* (REC and PLAY: let go, nothing else) */
            ui.lr_combo = 0;
            break;
        }
        if (ui.combo) {                  /* (REC and SEL: let go, nothing else) */
            if (!held(B_SEL))
                ui.combo = 0;
            break;
        }
        if (ui.long_done[b])
            break;
        if (ui.lrec) {                   /* (0.16.31) live recording: off, the steps playing on */
            lrec_set(0);
            break;
        }
        if (ui.erec >= 0) {              /* Endless: recorded, it plays */
            char n[8];
            eng_endless_rec(ui.erec, 0);
            int_text(eng_project()->endless_len[ui.erec], n);
            app_say(eng_project()->endless_len[ui.erec] ? "ENDLESS PLAYS" : "ENDLESS", eng_project()->endless_len[ui.erec] ? n : "EMPTY");
            ui.erec = -1;
            ui.full = 1;
            break;
        }
        if ((pt == PT_ARP || pt == PT_ARP2) && !ui.grid) {   /* Endless: recording from nothing */
            eng_endless_clear(ui.track);
            eng_endless_rec(ui.track, 1);
            ui.erec = ui.track;
            ui.grid = ui.mod = 0;
            ui.held_step = -1;
            dirty = 1;
            ui.full = 1;
            break;
        }
        if (ui.cview) {                  /* (0.16.16) a step's notes: back to the steps */
            ui.cview = 0;
            ui.held_step = -1;
            ui.full = 1;
            break;
        }
        if (ui.grid && ui.held_step >= 0 && ui.lane < ENG_TRACKS && eng_trig(ui.lane, ui.held_step)) {
            ui.cview = 1;                /* a trig held: its notes, the keys playing them in or out */
            ui.cstep = ui.held_step;
            ui.clane = ui.lane;
            ui.held_step = -1;
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
    case B_ENV:                          /* a tap: the track's envelopes (again: their next page); held with a knob: its
                                          * random */
        if (!ui.used[b]) {
            int f[MAXFACES];
            if (!ui.matrix && track_cat(ui.page) == CAT_ENV)
                go_page(page_at(ui.page, page_face(ui.page) + 1));
            else
                cat_pages(CAT_ENV, ui.track, f), go_page(f[0]);
        }
        ui.full = 1;
        break;
    case B_OCTDN: case B_OCTUP:          /* the random dose, TRAUMA: they stay as turned */
        if (ui.dice && !(held(B_OCTDN) && b == B_OCTUP))   /* (OCT+ let go, OCT- still held: it stays open) */
            dice_close();
        if (ui.trauma && b == B_OCTUP)
            trauma_close();
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
        int32_t old = c->depth, d = clamp(old + e * 2, -100, 100);
        c->depth = (int8_t)((old > 0 && d < 0) || (old < 0 && d > 0) ? 0 : d);   /* (through 0: it stops there) */
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
    int pl = page_lane(ui.page), pt = ptype(ui.page);
    int steps = pt == PT_STEP || pt == PT_RND || pt == PT_LANE || pt == PT_PLAY || pt == PT_CLIMB;
    ui.held_step = -1;
    if (pt == PT_EUC) {                  /* the Euclidean face: the other lane's */
        if (l < ENG_TRACKS)
            set_track(l);
        if (pl != l)
            go_page(PG_EUC(l));
    } else if (l < ENG_TRACKS) {
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
/* (0.16.16) a step's notes (a trig held and REC): the keys play their notes in or out of it (four at most, one at
 * least; heard as played), the knobs move them by semitones; the machines that play chords play them all, the others
 * the lowest (the rest shown dotted) */
static void chord_key(int note, int down)
{
    int nt[4], n = eng_step_notes(ui.clane, ui.cstep, nt), v = note - 48, at = -1;
    eng_key(ui.clane, note, down);       /* (heard) */
    if (!down)
        return;
    for (int i = 0; i < n; i++)
        if (nt[i] == v)
            at = i;
    if (at >= 0) {                       /* in it: out (the last one stays) */
        if (n == 1)
            return;
        for (int i = at; i < n - 1; i++)
            nt[i] = nt[i + 1];
        n--;
    } else if (n == 4) {
        app_say("CHORD", "4 NOTES AT MOST");
        return;
    } else {
        nt[n++] = v;
    }
    int e = eng_step_notes_set(ui.clane, ui.cstep, nt, n);
    if (e == -2)
        app_say("CHORD", "OUT OF RANGE");
    else if (e)
        app_say("CHORD", "NO ROOM");
    dirty = 1;
}

static void chord_turn(int k, int32_t e)
{
    int nt[4], n = eng_step_notes(ui.clane, ui.cstep, nt);
    if (k >= n)
        return;
    nt[k] += e > 0 ? 1 : -1;
    if (eng_step_notes_set(ui.clane, ui.cstep, nt, n) == 0)
        dirty = 1;
}

static void draw_chord_knob(int k, int force)
{
    int nt[4], n = eng_step_notes(ui.clane, ui.cstep, nt), poly = eng_track_chords(ui.clane);
    int hot = ui.touched_k == k && plat_ms() - ui.touched_at[k] < 1200u, on = k < n, off = on && k > 0 && !poly;
    uint16_t ac = track_accent(ui.clane);
    char txt[12], lab[12], under[24];
    lab[0] = under[0] = 0;
    text_cat(lab, "NOTE ", sizeof lab);
    int_text(k + 1, txt);
    text_cat(lab, txt, sizeof lab);
    txt[0] = 0;
    if (on)
        note_text(48 + nt[k], txt);
    else
        text_cat(txt, "-", sizeof txt);
    int32_t s[4] = {k | on << 4 | off << 5 | 1 << 23, text_hash(txt) * 31 + text_hash(lab), 0xC4D, hot | (int32_t)ac << 2};
    if (!force && !ui.full && !ui.body_knobs && !memcmp(s, ui.knob_shown[k], sizeof s))
        return;
    memcpy(ui.knob_shown[k], s, sizeof s);
    cv_begin(KNOB_W, KNOB_H, BG);
    {
        int w = text_w(&FONT_S, lab);
        cv_text(30 - w / 2, 0, &FONT_S, lab, hot ? HI : MUTED);
        if (hot)
            cv_rect(30 - w / 2, 17, w, 1, ac);
    }
    if (!on) {
        arc(RX, RY, R0 + 0.5f, R1 - 0.5f, 0.0f, 1.0f, TRACK);
    } else if (off) {                    /* (a machine of one voice: not played, dotted) */
        for (float q = 0.0f; q < 1.0f; q += 0.05f)
            arc(RX, RY, R0 + 0.5f, R1 - 0.5f, q, q + 0.022f, MUTED);
        text_cat(under, "MONO", sizeof under);
    } else {
        lit_arc(RX, RY, R0, R1, 0.0f, 1.0f, ac);
    }
    ring_text(txt, on && !off ? HI : DIM, under, sizeof under);
    if (under[0])
        cv_text(30 - text_w(&FONT_XS, under) / 2, 78, &FONT_XS, under, MUTED);
    blit((uint32_t)(k * KNOB_W), KNOB_Y);
}

static void panel_chord(void)            /* the step's notes on two octaves of keys, its chord's name, mono or not */
{
    int nt[4], n = eng_step_notes(ui.clane, ui.cstep, nt), poly = eng_track_chords(ui.clane);
    int32_t s[3] = {0xC40 | ui.cstep << 12 | ui.clane << 20 | poly << 23, n, 0};
    for (int i = 0; i < n; i++)
        s[2] = s[2] * 97 + nt[i] + 40;
    if (!ui.full && !ui.body_panel && !memcmp(s, ui.panel_shown, sizeof s))
        return;
    memcpy(ui.panel_shown, s, sizeof s);
    uint16_t ac = track_accent(ui.clane);
    char t[40], v[16];
    cv_begin(240, PANEL_H, BG);
    t[0] = 0;
    text_cat(t, "STEP ", sizeof t);
    int_text(ui.cstep + 1, v);
    text_cat(t, v, sizeof t);
    text_cat(t, "  ", sizeof t);
    if (n >= 2) {
        eng_chord_name(nt, n, v, sizeof v);
    } else {
        note_text(48 + nt[0], v);
    }
    text_cat(t, v, sizeof t);
    cv_text(8, 0, &FONT_M, t, HI);
    const char *badge = poly ? "CHORDS" : "MONO: THE LOWEST";
    cv_text(232 - text_w(&FONT_XS, badge), 6, &FONT_XS, badge, poly ? ac : MUTED);
    /* the keys: two octaves from the C under the lowest (three if the chord is wider) */
    int lo = 48 + nt[0], c0 = lo - ((lo % 12) + 12) % 12, oct = 48 + nt[n - 1] - c0 >= 24 ? 3 : 2;
    int nw = 7 * oct, kw = 224 / nw, x0 = (240 - kw * nw) / 2, y0 = 30, h = 44;
    static const int8_t WH[7] = {0, 2, 4, 5, 7, 9, 11}, BL[5] = {1, 3, 6, 8, 10}, BX[5] = {0, 1, 3, 4, 5};
    for (int pass = 0; pass < 2; pass++)
        for (int o = 0; o < oct; o++)
            for (int j = 0; j < (pass ? 5 : 7); j++) {
                int m = c0 + 12 * o + (pass ? BL[j] : WH[j]), in = -1;
                for (int i = 0; i < n; i++)
                    if (48 + nt[i] == m)
                        in = i;
                int x = pass ? x0 + (o * 7 + BX[j]) * kw + kw - kw / 3 : x0 + (o * 7 + j) * kw;
                int w = pass ? (kw * 2) / 3 : kw - 1, hh = pass ? h * 3 / 5 : h;
                uint16_t c = pass ? TRACK : MUTED;
                cv_rect(x, y0, w, hh, pass ? BG : TRACK);
                if (in >= 0 && (poly || in == 0)) {
                    c = ac;
                    cv_rect(x + 1, y0 + 1, w - 2, hh - 2, c);
                } else if (in >= 0) {        /* (ignored: dotted) */
                    for (int yy = y0 + 2; yy < y0 + hh - 2; yy += 3)
                        for (int xx = x + 2; xx < x + w - 2; xx += 3)
                            cv_rect(xx, yy, 1, 1, ac);
                } else if (pass) {
                    cv_rect(x + 1, y0 + 1, w - 2, hh - 2, TRACK);
                }
            }
    blit(0, PANEL_Y);
}

/* (0.16.15) a trig held (a), another step pressed (b): its length up to b (b before it: its own step again); with
 * ratchets, their span instead (its hits over those steps) */
static void step_span(int l, int a, int b)
{
    project_t *p = eng_project();
    float rv;
    char m[24], n[8];
    int sp, steps = b > a ? b - a + 1 : 1;
    int r = (int)((eng_lock(l, a, TP_RATCHET, &rv) ? rv : p->val[PG_STEP(l) * 4 + 2]) + 0.5f);
    int hits = eng_ratch_hits(r, &sp);
    m[0] = 0;
    if (hits > 1) {
        eng_lock_set(l, a, TP_RATCHET, (float)eng_ratch_join(hits, steps));
        int_text(hits, n);
        text_cat(m, n, sizeof m);
        text_cat(m, steps > 1 ? " OVER " : " ON 1 STEP", sizeof m);
        if (steps > 1) {
            int_text(steps, n);
            text_cat(m, n, sizeof m);
            text_cat(m, " STEPS", sizeof m);
        }
        app_say("RATCH", m);
    } else if (eng_step_tie_set(l, a, steps - 1) == 0) {
        int_text(steps, n);
        text_cat(m, n, sizeof m);
        text_cat(m, steps > 1 ? " STEPS" : " STEP", sizeof m);
        app_say("LENGTH", m);
    } else {
        app_say("LENGTH", "NO ROOM");
    }
    ui.step_edit = 1;                    /* (let go, the trig stays) */
    dirty = 1;
}

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
        if (down && ui.held_step >= 0 && st != ui.held_step && ln < ENG_TRACKS && eng_trig(ln, ui.held_step)) {
            step_span(ln, ui.held_step, st);   /* (0.16.15) a step held, another pressed: its length to there */
            return;
        }
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

/* (0.16.15) Endless' keys: the note to transpose it to (-1: none), the newest held; a key let go, the one before it
 * (none held: the transposition stays) */
static int endless_keys(int note, int down)
{
    int j = 0, was = ui.ekn ? ui.ekeys[ui.ekn - 1] : -1;
    for (int i = 0; i < ui.ekn; i++)
        if (ui.ekeys[i] != note)
            ui.ekeys[j++] = ui.ekeys[i];
    ui.ekn = j;
    if (down) {
        if (ui.ekn == 8) {
            for (int i = 1; i < 8; i++)
                ui.ekeys[i - 1] = ui.ekeys[i];
            ui.ekn = 7;
        }
        ui.ekeys[ui.ekn++] = note;
        return note;
    }
    return ui.ekn && was == note ? ui.ekeys[ui.ekn - 1] : -1;
}

/* (0.16.31) live recording (REC and PLAY): what the keys play written on the track's steps, at the nearest one (ahead:
 * not played when it gets there, the key was heard), its note (keys pressed within a step: a chord, four at most), held
 * over steps its length; PLAY red, REC blinking. The metronome (GLO and PLAY: on or off) counts a bar in from the stop */
static void lrec_set(int on)
{
    const char *m = on ? "ON" : "OFF";
    ui.lrec = on;
    ui.lr_s = -1;
    memset(ui.lr_trk, 0, sizeof ui.lr_trk);
    plat_play_red(on);
    eng_click(on && !ui.noclick);
    if (on) {
        ui.grid = ui.mod = ui.cview = 0;
        ui.held_step = -1;
        if (!eng_playing()) {            /* (from the stop, the metronome on: a bar to come in) */
            eng_count_in(ui.noclick ? 0 : 16);
            eng_play(1);
            ui.play_at = 0;
            m = ui.noclick ? m : "AFTER 1 BAR";
        }
        ui.full = 1;
    }
    app_say("LIVE REC", m);
}
static void lrec_key(int k, int note, int down)
{
    int t = down ? ui.track : ui.lr_trk[k] - 1, len, s, nt[4], n;
    float pos = t >= 0 ? eng_lane_pos(t) : -1.0f;
    if (pos < 0.0f)                      /* (counting in, stopped) */
        return;
    len = lane_len(t);
    if (!down) {                         /* held over so many steps: its length */
        n = (int)(pos - ui.lr_at[k] + 0.5f);
        if (n >= 2)
            eng_step_tie_set(t, ui.lr_step[k], (n > len ? len : n) - 1);
        ui.lr_trk[k] = 0;
        return;
    }
    s = eng_step_now(t);
    if (s < 0 || s >= len)
        return;
    if (pos - (float)(int)pos >= 0.5f && eng_project()->dir[t] == DIR_FWD) {   /* nearer the next: there */
        s = (s + 1) % len;
        eng_rec_skip(t, s);
    }
    n = s == ui.lr_s && t == ui.lr_t && pos - ui.lr_last < 1.0f ? eng_step_notes(t, s, nt) : 0;
    if (n < 4)
        nt[n++] = note - 48;
    eng_trig_set(t, s, 1);
    if (eng_step_notes_set(t, s, nt, n) == -1)
        app_say("LIVE REC", "NO ROOM");
    if (n == 1)
        eng_step_tie_set(t, s, 0);
    ui.lr_s = s, ui.lr_t = t, ui.lr_last = pos;
    ui.lr_at[k] = pos, ui.lr_step[k] = (int8_t)s, ui.lr_trk[k] = (int8_t)(t + 1);
    dirty = 1;
}

static void poll_keys(void)
{
    uint32_t keys = plat_keys(), k;
    if (keys == ui.keys)
        return;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int down = (keys & bit) && !(ui.keys & bit), up = !(keys & bit) && (ui.keys & bit);
        int note = 53 + (int)k + 12 * ui.octave;
        if (!down && !up)
            continue;
        if (held(B_SEQ)) {               /* the patterns' (a note let go meanwhile: let go) */
            pattern_key((int)k, down);
            if (up && !ui.grid && !ui.mod && ui.erec < 0)
                eng_key(ui.track, note, 0);
            continue;
        }
        if (held(B_EDIT)) {              /* the presets' (the same) */
            int heard = up && (int)k == pre_key;
            preset_key((int)k, down);
            if (up && !heard && !ui.grid && !ui.mod && ui.erec < 0)
                eng_key(ui.track, note, 0);
            continue;
        }
        if (ui.mod)                      /* MOD: the keys are the sources (held_source) */
            continue;
        if (ui.cview) {                  /* (0.16.16) a step's notes: a key plays its note in or out */
            chord_key(note, down);
        } else if (ui.grid) {
            grid_key((int)k, down);
        } else if (ui.erec >= 0) {       /* Endless recording: a note a key, heard */
            if (down && eng_endless_add(ui.erec, note) != 0)
                app_say("ENDLESS", "64 STEPS, FULL");
            eng_key(ui.erec, note, down);
            dirty = 1;
        } else if (eng_arp_opt(ui.track, AO_MODE) == AM_KEYS && ui.erec < 0) {   /* (0.16.13) the ARP: the keys held
                                          * its notes; stopped, a key held plays it */
            eng_arp_key(ui.track, note, down);
            if (down && !eng_playing())
                eng_endless_free(ui.track, 1);
            dirty = 1;
        } else if (eng_arp_opt(ui.track, AO_MODE) == AM_CHORD && ui.erec < 0) {   /* PADS' chord: stopped, a key held
                                          * plays it */
            if (down && !eng_playing())
                eng_endless_free(ui.track, 1);
        } else if (eng_endless_on(ui.track)) {   /* Endless: a key transposes it (0.16.11: stopped, it plays it from
                                          * there, while held); 0.16.15: the newest key held, back to the one
                                          * before when it is let go */
            int top = endless_keys(note, down);
            if (top >= 0) {
                eng_endless_key(ui.track, top);
                if (!eng_playing())
                    eng_endless_free(ui.track, 1);
                dirty = 1;
            }
        } else {
            eng_key(ui.track, note, down);
            if (ui.lrec)
                lrec_key((int)k, note, down);
        }
    }
    if (!keys) {                         /* (0.16.13: every key let go, the ARPs' not latched too; 0.16.15: the keys
                                          * held, on every track) */
        eng_arp_release_all();
        eng_key_release_all();
        ui.ekn = 0;
    }
    if (!keys || eng_playing())          /* (every key let go, or PLAY: Endless by the keys stops) */
        eng_endless_free(-1, 0);
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
    if (e && held(B_EDIT)) {             /* (the presets' screen: another track's; EDIT's tap not taken) */
        pre_used = 1;
        pre_hear_off();
        ui.full = 1;
    }
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
    if (e && held(B_EDIT))
        pre_used = 1;
    if (e && !ui.dice && held(B_OCTDN)) {   /* OCT- held, SELECT turned: the random dose's screen (the octave, or
                                          * the steps' page, OCT- moved put back) */
        ui.octave = ui.oct_was;
        ui.spage = ui.spage_was;
        ui.used[B_OCTDN] = 1;
        dice_open();
    }
    if (e && !ui.dice && !ui.trauma && held(B_OCTUP) && !held(B_OCTDN)) {   /* OCT+ held, SELECT turned: TRAUMA's
                                          * screen (the octave, or the steps' page, OCT+ moved put back) */
        ui.octave = ui.oct_was;
        ui.spage = ui.spage_was;
        ui.used[B_OCTUP] = 1;
        trauma_open();
    }
    if (e && ui.dice) {                  /* the random dose: 2 % a step (5 turned fast); from 0, another throw */
        project_t *p = eng_project();
        rand_turns++;
        if (!p->dice && e > 0)
            p->dice_seed++;
        p->dice = (uint8_t)clamp((int32_t)p->dice + e * (e > 1 || e < -1 ? 5 : 2), 0, 100);
        dirty = 1;
    } else if (e && ui.trauma) {         /* TRAUMA: 2 % a step (5 turned fast) */
        project_t *p = eng_project();
        trauma_turns++;
        p->trauma = (uint8_t)clamp((int32_t)p->trauma + e * (e > 1 || e < -1 ? 5 : 2), 0, 100);
        dirty = 1;
    } else if (e && !ui.matrix && page_count(ui.page) > 1)
        go_page_dir(page_at(ui.page, page_face(ui.page) + (e > 0 ? 1 : -1)), e > 0 ? 1 : -1);
    e = plat_enc(EN_ALGO);
    if (e && held(B_EDIT))
        pre_used = 1;
    if (e && !ui.matrix && (ptype(ui.page) == PT_WARPS1 || ptype(ui.page) == PT_WARPS2)) {
        /* Warps: ALGORITHM from mark to mark (its six cross-modulations, a fifth of the knob apart) */
        float *v = &eng_project()->val[PG_WARPS(0) * 4 + 2];
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
        eng_delay_mode(e > 0 ? DL_FREE : DL_SYNC);   /* the delay: on the tempo, or free (in ms), its time kept */
        dirty = 1;
        ui.touched_at[0] = now;
        ui.touched_k = ui.page == PG_DELAY(0) ? 0 : -1;
    } else if (e && !ui.matrix && (ptype(ui.page) == PT_COMP || ptype(ui.page) == PT_OTT)) {
        int m = clamp(bus_mode() + (e > 0 ? 1 : -1), MB_OFF, MB_OTT);   /* the master bus: off, COMP, OTT (its face) */
        eng_project()->algo[PG_COMP] = (uint8_t)m;
        dirty = 1;
        if (m != MB_OFF && ui.page != (m == MB_OTT ? PG_OTT : PG_COMP))
            go_page_dir(m == MB_OTT ? PG_OTT : PG_COMP, e > 0 ? 1 : -1);
        ui.full = ui.slide_dir ? ui.full : 1;
    } else if (e && !ui.matrix && track_cat(ui.page) == CAT_FILTER) {
        project_t *p = eng_project();    /* the filter's model (OFF, LADDER, MS-20, SEM, VOX); GLO held: the four's */
        int t = ptrack(ui.page), m = clamp((int32_t)p->algo[PG_FILT(t, 0)] + e, 0, MF_MODELS - 1);
        for (int i = 0; i < ENG_TRACKS; i++)
            if (i == t || held(B_GLO))
                p->algo[PG_FILT(i, 0)] = (uint8_t)m;
        if (held(B_GLO))
            ui.used[B_GLO] = 1;
        dirty = 1;
        ui.body_panel = 1;
        ui.anim = 1;                     /* (its name written again) */
        ui.anim_at = now;
    } else if (e && !ui.matrix && algo_page(ui.page) >= 0) {
        project_t *p = eng_project();
        int first = algo_page(ui.page), n = eng_algo_count(first), snd = track_cat(ui.page) == CAT_SOUND;
        if (n) {
            int a = clamp((int32_t)p->algo[first] + e, 0, n - 1);
            if (snd && !held(B_GLO))     /* (a model once a project, another track's: past it) */
                while (!eng_model_free(eng_page_track(first), a) && a + (e > 0 ? 1 : -1) >= 0 && a + (e > 0 ? 1 : -1) < n)
                    a += e > 0 ? 1 : -1;
            if (snd && !held(B_GLO) && !eng_model_free(eng_page_track(first), a))
                a = p->algo[first];
            if (held(B_GLO) && snd) {    /* GLO held: the four tracks on that model (PADS: the first) */
                for (int t = 0; t < ENG_TRACKS; t++)
                    if (eng_model_free(t, a))
                        eng_set_model(t, a);
                ui.used[B_GLO] = 1;
            } else if (snd) {
                eng_set_model(eng_page_track(first), a);   /* (an FM drum: its kit on its knobs) */
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
        if (held(B_EDIT))                /* (EDIT's tap not taken: a knob turned while it was held) */
            pre_used = 1;
        if (ui.page == VP_SYS) {         /* K1 the project REC held loads, K2 the output, K3 Warps' quality, K4 the
                                          * colours */
            project_t *p = eng_project();
            int d = e > 0 ? 1 : -1;
            if (i == 0)
                ui.init_sel = clamp(ui.init_sel + d, 0, 4);
            else if (i == 1)
                p->out_mode = (uint8_t)clamp(p->out_mode + d, 0, 2);
            else if (i == 2)
                p->warps_hq = (uint8_t)clamp(p->warps_hq + d, 0, 1);
            else
                p->theme = (uint8_t)clamp(p->theme % NTHEMES + d, 0, NTHEMES - 1);
            if (i)
                dirty = 1, ui.full = 1;
            continue;
        }
        if (ui.matrix) {
            matrix_turn(i, e);
            continue;
        }
        if (ui.cview) {                  /* (0.16.16) a step's notes: each a knob, by semitones */
            chord_turn(i, e);
            ui.touched_at[i] = plat_ms();
            ui.touched_k = i;
            continue;
        }
        if (byte_knob(ui.page, i)) {     /* SPEED, DIRECTION, Endless' options: the project's bytes, not a lock's */
            play_turn(i, e);
            continue;
        }
        if (ratch_field(ui.page, i) && !held(B_ENV) && !ui.mod) {   /* RATCH, SPAN: its hits, its span, alone */
            ratch_turn(ratch_field(ui.page, i), knob_param(ui.page, i), e);
            ui.touched_at[i] = plat_ms();
            ui.touched_k = i;
            continue;
        }
        int param = knob_param(ui.page, i), h = ui.mod ? held_source() : SRC_NONE;
        if (held(B_GLO))
            ui.used[B_GLO] = 1;
        if (ptype(ui.page) == PT_RND) {
            edit_rnd(param, e);
        } else if (ptype(ui.page) == PT_EUC && i < 2) {   /* FILLS, ROTATE: the lane's trigs written again */
            edit_param(param, e);
            euclid_write(eng_page_lane(ui.page));
        } else if (held(B_ENV)) {
            ui.used[B_ENV] = 1;
            edit_rnd(param, e);
        } else if (ui.held_step >= 0) {
            edit_lock(param, e);
        } else if (h != SRC_NONE) {      /* an LFO's key held: its cable, deeper (or upside down) */
            cable_turn(h, param, e);
        } else if (!held(B_GLO) || !edit_all(param, e)) {
            edit_param(param, e);
        }
        if (ptype(ui.page) == PT_EUC && i == 2)   /* LENGTH on the Euclidean face: the rhythm over the new length */
            euclid_write(eng_page_lane(ui.page));
        ui.touched_at[i] = now;
        ui.touched_k = i;
    }
    /* lights: the page's button, PLAY, SEL in MOD mode, REC with the steps, OCT- turning the random dose, OCT+
     * TRAUMA;
     * the keys: the steps (the one playing blinks), or held; MOD mode: the LFOs' keys blinking at their rates */
    {
        static const uint8_t PAGE_BTN[PT_SPREAD + 1] = {
            [PT_PLAITS1] = B_EDIT, [PT_PLAITS2] = B_ENV, [PT_PLAITS3] = B_EDIT, [PT_LFO] = B_LFO, [PT_FOLLOW] = B_LFO,
            [PT_MIX] = B_HOME, [PT_PAN] = B_HOME, [PT_REVERB] = B_FX, [PT_RSEND] = B_FX, [PT_DELAY1] = B_FX,
            [PT_DELAY2] = B_FX, [PT_DSEND] = B_FX, [PT_STEP] = B_SEQ, [PT_LANE] = B_FX, [PT_GLOBAL] = B_GLO,
            [PT_ARP] = B_ARP, [PT_WARPS1] = B_FX, [PT_WARPS2] = B_FX, [PT_DUCK] = B_HOME, [PT_COMP] = B_HOME,
            [PT_OTT] = B_HOME, [PT_EQ] = B_HOME, [PT_BEND] = B_EDIT, [PT_GLITCH] = B_EDIT, [PT_EUC] = B_SEQ, [PT_RND] = B_SEQ,
            [PT_FILT1] = B_EDIT, [PT_FILT2] = B_EDIT, [PT_FILT3] = B_EDIT, [PT_SON2] = B_EDIT, [PT_VFLT1] = B_EDIT,
            [PT_VFLT2] = B_EDIT, [PT_ENV2] = B_ENV, [PT_SYS] = B_GLO, [PT_PLAY] = B_SEQ, [PT_ARP2] = B_ARP,
            [PT_CLIMB] = B_SEQ, [PT_ARP3] = B_ARP, [PT_SPREAD] = B_HOME};
        static const uint8_t W[16] = PLAT_WHITE_KEYS, Bk[11] = PLAT_BLACK_KEYS;
        uint32_t bb = ui.dice ? 1u << B_OCTDN : ui.trauma ? 1u << B_OCTUP : 0, kk = ui.keys, mk = 0;
        int pt = ptype(ui.page);
        if (!ui.matrix)
            bb |= 1u << ((pt == PT_EUC || pt == PT_PLAY) && ptrack(ui.page) < 0 ? B_FX : track_cat(ui.page) == CAT_ENV ? B_ENV
                         : track_cat(ui.page) != CAT_NONE ? B_EDIT : PAGE_BTN[pt]);
        if (ui.mod || ui.matrix)
            bb |= 1u << B_SEL;
        if (ui.grid || ui.page == VP_SYS || ((ui.erec >= 0 || ui.lrec) && (now / 250u) & 1u))   /* recording: it blinks */
            bb |= 1u << B_REC;
        if (eng_playing())
            bb |= LED_PLAY_GREEN;
        if (dirty)
            bb |= 1u << B_SAVE;
        if (ui.grid) {                   /* the lane's steps, its key; OCT- OCT+ lit when there are pages that way */
            int len = lane_len(ui.lane), st = eng_step_now(ui.lane);
            uint32_t m = ui.lane < ENG_TRACKS ? held_steps(ui.lane, len) : 0;
            kk = 0;
            for (int i = 0; i < 16; i++)
                if (eng_trig(ui.lane, ui.spage * 16 + i))
                    kk |= 1u << W[i];
                else if ((m >> i) & 1u)  /* (0.16.31) a long note: the steps it holds, half lit */
                    mk |= 1u << W[i];
            if (st >= ui.spage * 16 && st < ui.spage * 16 + 16)
                kk ^= 1u << W[st - ui.spage * 16];
            kk |= 1u << Bk[ui.lane];
            if (ui.spage > 0)
                bb |= 1u << B_OCTDN;
            if ((ui.spage + 1) * 16 < len)
                bb |= 1u << B_OCTUP;
        } else if (ui.mod) {
            for (int i = 0; i < 3; i++)
                if (eng_lfo_phase(i) < 0.5f)
                    kk |= 1u << W[i];
        }
        plat_leds(bb, kk);
        plat_lights_mid(0, mk);
        plat_glow(1);
    }
}

void app_input_only(void) { input(); }

/* --------------------------------------------------------------- frames --- */

void app_splash(void)                    /* at boot, while the platform and the engine start: black, the intro next */
{
    lcd_fill(0, 0, 240, 240, 0);
}

void app_ui_init(void)                   /* at power-on, and after an update, a restore or the tables' transfer */
{
    static int booted;
    data_ok = data_check();
    if (!data_ok)
        halted = 1;
    ui.full = 1;
    if (!booted && data_ok) {            /* (power-on only: the intro) */
        booted = 1;
        intro_on = 1;
        intro_at = plat_ms();
    }
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
    chain_service();
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
