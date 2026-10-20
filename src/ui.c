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
static const char *const page_names[PAGE_COUNT] = {"PLAY", "SOUND", "COMPOSE", "MIX"};
static int page, cur_s;
static int ring;  /* PLAY: the selected ring of the selected string */
static int quiet; /* PLAY: the quiet view, strings and rings only */
static int l2_used, r2_used;
static int bowing = -1; /* the string bowed by hand (R1 held) */
static int start_used;  /* START took part in a page change */
/* some button drivers send a quick release and press while a button is
 * held: a bow survives a gap this short, and a pluck is not repeated */
#define REPEAT_GAP_MS 70
static int bow_release_ms, l1_up_ms = -1000;
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

static void set_scale(int sc)
{
    engine_set_scale(wrap(sc, engine_nscales()));
    say("%s", engine_scale_name(song.gp[GP_SCALE]));
}

/* ---------------------------------------------------------------- cells
 *
 * The SOUND, COMPOSE and MIX pages are laid out as cells, each one
 * parameter; the d-pad moves to the nearest cell in its direction. */
enum { CK_S, CK_G, CK_L, CK_F }; /* string, global, LFO, effect */
typedef struct {
    short x, y, w, h;
    char kind, a, b;
} Cell;
#define MAXC 64
static Cell cells[MAXC];
static int ncell, sel_cell[PAGE_COUNT];

static void cell(int x, int y, int w, int h, int kind, int a, int b)
{
    if (ncell < MAXC)
        cells[ncell++] = (Cell){(short)x, (short)y, (short)w, (short)h, (char)kind, (char)a, (char)b};
}

/* SOUND: per-string tone above, the shared resonance below */
static const int sound_rows[9] = {SP_NOTE, SP_HARM, SP_BOW, SP_DECAY, SP_BRIGHT, SP_FOLD, SP_STIFF, SP_POS, SP_BUZZ};
#define SOUND_Y 106
#define SOUND_RH 30
#define BAND_Y (SOUND_Y + 9 * SOUND_RH + 4)
/* COMPOSE: the progression on the left, its controls on the right */
static const int comp_a[7] = {GP_SCALE, GP_ROOT, GP_MODE, GP_CHANGES, GP_CHANCE, GP_GLIDE, GP_BPM};
static const int comp_b[7] = {GP_MOTION, GP_HOME, GP_COLOR, GP_EVOLVE, GP_RELATE, GP_DRIFT, GP_SYNC};
#define COMP_X 268
#define COMP_CW 184
#define LFO_Y 318
/* MIX: a strip per string, master, then delay, reverb and gate */
#define MIX_FX_Y 244
#define MIX_FXC_Y 330

static void layout(int pg)
{
    ncell = 0;
    const int cw = SCREEN_W / NS;
    switch (pg) {
    case PAGE_SOUND:
        for (int r = 0; r < 9; r++)
            for (int s = 0; s < NS; s++)
                cell(s * cw + 3, SOUND_Y + r * SOUND_RH, cw - 6, SOUND_RH - 3, CK_S, s, sound_rows[r]);
        cell(3, BAND_Y, 154, 32, CK_F, 0, FX_SYMP);
        cell(163, BAND_Y, 154, 32, CK_F, 0, FX_BODY);
        cell(323, BAND_Y, 154, 32, CK_F, 0, FX_BTYPE);
        cell(483, BAND_Y, 154, 32, CK_G, 0, GP_CUTOFF);
        for (int i = 0; i < 5; i++)
            cell(i * cw + 3, BAND_Y + 36, cw - 6, 34, CK_F, 0, FX_SCOUNT + i);
        break;
    case PAGE_COMPOSE:
        for (int r = 0; r < 7; r++) {
            cell(COMP_X, BODY_Y + r * 34, COMP_CW - 6, 31, CK_G, 0, comp_a[r]);
            cell(COMP_X + COMP_CW, BODY_Y + r * 34, COMP_CW - 6, 31, CK_G, 0, comp_b[r]);
        }
        for (int l = 0; l < NLFO; l++)
            for (int p = 0; p < NLP; p++)
                cell(COMP_X + l * COMP_CW, LFO_Y + p * 33, COMP_CW - 6, 30, CK_L, l, p);
        break;
    case PAGE_MIX:
        for (int s = 0; s < NS; s++) {
            cell(s * cw + 3, BODY_Y + 42, cw - 6, 31, CK_S, s, SP_LEVEL);
            cell(s * cw + 3, BODY_Y + 76, cw - 6, 31, CK_S, s, SP_PAN);
            cell(s * cw + 3, BODY_Y + 110, cw - 6, 31, CK_S, s, SP_GATE);
        }
        cell(3, BODY_Y + 146, 314, 31, CK_G, 0, GP_MASTER);
        cell(323, BODY_Y + 146, 314, 31, CK_F, 0, FX_SLEVEL);
        for (int c = 0; c < 3; c++)
            for (int p = 0; p < 3; p++)
                cell(c * 213 + 4, MIX_FXC_Y + p * 42, 205, 38, CK_F, 0, c * 3 + p);
        break;
    }
}

