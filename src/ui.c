/* Jaffedrone UI. The look borrows from Norman Jaffe's houses: long sloping
 * rooflines, chamfered angular masses, cedar and sandstone against slate,
 * one copper edge catching the light. */
#include "ui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "gfx.h"
#include "sync.h"

#define C_BG 0x14161b
#define C_PANEL 0x1c1f27
#define C_PANEL2 0x282c37
#define C_LINE 0x3a3f4c
#define C_TEXT 0xf2ebdd
#define C_DIM 0x7d7f8a
#define C_COPPER 0xb3875c
#define C_CEDAR 0x7f6553
#define C_SAND 0xd9c3a0
#define C_SEA 0x7fd1a8
#define C_RED 0xff5a50
#define C_OFF 0x3a3f4c
#define HEADER_H 46
#define BODY_Y 58
#define FOOTER_Y 456

static void nop(void) {}
void (*ui_lock)(void) = nop;
void (*ui_unlock)(void) = nop;

static const uint32_t scol[NS] = {0xc9a67c, 0xad7f61, 0xd9c3a0, 0x9fb7a4, 0x7f9aa6};
static const char *const page_names[PAGE_COUNT] = {"PLAY", "STRINGS", "DRONE", "MOD", "FX"};
static int page, cur_s, cur_p, gcur, mcur_l, mcur_p, fcur_c, fcur_p;
static int ring;  /* PLAY: the selected ring of the selected string */
static int quiet; /* PLAY: the quiet view, strings and rings only */
static int l2_used, r2_used;
static int bowing = -1; /* the string bowed by hand (A held) */
static int held[BTN_COUNT], hold_ms[BTN_COUNT], rep_ms[BTN_COUNT], rep_n[BTN_COUNT];
static int quit, changed, now_ms, toast_ms;
static char toast[48];

static int wrap(int x, int n) { return ((x % n) + n) % n; }
static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(toast, sizeof toast, fmt, ap);
    va_end(ap);
    toast_ms = 1400;
}

void ui_init(void) {}
int ui_quit_requested(void) { return quit; }
int ui_take_changed(void)
{
    int c = changed;
    changed = 0;
    return c;
}

/* ------------------------------------------------------------- controls */

static int *cur_value(const ParamDef **d)
{
    switch (page) {
    case PAGE_STRINGS: *d = sparam_def(cur_p); return &song.sp[cur_s][cur_p];
    case PAGE_DRONE: *d = gparam_def(gcur); return &song.gp[gcur];
    case PAGE_MOD: *d = lparam_def(mcur_p); return &song.lfo[mcur_l][mcur_p];
    default: *d = fparam_def(fcur_c * 3 + fcur_p); return &song.fx[fcur_c * 3 + fcur_p];
    }
}

static void set_scale(int sc)
{
    engine_set_scale(wrap(sc, engine_nscales()));
    say("%s", engine_scale_name(song.gp[GP_SCALE]));
}

static void adjust(int dir, int repeats)
{
    const ParamDef *d;
    int *v = cur_value(&d);
    int step = (repeats > 12 && d->max - d->min > 30) ? 4 : 1;
    int nv = clampi(*v + dir * step, d->min, d->max);
    if (nv == *v)
        return;
    if (page == PAGE_DRONE && gcur == GP_SCALE) {
        set_scale(nv);
        return;
    }
    *v = nv;
    if (page == PAGE_DRONE && gcur == GP_BPM)
        engine_set_bpm((float)nv);
    if (page == PAGE_DRONE && gcur == GP_SYNC)
        engine_set_rate(1.0);
    if (page == PAGE_DRONE && gcur == GP_MODE)
        say("%s", nv == MODE_MONO ? "MONO: ALL STRINGS FOLLOW STRING 1" : "CHORD: EACH STRING ITS OWN NOTE");
}

/* PLAY: change one field of the selected ring and show the pattern */
static void ring_show(void)
{
    const int *P = song.sp[cur_s];
    static const char *const rn[6] = {"1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS"};
    if (!P[RING_P(ring, 0)])
        say("%d %s OFF%s", cur_s + 1, ring ? "BOW" : "PLUCK", ring ? " (BOW HOLDS)" : "");
    else
        say("%d %s E(%d,%d) >%d %s", cur_s + 1, ring ? "BOW" : "PLUCK", P[RING_P(ring, 2)], P[RING_P(ring, 1)],
            P[RING_P(ring, 3)], rn[P[SP_RATE]]);
}

static void ring_edit(int field, int dir)
{
    int *P = song.sp[cur_s];
    if (field == SP_RATE)
        P[SP_RATE] = clampi(P[SP_RATE] + dir, 0, 5);
    else {
        int id = RING_P(ring, field);
        const ParamDef *d = sparam_def(id);
        if (field == 3)
            P[id] = wrap(P[id] + dir, P[RING_P(ring, 1)]);
        else
            P[id] = clampi(P[id] + dir, d->min, field == 2 ? P[RING_P(ring, 1)] : d->max);
        if (field == 1 && P[RING_P(ring, 2)] > P[id])
            P[RING_P(ring, 2)] = P[id];
        P[RING_P(ring, 0)] = 1; /* editing a ring switches it on */
    }
    ring_show();
}

