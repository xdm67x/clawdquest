#include "audio.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#define RATE 48000

typedef struct { float f, t0, d; } Beep;
static Beep q[64];
static int nq;
static double stream_t;   // seconds of audio generated so far
static bool on;

// One scheduled beep, like fx.js beep(f, d, t).
static void beep(float f, float d, float t) {
    if (!on || nq >= (int)(sizeof q / sizeof q[0])) return;
    q[nq].f = f;
    q[nq].d = d;
    q[nq].t0 = (float)stream_t + t;
    nq++;
}

// The callback synthesizes into the stream; called from the audio thread.
static void callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    (void)userdata;
    (void)total_amount;
    int frames = additional_amount / (int)sizeof(float);
    if (frames <= 0) return;
    static float buf[4800];
    if (frames > 4800) frames = 4800;
    memset(buf, 0, (size_t)frames * sizeof(float));

    for (int b = 0; b < nq; b++) {
        float start = q[b].t0 - (float)stream_t; // seconds from buffer start
        int i0 = (int)(start * RATE);
        int i1 = (int)((start + q[b].d) * RATE);
        if (i1 < 0) continue;
        if (i0 < 0) i0 = 0;
        if (i1 > frames) i1 = frames;
        for (int i = i0; i < i1; i++) {
            double t = (double)i / RATE - start;
            if (t < 0 || t > q[b].d) continue;
            double phase = fmod(t * q[b].f, 1.0);
            float sq = phase < 0.5 ? 1.0f : -1.0f;
            // Exponential decay 0.07 -> 0.001 across the beep.
            float g = 0.07f * (float)pow(0.001 / 0.07, t / q[b].d);
            buf[i] += sq * g;
        }
    }
    stream_t += (double)frames / RATE;

    // Retire beeps that ended before the buffer start.
    int w = 0;
    for (int b = 0; b < nq; b++)
        if (q[b].t0 + q[b].d > (float)stream_t) q[w++] = q[b];
    nq = w;

    SDL_PutAudioStreamData(stream, buf, frames * (int)sizeof(float));
}

void audio_init(void) {
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, RATE };
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, NULL);
    if (!stream) return;
    if (!SDL_ResumeAudioStreamDevice(stream)) return;
    on = true;
}

void sfx_hit(void) { beep(180, 0.10f, 0); beep(320, 0.12f, 0.06f); }
void sfx_win(void) {
    static const float f[4] = { 523, 659, 784, 1046 };
    for (int i = 0; i < 4; i++) beep(f[i], 0.15f, 0.09f * i);
}
void sfx_add(void) { beep(440, 0.06f, 0); }
