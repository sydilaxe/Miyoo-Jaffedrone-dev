/* Miyoo Mini (Plus) front-end: SDL 1.2 as shipped with OnionOS. */
#include <SDL/SDL.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"
#include "gfx.h"
#include "log.h"
#include "sync.h"
#include "ui.h"

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

#define SAVE_PATH "jaffedrone.sav"

static volatile sig_atomic_t got_signal;
static void on_signal(int s) { got_signal = s; }

static void lock(void) { SDL_LockAudio(); }
static void unlock(void) { SDL_UnlockAudio(); }

/* the audio callback's timing for the log: time spent, and gaps longer
 * than one and a half buffers (the device ran dry: a crackle) */
static AudioTiming timing;
static uint64_t clock0_us, made_us;

/* a dropout is when the device has played everything we made: real time
 * has run ahead of the audio produced by more than the device's buffers
 * (two fragments here). Drivers that call twice in a row and then wait
 * are fine; only a true gap counts. */
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    static int prio_tried;
    if (!prio_tried) {
        /* the audio thread first, always: drawing the screen must never hold
         * it up (the Miyoo runs the app as root, so this is allowed) */
        prio_tried = 1;
        struct sched_param sp = {.sched_priority = 40};
        timing.rt = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0;
#if defined(__arm__)
        /* numbers too small to hear (below 1e-38, at the very end of a
         * decay) are taken as zero, which the processor handles at full
         * speed */
        unsigned fpscr;
        __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= 1u << 24;
        __asm__ volatile("vmsr fpscr, %0" : : "r"(fpscr));
#endif
    }
    uint64_t t0 = now_us();
    static uint64_t prev_t0;
    if (prev_t0 && t0 - prev_t0 > timing.gap_max_us)
        timing.gap_max_us = (uint32_t)(t0 - prev_t0);
    prev_t0 = t0;
    if (!clock0_us)
        clock0_us = t0;
    else if (timing.buffer_us) {
        uint64_t real = t0 - clock0_us;
        if (real > made_us + 3ULL * timing.buffer_us) {
            timing.late++;
            clock0_us = t0 - made_us; /* start counting again from here */
        }
    }
    made_us += timing.buffer_us;
    engine_render((int16_t *)stream, len / 4);
    uint32_t spent = (uint32_t)(now_us() - t0);
    timing.calls++;
    timing.us_sum += spent;
    if (spent > timing.us_max)
        timing.us_max = spent;
}