static void play_action(int b)
{
    int l2 = held[BTN_L2], r2 = held[BTN_R2];
    if (l2) {
        l2_used = 1;
        switch (b) {
        case BTN_X: ring_edit(1, +1); return;
        case BTN_Y: ring_edit(1, -1); return;
        case BTN_LEFT: ring_edit(3, -1); return;
        case BTN_RIGHT: ring_edit(3, +1); return;
        case BTN_UP: ring_edit(SP_RATE, +1); return;
        case BTN_DOWN: ring_edit(SP_RATE, -1); return;
        case BTN_A: /* mute or unmute the string (it fades) */
            song.on[cur_s] = !song.on[cur_s];
            say("STRING %d %s", cur_s + 1, song.on[cur_s] ? "ON" : "MUTED");
            return;
        case BTN_B:
            engine_euclid_random(cur_s, ring);
            ring_show();
            return;
        }
    }
    if (r2) {
        r2_used = 1;
        switch (b) {
        case BTN_LEFT: set_scale(song.gp[GP_SCALE] - 1); return;
        case BTN_RIGHT: set_scale(song.gp[GP_SCALE] + 1); return;
        case BTN_UP: song.gp[GP_CUTOFF] = clampi(song.gp[GP_CUTOFF] + 2, 0, 100); say("TONE %d", song.gp[GP_CUTOFF]); return;
        case BTN_DOWN: song.gp[GP_CUTOFF] = clampi(song.gp[GP_CUTOFF] - 2, 0, 100); say("TONE %d", song.gp[GP_CUTOFF]); return;
        case BTN_X: song.fx[FX_SYMP] = clampi(song.fx[FX_SYMP] + 2, 0, 100); say("COUPLE %d", song.fx[FX_SYMP]); return;
        case BTN_Y: song.fx[FX_SYMP] = clampi(song.fx[FX_SYMP] - 2, 0, 100); say("COUPLE %d", song.fx[FX_SYMP]); return;
        case BTN_A: song.fx[FX_BODY] = clampi(song.fx[FX_BODY] + 5, 0, 100); say("BODY %d", song.fx[FX_BODY]); return;
        case BTN_B: song.fx[FX_BODY] = clampi(song.fx[FX_BODY] - 5, 0, 100); say("BODY %d", song.fx[FX_BODY]); return;
        }
    }
    switch (b) {
    case BTN_LEFT: cur_s = wrap(cur_s - 1, NS); break;
    case BTN_RIGHT: cur_s = wrap(cur_s + 1, NS); break;
    case BTN_UP:   /* up: the pluck ring; up again switches it on or off */
    case BTN_DOWN: /* down: the bow ring; down again switches it on or off */
    {
        int want = b == BTN_UP ? RING_PLUCK : RING_BOW;
        if (ring == want) {
            int *on = &song.sp[cur_s][RING_P(ring, 0)];
            *on = !*on;
        }
        ring = want;
        ring_show();
        break;
    }
    case BTN_X: ring_edit(2, +1); break;
    case BTN_Y: ring_edit(2, -1); break;
    case BTN_A: /* bow by hand while held */
        bowing = cur_s;
        engine_bow(cur_s, 1);
        break;
    case BTN_B: engine_pluck(cur_s); break;
    }
}

static void action(int b, int repeats)
{
    switch (b) {
    case BTN_L1: page = wrap(page - 1, PAGE_COUNT); return;
    case BTN_R1: page = wrap(page + 1, PAGE_COUNT); return;
    case BTN_START:
        engine_set_playing(!live.playing);
        say("%s", live.playing ? "DRONE ON" : "DRONE OFF");
        return;
    case BTN_SELECT:
        engine_next_chord();
        changed = 1;
        if (!quiet)
            say("%s  %s", engine_scale_name(song.gp[GP_SCALE]), engine_chord_name());
        return;
    case BTN_MENU: quit = 1; return;
    case BTN_L2:
    case BTN_R2: return;
    }
    changed = 1;
    if (page == PAGE_PLAY) {
        play_action(b);
        return;
    }
    switch (b) {
    case BTN_Y: adjust(-1, repeats); return;
    case BTN_X: adjust(+1, repeats); return;
    case BTN_B:
        if (page == PAGE_STRINGS)
            engine_pluck(cur_s);
        else
            for (int s = 0; s < NS; s++)
                engine_pluck(s);
        return;
    }
    int dir = b == BTN_DOWN ? 1 : b == BTN_UP ? -1 : 0;
    int side = b == BTN_RIGHT ? 1 : b == BTN_LEFT ? -1 : 0;
    switch (page) {
    case PAGE_STRINGS:
        cur_s = wrap(cur_s + side, NS);
        cur_p = wrap(cur_p + dir, NSOUND);
        if (b == BTN_A) {
            song.on[cur_s] = !song.on[cur_s];
            if (song.on[cur_s])
                engine_pluck(cur_s);
            say("STRING %d %s", cur_s + 1, song.on[cur_s] ? "ON" : "OFF");
        }
        break;
    case PAGE_DRONE: gcur = wrap(gcur + dir, NGP); break;
    case PAGE_MOD:
        mcur_l = wrap(mcur_l + side, NLFO);
        mcur_p = wrap(mcur_p + dir, NLP);
        break;
    default:
        fcur_c = wrap(fcur_c + side, 4);
        fcur_p = wrap(fcur_p + dir, 3);
        break;
    }
}

