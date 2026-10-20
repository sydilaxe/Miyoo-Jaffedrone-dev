#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SONG_MAGIC 0x4a44520au    /* JDR10: WIND engine (ENGINE, EXCITER, MATERIAL) and undertones (SUB) */
#define SONG_MAGIC_V8 0x4a445208u /* JDR8: chance, RELATE, HARM, string gates, binaural (v0.3) */
#define SONG_MAGIC_V7 0x4a445207u /* JDR7: sympathetic strings, pan, MOTION/HOME/COLOR/EVOLVE (v0.2) */
#define SONG_MAGIC_V6 0x4a445206u /* JDR6: scales, Euclidean rhythms, BUZZ, coupling, body (v0.1) */
#define SONG_MAGIC_V5 0x4a445205u /* JDR5: GLIDE global (v0.0.3) */
#define SONG_MAGIC_V4 0x4a445204u /* JDR4: five strings (v0.0.2) */
#define SONG_MAGIC_V3 0x4a445203u /* JDR3: four strings (v0.0.1) */
#define NS_V3 4
#define NSP_V5 10 /* note, fine, level, bow, pluck, decay, bright, fold, stiff, pos */
#define NGP_V5 8  /* mode, root, bpm, drift, glide, cutoff, master, sync */
#define NGP_V4 7  /* no GLIDE */
#define NFX_V5 9
typedef struct {
    uint32_t magic;
    int on[NS_V3];
    int sp[NS_V3][NSP_V5];
    int gp[NGP_V4];
    int lfo[NLFO][NLP];
    int fx[NFX_V5];
} SongV3;
typedef struct {
    uint32_t magic;
    int on[NS];
    int sp[NS][NSP_V5];
    int gp[NGP_V4];
    int lfo[NLFO][NLP];
    int fx[NFX_V5];
} SongV4;
typedef struct {
    uint32_t magic;
    int on[NS];
    int sp[NS][NSP_V5];
    int gp[NGP_V5];
    int lfo[NLFO][NLP];
    int fx[NFX_V5];
} SongV5;
#define NSP_V6 19
#define NGP_V6 10
#define NFX_V6 12
typedef struct {
    uint32_t magic;
    int on[NS];
    int sp[NS][NSP_V6];
    int gp[NGP_V6];
    int lfo[NLFO][NLP];
    int fx[NFX_V6];
} SongV6;
typedef struct {
    uint32_t magic;
    int on[NS];
    int sp[NS][20];
    int gp[14];
    int lfo[NLFO][NLP];
    int fx[NFX];
} SongV7;
typedef struct {
    uint32_t magic;
    int on[NS];
    int sp[NS][NSP];
    int gp[16];
    int lfo[NLFO][NLP];
    int fx[NFX];
} SongV8;

Song song;
Live live;

/* ------------------------------------------------------------ scales
 *
 * A scale is one octave of degrees in cents. SP_NOTE counts degrees above
 * the root, so a five-tone scale spans an octave in five steps. The style
 * decides what SELECT (and CHANGES) does next:
 *   ST_CHORD    chords stacked from the scale, moving by fourths, fifths,
 *               steps and thirds, drifting away and coming home, with smooth
 *               voice leading (common tones stay, the rest move least);
 *   ST_RAGA     a tanpura below (Sa and Pa, or Ma when the raga has no Pa)
 *               while the upper strings wander a step or two at a time;
 *   ST_GAMELAN  a gong tone, a kempyung pair, and a shimmering unison pair
 *               (ombak), the whole block moving in parallel. */
enum { ST_CHORD, ST_RAGA, ST_GAMELAN };
typedef struct {
    const char *name;
    int n;
    float cents[12];
    int style;
    const char *deg[12]; /* degree names; none: note names */
    int nh;              /* ST_CHORD: chords use these degrees (0: all) */
    int harm[7];
} Scale;

/* just ratios in cents: 16/15 111.73, 9/8 203.91, 7/6 266.87, 6/5 315.64,
 * 5/4 386.31, 4/3 498.04, 11/8 551.32, 45/32 590.22, 3/2 701.96, 8/5 813.69,
 * 13/8 840.53, 5/3 884.36, 27/16 905.87, 7/4 968.83, 9/5 1017.60, 15/8 1088.27 */
#define NSCALE 14
static const Scale scales[NSCALE] = {
    {"EQUAL", 12, {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100}, ST_CHORD, {0}, 7, {0, 2, 3, 5, 7, 9, 10}},
    {"JUST MAJOR", 7, {0, 203.91f, 386.31f, 498.04f, 701.96f, 884.36f, 1088.27f}, ST_CHORD, {0}, 0, {0}},
    {"JUST MINOR", 7, {0, 203.91f, 315.64f, 498.04f, 701.96f, 813.69f, 1017.60f}, ST_CHORD, {0}, 0, {0}},
    {"HARMONICS", 8, {0, 203.91f, 386.31f, 551.32f, 701.96f, 840.53f, 968.83f, 1088.27f}, ST_CHORD, {0}, 0, {0}},
    {"PENTA JUST", 5, {0, 203.91f, 386.31f, 701.96f, 884.36f}, ST_CHORD, {0}, 0, {0}},
    {"SEPTIMAL", 5, {0, 266.87f, 498.04f, 701.96f, 968.83f}, ST_CHORD, {0}, 0, {0}},
    {"YAMAN", 7, {0, 203.91f, 386.31f, 590.22f, 701.96f, 905.87f, 1088.27f}, ST_RAGA,
     {"Sa", "Re", "Ga", "MA", "Pa", "Dha", "Ni"}, 0, {0}},
    {"BHAIRAV", 7, {0, 111.73f, 386.31f, 498.04f, 701.96f, 813.69f, 1088.27f}, ST_RAGA,
     {"Sa", "re", "Ga", "Ma", "Pa", "dha", "Ni"}, 0, {0}},
    {"KAFI", 7, {0, 203.91f, 315.64f, 498.04f, 701.96f, 884.36f, 1017.60f}, ST_RAGA,
     {"Sa", "Re", "ga", "Ma", "Pa", "Dha", "ni"}, 0, {0}},
    {"BHUPALI", 5, {0, 203.91f, 386.31f, 701.96f, 884.36f}, ST_RAGA, {"Sa", "Re", "Ga", "Pa", "Dha"}, 0, {0}},
    {"MALKAUNS", 5, {0, 315.64f, 498.04f, 813.69f, 1017.60f}, ST_RAGA, {"Sa", "ga", "Ma", "dha", "ni"}, 0, {0}},
    {"SLENDRO", 5, {0, 231, 474, 717, 955}, ST_GAMELAN, {"ji", "ro", "lu", "mo", "nem"}, 0, {0}},
    {"PELOG", 7, {0, 120, 270, 540, 670, 785, 950}, ST_GAMELAN, {"ji", "ro", "lu", "pat", "mo", "nem", "pi"}, 0, {0}},
    {"SELISIR", 5, {0, 128, 262, 668, 792}, ST_GAMELAN, {"ding", "dong", "deng", "dung", "dang"}, 0, {0}},
};

static const ParamDef sparams[NSP] = {
    {"NOTE", -24, 24},  {"FINE", -50, 50}, {"LEVEL", 0, 100}, {"BOW", 0, 100},  {"DECAY", 0, 100},
    {"BRIGHT", 0, 100}, {"FOLD", 0, 100},  {"STIFF", 0, 100}, {"POS", 0, 100},  {"BUZZ", 0, 100},
    {"PLUCK", 0, 1},    {"P STEPS", 1, 16}, {"P PULSES", 1, 16}, {"P ROTATE", 0, 15},
    {"BOW RING", 0, 1}, {"B STEPS", 1, 16}, {"B PULSES", 1, 16}, {"B ROTATE", 0, 15}, {"RATE", 0, 5},
    {"PAN", -50, 50},   {"P CHANCE", 0, 100}, {"B CHANCE", 0, 100}, {"HARM", 0, 4}, {"GATE", 0, 100}};
static const ParamDef gparams[NGP] = {{"VOICING", 0, 1},   {"SCALE", 0, NSCALE - 1}, {"ROOT", 24, 72},
                                      {"CHANGES", 0, 5},   {"BPM", 40, 200},         {"DRIFT", 0, 100},
                                      {"GLIDE", 0, 7},     {"CUTOFF", 0, 100},       {"MASTER", 0, 100},
                                      {"WIFI SYNC", 0, NSYNCMODE - 1}, {"MOTION", 0, 100},
                                      {"HOME", 0, 100},    {"COLOR", 0, 100},        {"EVOLVE", 0, 100},
                                      {"CHANCE", 0, 100},  {"RELATE", 0, 2},          {"ENGINE", 0, NENGINE - 1},
                                      {"EXCITER", 0, NEXCITE - 1}, {"MATERIAL", 0, NMATERIAL - 1},
                                      {"SUB", 0, 100},     {"SUB DIV", 0, NSUBDIV - 1}, {"SUB RES", 0, 100}};
static const ParamDef lparams[NLP] = {{"RATE", 0, 6}, {"SHAPE", 0, NSH - 1}, {"TARGET", 0, NLT - 1}, {"DEPTH", 0, 100}};
static const ParamDef fparams[NFX] = {{"TIME", 0, 7},   {"FEEDBK", 0, 95}, {"MIX", 0, 100},
                                      {"SEND", 0, 100}, {"TIME", 0, 100},  {"TONE", 0, 100},
                                      {"BEAT", 0, 24},  {"DEPTH", 0, 100}, {"LOCK", 0, 1},
                                      {"COUPLE", 0, 100}, {"BODY", 0, 100}, {"TYPE", 0, NBODY - 1},
                                      {"SYMP", 0, NSYMP}, {"S TUNE", 0, NSTUNE - 1}, {"S OCT", -1, 2},
                                      {"S LEVEL", 0, 100}, {"S RING", 0, 100}};
/* rhythm step lengths in 16ths */
static const int rate_div16[6] = {1, 2, 4, 8, 16, 32};
static const char *const rate_names[6] = {"1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS"};
static const int change_bars[6] = {0, 1, 2, 4, 8, 16};
static const char *const change_names[6] = {"OFF", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS"};
static const char *const body_names[NBODY] = {"WOOD", "GOURD", "BRONZE", "HARMONIC"};
static const char *const stune_names[NSTUNE] = {"SCALE", "CHORD", "CHORD+", "DRONE", "FIFTHS"};
static const char *const note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const sync_names[NSYNCMODE] = {"OFF", "AUTO", "LEAD", "FOLLOW"};
static const float lfo_div16[7] = {64.f, 32.f, 16.f, 8.f, 4.f, 2.f, 1.f};
static const char *const lfo_rate_names[7] = {"4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16"};
static const char *const shape_names[NSH] = {"SINE", "TRI", "SAW", "SQUARE", "RANDOM"};
static const char *const target_names[NLT] = {"CUTOFF", "LEVEL", "PITCH", "BOW", "DELAY", "REVERB"};
static const float dly_div16[8] = {1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f};
static const char *const dly_names[8] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "3/4", "1 BAR"};
/* GLIDE: how long a note change takes to settle, in 16ths of the tempo */
static const float glide_div16[8] = {0.f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f};
static const char *const glide_names[8] = {"OFF", "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS"};
/* {note, fine, level, bow, decay, bright, fold, stiff, pos, buzz,
 *  pluck on, steps, pulses, rotate, bow ring on, steps, pulses, rotate, rate, pan,
 *  pluck chance, bow chance, harm, gate}
 * Notes are JUST MAJOR degrees: root, fifth, octave, twelfth, two octaves.
 * Each string has its own sparse pluck rhythm; the top string's bow ring
 * breathes in and out, the other bows play continuously. */
static const int def_sp[NS][NSP] = {{0, 0, 80, 35, 85, 45, 0, 0, 0, 0, 1, 4, 1, 0, 0, 4, 2, 0, 2, 0, 100, 100, 0, 0},
                                    {4, -4, 65, 45, 92, 40, 0, 0, 0, 0, 1, 5, 2, 0, 0, 5, 3, 0, 2, -22, 90, 100, 0, 0},
                                    {7, 3, 55, 40, 95, 55, 0, 0, 0, 0, 1, 3, 1, 1, 0, 3, 2, 0, 3, 22, 100, 100, 0, 0},
                                    {11, 0, 40, 50, 90, 65, 0, 0, 0, 0, 1, 7, 2, 3, 0, 7, 3, 0, 2, -12, 70, 100, 0, 0},
                                    {14, 2, 30, 55, 93, 70, 0, 0, 0, 0, 1, 6, 1, 2, 1, 5, 3, 0, 2, 12, 60, 80, 0, 0}};
static const int def_gp[NGP] = {MODE_CHORD, 1, 45, 4, 72, 30, 3, 70, 80, SYNC_OFF, 50, 50, 45, 30, 75, 1,
                                ENG_STRINGS, EX_FLUTE, MAT_WOOD, 0, SUB_OCT, 60};
static const char *const engine_names[NENGINE] = {"STRINGS", "WIND"};
static const char *const excite_names[NEXCITE] = {"LIP", "REED", "DOUBLE", "FLUTE", "FREE", "RECORDER", "BANSURI"};
static const char *const material_names[NMATERIAL] = {"WOOD", "METAL", "CERAMIC", "GLASS"};
static const int def_lfo[NLFO][NLP] = {{2, SH_SINE, LT_CUTOFF, 35}, {4, SH_TRI, LT_LEVEL, 0}};
static const int def_fx[NFX] = {5, 45, 25, 35, 60, 55, 0, 100, 0, 30, 35, BODY_WOOD, 7, STUNE_CHORD, 1, 45, 70};

