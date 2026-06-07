"""
measure.py -- recover Three Sisters' passband shape and Q(f) from recordings.

Two measurements:
  A) Exponential sine sweep (ESS) + Farina deconvolution -> impulse response
     -> complex transfer function (magnitude + phase). Harmonic distortion
     separates to negative time, so the linear IR is clean.
  B) Ping/decay -> Q at a given cutoff via Hilbert-envelope fit (Q = pi*f0*tau).

Plus a least-squares fitter that matches the ZDF block model (fc, q, q2) to a
measured magnitude curve, and a self-test that uses the model as a synthetic
device-under-test to prove the whole pipeline recovers known parameters.

Real-hardware workflow:
  1. write_sweep('sweep.wav')           # play this into the module input
  2. record the module output to out.wav at the same FS, same length
  3. f, mag, phase, ir = analyze_sweep('sweep.wav', 'out.wav')
  4. fc, q, q2 = fit_block_to_mag(f, mag, 'LOW', 'XOVER')
  For Q(f): ping the filter at each FREQ setting, record the ringdown, then
     f0, tau, Q = ping_q(ringdown)
"""
import numpy as np
from scipy.optimize import least_squares
from scipy.io import wavfile
from ts_model import FS, run_block, block_magnitude


# ---------- A) Exponential sine sweep ----------
def ess(f1=20.0, f2=20000.0, dur=8.0, fs=FS, amp=0.5):
    """Exponential sine sweep probe to play into the module."""
    N = int(dur * fs)
    t = np.arange(N) / fs
    L = dur / np.log(f2 / f1)
    sweep = np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1.0))
    return amp * sweep


def transfer_function(sweep, rec, fs=FS, reg=1e-5):
    """Regularized spectral division: H = conj(X)Y / (|X|^2 + eps).
    reg ~ 1e-5 for clean/high-SNR captures; raise toward 1e-3 for noisy ones.
    Robust to delay (linear phase). Returns f, |H|, unwrapped phase, ir."""
    n = 1 << int(np.ceil(np.log2(max(len(sweep), len(rec)) + 1)))
    X = np.fft.rfft(sweep, n)
    Y = np.fft.rfft(rec, n)
    eps = reg * np.max(np.abs(X) ** 2)
    H = np.conj(X) * Y / (np.abs(X) ** 2 + eps)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    ir = np.fft.irfft(H)
    return f, np.abs(H), np.unwrap(np.angle(H)), ir


def analyze_sweep(sweep_wav, out_wav):
    fs1, sweep = wavfile.read(sweep_wav)
    fs2, rec = wavfile.read(out_wav)
    sweep = _to_float(sweep); rec = _to_float(rec)
    return transfer_function(sweep, rec, fs1)


# ---------- B) Ping / decay -> Q ----------
def ping_q(ringdown, fs=FS, frame=128, db_lo=-5.0, db_hi=-35.0):
    """Q from ring decay via frame-RMS slope (no Hilbert edge artifacts).
    Q = pi*f0*tau, tau = amplitude 1/e decay time. Returns (f0, tau, Q)."""
    x = ringdown - np.mean(ringdown)
    fr = np.fft.rfftfreq(len(x), 1.0 / fs)
    F = np.abs(np.fft.rfft(x * np.hanning(len(x)))); F[fr < 30] = 0.0
    f0 = fr[np.argmax(F)]
    nf = len(x) // frame
    rms = np.sqrt(np.mean(x[:nf * frame].reshape(nf, frame) ** 2, axis=1))
    tfr = (np.arange(nf) * frame + frame / 2) / fs
    db = 20 * np.log10(rms / rms.max() + 1e-12)
    pk = np.argmax(rms)
    win = (np.arange(nf) >= pk) & (db <= db_lo) & (db >= db_hi)
    slope = np.polyfit(tfr[win], db[win], 1)[0]      # dB/sec
    tau = -20.0 / (np.log(10) * slope)
    return f0, tau, np.pi * f0 * tau