static int repeatable(int b)
{
    if (page == PAGE_PLAY && !held[BTN_L2] && !held[BTN_R2] && (b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT))
        return 0;
    return b <= BTN_RIGHT || b == BTN_X || b == BTN_Y;
}

void ui_button(int b, int down)
{
    if (b < 0 || b >= BTN_COUNT || held[b] == down)
        return;
    held[b] = down;
    hold_ms[b] = rep_ms[b] = rep_n[b] = 0;
    if (b == BTN_L2 && down)
        l2_used = 0;
    if (b == BTN_R2 && down)
        r2_used = 0;
    /* a tap of L2 on PLAY (no combination) toggles the quiet view */
    if (b == BTN_L2 && !down && !l2_used && page == PAGE_PLAY) {
        quiet = !quiet;
        return;
    }
    if (b == BTN_A && !down && bowing >= 0) {
        ui_lock();
        engine_bow(bowing, 0);
        ui_unlock();
        bowing = -1;
        return;
    }
    if (down) {
        ui_lock();
        action(b, 0);
        ui_unlock();
    }
}

void ui_tick(int ms)
{
    now_ms += ms;
    if (toast_ms > 0)
        toast_ms -= ms;
    for (int b = 0; b < BTN_COUNT; b++) {
        if (!held[b] || !repeatable(b))
            continue;
        hold_ms[b] += ms;
        if (hold_ms[b] < 300)
            continue;
        rep_ms[b] += ms;
        int interval = hold_ms[b] > 1500 ? 35 : 75;
        while (rep_ms[b] >= interval) {
            rep_ms[b] -= interval;
            ui_lock();
            action(b, ++rep_n[b]);
            ui_unlock();
        }
    }
}

/* -------------------------------------------------------- shape helpers */

/* a mass whose top edge slopes: left corner at y, right corner at y + drop */
static void roof(int x, int y, int w, int h, int drop, uint32_t c)
{
    int top = drop > 0 ? y + drop : y, bot = y + h;
    gfx_fill(x, top, w, bot - top, c);
    if (drop > 0)
        gfx_tri(x, y, x + w, y + drop, x, y + drop, c);
    else if (drop < 0)
        gfx_tri(x, y, x + w, y + drop, x + w, y, c);
}

/* a block with its top-right corner cut off */
static void chamfer(int x, int y, int w, int h, int cut, uint32_t c)
{
    gfx_fill(x, y + cut, w, h - cut, c);
    gfx_fill(x, y, w - cut, cut, c);
    gfx_tri(x + w - cut, y, x + w, y + cut, x + w - cut, y + cut, c);
}

/* a parallelogram whose top is shifted right by skew */
static void para(int x, int y, int w, int h, int skew, uint32_t c)
{
    gfx_fill(x + skew, y, w - skew, h, c);
    gfx_tri(x, y + h, x + skew, y, x + skew, y + h, c);
    gfx_tri(x + w, y, x + w + skew, y, x + w, y + h, c);
}

/* a thin diagonal line as two triangles */
static void slant(int x0, int y0, int x1, int y1, int t, uint32_t c)
{
    gfx_tri(x0, y0, x1, y1, x1, y1 + t, c);
    gfx_tri(x0, y0, x0, y0 + t, x1, y1 + t, c);
}

/* a straight line of thickness t at any angle, as a quad */
static void line(float x0, float y0, float x1, float y1, float t, uint32_t c)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.5f)
        return;
    float nx = -dy / l * t * 0.5f, ny = dx / l * t * 0.5f;
    gfx_tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    gfx_tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

/* a diamond (a square on its corner) */
static void diamond(float x, float y, float r, uint32_t c)
{
    gfx_tri(x, y - r, x + r, y, x, y + r, c);
    gfx_tri(x, y - r, x - r, y, x, y + r, c);
}

/* a parameter row: name left, value right, bar along the bottom */
static void param_row(int x, int y, int w, int h, int sel, int on, const char *name, const char *val,
                      float t, int bipolar, uint32_t col)
{
    chamfer(x, y, w, h, 7, sel ? gfx_mix(C_PANEL2, col, 0.18f) : C_PANEL);
    if (sel)
        gfx_fill(x, y, 3, h, col);
    gfx_text(x + 9, y + 2, name, sel ? col : C_DIM, FONT_SMALL);
    gfx_text(x + w - 9 - gfx_text_w(val, FONT_SMALL), y + 2, val, on ? C_TEXT : C_DIM, FONT_SMALL);
    int bx = x + 9, bw = w - 18, by = y + h - 8;
    gfx_fill(bx, by, bw, 4, C_LINE);
    if (bipolar) {
        int mid = bx + bw / 2, end = bx + (int)(bw * t);
        gfx_fill(end < mid ? end : mid, by, abs(end - mid) + 1, 4, on ? col : C_DIM);
    }
    else
        gfx_fill(bx, by, (int)(bw * t), 4, on ? col : C_DIM);
}

