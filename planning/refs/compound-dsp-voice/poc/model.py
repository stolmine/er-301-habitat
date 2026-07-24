#!/usr/bin/env python3
"""Systemic POC of the dual switched-capacitor filter (Bionic Lester).

Emergent-by-construction: we build the MECHANISMS (two switched-cap cores, each a
real clock; resonance-as-real-feedback; a saturating input; clock feedthrough) and
let the character (aliasing, clock combs, self-oscillation, breathing) fall out.
Nothing about the comb spacings / osc pitches / 16-corner map is hard-coded.

v5 architecture (2026-07-18): "clk src = both" is TWO cores cascaded, EACH clocked
by its OWN clock (A at f_A, B at f_B). This is what makes the comb spacing TRACK the
low clock (a core clocked at f_B S&H-images the signal into an f_B comb), the 2x
peak emerge at convergence (two identical cores stack), and the self-oscillation +
breathing emerge at divergence+high-res (each core oscillates at its own drifting
clock, the two beat). No gated/bolted-on oscillator - it all comes from the cascade.

NOT bit-exact. The clock law (cutoff->clock, ratio N) is coarse pending the hot-send
clock measurement; the goal is the emergent qualities, not the numbers. See
../synthesis.md.
"""
import numpy as np
from scipy.signal import lfilter

FS = 48000
DRIFT = 0.05           # analog clock drift depth (+/-5%) -> beats wander -> breathing
N_RATIO = 25.0         # switched-cap clock = cutoff * N (MEASURED 2026-07-18: N~25 fixed)
XCOUPLE = 0.06         # measured: each cutoff pulls the OTHER clock down ~6% (inverse)
FEEDTHRU = 0.03        # SC clock feedthrough into each core (-> resonant osc emerges)
SAT_D = 0.6667         # spiral saturator density -> bounds +/-1.5 (replaces 1.5*tanh)
GOV_AMT = 4.0          # soft-knee energy governor: extra damping when a core runs hot
GOV_KNEE = 0.25        # knee above the self-osc/comb level -> tames only the scream
GOV_ENV = 0.02         # per-tick |bp| envelope smoothing


def cutoff_hz(knob):   # knob 0..1 -> cutoff Hz (exp, saturates past ~0.8, like the hw)
    k = min(max(knob, 0.0), 1.0)
    return 30.0 * (5000.0 / 30.0) ** min(k / 0.8, 1.0)


def _softclip(x):      # odd-dominant symmetric clip (the measured distortion shape)
    return np.where(x > 1, 0.6667, np.where(x < -1, -0.6667, x - x**3 / 3.0))


def _spiral(x, d=SAT_D):   # Taylor-sin saturator (house/atoms/Spiral.h); bounds +/-1/d
    a = min(abs(x) * d, 1.5707963267948966)
    a2 = a * a
    s = a * (1.0 + a2 * (-1.0 / 6.0 + a2 * (1.0 / 120.0)))
    return (s / d) if x > 0.0 else -(s / d)


def _slow_drift(n, depth, seed, rate=0.99975):   # slow (~2 Hz) smooth analog wander
    r = np.random.default_rng(seed).standard_normal(n)
    d = lfilter([1.0 - rate], [1.0, -rate], r)
    return d / (d.std() + 1e-9) * depth


# mode -> shared SVF-feedback reconfiguration (measured: mode reshapes EVERY output,
# and shifts the self-osc). Applied identically to both cores (shared wiring).
MODE_Q = {0: 1.0, 1: 1.0, 2: 0.7, 3: 1.3, 4: 1.1, 5: 0.9}   # 2=hp,3=notch,4=ap,5=hidden


def _sc_core(x, fclk, drift, q, g, ft, mode, phase0=0.0):
    """One switched-capacitor core: a state-variable filter whose state advances ONLY
    on this core's clock ticks (sample-and-hold + zero-order hold). Because it only
    updates at the clock, its images/aliasing/comb fall out at the clock rate; the
    clock feedthrough seeds ringing; the resonance feedback makes it oscillate when
    driven unstable. The clock DRIFTS (analog wander) so beats between cores breathe."""
    n = len(x); out = np.empty(n)
    lp = bp = hp = held = env = 0.0
    ph = phase0
    inc = fclk / FS
    for i in range(n):
        ph += inc * (1.0 + drift[i])
        if ph >= 1.0:
            ph -= 1.0
            sq = 1.0 if ph < 0.5 else -1.0
            xin = x[i] + ft * sq                 # clock feedthrough seeds oscillation
            qe = q + (env - GOV_KNEE) * GOV_AMT if env > GOV_KNEE else q
            hp = xin - lp - qe * bp
            bp += g * hp
            bp = _spiral(bp)                     # bounded self-osc (spiral saturator)
            lp += g * bp
            lp = _spiral(lp)
            env += GOV_ENV * (abs(bp) - env)
            if mode == 0:   held = lp
            elif mode == 1: held = bp
            elif mode == 2: held = hp
            elif mode == 3: held = lp + hp           # notch
            elif mode == 4: held = lp - qe * bp + hp  # allpass-ish
            else:           held = hp + 0.3 * lp     # hidden (bright notch)
        out[i] = held
    return out