static int *cell_ref(const Cell *c, const ParamDef **d)
{
    switch (c->kind) {
    case CK_S: *d = sparam_def(c->b); return &song.sp[(int)c->a][(int)c->b];
    case CK_G: *d = gparam_def(c->b); return &song.gp[(int)c->b];
    case CK_L: *d = lparam_def(c->b); return &song.lfo[(int)c->a][(int)c->b];
    default: *d = fparam_def(c->b); return &song.fx[(int)c->b];
    }
}

static void cell_format(const Cell *c, char *v, int n)
{
    switch (c->kind) {
    case CK_S: sparam_format(c->a, c->b, v, n); break;
    case CK_G: gparam_format(c->b, v, n); break;
    case CK_L: lparam_format(c->a, c->b, v, n); break;
    default: fparam_format(c->b, v, n); break;
    }
}

/* the nearest cell from the selected one in a direction */
static void nav(int dx, int dy)
{
    layout(page);
    int cur = sel_cell[page] < ncell ? sel_cell[page] : 0, best = cur;
    float cx = cells[cur].x + cells[cur].w / 2.f, cy = cells[cur].y + cells[cur].h / 2.f, bc = 1e9f;
    for (int i = 0; i < ncell; i++) {
        float ex = cells[i].x + cells[i].w / 2.f - cx, ey = cells[i].y + cells[i].h / 2.f - cy;
        float along = ex * dx + ey * dy, across = fabsf(dx ? ey : ex);
        if (along < 4.f)
            continue;
        float cost = along + 3.f * across;
        if (cost < bc)
            bc = cost, best = i;
    }
    sel_cell[page] = best;
}

static void adjust(int dir, int repeats)
{
    layout(page);
    if (sel_cell[page] >= ncell)
        return;
    const Cell *c = &cells[sel_cell[page]];
    const ParamDef *d;
    int *v = cell_ref(c, &d);
    int step = (repeats > 12 && d->max - d->min > 30) ? 4 : 1;
    int nv = clampi(*v + dir * step, d->min, d->max);
    if (nv == *v)
        return;
    if (c->kind == CK_G && c->b == GP_SCALE) {
        set_scale(nv);
        return;
    }
    *v = nv;
    if (c->kind == CK_G && c->b == GP_BPM)
        engine_set_bpm((float)nv);
    if (c->kind == CK_G && c->b == GP_SYNC)
        engine_set_rate(1.0);
    if (c->kind == CK_G && c->b == GP_MODE)
        say("%s", nv == MODE_MONO ? "MONO: ALL STRINGS FOLLOW STRING 1" : "CHORD: EACH STRING ITS OWN NOTE");
}

