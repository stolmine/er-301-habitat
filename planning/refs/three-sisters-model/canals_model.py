"""Python mirror of stolmine::Canals (spreadsheet 2.7.1.7 DSP).

Use this to predict how our shipping C++ Canals responds without
needing to capture from the emulator. Compare against hardware
captures to find DSP-formula deltas.

Mirrors:
  mods/spreadsheet/SistersSvf.h (setFreq + process + fastTanh)
  mods/spreadsheet/Canals.cpp (Q knob curve, block routing,
                               anti-res topology, fader morph)

If you change the C++ DSP, mirror the change here too so the
predictions stay accurate.
"""
import math
import numpy as np

FS = 48000.0
PI = math.pi


# ---------- Per-SVF (mirrors SistersSvf::process/setFreq) ----------

def fast_tanh(x):
    """Padé[3/2] tanh — matches C++ fastTanh()."""
    x2 = x * x
    return x * (27.0 + x2) / (27.0 + 9.0 * x2)


class CanalsSvf:
    """Mirrors stolmine::SistersSvf."""
    __slots__ = ('s1', 's2', 'g', 'r', 'h')

    def __init__(self):
        self.s1 = 0.0
        self.s2 = 0.0
        self.g = 0.0
        self.r = 0.0
        self.h = 0.0

    def set_freq(self, normalized_freq, damping):
        pif = PI * normalized_freq
        self.g = pif * (1.0 + pif * pif * 0.333333)
        self.r = damping
        self.h = 1.0 / (1.0 + self.r * self.g + self.g * self.g)

    def process(self, x):
        hp = (x - self.r * self.s1 - self.g * self.s1 - self.s2) * self.h
        bp = self.g * hp + self.s1
        lp = self.g * bp + self.s2
        # In-loop K-stretched tanh: K * fastTanh(x/K). K=1.5 calibrated
        # to allow larger limit cycles that trigger rail-clip on ALL.
        K = 1.5
        self.s1 = K * fast_tanh((self.g * hp + bp) / K)
        self.s2 = self.g * bp + lp
        return lp, bp, hp


# ---------- Knob mappings (mirrors Canals.cpp) ----------

def quality_to_damping(quality):
    """Canals.cpp Q knob → damping curve.
    quality ∈ [-1, +1]. Negative half: Butterworth + antiRes.
    Positive half: cubic up to q≈50; top decile crosses through zero
    into slight negative for self-oscillation."""
    if quality < 0.0:
        return 1.0 / 0.7071
    elif quality < 0.9:
        t = quality * (1.0 / 0.9)
        q_mag = 0.7071 + t * t * t * 49.3
        return 1.0 / q_mag
    else:
        # Calibrated -0.5 at full CW after internal ER-301 captures
        # (not MOTU-attenuated). Hardware self-osc is much louder than
        # the earlier -0.075 produced. See planning/refs/three-sisters-
        # hardware/internal/.
        t = (quality - 0.9) * 10.0
        return 0.02 * (1.0 - t) + (-0.5) * t


def semis_to_ratio(semis):
    return 2.0 ** (semis / 12.0)


def clamp_norm(hz):
    f = hz / FS
    return max(0.001, min(0.499, f))


def derive_cutoffs(fundamental, span, voct=0.0):
    """Returns (low_f, ctr_f, high_f) normalized cutoffs."""
    total_semis = voct * 120.0 + fundamental
    freq_hz = 261.63 * semis_to_ratio(total_semis)
    freq_hz = max(20.0, min(20000.0, freq_hz))
    span_semis = span * 48.0
    low_hz = max(20.0, min(20000.0, freq_hz / semis_to_ratio(span_semis)))
    high_hz = max(20.0, min(20000.0, freq_hz * semis_to_ratio(span_semis)))
    return clamp_norm(low_hz), clamp_norm(freq_hz), clamp_norm(high_hz)


# ---------- Block routing + per-sample processing ----------

class CanalsState:
    """Mirrors Canals::Internal."""
    def __init__(self):
        self.low1 = CanalsSvf()
        self.low2 = CanalsSvf()
        self.ctr1 = CanalsSvf()
        self.ctr2 = CanalsSvf()
        self.hi1 = CanalsSvf()
        self.hi2 = CanalsSvf()


def configure(state, mode, fundamental, span, quality, voct=0.0):
    """Apply param-derived coefs to all 6 SVFs. Returns antiRes scalar."""
    damping = quality_to_damping(quality)
    anti_res = max(0.0, -quality)
    low_f, ctr_f, high_f = derive_cutoffs(fundamental, span, voct)
    butter_damp = 1.0 / 0.7071
    # FIDELITY FIX 1: SVF2 Butterworth on ALL three blocks.
    # FIDELITY FIX 2: Frequency-compensated damping per resonant
    #   stage frequency, with f_ref = ctr_f. Compensates for the
    #   SVF's frequency-dependent self-osc amplitude scaling. Brings
    #   per-block self-osc amplitudes to hardware-matching pattern
    #   (CTR > LOW ≈ HIGH).
    def comp(f):
        ratio = ctr_f / max(f, 0.001)
        return damping * min(ratio, 4.0)
    if mode == 'XOVER':
        state.low1.set_freq(low_f, comp(low_f))
        state.low2.set_freq(low_f, butter_damp)
        state.ctr1.set_freq(low_f, comp(low_f))    # CENTRE SVF1 at lowF
        state.ctr2.set_freq(high_f, butter_damp)
        state.hi1.set_freq(high_f, comp(high_f))
        state.hi2.set_freq(high_f, butter_damp)
    else:  # FORMANT
        state.low1.set_freq(low_f, comp(low_f))
        state.low2.set_freq(low_f, butter_damp)
        state.ctr1.set_freq(ctr_f, comp(ctr_f))    # = damping (no comp)
        state.ctr2.set_freq(ctr_f, butter_damp)
        state.hi1.set_freq(high_f, comp(high_f))
        state.hi2.set_freq(high_f, butter_damp)
    return anti_res