/* a vibrating string: a standing wave whose amplitude follows the meter */
static void draw_string_big(int x0, int x1, int y, float amp, float ph, uint32_t col, int on, float big)
{
    int w = x1 - x0;
    float a = fminf(1.f, sqrtf(amp * 6.f)) * 11.f * big;
    int py = y;
    for (int i = 0; i <= w; i++) {
        float t = (float)i / w;
        float s = sinf(t * 3.14159265f) * sinf(t * 6.2831853f * 2.f + ph) * a;
        int yy = y + (int)s;
        if (i > 0) {
            int lo = yy < py ? yy : py, hi = yy < py ? py : yy;
            gfx_fill(x0 + i, lo, 1, hi - lo + 2, on ? col : C_OFF);
        }
        py = yy;
    }
    /* end posts */
    gfx_fill(x0 - 2, y - 7, 3, 15, on ? col : C_OFF);
    gfx_fill(x1, y - 7, 3, 15, on ? col : C_OFF);
}

static void draw_string(int x0, int x1, int y, float amp, float ph, uint32_t col, int on)
{
    draw_string_big(x0, x1, y, amp, ph, col, on, 1.f);
}

/* ---------------------------------------------------------------- pages */

static void draw_header(void)
{
    gfx_fill(0, 0, SCREEN_W, HEADER_H, C_PANEL);
    /* the roofline: one copper edge across the band */
    slant(150, HEADER_H - 3, SCREEN_W, HEADER_H - 16, 3, C_COPPER);
    gfx_text(10, 4, "JAFFEDRONE", C_SAND, FONT_LARGE);
    int x = 172;
    for (int p = 0; p < PAGE_COUNT; p++) {
        int w = gfx_text_w(page_names[p], FONT_SMALL) + 12;
        para(x, 10, w, 24, 6, p == page ? C_COPPER : C_PANEL2);
        gfx_text(x + 9, 11, page_names[p], p == page ? 0x000000 : C_DIM, FONT_SMALL);
        x += w + 6;
    }
    char s[24], nm[8];
    note_name(song.gp[GP_ROOT], nm, sizeof nm);
    snprintf(s, sizeof s, "%s %d", nm, song.gp[GP_BPM]);
    int bw = gfx_text_w(s, FONT_LARGE);
    gfx_text(SCREEN_W - 10 - bw, 4, s, C_SAND, FONT_LARGE);
    int ix = SCREEN_W - 10 - bw - 30;
    if (live.playing) {
        float ph = 1.f - fmodf(now_ms / 1000.f * engine_bpm() / 60.f, 1.f);
        gfx_disc(ix + 8, 24, 5.f + 4.f * ph * ph, C_SEA);
    }
    else
        gfx_ring(ix + 8, 24, 7.f, 2.f, C_DIM);
    if (song.gp[GP_SYNC] != SYNC_OFF) {
        const SyncStatus *ss = sync_status();
        int linked = ss->leading || ss->have_leader;
        const char *t = ss->leading ? "LEAD" : ss->have_leader ? "FOLLOW" : "SYNC";
        int w = gfx_text_w(t, FONT_SMALL) + 16;
        para(ix - w - 14, 12, w, 22, 6, linked ? C_SEA : C_PANEL2);
        gfx_text(ix - w - 14 + 11, 13, t, linked ? 0x000000 : C_DIM, FONT_SMALL);
    }
}

static void draw_footer(void)
{
    static const char *const hints[PAGE_COUNT] = {
        "A:BOW B:PLUCK ^v:RING ON/OFF X/Y:PULSES L2:EDIT R2:SOUND SEL:CHORD",
        "<>:STRING ^v:PARAM Y:- X:+ A:ON/OFF B:PLUCK SEL:CHORD START:DRONE",
        "^v:PARAM Y:- X:+ B:PLUCK ALL SEL:NEW CHORD START:DRONE",
        "<>:LFO ^v:PARAM Y:- X:+ B:PLUCK ALL START:DRONE",
        "<>:SECTION ^v:PARAM Y:- X:+ B:PLUCK ALL START:DRONE",
    };
    const char *h = hints[page];
    if (page == PAGE_PLAY && held[BTN_L2])
        h = "L2+ A:MUTE STRING X/Y:STEPS <>:ROTATE ^v:RATE B:NEW RING";
    else if (page == PAGE_PLAY && held[BTN_R2])
        h = "R2+ <>:SCALE ^v:TONE X/Y:COUPLE A/B:BODY";
    gfx_fill(0, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, C_PANEL);
    gfx_fill(0, FOOTER_Y, 4, SCREEN_H - FOOTER_Y, C_COPPER);
    gfx_text(12, FOOTER_Y + 2, h, C_DIM, FONT_SMALL);
}