def _tap(mode, lp, bp, hp, q):
    if mode == 0:   return lp
    elif mode == 1: return bp
    elif mode == 2: return hp
    elif mode == 3: return lp + hp           # notch
    elif mode == 4: return lp - q * bp + hp  # allpass-ish
    return hp + 0.3 * lp                      # hidden (bright notch)


def _dual_core(x, fA, fB, dA, dB, q, g, ft, mode, kfb):
    """Both clocks: two SC cores cascaded (A->B) with a SHARED resonance loop (B's
    output fed back into A, gain kfb). The cascade gives the f_B comb; the shared loop
    is what SELF-OSCILLATES - and only exists when BOTH cores run, so single-clock
    never self-oscs (matches the module). Divergent clocks -> two pitches in the loop
    -> beating/breathing; converged -> one pitch -> clean. All emergent."""
    n = len(x); out = np.empty(n)
    lpA = bpA = hpA = heldA = envA = 0.0; phA = 0.0;  incA = fA / FS
    lpB = bpB = hpB = heldB = envB = 0.0; phB = 0.31; incB = fB / FS
    fbk = 0.0
    for i in range(n):
        phA += incA * (1.0 + dA[i])
        if phA >= 1.0:
            phA -= 1.0
            sqA = 1.0 if phA < 0.5 else -1.0
            xin = x[i] + ft * sqA + kfb * fbk           # shared resonance feedback
            qeA = q + (envA - GOV_KNEE) * GOV_AMT if envA > GOV_KNEE else q
            hpA = xin - lpA - qeA * bpA
            bpA += g * hpA; bpA = _spiral(bpA)
            lpA += g * bpA; lpA = _spiral(lpA)
            envA += GOV_ENV * (abs(bpA) - envA)
            heldA = _tap(mode, lpA, bpA, hpA, qeA)
        phB += incB * (1.0 + dB[i])
        if phB >= 1.0:
            phB -= 1.0
            sqB = 1.0 if phB < 0.5 else -1.0
            xin = heldA + ft * sqB
            qeB = q + (envB - GOV_KNEE) * GOV_AMT if envB > GOV_KNEE else q
            hpB = xin - lpB - qeB * bpB
            bpB += g * hpB; bpB = _spiral(bpB)
            lpB += g * bpB; lpB = _spiral(lpB)
            envB += GOV_ENV * (abs(bpB) - envB)
            heldB = _tap(mode, lpB, bpB, hpB, qeB)
        fbk = heldB
        out[i] = heldB
    return out


def render(x, cutA=0.5, cutB=0.5, res=0.2, gain=1.0, clksrc=0, mode=1, alias=0,
           N=N_RATIO, ft=FEEDTHRU):
    """clksrc: 0=A, 1=B, 2=both(two cores cascaded, each own clock).
    mode: 0 lp,1 bp,2 hp,3 notch,4 ap,5 hidden.  alias: 0 lo, 1 hi (HF smoothing)."""
    n = len(x)
    fca0, fcb0 = cutoff_hz(cutA), cutoff_hz(cutB)
    # measured inverse cross-coupling: the other channel's cutoff pulls this clock down
    fca = fca0 * (1.0 - XCOUPLE * cutB)
    fcb = fcb0 * (1.0 - XCOUPLE * cutA)
    fclkA = min(fca * N, FS * 0.49)
    fclkB = min(fcb * N, FS * 0.49)
    dA = _slow_drift(n, DRIFT, 1)
    dB = _slow_drift(n, DRIFT, 2)
    g = 2.0 * np.sin(np.pi / N)                     # SVF coeff -> cutoff = fclk/N
    q = max(0.004, 1.0 / (1.0 + res * 40.0)) * MODE_Q.get(mode, 1.0)
    xin = _softclip(gain * x).astype(np.float64)
    if clksrc == 0:
        y = _sc_core(xin, fclkA, dA, q, g, ft, mode)
    elif clksrc == 1:
        y = _sc_core(xin, fclkB, dB, q, g, ft, mode, phase0=0.31)
    else:
        # BOTH: two cascaded cores, each on its own drifting clock, plus a shared
        # resonance loop. Comb tracks the low clock; convergence stacks to the 2x
        # peak; the shared loop self-oscillates at high res (breathes at divergence,
        # clean at convergence). Self-osc onset: kfb engages only past res~0.7.
        kfb = max(0.0, (res - 0.7) / 0.3) * 0.95
        y = _dual_core(xin, fclkA, fclkB, dA, dB, q, g, ft, mode, kfb)
    if alias:                                       # HI = mild anti-alias smoothing
        y = lfilter([0.6, 0.4], [1.0], y)
    return y
