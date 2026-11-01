/*
 * gen_sounds.c — synthesises Cinephilia's sound effects.
 *
 * Every sound is built here from sine waves and filtered noise, so the
 * whole palette is original work that can be redistributed with the source
 * code. Nothing is sampled or downloaded. Output is deterministic (a fixed
 * noise seed), 44.1 kHz, 16-bit stereo WAV.
 *
 * Design notes: everything is quiet (peaks between -26 and -12 dBFS),
 * transients are softened (no attack faster than ~2 ms), high frequencies
 * are rolled off above ~4 kHz, and every file fades in and out so there is
 * never a click at its edges. The projector's shutter runs at 24 frames per
 * second — the speed of sound film.
 *
 * Usage: gen_sounds OUTPUT_DIR
 */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RATE 44100
#define PI 3.14159265358979323846

typedef struct {
    float *l;
    float *r;
    size_t n;
} Buf;

static uint64_t rng_state = 0x5EEDC1AEULL;

static double noise(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (double)(rng_state >> 11) / (double)(1ULL << 53) * 2.0 - 1.0;
}

static Buf buf_new(double seconds)
{
    Buf b;
    b.n = (size_t)(seconds * RATE);
    b.l = calloc(b.n, sizeof(float));
    b.r = calloc(b.n, sizeof(float));
    if (!b.l || !b.r) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return b;
}

static void buf_free(Buf *b)
{
    free(b->l);
    free(b->r);
}

/* Constant-power pan: -1 left .. +1 right. */
static void put(Buf *b, size_t i, double v, double pan)
{
    if (i >= b->n)
        return;
    double a = (pan + 1.0) * PI / 4.0;
    b->l[i] += (float)(v * cos(a));
    b->r[i] += (float)(v * sin(a));
}

/* --- simple filters (RBJ biquad) ------------------------------------ */

typedef struct {
    double b0, b1, b2, a1, a2, x1, x2, y1, y2;
} Biquad;

static Biquad biquad(int type, double freq, double q)
{
    double w = 2 * PI * freq / RATE;
    double c = cos(w), s = sin(w), alpha = s / (2 * q);
    double b0, b1, b2, a0, a1, a2;
    if (type == 0) { /* low-pass */
        b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2;
    } else if (type == 1) { /* high-pass */
        b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2;
    } else { /* band-pass, constant peak gain */
        b0 = alpha; b1 = 0; b2 = -alpha;
    }
    a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
    Biquad f = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0, 0, 0, 0, 0};
    return f;
}

