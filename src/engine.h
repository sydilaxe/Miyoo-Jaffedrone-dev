/* Jaffedrone audio engine: five Karplus-Strong strings kept ringing by a
 * noise bow and plucked by a Euclidean rhythm each, tuned to a scale (equal,
 * just, raga, gamelan), coupled through a shared bridge (sympathetic
 * resonance) into a resonant body, with two beat-synced LFOs, a tempo-synced
 * delay, a reverb, and Wi-Fi tempo sync compatible with HI5. Chord changes
 * follow a progression that fits the scale. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define JD_VERSION "0.1.0"

#define NS 5
#define NSP 19 /* params per string */
#define NGP 10 /* global params */
#define NLFO 2
#define NLP 4  /* params per LFO */
#define NFX 12 /* effect params */

/* SP_NOTE is a scale degree above the root (in EQUAL, a semitone). Each
 * string has two Euclidean rings sharing one step length (SP_RATE): the
 * pluck ring plucks on its pulses, the bow ring swells the bow on its pulses
 * (off: the bow plays continuously). Ring fields: on, steps, pulses, rotate. */
enum { SP_NOTE, SP_FINE, SP_LEVEL, SP_BOW, SP_DECAY, SP_BRIGHT, SP_FOLD, SP_STIFF, SP_POS, SP_BUZZ,
       SP_PON, SP_PSTEPS, SP_PPULSES, SP_PROT, SP_BON, SP_BSTEPS, SP_BPULSES, SP_BROT, SP_RATE };
enum { RING_PLUCK, RING_BOW, NRING };
#define RING_P(ring, field) (SP_PON + (ring) * 4 + (field)) /* field 0 on, 1 steps, 2 pulses, 3 rotate */
#define NSOUND SP_PON /* sound params come first */
enum { GP_MODE, GP_SCALE, GP_ROOT, GP_CHANGES, GP_BPM, GP_DRIFT, GP_GLIDE, GP_CUTOFF, GP_MASTER, GP_SYNC };
enum { LP_RATE, LP_SHAPE, LP_TARGET, LP_DEPTH };
enum { FX_DTIME, FX_DFB, FX_DMIX, FX_VSEND, FX_VTIME, FX_VTONE, FX_GRATE, FX_GDUTY, FX_GSMOOTH, FX_SYMP, FX_BODY, FX_BTYPE };
enum { BODY_WOOD, BODY_GOURD, BODY_BRONZE, BODY_HARMONIC, NBODY };
enum { MODE_MONO, MODE_CHORD };
enum { SYNC_OFF, SYNC_AUTO, SYNC_LEAD, SYNC_FOLLOW, NSYNCMODE };
enum { LT_CUTOFF, LT_LEVEL, LT_PITCH, LT_BOW, LT_DELAY, LT_VERB, NLT };
enum { SH_SINE, SH_TRI, SH_SAW, SH_SQUARE, SH_RANDOM, NSH };

typedef struct {
    const char *name;
    int min, max;
} ParamDef;

typedef struct {
    uint32_t magic;
    int on[NS];
    int sp[NS][NSP];
    int gp[NGP];
    int lfo[NLFO][NLP];
    int fx[NFX];
} Song;

/* shared with the UI; the audio thread publishes meters and the beat */
typedef struct {
    int playing;
    volatile float meter[NS];
    volatile float master_meter;
    volatile uint32_t beat;
    volatile float phase[NS];  /* string animation phase */
    volatile float lfo_val[NLFO]; /* 0..1 for the display */
    volatile float gate;          /* current gate gain for the display */
    volatile int step[NS][NRING];   /* current step of each ring */
    volatile float flash[NS][NRING]; /* 1 on a hit, fading (display) */
    volatile float bow_env[NS];     /* how open each bow is now */
    volatile uint32_t chords;     /* counts chord changes (manual and CHANGES) */
} Live;

extern Song song;
extern Live live;

const ParamDef *sparam_def(int p);
const ParamDef *gparam_def(int p);
const ParamDef *lparam_def(int p);
const ParamDef *fparam_def(int p);
void sparam_format(int s, int p, char *b, int n);
void gparam_format(int p, char *b, int n);
void lparam_format(int l, int p, char *b, int n);
void fparam_format(int p, char *b, int n);
void note_name(int midi, char *b, int n);
const char *sync_mode_name(int m);

void engine_init(int sample_rate);
void engine_render(int16_t *out, int frames);
void engine_set_playing(int on);
void engine_pluck(int s);
void engine_bow(int s, int on); /* bow by hand while held, over the rings */
void engine_next_chord(void);  /* the next chord of the progression */
void engine_set_scale(int sc); /* retune: each string moves to the nearest degree */
void engine_euclid_random(int s, int ring); /* a new simple rhythm for one ring */
int euclid_hit(int steps, int pulses, int rot, int step);
int engine_nscales(void);
const char *engine_scale_name(int sc);
const char *engine_chord_name(void); /* the current chord, in the scale's own names */
void engine_degree_name(int degree, char *b, int n); /* e.g. "Pa'" or "E3-14" */

/* transport for wifi sync: position in 16th steps with fraction, a hard
 * jump, a playback speed trim (1.0 = none) and a float tempo */
double engine_position(void);
void engine_set_position(double steps);
void engine_set_rate(double r);
float engine_bpm(void);
void engine_set_bpm(float bpm);

void song_default(void);
int song_save(const char *path);
int song_load(const char *path);
