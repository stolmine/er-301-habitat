/* three_sisters_svf.c
 *
 * Structural model of a Mannequins Three Sisters filter block.
 * Core: Cytomic / Zavalishin TPT (zero-delay-feedback) state-variable filter.
 * ZDF chosen over Chamberlin for stability under audio-rate FREQ/SPAN
 * modulation (the CENTRE->SPAN 2-op FM self-patch) and high Q.
 *
 * Q-coupling, taken from the official block diagrams:
 *   LOW / HIGH : resonance on SVF1 only; SVF2 is fixed-damping (one peak).
 *   CENTRE     : resonance on BOTH SVFs (two peaks in xover, coincident in formant).
 *
 * Single precision throughout (ARMv7 NEON / float-friendly).
 */

#include <math.h>

#define TS_PI 3.14159265358979f

/* ---- one ZDF SVF: produces lp/bp/hp in a single tick ---- */
typedef struct {
    float ic1eq, ic2eq;   /* integrator states */
} svf_t;

typedef struct { float lp, bp, hp; } svf_out_t;

static inline void svf_reset(svf_t *s) { s->ic1eq = s->ic2eq = 0.0f; }

/* fc in Hz, fs in Hz, Q is filter quality (0.5 = Butterworth-ish damping,
 * higher = more resonance). Coefficients recomputed per call so cutoff and
 * Q can move at audio rate. */
static inline svf_out_t svf_tick(svf_t *s, float v0, float fc, float fs, float Q)
{
    float g  = tanf(TS_PI * fc / fs);
    float k  = 1.0f / Q;                 /* k -> 0 == self-oscillation edge */
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;

    float v3 = v0 - s->ic2eq;
    float v1 = a1 * s->ic1eq + a2 * v3;
    float v2 = s->ic2eq + a2 * s->ic1eq + a3 * v3;
    s->ic1eq = 2.0f * v1 - s->ic1eq;
    s->ic2eq = 2.0f * v2 - s->ic2eq;

    svf_out_t o;
    o.bp = v1;
    o.lp = v2;
    o.hp = v0 - k * v1 - v2;
    return o;
}

/* Nonlinear variant for the resonant stages: soft-clips the bandpass
 * integrator state so that very high Q settles into a bounded sine limit
 * cycle instead of blowing up. This is the cheap route (saturate the state);
 * the higher-fidelity option is one Newton iteration on an implicit tanh in
 * the feedback (Simper, "Solving the continuous SVF equations..."). The
 * tanh keeps the self-osc tone near-sinusoidal -> the low-distortion sig. */
static inline svf_out_t svf_tick_nl(svf_t *s, float v0, float fc, float fs, float Q)
{
    float g  = tanf(TS_PI * fc / fs);
    float k  = 1.0f / Q;
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;

    float v3 = v0 - s->ic2eq;
    float v1 = a1 * s->ic1eq + a2 * v3;
    float v2 = s->ic2eq + a2 * s->ic1eq + a3 * v3;
    s->ic1eq = tanhf(2.0f * v1 - s->ic1eq);   /* bounded bp state -> limit cycle */
    s->ic2eq = 2.0f * v2 - s->ic2eq;

    svf_out_t o;
    o.bp = v1;
    o.lp = v2;
    o.hp = v0 - k * v1 - v2;
    return o;
}

/* ---- FREQ / SPAN -> per-block cutoff in Hz ----
 * Three Sisters tracks 1V/oct (calibration: +2V == 4x freq). FREQ and SPAN
 * are both exponential. Supply octave offsets (knob octaves + CV volts,
 * 1V == 1 oct) relative to a chosen center frequency f0 (the noon/0V pitch).
 *   which: -1 -> LOW (FREQ-SPAN), +1 -> HIGH (FREQ+SPAN), 0 -> CENTRE/FREQ */
#define TS_F0 110.0f      /* center pitch at 0 octaves; tune to taste        */
static inline float ts_cutoff_hz(float freq_oct, float span_oct, int which)
{
    return TS_F0 * exp2f(freq_oct + (float)which * span_oct);
}

/* ---- block-level enums ---- */
typedef enum { TS_XOVER, TS_FORMANT } ts_mode_t;
typedef enum { TS_LOW, TS_HIGH, TS_CENTRE } ts_block_t;