static void draw_toast(void)
{
    if (toast_ms <= 0)
        return;
    int w = gfx_text_w(toast, FONT_SMALL) + 36, h = 30;
    int x = (SCREEN_W - w) / 2, y = FOOTER_Y - h - 10;
    chamfer(x, y, w, h, 10, C_PANEL2);
    gfx_fill(x, y, 3, h, C_COPPER);
    gfx_text(x + 16, y + 6, toast, C_TEXT, FONT_SMALL);
}

static void draw_strings(void)
{
    const int cw = SCREEN_W / NS;
    const int hh = 72, y0 = BODY_Y + hh + 4, rh = (FOOTER_Y - 4 - y0) / NSOUND;
    for (int s = 0; s < NS; s++) {
        int x = s * cw + 3, w = cw - 6;
        int on = song.on[s], sel = s == cur_s;
        uint32_t col = scol[s];
        /* one long roof falls across the five columns */
        int drop = 6, top = BODY_Y + s * drop;
        roof(x, top, w, hh - s * drop, drop, sel ? gfx_mix(C_PANEL2, col, 0.14f) : C_PANEL);
        if (sel)
            gfx_fill(x, BODY_Y + hh - 3, w, 3, col);
        char nm[24], s1[16];
        int note = song.gp[GP_MODE] == MODE_MONO ? song.sp[0][SP_NOTE] : song.sp[s][SP_NOTE];
        engine_degree_name(note, s1, sizeof s1);
        snprintf(nm, sizeof nm, "%d %s", s + 1, s1);
        gfx_text(x + 8, top + drop + 2, nm, on ? col : C_DIM, FONT_SMALL);
        const char *ot = on ? "ON" : "OFF";
        gfx_text(x + w - 8 - gfx_text_w(ot, FONT_SMALL), top + drop + 2, ot, on ? C_SEA : C_DIM, FONT_SMALL);
        draw_string(x + 12, x + w - 12, BODY_Y + 56, on ? live.meter[s] : 0.f, live.phase[s], col, on);

        for (int p = 0; p < NSOUND; p++) {
            const ParamDef *d = sparam_def(p);
            char v[20];
            sparam_format(s, p, v, sizeof v);
            float t = (float)(song.sp[s][p] - d->min) / (d->max - d->min);
            int dimmed = song.gp[GP_MODE] == MODE_MONO && s > 0 && p == SP_NOTE;
            param_row(x, y0 + p * rh, w, rh - 3, sel && p == cur_p, on && !dimmed, d->name, v, t, d->min < 0, col);
        }
    }
}

static void draw_drone(void)
{
    /* the house: a long low roof over the five strings, chimney meter */
    int lx = 8, lw = 300, ly = BODY_Y, lh = FOOTER_Y - 6 - BODY_Y;
    roof(lx, ly, lw, lh, 54, C_PANEL);
    gfx_fill(lx + lw - 30, ly + 24, 10, 36, C_PANEL2); /* chimney */
    slant(lx, ly, lx + lw, ly + 54, 3, C_COPPER);
    gfx_text(lx + 12, ly + 64, engine_scale_name(song.gp[GP_SCALE]), C_DIM, FONT_SMALL);
    for (int s = 0; s < NS; s++) {
        int y = ly + 116 + s * 60;
        char nm[16];
        int note = song.gp[GP_MODE] == MODE_MONO ? song.sp[0][SP_NOTE] : song.sp[s][SP_NOTE];
        engine_degree_name(note, nm, sizeof nm);
        gfx_text(lx + 14, y - 26, nm, song.on[s] ? scol[s] : C_DIM, FONT_SMALL);
        draw_string(lx + 16, lx + lw - 50, y, song.on[s] ? live.meter[s] : 0.f, live.phase[s], scol[s], song.on[s]);
    }
    int mx = lx + lw - 24, mt = ly + 100, mhh = lh - 120, mh = (int)(mhh * fminf(1.f, live.master_meter));
    gfx_fill(mx, mt, 8, mhh, C_PANEL2);
    gfx_fill(mx, mt + mhh - mh, 8, mh, live.master_meter > 0.95f ? C_RED : C_SEA);

    int x = 320, w = SCREEN_W - 8 - x, rh = (FOOTER_Y - 6 - BODY_Y) / NGP;
    for (int p = 0; p < NGP; p++) {
        const ParamDef *d = gparam_def(p);
        char v[20];
        gparam_format(p, v, sizeof v);
        float t = (float)(song.gp[p] - d->min) / (d->max - d->min);
        param_row(x, BODY_Y + p * rh, w, rh - 4, p == gcur, 1, d->name, v, t, 0, C_COPPER);
    }
}