const ParamDef *sparam_def(int p) { return &sparams[p]; }
/* in WIND the string controls take on their wind meanings */
const char *sparam_name(int p)
{
    if (song.gp[GP_ENGINE] == ENG_WIND)
        switch (p) {
        case SP_BOW: return "BREATH";
        case SP_DECAY: return "AIR";
        case SP_STIFF: return "EMBOUCH";
        case SP_POS: return "VIBRATO";
        case SP_BUZZ: return "GROWL";
        case SP_HARM: return "REG";
        case SP_PON: return "TONGUE";
        case SP_BON: return "BREATH RING";
        }
    return sparams[p].name;
}
const ParamDef *gparam_def(int p) { return &gparams[p]; }
const ParamDef *lparam_def(int p) { return &lparams[p]; }
const ParamDef *fparam_def(int p) { return &fparams[p]; }
const char *sync_mode_name(int m) { return sync_names[m]; }
int engine_nscales(void) { return NSCALE; }
const char *engine_scale_name(int sc) { return scales[sc].name; }

void note_name(int midi, char *b, int n) { snprintf(b, n, "%s%d", note_names[midi % 12], midi / 12 - 1); }

static const Scale *cur_scale(void) { return &scales[song.gp[GP_SCALE]]; }
static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int fmod_i(int a, int b) { return a - fdiv(a, b) * b; }
static float deg_cents(const Scale *sc, int d)
{
    int o = fdiv(d, sc->n);
    return o * 1200.f + sc->cents[d - o * sc->n];
}

void engine_degree_name(int d, char *b, int n)
{
    const Scale *sc = cur_scale();
    int o = fdiv(d, sc->n), i = d - o * sc->n;
    if (sc->deg[0]) {
        /* sargam / gamelan names, octave marks: ' above, , below */
        char marks[4] = {0};
        for (int k = 0; k < 3 && k < abs(o); k++)
            marks[k] = o > 0 ? '\'' : ',';
        snprintf(b, n, "%s%s", sc->deg[i], marks);
        return;
    }
    float m = song.gp[GP_ROOT] + deg_cents(sc, d) / 100.f;
    int mi = (int)floorf(m + 0.5f), dev = (int)lrintf((m - mi) * 100.f);
    char nm[16];
    note_name(mi < 0 ? 0 : mi, nm, sizeof nm);
    if (dev)
        snprintf(b, n, "%s%+d", nm, dev);
    else
        snprintf(b, n, "%s", nm);
}

void sparam_format(int s, int p, char *b, int n)
{
    int x = song.sp[s][p];
    switch (p) {
    case SP_NOTE: {
        char nm[12];
        if (song.gp[GP_MODE] == MODE_MONO && s > 0) {
            snprintf(b, n, "= 1");
            return;
        }
        engine_degree_name(x, nm, sizeof nm);
        snprintf(b, n, "%s", nm);
        return;
    }
    case SP_FINE: snprintf(b, n, "%+dc", x); return;
    case SP_RATE: snprintf(b, n, "%s", rate_names[x]); return;
    case SP_HARM: {
        static const char *const hn[5] = {"OFF", "2ND", "3RD", "4TH", "5TH"};
        static const char *const rn[5] = {"1ST", "2ND", "3RD", "4TH", "5TH"};
        snprintf(b, n, "%s", song.gp[GP_ENGINE] == ENG_WIND ? rn[x] : hn[x]);
        return;
    }
    case SP_PCHANCE:
    case SP_BCHANCE:
    case SP_GATE:
        snprintf(b, n, x == 0 && p == SP_GATE ? "OFF" : "%d%%", x);
        return;
    case SP_PAN:
        if (x == 0)
            snprintf(b, n, "C");
        else
            snprintf(b, n, "%c%d", x < 0 ? 'L' : 'R', abs(x));
        return;
    case SP_PON:
    case SP_BON: snprintf(b, n, "%s", x ? "ON" : "OFF"); return;
    case SP_FOLD:
    case SP_STIFF:
    case SP_POS:
    case SP_BUZZ:
        if (x == 0)
            snprintf(b, n, "OFF");
        else
            snprintf(b, n, "%d", x);
        return;
    default: snprintf(b, n, "%d", x); return;
    }
}

void gparam_format(int p, char *b, int n)
{
    int x = song.gp[p];
    switch (p) {
    case GP_MODE: snprintf(b, n, "%s", x == MODE_MONO ? "MONO" : "CHORD"); return;
    case GP_SCALE: snprintf(b, n, "%s", scales[x].name); return;
    case GP_ROOT: note_name(x, b, n); return;
    case GP_CHANGES: snprintf(b, n, "%s", change_names[x]); return;
    case GP_GLIDE: snprintf(b, n, "%s", glide_names[x]); return;
    case GP_SYNC: snprintf(b, n, "%s", sync_names[x]); return;
    case GP_CHANCE: snprintf(b, n, "%d%%", x); return;
    case GP_ENGINE: snprintf(b, n, "%s", engine_names[x]); return;
    case GP_EXCITE: snprintf(b, n, "%s", excite_names[x]); return;
    case GP_MATERIAL: snprintf(b, n, "%s", material_names[x]); return;
    case GP_SUB: snprintf(b, n, x ? "%d" : "OFF", x); return;
    case GP_SUBDIV: {
        static const char *const dn[NSUBDIV] = {"OCT", "12TH", "2 OCT", "OCT+12"};
        snprintf(b, n, "%s", dn[x]);
        return;
    }
    case GP_RELATE: {
        static const char *const rn[3] = {"FREE", "1 TONE", "2 TONES"};
        snprintf(b, n, "%s", rn[x]);
        return;
    }
    default: snprintf(b, n, "%d", x); return;
    }
}

void lparam_format(int l, int p, char *b, int n)
{
    int x = song.lfo[l][p];
    switch (p) {
    case LP_RATE: snprintf(b, n, "%s", lfo_rate_names[x]); return;
    case LP_SHAPE: snprintf(b, n, "%s", shape_names[x]); return;
    case LP_TARGET: snprintf(b, n, "%s", target_names[x]); return;
    default: snprintf(b, n, "%d", x); return;
    }
}

void fparam_format(int p, char *b, int n)
{
    int x = song.fx[p];
    if (p == FX_DTIME)
        snprintf(b, n, "%s", dly_names[x]);
    else if (p == FX_BINBEAT) {
        /* the brainwave band the beat falls in */
        float hz = x * 0.5f;
        if (x == 0)
            snprintf(b, n, "OFF");
        else
            snprintf(b, n, "%.1f %s", hz, hz < 4.f ? "DELTA" : hz < 8.f ? "THETA" : "ALPHA");
    }
    else if (p == FX_BINLOCK)
        snprintf(b, n, "%s", x ? "TEMPO" : "FREE");
    else if (p == FX_BTYPE)
        snprintf(b, n, "%s", body_names[x]);
    else if (p == FX_STUNE)
        snprintf(b, n, "%s", stune_names[x]);
    else if (p == FX_SOCT)
        snprintf(b, n, "%+d", x);
    else if (p == FX_SCOUNT && x == 0)
        snprintf(b, n, "OFF");
    else if (p == FX_DFB || p == FX_DMIX || p == FX_VSEND)
        snprintf(b, n, "%d%%", x);
    else if ((p == FX_SYMP || p == FX_BODY) && x == 0)
        snprintf(b, n, "OFF");
    else
        snprintf(b, n, "%d", x);
}

/* ------------------------------------------------------------------ dsp */