typedef struct {
    svf_t   s1, s2;
    float   q2_fixed;     /* fixed damping of the non-resonant 2nd stage     */
} ts_block_state_t;

static inline void ts_block_init(ts_block_state_t *b)
{
    svf_reset(&b->s1);
    svf_reset(&b->s2);
    b->q2_fixed = 0.7071f;            /* Butterworth default; tune to taste  */
}

/* fc_low/fc_high are the per-block cutoffs already derived from FREQ/SPAN:
 *   LOW    block -> fc = FREQ - SPAN
 *   HIGH   block -> fc = FREQ + SPAN
 *   CENTRE block -> fc_low = FREQ - SPAN (SVF1 hp), fc_high = FREQ + SPAN (SVF2 lp);
 *                   in FORMANT both collapse to FREQ.
 *
 * q is the QUALITY-derived resonance (clockwise half). anti is the CCW
 * anti-resonance amount in [0,1] driving the complementary-mix VCA.
 *
 * Returns the block output (main + anti-resonance complement).
 */
static inline float ts_block_tick(ts_block_state_t *b, ts_block_t blk,
                                   ts_mode_t mode, float in,
                                   float fc_low, float fc_high,
                                   float fs, float q, float anti)
{
    svf_out_t o1, o2;
    float main_out = 0.0f, comp = 0.0f;

    switch (blk) {
    case TS_LOW:
        /* SVF1 resonant lp -> SVF2 (fixed) lp(xover) / hp(formant) */
        o1 = svf_tick_nl(&b->s1, in,    fc_low, fs, q);
        o2 = svf_tick(&b->s2, o1.lp, fc_low, fs, b->q2_fixed);
        main_out = (mode == TS_XOVER) ? o2.lp : o2.hp;
        comp     = o1.hp;                       /* anti-res mixes SVF1 hp     */
        break;

    case TS_HIGH:
        /* SVF1 resonant hp -> SVF2 (fixed) hp(xover) / lp(formant) */
        o1 = svf_tick_nl(&b->s1, in,    fc_high, fs, q);
        o2 = svf_tick(&b->s2, o1.hp, fc_high, fs, b->q2_fixed);
        main_out = (mode == TS_XOVER) ? o2.hp : o2.lp;
        comp     = o1.lp;                       /* anti-res mixes SVF1 lp     */
        break;

    case TS_CENTRE: {
        /* BOTH stages resonant. hp@lower -> lp@upper.
         * formant: both cutoffs == FREQ (pass FREQ in fc_low==fc_high). */
        float clo = fc_low;
        float chi = (mode == TS_XOVER) ? fc_high : fc_low;
        o1 = svf_tick_nl(&b->s1, in,    clo, fs, q);
        o2 = svf_tick_nl(&b->s2, o1.hp, chi, fs, q);
        main_out = o2.lp;
        comp     = o1.lp + o2.hp;               /* SVF1 lp + SVF2 hp          */
        break;
    }
    }

    return main_out + anti * comp;
}

/* NOTES / TODO for a faithful port:
 *
 * 1. Self-oscillation: a purely linear ZDF SVF always decays (k>0). To get the
 *    clean sustained sine of the real unit, drive q past a threshold and add a
 *    soft saturator on the bp state inside svf_tick (e.g. v1 = tanhf(v1)) to
 *    set the limit-cycle amplitude. tanh keeps the self-osc tone near-sinusoidal,
 *    which is the whole low-distortion signature.
 *
 * 2. Q-vs-frequency: forum + calibration notes say Q tracks unevenly across
 *    pitch. If you want that, make q a function of fc rather than a constant.
 *
 * 3. q2_fixed is the unknown analog constant. 0.707 (Butterworth) gives a clean
 *    4-pole; lower values flatten the second stage and let SVF1's peak dominate
 *    more. Match against a measured sweep before committing.
 *
 * 4. FREQ/SPAN are exponential (v/oct). Convert to Hz before calling:
 *    fc = f0 * exp2f(freq_volts +/- span_volts).
 *
 * 5. ALL(in) = sum into every block input; ALL(out) = equal sum of the three
 *    block outputs (watch headroom — the real ALL clip is rail-sum, not core).
 */