# ---------- least-squares fit of the block model to a measured magnitude ----------
def fit_block_to_mag(f, mag_meas, block, mode, fmin=20.0, fmax=20000.0,
                     fc0=800.0, q0=4.0, fit_q2=True):
    band = (f >= fmin) & (f <= fmax)
    fb = f[band]
    target = 20 * np.log10(mag_meas[band] / np.max(mag_meas[band]) + 1e-9)

    def model_db(fc, q, q2):
        # span fixed small for a single block; for CENTRE pass a ratio
        flo, fhi = fc / 1.0001, fc * 1.0001
        fm, m = block_magnitude(block, mode, flo, fhi, q, q2=q2, N=1 << 16)
        mi = np.interp(fb, fm, m)
        return 20 * np.log10(mi / np.max(mi) + 1e-9)

    def resid(p):
        fc, q = p[0], p[1]
        q2 = p[2] if fit_q2 else 0.7071
        return model_db(fc, q, q2) - target

    p0 = [fc0, q0] + ([0.7071] if fit_q2 else [])
    lo = [20.0, 0.4] + ([0.3] if fit_q2 else [])
    hi = [20000.0, 500.0] + ([2.0] if fit_q2 else [])
    r = least_squares(resid, p0, bounds=(lo, hi), method='trf')
    fc, q = r.x[0], r.x[1]
    q2 = r.x[2] if fit_q2 else 0.7071
    return fc, q, q2, r.cost


# ---------- helpers ----------
def _to_float(a):
    if a.ndim > 1: a = a[:, 0]
    if np.issubdtype(a.dtype, np.integer):
        a = a.astype(np.float64) / np.iinfo(a.dtype).max
    return a.astype(np.float64)


def write_sweep(path, dur=8.0, fs=FS):
    s = ess(dur=dur, fs=fs)
    wavfile.write(path, int(fs), (s * 0.9 * 32767).astype(np.int16))


# ---------- self-test: model as synthetic DUT ----------
def _self_test():
    fs = FS
    true_block, true_mode = 'LOW', 'XOVER'
    true_fc, true_q, true_q2 = 740.0, 5.0, 0.7071
    flo, fhi = true_fc / 1.0001, true_fc * 1.0001

    # A) sweep through the model, deconvolve, recover transfer function
    sweep = ess(dur=8.0, fs=fs)
    rec = run_block(true_block, true_mode, sweep, flo, fhi, true_q, q2=true_q2)
    f, mag, ph, ir = transfer_function(sweep, rec, fs)

    fr, magr = block_magnitude(true_block, true_mode, flo, fhi, true_q, q2=true_q2)
    magr_i = np.interp(f, fr, magr)
    band = (f > 50) & (f < 8000)
    a = 20 * np.log10(mag[band] / np.max(mag[band]) + 1e-9)
    b = 20 * np.log10(magr_i[band] / np.max(magr_i[band]) + 1e-9)
    print(f"[A] ESS deconvolution vs model magnitude: RMS err {np.sqrt(np.mean((a-b)**2)):.3f} dB")

    fc, q, q2, cost = fit_block_to_mag(f, mag, true_block, true_mode, fc0=500, q0=3)
    print(f"[A] fit recovered: fc={fc:.1f}Hz (true {true_fc})  "
          f"q={q:.2f} (true {true_q})  q2={q2:.3f} (true {true_q2})")

    # B) ping/decay -> Q, swept across cutoff to map Q(f). Single resonant SVF
    #    isolates the core decay (what dominates on hardware just below self-osc).
    from ts_model import SVF
    print("[B] ping-decay Q(f):")
    for fc_test, q_test in [(200, 8.0), (800, 8.0), (3000, 20.0)]:
        s = SVF(); N = int(0.5 * fs)
        ring = np.empty(N)
        for n in range(N):
            _, bp, _ = s.tick(1.0 if n == 0 else 0.0, fc_test, q_test)
            ring[n] = bp
        f0, tau, Qm = ping_q(ring, fs)
        print(f"    set fc={fc_test:>5}Hz Q={q_test}: "
              f"measured f0={f0:6.1f}Hz  tau={tau*1e3:5.2f}ms  Q={Qm:5.2f}")


if __name__ == '__main__':
    _self_test()
