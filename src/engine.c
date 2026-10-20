#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SONG_MAGIC 0x4a445207u    /* JDR7: sympathetic strings, pan, MOTION/HOME/COLOR/EVOLVE */
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
    {"PAN", -50, 50}};
static const ParamDef gparams[NGP] = {{"VOICING", 0, 1},   {"SCALE", 0, NSCALE - 1}, {"ROOT", 24, 72},
                                      {"CHANGES", 0, 5},   {"BPM", 40, 200},         {"DRIFT", 0, 100},
                                      {"GLIDE", 0, 7},     {"CUTOFF", 0, 100},       {"MASTER", 0, 100},
                                      {"WIFI SYNC", 0, NSYNCMODE - 1}, {"MOTION", 0, 100},
                                      {"HOME", 0, 100},    {"COLOR", 0, 100},        {"EVOLVE", 0, 100}};
static const ParamDef lparams[NLP] = {{"RATE", 0, 6}, {"SHAPE", 0, NSH - 1}, {"TARGET", 0, NLT - 1}, {"DEPTH", 0, 100}};
static const ParamDef fparams[NFX] = {{"TIME", 0, 7},   {"FEEDBK", 0, 95}, {"MIX", 0, 100},
                                      {"SEND", 0, 100}, {"TIME", 0, 100},  {"TONE", 0, 100},
                                      {"RATE", 0, 7},   {"DUTY", 1, 9},    {"SMOOTH", 0, 100},
                                      {"COUPLE", 0, 100}, {"BODY", 0, 100}, {"TYPE", 0, NBODY - 1},
                                      {"SYMP", 0, NSYMP}, {"S TUNE", 0, NSTUNE - 1}, {"S OCT", -1, 2},
                                      {"S LEVEL", 0, 100}, {"S RING", 0, 100}};
static const float gate_div16[8] = {0.f, 8.f, 4.f, 2.f, 1.f, 0.5f, 1.3333f, 0.6667f};
static const char *const gate_names[8] = {"OFF", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T"};
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
 *  pluck on, steps, pulses, rotate, bow ring on, steps, pulses, rotate, rate, pan}
 * Notes are JUST MAJOR degrees: root, fifth, octave, twelfth, two octaves.
 * Each string has its own sparse pluck rhythm; the top string's bow ring
 * breathes in and out, the other bows play continuously. */
static const int def_sp[NS][NSP] = {{0, 0, 80, 35, 85, 45, 0, 0, 0, 0, 1, 4, 1, 0, 0, 4, 2, 0, 2, 0},
                                    {4, -4, 65, 45, 92, 40, 0, 0, 0, 0, 1, 5, 2, 0, 0, 5, 3, 0, 2, -22},
                                    {7, 3, 55, 40, 95, 55, 0, 0, 0, 0, 1, 3, 1, 1, 0, 3, 2, 0, 3, 22},
                                    {11, 0, 40, 50, 90, 65, 0, 0, 0, 0, 1, 7, 2, 3, 0, 7, 3, 0, 2, -12},
                                    {14, 2, 30, 55, 93, 70, 0, 0, 0, 0, 1, 6, 1, 2, 1, 5, 3, 0, 2, 12}};
static const int def_gp[NGP] = {MODE_CHORD, 1, 45, 4, 72, 30, 3, 70, 80, SYNC_OFF, 50, 50, 45, 30};
static const int def_lfo[NLFO][NLP] = {{2, SH_SINE, LT_CUTOFF, 35}, {4, SH_TRI, LT_LEVEL, 0}};
static const int def_fx[NFX] = {5, 45, 25, 35, 60, 55, 0, 5, 40, 30, 35, BODY_WOOD, 7, STUNE_CHORD, 1, 45, 70};

const ParamDef *sparam_def(int p) { return &sparams[p]; }
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
    else if (p == FX_GRATE)
        snprintf(b, n, "%s", gate_names[x]);
    else if (p == FX_GDUTY)
        snprintf(b, n, "%d%%", x * 10);
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
    float midi_cur; /* GLIDE: the pitch slides here toward its target */
    int midi_set;
    float last_y;   /* last loop output, for the bridge */
    int bow_open;   /* bow ring: open on its pulses */
    float bow_env;  /* the bow swell following bow_open */
    int hand;       /* bowed by hand */
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
static float gate_g = 1.f;
/* reverb: four damped combs + two allpasses per side */
#define NCOMB 4
#define COMB_MAX 2048
#define AP_MAX 1024
static const int comb_len44[NCOMB] = {1116, 1188, 1277, 1356};
static const int ap_len44[2] = {556, 441};
static float comb_buf[2][NCOMB][COMB_MAX], comb_lp[2][NCOMB];
static int comb_i[2][NCOMB];
static float ap_buf[2][2][AP_MAX];
static int ap_i[2][2];

static float midi_hz(float m) { return 440.f * exp2f((m - 69.f) / 12.f); }

/* a pluck is a short burst of noise into the loop, about one period long
 * (at most 6 ms), shaped by a raised cosine so it has no hard edges */