static double run(Biquad *f, double x)
{
    double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

/* --- building blocks ------------------------------------------------- */

/* A sine with a soft attack and an exponential decay (tau seconds). */
static void tone(Buf *b, double at, double freq, double amp, double attack, double tau,
                 double length, double pan, double detune)
{
    size_t start = (size_t)(at * RATE);
    size_t len = (size_t)(length * RATE);
    for (size_t k = 0; k < len; k++) {
        double t = (double)k / RATE;
        double env = (t < attack ? sin(PI / 2 * t / attack) : 1.0) * exp(-(t > attack ? t - attack : 0) / tau);
        double v = sin(2 * PI * freq * t) + 0.18 * sin(4 * PI * freq * t); /* warm 2nd harmonic */
        put(b, start + k, amp * env * v, pan - 0.25);
        double v2 = sin(2 * PI * (freq + detune) * t) + 0.18 * sin(4 * PI * (freq + detune) * t);
        put(b, start + k, amp * env * v2, pan + 0.25);
    }
}

/* A muted mechanical tick: band-limited noise with a tiny tonal body. */
static void tick(Buf *b, double at, double amp, double centre, double pan)
{
    Biquad bp = biquad(2, centre, 1.4);
    Biquad lp = biquad(0, 4200, 0.7);
    size_t start = (size_t)(at * RATE);
    size_t len = (size_t)(0.03 * RATE);
    for (size_t k = 0; k < len; k++) {
        double t = (double)k / RATE;
        double attack = t < 0.0015 ? t / 0.0015 : 1.0;
        double env = attack * exp(-t / 0.0045);
        double v = run(&lp, run(&bp, noise())) * 2.2 + 0.35 * sin(2 * PI * centre * 0.45 * t);
        put(b, start + k, amp * env * v, pan);
    }
}

/* A soft low thump, like a card or a drawer settling. */
static void thump(Buf *b, double at, double freq, double amp, double tau)
{
    size_t start = (size_t)(at * RATE);
    size_t len = (size_t)(tau * 7 * RATE);
    Biquad lp = biquad(0, 500, 0.7);
    for (size_t k = 0; k < len; k++) {
        double t = (double)k / RATE;
        double attack = t < 0.004 ? t / 0.004 : 1.0;
        double env = attack * exp(-t / tau);
        double f = freq * (1.0 + 0.25 * exp(-t / 0.02)); /* slight downward pitch */
        double v = sin(2 * PI * f * t) + 0.3 * run(&lp, noise());
        put(b, start + k, amp * env * v, 0);
    }
}

/* Filtered noise swept between two centre frequencies (paper, air). */
static void swish(Buf *b, double at, double length, double f0, double f1, double amp,
                  double attack, double pan0, double pan1)
{
    size_t start = (size_t)(at * RATE);
    size_t len = (size_t)(length * RATE);
    Biquad hp = biquad(1, 300, 0.7);
    /* Chamberlin state-variable filter: its band-pass output stays smooth
     * while the centre frequency moves, which a recomputed biquad would not. */
    double low = 0, band = 0;
    const double damping = 1.1;
    Biquad soften = biquad(0, 3200, 0.6); /* keep paper warm, not hissy */
    for (size_t k = 0; k < len; k++) {
        double t = (double)k / RATE;
        double p = t / length;
        double centre = f0 + (f1 - f0) * p;
        double f = 2 * sin(PI * centre / RATE);
        double x = run(&hp, noise());
        low += f * band;
        double high = x - low - damping * band;
        band += f * high;
        double env = (t < attack ? t / attack : 1.0) * pow(1 - p, 1.6);
        put(b, start + k, amp * env * run(&soften, band), pan0 + (pan1 - pan0) * p);
    }
}

/* A projector motor whose rotation frequency follows f(t) = a + (b-a)*curve.
 * spin_up: true rises a->b with ease-out; false falls a->b with ease-in. */
static void motor(Buf *b, double at, double length, double fa, double fb, double amp, int spin_up,
                  double fade_in, double fade_out)
{
    size_t start = (size_t)(at * RATE);
    size_t len = (size_t)(length * RATE);
    double phase = 0;
    Biquad bp = biquad(2, 260, 0.8);
    Biquad lp = biquad(0, 900, 0.7);
    for (size_t k = 0; k < len; k++) {
        double t = (double)k / RATE;
        double p = t / length;
        double curve = spin_up ? 1 - pow(1 - (p < 0.6 ? p / 0.6 : 1), 3) : pow(p, 1.4);
        double f = fa + (fb - fa) * curve;
        phase += 2 * PI * f / RATE;
        double flutter = 1.0 + 0.06 * sin(2 * PI * 24 * t); /* 24 fps shutter */
        double hum = 0.55 * sin(phase) + 0.30 * sin(2 * phase + 0.4) + 0.12 * sin(3 * phase + 1.1);
        double whir = run(&lp, run(&bp, noise())) * 1.6 * (f / (fa > fb ? fa : fb));
        double env = 1.0;
        if (t < fade_in)
            env = t / fade_in;
        if (t > length - fade_out)
            env *= (length - t) / fade_out;
        put(b, start + k, amp * env * flutter * (hum + whir), 0);
    }
}

/* Quiet room tone: low-passed noise, slightly different in each ear. */
static void room(Buf *b, double at, double length, double amp)
{
    size_t start = (size_t)(at * RATE);
    size_t len = (size_t)(length * RATE);
    Biquad la = biquad(0, 380, 0.7), lb = biquad(0, 380, 0.7);
    for (size_t k = 0; k < len; k++) {
        double t = (double)k / RATE;
        double env = sin(PI * t / length);
        size_t i = start + k;
        if (i < b->n) {
            b->l[i] += (float)(amp * env * run(&la, noise()) * 4);
            b->r[i] += (float)(amp * env * run(&lb, noise()) * 4);
        }
    }
}

/* --- finishing ------------------------------------------------------- */

static void finish(Buf *b, double peak_dbfs)
{
    /* Remove DC, fade edges, then normalise to the target peak. */
    double ml = 0, mr = 0;
    for (size_t i = 0; i < b->n; i++) {
        ml += b->l[i];
        mr += b->r[i];
    }
    ml /= (double)b->n;
    mr /= (double)b->n;
    size_t fin = (size_t)(0.004 * RATE), fout = (size_t)(0.03 * RATE);
    double peak = 1e-9;
    for (size_t i = 0; i < b->n; i++) {
        double g = 1.0;
        if (i < fin)
            g = (double)i / fin;
        if (i + fout > b->n)
            g *= (double)(b->n - i) / fout;
        b->l[i] = (float)((b->l[i] - ml) * g);
        b->r[i] = (float)((b->r[i] - mr) * g);
        double a = fabs(b->l[i]) > fabs(b->r[i]) ? fabs(b->l[i]) : fabs(b->r[i]);
        if (a > peak)
            peak = a;
    }
    double gain = pow(10.0, peak_dbfs / 20.0) / peak;
    for (size_t i = 0; i < b->n; i++) {
        b->l[i] = (float)(b->l[i] * gain);
        b->r[i] = (float)(b->r[i] * gain);
    }
}

static void put_le(FILE *fp, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; i++)
        fputc((int)((v >> (8 * i)) & 0xFF), fp);
}