/* the string of the selected cell, or -1 */
static int cell_string(void)
{
    layout(page);
    return sel_cell[page] < ncell && cells[sel_cell[page]].kind == CK_S ? cells[sel_cell[page]].a : -1;
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
        /* as on RadioScan: steps and step length on the d-pad */
        switch (b) {
        case BTN_UP: ring_edit(1, +1); return;
        case BTN_DOWN: ring_edit(1, -1); return;
        case BTN_RIGHT: ring_edit(SP_RATE, -1); return; /* right = faster */
        case BTN_LEFT: ring_edit(SP_RATE, +1); return;
        case BTN_X: ring_edit(2, +1); return; /* more pulses */
        case BTN_Y: ring_edit(3, -1); return; /* rotate back */
        case BTN_A: /* mute or unmute the string (it fades) */
            song.on[cur_s] = !song.on[cur_s];
            say("STRING %d %s", cur_s + 1, song.on[cur_s] ? "ON" : "MUTED");
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
        case BTN_X:
        case BTN_Y: { /* the chance each pulse of the selected ring plays */
            int *c = &song.sp[cur_s][ring ? SP_BCHANCE : SP_PCHANCE];
            *c = clampi(*c + (b == BTN_X ? 10 : -10), 0, 100);
            say("%d %s CHANCE %d%%", cur_s + 1, ring ? "BOW" : "PLUCK", *c);
            return;
        }
        case BTN_A: song.fx[FX_BODY] = clampi(song.fx[FX_BODY] + 5, 0, 100); say("BODY %d", song.fx[FX_BODY]); return;
        case BTN_B: song.fx[FX_BODY] = clampi(song.fx[FX_BODY] - 5, 0, 100); say("BODY %d", song.fx[FX_BODY]); return;
        }
    }
    /* the rings play like HI5's pattern cells and RadioScan's rhythms:
     * A on/off, B fewer pulses (wrapping back to all), X rotate, Y a new
     * pattern; the d-pad picks the string and the ring */
    int *P = song.sp[cur_s];
    switch (b) {
    case BTN_LEFT: cur_s = wrap(cur_s - 1, NS); break;
    case BTN_RIGHT: cur_s = wrap(cur_s + 1, NS); break;
    case BTN_UP: ring = RING_PLUCK; ring_show(); break;
    case BTN_DOWN: ring = RING_BOW; ring_show(); break;
    case BTN_A:
        P[RING_P(ring, 0)] = !P[RING_P(ring, 0)];
        ring_show();
        break;
    case BTN_B: {
        int pu = P[RING_P(ring, 2)];
        P[RING_P(ring, 2)] = pu > 1 ? pu - 1 : P[RING_P(ring, 1)];
        P[RING_P(ring, 0)] = 1;
        ring_show();
        break;
    }
    case BTN_X: ring_edit(3, +1); break;
    case BTN_Y:
        engine_euclid_random(cur_s, ring);
        ring_show();
        break;
    }
}

