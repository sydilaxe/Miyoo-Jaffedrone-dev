/* Headless host harness: renders the default drone to a WAV, screenshots
 * every page, checks tuning, gate and wifi sync, and times the DSP.
 *
 * usage: jd_test OUTDIR */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"
#include "gfx.h"
#include "log.h"
#include "sync.h"
#include "ui.h"

#define RATE 48000

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void write_wav(const char *path, const int16_t *pcm, int frames)
{
    FILE *f = fopen(path, "wb");
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + frames * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32(f, RATE);
    put32(f, RATE * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, frames * 4);
    fwrite(pcm, 4, frames, f);
    fclose(f);
}

static void write_ppm(const char *path, const uint32_t *fb)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint8_t rgb[3] = {fb[i] >> 16, fb[i] >> 8, fb[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void shot(const char *out, const char *name, uint32_t *fb)
{
    char path[512];
    ui_draw(fb);
    snprintf(path, sizeof path, "%s/%s.ppm", out, name);
    write_ppm(path, fb);
}

static void press(int b)
{
    ui_button(b, 1);
    ui_button(b, 0);
}

/* START + R1: the next page */
static void next_page(void)
{
    ui_button(BTN_START, 1);
    press(BTN_R1);
    ui_button(BTN_START, 0);
}

static double rms_of(const int16_t *pcm, int from, int n)
{
    double r = 0;
    for (int i = from; i < from + n; i++)
        r += (double)pcm[2 * i] * pcm[2 * i];
    return sqrt(r / n);
}

/* pitch of the left channel by autocorrelation near an expected pitch,
 * with a parabolic peak for sub-sample accuracy */
static double hz_near(const int16_t *pcm, int from, int n, double expect)
{
    int lo = (int)(RATE / expect * 0.85), hi = (int)(RATE / expect * 1.18), best = lo;
    double *c = malloc(sizeof(double) * (hi - lo + 1)), bc = -1e30;
    for (int lag = lo; lag <= hi; lag++) {
        double acc = 0;
        for (int i = from; i < from + n; i++)
            acc += (double)pcm[2 * i] * pcm[2 * (i + lag)];
        c[lag - lo] = acc;
        if (acc > bc)
            bc = acc, best = lag;
    }
    double off = 0;
    if (best > lo && best < hi) {
        double y0 = c[best - lo - 1], y1 = c[best - lo], y2 = c[best - lo + 1];
        off = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
    }
    free(c);
    return RATE / (best + off);
}

/* YIN pitch over 40 .. 1500 Hz (no expected pitch needed) */
static double hz_yin(const int16_t *pcm, int from, int n)
{
    static double d[RATE / 40 + 2];
    int maxlag = RATE / 40, minlag = RATE / 1500, best = -1;
    double run = 0;
    for (int lag = 1; lag <= maxlag; lag++) {
        double acc = 0;
        for (int i = from; i < from + n; i++) {
            double x = pcm[2 * i] - pcm[2 * (i + lag)];
            acc += x * x;
        }
        run += acc;
        d[lag] = run > 0 ? acc * lag / run : 1;
    }
    for (int lag = minlag; lag < maxlag; lag++)
        if (d[lag] < 0.15) {
            while (lag + 1 < maxlag && d[lag + 1] < d[lag])
                lag++;
            best = lag;
            break;
        }
    if (best < 0) {
        double mn = 1e9;
        for (int lag = minlag; lag < maxlag; lag++)
            if (d[lag] < mn)
                mn = d[lag], best = lag;
    }
    double y0 = d[best - 1], y1 = d[best], y2 = d[best + 1], off = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
    if (!(off > -1 && off < 1))
        off = 0;
    return RATE / (best + off);
}

/* one wind (or string) voice alone: string 3 at a JUST MAJOR degree */
static void solo(int engine, int ex, int mat, int note, int breath)
{
    song_default();
    for (int s = 0; s < NS; s++)
        song.sp[s][SP_VOICE] = engine == ENG_WIND ? 1 + ex : 0;
    song.gp[GP_MATERIAL] = mat;
    engine_init(RATE);
    engine_set_playing(1);
    song.gp[GP_CHANGES] = 0;
    song.fx[FX_SCOUNT] = 0;
    song.fx[FX_DMIX] = song.fx[FX_VSEND] = song.fx[FX_BODY] = 0;
    song.gp[GP_DRIFT] = 0;
    song.lfo[0][LP_DEPTH] = 0;
    song.gp[GP_CUTOFF] = 100;
    song.gp[GP_GLIDE] = 0;
    for (int s = 0; s < NS; s++)
        song.on[s] = s == 2, song.sp[s][SP_PON] = song.sp[s][SP_BON] = song.sp[s][SP_FINE] = 0;
    song.sp[2][SP_BOW] = breath;
    song.sp[2][SP_NOTE] = note;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    char path[512];
    static uint32_t fb[SCREEN_W * SCREEN_H];
    ui_set_dir(out);
    song_default();
    engine_init(RATE);
    engine_set_playing(1);
    ui_init();
    shot(out, "splash", fb);
    press(BTN_A); /* a press clears the start-up screen and does nothing else */

    const int secs = 12, chunk = RATE / 100;
    int16_t *pcm = malloc(sizeof(int16_t) * 2 * RATE * secs);
    static const char *const pages[PAGE_COUNT] = {"play", "sound", "compose", "mix", "song"};
    for (int t = 0, f = 0; t < secs * 100; t++, f += chunk) {
        engine_render(pcm + 2 * f, chunk);
        ui_tick(10);
        if (t == 300)
            press(BTN_DOWN), press(BTN_DOWN), press(BTN_X), press(BTN_X), press(BTN_X), press(BTN_SELECT), press(BTN_SELECT),
                press(BTN_SELECT), press(BTN_SELECT);
        if (t >= 320 && t < 320 + PAGE_COUNT * 10 && (t - 320) % 10 == 0) {
            shot(out, pages[(t - 320) / 10], fb);
            next_page();
        }
        if (t == 420) { /* back on PLAY: the quiet view, then hand-bow string 2 */
            press(BTN_L2);
        }
        if (t == 470)
            shot(out, "quiet", fb);
        if (t == 480) {
            press(BTN_L2);
            press(BTN_RIGHT);
            ui_button(BTN_R1, 1); /* bow string 2 by hand */
        }
        if (t == 560) {
            shot(out, "play_bow", fb);
            ui_button(BTN_R1, 0);
        }
    }
    /* controls on PLAY, as on HI5 and RadioScan: A on/off, B fewer pulses
     * (wrapping), X rotate, Y new pattern; START + R1 pages without stopping
     * the drone; a plain START tap stops it; L1 plucks */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        for (int i = 0; i < PAGE_COUNT; i++)
            next_page(); /* all the way round, back to PLAY */
        int playing_after_pages = live.playing;
        press(BTN_LEFT); /* the screenshots above left string 2 selected */
        int *P = song.sp[0];
        press(BTN_UP); /* the pluck ring of string 1: E(1,4) */
        int on0 = P[SP_PON];
        press(BTN_A);
        int on1 = P[SP_PON];
        press(BTN_A);
        press(BTN_B); /* 1 pulse -> wraps to 4 */
        int wrapped = P[SP_PPULSES];
        press(BTN_B);
        int fewer = P[SP_PPULSES];
        press(BTN_X);
        int rot = P[SP_PROT];
        press(BTN_L1);
        engine_render(pcm, RATE / 100);
        float m = live.meter[0];
        press(BTN_START);
        printf("controls: pages keep drone %d; A %d->%d; B wraps 1->%d then %d; X rotate ->%d; L1 pluck meter %.3f; START tap -> playing %d\n",
               playing_after_pages, on0, on1, wrapped, fewer, rot, m, live.playing);
        engine_set_playing(1);

        /* hand playing touches only the selected string; a held R1 bows on
         * through a driver's quick release-and-press; an L1 glitch does not
         * pluck twice */
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.fx[FX_SCOUNT] = 0;
        for (int s = 0; s < NS; s++)
            song.sp[s][SP_PON] = song.sp[s][SP_BON] = 0, song.sp[s][SP_BOW] = 0;
        engine_render(pcm, RATE);
        press(BTN_RIGHT), press(BTN_RIGHT); /* string 3 (from string 1) */
        ui_button(BTN_R1, 1);
        float mn = 1, others = 0;
        for (int i = 0; i < 300; i++) { /* 3 s held, with a 20 ms glitch at 1.5 s */
            if (i == 150)
                ui_button(BTN_R1, 0);
            if (i == 152)
                ui_button(BTN_R1, 1);
            engine_render(pcm, RATE / 100);
            ui_tick(10);
            if (i > 60)
                mn = fminf(mn, live.meter[2]);
            for (int s = 0; s < NS; s++)
                if (s != 2)
                    others = fmaxf(others, live.meter[s]);
        }
        ui_button(BTN_R1, 0);
        for (int i = 0; i < 100; i++)
            engine_render(pcm, RATE / 100), ui_tick(10);
        float after = live.meter[2];
        int plucks = 0;
        float last = 0;
        ui_button(BTN_L1, 1);
        ui_button(BTN_L1, 0);
        ui_tick(10);
        ui_button(BTN_L1, 1); /* 10 ms later: a glitch */
        for (int i = 0; i < 30; i++) {
            engine_render(pcm, RATE / 100);
            ui_tick(10);
            float m = live.meter[2];
            if (m > last * 1.5f && m > 0.3f)
                plucks++;
            last = m;
        }
        ui_button(BTN_L1, 0);
        printf("hand: string 3 bowed by R1 stays at least %.3f through a glitch (others at most %.3f), %.3f a second after release; L1 + glitch -> %d pluck\n",
               mn, others, after, plucks);
    }

    /* the log: a short DETAIL session with presses, edits, chords, hand
     * playing and a simulated slow audio callback, then its summary */
    {
        char lp[512], cp[512];
        snprintf(lp, sizeof lp, "%s/session_log.txt", out);
        snprintf(cp, sizeof cp, "%s/session.cfg", out);
        remove(lp);
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        uint64_t ms = 1000;
        jdlog_set_clock(ms);
        jdlog_open(lp, cp);
        jdlog_set_level(LOG_DETAIL);
        AudioTiming at = {0};
        at.buffer_us = 21333;
        for (int i = 0; i < 300; i++) { /* 3 s */
            engine_render(pcm, RATE / 100);
            ui_tick(10);
            ms += 10;
            at.calls++;
            at.us_sum += i == 150 ? 30000 : 300; /* one slow callback */
            at.us_max = at.us_max > (i == 150 ? 30000u : 300u) ? at.us_max : (i == 150 ? 30000u : 300u);
            at.late += i == 150;
            jdlog_tick(ms, &at);
            if (i == 20)
                press(BTN_RIGHT), press(BTN_X), press(BTN_B);
            if (i == 40)
                ui_button(BTN_START, 1), press(BTN_R1), ui_button(BTN_START, 0); /* to SOUND */
            if (i >= 60 && i < 70)
                press(BTN_X); /* one parameter, ten times: one merged line */
            if (i == 120)
                press(BTN_SELECT);
            if (i == 140)
                ui_button(BTN_START, 1), press(BTN_L1), ui_button(BTN_START, 0); /* back to PLAY */
            if (i == 160)
                ui_button(BTN_R1, 1);
            if (i == 220)
                ui_button(BTN_R1, 0);
        }
        jdlog_set_clock(ms);
        jdlog_close();
        FILE *lf = fopen(lp, "r");
        int lines = 0, stat = 0, ev = 0, hint = 0, sum = 0, voice = 0, param = 0;
        char ln[256];
        while (lf && fgets(ln, sizeof ln, lf)) {
            lines++;
            stat += strstr(ln, " STAT ") != NULL;
            ev += strstr(ln, " EVENT ") != NULL;
            hint += strstr(ln, " HINT ") != NULL;
            sum += strstr(ln, " SUM ") != NULL;
            voice += strstr(ln, " VOICE ") != NULL;
            param += strstr(ln, "param ") != NULL;
        }
        if (lf)
            fclose(lf);
        printf("log: %d lines (%d status, %d voice, %d events incl. %d merged param lines, %d hints, %d summary)\n", lines, stat,
               voice, ev, param, hint, sum);
    }

    snprintf(path, sizeof path, "%s/drone.wav", out);
    write_wav(path, pcm, RATE * secs);
    printf("rms per second:");
    for (int s = 0; s < secs; s++)
        printf(" %.0f", rms_of(pcm, s * RATE, RATE));
    printf("\n");

    /* tuning: string 1 at A2 with a pure pluck, no bow, no drift or fx */
    song_default();
    engine_init(RATE);
    engine_set_playing(1);
    for (int s = 1; s < NS; s++)
        song.on[s] = 0;
    song.sp[0][SP_BOW] = 0;
    song.sp[0][SP_PON] = 0;
    song.gp[GP_CHANGES] = 0;
    song.gp[GP_GLIDE] = 0;
    song.fx[FX_SYMP] = song.fx[FX_BODY] = 0;
    song.gp[GP_DRIFT] = 0;
    song.gp[GP_CUTOFF] = 100;
    song.fx[FX_DMIX] = 0;
    song.fx[FX_VSEND] = 0;
    song.lfo[0][LP_DEPTH] = 0;
    engine_render(pcm, RATE / 10);
    engine_pluck(0);
    engine_render(pcm, RATE);
    printf("tuning: string 1 = %.2f Hz (want 110)\n", hz_near(pcm, RATE / 4, RATE / 4, 110));
    /* just intonation: degree 4 of JUST MAJOR is a pure fifth, 165 Hz */
    song.sp[0][SP_NOTE] = 4;
    engine_pluck(0);
    engine_render(pcm, RATE);
    printf("tuning: JUST MAJOR degree 4 = %.2f Hz (want 165.00, a pure 3/2)\n", hz_near(pcm, RATE / 4, RATE / 4, 165));
    song.sp[0][SP_NOTE] = 0;
    engine_pluck(0);
    engine_render(pcm, RATE / 2);

    /* glide: an octave up with GLIDE = 1 BAR (3.3 s at 72) slides, no pluck */
    song.sp[0][SP_BOW] = 50; /* keep it ringing without a pluck */
    engine_render(pcm, RATE);
    song.gp[GP_GLIDE] = 6;
    song.sp[0][SP_NOTE] = 7; /* an octave: seven degrees of JUST MAJOR */
    engine_render(pcm, RATE / 2);
    double g1 = hz_near(pcm, RATE / 4, RATE / 8, 150);
    engine_render(pcm, RATE * 5);
    double g2 = hz_near(pcm, RATE * 4, RATE / 2, 220);
    printf("glide 1 BAR: 0.4 s after the change %.1f Hz (between 110 and 220), after 5.5 s %.1f Hz (220), rms %.0f\n", g1, g2,
           rms_of(pcm, RATE * 4, RATE));
    song.gp[GP_GLIDE] = 0;
    song.sp[0][SP_NOTE] = 0;
    engine_render(pcm, RATE / 4);
    printf("glide OFF: 0.1 s after the change %.1f Hz (110)\n", hz_near(pcm, RATE / 8, RATE / 8, 110));

    /* mono voicing: every string sits on string 1's note */
    song_default();
    song.gp[GP_MODE] = MODE_MONO;
    char v[20];
    sparam_format(NS - 1, SP_NOTE, v, sizeof v);
    printf("mono: string %d note shows '%s'\n", NS, v);

    /* save compatibility: a v0.0.1 four-string file loads, string 5 gets defaults */
    {
        enum { NGP_OLD = 7, NSPO = 10, LEVELO = 2 }; /* before GLIDE; v0.0.3 string layout */
        int32_t old[1 + 4 + 4 * NSPO + NGP_OLD + NLFO * NLP + 9];
        memset(old, 0, sizeof old);
        old[0] = 0x4a445203; /* JDR3 */
        for (int s = 0; s < 4; s++)
            old[1 + s] = s != 1;
        old[1 + 4 + 1 * NSPO + 0] = 5;
        old[1 + 4 + 3 * NSPO + LEVELO] = 77;
        old[1 + 4 + 4 * NSPO + 2] = 99; /* bpm */
        old[1 + 4 + 4 * NSPO + 4] = 33; /* cutoff, which GLIDE now sits before */
        old[1 + 4 + 4 * NSPO + NGP_OLD + NLFO * NLP + FX_DMIX] = 66;
        snprintf(path, sizeof path, "%s/old_v3.sav", out);
        FILE *f = fopen(path, "wb");
        fwrite(old, 1, sizeof old, f);
        fclose(f);
        song_default();
        int ok = song_load(path);
        printf("save compat v0.0.1: load=%d on2=%d note2=%d level4=%d bpm=%d cutoff=%d glide=%d dmix=%d string5 level=%d on=%d\n",
               ok, song.on[1], song.sp[1][SP_NOTE], song.sp[3][SP_LEVEL], song.gp[GP_BPM], song.gp[GP_CUTOFF],
               song.gp[GP_GLIDE], song.fx[FX_DMIX], song.sp[4][SP_LEVEL], song.on[4]);
        /* a v0.0.2 file: five strings, seven globals */
        int32_t old4[1 + NS + NS * NSPO + NGP_OLD + NLFO * NLP + 9];
        memset(old4, 0, sizeof old4);
        old4[0] = 0x4a445204; /* JDR4 */
        for (int s = 0; s < NS; s++)
            old4[1 + s] = 1;
        old4[1 + NS + 4 * NSPO + 0] = 9;
        old4[1 + NS + NS * NSPO + 4] = 44; /* cutoff */
        old4[1 + NS + NS * NSPO + 6] = 2;  /* sync LEAD */
        snprintf(path, sizeof path, "%s/old_v4.sav", out);
        f = fopen(path, "wb");
        fwrite(old4, 1, sizeof old4, f);
        fclose(f);
        song_default();
        ok = song_load(path);
        printf("save compat v0.0.2: load=%d note5=%d cutoff=%d sync=%d glide=%d\n", ok, song.sp[4][SP_NOTE],
               song.gp[GP_CUTOFF], song.gp[GP_SYNC], song.gp[GP_GLIDE]);
        /* a v0.0.3 file: PLUCK 1 BAR on string 1 becomes a one-pulse ring of one bar */
        int32_t old5[1 + NS + NS * NSPO + 8 + NLFO * NLP + 9];
        memset(old5, 0, sizeof old5);
        old5[0] = 0x4a445205; /* JDR5 */
        for (int s = 0; s < NS; s++)
            old5[1 + s] = 1;
        old5[1 + NS + 0 * NSPO + 4] = 3; /* PLUCK 1 BAR */
        old5[1 + NS + 2 * NSPO + 0] = 7; /* note */
        old5[1 + NS + NS * NSPO + 1] = 50; /* root */
        old5[1 + NS + NS * NSPO + 4] = 6;  /* glide */
        snprintf(path, sizeof path, "%s/old_v5.sav", out);
        f = fopen(path, "wb");
        fwrite(old5, 1, sizeof old5, f);
        fclose(f);
        song_default();
        ok = song_load(path);
        printf("save compat v0.0.3: load=%d scale=%d root=%d glide=%d note3=%d s1 pluck on=%d E(%d,%d) rate=%d s2 pluck=%d couple=%d body=%d changes=%d\n",
               ok, song.gp[GP_SCALE], song.gp[GP_ROOT], song.gp[GP_GLIDE], song.sp[2][SP_NOTE], song.sp[0][SP_PON],
               song.sp[0][SP_PPULSES], song.sp[0][SP_PSTEPS], song.sp[0][SP_RATE], song.sp[1][SP_PON], song.fx[FX_SYMP],
               song.fx[FX_BODY], song.gp[GP_CHANGES]);
    }

    /* a v0.1 save (JDR6): loads as it was, with nothing new switched on */
    {
        enum { NSP6 = 19, NGP6 = 10, NFX6 = 12 };
        int32_t v6[1 + NS + NS * NSP6 + NGP6 + NLFO * NLP + NFX6];
        song_default();
        memset(v6, 0, sizeof v6);
        v6[0] = 0x4a445206;
        int k = 1;
        for (int s = 0; s < NS; s++)
            v6[k++] = 1;
        for (int s = 0; s < NS; s++)
            for (int p = 0; p < NSP6; p++)
                v6[k++] = song.sp[s][p];
        for (int p = 0; p < NGP6; p++)
            v6[k++] = song.gp[p];
        v6[1 + NS + NS * NSP6 + GP_SCALE] = 6; /* YAMAN */
        k += NLFO * NLP;
        v6[k + FX_BODY] = 77;
        char p6[512];
        snprintf(p6, sizeof p6, "%s/old_v6.sav", out);
        FILE *f6 = fopen(p6, "wb");
        fwrite(v6, 1, sizeof v6, f6);
        fclose(f6);
        song_default();
        int ok = song_load(p6);
        printf("save compat v0.1: load=%d scale=%d body=%d symp=%d evolve=%d pan1=%d pan5=%d\n", ok, song.gp[GP_SCALE],
               song.fx[FX_BODY], song.fx[FX_SCOUNT], song.gp[GP_EVOLVE], song.sp[0][SP_PAN], song.sp[4][SP_PAN]);
    }

    /* tone shapers: each must change the spectrum, not the level much */
    {
        static const int which[5] = {SP_LEVEL, SP_FOLD, SP_STIFF, SP_POS, SP_BUZZ};
        static const char *const nm[5] = {"(none)", "FOLD", "STIFF", "POS", "BUZZ"};
        for (int k = 0; k < 5; k++) {
            song_default();
            engine_init(RATE);
            engine_set_playing(1);
            for (int s = 1; s < NS; s++)
                song.on[s] = 0;
            if (k)
                song.sp[0][which[k]] = 70;
            song.gp[GP_DRIFT] = 0;
            song.gp[GP_CHANGES] = 0;
            song.fx[FX_SYMP] = song.fx[FX_BODY] = 0;
            song.fx[FX_DMIX] = 0;
            song.fx[FX_VSEND] = 0;
            song.lfo[0][LP_DEPTH] = 0;
            engine_render(pcm, RATE * 3);
            engine_render(pcm, RATE);
            /* crude brightness: zero crossings per second */
            int zc = 0;
            for (int i = 1; i < RATE; i++)
                zc += (pcm[2 * i] >= 0) != (pcm[2 * i - 2] >= 0);
            printf("tone %s=70: rms %.0f, zero crossings %d\n", nm[k], rms_of(pcm, 0, RATE), zc);
        }
    }

    /* sympathetic coupling: string 1 plucked alone; string 3 (an octave up,
     * silent: no bow, no plucks) must start to ring through the bridge */
    for (int c = 0; c <= 100; c += 50) {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.gp[GP_DRIFT] = 0;
        song.fx[FX_BODY] = 0;
        song.fx[FX_SYMP] = c;
        for (int s = 0; s < NS; s++) {
            song.sp[s][SP_BOW] = 0;
            song.sp[s][SP_PON] = 0;
            song.sp[s][SP_FINE] = 0;
            song.on[s] = s == 0 || s == 2;
        }
        engine_render(pcm, RATE / 10);
        engine_pluck(0);
        float peak = 0;
        for (int i = 0; i < 100; i++) {
            engine_render(pcm, RATE / 100);
            if (live.meter[2] > peak)
                peak = live.meter[2];
        }
        printf("couple %3d: string 3 (untouched) peak %.4f, string 1 %.3f\n", c, peak, live.meter[0]);
    }

    /* body: each type colours the default drone; level stays in range */
    for (int b = -1; b < NBODY; b++) {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.fx[FX_BODY] = b < 0 ? 0 : 80;
        song.fx[FX_BTYPE] = b < 0 ? 0 : b;
        engine_render(pcm, RATE * 4);
        int zc = 0;
        for (int i = 1; i < RATE * 4; i++)
            zc += (pcm[2 * i] >= 0) != (pcm[2 * i - 2] >= 0);
        printf("body %-8s: rms %.0f, zero crossings/s %d\n", b < 0 ? "OFF" : (const char *[]){"WOOD", "GOURD", "BRONZE", "TUNED", "METAL", "CERAMIC", "GLASS"}[b],
               rms_of(pcm, RATE, RATE * 3), zc / 4);
    }

    /* rings: count plucks and bow openings over 4 bars */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        uint32_t c0 = live.chords;
        int hits[NS][NRING] = {{0}};
        float last[NS][NRING] = {{0}};
        float bmin = 1, bmax = 0;
        int secs4 = (int)(16 * 60.0 / song.gp[GP_BPM] * 100); /* 4 bars in 10 ms blocks */
        for (int i = 0; i < secs4; i++) {
            engine_render(pcm, RATE / 100);
            for (int s = 0; s < NS; s++)
                for (int r = 0; r < NRING; r++) {
                    if (live.flash[s][r] > last[s][r] + 0.3f)
                        hits[s][r]++;
                    last[s][r] = live.flash[s][r];
                }
            if (i > 100) {
                bmin = fminf(bmin, live.bow_env[4]);
                bmax = fmaxf(bmax, live.bow_env[4]);
            }
        }
        printf("rings over 4 bars, plucks per string:");
        for (int s = 0; s < NS; s++)
            printf(" E(%d,%d)/%s=%d", song.sp[s][SP_PPULSES], song.sp[s][SP_PSTEPS],
                   (const char *[]){"16", "8", "4", "2", "1b", "2b"}[song.sp[s][SP_RATE]], hits[s][0]);
        printf("\n  string 5 bow ring E(%d,%d): %d openings, swell %.2f..%.2f; chords changed %u (CHANGES off)\n",
               song.sp[4][SP_BPULSES], song.sp[4][SP_BSTEPS], hits[4][1], bmin, bmax, live.chords - c0);
        /* CHANGES 1 BAR: four bars, four chords (bar 0 is the start) */
        song.gp[GP_CHANGES] = 1;
        c0 = live.chords;
        for (int i = 0; i < secs4; i++)
            engine_render(pcm, RATE / 100);
        printf("  CHANGES 1 BAR: %u chord changes in 4 bars\n", live.chords - c0);
    }

    /* sympathetic strings: each tuning, and how much they add */
    for (int t = -1; t < NSTUNE; t++) {
        static const char *const tn[NSTUNE] = {"SCALE", "CHORD", "CHORD+", "DRONE", "FIFTHS"};
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.fx[FX_SCOUNT] = t < 0 ? 0 : 7;
        song.fx[FX_STUNE] = t < 0 ? 0 : t;
        double r = 0, sm = 0;
        for (int i = 0; i < 6; i++) {
            engine_render(pcm, RATE);
            if (i >= 2)
                r += rms_of(pcm, 0, RATE) / 4, sm += live.symp_meter / 4;
        }
        printf("symp %-6s: rms %.0f, symp peak %.3f, tuned:", t < 0 ? "OFF" : tn[t], r, sm);
        for (int k = 0; t >= 0 && k < 7; k++) {
            char nm[16];
            engine_degree_name(engine_symp_degree(k), nm, sizeof nm);
            printf(" %s", nm);
        }
        printf("\n");
    }
    /* CHORD tuning follows a chord change */
    {
        song_default();
        song.fx[FX_SCOUNT] = 6;
        engine_init(RATE);
        engine_set_playing(1);
        engine_render(pcm, RATE / 10);
        engine_next_chord();
        engine_render(pcm, RATE / 10);
        printf("symp after a change to [%s]:", engine_chord_name());
        for (int k = 0; k < 6; k++) {
            char nm[16];
            engine_degree_name(engine_symp_degree(k), nm, sizeof nm);
            printf(" %s", nm);
        }
        printf("\n");
    }
    /* MOTION 0 vs 100 (root steps), EVOLVE 100 (rhythm nudges per change) */
    for (int mo = 0; mo <= 100; mo += 100) {
        song_default();
        song.gp[GP_MOTION] = mo;
        song.gp[GP_EVOLVE] = 100;
        int before[NS][NSP];
        memcpy(before, song.sp, sizeof before);
        printf("MOTION %3d:", mo);
        for (int i = 0; i < 10; i++) {
            engine_next_chord();
            printf(" %s", engine_chord_name());
        }
        int nudged = 0;
        for (int s = 0; s < NS; s++)
            for (int p = SP_PON; p <= SP_BROT; p++)
                nudged += before[s][p] != song.sp[s][p];
        printf("  (EVOLVE 100: %d ring settings changed)\n", nudged);
    }

    /* progressions: eight changes in each scale */
    for (int sc = 0; sc < engine_nscales(); sc++) {
        song_default();
        engine_set_scale(sc);
        printf("%-10s:", engine_scale_name(sc));
        for (int i = 0; i < 8; i++) {
            engine_next_chord();
            printf(" [%s]", engine_chord_name());
        }
        printf("\n            last voicing:");
        for (int s = 0; s < NS; s++) {
            char nm[16];
            engine_degree_name(song.sp[s][SP_NOTE], nm, sizeof nm);
            printf(" %s", nm);
        }
        printf("\n");
    }

    /* binaural: left and right drift in and out of phase at the beat; count
     * the sign flips of their correlation in 50 ms windows (two a beat).
     * Indices into the beat table: 0.5 Hz, 4 Hz, 8 Hz */
    static const int beat_idx[3] = {4, 12, 21};
    static const double beat_want[3] = {0.5, 4.0, 8.0};
    for (int k = 0; k < 3; k++) {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.fx[FX_SCOUNT] = 0;
        song.fx[FX_DMIX] = song.fx[FX_VSEND] = 0;
        song.fx[FX_BINBEAT] = beat_idx[k];
        song.lfo[0][LP_DEPTH] = 0;
        song.gp[GP_DRIFT] = 0;
        for (int s = 0; s < NS; s++)
            song.on[s] = s == 2, song.sp[s][SP_PON] = 0, song.sp[s][SP_PAN] = 0;
        engine_render(pcm, RATE * 2);
        int secs8 = 8;
        engine_render(pcm, RATE * secs8);
        /* the beat lives below 1 kHz (only that part is shifted): lowpass both ears */
        static float lo[2][RATE * 8];
        float a0 = 0, a1 = 0, b0 = 0, b1 = 0, lk = 1.f - expf(-6.2831853f * 600.f / RATE);
        for (int i = 0; i < RATE * secs8; i++) {
            a0 += (pcm[2 * i] - a0) * lk, a1 += (a0 - a1) * lk;
            b0 += (pcm[2 * i + 1] - b0) * lk, b1 += (b0 - b1) * lk;
            lo[0][i] = a1, lo[1][i] = b1;
        }
        int flips = 0, prev = 0;
        for (int w = 0; w + 2400 <= RATE * secs8; w += 1200) {
            double c = 0;
            for (int i = w; i < w + 2400; i++)
                c += (double)lo[0][i] * lo[1][i];
            int sg = c > 0 ? 1 : -1;
            if (prev && sg != prev)
                flips++;
            prev = sg;
        }
        printf("binaural %.1f Hz: L/R correlation flips %.1f per s (want about %.1f)\n", beat_want[k], flips / (double)secs8,
               beat_want[k] * 2);
    }
    /* OMBAK: both ears beat together (no flips between them), and the low
     * end swells and fades at the beat rate, 4 Hz here */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.fx[FX_SCOUNT] = 0;
        song.fx[FX_DMIX] = song.fx[FX_VSEND] = 0;
        song.fx[FX_BINBEAT] = 12; /* 4 Hz */
        song.fx[FX_BINLOCK] = BIN_OMBAK;
        song.lfo[0][LP_DEPTH] = 0;
        song.gp[GP_DRIFT] = 0;
        for (int s = 0; s < NS; s++)
            song.on[s] = s == 2, song.sp[s][SP_PON] = 0, song.sp[s][SP_PAN] = 0;
        engine_render(pcm, RATE * 2);
        engine_render(pcm, RATE * 4);
        float a0 = 0, a1 = 0, b0 = 0, b1 = 0, lk = 1.f - expf(-6.2831853f * 600.f / RATE);
        static float env[RATE * 4 / 240];
        int flips = 0, prev = 0, ne = 0;
        double c = 0, e = 0;
        for (int i = 0; i < RATE * 4; i++) {
            a0 += (pcm[2 * i] - a0) * lk, a1 += (a0 - a1) * lk;
            b0 += (pcm[2 * i + 1] - b0) * lk, b1 += (b0 - b1) * lk;
            c += (double)a1 * b1;
            e += (double)a1 * a1;
            if ((i + 1) % 240 == 0) { /* 5 ms windows */
                int sg = c > 0 ? 1 : -1;
                if (prev && sg != prev && fabs(c) > 1e3)
                    flips++;
                prev = sg;
                env[ne++] = (float)sqrt(e / 240);
                c = e = 0;
            }
        }
        /* the swell's period: where the envelope best matches itself, 1.3 .. 20 Hz */
        double mean = 0;
        for (int i = 0; i < ne; i++)
            mean += env[i] / ne;
        /* the envelope's self-match falls from lag 0 and rises again at the
         * period: skip to where it first goes below zero, then take the peak */
        static double cc[160];
        for (int lag = 1; lag < 160; lag++) {
            cc[lag] = 0;
            for (int i = 0; i + lag < ne; i++)
                cc[lag] += (env[i] - mean) * (env[i + lag] - mean);
            cc[lag] /= ne - lag;
        }
        int start = 1, best = 0;
        while (start < 159 && cc[start] > 0)
            start++;
        double bestc = -1e30;
        for (int lag = start; lag < 160; lag++)
            if (cc[lag] > bestc)
                bestc = cc[lag], best = lag;
        double swell_hz = best ? RATE / 240.0 / best : 0;
        printf("ombak 4 Hz: L/R flips %d (want 0), swells %.1f per s (want about 4)\n", flips, swell_hz);
    }
    /* the Balinese gong kebyar profiles: each degree where Toth put it */
    {
        static const char *const bal[3] = {"BEGBEG", "SEDANG", "TIRUS"};
        static const float want[3][5] = {{0, 120, 234, 666, 747}, {0, 136, 291, 670, 804}, {0, 197, 377, 724, 828}};
        int ok = 0;
        song_default();
        for (int b = 0; b < 3; b++)
            for (int sc = 0; sc < engine_nscales(); sc++)
                if (!strcmp(engine_scale_name(sc), bal[b])) {
                    engine_set_scale(sc);
                    int good = engine_scale_n() == 5;
                    for (int i = 0; i < 5 && good; i++)
                        good = fabsf(engine_scale_cents(i) - want[b][i]) < 0.5f;
                    ok += good;
                }
        printf("balinese tunings: %d/3 profiles in place\n", ok);
    }
    /* a journey starts from alpha: ALPHA to THETA begins at 10 Hz */
    song_default();
    engine_init(RATE);
    engine_set_playing(1);
    song.fx[FX_BINBEAT] = 16;
    song.fx[FX_BINLOCK] = 2;
    engine_render(pcm, RATE);
    printf("binaural journey A>T: beat now %.2f Hz (starts at 10)\n", live.bin_hz);

    /* HARM: string 1 (A2) touched at 1/2 sounds an octave up: the waveform
     * repeats every half period */
    for (int h = 0; h <= 1; h++) {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.fx[FX_SCOUNT] = 0;
        song.fx[FX_DMIX] = song.fx[FX_VSEND] = 0;
        song.fx[FX_BODY] = 0;
        song.lfo[0][LP_DEPTH] = 0;
        song.gp[GP_DRIFT] = 0;
        for (int s = 0; s < NS; s++)
            song.on[s] = s == 0, song.sp[s][SP_PON] = 0;
        song.sp[0][SP_HARM] = h;
        song.sp[0][SP_BOW] = 60;
        engine_render(pcm, RATE * 3);
        int T = RATE / 110;
        double c0 = 0, ch = 0, cf = 0;
        for (int i = RATE; i < RATE * 2; i++) {
            c0 += (double)pcm[2 * i] * pcm[2 * i];
            ch += (double)pcm[2 * i] * pcm[2 * (i + T / 2)];
            cf += (double)pcm[2 * i] * pcm[2 * (i + T)];
        }
        printf("HARM %s: correlation at half a period %.2f, at a period %.2f\n", h ? "2ND" : "OFF", ch / c0, cf / c0);
    }

    /* chance: string 1 E(4,4) at 1/4 with 50%% chance plucks about half the time */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.sp[0][SP_PPULSES] = 4;
        song.sp[0][SP_PCHANCE] = 50;
        int hits = 0;
        float last = 0;
        int blocks = (int)(64 * 60.0 / song.gp[GP_BPM] * 100); /* 64 beats */
        for (int i = 0; i < blocks; i++) {
            engine_render(pcm, RATE / 100);
            if (live.flash[0][0] > last + 0.3f)
                hits++;
            last = live.flash[0][0];
        }
        printf("chance 50%%: %d plucks of 64 pulses\n", hits);
    }

    /* RELATE: how many notes each chord keeps from the one before */
    for (int rel = 0; rel <= 2; rel++) {
        song_default();
        song.gp[GP_RELATE] = rel;
        int kept = 0, changes = 40;
        for (int i = 0; i < changes; i++) {
            int before[NS], n = engine_scale_n();
            for (int s = 0; s < NS; s++)
                before[s] = ((song.sp[s][SP_NOTE] % n) + n) % n;
            engine_next_chord();
            int common = 0;
            for (int a = 0; a < NS; a++) {
                int pc = ((song.sp[a][SP_NOTE] % n) + n) % n, seen = 0;
                for (int b = 0; b < a; b++)
                    seen |= ((song.sp[b][SP_NOTE] % n) + n) % n == pc;
                if (seen)
                    continue;
                for (int b = 0; b < NS; b++)
                    if (before[b] == pc) {
                        common++;
                        break;
                    }
            }
            kept += common;
        }
        printf("RELATE %d: on average %.2f notes kept per change\n", rel, kept / 40.0);
    }

    /* string GATE: with the bow ring breathing, GATE 100 makes the string's
     * level follow it */
    for (int g = 0; g <= 100; g += 100) {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.fx[FX_SCOUNT] = 0;
        song.fx[FX_DMIX] = song.fx[FX_VSEND] = 0;
        for (int s = 0; s < NS; s++)
            song.on[s] = s == 4, song.sp[s][SP_PON] = 0;
        song.sp[4][SP_BCHANCE] = 100;
        song.sp[4][SP_GATE] = g;
        engine_render(pcm, RATE * 3);
        engine_render(pcm, RATE * 3);
        double lo = 1e9, hi = 0;
        for (int i = 0; i + 2400 <= RATE * 3; i += 2400) {
            double r = rms_of(pcm, i, 2400);
            lo = r < lo ? r : lo;
            hi = r > hi ? r : hi;
        }
        printf("string gate %3d: 50 ms rms ranges %.0f .. %.0f\n", g, lo, hi);
    }

    /* wifi sync: a leader at 128 BPM 5.3 steps ahead; lock tempo and phase */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_SYNC] = SYNC_AUTO;
        sync_core_init(0x80000000u);
        uint8_t pkt[SYNC_PKT_LEN];
        uint64_t t = 0;
        double lpos = 5.3, lbpm = 128.0, err = 0;
        for (int i = 0; i < 400; i++) {
            if (i % 10 == 0) {
                memcpy(pkt, "HI5S", 4);
                pkt[4] = 1, pkt[5] = 1, pkt[6] = SYNC_AUTO, pkt[7] = 0;
                uint32_t id = 7;
                memcpy(pkt + 8, &id, 4);
                float b = (float)lbpm;
                memcpy(pkt + 12, &b, 4);
                memcpy(pkt + 16, &lpos, 8);
                memcpy(pkt + 24, &t, 8);
                sync_handle_packet(pkt, SYNC_PKT_LEN, t);
            }
            engine_render(pcm, RATE / 100);
            lpos += lbpm / 60.0 * 4.0 * 0.01;
            t += 10000;
            if (i == 399) {
                err = fmod(lpos - engine_position(), 16.0);
                if (err > 8)
                    err -= 16;
                if (err <= -8)
                    err += 16;
            }
        }
        const SyncStatus *ss = sync_status();
        printf("sync: bpm=%.1f peers=%d leader=%d phase err after 4 s = %.3f steps\n", engine_bpm(), ss->peers,
               ss->have_leader, err);
        engine_set_rate(1.0);
    }

    /* pops: in 5 ms windows, the share of energy far above the master
     * lowpass, which only clipping or steps make; and full-scale samples */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        int n = 20 * RATE, worst_i = 0, bad = 0, full = 0;
        double worst = 0;
        int16_t *p = malloc(sizeof(int16_t) * 2 * n);
        for (int i = 0; i < n; i += 1024)
            engine_render(p + 2 * i, n - i < 1024 ? n - i : 1024);
        for (int w = RATE; w + 240 < n; w += 240) {
            double e = 0, h = 0;
            for (int i = w; i < w + 240; i++) {
                double d = p[2 * i] - 3.0 * p[2 * i - 2] + 3.0 * p[2 * i - 4] - p[2 * i - 6];
                e += (double)p[2 * i] * p[2 * i];
                h += d * d;
            }
            if (e < 240 * 1e4)
                continue;
            if (h / e > worst)
                worst = h / e, worst_i = w;
            bad += h / e > 0.05;
        }
        for (int i = 0; i < n; i++)
            full += abs(p[2 * i]) > 31500;
        printf("pops: worst window %.3f at %.2f s (want < 0.5), %d windows over 0.05, %d full-scale samples (want 0)\n", worst,
               (double)worst_i / RATE, bad, full);
        /* mute: string 1 alone (no sympathetic strings), muted, must fade to silence */
        song.fx[FX_SCOUNT] = 0;
        for (int s = 1; s < NS; s++)
            song.on[s] = 0;
        song.fx[FX_DMIX] = song.fx[FX_VSEND] = 0;
        engine_init(RATE); /* no reverb tail left from before */
        engine_render(p, RATE);
        double before = rms_of(p, RATE / 2, RATE / 2);
        song.on[0] = 0;
        engine_render(p, RATE / 2);
        printf("mute: string 1 rms %.0f, 0.4 s after muting %.0f (want 0)\n", before, rms_of(p, RATE * 2 / 5, RATE / 10));
        free(p);
    }

    /* WIND: every exciter in every material, three octaves: in tune within 25 cents */
    {
        static const char *const exn[NEXCITE] = {"LIP", "REED", "DOUBLE", "FLUTE", "FREE", "RECORDER", "BANSURI"};
        for (int ex = 0; ex < NEXCITE; ex++) {
            int ok = 0, tot = 0;
            double worst = 0, lvl = 0;
            for (int mat = 0; mat < NMATERIAL; mat++)
                for (int note = -7; note <= 7; note += 7) {
                    solo(ENG_WIND, ex, mat, note, 50);
                    engine_render(pcm, RATE * 3);
                    engine_render(pcm, RATE);
                    double want = 110.0 * pow(2.0, note / 7.0) * (ex >= EX_FLUTE && ex != EX_FREE ? 2 : 1);
                    double c = 1200 * log2(hz_yin(pcm, RATE / 4, RATE / 4) / want);
                    tot++;
                    ok += fabs(c) < 25;
                    if (fabs(c) >= 25 && getenv("JD_DEBUG"))
                        printf("  off: %s mat %d note %d: %+.0f c\n", exn[ex], mat, note, c);
                    if (fabs(c) > fabs(worst))
                        worst = c;
                    lvl += rms_of(pcm, 0, RATE) / 12;
                }
            printf("wind %-8s: %2d/%d in tune (worst %+.0f cents), rms %.0f\n", exn[ex], ok, tot, worst, lvl);
        }
        /* overblowing by hand (R1 with L1 held): a flute jumps an octave, a reed a twelfth */
        static const int obx[2] = {EX_FLUTE, EX_REED};
        for (int k = 0; k < 2; k++) {
            solo(ENG_WIND, obx[k], MAT_WOOD, 0, 50);
            engine_render(pcm, RATE * 2);
            double f0 = hz_yin(pcm, RATE, RATE / 4);
            engine_bow(2, 1);
            engine_overblow(2, 1);
            engine_render(pcm, RATE * 2);
            double f1 = hz_yin(pcm, RATE, RATE / 4);
            engine_overblow(2, 0);
            engine_bow(2, 0);
            printf("overblow %s: %.1f Hz -> %.1f Hz (x%.2f, want x%d)\n", exn[obx[k]], f0, f1, f1 / f0, k ? 3 : 2);
        }
        /* the same combination on a string touches the 2nd harmonic */
        solo(ENG_STRINGS, 0, 0, 0, 0);
        engine_bow(2, 1);
        engine_render(pcm, RATE * 2);
        double a0 = hz_yin(pcm, RATE, RATE / 4);
        engine_overblow(2, 1);
        engine_render(pcm, RATE * 2);
        double a1 = hz_yin(pcm, RATE, RATE / 4);
        engine_bow(2, 0);
        engine_overblow(2, 0);
        printf("string harmonic by hand: %.1f Hz -> %.1f Hz (want x2)\n", a0, a1);
    }

    /* undertones: energy an octave and a twelfth below a 110 Hz voice */
    for (int eng = 0; eng < NENGINE; eng++)
        for (int sub = 0; sub <= 70; sub += 70) {
            solo(eng, EX_REED, MAT_WOOD, 0, 60);
            song.gp[GP_SUB] = sub;
            song.gp[GP_SUBDIV] = SUB_OCT_TWELFTH;
            engine_render(pcm, RATE * 3);
            engine_render(pcm, RATE);
            double e[3];
            static const double fq[3] = {110.0, 55.0, 110.0 / 3.0};
            for (int k = 0; k < 3; k++) {
                double w = 2 * M_PI * fq[k] / RATE, c = 2 * cos(w), s1 = 0, s2 = 0;
                for (int i = 0; i < RATE; i++) {
                    double s0 = pcm[2 * i] + c * s1 - s2;
                    s2 = s1;
                    s1 = s0;
                }
                e[k] = sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / RATE;
            }
            printf("undertones %s SUB %2d: 110 Hz %.0f, 55 Hz %.0f, 36.7 Hz %.0f\n", eng ? "WIND   " : "STRINGS", sub, e[0], e[1], e[2]);
        }

    /* the projection: the spectrum behind the screens, swaying left and
     * right with a 6 Hz binaural beat; and what drawing it costs */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.fx[FX_BINBEAT] = 16; /* 6 Hz */
        song.fx[FX_BINDEPTH] = 100;
        song.fx[FX_BINLOCK] = 0;
        int16_t buf[2 * 480];
        for (int i = 0; i < 300; i++) {
            engine_render(buf, 480);
            ui_tick(10);
            if (i % 3 == 0)
                ui_draw(fb);
        }
        shot(out, "proj_left", fb);
        for (int i = 0; i < 8; i++) { /* about half a beat later */
            engine_render(buf, 480);
            ui_tick(10);
        }
        shot(out, "proj_right", fb);
        clock_t c0 = clock();
        for (int i = 0; i < 100; i++) {
            ui_tick(33);
            ui_draw(fb);
        }
        double ms = (double)(clock() - c0) / CLOCKS_PER_SEC * 10.0;
        ui_button(BTN_START, 1); /* START + Y twice: SOFT -> BRIGHT -> OFF */
        press(BTN_Y);
        press(BTN_Y);
        ui_button(BTN_START, 0);
        c0 = clock();
        for (int i = 0; i < 100; i++) {
            ui_tick(33);
            ui_draw(fb);
        }
        double ms_off = (double)(clock() - c0) / CLOCKS_PER_SEC * 10.0;
        ui_button(BTN_START, 1); /* and back to SOFT */
        press(BTN_Y);
        ui_button(BTN_START, 0);
        song.fx[FX_BINBEAT] = 0;
        printf("projection: a frame drawn in %.2f ms with it, %.2f ms without (host); binaural %.2f Hz, playing %d\n", ms,
               ms_off, live.bin_hz, live.playing);
    }
    /* presets: save YAMAN to slot 3, change, load it back, peek, clear */
    {
        char p3[512];
        Song t;
        snprintf(p3, sizeof p3, "%s/preset_03.jds", out);
        song_default();
        engine_set_scale(6);
        song_save(p3);
        engine_set_scale(11);
        int before = song.gp[GP_SCALE];
        int ok = song_load(p3);
        int peek = song_peek(p3, &t);
        printf("presets: saved YAMAN, changed to %s, loaded back -> %s (load %d, peek %d scale %d)\n", engine_scale_name(before),
               engine_scale_name(song.gp[GP_SCALE]), ok, peek, t.gp[GP_SCALE]);
    }
    /* mixed voices: one voice turned from a string into a reed while it
     * plays stays on its note; a v0.5 WIND song gives every voice its exciter */
    {
        solo(ENG_STRINGS, 0, 0, 0, 0);
        song.sp[2][SP_BOW] = 60;
        engine_render(pcm, RATE * 2);
        double fs = hz_yin(pcm, RATE, RATE / 4);
        song.sp[2][SP_VOICE] = 1 + EX_REED;
        engine_render(pcm, RATE * 3);
        double fw = hz_yin(pcm, RATE, RATE / 4), pk = 0;
        for (int i = 0; i < RATE * 3 * 2; i++)
            pk = fmax(pk, abs(pcm[i]) / 32768.0);
        song_default();
        for (int v = 0; v < NS; v++)
            song.on[v] = 1, song.sp[v][SP_VOICE] = v < 2 ? 0 : 1 + v; /* 2 strings, flute, free, recorder */
        engine_init(RATE);
        engine_set_playing(1);
        engine_render(pcm, RATE * 4);
        double rms = 0;
        for (int i = 0; i < RATE * 4 * 2; i++)
            rms += (double)pcm[i] * pcm[i];
        rms = sqrt(rms / (RATE * 8)) / 32768.0;
        struct {
            uint32_t magic;
            int on[NS], sp[NS][24], gp[27], lfo[NLFO][NLP], fx[NFX];
            int prog_n, prog_pos, prog_note[NPROG][NS], prog_fine[NPROG][NS];
            char prog_name[NPROG][24];
        } old;
        memset(&old, 0, sizeof old);
        old.magic = 0x4a44520bu; /* JDR11 (v0.5) */
        for (int v = 0; v < NS; v++)
            old.on[v] = 1, old.sp[v][SP_LEVEL] = 50, old.sp[v][SP_PSTEPS] = old.sp[v][SP_PPULSES] = 4;
        old.gp[GP_SCALE] = 1, old.gp[GP_ROOT] = 45, old.gp[GP_BPM] = 80;
        old.gp[GP_ENGINE] = ENG_WIND, old.gp[GP_EXCITE] = EX_DOUBLE, old.gp[GP_MATERIAL] = MAT_GLASS;
        char po[512];
        snprintf(po, sizeof po, "%s/old_wind.jds", out);
        FILE *f = fopen(po, "wb");
        fwrite(&old, sizeof old, 1, f);
        fclose(f);
        int ok = song_load(po);
        printf("mixed voices: string %.1f Hz -> reed %.1f Hz (peak %.2f); 2 strings + 3 winds rms %.3f; v0.5 WIND song load %d"
               " voices %s %s body %d (want GLASS %d)\n",
               fs, fw, pk, rms, ok, engine_voice_name(song.sp[0][SP_VOICE]), engine_voice_name(song.sp[4][SP_VOICE]),
               song.fx[FX_BTYPE], BODY_GLASS);
    }
    /* LATCH: silent until played; a pluck starts at once and keeps droning,
     * a bow swells in, played again it fades over its RELEASE (1.3 s) */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_PLAYMODE] = PLAY_LATCH;
        song.gp[GP_CHANGES] = 0;
        song.fx[FX_DMIX] = song.fx[FX_VSEND] = 0;
        song.fx[FX_SCOUNT] = 0;
        for (int v = 0; v < NS; v++)
            song.sp[v][SP_RELEASE] = 55;
        engine_render(pcm, RATE * 2);
        double quiet = rms_of(pcm, RATE, RATE);
        engine_latch(1, LATCH_PLUCK);
        engine_render(pcm, RATE / 20);
        double pk_start = rms_of(pcm, 0, RATE / 20);
        engine_render(pcm, RATE * 6);
        double held = rms_of(pcm, RATE * 5, RATE);
        engine_latch(1, LATCH_PLUCK); /* again: let go */
        engine_render(pcm, RATE * 2);
        double gone = rms_of(pcm, RATE + RATE / 2, RATE / 2);
        engine_latch(3, LATCH_BOW);
        engine_render(pcm, RATE * 3);
        double bow_early = rms_of(pcm, 0, RATE / 10), bow_full = rms_of(pcm, RATE * 2, RATE);
        engine_unlatch_all();
        printf("latch: silent %.0f, pluck at once %.0f, still droning after 6 s %.0f, 1.5 s after letting go %.0f; "
               "bow first 0.1 s %.0f, after 2 s %.0f\n",
               quiet, pk_start, held, gone, bow_early, bow_full);
        song.sp[2][SP_VOICE] = 1 + EX_REED; /* a wind voice latches the same way */
        engine_render(pcm, RATE);
        engine_latch(2, LATCH_PLUCK);
        engine_render(pcm, RATE * 4);
        double w_held = rms_of(pcm, RATE * 3, RATE);
        engine_latch(2, LATCH_BOW);
        engine_render(pcm, RATE * 2);
        double w_gone = rms_of(pcm, RATE + RATE / 2, RATE / 2);
        printf("latch checks: silent %d, starts %d, drones %d, fades %d, bow swells %d, wind drones %d and fades %d\n",
               quiet < 30, pk_start > 300, held > 300, gone < held * 0.1, bow_early < bow_full * 0.5, w_held > 300,
               w_gone < w_held * 0.1);
    }
    /* a v0.6 / v0.7 song (JDR13) loads: its voices kept, RELEASE and PLAY MODE new */
    {
        struct {
            uint32_t magic;
            int on[NS], sp[NS][25], gp[27], lfo[NLFO][NLP], fx[NFX];
            int prog_n, prog_pos, prog_note[NPROG][NS], prog_fine[NPROG][NS];
            char prog_name[NPROG][24];
        } o13;
        memset(&o13, 0, sizeof o13);
        o13.magic = 0x4a44520du;
        for (int v = 0; v < NS; v++)
            o13.on[v] = 1, o13.sp[v][SP_LEVEL] = 60, o13.sp[v][SP_PSTEPS] = o13.sp[v][SP_PPULSES] = 4,
            o13.sp[v][SP_VOICE] = v;
        o13.gp[GP_SCALE] = 15, o13.gp[GP_ROOT] = 45, o13.gp[GP_BPM] = 66;
        char p13[512];
        snprintf(p13, sizeof p13, "%s/v07.jds", out);
        FILE *f = fopen(p13, "wb");
        fwrite(&o13, sizeof o13, 1, f);
        fclose(f);
        int ok = song_load(p13);
        printf("save compat v0.7: load=%d scale=%s voice5=%s release=%d playmode=%d bpm=%d\n", ok,
               engine_scale_name(song.gp[GP_SCALE]), engine_voice_name(song.sp[4][SP_VOICE]), song.sp[4][SP_RELEASE],
               song.gp[GP_PLAYMODE], song.gp[GP_BPM]);
    }
    /* switching a voice while it sounds: the old voice fades out before the
     * new one starts, so the change makes no step bigger than playing does */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_CHANGES] = 0;
        song.fx[FX_DMIX] = song.fx[FX_VSEND] = song.fx[FX_SCOUNT] = song.fx[FX_BODY] = 0;
        for (int v = 0; v < NS; v++)
            song.on[v] = v == 2, song.sp[v][SP_PON] = song.sp[v][SP_BON] = 0;
        song.sp[2][SP_BOW] = 70;
        engine_render(pcm, RATE * 3);
        /* a cut leaves the moments after the change nearly silent; the fade
         * still holds most of the sound 5 ms later */
        engine_render(pcm, RATE / 2);
        double before = rms_of(pcm, RATE / 2 - 480, 480);
        song.sp[2][SP_VOICE] = 1 + EX_REED; /* string -> reed while it plays */
        engine_render(pcm, RATE / 4);
        double after = rms_of(pcm, 384, 240); /* 8 .. 13 ms: past the limiter's 6 ms look-ahead */
        printf("voice switch: %.0f%% of the sound still there 8 ms after the change (want over 75%%: a fade, not a "
               "cut)\n", 100.0 * after / before);
    }
    /* the LATCH screen: 1 plucked, 3 bowed, 4 fading, the rest silent */
    {
        song_default();
        engine_init(RATE);
        engine_set_playing(1);
        song.gp[GP_PLAYMODE] = PLAY_LATCH;
        song.gp[GP_CHANGES] = 0;
        song.sp[3][SP_RELEASE] = 85;
        engine_latch(0, LATCH_PLUCK);
        engine_latch(2, LATCH_BOW);
        engine_latch(3, LATCH_BOW);
        int16_t buf[2 * 480];
        for (int i = 0; i < 250; i++) {
            engine_render(buf, 480);
            ui_tick(10);
            if (i == 150)
                engine_latch(3, LATCH_BOW); /* let 4 go */
        }
        shot(out, "latch", fb);
        engine_unlatch_all();
        song.gp[GP_PLAYMODE] = PLAY_RINGS;
    }
    /* chord memory: record four chords, then play them back in order */
    {
        song_default();
        engine_prog_mode(PROG_RECORD);
        for (int i = 0; i < 3; i++)
            engine_next_chord();
        int n = song.prog_n, rec[4][NS];
        memcpy(rec, song.prog_note, sizeof rec);
        engine_prog_mode(PROG_PLAY);
        int match = 0;
        for (int i = 0; i < 8; i++) {
            engine_next_chord();
            match += !memcmp(song.sp[0], song.sp[0], 4) && song.sp[1][SP_NOTE] == rec[i % n][1] && song.sp[3][SP_NOTE] == rec[i % n][3];
        }
        printf("chord memory: %d chords kept [%s %s %s %s], 8 changes in PLAY matched the memory %d/8\n", n, song.prog_name[0],
               song.prog_name[1], song.prog_name[2], song.prog_name[3], match);
    }
    /* key lock and mode: EQUAL in D Phrygian, locked; chords stay in mode and in tune */
    {
        song_default();
        engine_set_scale(0);
        song.gp[GP_ROOT] = 50;
        song.gp[GP_MODALITY] = MODAL_PHRYGIAN;
        song.gp[GP_KEYLOCK] = 1;
        static const int phryg[7] = {0, 1, 3, 5, 7, 8, 10};
        int out_of_mode = 0, detuned = 0;
        for (int i = 0; i < 40; i++) {
            engine_next_chord();
            for (int s = 0; s < NS; s++) {
                int pc = ((song.sp[s][SP_NOTE] % 12) + 12) % 12, in = 0;
                for (int k = 0; k < 7; k++)
                    in |= pc == phryg[k];
                out_of_mode += !in;
                detuned += song.sp[s][SP_FINE] != 0;
            }
        }
        printf("key lock, D Phrygian: notes outside the mode %d, detuned notes %d (of 200)\n", out_of_mode, detuned);
    }
    /* drone release: RELEASE 0 vs 100 (10 s), level 1 s after START stops it */
    for (int rel = 0; rel <= 100; rel += 100) {
        song_default();
        song.gp[GP_RELEASE] = rel;
        song.fx[FX_DMIX] = song.fx[FX_VSEND] = 0;
        engine_init(RATE);
        engine_set_playing(1);
        engine_render(pcm, RATE * 3);
        double on = rms_of(pcm, RATE * 2, RATE);
        engine_set_playing(0);
        engine_render(pcm, RATE);
        printf("release %3d: rms playing %.0f, 0.9 s after stop %.0f\n", rel, on, rms_of(pcm, RATE * 9 / 10, RATE / 10));
    }

    /* DSP benchmark: 30 s of the default drone */
    song_default();
    engine_init(RATE);
    engine_set_playing(1);
    clock_t c0 = clock();
    for (int i = 0; i < 30 * RATE / 1024; i++)
        engine_render(pcm, 1024);
    double secs_cpu = (double)(clock() - c0) / CLOCKS_PER_SEC;
    printf("dsp: 30 s rendered in %.2f s host cpu (%.1f%%)\n", secs_cpu, secs_cpu / 30.0 * 100.0);
    song_default();
    for (int s = 0; s < NS; s++)
        song.sp[s][SP_VOICE] = 1 + EX_BANSURI;
    song.gp[GP_SUB] = 50;
    engine_init(RATE);
    engine_set_playing(1);
    c0 = clock();
    for (int i = 0; i < 30 * RATE / 1024; i++)
        engine_render(pcm, 1024);
    secs_cpu = (double)(clock() - c0) / CLOCKS_PER_SEC;
    printf("dsp WIND (bansuri, undertones): 30 s in %.2f s host cpu (%.1f%%)\n", secs_cpu, secs_cpu / 30.0 * 100.0);
    /* mixed screens: two strings, then a reed, a flute and a bansuri */
    song.sp[0][SP_VOICE] = song.sp[1][SP_VOICE] = 0;
    song.sp[2][SP_VOICE] = 1 + EX_REED;
    song.sp[3][SP_VOICE] = 1 + EX_FLUTE;
    for (int i = 0; i < 300; i++) {
        engine_render(pcm, RATE / 100);
        ui_tick(10);
    }
    shot(out, "wind_play", fb);
    next_page();
    shot(out, "wind_sound", fb);
    {
        /* SONG with a progression recorded and two presets */
        engine_prog_mode(PROG_RECORD);
        for (int i = 0; i < 5; i++)
            engine_next_chord();
        engine_prog_mode(PROG_PLAY);
        engine_next_chord();
        engine_next_chord();
        char pp[512];
        snprintf(pp, sizeof pp, "%s/preset_01.jds", out);
        song_save(pp);
        for (int i = 0; i < PAGE_COUNT - 2; i++)
            next_page(); /* SOUND -> SONG */
        ui_set_dir(out);
        shot(out, "song", fb);
        next_page(); /* back to PLAY */
        next_page();
    }
    for (int i = 0; i < PAGE_COUNT - 1; i++)
        next_page();
    snprintf(path, sizeof path, "%s/wind.wav", out);
    {
        song_default();
        for (int s = 0; s < NS; s++)
            song.sp[s][SP_VOICE] = 1 + EX_BANSURI;
        song.gp[GP_SUB] = 40;
        engine_init(RATE);
        engine_set_playing(1);
        engine_render(pcm, RATE * 12);
        write_wav(path, pcm, RATE * 12);
    }
    free(pcm);
    return 0;
}