static void draw_mod(void)
{
    const int cw = SCREEN_W / NLFO, hh = 120, y0 = BODY_Y + hh + 6, rh = (FOOTER_Y - 6 - y0) / NLP;
    for (int l = 0; l < NLFO; l++) {
        int x = l * cw + 8, w = cw - 16, sel = l == mcur_l;
        uint32_t col = l ? C_SAND : C_COPPER;
        roof(x, BODY_Y, w, hh, l ? -12 : 12, sel ? gfx_mix(C_PANEL2, col, 0.12f) : C_PANEL);
        char t[12];
        snprintf(t, sizeof t, "LFO %d", l + 1);
        gfx_text(x + 10, BODY_Y + 16, t, sel ? col : C_DIM, FONT_SMALL);
        /* the shape over one cycle, and where we are in it */
        int gx = x + 14, gw = w - 28, gy = BODY_Y + 44, gh = 60;
        gfx_fill(gx, gy + gh, gw, 1, C_LINE);
        int shape = song.lfo[l][LP_SHAPE];
        float depth = song.lfo[l][LP_DEPTH] / 100.f;
        for (int i = 0; i < gw; i++) {
            float ph = (float)i / gw, v;
            switch (shape) {
            case SH_SINE: v = 0.5f - 0.5f * cosf(ph * 6.2831853f); break;
            case SH_TRI: v = ph < 0.5f ? ph * 2.f : 2.f - ph * 2.f; break;
            case SH_SAW: v = 1.f - ph; break;
            case SH_SQUARE: v = ph < 0.5f ? 1.f : 0.f; break;
            default: v = 0.3f + 0.4f * ((i * 7919 / (gw / 6 + 1)) % 5) / 4.f; break;
            }
            int h = (int)(v * gh * (0.15f + 0.85f * depth));
            gfx_fill(gx + i, gy + gh - h, 1, h + 1, depth > 0.f ? gfx_mix(C_PANEL2, col, 0.6f) : C_LINE);
        }
        float cur = live.lfo_val[l];
        int cx = gx + (int)(gw * fmodf(now_ms * 0.001f, 1.f));
        (void)cx;
        gfx_fill(gx + gw + 4, gy + gh - (int)(cur * gh), 6, (int)(cur * gh) + 1, col);
        for (int p = 0; p < NLP; p++) {
            const ParamDef *d = lparam_def(p);
            char v[20];
            lparam_format(l, p, v, sizeof v);
            float t = (float)(song.lfo[l][p] - d->min) / (d->max - d->min);
            param_row(x, y0 + p * rh, w, rh - 4, sel && p == mcur_p, 1, d->name, v, t, 0, col);
        }
    }
}

static void draw_fx(void)
{
    static const char *const names[4] = {"DELAY", "REVERB", "GATE", "BODY"};
    const int cw = SCREEN_W / 4, hh = 110, y0 = BODY_Y + hh + 6, rh = (FOOTER_Y - 6 - y0) / 3;
    for (int c = 0; c < 4; c++) {
        int x = c * cw + 5, w = cw - 10, sel = c == fcur_c;
        uint32_t col = c == 0 ? C_COPPER : c == 1 ? C_SAND : c == 2 ? C_SEA : C_CEDAR + 0x202020;
        roof(x, BODY_Y + c * 8, w, hh - c * 8, 8, sel ? gfx_mix(C_PANEL2, col, 0.12f) : C_PANEL);
        gfx_text(x + 10, BODY_Y + c * 8 + 14, names[c], sel ? col : C_DIM, FONT_SMALL);
        int gx = x + 14, gy = BODY_Y + 48, gw = w - 28, gh = 50;
        if (c == 0) {
            /* echoes dying away */
            float fb = song.fx[FX_DFB] / 100.f, a = 1.f;
            for (int i = 0; i < 6; i++) {
                int h = (int)(gh * a * (0.3f + 0.7f * song.fx[FX_DMIX] / 100.f));
                gfx_fill(gx + i * (gw / 6), gy + gh - h, gw / 6 - 6, h, i ? gfx_mix(C_PANEL2, col, a) : col);
                a *= 0.35f + 0.6f * fb;
            }
        }
        else if (c == 1) {
            /* a wedge of decay */
            int len = (int)(gw * (0.3f + 0.7f * song.fx[FX_VTIME] / 100.f));
            gfx_tri(gx, gy, gx + len, gy + gh, gx, gy + gh, gfx_mix(C_PANEL2, col, 0.3f + 0.6f * song.fx[FX_VSEND] / 100.f));
        }
        else if (c == 3) {
            /* five posts on a bridge: the strings, joined as strongly as they couple */
            float cp = song.fx[FX_SYMP] / 100.f, bd = song.fx[FX_BODY] / 100.f;
            int px[NS];
            for (int s = 0; s < NS; s++) {
                px[s] = gx + 4 + s * (gw - 8) / (NS - 1);
                int h = 8 + (int)(live.meter[s] * 30.f);
                gfx_fill(px[s] - 2, gy + gh - 12 - h, 5, h, song.on[s] ? scol[s] : C_OFF);
            }
            if (cp > 0.f)
                for (int s = 0; s + 1 < NS; s++)
                    slant(px[s], gy + gh - 12, px[s + 1], gy + gh - 12, 1 + (int)(cp * 4.f), gfx_mix(C_PANEL2, col, 0.4f + 0.6f * cp));
            /* the body under them, deeper with more BODY */
            roof(gx, gy + gh - 8, gw, 8 + (int)(bd * 10.f), 4, gfx_mix(C_PANEL2, col, 0.25f + 0.6f * bd));
        }
        else {
            /* the gate pattern over a beat, live gain on the right */
            int on = song.fx[FX_GRATE] > 0, duty = song.fx[FX_GDUTY];
            int slots = 4;
            for (int i = 0; i < slots; i++) {
                int sw = gw / slots, ow = sw * duty / 10;
                gfx_fill(gx + i * sw, gy + 10, sw - 4, gh - 10, C_PANEL2);
                gfx_fill(gx + i * sw, gy + 10, on ? ow : sw - 4, gh - 10, on ? gfx_mix(C_PANEL2, col, 0.7f) : C_LINE);
            }
            int lh = (int)((gh - 10) * live.gate);
            gfx_fill(gx + gw + 2, gy + gh - lh, 6, lh + 1, col);
        }
        for (int p = 0; p < 3; p++) {
            int idx = c * 3 + p;
            const ParamDef *d = fparam_def(idx);
            char v[20];
            fparam_format(idx, v, sizeof v);
            float t = (float)(song.fx[idx] - d->min) / (d->max - d->min);
            param_row(x, y0 + p * rh, w, rh - 4, sel && p == fcur_p, 1, d->name, v, t, 0, col);
        }
    }
}