def process_block(state, mode, x_array, anti_res):
    """Process audio, return (low_out, ctr_out, hi_out)."""
    N = len(x_array)
    low_out = np.zeros(N)
    ctr_out = np.zeros(N)
    hi_out = np.zeros(N)

    # Per-block POST-GAIN compensation (LOW × 2.0, HIGH × 1.8)
    # offsets the dual-LP / dual-HP cascade attenuation (~6 dB at fc)
    # that CTR's HP→LP doesn't suffer. Calibrated to hardware.
    LOW_GAIN = 2.0
    HIGH_GAIN = 1.8

    if mode == 'XOVER':
        for i in range(N):
            x = x_array[i]
            lo1_lp, _, lo1_hp = state.low1.process(x)
            lo2_lp, _, _ = state.low2.process(lo1_lp)
            low_out[i] = (lo2_lp + anti_res * lo1_hp) * LOW_GAIN

            ct1_lp, _, ct1_hp = state.ctr1.process(x)
            ct2_lp, _, ct2_hp = state.ctr2.process(ct1_hp)
            ctr_out[i] = ct2_lp + anti_res * (ct1_lp + ct2_hp)

            hi1_lp, _, hi1_hp = state.hi1.process(x)
            _, _, hi2_hp = state.hi2.process(hi1_hp)
            hi_out[i] = (hi2_hp + anti_res * hi1_lp) * HIGH_GAIN
    else:  # FORMANT (HIGH stage order swapped per Issue #8)
        for i in range(N):
            x = x_array[i]
            lo1_lp, _, lo1_hp = state.low1.process(x)
            _, _, lo2_hp = state.low2.process(lo1_lp)
            low_out[i] = (lo2_hp + anti_res * lo1_hp) * LOW_GAIN

            ct1_lp, _, ct1_hp = state.ctr1.process(x)
            ct2_lp, _, ct2_hp = state.ctr2.process(ct1_hp)
            ctr_out[i] = ct2_lp + anti_res * (ct1_lp + ct2_hp)

            hi1_lp, _, hi1_hp = state.hi1.process(x)
            hi2_lp, _, _ = state.hi2.process(hi1_hp)
            hi_out[i] = (hi2_lp + anti_res * hi1_lp) * HIGH_GAIN

    return low_out, ctr_out, hi_out


def fader_morph(pos, low_out, ctr_out, hi_out):
    """Mirror Canals.cpp Out 1 fader weights + rail-clip saturation.
    pos ∈ [2, 3] morphs HIGH → unweighted LOW+CENTRE+HIGH sum (3x
    peak at full ALL). fastTanh on the output emulates the hardware
    rail-sum distortion character."""
    if pos <= 1.0:
        wL, wC, wH = 1.0 - pos, pos, 0.0
    elif pos <= 2.0:
        wL, wC, wH = 0.0, 2.0 - pos, pos - 1.0
    else:
        t = pos - 2.0
        wL, wC, wH = t, t, 1.0
    mix = low_out * wL + ctr_out * wC + hi_out * wH
    # Soft saturation (rail-clip emulation) — Padé tanh, same curve
    # as the in-filter sat. At low fader positions, mix is bounded ≤1
    # and the curve is near-transparent.
    mix2 = mix * mix
    return mix * (27.0 + mix2) / (27.0 + 9.0 * mix2)


# ---------- Top-level run_canals ----------

def run_canals(mode, x_array, fundamental, span, quality,
               output_pos=0.0, voct=0.0, output_select='out',
               selfosc_kick=0.0):
    """Run a full Canals simulation.

    output_select: 'out' (fader morph), 'low', 'centre', 'high'.
    selfosc_kick: nonzero to seed CENTRE SVF1 state, for simulating
        self-oscillation in silent input. (C++ relies on denormal
        noise; Python float64 needs an explicit kick.)
    """
    state = CanalsState()
    anti_res = configure(state, mode, fundamental, span, quality, voct)
    if selfosc_kick != 0.0:
        state.ctr1.s1 = selfosc_kick
        state.low1.s1 = selfosc_kick
        state.hi1.s1 = selfosc_kick
    low_out, ctr_out, hi_out = process_block(state, mode, x_array, anti_res)
    if output_select == 'low':
        return low_out
    elif output_select == 'centre':
        return ctr_out
    elif output_select == 'high':
        return hi_out
    else:
        return fader_morph(output_pos, low_out, ctr_out, hi_out)


# ---------- Quick sanity tests when run as script ----------

if __name__ == '__main__':
    # Sanity: feed impulse, verify it doesn't explode
    x = np.zeros(4800)
    x[0] = 1.0
    for mode in ('XOVER', 'FORMANT'):
        for out in ('low', 'centre', 'high'):
            y = run_canals(mode, x, fundamental=0.0, span=0.25,
                           quality=0.5, output_select=out)
            print(f"{mode} {out}: peak={np.abs(y).max():.4f}, "
                  f"sum={y.sum():.4f}")

    # Self-osc test: silence input + quality=1.0 + kick
    silence = np.zeros(48000)
    for q in (0.85, 0.92, 0.96, 1.0):
        y = run_canals('XOVER', silence, fundamental=0.0, span=0.25,
                       quality=q, output_select='centre', selfosc_kick=0.1)
        steady_state_amp = np.abs(y[-4800:]).max()
        print(f"Self-osc Q={q:.2f}: steady-state peak={steady_state_amp:.4f}")