static void action(int b, int repeats)
{
    switch (b) {
    case BTN_L1:
    case BTN_R1:
        /* START + L1 / R1 changes page everywhere; L1 / R1 alone changes
         * page on the edit pages, and plays the selected string on PLAY:
         * L1 plucks, R1 bows while held */
        if (held[BTN_START] || page != PAGE_PLAY) {
            start_used |= held[BTN_START];
            page = wrap(page + (b == BTN_R1 ? 1 : -1), PAGE_COUNT);
        }
        else if (b == BTN_L1)
            engine_pluck(cur_s);
        else {
            bowing = cur_s;
            engine_bow(cur_s, 1);
        }
        return;
    case BTN_START: return; /* acts on release, unless used for a page change */
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
    int cs = cell_string();
    switch (b) {
    case BTN_Y: adjust(-1, repeats); return;
    case BTN_X: adjust(+1, repeats); return;
    case BTN_UP: nav(0, -1); return;
    case BTN_DOWN: nav(0, 1); return;
    case BTN_LEFT: nav(-1, 0); return;
    case BTN_RIGHT: nav(1, 0); return;
    case BTN_A: /* mute the string the cell belongs to */
        if (cs >= 0) {
            song.on[cs] = !song.on[cs];
            say("STRING %d %s", cs + 1, song.on[cs] ? "ON" : "MUTED");
        }
        return;
    case BTN_B: /* pluck that string, or the one selected on PLAY */
        engine_pluck(cs >= 0 ? cs : cur_s);
        return;
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
    if (b == BTN_R1 && !down && bowing >= 0) {
        bow_release_ms = REPEAT_GAP_MS; /* stops in ui_tick unless R1 comes back */
        return;
    }
    if (b == BTN_R1 && down && bowing >= 0) {
        bow_release_ms = 0; /* still bowing the same string */
        return;
    }
    if (b == BTN_L1 && page == PAGE_PLAY && !held[BTN_START]) {
        if (!down)
            l1_up_ms = now_ms;
        else if (now_ms - l1_up_ms < REPEAT_GAP_MS)
            return; /* a glitch, not a new pluck */
    }
    if (b == BTN_START) {
        if (down)
            start_used = 0;
        else if (!start_used) { /* a plain tap: start or stop the drone */
            ui_lock();
            engine_set_playing(!live.playing);
            ui_unlock();
            say("%s", live.playing ? "DRONE ON" : "DRONE OFF");
        }
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
    if (bow_release_ms > 0 && (bow_release_ms -= ms) <= 0 && bowing >= 0 && !held[BTN_R1]) {
        ui_lock();
        engine_bow(bowing, 0);
        ui_unlock();
        bowing = -1;
    }
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
        "A:ON/OFF B:FEWER X:ROTATE Y:NEW L1:PLUCK R1:BOW L2:EDIT R2:SOUND",
        "D-PAD:MOVE Y:- X:+ A:MUTE B:PLUCK L1/R1:PAGE SEL:CHORD",
        "D-PAD:MOVE Y:- X:+ B:PLUCK L1/R1:PAGE SEL:NEXT CHORD",
        "D-PAD:MOVE Y:- X:+ A:MUTE B:PLUCK L1/R1:PAGE SEL:CHORD",
    };
    const char *h = hints[page];
    if (page == PAGE_PLAY && held[BTN_L2])
        h = "L2+ ^v:STEPS <>:RATE X:MORE PULSES Y:ROTATE BACK A:MUTE";
    else if (page == PAGE_PLAY && held[BTN_R2])
        h = "R2+ X/Y:RING CHANCE <>:SCALE ^v:TONE A/B:BODY";
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

/* the colour of a cell: its string's, or its section's */
static uint32_t cell_col(const Cell *c)
{
    if (c->kind == CK_S)
        return scol[(int)c->a];
    if (c->kind == CK_L)
        return c->a ? C_SAND : C_COPPER;
    if (c->kind == CK_F && c->b >= FX_SCOUNT)
        return C_SEA;
    if (c->kind == CK_F && c->b >= FX_SYMP)
        return C_SAND;
    if (page == PAGE_COMPOSE && c->kind == CK_G && c->b >= GP_MOTION)
        return C_SEA;
    if (page == PAGE_MIX && c->kind == CK_F && c->b < FX_SYMP)
        return c->b < 3 ? C_COPPER : c->b < 6 ? C_SAND : C_SEA;
    return c->kind == CK_G && c->b == GP_CUTOFF ? C_SAND : C_COPPER;
}

static void draw_cells(void)
{
    layout(page);
    for (int i = 0; i < ncell; i++) {
        const Cell *c = &cells[i];
        const ParamDef *d;
        int *v = cell_ref(c, &d);
        char val[24];
        cell_format(c, val, sizeof val);
        float t = (float)(*v - d->min) / (d->max - d->min);
        int on = c->kind == CK_S ? song.on[(int)c->a] : 1;
        if (c->kind == CK_F && c->b > FX_SCOUNT && song.fx[FX_SCOUNT] == 0)
            on = 0;
        if (c->kind == CK_S && c->b == SP_NOTE && song.gp[GP_MODE] == MODE_MONO && c->a > 0)
            on = 0;
        param_row(c->x, c->y, c->w, c->h, i == sel_cell[page], on, d->name, val, t, d->min < 0, cell_col(c));
    }
}

/* the five strings as a header: a long roof falling across them */
static void string_heads(int y0, int h)
{
    const int cw = SCREEN_W / NS;
    int cs = cell_string();
    for (int s = 0; s < NS; s++) {
        int x = s * cw + 3, w = cw - 6, on = song.on[s];
        uint32_t col = scol[s];
        roof(x, y0, w, h, 5, s == cs ? gfx_mix(C_PANEL2, col, 0.14f) : C_PANEL);
        char nm[24], dn[16];
        engine_degree_name(song.gp[GP_MODE] == MODE_MONO ? song.sp[0][SP_NOTE] : song.sp[s][SP_NOTE], dn, sizeof dn);
        snprintf(nm, sizeof nm, "%d %s", s + 1, dn);
        gfx_text(x + 8, y0 + 1, nm, on ? col : C_DIM, FONT_SMALL);
        if (!on)
            gfx_fill(x + w - 26, y0 + 12, 18, 4, C_RED);
        draw_string_big(x + 12, x + w - 12, y0 + h - 9, on ? live.meter[s] : 0.f, live.phase[s], col, on, 0.6f);
    }
}

static void draw_sound(void)
{
    string_heads(BODY_Y, SOUND_Y - BODY_Y - 3);
    /* the resonance band: one copper roofline over body and sympathetic strings */
    slant(0, BAND_Y - 2, SCREEN_W, BAND_Y - 4, 2, C_COPPER);
    draw_cells();
    /* the sympathetic strings' meters, small posts along the bottom row */
    int n = song.fx[FX_SCOUNT];
    for (int k = 0; k < n; k++) {
        int h = (int)(fminf(1.f, engine_symp_meter(k) * 6.f) * 10.f);
        gfx_fill(SCREEN_W - 10 - (n - k) * 5, BAND_Y + 36 + 30 - h, 3, h + 1, C_SEA);
    }
}

static void draw_compose(void)
{
    /* the scale as a polygon: each degree at its true angle in the octave;
     * the chord joined inside it, the sympathetic strings as dots */
    int px = 6, pw = 252;
    roof(px, BODY_Y, pw, FOOTER_Y - 6 - BODY_Y, 20, C_PANEL);
    slant(px, BODY_Y, px + pw, BODY_Y + 20, 2, C_COPPER);
    gfx_text(px + 10, BODY_Y + 22, engine_scale_name(song.gp[GP_SCALE]), C_DIM, FONT_SMALL);
    int n = engine_scale_n();
    float cx = px + pw / 2.f, cy = BODY_Y + 150, r = 74.f, vx[12], vy[12];
    for (int i = 0; i < n; i++) {
        float a = -1.5707963f + 6.2831853f * engine_scale_cents(i) / 1200.f;
        vx[i] = cx + r * cosf(a);
        vy[i] = cy + r * sinf(a);
    }
    for (int i = 0; i < n; i++)
        line(vx[i], vy[i], vx[(i + 1) % n], vy[(i + 1) % n], 1.f, C_LINE);
    int has[12] = {0}, order[12], no = 0;
    for (int s = 0; s < NS; s++)
        if (song.on[s]) {
            int d = song.gp[GP_MODE] == MODE_MONO ? song.sp[0][SP_NOTE] : song.sp[s][SP_NOTE];
            has[((d % n) + n) % n] |= 1 << s;
        }
    for (int i = 0; i < n; i++)
        if (has[i])
            order[no++] = i;
    uint32_t fill = gfx_mix(C_BG, C_COPPER, 0.35f + 0.3f * (live.master_meter > 0.5f));
    for (int k = 1; k + 1 < no; k++)
        gfx_tri(vx[order[0]], vy[order[0]], vx[order[k]], vy[order[k]], vx[order[k + 1]], vy[order[k + 1]], fill);
    for (int k = 0; no >= 2 && k < no; k++)
        line(vx[order[k]], vy[order[k]], vx[order[(k + 1) % no]], vy[order[(k + 1) % no]], 2.f, C_COPPER);
    int sym[12] = {0};
    for (int k = 0; k < song.fx[FX_SCOUNT]; k++) {
        int d = engine_symp_degree(k);
        sym[((d % n) + n) % n]++;
    }
    for (int i = 0; i < n; i++) {
        diamond(vx[i], vy[i], has[i] ? 6.f : 3.f, has[i] ? C_TEXT : C_DIM);
        /* the strings on a degree step inward, the sympathetic strings outward */
        for (int s = 0, j = 0; s < NS; s++)
            if (has[i] & (1 << s))
                diamond(cx + (vx[i] - cx) * (0.84f - 0.12f * j), cy + (vy[i] - cy) * (0.84f - 0.12f * j), 4.f, scol[s]), j++;
        for (int k = 0; k < sym[i]; k++)
            gfx_disc(cx + (vx[i] - cx) * (1.12f + 0.07f * k), cy + (vy[i] - cy) * (1.12f + 0.07f * k), 2.f, C_SEA);
        char nm[12];
        engine_pc_name(i, nm, sizeof nm);
        float lx = cx + (vx[i] - cx) * 1.5f, ly = cy + (vy[i] - cy) * 1.42f;
        if (n <= 8 || has[i])
            gfx_text((int)lx - gfx_text_w(nm, FONT_SMALL) / 2, (int)ly - 11, nm, has[i] ? C_SAND : C_DIM, FONT_SMALL);
    }
    /* the chord now, and the ones before it fading down the roof */
    const char *cn = engine_chord_name();
    gfx_text((int)cx - gfx_text_w(cn, FONT_LARGE) / 2, BODY_Y + 254, cn[0] ? cn : "-", C_SAND, FONT_LARGE);
    for (int i = 1; i < 4; i++) {
        const char *h = engine_chord_history(i);
        if (!h[0])
            break;
        int y = BODY_Y + 288 + (i - 1) * 22;
        para(px + 16 + i * 6, y + 4, 10, 12, 3, gfx_mix(C_PANEL, C_COPPER, 0.9f - 0.15f * i));
        gfx_text(px + 36 + i * 6, y, h, gfx_mix(C_PANEL, C_DIM, 1.f - 0.12f * i), FONT_SMALL);
    }
    static const int bars_of[6] = {0, 1, 2, 4, 8, 16};
    int bars = bars_of[song.gp[GP_CHANGES]];
    if (bars) {
        int done = (int)((live.beat / 4) % (uint32_t)bars), bw = bars > 8 ? 9 : 16;
        for (int i = 0; i < bars; i++)
            para(px + 14 + i * (bw + 4), FOOTER_Y - 26, bw, 12, 3, i <= done ? C_COPPER : C_PANEL2);
    }
    /* the controls: harmony and time, the algorithm, then two LFOs */
    for (int l = 0; l < NLFO; l++) {
        int x = COMP_X + l * COMP_CW;
        char t[12];
        snprintf(t, sizeof t, "LFO %d", l + 1);
        gfx_text(x + 4, LFO_Y - 22, t, l ? C_SAND : C_COPPER, FONT_SMALL);
        int bw = COMP_CW - 70;
        gfx_fill(x + 60, LFO_Y - 13, bw, 4, C_PANEL2);
        gfx_fill(x + 60, LFO_Y - 13, (int)(bw * live.lfo_val[l]), 4, song.lfo[l][LP_DEPTH] ? (l ? C_SAND : C_COPPER) : C_LINE);
    }
    draw_cells();
}

static void draw_mix(void)
{
    const int cw = SCREEN_W / NS;
    int cs = cell_string();
    for (int s = 0; s < NS; s++) {
        int x = s * cw + 3, w = cw - 6, on = song.on[s];
        uint32_t col = scol[s];
        chamfer(x, BODY_Y, w, 38, 8, s == cs ? gfx_mix(C_PANEL2, col, 0.14f) : C_PANEL);
        char nm[8];
        snprintf(nm, sizeof nm, "%d", s + 1);
        gfx_text(x + 8, BODY_Y, nm, on ? col : C_DIM, FONT_SMALL);
        if (!on)
            gfx_fill(x + 24, BODY_Y + 9, 18, 4, C_RED);
        /* the meter, placed where the string sits in the stereo field */
        int mw = w - 16, mx = x + 8, my = BODY_Y + 26;
        gfx_fill(mx, my, mw, 5, C_PANEL2);
        int lv = (int)(mw * fminf(1.f, sqrtf(live.meter[s] * 4.f)) * (on ? 1 : 0));
        gfx_fill(mx, my, lv, 5, col);
        int pp = mx + mw / 2 + song.sp[s][SP_PAN] * mw / 100;
        gfx_fill(pp - 1, my - 4, 3, 13, C_TEXT);
    }
    int mm = (int)(628 * fminf(1.f, live.master_meter));
    gfx_fill(6, BODY_Y + 180, 628, 3, C_PANEL2);
    gfx_fill(6, BODY_Y + 180, mm, 3, live.master_meter > 0.95f ? C_RED : C_SEA);

    static const char *const names[3] = {"DELAY", "REVERB", "BINAURAL"};
    for (int c = 0; c < 3; c++) {
        int x = c * 213 + 4, w = 205;
        uint32_t col = c == 0 ? C_COPPER : c == 1 ? C_SAND : C_SEA;
        roof(x, MIX_FX_Y + c * 6, w, MIX_FXC_Y - 4 - MIX_FX_Y - c * 6, 6, C_PANEL);
        gfx_text(x + 10, MIX_FX_Y + c * 6 + 6, names[c], col, FONT_SMALL);
        int gx = x + 14, gy = MIX_FX_Y + 36, gw = w - 28, gh = 48;
        if (c == 0) { /* echoes dying away */
            float fb = song.fx[FX_DFB] / 100.f, a = 1.f;
            for (int i = 0; i < 6; i++) {
                int h = (int)(gh * a * (0.3f + 0.7f * song.fx[FX_DMIX] / 100.f));
                gfx_fill(gx + i * (gw / 6), gy + gh - h, gw / 6 - 6, h, i ? gfx_mix(C_PANEL2, col, a) : col);
                a *= 0.35f + 0.6f * fb;
            }
        }
        else if (c == 1) { /* a wedge of decay */
            int len = (int)(gw * (0.3f + 0.7f * song.fx[FX_VTIME] / 100.f));
            gfx_tri(gx, gy, gx + len, gy + gh, gx, gy + gh, gfx_mix(C_PANEL2, col, 0.3f + 0.6f * song.fx[FX_VSEND] / 100.f));
        }
        else { /* two waves, left and right, sliding past each other at the beat */
            float hz = live.bin_hz, d = song.fx[FX_BINDEPTH] / 100.f;
            float drift = hz > 0.f ? 6.2831853f * fmodf(now_ms * 0.001f * hz, 1.f) * d : 0.f;
            for (int w = 0; w < 2; w++) {
                int py = 0;
                uint32_t wc = hz > 0.f ? (w ? col : gfx_mix(col, C_SAND, 0.5f)) : C_LINE;
                for (int i = 0; i <= gw; i++) {
                    float t = (float)i / gw * 6.2831853f * 3.f + (w ? drift : 0.f);
                    int yy = gy + gh / 2 + (int)(sinf(t) * gh * 0.32f) + (w ? 0 : 0);
                    if (i)
                        gfx_fill(gx + i, yy < py ? yy : py, 1, abs(yy - py) + 2, wc);
                    py = yy;
                }
            }
            gfx_text(x + w - 14 - gfx_text_w(hz > 0.f ? "L  R" : "OFF", FONT_SMALL), MIX_FX_Y + c * 6 + 6,
                     hz > 0.f ? "L  R" : "OFF", C_DIM, FONT_SMALL);
        }
    }
    draw_cells();
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
        char lb[24];
        snprintf(lb, sizeof lb, P[SP_PCHANCE] < 100 ? "PLUCK %d%%" : "PLUCK", P[SP_PCHANCE]);
        gfx_text((int)cx - gfx_text_w(lb, FONT_SMALL) / 2, (int)(pcy + r + 4), lb, sel && ring == 0 ? col : C_DIM, FONT_SMALL);
        snprintf(lb, sizeof lb, P[SP_BCHANCE] < 100 ? "BOW %d%%" : "BOW", P[SP_BCHANCE]);
        gfx_text((int)cx - gfx_text_w(lb, FONT_SMALL) / 2, (int)(bcy + r + 4), lb, sel && ring == 1 ? col : C_DIM, FONT_SMALL);
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
    case PAGE_SOUND: draw_sound(); break;
    case PAGE_COMPOSE: draw_compose(); break;
    default: draw_mix(); break;
    }
    draw_footer();
    draw_toast();
}