/* a Euclidean ring as geometry: the steps are the corners of a polygon, the
 * pulses are joined into a second polygon inside it, which lights on a hit.
 * glow (0..1) fills it: the flash for plucks, the bow's swell for bows. */
static void draw_ring(float cx, float cy, float r, const int *P, int rg, int cur, float glow, uint32_t col, int sel,
                      int dim)
{
    int on = P[RING_P(rg, 0)] && !dim, steps = P[RING_P(rg, 1)], pulses = P[RING_P(rg, 2)], rot = P[RING_P(rg, 3)];
    uint32_t edge = on ? gfx_mix(C_PANEL2, col, 0.45f) : C_LINE;
    float vx[16], vy[16];
    for (int i = 0; i < steps; i++) {
        float a = -1.5707963f + 6.2831853f * i / steps;
        vx[i] = cx + r * cosf(a);
        vy[i] = cy + r * sinf(a);
    }
    if (sel) /* a square frame, chamfered, around the selected ring */
        gfx_rect((int)(cx - r - 10), (int)(cy - r - 10), (int)(2 * r + 20), (int)(2 * r + 20), 1, col);
    /* the pulse polygon, filled as a fan */
    int px[16], np = 0;
    for (int i = 0; i < steps; i++)
        if (euclid_hit(steps, pulses, rot, i))
            px[np++] = i;
    uint32_t fill = on ? gfx_mix(C_BG, col, 0.12f + 0.55f * glow) : gfx_mix(C_BG, C_LINE, 0.5f);
    if (np >= 3)
        for (int k = 1; k + 1 < np; k++)
            gfx_tri(vx[px[0]], vy[px[0]], vx[px[k]], vy[px[k]], vx[px[k + 1]], vy[px[k + 1]], fill);
    for (int k = 0; np >= 2 && k < np; k++) {
        int a = px[k], b = px[(k + 1) % np];
        if (np == 2 && k == 1)
            break;
        line(vx[a], vy[a], vx[b], vy[b], 2.f, on ? gfx_mix(fill, col, 0.6f) : C_LINE);
    }
    if (np == 1 && on)
        line(cx, cy, vx[px[0]], vy[px[0]], 2.f, gfx_mix(fill, col, 0.6f));
    /* the outer polygon */
    if (steps >= 2)
        for (int i = 0; i < steps; i++) {
            int j = (i + 1) % steps;
            if (steps == 2 && i == 1)
                break;
            line(vx[i], vy[i], vx[j], vy[j], 1.f, edge);
        }
    /* corners: pulses solid, rests small, the current step ringed */
    for (int i = 0; i < steps; i++) {
        int hit = euclid_hit(steps, pulses, rot, i);
        if (hit)
            diamond(vx[i], vy[i], 5.f, on ? col : C_DIM);
        else
            diamond(vx[i], vy[i], 2.5f, edge);
        if (on && i == cur && live.playing) {
            diamond(vx[i], vy[i], 8.f, C_TEXT);
            diamond(vx[i], vy[i], 6.f, hit ? col : C_BG);
        }
    }
}