static int write_wav(const char *dir, const char *name, const Buf *b)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "gen_sounds: cannot write %s: %s\n", path, strerror(errno));
        return 1;
    }
    uint32_t data = (uint32_t)(b->n * 4);
    fputs("RIFF", fp);
    put_le(fp, 36 + data, 4);
    fputs("WAVEfmt ", fp);
    put_le(fp, 16, 4);
    put_le(fp, 1, 2);        /* PCM */
    put_le(fp, 2, 2);        /* stereo */
    put_le(fp, RATE, 4);
    put_le(fp, RATE * 4, 4); /* byte rate */
    put_le(fp, 4, 2);        /* block align */
    put_le(fp, 16, 2);       /* bits */
    fputs("data", fp);
    put_le(fp, data, 4);
    for (size_t i = 0; i < b->n; i++) {
        float ch[2] = {b->l[i], b->r[i]};
        for (int c = 0; c < 2; c++) {
            double v = ch[c] < -1 ? -1 : ch[c] > 1 ? 1 : ch[c];
            int16_t s = (int16_t)lrint(v * 32767.0);
            put_le(fp, (uint16_t)s, 2);
        }
    }
    int bad = ferror(fp);
    if (fclose(fp) != 0 || bad) {
        fprintf(stderr, "gen_sounds: error writing %s\n", path);
        return 1;
    }
    printf("  %-20s %.2fs\n", name, (double)b->n / RATE);
    return 0;
}

/* --- the sounds ------------------------------------------------------ */

static Buf make_startup(void)
{
    Buf b = buf_new(2.9);
    room(&b, 0.0, 2.9, 0.010);
    motor(&b, 0.12, 2.6, 6, 46, 0.045, 1, 0.25, 1.1);
    /* Shutter: ticks accelerate to 24 per second, then settle. */
    double t = 0.25;
    int n = 0;
    while (t < 1.9) {
        double rate = t < 0.9 ? 5 + (24 - 5) * (t - 0.25) / 0.65 : 24;
        double fade = t > 1.4 ? 1 - (t - 1.4) / 0.5 : 1;
        tick(&b, t, 0.05 * fade, 2100, (n % 2) ? 0.15 : -0.15);
        t += 1.0 / rate;
        n++;
    }
    /* The tonal accent, timed to the wordmark: A major with an added ninth,
     * soft attack, slow decay, a little wider than the room. */
    const double chord[] = {110.0, 164.81, 220.0, 277.18, 329.63, 493.88};
    const double amps[] = {0.050, 0.040, 0.034, 0.026, 0.020, 0.010};
    for (int i = 0; i < 6; i++)
        tone(&b, 0.78 + i * 0.012, chord[i], amps[i], 0.32, 0.85, 2.1, 0, 0.45);
    finish(&b, -12);
    return b;
}

static Buf make_move(void)
{
    Buf b = buf_new(0.045);
    tick(&b, 0.002, 0.5, 1700, 0);
    finish(&b, -26);
    return b;
}

static Buf make_section(void)
{
    Buf b = buf_new(0.36);
    thump(&b, 0.0, 95, 0.35, 0.035);
    swish(&b, 0.01, 0.32, 1400, 2900, 0.5, 0.07, -0.2, 0.2);
    finish(&b, -20);
    return b;
}

