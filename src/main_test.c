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

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    char path[512];
    static uint32_t fb[SCREEN_W * SCREEN_H];
    song_default();
    engine_init(RATE);
    engine_set_playing(1);

    const int secs = 12, chunk = RATE / 100;
    int16_t *pcm = malloc(sizeof(int16_t) * 2 * RATE * secs);
    static const char *const pages[PAGE_COUNT] = {"play", "strings", "drone", "mod", "fx"};
    for (int t = 0, f = 0; t < secs * 100; t++, f += chunk) {
        engine_render(pcm + 2 * f, chunk);
        ui_tick(10);
        if (t == 300)
            press(BTN_DOWN), press(BTN_DOWN), press(BTN_X), press(BTN_X), press(BTN_X);
        if (t >= 320 && t < 320 + PAGE_COUNT * 10 && (t - 320) % 10 == 0) {
            shot(out, pages[(t - 320) / 10], fb);
            press(BTN_R1);
        }
        if (t == 420) { /* back on PLAY: the quiet view, then hand-bow string 2 */
            press(BTN_L2);
        }
        if (t == 470)
            shot(out, "quiet", fb);
        if (t == 480) {
            press(BTN_L2);
            press(BTN_RIGHT);
            ui_button(BTN_A, 1);
        }
        if (t == 560) {
            shot(out, "play_bow", fb);
            ui_button(BTN_A, 0);
        }
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
        printf("body %-8s: rms %.0f, zero crossings/s %d\n", b < 0 ? "OFF" : (const char *[]){"WOOD", "GOURD", "BRONZE", "HARMONIC"}[b],
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

    /* gate at 1/8, 50%: the level must chop between loud and near-silent */
    song_default();
    engine_init(RATE);
    engine_set_playing(1);
    song.fx[FX_GRATE] = 3;
    song.fx[FX_GDUTY] = 5;
    song.fx[FX_GSMOOTH] = 10;
    song.fx[FX_DMIX] = 0;
    song.fx[FX_VSEND] = 0;
    engine_render(pcm, RATE * 3);
    engine_render(pcm, RATE);
    {
        double lo = 1e9, hi = 0;
        int win = RATE / 100;
        for (int i = 0; i + win <= RATE; i += win) {
            double r = rms_of(pcm, i, win);
            if (r < lo)
                lo = r;
            if (r > hi)
                hi = r;
        }
        printf("gate 1/8 50%%: 10 ms rms min %.0f max %.0f\n", lo, hi);
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

    /* DSP benchmark: 30 s of the default drone */
    song_default();
    engine_init(RATE);
    engine_set_playing(1);
    clock_t c0 = clock();
    for (int i = 0; i < 30 * RATE / 1024; i++)
        engine_render(pcm, 1024);
    double secs_cpu = (double)(clock() - c0) / CLOCKS_PER_SEC;
    printf("dsp: 30 s rendered in %.2f s host cpu (%.1f%%)\n", secs_cpu, secs_cpu / 30.0 * 100.0);
    free(pcm);
    return 0;
}