static void draw_play(void)
{
    const int cw = SCREEN_W / NS;
    /* the chord band: one long roof, the scale, the chord, and the bars to
     * the next change as a row of tilted blocks */
    roof(0, BODY_Y - 6, SCREEN_W, 44, -10, C_PANEL);
    slant(0, BODY_Y + 38, SCREEN_W, BODY_Y + 28, 2, C_COPPER);
    gfx_text(12, BODY_Y, engine_scale_name(song.gp[GP_SCALE]), C_DIM, FONT_SMALL);
    const char *cn = engine_chord_name();
    if (cn[0])
        gfx_text((SCREEN_W - gfx_text_w(cn, FONT_LARGE)) / 2, BODY_Y - 8, cn, C_SAND, FONT_LARGE);
    static const int bars_of[6] = {0, 1, 2, 4, 8, 16};
    int bars = bars_of[song.gp[GP_CHANGES]];
    if (bars) {
        int done = (int)((live.beat / 4) % (uint32_t)bars), bw = bars > 8 ? 7 : 12;
        for (int i = 0; i < bars; i++) {
            int x = SCREEN_W - 14 - (bars - i) * (bw + 3);
            para(x, BODY_Y + 2, bw, 14, 3, i <= done ? C_COPPER : C_PANEL2);
        }
    }

    int top = BODY_Y + 46;
    for (int s = 0; s < NS; s++) {
        int x = s * cw + 3, w = cw - 6, sel = s == cur_s, on = song.on[s];
        uint32_t col = scol[s];
        const int *P = song.sp[s];
        int drop = 5, t = top + s * drop;
        roof(x, t, w, FOOTER_Y - 6 - t, drop, sel ? gfx_mix(C_BG, col, 0.10f) : C_PANEL);
        if (sel)
            gfx_fill(x, FOOTER_Y - 9, w, 3, col);
        char nm[24], dn[16];
        int note = song.gp[GP_MODE] == MODE_MONO ? song.sp[0][SP_NOTE] : P[SP_NOTE];
        engine_degree_name(note, dn, sizeof dn);
        snprintf(nm, sizeof nm, "%d %s", s + 1, dn);
        gfx_text(x + 8, t + drop + 2, nm, on ? col : C_DIM, FONT_SMALL);
        if (!on) /* muted: a red bar across the header */
            gfx_fill(x + w - 30, t + drop + 10, 20, 4, C_RED);
        /* the string itself, brighter while bowed by hand */
        int sy = t + drop + 42;
        draw_string(x + 12, x + w - 12, sy, on ? live.meter[s] : 0.f, live.phase[s],
                    bowing == s ? gfx_mix(col, C_TEXT, 0.5f) : col, on);
        float r = 38.f, cx = x + w / 2.f;
        float pcy = sy + 20 + r + 12, bcy = pcy + 2 * r + 34;
        draw_ring(cx, pcy, r, P, RING_PLUCK, live.step[s][RING_PLUCK], live.flash[s][RING_PLUCK], col,
                  sel && ring == RING_PLUCK, !on);
        draw_ring(cx, bcy, r, P, RING_BOW, live.step[s][RING_BOW], live.bow_env[s], col, sel && ring == RING_BOW, !on);
        /* when the bow ring is off the bow holds: a steady bar across the ring */
        if (!P[SP_BON] && on && P[SP_BOW] > 0)
            gfx_fill((int)(cx - r * 0.6f), (int)bcy - 1, (int)(r * 1.2f), 3, gfx_mix(C_BG, col, 0.5f));
        gfx_text((int)cx - gfx_text_w("PLUCK", FONT_SMALL) / 2, (int)(pcy + r + 4), "PLUCK", sel && ring == 0 ? col : C_DIM,
                 FONT_SMALL);
        gfx_text((int)cx - gfx_text_w("BOW", FONT_SMALL) / 2, (int)(bcy + r + 4), "BOW", sel && ring == 1 ? col : C_DIM,
                 FONT_SMALL);
    }
}

/* the quiet view: five strings across the room under one long roof, each
 * with its rings small at the end; no words but the chord */
static void draw_quiet(void)
{
    slant(0, 70, SCREEN_W, 30, 3, gfx_mix(C_BG, C_COPPER, 0.6f));
    for (int s = 0; s < NS; s++) {
        int y = 120 + s * 72, on = song.on[s];
        const int *P = song.sp[s];
        draw_string_big(40, SCREEN_W - 150, y, on ? live.meter[s] : 0.f, live.phase[s], scol[s], on, 2.4f);
        draw_ring(SCREEN_W - 110, y, 20.f, P, RING_PLUCK, live.step[s][RING_PLUCK], live.flash[s][RING_PLUCK], scol[s], 0, !on);
        draw_ring(SCREEN_W - 50, y, 20.f, P, RING_BOW, live.step[s][RING_BOW], live.bow_env[s], scol[s], 0, !on);
    }
    const char *cn = engine_chord_name();
    gfx_text((SCREEN_W - gfx_text_w(cn, FONT_SMALL)) / 2, SCREEN_H - 34, cn, C_DIM, FONT_SMALL);
}

void ui_draw(uint32_t *fb)
{
    gfx_target(fb);
    gfx_clear(C_BG);
    if (page == PAGE_PLAY && quiet) {
        draw_quiet();
        draw_toast();
        return;
    }
    draw_header();
    switch (page) {
    case PAGE_PLAY: draw_play(); break;
    case PAGE_STRINGS: draw_strings(); break;
    case PAGE_DRONE: draw_drone(); break;
    case PAGE_MOD: draw_mod(); break;
    default: draw_fx(); break;
    }
    draw_footer();
    draw_toast();
}