static void string_pluck(String *s, float amp)
{
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
    if (bars && k > 0 && k % (16 * bars) == 0)
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
    int next = ch_root;
    if (ch_root != 0 && ch_away >= 2 && frand() < 0.15f * ch_away * home)
        next = 0;
    else
        for (int tries = 0; tries < 8 && (next == ch_root || next == ch_prev); tries++)
            next = m >= 7 ? fmod_i(ch_root + off7[pick_weighted(w7, 7)], m)
                          : fmod_i(ch_root + off5[pick_weighted(w5, 4)], m);
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
    int moved = 0;
    for (int s = 2; s < NS; s++) {
        int cur = song.sp[s][SP_NOTE], d = cur;
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

static void reverb_tick(float in, float *outL, float *outR, float decay, float tone)
{
    float scale = sr / 44100.f;
    float fb = 0.72f + 0.26f * decay, damp = 0.85f - 0.75f * tone;
    for (int ch = 0; ch < 2; ch++) {
        float acc = 0.f, x = in * 0.05f;
        for (int c = 0; c < NCOMB; c++) {
            int len = (int)((comb_len44[c] + ch * 23) * scale);
            if (len > COMB_MAX)
                len = COMB_MAX;
            float *b = comb_buf[ch][c];
            int i = comb_i[ch][c];
            float o = b[i];
            comb_lp[ch][c] = o * (1.f - damp) + comb_lp[ch][c] * damp;
            b[i] = x + comb_lp[ch][c] * fb;
            comb_i[ch][c] = i + 1 < len ? i + 1 : 0;
            acc += o;
        }
        for (int a = 0; a < 2; a++) {
            int len = (int)((ap_len44[a] + ch * 23) * scale);
            if (len > AP_MAX)
                len = AP_MAX;
            float *b = ap_buf[ch][a];
            int i = ap_i[ch][a];
            float bo = b[i];
            float o = -acc + bo;
            b[i] = acc + bo * 0.5f;
            ap_i[ch][a] = i + 1 < len ? i + 1 : 0;
            acc = o;
        }
        if (ch)
            *outR = acc;
        else
            *outL = acc;
    }
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

#define SUB 64

void engine_render(int16_t *out, int frames)
{
    float drift_amt = song.gp[GP_DRIFT] / 100.f;
    float verb_send = song.fx[FX_VSEND] / 100.f, verb_decay = song.fx[FX_VTIME] / 100.f,
          verb_tone = song.fx[FX_VTONE] / 100.f;
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
    /* tempo gate: a square on the beat grid, smoothed into a swell */
    float gate_per = gate_div16[song.fx[FX_GRATE]], gate_duty = song.fx[FX_GDUTY] * 0.1f;
    float gate_k = 1.f - expf(-1.f / ((0.0005f + 0.06f * song.fx[FX_GSMOOTH] / 100.f) * sr));
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
        for (int i = 0; i < NMODE; i++) {
            float f, q;
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
            float lp_delay = (1.f - lp_k[s]) / lp_k[s];
            if (lp_delay > 6.f)
                lp_delay = 6.f;
            float ap_delay = stiff_a[s] > 0.f ? 2.f * (1.f - stiff_a[s]) / (1.f + stiff_a[s]) : 0.f;
            st->D = fmaxf(2.f, fminf(sr / hz - lp_delay - ap_delay, DL_MAX - 3.f));
            if (st->D_cur <= 0.f)
                st->D_cur = st->D;
            float l = song.sp[s][SP_LEVEL] / 100.f;
            st->lvl = l * l * m_lvl;
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

                /* fractional read D samples back (linear interpolation so
                 * pitch modulation glides instead of stepping) */
                float rp = st->w - st->D_cur;
                if (rp < 0.f)
                    rp += DL_MAX;
                int ri = (int)rp;
                float fr = rp - ri;
                int rj = ri + 1 < DL_MAX ? ri + 1 : 0;
                float y = st->buf[ri] + (st->buf[rj] - st->buf[ri]) * fr;
                float o = y;
                if (pos_f[s] > 0.f) {
                    /* comb with a tap further back: the pick-position filter */
                    float rq = rp - st->D_cur * pos_f[s];
                    if (rq < 0.f)
                        rq += DL_MAX;
                    int qi = (int)rq, qj = qi + 1 < DL_MAX ? qi + 1 : 0;
                    o = y - (st->buf[qi] + (st->buf[qj] - st->buf[qi]) * (rq - qi));
                }
                if (fold_g[s] > 0.f) {
                    float g = 1.f + fold_g[s] * 5.f, f = o * g;
                    /* triangle fold back into -1..1 */
                    f = fmodf(f + 1.f, 4.f);
                    if (f < 0.f)
                        f += 4.f;
                    f = f < 2.f ? f - 1.f : 3.f - f;
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
                float ex = st->bow_lp * (bow_amp[s] + st->hand * 0.012f) * m_bow * headroom * play_g * st->bow_env;
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

                o *= st->lvl_cur * st->gain * play_g;
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

            /* rhythmic gate before the effects, so the delay and reverb
             * smear the chops */
            if (gate_per > 0.f) {
                float gp = (float)fmod(engine_position(), (double)gate_per) / gate_per;
                gate_g += ((gp < gate_duty ? 1.f : 0.f) - gate_g) * gate_k;
            }
            else
                gate_g += (1.f - gate_g) * gate_k;
            L *= gate_g;
            R *= gate_g;

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
            reverb_tick((send + (dl0 + dr0) * 0.5f * dly_mix) * vsend, &wl, &wr, verb_decay, verb_tone);
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
            out[2 * n] = (int16_t)(L * 32000.f);
            out[2 * n + 1] = (int16_t)(R * 32000.f);
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
    live.gate = gate_g;
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
static void legacy_quiet(Song *s)
{
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
    unsigned char buf[sizeof s + 1];
    size_t got = fread(buf, 1, sizeof buf, f);
    fclose(f);
    uint32_t magic = 0;
    if (got >= sizeof magic)
        memcpy(&magic, buf, sizeof magic);
    if (magic == SONG_MAGIC && got == sizeof s)
        memcpy(&s, buf, sizeof s);
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