static float sr;
static uint32_t rng = 0x9e3779b9u;
static inline uint32_t xr(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static inline float frand(void) { return (xr() >> 8) * (1.f / 16777216.f); }
static inline float fnoise(void) { return frand() * 2.f - 1.f; }

static inline float softclip(float x)
{
    if (x > 3.f)
        return 1.f;
    if (x < -3.f)
        return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

#define DL_MAX 8192
typedef struct {
    float buf[DL_MAX];
    int w;
    float lp;     /* loop damping state */
    float env;    /* loop amplitude follower, for the bow's energy balance */
    float bow_lp; /* bow noise colour */
    float gain;   /* on/off fade */
    float drift, drift_target, drift_t;
    float vis_ph;
    int burst, burst_len; /* pluck: samples of burst left, and its length */
    float burst_amp, burst_lp;
    float ap[2][2]; /* STIFF: two dispersive allpasses in the loop */
    float fold_x1;  /* FOLD: last input, for the anti-aliased folder */
    /* WIND: the jet delay, a DC blocker, the lip resonator, breath and tongue */
    float jet[2048];
    int jw, chiff_n;
    float dcx, dcy, lx1, lx2, lz1, lz2;
    float tg, chiff, pr, vph, gr_lp, nlp, frph, fr_amp;
    /* the player's ear: the sounding period from zero crossings of a
     * lowpassed copy, and the bore correction that pulls it onto the note */
    float tr_a, tr_b, tr_prev, tr_count, tr_period, corr;
    float midi_cur; /* GLIDE: the pitch slides here toward its target */
    int midi_set;
    float last_y;   /* last loop output, for the bridge */
    int bow_open;   /* bow ring: open on its pulses */
    float bow_env;  /* the bow swell following bow_open */
    int hand;       /* bowed by hand */
    int over;       /* ... and overblown, or touched at a harmonic */
    /* undertones: crossings of a lowpassed copy pulse resonators below */
    float sb_a, sb_b, sb_prev, sb_r[2][2];
    int sb_n;
    float f_sound; /* the voice's pitch in Hz now */
    /* per sub-block targets, ramped */
    float D, D_cur, lvl, lvl_cur;
} String;

static String str[NS];
static double beat_phase, sync_rate = 1.0;
static uint32_t beat;
static int64_t last16 = -1;  /* the last 16th the rhythms acted on */
static float flash[NS][NRING];
/* sympathetic coupling: every string hears the others through the bridge */
static float bridge, bridge_x1, bridge_hp;
/* body: five resonant modes per channel */
#define NMODE 5
static float body_ic[2][NMODE][2];
/* sympathetic strings: plain loops, never plucked or bowed, fed only by
 * the bridge; they ring when the played strings hit their pitches */
#define SB 2048
typedef struct {
    float buf[SB];
    int w, set;
    float lp, D, D_cur, midi, peak;
} Symp;
static Symp sy[NSYMP];
static int symp_deg[NSYMP];
static float symp_fine[NSYMP]; /* cents: doubled strings shimmer against each other */
static float symp_pk[NSYMP];
static float bpm_f = 72.f;
static float master_g, play_g;
/* output leveller: up to LEV_MAX of makeup gain, backing off so peaks sit
 * at LEV_TARGET before MASTER */
#define LEV_MAX 16.f
#define LEV_TARGET 0.7f
static float lev_env, lev_g = 1.f;
/* look-ahead: the leveller sees the sound 3 ms before it is heard, so it
 * can turn down smoothly ahead of a pluck instead of flattening its front */
#define LA_MAX 256
static float la_buf[2][LA_MAX];
static int la_w;
static float f_ic1[2], f_ic2[2];
static float lfo_rand[NLFO];
static int lfo_cycle[NLFO];
/* tempo delay: up to 4 s per channel */
#define DLY_MAX (1 << 18)
static float dly[2][DLY_MAX];
static uint32_t dly_w;
static float dly_time_cur, dly_lp[2];
/* reverb: four damped combs + two allpasses per side */
/* reverb: an eight-line feedback delay network with a Householder mix,
 * damping in every line, two lines slowly modulated so the tail never rings,
 * and two allpasses in front to thicken the onset */
#define FDN 8
#define FDN_MAX 4096
static const int fdn_len48[FDN] = {1123, 1361, 1597, 1801, 2063, 2311, 2621, 2903};
static float fdn_buf[FDN][FDN_MAX], fdn_lp[FDN];
static int fdn_w;
static float pre_buf[2][1024];
static int pre_i[2];
static float fdn_mod;
#define VERB_IN 0.22f
/* binaural: a Hilbert pair (two allpass chains 90 degrees apart) per channel
 * and a slowly turning phasor shift the spectrum by a few hertz */
static const float hil_a[4] = {0.47940086558884f, 0.87621849353931f, 0.976597589508199f, 0.997499255935549f};
static const float hil_b[4] = {0.161758498367701f, 0.733028932341480f, 0.945349700329112f, 0.990599156684529f};
static float hil_s[2][2][4][4]; /* channel, chain, section, {x1, x2, y1, y2} */
static float hil_d[2];          /* the second chain's one-sample delay */
static float bin_c = 1.f, bin_s;

static float midi_hz(float m) { return 440.f * exp2f((m - 69.f) / 12.f); }

/* a pluck is a short burst of noise into the loop, about one period long
 * (at most 6 ms), shaped by a raised cosine so it has no hard edges */
#define CHIFF_MS 0.03f
static void string_pluck(String *s, float amp)
{
    if (song.gp[GP_ENGINE] == ENG_WIND) {
        /* a tongued note: a push of breath that falls back, with a chiff */
        s->tg = amp;
        s->chiff = amp;
        s->chiff_n = (int)(CHIFF_MS * sr);
        return;
    }
    int len = s->D_cur > 8.f ? (int)s->D_cur : (int)(0.004f * sr);
    if (len > (int)(0.006f * sr))
        len = (int)(0.006f * sr);
    s->burst = s->burst_len = len;
    s->burst_amp = amp;
}

void engine_pluck(int s)
{
    if (s >= 0 && s < NS && song.on[s])
        string_pluck(&str[s], 0.9f);
}

void engine_bow(int s, int on)
{
    if (s >= 0 && s < NS)
        str[s].hand = on;
}

void engine_overblow(int s, int on)
{
    if (s >= 0 && s < NS)
        str[s].over = on;
}

void engine_set_playing(int on) { live.playing = on; }

float engine_bpm(void) { return bpm_f; }
void engine_set_bpm(float b)
{
    bpm_f = b < 40.f ? 40.f : b > 250.f ? 250.f : b;
    song.gp[GP_BPM] = (int)(bpm_f + 0.5f);
}

double engine_position(void) { return ((double)beat + beat_phase) * 4.0; }

void engine_set_position(double steps)
{
    if (steps < 0.0)
        steps = 0.0;
    beat = (uint32_t)(steps / 4.0);
    beat_phase = steps / 4.0 - beat;
    live.beat = beat;
    last16 = (int64_t)beat * 4 + (int)(beat_phase * 4.0); /* no double hit after a jump */
}

/* a new 16th: advance the rings, pluck, open bows, maybe change chord */
static void on_step(int64_t k)
{
    for (int s = 0; s < NS; s++) {
        const int *P = song.sp[s];
        String *st = &str[s];
        int div = rate_div16[P[SP_RATE]];
        if (k % div)
            continue;
        int64_t idx = k / div;
        for (int r = 0; r < NRING; r++) {
            int steps = P[RING_P(r, 1)], pulses = P[RING_P(r, 2)], rot = P[RING_P(r, 3)];
            int i = (int)(idx % steps), hit = euclid_hit(steps, pulses, rot, i);
            live.step[s][r] = i;
            if (!P[RING_P(r, 0)]) {
                if (r == RING_BOW)
                    st->bow_open = 1;
                continue;
            }
            /* CHANCE: each pulse plays only so often */
            if (hit && frand() * 100.f >= P[r == RING_BOW ? SP_BCHANCE : SP_PCHANCE])
                hit = 0;
            if (r == RING_BOW)
                st->bow_open = hit;
            if (!hit)
                continue;
            flash[s][r] = 1.f;
            if (r == RING_PLUCK && song.on[s]) {
                /* the first pulse of the cycle a little stronger */
                int first = ((i - rot) % steps + steps) % steps == 0;
                string_pluck(st, first ? 0.85f : 0.55f + 0.2f * frand());
            }
        }
    }
    int bars = change_bars[song.gp[GP_CHANGES]];
    if (bars && k > 0 && k % (16 * bars) == 0 && frand() * 100.f < song.gp[GP_CHANCE])
        engine_next_chord();
}

void engine_set_rate(double r) { sync_rate = r < 0.9 ? 0.9 : r > 1.1 ? 1.1 : r; }

int euclid_hit(int steps, int pulses, int rot, int step)
{
    if (steps <= 0 || pulses <= 0)
        return 0;
    if (pulses >= steps)
        return 1;
    int j = ((step - rot) % steps + steps) % steps;
    return (j * pulses) % steps < pulses;
}

/* a new rhythm for one ring: few steps, few pulses, like a tanpura's */
void engine_euclid_random(int s, int ring)
{
    static const int st[6] = {3, 4, 5, 5, 6, 8};
    int *P = song.sp[s];
    int steps = st[xr() % 6];
    P[RING_P(ring, 1)] = steps;
    P[RING_P(ring, 2)] = ring == RING_BOW ? 1 + (int)(xr() % (unsigned)(steps - 1)) : 1 + (xr() % 4 == 0);
    P[RING_P(ring, 3)] = (int)(xr() % (unsigned)steps);
    P[RING_P(ring, 0)] = 1;
}

/* ------------------------------------------------------- progressions */

static int ch_root, ch_prev = -1, ch_away, ch_kind; /* ST_CHORD state */
static char chord_nm[48] = "";
#define NHIST 6
static char chord_hist[NHIST][48];
const char *engine_chord_name(void) { return chord_nm; }
const char *engine_chord_history(int i) { return i == 0 ? chord_nm : i < NHIST ? chord_hist[i] : ""; }
static void name_standing(void);

/* each string keeps to its own register (cents from the root) */
static const float reg_center[NS] = {-1200.f, 0.f, 700.f, 1200.f, 1900.f};
#define REG_HALF 800.f

static int clamp_deg(int d) { return d < -24 ? -24 : d > 24 ? 24 : d; }

static int nearest_degree(const Scale *sc, float cents)
{
    int best = 0;
    float bd = 1e9f;
    for (int d = -24; d <= 24; d++) {
        float e = fabsf(deg_cents(sc, d) - cents);
        if (e < bd)
            bd = e, best = d;
    }
    return best;
}

void engine_set_scale(int sc)
{
    if (sc < 0 || sc >= NSCALE)
        return;
    const Scale *from = cur_scale(), *to = &scales[sc];
    for (int s = 0; s < NS; s++)
        song.sp[s][SP_NOTE] = nearest_degree(to, deg_cents(from, song.sp[s][SP_NOTE]));
    song.gp[GP_SCALE] = sc;
    ch_root = ch_away = 0;
    ch_prev = -1;
    name_standing();
}

static int pick_weighted(const int *w, int n)
{
    int tot = 0;
    for (int i = 0; i < n; i++)
        tot += w[i];
    int r = (int)(xr() % (unsigned)tot);
    for (int i = 0; i < n; i++)
        if ((r -= w[i]) < 0)
            return i;
    return 0;
}

/* the scale degree (0..n-1) of harmony degree h */
static int harm_pc(const Scale *sc, int h)
{
    int m = sc->nh ? sc->nh : sc->n;
    h = fmod_i(h, m);
    return sc->nh ? sc->harm[h] : h;
}

/* the degree nearest a target pitch among those whose degree class is in pcs */
static int voice(const Scale *sc, const int *pcs, int npc, const int *used, float target, float center, float rnd)
{
    int best = 0;
    float bc = 1e9f;
    for (int d = -24; d <= 24; d++) {
        float c = deg_cents(sc, d);
        if (fabsf(c - center) > REG_HALF)
            continue;
        int pc = fmod_i(d, sc->n), k;
        for (k = 0; k < npc && pcs[k] != pc; k++)
            ;
        if (k == npc)
            continue;
        float cost = fabsf(c - target) + (used ? 450.f * used[k] : 0.f) + rnd * frand();
        if (cost < bc)
            bc = cost, best = d;
    }
    return best;
}

static void name_add(const char *t)
{
    size_t l = strlen(chord_nm);
    snprintf(chord_nm + l, sizeof chord_nm - l, "%s%s", l ? " " : "", t);
}

static void deg_name_noct(const Scale *sc, int pc, char *b, int n)
{
    if (sc->deg[0]) {
        snprintf(b, n, "%s", sc->deg[pc]);
        return;
    }
    int mi = song.gp[GP_ROOT] + (int)floorf(sc->cents[pc] / 100.f + 0.5f);
    snprintf(b, n, "%s", note_names[mi % 12]);
}

/* how many notes of the triad on harmony degree c are sounding now */
static int shared_tones(const Scale *sc, int c)
{
    int t[3] = {harm_pc(sc, c), harm_pc(sc, c + 2), harm_pc(sc, c + 4)}, n = 0;
    for (int k = 0; k < 3; k++)
        for (int s = 0; s < NS; s++)
            if (song.on[s] && fmod_i(song.sp[s][SP_NOTE], sc->n) == t[k]) {
                n++;
                break;
            }
    return n;
}

static void next_chord_harmony(const Scale *sc)
{
    int m = sc->nh ? sc->nh : sc->n;
    /* root motion: by fourth and fifth most, then by third and step; the
     * longer it has been away from home, the likelier it returns */
    /* MOTION: calm, common-tone moves (low) .. bold ones (high); HOME
     * scales the pull back to the tonic */
    float mo = song.gp[GP_MOTION] / 100.f, home = song.gp[GP_HOME] / 50.f;
    static const int off7[7] = {3, 4, 1, -1, 2, -2, 5}, off5[4] = {1, -1, 2, -2};
    int w7[7], w5[4];
    for (int i = 0; i < 7; i++) {
        /* fourths and fifths always; thirds (two common tones) when calm;
         * steps and the far move when bold */
        float w = i < 2 ? 3.f : i < 4 ? 0.5f + 3.f * mo : i < 6 ? 3.5f - 2.f * mo : 2.f * mo;
        w7[i] = 1 + (int)(w * 4.f);
    }
    for (int i = 0; i < 4; i++)
        w5[i] = 1 + (int)(4.f * (i < 2 ? 1.6f - mo : 0.4f + mo));
    /* RELATE: the next chord keeps at least this many of the notes now
     * sounding, so every change grows out of the one before */
    int need = song.gp[GP_RELATE];
    int next = ch_root, best = -1, best_c = -1;
    if (ch_root != 0 && ch_away >= 2 && frand() < 0.15f * ch_away * home && shared_tones(sc, 0) >= need)
        next = 0;
    else {
        for (int tries = 0; tries < 24; tries++) {
            int c = m >= 7 ? fmod_i(ch_root + off7[pick_weighted(w7, 7)], m)
                           : fmod_i(ch_root + off5[pick_weighted(w5, 4)], m);
            if (c == ch_root || (c == ch_prev && tries < 12))
                continue;
            int sh = shared_tones(sc, c);
            if (sh > best_c)
                best_c = sh, best = c;
            if (sh >= need) {
                best = c;
                break;
            }
        }
        next = best >= 0 ? best : fmod_i(ch_root + 3, m);
    }
    ch_prev = ch_root;
    ch_root = next;
    ch_away = ch_root ? ch_away + 1 : 0;
    float r = frand();
    float co = song.gp[GP_COLOR] / 100.f; /* COLOR 45: about 1 in 7 sus, 1 in 3 sevenths */
    ch_kind = m < 6 ? 0 : r < 0.33f * co ? 2 : r < 1.1f * co ? 1 : 0; /* triad, seventh, sus */

    int tones[4], nt = 3;
    tones[0] = harm_pc(sc, ch_root);
    if (ch_kind == 2) {
        tones[1] = harm_pc(sc, ch_root + 3);
        tones[2] = harm_pc(sc, ch_root + 4);
    }
    else {
        tones[1] = harm_pc(sc, ch_root + 2);
        tones[2] = harm_pc(sc, ch_root + 4);
        if (ch_kind == 1)
            tones[nt++] = harm_pc(sc, ch_root + 6);
    }

    /* bass: the root, or (sometimes) hold the old bass as a pedal or an
     * inversion when it belongs to the new chord */
    int old_bass = fmod_i(song.sp[0][SP_NOTE], sc->n), keep = 0;
    for (int k = 0; k < nt; k++)
        keep |= tones[k] == old_bass;
    if (!(keep && frand() < 0.25f))
        song.sp[0][SP_NOTE] = voice(sc, tones, 1, NULL, reg_center[0], reg_center[0], 0.f);
    /* upper strings: each to the nearest chord tone, spreading over the
     * chord so every tone sounds */
    int used[4] = {0};
    for (int k = 0; k < nt; k++)
        used[k] = tones[k] == fmod_i(song.sp[0][SP_NOTE], sc->n);
    for (int s = 1; s < NS; s++) {
        float cur = deg_cents(sc, song.sp[s][SP_NOTE]);
        int d = voice(sc, tones, nt, used, cur, reg_center[s], 80.f);
        song.sp[s][SP_NOTE] = d;
        int pc = fmod_i(d, sc->n);
        for (int k = 0; k < nt; k++)
            if (tones[k] == pc)
                used[k]++;
    }
    for (int s = 0; s < NS; s++)
        song.sp[s][SP_FINE] = (int)(fnoise() * (sc->n == 12 ? 8.f : 2.f));

    /* name: a roman numeral in seven-tone scales, else the root's name */
    char t[12];
    chord_nm[0] = 0;
    if (m == 7) {
        static const char *const up[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
        static const char *const lo[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
        float third = fmodf(sc->cents[tones[1]] - sc->cents[tones[0]] + 1200.f, 1200.f);
        snprintf(t, sizeof t, "%s%s", ch_kind == 2 || third > 350.f ? up[ch_root] : lo[ch_root],
                 ch_kind == 1 ? "7" : ch_kind == 2 ? "sus" : "");
    }
    else {
        deg_name_noct(sc, tones[0], t, sizeof t);
        if (ch_kind == 1)
            strcat(t, "7");
    }
    name_add(t);
}

static void next_chord_raga(const Scale *sc)
{
    /* tanpura: Sa low, and Pa (Ma if there is no Pa) below the middle Sa */
    int pa = -1, ma = -1;
    for (int i = 0; i < sc->n; i++) {
        if (fabsf(sc->cents[i] - 701.96f) < 20.f)
            pa = i;
        if (fabsf(sc->cents[i] - 498.04f) < 20.f)
            ma = i;
    }
    int second = pa >= 0 ? pa : ma >= 0 ? ma : 0;
    song.sp[0][SP_NOTE] = -sc->n;
    song.sp[1][SP_NOTE] = second - sc->n;
    song.sp[0][SP_FINE] = song.sp[1][SP_FINE] = 0;
    /* the upper strings wander a step or two; at least one moves, and no two
     * land on the same degree */
    /* MOTION: how far a voice may step (1 .. 3 degrees); HOME: the chance a
     * moving voice settles on Sa or the tanpura's second note instead */
    int reach = 1 + (int)(song.gp[GP_MOTION] / 40.f);
    float home = song.gp[GP_HOME] / 100.f * 0.4f;
    int moved = 0, hold = song.gp[GP_RELATE], held_mask = 0; /* RELATE: voices that stay */
    while (hold-- > 0)
        held_mask |= 1 << (2 + xr() % (NS - 2));
    for (int s = 2; s < NS; s++) {
        int cur = song.sp[s][SP_NOTE], d = cur;
        if ((held_mask & (1 << s)) && s != NS - 1)
            continue;
        for (int tries = 0; tries < 12; tries++) {
            int st = (int)(xr() % (unsigned)(2 * reach + 1)) - reach;
            d = clamp_deg(cur + st);
            if (frand() < home) {
                int o = fdiv(cur, sc->n), pc = frand() < 0.5f ? 0 : second;
                d = clamp_deg(o * sc->n + pc + (abs(o * sc->n + pc - cur) > abs(o * sc->n + pc + sc->n - cur) ? sc->n : 0));
            }
            if (s == NS - 1 && !moved && d == cur)
                continue;
            if (fabsf(deg_cents(sc, d) - reg_center[s]) > REG_HALF)
                continue;
            int dup = 0;
            for (int o = 2; o < s; o++)
                dup |= song.sp[o][SP_NOTE] == d;
            if (!dup)
                break;
        }
        if (fabsf(deg_cents(sc, d) - reg_center[s]) > REG_HALF)
            d = nearest_degree(sc, reg_center[s]);
        moved |= d != cur;
        song.sp[s][SP_NOTE] = d;
        song.sp[s][SP_FINE] = (int)(fnoise() * 2.f);
    }
    char t[12];
    chord_nm[0] = 0;
    for (int s = 2; s < NS; s++) {
        engine_degree_name(song.sp[s][SP_NOTE], t, sizeof t);
        name_add(t);
    }
}

static void next_chord_gamelan(const Scale *sc)
{
    /* the block moves in parallel; kempyung is three keys up in five tones */
    static const int mv[5] = {1, -1, 2, -2, 3};
    int r = fmod_i(song.sp[1][SP_NOTE], sc->n), k = sc->n == 5 ? 3 : 4, nr = r;
    int span = 2 + (int)(song.gp[GP_MOTION] / 34.f); /* MOTION: steps only .. leaps */
    if (span > 5)
        span = 5;
    if (r != 0 && frand() < song.gp[GP_HOME] / 100.f * 0.35f)
        nr = 0; /* HOME: back to the first tone */
    if (song.gp[GP_RELATE] && nr == r) /* RELATE: move by kempyung so one tone stays */
        nr = fmod_i(r + (frand() < 0.5f ? k : -k), sc->n);
    while (nr == r)
        nr = fmod_i(r + mv[xr() % (unsigned)span], sc->n);
    int om = 2 + song.gp[GP_COLOR] / 12; /* COLOR: how widely the pair beats */
    song.sp[0][SP_NOTE] = nr - sc->n; /* gong */
    song.sp[1][SP_NOTE] = nr;
    song.sp[2][SP_NOTE] = nr + k;
    song.sp[3][SP_NOTE] = nr; /* the pair, tuned apart so they beat (ombak) */
    song.sp[4][SP_NOTE] = nr + k + sc->n;
    song.sp[0][SP_FINE] = 0;
    song.sp[1][SP_FINE] = -om;
    song.sp[3][SP_FINE] = om;
    song.sp[2][SP_FINE] = -2;
    song.sp[4][SP_FINE] = 2;
    char t[12];
    chord_nm[0] = 0;
    deg_name_noct(sc, nr, t, sizeof t);
    name_add(t);
    deg_name_noct(sc, fmod_i(nr + k, sc->n), t, sizeof t);
    name_add(t);
}

/* a name for the chord as it stands, before any change */
static void name_standing(void)
{
    const Scale *sc = cur_scale();
    char t[12];
    chord_nm[0] = 0;
    memset(chord_hist, 0, sizeof chord_hist);
    if (sc->style == ST_CHORD) {
        if ((sc->nh ? sc->nh : sc->n) == 7)
            snprintf(t, sizeof t, "%s", sc->n == 12 ? "i" : "I");
        else
            deg_name_noct(sc, fmod_i(song.sp[0][SP_NOTE], sc->n), t, sizeof t);
        name_add(t);
        return;
    }
    for (int s = sc->style == ST_RAGA ? 2 : 1; s < (sc->style == ST_RAGA ? NS : 3); s++) {
        if (sc->style == ST_RAGA)
            engine_degree_name(song.sp[s][SP_NOTE], t, sizeof t);
        else
            deg_name_noct(sc, fmod_i(song.sp[s][SP_NOTE], sc->n), t, sizeof t);
        name_add(t);
    }
}

/* the next chord. The strings are not plucked: they keep ringing and slide
 * (or jump) to the new notes, see GLIDE. */
/* EVOLVE: a chord change may also nudge one string's rhythm: rotate a ring,
 * add or take away a pulse or a step, or (rarely) start or stop a bow ring */
static void evolve(void)
{
    float ev = song.gp[GP_EVOLVE] / 100.f;
    if (frand() >= ev)
        return;
    int s = (int)(xr() % NS), r = (int)(xr() % NRING);
    int *P = song.sp[s];
    if (!P[RING_P(r, 0)] && r == RING_BOW && frand() < 0.3f * ev) {
        P[SP_BON] = 1;
        return;
    }
    if (!P[RING_P(r, 0)])
        r = RING_PLUCK;
    int steps = P[RING_P(r, 1)], dir = frand() < 0.5f ? -1 : 1;
    float k = frand();
    if (k < 0.5f)
        P[RING_P(r, 3)] = fmod_i(P[RING_P(r, 3)] + dir, steps);
    else if (k < 0.85f)
        P[RING_P(r, 2)] = P[RING_P(r, 2)] + dir < 1 ? 1 : P[RING_P(r, 2)] + dir > steps ? steps : P[RING_P(r, 2)] + dir;
    else {
        int ns = steps + dir < 2 ? 2 : steps + dir > 8 ? 8 : steps + dir;
        P[RING_P(r, 1)] = ns;
        if (P[RING_P(r, 2)] > ns)
            P[RING_P(r, 2)] = ns;
        P[RING_P(r, 3)] %= ns;
    }
    if (r == RING_BOW && P[SP_BON] && frand() < 0.15f * ev)
        P[SP_BON] = 0; /* the bow holds again */
}

void engine_next_chord(void)
{
    const Scale *sc = cur_scale();
    char prev[48];
    memcpy(prev, chord_nm, sizeof prev);
    switch (sc->style) {
    case ST_RAGA: next_chord_raga(sc); break;
    case ST_GAMELAN: next_chord_gamelan(sc); break;
    default: next_chord_harmony(sc); break;
    }
    for (int i = NHIST - 1; i > 1; i--)
        memcpy(chord_hist[i], chord_hist[i - 1], sizeof chord_hist[i]);
    memcpy(chord_hist[1], prev, sizeof prev);
    evolve();
    live.chords++;
}

/* where the sympathetic strings sit, as degrees from the root, rising */
static void symp_tune(const Scale *sc, int count)
{
    int base = song.fx[FX_SOCT] * sc->n, m = sc->nh ? sc->nh : sc->n;
    int mono = song.gp[GP_MODE] == MODE_MONO;
    /* the chord as it sounds: the strings' degree classes, bass first */
    int pcs[NS * 2], np = 0;
    for (int s = 0; s < NS; s++) {
        if (!song.on[s])
            continue;
        int pc = fmod_i(mono ? song.sp[0][SP_NOTE] : song.sp[s][SP_NOTE], sc->n), k;
        for (k = 0; k < np && pcs[k] != pc; k++)
            ;
        if (k == np)
            pcs[np++] = pc;
    }
    if (!np)
        pcs[np++] = 0;
    int chord = np;
    if (song.fx[FX_STUNE] == STUNE_CHORD9) /* add the scale tone above each chord tone */
        for (int k = 0; k < chord; k++) {
            int up = fmod_i(pcs[k] + (sc->n == 12 ? 2 : 1), sc->n), j;
            for (j = 0; j < np && pcs[j] != up; j++)
                ;
            if (j == np)
                pcs[np++] = up;
        }
    /* sort the classes upward from the bass */
    int b0 = pcs[0];
    for (int i = 1; i < np; i++)
        for (int j = i; j > 0 && fmod_i(pcs[j] - b0, sc->n) < fmod_i(pcs[j - 1] - b0, sc->n); j--) {
            int t = pcs[j];
            pcs[j] = pcs[j - 1];
            pcs[j - 1] = t;
        }
    int fifth = 0;
    for (int i = 0; i < sc->n; i++)
        if (fabsf(sc->cents[i] - 701.96f) < fabsf(sc->cents[fifth] - 701.96f))
            fifth = i;
    /* each tuning repeats within two octaves; a second pass over the same
     * pitches is detuned a few cents, as on a sitar's taraf */
    for (int k = 0; k < count; k++) {
        int d, per, layer;
        switch (song.fx[FX_STUNE]) {
        case STUNE_SCALE: per = m; break;
        case STUNE_DRONE: per = 2; break;
        case STUNE_FIFTHS: per = 7; break;
        default: per = np; break;
        }
        layer = k / per;
        symp_fine[k] = layer >= 2 ? ((layer & 1) ? 5.f : -5.f) * (layer / 2) : 0.f;
        int o = layer % 2, i = k % per;
        switch (song.fx[FX_STUNE]) {
        case STUNE_SCALE: d = base + o * sc->n + harm_pc(sc, i); break;
        case STUNE_DRONE: d = base + o * sc->n + (i ? fifth : 0); break;
        case STUNE_FIFTHS: {
            float c = fmodf(701.96f * i, 1200.f) + 1200.f * o;
            d = base + nearest_degree(sc, c);
            break;
        }
        default: { /* CHORD, CHORD+: the chord's classes, rising */
            int pc = pcs[i];
            d = base + o * sc->n + fmod_i(pc - b0, sc->n) + b0;
            if (b0 >= sc->n / 2)
                d -= sc->n;
            break;
        }
        }
        symp_deg[k] = clamp_deg(d);
    }
}

int engine_symp_degree(int k) { return k >= 0 && k < NSYMP ? symp_deg[k] : 0; }
float engine_symp_meter(int k) { return k >= 0 && k < NSYMP ? symp_pk[k] : 0.f; }
int engine_scale_n(void) { return cur_scale()->n; }
float engine_scale_cents(int i) { return cur_scale()->cents[i]; }
void engine_pc_name(int pc, char *b, int n) { deg_name_noct(cur_scale(), pc, b, n); }

static void reverb_tick(float in, float *outL, float *outR, const float *g, float damp, float mod)
{
    /* two allpasses in series, then into every line with alternating signs */
    static const int pre_len[2] = {142, 379};
    float x = in;
    for (int k = 0; k < 2; k++) {
        float *b = pre_buf[k];
        int i = pre_i[k];
        float d = b[i], v = x + 0.6f * d;
        b[i] = v;
        x = d - 0.6f * v;
        pre_i[k] = i + 1 < pre_len[k] ? i + 1 : 0;
    }
    float o[FDN], sum = 0.f;
    float scale = sr / 48000.f;
    for (int l = 0; l < FDN; l++) {
        float len = fdn_len48[l] * scale;
        if (l == 0)
            len += mod;
        else if (l == 5)
            len -= mod;
        float rp = (float)fdn_w - len;
        while (rp < 0.f)
            rp += FDN_MAX;
        int ri = (int)rp, rj = (ri + 1) & (FDN_MAX - 1);
        float y = fdn_buf[l][ri] + (fdn_buf[l][rj] - fdn_buf[l][ri]) * (rp - ri);
        fdn_lp[l] += (y - fdn_lp[l]) * damp;
        o[l] = fdn_lp[l] * g[l];
        sum += o[l];
    }
    sum *= 2.f / FDN; /* Householder: o - 2/N * sum */
    for (int l = 0; l < FDN; l++)
        fdn_buf[l][fdn_w] = o[l] - sum + ((l & 1) ? -x : x);
    fdn_w = (fdn_w + 1) & (FDN_MAX - 1);
    *outL = (o[0] - o[2] + o[4] - o[6]) * 0.5f;
    *outR = (o[1] - o[3] + o[5] - o[7]) * 0.5f;
}

/* one Hilbert chain step: y = a (x + y2) - x2 per section */
static inline float hil_chain(float x, const float *a, float (*st)[4])
{
    for (int k = 0; k < 4; k++) {
        float y = a[k] * (x + st[k][3]) - st[k][1];
        st[k][1] = st[k][0];
        st[k][0] = x;
        st[k][3] = st[k][2];
        st[k][2] = y;
        x = y;
    }
    return x;
}

/* FOLD: the triangle folder and its antiderivative, for anti-aliasing */
static inline float fold_u(float x)
{
    float u = fmodf(x + 1.f, 4.f);
    return u < 0.f ? u + 4.f : u;
}
static inline float fold_f(float x)
{
    float u = fold_u(x);
    return u < 2.f ? u - 1.f : 3.f - u;
}
static inline float fold_F(float x)
{
    float u = fold_u(x);
    return u < 2.f ? 0.5f * u * u - u : 3.f * (u - 2.f) - 0.5f * (u * u - 4.f);
}

/* LFO value 0..1 from the beat position */
static float lfo_value(int l, double pos16)
{
    const int *P = song.lfo[l];
    double cyc = pos16 / lfo_div16[P[LP_RATE]];
    int n = (int)cyc;
    float ph = (float)(cyc - n);
    if (n != lfo_cycle[l]) {
        lfo_cycle[l] = n;
        lfo_rand[l] = frand();
    }
    switch (P[LP_SHAPE]) {
    case SH_SINE: return 0.5f - 0.5f * cosf(ph * 6.2831853f);
    case SH_TRI: return ph < 0.5f ? ph * 2.f : 2.f - ph * 2.f;
    case SH_SAW: return 1.f - ph;
    case SH_SQUARE: return ph < 0.5f ? 1.f : 0.f;
    default: return lfo_rand[l];
    }
}

/* ------------------------------------------------------------------ wind
 *
 * A wind voice is a bore: one delay line closed by a loss filter (the open
 * end and the walls), driven at the mouthpiece by an exciter, after the
 * Cook / Smith waveguide instruments:
 *   LIP       a lip resonator near the note, squared into a pressure valve;
 *             the bore is two periods long, so the lips pick an upper mode
 *   REED      a single reed: a pressure-controlled valve on a cylindrical
 *             bore half a period long (odd harmonics, like a clarinet)
 *   DOUBLE    a stiffer double reed on a full-length bore (all harmonics)
 *   FLUTE     an air jet: breath through a jet delay into a cubic edge
 *   FREE      a free reed sounding at its own pitch into a short pipe
 *   RECORDER  a duct-guided jet: steady, sweet, a chiff on each attack
 *   BANSURI   a transverse bamboo flute: a longer, breathier jet
 * The material sets the losses, a little wall dispersion, and the body. */
/* found by sweeping each against pitch and stability (jd_test checks them):
 * the jets' share of the loss filter's delay, the lips' opening gain and
 * rest, the flute and bansuri jet lengths, the jets' peak breath and noise */
static const float jet_lpfac = 0.4f, lip_gain = 10.f, lip_rest = 0.3f, jet_ratio_f = 0.30f, jet_ratio_b = 0.34f,
                   jet_loop = 1.f, jet_pmax = 0.85f, jet_air = 0.5f;
typedef struct {
    float lp_scale, rho, disp;
    float f[NMODE], q[NMODE];
    int root_rel; /* body modes as ratios of the root */
} Material;
static const Material materials[NMATERIAL] = {
    {0.75f, 0.990f, 0.00f, {420, 900, 1800, 3200, 5200}, {6, 7, 8, 8, 6}, 0},          /* wood: warm, soft walls */
    {1.00f, 0.997f, 0.12f, {2.0f, 5.40f, 8.93f, 13.3f, 18.6f}, {40, 40, 50, 50, 60}, 1}, /* metal: bright, ringing */
    {0.90f, 0.994f, 0.05f, {330, 760, 1350, 2400, 3900}, {14, 12, 10, 9, 8}, 0},       /* ceramic: an earthen hollow */
    {1.10f, 0.998f, 0.28f, {1900, 3300, 5100, 7400, 9800}, {70, 70, 80, 80, 90}, 0},   /* glass: pure, sparkling */
};

typedef struct {
    int ex, reg;
    float target_period, tr_k; /* the ear: the period wanted, its lowpass */
    float base, air, lpk, rho, disp, emb, vib, growl;
    float pmax, att_k, tg_dec, bow_k, hand_k, out_g;
    float roff, rslope;                  /* reeds */
    float lb0, la1, la2, lgain;          /* lips */
    float jetD, jet_refl, end_refl;      /* jets */
    float fr_inc, fr_k, fr_th;           /* free reed */
    float vib_inc;
} WindVoice;

static float rd_hermite(const float *buf, int w, float D)
{
    float rp = w - D;
    if (rp < 0.f)
        rp += DL_MAX;
    int ri = (int)rp;
    float fr = rp - ri;
    int r0 = ri ? ri - 1 : DL_MAX - 1, r2 = ri + 1 < DL_MAX ? ri + 1 : 0, r3 = r2 + 1 < DL_MAX ? r2 + 1 : 0;
    float y0 = buf[r0], y1 = buf[ri], y2 = buf[r2], y3 = buf[r3];
    float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * fr + c2) * fr + c1) * fr + y1;
}

/* one sample of a wind voice: y is the bore (for meters and the bridge),
 * o the sound */
static void wind_tick(String *st, const WindVoice *v, float inj, float *py, float *po)
{
    /* breath: steady blowing (BREATH, opened by the breath ring or the
     * hand), plus tongued notes that push and fall back */
    st->bow_env += ((float)(st->bow_open | st->hand) - st->bow_env) * (st->hand ? v->hand_k : v->bow_k);
    float target = (st->hand ? fmaxf(v->base, 0.8f) : v->base) * st->bow_env + st->tg;
    st->tg *= v->tg_dec;
    st->pr += (target - st->pr) * v->att_k;
    st->vph += v->vib_inc;
    if (st->vph > 6.2831853f)
        st->vph -= 6.2831853f;
    float p = st->pr * (1.f + v->vib * 0.07f * sinf(st->vph));
    st->gr_lp += (fnoise() - st->gr_lp) * 0.004f; /* growl: a slow rasp */
    p *= 1.f + v->growl * 4.f * st->gr_lp;
    float bp = v->pmax * (1.f - expf(-2.5f * fmaxf(p, 0.f)));
    st->nlp += (fnoise() - st->nlp) * 0.55f;
    bp *= 1.f + st->nlp * v->air;
    float ch = 0.f;
    if (st->chiff_n > 0) {
        ch = fnoise() * st->chiff * st->chiff_n / (CHIFF_MS * sr);
        st->chiff_n--;
    }

    float y = rd_hermite(st->buf, st->w, st->D_cur), x = y;
    if (v->disp > 0.f)
        for (int k = 0; k < 2; k++) {
            float *a = st->ap[k];
            float yk = v->disp * x + a[0] - v->disp * a[1];
            a[0] = x;
            a[1] = yk;
            x = yk;
        }
    st->lp += (x - st->lp) * v->lpk;
    float b = st->lp * v->rho, out, o;
    switch (v->ex) {
    case EX_REED:
    case EX_DOUBLE: {
        /* the reed opens and closes with the pressure across it */
        float pd = (v->ex == EX_REED ? -0.95f : 0.95f) * b - bp;
        float t = v->roff + v->rslope * pd;
        t = t > 1.f ? 1.f : t < -1.f ? -1.f : t;
        out = bp + pd * t + ch * 0.15f;
        o = y * v->out_g;
        break;
    }
    case EX_LIP: {
        /* the lips ring near the note; their opening lets breath through */
        /* the lips: a resonance near the note driven by the pressure across
         * them (no response to steady pressure), resting slightly open */
        float mp = bp, refl = 0.85f * b, dp = mp - refl;
        float l = v->lb0 * (dp - st->lx2) + v->la1 * st->lz1 - v->la2 * st->lz2;
        st->lx2 = st->lx1;
        st->lx1 = dp;
        st->lz2 = st->lz1;
        st->lz1 = l;
        float area = lip_rest + v->lgain * l;
        area = area < 0.f ? 0.f : area > 1.f ? 1.f : area;
        area *= area;
        out = area * mp + (1.f - area) * refl + ch * 0.05f;
        o = y * v->out_g;
        break;
    }
    case EX_FREE: {
        /* the reed swings at its own pitch; puffs of air sound the pipe */
        st->fr_amp += (bp - st->fr_amp) * v->fr_k;
        st->frph += v->fr_inc;
        if (st->frph >= 1.f)
            st->frph -= 1.f;
        float r = sinf(6.2831853f * st->frph) + v->fr_th;
        float flow = st->fr_amp * (r > 0.f ? r * r : 0.f) - st->fr_amp * 0.4f;
        out = flow * 0.3f + b * 0.6f;
        o = (y + flow * 0.6f) * v->out_g;
        break;
    }
    default: {
        /* the jet: breath crosses a gap and splits on the edge */
        float breath = bp + ch;
        float pd = breath - v->jet_refl * b;
        st->jet[st->jw] = pd;
        float rp = st->jw - v->jetD;
        if (rp < 0.f)
            rp += 2048.f;
        int ri = (int)rp, rj = (ri + 1) & 2047;
        pd = st->jet[ri] + (st->jet[rj] - st->jet[ri]) * (rp - ri);
        st->jw = (st->jw + 1) & 2047;
        pd = pd * (pd * pd - 1.f);
        pd = pd > 1.f ? 1.f : pd < -1.f ? -1.f : pd;
        out = pd + v->end_refl * b;
        o = out * v->out_g;
        break;
    }
    }
    if (v->ex != EX_REED && v->ex != EX_DOUBLE) { /* keep the loop free of DC */
        float d = out - st->dcx + 0.995f * st->dcy;
        st->dcx = out;
        st->dcy = d;
        out = d;
    }
    st->buf[st->w] = softclip(out + inj);
    st->w = st->w + 1 < DL_MAX ? st->w + 1 : 0;
    st->env = fmaxf(fabsf(y), st->env * 0.9997f);
    /* the ear: when the voice is sounding, compare its period with the
     * note's and nudge the bore (slowly, within 20%) */
    if (v->target_period > 0.f) {
        st->tr_a += (y - st->tr_a) * v->tr_k;
        st->tr_b += (st->tr_a - st->tr_b) * v->tr_k;
        float z = st->tr_b;
        st->tr_count += 1.f;
        if (st->tr_prev < 0.f && z >= 0.f) {
            float frac = st->tr_prev / (st->tr_prev - z), per = st->tr_count - 1.f + frac;
            st->tr_count = 1.f - frac;
            if (st->env > 0.02f && per > 0.6f * v->target_period && per < 1.6f * v->target_period) {
                st->tr_period = st->tr_period > 0.f ? st->tr_period + (per - st->tr_period) * 0.2f : per;
                st->corr *= 1.f - 0.02f * (st->tr_period / v->target_period - 1.f);
                st->corr = st->corr < 0.8f ? 0.8f : st->corr > 1.25f ? 1.25f : st->corr;
            }
        }
        st->tr_prev = z;
    }
    *py = y;
    *po = o;
}

/* the wind voice's settings for this sub-block (pitch dependent) and its
 * bore length */
static void wind_setup(WindVoice *v, String *st, const int *P, float hz, float sps16, float hand_k)
{
    const Material *m = &materials[song.gp[GP_MATERIAL]];
    int ex = song.gp[GP_EXCITE];
    v->ex = ex;
    v->reg = P[SP_HARM] + 1;
    if (st->over) /* overblown by hand: a reed like a clarinet's jumps a twelfth, the rest an octave */
        v->reg *= ex == EX_REED ? 3 : 2;
    v->base = P[SP_BOW] / 100.f;
    float d = P[SP_DECAY] / 100.f;
    v->air = 0.015f + 0.2f * d * d * d * d; /* breath is never perfectly still */
    float br = P[SP_BRIGHT] / 100.f;
    v->lpk = fminf(0.95f, (0.25f + 0.7f * powf(br, 1.2f)) * m->lp_scale);
    v->rho = m->rho;
    v->disp = m->disp;
    v->emb = P[SP_STIFF] / 100.f;
    v->vib = P[SP_POS] / 100.f;
    v->growl = P[SP_BUZZ] / 100.f;
    v->bow_k = 1.f - expf(-1.f / (0.5f * sps16 * rate_div16[P[SP_RATE]]));
    v->hand_k = hand_k;
    v->tg_dec = expf(-1.f / (0.3f * sr));
    v->vib_inc = 6.2831853f * (4.6f + 0.3f * (float)(st - str)) / sr;
    static const float pmax[NEXCITE] = {1.0f, 0.95f, 0.8f, 1.25f, 1.0f, 1.1f, 1.2f};
    static const float att[NEXCITE] = {0.015f, 0.02f, 0.02f, 0.012f, 0.04f, 0.01f, 0.015f};
    static const float outg[NEXCITE] = {1.0f, 1.0f, 1.0f, 0.3f, 0.8f, 0.3f, 0.3f};
    v->pmax = pmax[ex];
    if (ex == EX_FLUTE || ex == EX_RECORDER || ex == EX_BANSURI) {
        /* a gentle jet locks to the fundamental; some turbulence is always
         * there to start it */
        v->pmax = jet_pmax;
        v->air = 0.01f + v->air * jet_air;
    }
    v->att_k = 1.f - expf(-1.f / (att[ex] * sr));
    v->out_g = outg[ex];
    if (ex == EX_BANSURI)
        v->air += 0.06f; /* bamboo breathes */
    if (ex == EX_RECORDER)
        v->air *= 0.4f;

    float f = hz, loop;
    switch (ex) {
    case EX_LIP:
        loop = (0.6f + 2.2f / f) * sr / f; /* the lips pull the pitch down: a shorter bore */
        {
            float lf = f * v->reg * (1.f + 0.05f * v->emb), r = 0.997f, w = 6.2831853f * lf / sr;
            v->lb0 = 0.5f - 0.5f * r * r;
            v->la1 = 2.f * r * cosf(w);
            v->la2 = r * r;
            v->lgain = lip_gain * (0.7f + 0.6f * v->emb);
        }
        break;
    case EX_REED:
        loop = 0.5f * sr / (f * v->reg); /* half a period: odd harmonics */
        v->roff = 0.7f;
        v->rslope = -0.3f + 0.2f * v->emb;
        break;
    case EX_DOUBLE:
        loop = sr / (f * v->reg);
        v->roff = 0.6f;
        v->rslope = -0.6f + 0.35f * v->emb;
        break;
    case EX_FREE:
        loop = sr / (f * v->reg);
        v->fr_inc = f * v->reg / sr;
        v->fr_k = 1.f - expf(-1.f / (0.04f * sr));
        v->fr_th = -0.2f + 0.6f * v->emb;
        break;
    default: { /* FLUTE, RECORDER, BANSURI: an octave up (flutes are high), on a
                * bore two periods long whose second mode the jet locks to */
        f *= 2.f;
        loop = jet_loop * sr / (f * v->reg); /* REGISTER: a shorter bore, as by cross-fingering */
        float ratio = ex == EX_RECORDER ? 0.26f : ex == EX_BANSURI ? jet_ratio_b : jet_ratio_f;
        ratio *= 1.f - 0.35f * v->emb;
        v->jetD = fminf(fmaxf(loop * ratio, 2.f), 2040.f);
        v->jet_refl = ex == EX_BANSURI ? 0.55f : ex == EX_RECORDER ? 0.45f : 0.5f;
        v->end_refl = ex == EX_BANSURI ? 0.45f : 0.5f;
        break;
    }
    }
    /* the note that should sound, for the ear (the free reed keeps its own) */
    float sound = ex == EX_LIP ? f * v->reg : 0.f; /* only the lips need lipping into tune */
    if (ex == EX_LIP)
        loop /= v->reg;
    v->target_period = sound > 0.f ? sr / sound : 0.f;
    st->f_sound = (ex >= EX_FLUTE && ex != EX_FREE ? f : f) * v->reg;
    /* the loss filter's and the wall allpasses' delay at the note itself,
     * so high notes stay in tune */
    float fs = ex >= EX_FLUTE && ex != EX_FREE ? f * v->reg : ex == EX_LIP ? f * v->reg : f * v->reg;
    float w = 6.2831853f * fminf(fs, 0.45f * sr) / sr, c1 = 1.f - v->lpk;
    float lp_delay = atan2f(c1 * sinf(w), 1.f - c1 * cosf(w)) / w, ap_delay = 0.f;
    if (ex >= EX_FLUTE && ex != EX_FREE)
        lp_delay *= jet_lpfac;
    if (v->disp > 0.f) {
        float a = v->disp, ph = atan2f(-sinf(w), a + cosf(w)) - atan2f(-a * sinf(w), 1.f + a * cosf(w));
        ap_delay = -2.f * ph / w;
    }
    v->tr_k = 1.f - expf(-6.2831853f * 1.4f * fmaxf(sound, 20.f) / sr);
    if (st->corr <= 0.f)
        st->corr = 1.f;
    st->D = fmaxf(3.f, fminf(loop * st->corr - lp_delay - ap_delay, DL_MAX - 4.f));
}

#define SUB 64

/* tiny numbers in decaying tails are flushed to zero: on ARM VFP and x86
 * SSE alike they would otherwise cost many times a normal multiply */
static void flush_denormals(void)
{
#if defined(__arm__) && defined(__ARM_PCS_VFP)
    unsigned int fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= 1u << 24; /* FZ */
    __asm__ volatile("vmsr fpscr, %0" : : "r"(fpscr));
#elif defined(__SSE__)
    unsigned int csr;
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    csr |= 0x8040; /* FTZ | DAZ */
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
#endif
}

void engine_render(int16_t *out, int frames)
{
    flush_denormals();
    float drift_amt = song.gp[GP_DRIFT] / 100.f;
    float verb_send = song.fx[FX_VSEND] / 100.f;
    /* MASTER is a plain fader up to 80; above that it drives up to +14 dB
     * into the soft clipper, trading clean peaks for loudness */
    float m = song.gp[GP_MASTER] / 100.f;
    float master_t = m <= 0.8f ? m * m * 1.3f : 0.832f * exp2f((m - 0.8f) * 12.f);
    float play_t = live.playing ? 1.f : 0.f;
    float g_ramp = 1.f / (0.02f * sr);
    /* the gain comes down within the look-ahead (about 3 time constants)
     * and climbs back slowly */
    int la_n = (int)(0.003f * sr) < LA_MAX - 1 ? (int)(0.003f * sr) : LA_MAX - 1;
    float lev_rel = expf(-1.f / (0.6f * sr)), lev_att = 1.f - expf(-3.f / la_n),
          lev_slow = 1.f - expf(-1.f / (0.05f * sr));
    float meter[NS] = {0}, mm = 0.f;
    int mono = song.gp[GP_MODE] == MODE_MONO;
    float sps16 = sr * 15.f / bpm_f; /* samples per 16th */
    /* GLIDE OFF still slides over ~10 ms, so a jump never clicks */
    float glide_samps = fmaxf(sps16 * glide_div16[song.gp[GP_GLIDE]], 0.01f * sr);
    float dly_target = fminf(sps16 * dly_div16[song.fx[FX_DTIME]], DLY_MAX - 4.f);
    float dly_fb = song.fx[FX_DFB] / 100.f, dly_mix_p = song.fx[FX_DMIX] / 100.f;
    if (dly_time_cur <= 0.f)
        dly_time_cur = dly_target;
    /* COUPLE: how much of the bridge each string hears */
    float couple = song.fx[FX_SYMP] / 100.f;
    couple = couple * couple * 0.012f;
    /* BODY: five resonant modes. WOOD and GOURD are fixed boxes, BRONZE rings
     * inharmonically and HARMONIC on the overtones, both tuned to the root */
    float body_amt = song.fx[FX_BODY] / 100.f, bm_a1[NMODE], bm_a2[NMODE], bm_a3[NMODE], bm_g[NMODE];
    if (body_amt > 0.f) {
        static const float wood_f[NMODE] = {105, 210, 330, 480, 1100}, wood_q[NMODE] = {12, 10, 10, 8, 6};
        static const float gourd_f[NMODE] = {140, 290, 520, 960, 2400}, gourd_q[NMODE] = {8, 9, 10, 8, 5};
        static const float bronze_r[NMODE] = {1.f, 2.76f, 5.40f, 8.93f, 13.34f};
        static const float gain[NMODE] = {1.f, 0.8f, 0.65f, 0.5f, 0.35f};
        float root_hz = midi_hz((float)song.gp[GP_ROOT]);
        while (root_hz < 90.f)
            root_hz *= 2.f;
        int windb = song.gp[GP_ENGINE] == ENG_WIND;
        const Material *mt = &materials[song.gp[GP_MATERIAL]];
        for (int i = 0; i < NMODE; i++) {
            float f, q;
            if (windb) { /* in WIND the body is the instrument's material */
                f = mt->root_rel ? root_hz * mt->f[i] : mt->f[i];
                q = mt->q[i];
            }
            else
            switch (song.fx[FX_BTYPE]) {
            case BODY_WOOD: f = wood_f[i], q = wood_q[i]; break;
            case BODY_GOURD: f = gourd_f[i], q = gourd_q[i]; break;
            case BODY_BRONZE: f = root_hz * 2.f * bronze_r[i], q = 70.f; break;
            default: f = root_hz * (i + 1), q = 40.f; break;
            }
            f = fminf(f, 0.45f * sr);
            float g = tanf(3.14159265f * f / sr), k = 1.f / q;
            bm_a1[i] = 1.f / (1.f + g * (g + k));
            bm_a2[i] = g * bm_a1[i];
            bm_a3[i] = g * bm_a2[i];
            bm_g[i] = gain[i] * k * 2.2f; /* band output scaled to unity peak, then a lift */
        }
    }

    /* static per-string settings for this block */
    float lp_k[NS], rho[NS], bow_amp[NS], bow_col[NS], base_midi[NS], fold_g[NS], stiff_a[NS], pos_f[NS];
    float buzz_th[NS], buzz_k[NS], bow_k[NS], pk_col[NS], pk_gain[NS];
    const Scale *sc = cur_scale();
    float hand_k = 1.f - expf(-1.f / (0.15f * sr));
    int wind = song.gp[GP_ENGINE] == ENG_WIND;
    /* undertones: which divisions, how long they ring, how loud */
    static const int sub_divs[NSUBDIV][2] = {{2, 0}, {3, 0}, {4, 0}, {2, 3}};
    const int *sdiv = sub_divs[song.gp[GP_SUBDIV]];
    float sub_g = song.gp[GP_SUB] / 100.f * 1.4f;
    float sub_r = expf(-1.f / ((0.06f + 1.5f * song.gp[GP_SUBRES] / 100.f) * sr));
    float sb_a1[NS][2], sb_a2 = sub_r * sub_r, sb_amp[NS][2], sb_k[NS];
    WindVoice wv[NS];
    if (wind)
        couple *= 0.3f; /* air couples more gently than a bridge */
    /* binaural: the beat in hertz; LOCK puts it on the nearest power-of-two
     * multiple of the beat rate, so it pulses with the rhythm */
    float bin_hz = song.fx[FX_BINBEAT] * 0.5f, bin_mix = song.fx[FX_BINDEPTH] / 100.f;
    if (bin_hz > 0.f && song.fx[FX_BINLOCK]) {
        float b = bpm_f / 60.f;
        while (b * 1.41421f < bin_hz)
            b *= 2.f;
        while (b > bin_hz * 1.41421f)
            b *= 0.5f;
        bin_hz = b;
    }
    live.bin_hz = bin_hz;
    float bin_dc = cosf(3.14159265f * bin_hz / sr), bin_ds = sinf(3.14159265f * bin_hz / sr); /* half the beat per ear */
    /* reverb: TIME sets the decay (0.5 .. 10 s), TONE the damping */
    float t60 = 0.5f * powf(20.f, song.fx[FX_VTIME] / 100.f), fdn_g[FDN];
    for (int l = 0; l < FDN; l++)
        fdn_g[l] = powf(10.f, -3.f * fdn_len48[l] * (sr / 48000.f) / (t60 * sr));
    float fdn_damp = 0.15f + 0.8f * song.fx[FX_VTONE] / 100.f;
    float mod_inc = 6.2831853f * 0.13f / sr;
    float harm_n[NS], gate_d[NS];
    for (int s = 0; s < NS; s++) {
        harm_n[s] = song.sp[s][SP_HARM] ? (float)(song.sp[s][SP_HARM] + 1) : 0.f;
        if (str[s].over) /* touched by hand: the next harmonic up */
            harm_n[s] = harm_n[s] > 0.f ? fminf(harm_n[s] + 1.f, 6.f) : 2.f;
        gate_d[s] = song.sp[s][SP_GATE] / 100.f;
    }
    /* sympathetic strings: tune them, and set their loop */
    int nsy = song.fx[FX_SCOUNT];
    float sy_rho = 0.97f + 0.0295f * sqrtf(song.fx[FX_SRING] / 100.f);
    float sy_gain = nsy ? song.fx[FX_SLEVEL] / 100.f * 2.5f / sqrtf((float)nsy) : 0.f;
    float sy_drive = 0.02f;
    if (nsy)
        symp_tune(sc, nsy);
    for (int s = 0; s < NS; s++) {
        const int *P = song.sp[s];
        String *st = &str[s];
        st->drift_t -= frames;
        if (st->drift_t <= 0.f) {
            st->drift_t = (1.f + 2.f * frand()) * sr;
            st->drift_target = fnoise();
        }
        st->drift += (st->drift_target - st->drift) * 0.002f * frames / 64.f;
        float cents = P[SP_FINE] + st->drift * 25.f * drift_amt;
        int note = mono ? song.sp[0][SP_NOTE] : P[SP_NOTE];
        base_midi[s] = song.gp[GP_ROOT] + (deg_cents(sc, note) + cents) / 100.f;
        lp_k[s] = 0.08f + 0.9f * powf(P[SP_BRIGHT] / 100.f, 1.5f);
        float d = P[SP_DECAY] / 100.f;
        rho[s] = 0.85f + 0.14995f * powf(d, 0.35f);
        bow_amp[s] = P[SP_BOW] / 100.f * 0.02f;
        bow_col[s] = 0.05f + 0.5f * P[SP_BRIGHT] / 100.f;
        pk_col[s] = 0.12f + 0.6f * P[SP_BRIGHT] / 100.f;
        pk_gain[s] = 1.4f / sqrtf(pk_col[s] / (2.f - pk_col[s])); /* make up the noise lowpass's loss */
        /* BUZZ: a jawari-like bridge; past a threshold the string's swing
         * is squashed, so a strong note buzzes and the buzz blooms and
         * fades as it decays */
        float bz = P[SP_BUZZ] / 100.f;
        buzz_th[s] = 0.4f * (1.f - bz) * (1.f - bz) + 0.01f;
        buzz_k[s] = bz > 0.f ? 40.f * bz : 0.f;
        /* bow ring: the bow swells in and out over about half a step */
        if (!P[SP_BON])
            st->bow_open = 1;
        bow_k[s] = 1.f - expf(-1.f / (0.5f * sps16 * rate_div16[P[SP_RATE]]));
        /* tone shapers: FOLD drives the output into a wavefolder, STIFF
         * sets the allpass dispersion in the loop (inharmonic, bell-like),
         * POS is the pick-position comb (0 = off, else 1 .. 50% along) */
        fold_g[s] = P[SP_FOLD] / 100.f;
        stiff_a[s] = P[SP_STIFF] / 100.f * 0.7f;
        pos_f[s] = P[SP_POS] ? 0.005f + 0.495f * P[SP_POS] / 100.f : 0.f;
    }

    for (int n0 = 0; n0 < frames; n0 += SUB) {
        int nn = frames - n0 < SUB ? frames - n0 : SUB;
        /* modulation, once per sub-block */
        double pos16 = engine_position();
        float m_cut = 0.f, m_lvl = 1.f, m_pitch = 0.f, m_bow = 1.f, m_dly = 1.f, m_verb = 1.f;
        for (int l = 0; l < NLFO; l++) {
            float v = lfo_value(l, pos16), depth = song.lfo[l][LP_DEPTH] / 100.f;
            live.lfo_val[l] = v;
            switch (song.lfo[l][LP_TARGET]) {
            case LT_CUTOFF: m_cut += depth * 3.f * (v - 0.5f) * 2.f; break; /* octaves */
            case LT_LEVEL: m_lvl *= 1.f - depth * (1.f - v); break;
            case LT_PITCH: m_pitch += depth * (v - 0.5f) * 2.f; break; /* semitones */
            case LT_BOW: m_bow *= 1.f - depth * (1.f - v); break;
            case LT_DELAY: m_dly *= 1.f - depth * (1.f - v); break;
            case LT_VERB: m_verb *= 1.f - depth * (1.f - v); break;
            }
        }
        float cut = 60.f * powf(300.f, song.gp[GP_CUTOFF] / 100.f) * exp2f(m_cut);
        float fg = tanf(3.14159265f * fminf(fmaxf(cut, 30.f), 0.45f * sr) / sr), fk = 1.2f;
        float fa1 = 1.f / (1.f + fg * (fg + fk)), fa2 = fg * fa1, fa3 = fg * fa2;
        float dly_mix = dly_mix_p * m_dly, vsend = verb_send * m_verb;
        for (int s = 0; s < NS; s++) {
            String *st = &str[s];
            /* portamento: a lag toward the target pitch that settles in
             * about the GLIDE time (4 time constants) */
            if (!st->midi_set) {
                st->midi_cur = base_midi[s];
                st->midi_set = 1;
            }
            else
                st->midi_cur += (base_midi[s] - st->midi_cur) * (1.f - expf(-4.f * nn / glide_samps));
            float hz = midi_hz(st->midi_cur + m_pitch);
            hz = fmaxf(20.f, fminf(hz, sr / 8.f));
            if (wind) {
                wind_setup(&wv[s], st, song.sp[s], hz, sps16, hand_k);
                wv[s].base *= m_bow;
                if (st->D_cur <= 0.f)
                    st->D_cur = st->D;
                float l = song.sp[s][SP_LEVEL] / 100.f;
                st->lvl = l * l * m_lvl;
                continue;
            }
            st->f_sound = hz;
            float lp_delay = (1.f - lp_k[s]) / lp_k[s];
            if (lp_delay > 6.f)
                lp_delay = 6.f;
            float ap_delay = stiff_a[s] > 0.f ? 2.f * (1.f - stiff_a[s]) / (1.f + stiff_a[s]) : 0.f;
            st->D = fmaxf(3.f, fminf(sr / hz - lp_delay - ap_delay, DL_MAX - 4.f));
            if (st->D_cur <= 0.f)
                st->D_cur = st->D;
            float l = song.sp[s][SP_LEVEL] / 100.f;
            st->lvl = l * l * m_lvl;
        }
        if (sub_g > 0.f)
            for (int s = 0; s < NS; s++) {
                float fv = fmaxf(str[s].f_sound, 20.f);
                sb_k[s] = 1.f - expf(-6.2831853f * 1.3f * fv / sr);
                for (int j = 0; j < 2; j++) {
                    if (!sdiv[j]) {
                        sb_a1[s][j] = sb_amp[s][j] = 0.f;
                        continue;
                    }
                    float w = 6.2831853f * fv / sdiv[j] / sr;
                    sb_a1[s][j] = 2.f * sub_r * cosf(w);
                    /* a pulse per sub period keeps the ring at the voice's level */
                    sb_amp[s][j] = sinf(w) * (1.f - powf(sub_r, sr * sdiv[j] / fv));
                }
            }
        float sy_dD[NSYMP];
        for (int k = 0; k < nsy; k++) {
            Symp *q = &sy[k];
            float target = song.gp[GP_ROOT] + (deg_cents(sc, symp_deg[k]) + symp_fine[k]) / 100.f;
            if (!q->set) {
                q->midi = target;
                q->set = 1;
            }
            else
                q->midi += (target - q->midi) * (1.f - expf(-4.f * nn / glide_samps));
            float hz = fmaxf(30.f, fminf(midi_hz(q->midi), sr / 8.f));
            q->D = fmaxf(2.f, fminf(sr / hz - 1.f, SB - 3.f)); /* the lowpass adds about a sample */
            if (q->D_cur <= 0.f)
                q->D_cur = q->D;
            sy_dD[k] = (q->D - q->D_cur) / nn;
        }
        float dD[NS], dl[NS];
        for (int s = 0; s < NS; s++) {
            dD[s] = (str[s].D - str[s].D_cur) / nn;
            dl[s] = (str[s].lvl - str[s].lvl_cur) / nn;
        }

        for (int n = n0; n < n0 + nn; n++) {
            if (live.playing) {
                beat_phase += bpm_f / 60.f / sr * sync_rate;
                if (beat_phase >= 1.0) {
                    beat_phase -= 1.0;
                    beat++;
                    live.beat = beat;
                }
                int64_t k16 = (int64_t)beat * 4 + (int)(beat_phase * 4.0);
                if (k16 != last16) {
                    last16 = k16;
                    on_step(k16);
                }
            }
            play_g += (play_t - play_g) * g_ramp;

            float L = 0.f, R = 0.f, send = 0.f, br = 0.f;
            for (int s = 0; s < NS; s++) {
                String *st = &str[s];
                float on_t = song.on[s] ? 1.f : 0.f;
                st->gain += (on_t - st->gain) * g_ramp;
                st->D_cur += dD[s];
                st->lvl_cur += dl[s];
                if (st->gain < 1e-4f && !song.on[s])
                    continue;

                float y, o;
                if (wind) {
                    wind_tick(st, &wv[s], couple * (bridge - st->last_y), &y, &o);
                    if (fold_g[s] > 0.f) {
                        float g = 1.f + fold_g[s] * 5.f, xf = o * g, dx = xf - st->fold_x1, f;
                        f = fabsf(dx) > 1e-4f ? (fold_F(xf) - fold_F(st->fold_x1)) / dx : fold_f(0.5f * (xf + st->fold_x1));
                        st->fold_x1 = xf;
                        o = f * (0.8f / sqrtf(g));
                    }
                    st->last_y = y * st->gain;
                    br += st->last_y;
                    goto voice_out;
                }
                /* fractional read D samples back: four-point Hermite, which
                 * keeps the highs that linear interpolation would dull on
                 * every pass, and still glides under pitch modulation */
                float rp = st->w - st->D_cur;
                if (rp < 0.f)
                    rp += DL_MAX;
                int ri = (int)rp;
                float fr = rp - ri;
                int r0 = ri ? ri - 1 : DL_MAX - 1, r2 = ri + 1 < DL_MAX ? ri + 1 : 0, r3 = r2 + 1 < DL_MAX ? r2 + 1 : 0;
                float y0 = st->buf[r0], y1 = st->buf[ri], y2 = st->buf[r2], y3 = st->buf[r3];
                float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
                float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                y = ((c3 * fr + c2) * fr + c1) * fr + y1;
                if (harm_n[s] > 0.f) {
                    /* HARM: a light touch at 1/n of the string. Taps spaced
                     * D/n apart agree only on multiples of the n-th harmonic,
                     * so in the loop the others fade and a flageolet blooms */
                    float acc = y;
                    int nn_ = (int)harm_n[s];
                    for (int j = 1; j < nn_; j++) {
                        float rq = rp + st->D_cur * j / harm_n[s];
                        if (rq >= DL_MAX)
                            rq -= DL_MAX;
                        int qi = (int)rq, qj = qi + 1 < DL_MAX ? qi + 1 : 0;
                        acc += st->buf[qi] + (st->buf[qj] - st->buf[qi]) * (rq - qi);
                    }
                    y = 0.6f * y + 0.4f * acc / harm_n[s];
                }
                o = y;
                if (pos_f[s] > 0.f) {
                    /* comb with a tap further back: the pick-position filter */
                    float rq = rp - st->D_cur * pos_f[s];
                    if (rq < 0.f)
                        rq += DL_MAX;
                    int qi = (int)rq, qj = qi + 1 < DL_MAX ? qi + 1 : 0;
                    o = y - (st->buf[qi] + (st->buf[qj] - st->buf[qi]) * (rq - qi));
                }
                if (fold_g[s] > 0.f) {
                    /* triangle fold back into -1..1, anti-aliased: the
                     * average of the fold over each sample step (from its
                     * antiderivative) instead of a point sample */
                    float g = 1.f + fold_g[s] * 5.f, xf = o * g, dx = xf - st->fold_x1, f;
                    if (fabsf(dx) > 1e-4f)
                        f = (fold_F(xf) - fold_F(st->fold_x1)) / dx;
                    else
                        f = fold_f(0.5f * (xf + st->fold_x1));
                    st->fold_x1 = xf;
                    o = f * (0.8f / sqrtf(g));
                }
                float x = y;
                if (stiff_a[s] > 0.f)
                    for (int k = 0; k < 2; k++) {
                        float *st2 = st->ap[k];
                        float yk = stiff_a[s] * x + st2[0] - stiff_a[s] * st2[1];
                        st2[0] = x;
                        st2[1] = yk;
                        x = yk;
                    }
                if (buzz_k[s] > 0.f && x > buzz_th[s]) {
                    float e = x - buzz_th[s];
                    x = buzz_th[s] + e / (1.f + buzz_k[s] * e);
                }
                st->lp += (x - st->lp) * lp_k[s];
                float fbk = st->lp * rho[s];

                st->env = fmaxf(fabsf(y), st->env * 0.9997f);
                float headroom = fmaxf(0.f, 1.f - st->env * 1.6f);
                st->bow_lp += (fnoise() - st->bow_lp) * bow_col[s];
                /* the hand bow opens the bow and presses harder, even at BOW 0 */
                st->bow_env += ((float)(st->bow_open | st->hand) - st->bow_env) * (st->hand ? hand_k : bow_k[s]);
                /* the hand bow presses hard and keeps the string singing at a
                 * full, steady level for as long as it is held */
                float ex = st->hand ? st->bow_lp * (bow_amp[s] + 0.09f) * fmaxf(0.f, 1.f - st->env * 1.4f) * play_g * st->bow_env
                                    : st->bow_lp * bow_amp[s] * m_bow * headroom * play_g * st->bow_env;
                /* the other strings, heard through the bridge */
                ex += couple * (bridge - st->last_y);
                st->last_y = y * st->gain;
                br += st->last_y;
                if (st->burst > 0) {
                    /* coloured by BRIGHT, windowed, and with less push into
                     * a string that is already loud, so the loop never
                     * clips on a pluck */
                    float ph = 1.f - (float)st->burst / st->burst_len;
                    float win = 0.5f - 0.5f * cosf(6.2831853f * ph);
                    st->burst_lp += (fnoise() - st->burst_lp) * pk_col[s];
                    st->burst--;
                    ex += st->burst_lp * st->burst_amp * win * play_g * pk_gain[s] * (1.f - 0.6f * fminf(1.f, st->env));
                }
                st->buf[st->w] = softclip(fbk + ex);
                st->w = st->w + 1 < DL_MAX ? st->w + 1 : 0;

            voice_out:
                if (sub_g > 0.f) {
                    /* undertones: every n-th upward crossing of the voice (lowpassed
                     * to its fundamental) strikes a resonance n times lower */
                    st->sb_a += (y - st->sb_a) * sb_k[s];
                    st->sb_b += (st->sb_a - st->sb_b) * sb_k[s];
                    int cross = st->sb_prev < 0.f && st->sb_b >= 0.f;
                    st->sb_prev = st->sb_b;
                    if (cross)
                        st->sb_n = (st->sb_n + 1) % 12;
                    float sub = 0.f;
                    for (int j = 0; j < 2; j++) {
                        if (!sdiv[j])
                            continue;
                        float *r = st->sb_r[j];
                        float in = cross && st->sb_n % sdiv[j] == 0 ? sb_amp[s][j] * fminf(st->env, 0.6f) * 1.6f : 0.f;
                        float z = in + sb_a1[s][j] * r[0] - sb_a2 * r[1];
                        r[1] = r[0];
                        r[0] = z;
                        sub += z;
                    }
                    o += sub * sub_g;
                }
                /* GATE: the voice's sound follows its bow (breath) ring */
                o *= st->lvl_cur * st->gain * play_g * (1.f - gate_d[s] + gate_d[s] * st->bow_env);
                float pan = song.sp[s][SP_PAN] / 50.f;
                L += o * (1.f - pan);
                R += o * (1.f + pan);
                send += o;
                float a = fabsf(y);
                if (a > meter[s])
                    meter[s] = a;
                st->vis_ph += 0.6f / st->D_cur;
            }
            /* the bridge, without DC, for the next sample */
            bridge_hp = br - bridge_x1 + 0.995f * bridge_hp;
            bridge_x1 = br;
            bridge = bridge_hp;

            /* the sympathetic strings hear the bridge, spread across the
             * stereo field, and go to the reverb a little more */
            if (nsy) {
                float sl = 0.f, sr_ = 0.f;
                for (int k = 0; k < nsy; k++) {
                    Symp *q = &sy[k];
                    q->D_cur += sy_dD[k];
                    float rp = q->w - q->D_cur;
                    if (rp < 0.f)
                        rp += SB;
                    int ri = (int)rp, rj = ri + 1 < SB ? ri + 1 : 0;
                    float y = q->buf[ri] + (q->buf[rj] - q->buf[ri]) * (rp - ri);
                    q->lp += (y - q->lp) * 0.5f;
                    q->buf[q->w] = softclip(q->lp * sy_rho + bridge * sy_drive);
                    q->w = q->w + 1 < SB ? q->w + 1 : 0;
                    float pan = (k & 1 ? 1.f : -1.f) * (0.25f + 0.6f * (float)k / NSYMP);
                    sl += y * (1.f - pan);
                    sr_ += y * (1.f + pan);
                    float a = fabsf(y);
                    if (a > q->peak)
                        q->peak = a;
                }
                L += sl * sy_gain * play_g;
                R += sr_ * sy_gain * play_g;
                send += (sl + sr_) * 0.8f * sy_gain * play_g;
            }

            /* the body resonates under the strings */
            if (body_amt > 0.f)
                for (int ch = 0; ch < 2; ch++) {
                    float x = ch ? R : L, b = 0.f;
                    for (int i = 0; i < NMODE; i++) {
                        float *ic = body_ic[ch][i];
                        float v3 = x - ic[1];
                        float v1 = bm_a1[i] * ic[0] + bm_a2[i] * v3;
                        float v2 = ic[1] + bm_a2[i] * ic[0] + bm_a3[i] * v3;
                        ic[0] = 2.f * v1 - ic[0];
                        ic[1] = 2.f * v2 - ic[1];
                        b += v1 * bm_g[i];
                    }
                    x = x * (1.f - 0.6f * body_amt) + b * body_amt;
                    if (ch)
                        R = x;
                    else
                        L = x;
                }


            /* binaural: left a little down, right a little up, so every
             * partial of every string beats between the ears */
            if (bin_hz > 0.f) {
                float nc = bin_c * bin_dc - bin_s * bin_ds;
                bin_s = bin_s * bin_dc + bin_c * bin_ds;
                bin_c = nc;
                for (int ch = 0; ch < 2; ch++) {
                    float x = ch ? R : L;
                    float i_ = hil_chain(x, hil_a, hil_s[ch][0]);
                    float q_ = hil_d[ch];
                    hil_d[ch] = hil_chain(x, hil_b, hil_s[ch][1]);
                    float shifted = ch ? i_ * bin_c - q_ * bin_s : i_ * bin_c + q_ * bin_s;
                    x = i_ + (shifted - i_) * bin_mix;
                    if (ch)
                        R = x;
                    else
                        L = x;
                }
            }

            /* master lowpass: 2-pole SVF with a little resonance */
            for (int ch = 0; ch < 2; ch++) {
                float x = ch ? R : L;
                float v3 = x - f_ic2[ch];
                float v1 = fa1 * f_ic1[ch] + fa2 * v3;
                float v2 = f_ic2[ch] + fa2 * f_ic1[ch] + fa3 * v3;
                f_ic1[ch] = 2.f * v1 - f_ic1[ch];
                f_ic2[ch] = 2.f * v2 - f_ic2[ch];
                if (ch)
                    R = v2;
                else
                    L = v2;
            }

            /* tempo delay, time slewed so tempo changes glide */
            dly_time_cur += (dly_target - dly_time_cur) * 0.0005f;
            float rp = (float)dly_w - dly_time_cur;
            if (rp < 0.f)
                rp += DLY_MAX;
            uint32_t ri = (uint32_t)rp;
            float fr = rp - ri;
            float dl0 = dly[0][ri & (DLY_MAX - 1)] + (dly[0][(ri + 1) & (DLY_MAX - 1)] - dly[0][ri & (DLY_MAX - 1)]) * fr;
            float dr0 = dly[1][ri & (DLY_MAX - 1)] + (dly[1][(ri + 1) & (DLY_MAX - 1)] - dly[1][ri & (DLY_MAX - 1)]) * fr;
            dly_lp[0] += (dl0 - dly_lp[0]) * 0.5f;
            dly_lp[1] += (dr0 - dly_lp[1]) * 0.5f;
            dly[0][dly_w & (DLY_MAX - 1)] = softclip(L + dly_lp[1] * dly_fb); /* ping-pong */
            dly[1][dly_w & (DLY_MAX - 1)] = softclip(R + dly_lp[0] * dly_fb);
            dly_w++;
            L += dl0 * dly_mix;
            R += dr0 * dly_mix;

            float wl = 0.f, wr = 0.f;
            fdn_mod += mod_inc;
            if (fdn_mod > 6.2831853f)
                fdn_mod -= 6.2831853f;
            reverb_tick((send + (dl0 + dr0) * 0.5f * dly_mix) * vsend * VERB_IN, &wl, &wr, fdn_g, fdn_damp, 8.f * sinf(fdn_mod));
            L += wl;
            R += wr;

            /* the bowed drone alone sits some 30 dB under the plucks: lift
             * it, and turn the plucks down ahead of time (look-ahead), with a
             * smooth gain so nothing clips or steps */
            float pk = fmaxf(fabsf(L), fabsf(R));
            lev_env = pk > lev_env ? pk : lev_env * lev_rel;
            float lev_t = fminf(LEV_MAX, LEV_TARGET / fmaxf(lev_env, 1e-6f));
            lev_g += (lev_t - lev_g) * (lev_t < lev_g ? lev_att : lev_slow);
            la_buf[0][la_w] = L;
            la_buf[1][la_w] = R;
            int la_r = la_w - la_n;
            if (la_r < 0)
                la_r += LA_MAX;
            la_w = la_w + 1 < LA_MAX ? la_w + 1 : 0;
            L = la_buf[0][la_r] * lev_g;
            R = la_buf[1][la_r] * lev_g;

            master_g += (master_t - master_g) * g_ramp;
            L = softclip(L * master_g);
            R = softclip(R * master_g);
            float a = fmaxf(fabsf(L), fabsf(R));
            if (a > mm)
                mm = a;
            /* to 16 bits with triangular dither, rounded: quiet tails fade
             * into soft noise instead of grainy steps */
            out[2 * n] = (int16_t)lrintf(L * 32000.f + frand() - frand());
            out[2 * n + 1] = (int16_t)lrintf(R * 32000.f + frand() - frand());
        }
        for (int s = 0; s < NS; s++) {
            str[s].D_cur = str[s].D;
            str[s].lvl_cur = str[s].lvl;
        }
    }
    float fl = expf(-frames / (0.3f * sr));
    for (int s = 0; s < NS; s++) {
        live.meter[s] = meter[s];
        live.phase[s] = str[s].vis_ph;
        live.bow_env[s] = str[s].bow_env;
    }
    {
        float sm = 0.f;
        for (int k = 0; k < NSYMP; k++) {
            symp_pk[k] = k < nsy ? sy[k].peak : 0.f;
            sy[k].peak *= 0.5f;
            if (symp_pk[k] > sm)
                sm = symp_pk[k];
        }
        live.symp_meter = sm;
    }
    for (int s = 0; s < NS; s++) {
        for (int r = 0; r < NRING; r++)
            live.flash[s][r] = flash[s][r] *= fl;
    }
    live.master_meter = mm;

}

void engine_init(int sample_rate)
{
    sr = (float)sample_rate;
    memset(str, 0, sizeof str);
    bpm_f = (float)song.gp[GP_BPM];
    for (int s = 0; s < NS; s++)
        str[s].bow_open = 1, str[s].bow_env = 1.f;
    beat = 0;
    beat_phase = 0.0;
    last16 = -1;
    dly_time_cur = 0.f;
    bridge = bridge_x1 = bridge_hp = 0.f;
    lev_env = 0.f;
    lev_g = 1.f;
    memset(la_buf, 0, sizeof la_buf);
    memset(body_ic, 0, sizeof body_ic);
    memset(sy, 0, sizeof sy);
    memset(fdn_buf, 0, sizeof fdn_buf);
    memset(fdn_lp, 0, sizeof fdn_lp);
    memset(pre_buf, 0, sizeof pre_buf);
    memset(hil_s, 0, sizeof hil_s);
    hil_d[0] = hil_d[1] = 0.f;
    bin_c = 1.f, bin_s = 0.f;
}

/* ------------------------------------------------------------ song state */

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

static void fill_default(Song *s)
{
    memset(s, 0, sizeof *s);
    s->magic = SONG_MAGIC;
    for (int i = 0; i < NS; i++) {
        s->on[i] = 1;
        memcpy(s->sp[i], def_sp[i], sizeof s->sp[i]);
    }
    memcpy(s->gp, def_gp, sizeof s->gp);
    memcpy(s->lfo, def_lfo, sizeof s->lfo);
    memcpy(s->fx, def_fx, sizeof s->fx);
}

void song_default(void)
{
    fill_default(&song);
    name_standing();
    bpm_f = (float)song.gp[GP_BPM];
}

/* a v0.0.3 save, or an older one brought up to it: strings in EQUAL
 * semitones, PLUCK every N beats. The pluck becomes a one-pulse ring with
 * the same period and phase; the bow rings stay off, and COUPLE, BODY and
 * CHANGES stay off so the song sounds as it did. */
/* what is new since v0.1 stays out of an older song, so it sounds as it
 * did: no sympathetic strings, no EVOLVE, and the old gentle pan */
/* what is new in v0.3 stays out of an older song: every pulse plays, no
 * harmonic touch or string gate, every scheduled change happens with no
 * common-tone rule, binaural off (the old master gate is gone) */
static void neutral_v03(Song *s)
{
    for (int i = 0; i < NS; i++) {
        s->sp[i][SP_PCHANCE] = s->sp[i][SP_BCHANCE] = 100;
        s->sp[i][SP_HARM] = s->sp[i][SP_GATE] = 0;
    }
    s->gp[GP_CHANCE] = 100, s->gp[GP_RELATE] = 0;
    s->fx[FX_BINBEAT] = 0, s->fx[FX_BINDEPTH] = 100, s->fx[FX_BINLOCK] = 0;
}

static void from_v7(Song *s, const SongV7 *o)
{
    fill_default(s);
    memcpy(s->on, o->on, sizeof s->on);
    for (int i = 0; i < NS; i++)
        memcpy(s->sp[i], o->sp[i], sizeof o->sp[i]);
    memcpy(s->gp, o->gp, sizeof o->gp);
    memcpy(s->lfo, o->lfo, sizeof s->lfo);
    memcpy(s->fx, o->fx, sizeof o->fx);
    neutral_v03(s);
}

static void from_v8(Song *s, const SongV8 *o)
{
    fill_default(s); /* ENGINE: STRINGS */
    memcpy(s->on, o->on, sizeof s->on);
    memcpy(s->sp, o->sp, sizeof s->sp);
    memcpy(s->gp, o->gp, sizeof o->gp);
    memcpy(s->lfo, o->lfo, sizeof s->lfo);
    memcpy(s->fx, o->fx, sizeof s->fx);
}

static void legacy_quiet(Song *s)
{
    neutral_v03(s);
    static const int pan[NS] = {-12, -6, 0, 6, 12};
    for (int i = 0; i < NS; i++)
        s->sp[i][SP_PAN] = pan[i];
    s->gp[GP_MOTION] = 50, s->gp[GP_HOME] = 50, s->gp[GP_COLOR] = 45, s->gp[GP_EVOLVE] = 0;
    s->fx[FX_SCOUNT] = 0;
}

static void from_v6(Song *s, const SongV6 *o)
{
    fill_default(s);
    memcpy(s->on, o->on, sizeof s->on);
    for (int i = 0; i < NS; i++)
        memcpy(s->sp[i], o->sp[i], sizeof o->sp[i]);
    memcpy(s->gp, o->gp, sizeof o->gp);
    memcpy(s->lfo, o->lfo, sizeof s->lfo);
    memcpy(s->fx, o->fx, sizeof o->fx);
    legacy_quiet(s);
}

static void from_v5(Song *s, const SongV5 *o)
{
    static const int pk_steps[6] = {4, 1, 1, 1, 1, 2}, pk_rate[6] = {2, 2, 3, 4, 5, 5};
    fill_default(s);
    for (int i = 0; i < NS; i++) {
        const int *q = o->sp[i];
        int *P = s->sp[i];
        s->on[i] = o->on[i];
        P[SP_NOTE] = q[0], P[SP_FINE] = q[1], P[SP_LEVEL] = q[2], P[SP_BOW] = q[3];
        P[SP_DECAY] = q[5], P[SP_BRIGHT] = q[6], P[SP_FOLD] = q[7], P[SP_STIFF] = q[8], P[SP_POS] = q[9];
        P[SP_BUZZ] = 0;
        int pk = clampi(q[4], 0, 5);
        P[SP_PON] = pk > 0;
        P[SP_PSTEPS] = pk_steps[pk], P[SP_PPULSES] = 1, P[SP_PROT] = 0, P[SP_RATE] = pk_rate[pk];
        P[SP_BON] = 0;
    }
    const int *g = o->gp;
    s->gp[GP_MODE] = g[0], s->gp[GP_SCALE] = 0, s->gp[GP_ROOT] = g[1], s->gp[GP_CHANGES] = 0;
    s->gp[GP_BPM] = g[2], s->gp[GP_DRIFT] = g[3], s->gp[GP_GLIDE] = g[4], s->gp[GP_CUTOFF] = g[5];
    s->gp[GP_MASTER] = g[6], s->gp[GP_SYNC] = g[7];
    memcpy(s->lfo, o->lfo, sizeof s->lfo);
    memcpy(s->fx, o->fx, sizeof o->fx);
    s->fx[FX_SYMP] = s->fx[FX_BODY] = 0;
    s->fx[FX_BTYPE] = BODY_WOOD;
    legacy_quiet(s);
}

/* globals from a save without GLIDE (it gets v0.0.3's default) */
static void gp_from_v4(SongV5 *s, const int *g)
{
    s->gp[0] = g[0], s->gp[1] = g[1], s->gp[2] = g[2], s->gp[3] = g[3];
    s->gp[4] = 3;
    s->gp[5] = g[4], s->gp[6] = g[5], s->gp[7] = g[6];
}

/* a v0.0.2 save: five strings, no GLIDE */
static int load_v4(SongV5 *s, const unsigned char *buf, size_t got)
{
    SongV4 o;
    if (got != sizeof o)
        return 0;
    memcpy(&o, buf, sizeof o);
    memcpy(s->on, o.on, sizeof s->on);
    memcpy(s->sp, o.sp, sizeof s->sp);
    gp_from_v4(s, o.gp);
    memcpy(s->lfo, o.lfo, sizeof s->lfo);
    memcpy(s->fx, o.fx, sizeof s->fx);
    return 1;
}

/* a v0.0.1 save: strings 1-4 as saved, string 5 as v0.0.2's default */
static int load_v3(SongV5 *s, const unsigned char *buf, size_t got)
{
    static const int s5[NSP_V5] = {24, 2, 30, 55, 5, 93, 70, 0, 0, 0};
    SongV3 o;
    if (got != sizeof o)
        return 0;
    memcpy(&o, buf, sizeof o);
    for (int i = 0; i < NS_V3; i++) {
        s->on[i] = o.on[i];
        memcpy(s->sp[i], o.sp[i], sizeof s->sp[i]);
    }
    s->on[4] = 1;
    memcpy(s->sp[4], s5, sizeof s5);
    gp_from_v4(s, o.gp);
    memcpy(s->lfo, o.lfo, sizeof s->lfo);
    memcpy(s->fx, o.fx, sizeof s->fx);
    return 1;
}

int song_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    Song s;
    SongV5 o;
    SongV6 o6;
    SongV7 o7;
    SongV8 o8;
    unsigned char buf[sizeof s + 1];
    size_t got = fread(buf, 1, sizeof buf, f);
    fclose(f);
    uint32_t magic = 0;
    if (got >= sizeof magic)
        memcpy(&magic, buf, sizeof magic);
    if (magic == SONG_MAGIC && got == sizeof s)
        memcpy(&s, buf, sizeof s);
    else if (magic == SONG_MAGIC_V8 && got == sizeof o8) {
        memcpy(&o8, buf, sizeof o8);
        from_v8(&s, &o8);
    }
    else if (magic == SONG_MAGIC_V7 && got == sizeof o7) {
        memcpy(&o7, buf, sizeof o7);
        from_v7(&s, &o7);
    }
    else if (magic == SONG_MAGIC_V6 && got == sizeof o6) {
        memcpy(&o6, buf, sizeof o6);
        from_v6(&s, &o6);
    }
    else {
        if (magic == SONG_MAGIC_V5 && got == sizeof o)
            memcpy(&o, buf, sizeof o);
        else if (!(magic == SONG_MAGIC_V4 && load_v4(&o, buf, got)) && !(magic == SONG_MAGIC_V3 && load_v3(&o, buf, got)))
            return 0;
        from_v5(&s, &o);
    }
    for (int i = 0; i < NS; i++) {
        s.on[i] = !!s.on[i];
        for (int p = 0; p < NSP; p++)
            s.sp[i][p] = clampi(s.sp[i][p], sparams[p].min, sparams[p].max);
    }
    for (int p = 0; p < NGP; p++)
        s.gp[p] = clampi(s.gp[p], gparams[p].min, gparams[p].max);
    for (int l = 0; l < NLFO; l++)
        for (int p = 0; p < NLP; p++)
            s.lfo[l][p] = clampi(s.lfo[l][p], lparams[p].min, lparams[p].max);
    for (int p = 0; p < NFX; p++)
        s.fx[p] = clampi(s.fx[p], fparams[p].min, fparams[p].max);
    song = s;
    bpm_f = (float)song.gp[GP_BPM];
    name_standing();
    return 1;
}

int song_save(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return 0;
    int ok = fwrite(&song, sizeof song, 1, f) == 1;
    ok &= fclose(f) == 0;
    return ok && rename(tmp, path) == 0;
}
