"""Regenerate the excitation WAVs (sweep + impulse burst) used for the
Three Sisters hardware capture session.

Both stereo, 48 kHz, peak −1 dBFS. Mono → stereo duplication avoids
pipewire's half-amplitude downmix when playing to a 2-channel sink.
Reproducible (impulse uses fixed seed).
"""
import numpy as np
from scipy.io import wavfile

FS = 48000


def make_sweep(path, dur=8.0, f1=20.0, f2=20000.0):
    """Farina exponential sine sweep (ESS) — 20Hz→20kHz over 8s by
    default. 10ms Hann fade in/out to suppress click artifacts at
    the sample boundaries."""
    N = int(dur * FS)
    t = np.arange(N) / FS
    L = dur / np.log(f2 / f1)
    sweep = np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1.0))
    fade_n = int(0.01 * FS)
    sweep[:fade_n] *= np.linspace(0, 1, fade_n)
    sweep[-fade_n:] *= np.linspace(1, 0, fade_n)
    peak_target = 10 ** (-1.0 / 20)
    sweep = sweep * (peak_target / np.max(np.abs(sweep)))
    stereo = np.stack([sweep, sweep], axis=1)
    wavfile.write(path, FS, (stereo * 32767).astype(np.int16))
    return sweep


def make_impulse(path, total_dur=1.0, burst_dur_ms=1.0, t_burst=0.1, seed=42):
    """1ms Hann-windowed white-noise burst at t=100ms by default.
    Broadband impulse for filter ringdown excitation."""
    N = int(total_dur * FS)
    x = np.zeros((N, 2))
    burst_n = int(burst_dur_ms / 1000.0 * FS)
    rng = np.random.RandomState(seed)
    burst = rng.randn(burst_n) * np.hanning(burst_n)
    peak_target = 10 ** (-1.0 / 20)
    burst = burst * (peak_target / np.max(np.abs(burst)))
    start = int(t_burst * FS)
    x[start:start + burst_n, 0] = burst
    x[start:start + burst_n, 1] = burst
    wavfile.write(path, FS, (x * 32767).astype(np.int16))
    return burst


if __name__ == '__main__':
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    make_sweep(os.path.join(here, 'sweep.wav'))
    make_impulse(os.path.join(here, 'impulse.wav'))
    print('regenerated sweep.wav + impulse.wav in', here)