static int map_key(SDLKey k)
{
    switch (k) {
    case SDLK_UP: return BTN_UP;
    case SDLK_DOWN: return BTN_DOWN;
    case SDLK_LEFT: return BTN_LEFT;
    case SDLK_RIGHT: return BTN_RIGHT;
    case SDLK_SPACE: return BTN_A;
    case SDLK_LCTRL: return BTN_B;
    case SDLK_LSHIFT: return BTN_X;
    case SDLK_LALT: return BTN_Y;
    case SDLK_e: return BTN_L1;
    case SDLK_t: return BTN_R1;
    case SDLK_TAB: return BTN_L2;
    case SDLK_BACKSPACE: return BTN_R2;
    case SDLK_RCTRL: return BTN_SELECT;
    case SDLK_RETURN: return BTN_START;
    case SDLK_ESCAPE: return BTN_MENU;
    default: return -1;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    fprintf(stderr, "jaffedrone v%s\n", JD_VERSION);
    jdlog_set_clock(now_us() / 1000);
    jdlog_open("jaffedrone_log.txt", "jaffedrone.cfg");
    song_default();
    if (song_load(SAVE_PATH))
        fprintf(stderr, "jaffedrone: restored %s\n", SAVE_PATH);

    int rate = getenv("JD_RATE") ? atoi(getenv("JD_RATE")) : 48000;
    int bufsz = getenv("JD_BUFFER") ? atoi(getenv("JD_BUFFER")) : 1024;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "jaffedrone: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Surface *video = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 32, SDL_HWSURFACE);
    SDL_Surface *screen = SDL_CreateRGBSurface(SDL_HWSURFACE, SCREEN_W, SCREEN_H, 32,
                                               0x00ff0000, 0x0000ff00, 0x000000ff, 0);
    if (!video || !screen) {
        fprintf(stderr, "jaffedrone: video: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    engine_init(rate);
    engine_set_playing(1);

    SDL_AudioSpec want;
    memset(&want, 0, sizeof want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = (Uint16)bufsz;
    want.callback = audio_cb;
    int have_audio = SDL_OpenAudio(&want, NULL) == 0;
    if (have_audio) {
        fprintf(stderr, "jaffedrone: audio %d Hz, %d frames/buffer\n", rate, bufsz);
        timing.buffer_us = (uint32_t)(1000000.0 * bufsz / rate);
        jdlog("audio %d Hz, %d frames a buffer (%.1f ms); voices %s %s %s %s %s, scale %s", rate, bufsz,
              timing.buffer_us / 1000.0, engine_voice_name(song.sp[0][SP_VOICE]), engine_voice_name(song.sp[1][SP_VOICE]),
              engine_voice_name(song.sp[2][SP_VOICE]), engine_voice_name(song.sp[3][SP_VOICE]),
              engine_voice_name(song.sp[4][SP_VOICE]), engine_scale_name(song.gp[GP_SCALE]));
        ui_lock = lock;
        ui_unlock = unlock;
        /* the screen gives way: this (main) thread runs below everything the
         * sound needs, the audio driver's helpers included */
        if (setpriority(PRIO_PROCESS, 0, 10) != 0)
            fprintf(stderr, "jaffedrone: could not lower the screen's priority\n");
        SDL_PauseAudio(0);
    }
    else
        fprintf(stderr, "jaffedrone: SDL_OpenAudio: %s\n", SDL_GetError());

    ui_init();
    sync_init();
    Uint32 last = SDL_GetTicks(), last_save = last;
    while (!ui_quit_requested() && !got_signal) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
                ui_button(map_key(e.key.keysym.sym), e.type == SDL_KEYDOWN);
            else if (e.type == SDL_QUIT)
                got_signal = 1;
        }
        Uint32 now = SDL_GetTicks();
        ui_tick((int)(now - last));
        last = now;
        sync_poll(now_us());
        jdlog_tick(now_us() / 1000, &timing);

        if (SDL_MUSTLOCK(screen))
            SDL_LockSurface(screen);
        if (screen->pitch == SCREEN_W * 4)
            ui_draw((uint32_t *)screen->pixels);
        else {
            static uint32_t fb[SCREEN_W * SCREEN_H];
            ui_draw(fb);
            for (int y = 0; y < SCREEN_H; y++)
                memcpy((uint8_t *)screen->pixels + y * screen->pitch, fb + y * SCREEN_W, SCREEN_W * 4);
        }
        if (SDL_MUSTLOCK(screen))
            SDL_UnlockSurface(screen);
        SDL_BlitSurface(screen, NULL, video, NULL);
        SDL_Flip(video);

        if (now - last_save > 15000) {
            last_save = now;
            if (ui_take_changed())
                song_save(SAVE_PATH);
        }
        Uint32 spent = SDL_GetTicks() - now;
        timing.ui_frames++;
        timing.ui_ms_sum += spent;
        if (spent > timing.ui_ms_max)
            timing.ui_ms_max = spent;
        /* about 30 frames a second, 20 when the sound is working hard, and
         * always a pause, even after a slow frame, so the screen never takes
         * the processor from the audio */
        Uint32 frame = 33;
        if (timing.calls && timing.buffer_us && timing.us_sum / timing.calls > timing.buffer_us * 35 / 100)
            frame = 50;
        SDL_Delay(spent + 4 < frame ? frame - spent : 4);
    }

    song_save(SAVE_PATH);
    jdlog_set_clock(now_us() / 1000);
    jdlog_close();
    sync_close();
    if (have_audio)
        SDL_CloseAudio();
    SDL_Quit();
    return 0;
}
