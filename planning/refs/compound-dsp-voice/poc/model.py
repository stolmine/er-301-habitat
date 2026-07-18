#!/usr/bin/env python3
"""Systemic POC of the dual switched-capacitor filter (Bionic Lester).

Emergent-by-construction: we build the MECHANISMS (a switched-cap clock, an XOR
clock-mux, resonance-as-real-feedback, a saturating input, clock feedthrough) and
let the character (aliasing, clock combs, self-oscillation) fall out. Nothing about
the comb spacings / osc pitches / 16-corner map is hard-coded - they emerge from
the clock interaction. See ../synthesis.md.

NOT bit-exact. The clock law (cutoff->clock freq, ratio N) is coarse pending the
hot-send clock measurement; the goal is the emergent qualities, not the numbers.
"""
import numpy as np

FS = 48000
N_RATIO = 8.0          # switched-cap clock = cutoff * N (coarse; sets aliasing character)
FEEDTHRU = 0.03        # SC clock feedthrough into the filter (-> resonant osc emerges)


def cutoff_hz(knob):   # knob 0..1 -> cutoff Hz (exp, saturates past ~0.8, like the hw)
    k = min(max(knob, 0.0), 1.0)
    return 30.0 * (5000.0 / 30.0) ** min(k / 0.8, 1.0)


def _softclip(x):      # odd-dominant symmetric clip (the measured distortion shape)
    return np.where(x > 1, 0.6667, np.where(x < -1, -0.6667, x - x**3 / 3.0))


def _clock(fclk, n, phase0=0.0):   # bipolar square at fclk, sampled at FS
    ph = (np.arange(n) * (fclk / FS) + phase0) % 1.0
    return np.where(ph < 0.5, 1.0, -1.0)


def _svf_loop(x, clk, ticks, g, q, ft, mode, alias):
    """SVF running at the SC clock: updates only on clock ticks (S&H + ZOH), so
    aliasing/imaging fall out at low clock; resonance feedback + clock feedthrough
    make it ring/oscillate. Pure-python inner loop (no numba)."""
    n = len(x); out = np.empty(n)
    lp = bp = hp = held = yp = 0.0
    for i in range(n):
        if ticks[i]:
            xin = x[i] + ft * clk[i]            # clock feedthrough (seeds oscillation)
            hp = xin - lp - q * bp
            bp += g * hp
            bp = 1.5 * np.tanh(bp * 0.6667)     # soft feedback limiter -> bounded self-osc
            lp += g * bp
            lp = 1.5 * np.tanh(lp * 0.6667)
            if mode == 0: held = lp
            elif mode == 1: held = bp
            elif mode == 2: held = hp
            elif mode == 3: held = lp + hp          # notch
            else: held = lp - q * bp + hp           # allpass-ish
        y = held
        if alias:                                   # HI = mild anti-alias smoothing
            y = 0.6 * y + 0.4 * yp
        yp = y
        out[i] = y
    return out


def render(x, cutA=0.5, cutB=0.5, res=0.2, gain=1.0, clksrc=0, mode=1, alias=0,
           N=N_RATIO, ft=FEEDTHRU):
    """clksrc: 0=A, 1=B, 2=both(XOR). mode: 0 lp,1 bp,2 hp,3 notch,4 ap. alias:0 lo,1 hi."""
    n = len(x)
    fca, fcb = cutoff_hz(cutA), cutoff_hz(cutB)
    fclkA = min(fca * N, FS * 0.49)
    fclkB = min(fcb * N, FS * 0.49)
    sqA, sqB = _clock(fclkA, n), _clock(fclkB, n, phase0=0.31)  # B offset -> XOR non-degenerate
    if clksrc == 0: clk = sqA
    elif clksrc == 1: clk = sqB
    else: clk = np.where((sqA > 0) ^ (sqB > 0), 1.0, -1.0)   # XOR of the two clocks
    ticks = np.zeros(n, bool)
    hi = clk > 0
    ticks[1:] = hi[1:] & (~hi[:-1]); ticks[0] = hi[0]
    g = 2.0 * np.sin(np.pi / N)                     # SVF coeff -> cutoff = fclk/N
    # res 0..1 -> damping (Q ~1..40); strong ring at max res, stable (no forced osc).
    # NOTE: true self-osc + clock combs do NOT emerge from this simplified model -
    # they need the exact SC clock law + a faithful switched-cap feedback model.
    q = max(0.004, 1.0 / (1.0 + res * 40.0))
    xin = _softclip(gain * x).astype(np.float64)
    y = _svf_loop(xin, clk.astype(np.float64), ticks, g, q, ft, mode, alias)
    return y
