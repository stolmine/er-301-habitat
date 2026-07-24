#!/usr/bin/env python3
"""Generate the excitation signals for the compound DSP voice (dual switched-
capacitor filter) capture matrix. See capture-matrix.md.

All stereo (mono duplicated -> avoids PipeWire downmix halving), 48 kHz, peak
-1 dBFS (matches the locked send level in calibration.md; sweeps run at lower RMS
than a sine, which is fine for transfer-function work). Reproducible: the noise
burst uses a fixed seed.

    ess.wav   - Farina exponential sine sweep 20 Hz->20 kHz, 8 s. Transfer
                function per mode/cutoff/res; deconvolution (Farina) also splits
                harmonic-distortion orders into separate IRs.
    burst.wav - 1 ms Hann noise burst at t=100 ms, 1 s total. Resonance ringdown.
    shf.wav   - exponential tone sweep 2 kHz->20 kHz, 6 s. Exposes switched-cap
                aliasing fold-back (watch for descending images as the tone rises).
    s1k.wav   - steady 1 kHz sine, 6 s. Harmonic-distortion probe (gain sweeps).
    s110.wav  - steady 110 Hz sine, 6 s. Low-freq distortion / through-cutoff.

The distortion-vs-input-level ladder (cal_1k_rms-30..-06) comes from gen_tones.sh.
"""
import os
import numpy as np
from scipy.io import wavfile

FS = 48000
PEAK = 10 ** (-1.0 / 20)          # -1 dBFS
HERE = os.path.dirname(os.path.abspath(__file__))


def _fade(x, ms=10.0):
    n = int(ms / 1000.0 * FS)
    x[:n] *= np.linspace(0, 1, n)
    x[-n:] *= np.linspace(1, 0, n)
    return x


def _write(name, mono):
    mono = mono * (PEAK / (np.max(np.abs(mono)) + 1e-20))
    stereo = np.stack([mono, mono], axis=1)
    wavfile.write(os.path.join(HERE, name), FS, (stereo * 32767).astype(np.int16))
    print(f"  {name}  ({len(mono) / FS:.1f}s)")


def ess(f1=20.0, f2=20000.0, dur=8.0):
    """Farina exponential sine sweep."""
    N = int(dur * FS)
    t = np.arange(N) / FS
    L = dur / np.log(f2 / f1)
    x = np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1.0))
    return _fade(x)


def hf_sweep(f1=2000.0, f2=20000.0, dur=6.0):
    """Slow exponential tone sweep for aliasing fold-back exposure."""
    N = int(dur * FS)
    t = np.arange(N) / FS
    L = dur / np.log(f2 / f1)
    x = np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1.0))
    return _fade(x)


def sine(freq, dur=6.0):
    t = np.arange(int(dur * FS)) / FS
    return _fade(np.sin(2 * np.pi * freq * t))


def noise_burst(total=1.0, burst_ms=1.0, t_burst=0.1, seed=42):
    N = int(total * FS)
    x = np.zeros(N)
    bn = int(burst_ms / 1000.0 * FS)
    rng = np.random.RandomState(seed)
    s = int(t_burst * FS)
    x[s:s + bn] = rng.randn(bn) * np.hanning(bn)
    return x


if __name__ == '__main__':
    print('generating excitation signals in', HERE)
    _write('ess.wav', ess())
    _write('burst.wav', noise_burst())
    _write('shf.wav', hf_sweep())
    _write('s1k.wav', sine(1000.0))
    _write('s110.wav', sine(110.0))
    print('done. (level ladder cal_1k_rms-* comes from gen_tones.sh)')