static Buf make_filed(void)
{
    Buf b = buf_new(0.62);
    swish(&b, 0.0, 0.17, 2200, 1500, 0.45, 0.04, 0.25, -0.05);
    thump(&b, 0.15, 120, 0.6, 0.045);
    tick(&b, 0.152, 0.25, 900, 0);
    tone(&b, 0.21, 783.99, 0.07, 0.012, 0.22, 0.4, 0, 0.6);  /* G5 */
    tone(&b, 0.27, 1174.66, 0.04, 0.012, 0.20, 0.35, 0, 0.6); /* D6 */
    finish(&b, -18);
    return b;
}

static Buf make_confirm(void)
{
    Buf b = buf_new(0.32);
    tone(&b, 0.0, 659.25, 0.10, 0.010, 0.09, 0.3, -0.1, 0.4);  /* E5 */
    tone(&b, 0.06, 987.77, 0.06, 0.010, 0.09, 0.25, 0.1, 0.4); /* B5 */
    finish(&b, -24);
    return b;
}

static Buf make_reel(void)
{
    Buf b = buf_new(0.85);
    motor(&b, 0.0, 0.8, 34, 14, 0.03, 0, 0.05, 0.25);
    /* Ticks slow down as the reel loses speed, matching the visual reveal. */
    double t = 0.01;
    int n = 0;
    while (t < 0.75) {
        double rate = 22 - 16 * (t / 0.75);
        tick(&b, t, 0.07 * (1 - t / 1.2), 1600 + (n % 3) * 120, (n % 2) ? 0.2 : -0.2);
        t += 1.0 / rate;
        n++;
    }
    finish(&b, -20);
    return b;
}

static Buf make_reveal(void)
{
    Buf b = buf_new(0.95);
    tone(&b, 0.00, 293.66, 0.07, 0.025, 0.40, 0.9, -0.2, 0.5); /* D4 */
    tone(&b, 0.04, 440.00, 0.05, 0.025, 0.36, 0.85, 0.2, 0.5); /* A4 */
    tone(&b, 0.08, 587.33, 0.035, 0.025, 0.32, 0.8, 0.0, 0.5); /* D5 */
    finish(&b, -20);
    return b;
}

static Buf make_delete(void)
{
    Buf b = buf_new(0.32);
    swish(&b, 0.0, 0.16, 1200, 600, 0.35, 0.02, 0.1, -0.1);
    thump(&b, 0.07, 72, 0.6, 0.06);
    finish(&b, -22);
    return b;
}

static Buf make_error(void)
{
    Buf b = buf_new(0.28);
    tone(&b, 0.00, 196.00, 0.12, 0.004, 0.035, 0.12, 0, 0);
    tone(&b, 0.11, 185.00, 0.10, 0.004, 0.040, 0.14, 0, 0);
    finish(&b, -24);
    return b;
}

static Buf make_stop(void)
{
    Buf b = buf_new(1.5);
    motor(&b, 0.0, 1.45, 46, 4, 0.045, 0, 0.03, 0.6);
    double t = 0.0;
    int n = 0;
    while (t < 1.3) {
        double rate = 24 * (1 - t / 1.45) + 2;
        tick(&b, t, 0.045 * (1 - t / 1.5), 2000, (n % 2) ? 0.15 : -0.15);
        t += 1.0 / rate;
        n++;
    }
    room(&b, 0.0, 1.5, 0.008);
    finish(&b, -16);
    return b;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: gen_sounds OUTPUT_DIR\n");
        return 2;
    }
    const char *dir = argv[1];
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "gen_sounds: cannot create %s: %s\n", dir, strerror(errno));
        return 1;
    }
    struct {
        const char *name;
        Buf (*make)(void);
    } sounds[] = {
        {"startup.wav", make_startup}, {"menu_move.wav", make_move},
        {"section_open.wav", make_section}, {"film_added.wav", make_filed},
        {"confirm.wav", make_confirm}, {"film_reel.wav", make_reel},
        {"reveal.wav", make_reveal}, {"delete.wav", make_delete},
        {"error.wav", make_error}, {"projector_stop.wav", make_stop},
    };
    int failures = 0;
    printf("Synthesising sounds into %s\n", dir);
    for (size_t i = 0; i < sizeof sounds / sizeof sounds[0]; i++) {
        Buf b = sounds[i].make();
        failures += write_wav(dir, sounds[i].name, &b);
        buf_free(&b);
    }
    return failures ? 1 : 0;
}
