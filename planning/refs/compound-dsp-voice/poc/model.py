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
from scipy.signal import lfilter

FS = 48000
DRIFT = 0.05           # analog clock drift depth (+/-5%) -> beat wanders -> breathing
N_RATIO = 25.0         # switched-cap clock = cutoff * N (MEASURED 2026-07-18: N~25 fixed)
XCOUPLE = 0.06         # measured: each cutoff pulls the OTHER clock down ~6% (inverse)
FEEDTHRU = 0.03        # SC clock feedthrough into the filter (-> resonant osc emerges)


def cutoff_hz(knob):   # knob 0..1 -> cutoff Hz (exp, saturates past ~0.8, like the hw)
    k = min(max(knob, 0.0), 1.0)
    return 30.0 * (5000.0 / 30.0) ** min(k / 0.8, 1.0)


def _softclip(x):      # odd-dominant symmetric clip (the measured distortion shape)
    return np.where(x > 1, 0.6667, np.where(x < -1, -0.6667, x - x**3 / 3.0))


def _slow_drift(n, depth, seed, rate=0.99975):   # slow (~2 Hz) smooth analog wander
    r = np.random.default_rng(seed).standard_normal(n)
    d = lfilter([1.0 - rate], [1.0, -rate], r)
    return d / (d.std() + 1e-9) * depth


def _clock(fclk, n, phase0=0.0, drift=None):   # bipolar square; drift = per-sample freq wander
    inc = fclk / FS
    if drift is not None:
        ph = (np.cumsum(inc * (1.0 + drift)) + phase0) % 1.0
    else:
        ph = (np.arange(n) * inc + phase0) % 1.0
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


# mode -> shared SVF-feedback reconfiguration (measured: HP self-osc ~2x hidden,
# and mode reshapes all outputs). fcar = self-osc carrier scale; q = damping scale.
MODE_FCAR = {0: 1.0, 1: 1.0, 2: 3.6, 3: 2.2, 4: 2.8, 5: 1.0}   # 2=hp,3=notch,4=ap,5=hidden
MODE_Q    = {0: 1.0, 1: 1.0, 2: 0.7, 3: 1.3, 4: 1.1, 5: 0.9}


def _clock_osc(clk, fcar, res):
    """Clock-domain self-oscillation with the CLOCK<->FILTER CLOSED LOOP (measured
    2026-07-18: the osc BREATHES - comb morphs continuously, carrier wanders +/-26%).
    A resonant path at the clock carrier, fed by the XOR clock, whose center frequency
    is SELF-MODULATED by a slow envelope of its own output -> the loop drifts/breathes
    (chaotic) instead of sitting still. Bounded by soft saturation. Carrier = A's clock
    (windows); XOR beat -> sidebands; self-FM -> the breathing."""
    n = len(clk); out = np.zeros(n)
    fbase = min(fcar, FS * 0.40)
    q = max(-0.006, (1.0 - res) * 0.45)      # res->1 : q->~0 (instability threshold)
    lp = bp = env = env2 = 0.0
    for i in range(n):
        # two nested slow envelopes at different rates -> non-settling chaotic drift
        fmod = fbase * (1.0 + 0.12 * env - 0.08 * env * env2)   # mild self-FM (drift is primary)
        g = 2.0 * np.sin(np.pi * min(max(fmod, 20.0), FS * 0.46) / FS)
        hp = clk[i] - lp - q * bp
        bp += g * hp; bp = 1.2 * np.tanh(bp * 0.8333)
        lp += g * bp; lp = 1.2 * np.tanh(lp * 0.8333)
        out[i] = bp
        env += 0.00010 * (5.0 * bp * bp - env)                 # fast-ish energy env
        env2 += 0.000018 * (env - env2)                        # slower env of the env
    return out


def render(x, cutA=0.5, cutB=0.5, res=0.2, gain=1.0, clksrc=0, mode=1, alias=0,
           N=N_RATIO, ft=FEEDTHRU):
    """clksrc: 0=A, 1=B, 2=both(XOR). mode: 0 lp,1 bp,2 hp,3 notch,4 ap. alias:0 lo,1 hi."""
    n = len(x)
    fca0, fcb0 = cutoff_hz(cutA), cutoff_hz(cutB)
    # measured inverse cross-coupling: the other channel's cutoff pulls this clock down
    fca = fca0 * (1.0 - XCOUPLE * cutB)
    fcb = fcb0 * (1.0 - XCOUPLE * cutA)
    fclkA = min(fca * N, FS * 0.49)
    fclkB = min(fcb * N, FS * 0.49)
    # two INDEPENDENTLY DRIFTING analog clocks -> their beat wanders -> the self-osc
    # comb morphs/breathes (measured); B phase-offset keeps XOR non-degenerate.
    dA, dB = _slow_drift(n, DRIFT, 1), _slow_drift(n, DRIFT, 2)
    sqA, sqB = _clock(fclkA, n, drift=dA), _clock(fclkB, n, phase0=0.31, drift=dB)
    if clksrc == 0: clk = sqA
    elif clksrc == 1: clk = sqB
    else: clk = np.where((sqA > 0) ^ (sqB > 0), 1.0, -1.0)   # XOR of the two clocks
    ticks = np.zeros(n, bool)
    hi = clk > 0
    ticks[1:] = hi[1:] & (~hi[:-1]); ticks[0] = hi[0]
    g = 2.0 * np.sin(np.pi / N)                     # SVF coeff -> cutoff = fclk/N
    # res 0..1 -> damping (Q ~1..40); mode scales it (shared feedback reconfig ->
    # reshapes every output, measured 2026-07-18).
    q = max(0.004, 1.0 / (1.0 + res * 40.0)) * MODE_Q.get(mode, 1.0)
    xin = _softclip(gain * x).astype(np.float64)
    y = _svf_loop(xin, clk.astype(np.float64), ticks, g, q, ft, mode, alias)
    # clock-domain self-oscillation (measured mechanism): needs clk=both + high res
    # + DIVERGENT clocks. Converged XOR is a clean 2x tone (no interference) -> no osc.
    diverge = abs(cutA - cutB)
    if clksrc == 2 and res > 0.8 and diverge > 0.3:
        # mode scales the self-osc carrier (feedback picks the oscillating mode)
        co = _clock_osc(clk.astype(np.float64), fca * N * MODE_FCAR.get(mode, 1.0), res)
        y = y + 0.6 * co * min(1.0, (diverge - 0.3) / 0.4)   # scale by divergence
    return y
