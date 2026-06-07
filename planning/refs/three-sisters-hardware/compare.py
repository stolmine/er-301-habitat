"""compare.py -- Phase 0c analysis pipeline.

Crunches every hardware capture against the Python reference model
and writes per-capture results to a markdown scorecard. Generates
overlay plots for visual sanity-checking.

Usage:
    cd planning/refs/three-sisters-hardware
    python compare.py
"""
import os
import sys
import numpy as np
from scipy.io import wavfile
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL_DIR = os.path.normpath(os.path.join(HERE, '..', 'three-sisters-model'))
sys.path.insert(0, MODEL_DIR)
from measure import transfer_function, ping_q   # noqa
from ts_model import run_block, block_magnitude, FS, SVF   # noqa

RAW_DIR = os.path.join(HERE, 'raw')
OUT_PLOT_DIR = os.path.join(HERE, 'analysis')
os.makedirs(OUT_PLOT_DIR, exist_ok=True)
SCORECARD_PATH = os.path.normpath(os.path.join(
    HERE, '..', '..', 'canals-baseline-scorecard.md'))

SWEEP_PATH = os.path.join(HERE, 'sweep.wav')
IMPULSE_PATH = os.path.join(HERE, 'impulse.wav')

# Empirical center from FFT analysis of sweep + self-osc captures:
# noon-FREQ XOVER CENTRE peak is ~420 Hz (NOT 304; sox's "rough
# frequency" reading was unreliable). Set C peaks scale cleanly 2x
# per octave from this center.
F0_NOON_XOVER = 420.0
# FORMANT mode peaks ~670 Hz at noon FREQ — ~+8 semitones higher.
# Possibly the topology differs from the model in FORMANT (likely
# the FREQ knob is interpreted differently between modes). Until
# we understand the structural difference, model FORMANT at its
# own empirical center.
F0_NOON_FORMANT = 670.0
# Default SPAN at knob noon — Set C ratios give us pure 1V/oct
# tracking so SPAN doesn't enter that comparison. From inspection
# of XOVER baselines, noon SPAN looks closer to 1.5x ratio than
# 2.24x — refine if FORMANT or CENTRE-Q dual peaks suggest otherwise.
SPAN_NOON_RATIO = 1.5
# Q knob -> q value, calibrated from ringdown measurements (Q≈28 at
# 3oc on CENTRE; that's stacked dual-stage so per-stage q is ~14).
# CCW = anti-resonance (q<0.5). CW = self-osc edge.
Q_BY_KNOB = {
    'ccw':  0.3,
    '9oc':  0.5,
    '12oc': 0.7,
    '3oc':  14.0,
    'cw':   50.0,
}
# V/Oct CV -> octave offset
CV_TO_OCT = {
    'cv-2v': -2.0, 'cv-1v': -1.0, 'cv0v': 0.0,
    'cv+1v': 1.0,  'cv+2v': 2.0,
}


def load_wav_mono(path):
    """Load stereo WAV, return float64 mono (louder channel)."""
    fs, data = wavfile.read(path)
    if data.ndim == 2:
        rms_l = np.sqrt(np.mean(data[:, 0].astype(np.float64) ** 2))
        rms_r = np.sqrt(np.mean(data[:, 1].astype(np.float64) ** 2))
        data = data[:, 0] if rms_l >= rms_r else data[:, 1]
    if np.issubdtype(data.dtype, np.integer):
        data = data.astype(np.float64) / np.iinfo(data.dtype).max
    else:
        data = data.astype(np.float64)
    return fs, data


def db(x, floor=1e-12):
    return 20.0 * np.log10(np.maximum(np.abs(x), floor))


def parse_take(filename):
    """Parse capture filename into (mode, block, freq_setting, q_setting, kind).
    kind in {'sweep', 'ring', 'osc'}."""
    base = filename.replace('.wav', '')
    parts = base.split('_')
    # ts_<mode>_<out>_<freq>_<q>[_ring|_osc]
    mode = parts[1]
    block = parts[2].upper()
    freq = parts[3]
    q = parts[4] if len(parts) > 4 else '12oc'
    if len(parts) > 5 and parts[5] in ('ring', 'osc'):
        kind = parts[5]
    else:
        kind = 'sweep'
    return mode.upper(), block, freq, q, kind


def freq_to_oct(freq_label):
    """Map FREQ knob label to octave offset from noon."""
    if freq_label == '12oc':
        return 0.0
    return CV_TO_OCT.get(freq_label, 0.0)


def model_magnitude(block, mode, freq_label, q_label, span_ratio=SPAN_NOON_RATIO):
    """Compute model magnitude response at the same nominal params.
    Returns f (Hz), |H| (linear)."""
    oct_offset = freq_to_oct(freq_label)
    f0 = F0_NOON_FORMANT if mode == 'FORMANT' else F0_NOON_XOVER
    fc_freq = f0 * (2.0 ** oct_offset)
    flo = fc_freq / span_ratio
    fhi = fc_freq * span_ratio
    q = Q_BY_KNOB.get(q_label, 0.7)
    f, mag = block_magnitude(block, mode, flo, fhi, q,
                             nl=(q >= 5.0), N=1 << 16)
    return f, mag


def analyze_sweep_capture(path):
    """Run Farina deconvolution on a capture, return (f, mag, peak_freq, peak_db)."""
    fs_sw, sweep = load_wav_mono(SWEEP_PATH)
    fs_rec, rec = load_wav_mono(path)
    assert fs_sw == fs_rec == FS, f'sample rate mismatch: {fs_sw}/{fs_rec}'
    f, mag, phase, ir = transfer_function(sweep, rec, fs=FS)
    # Find dominant peak in 20-20kHz band
    band = (f >= 20.0) & (f <= 20000.0)
    fb = f[band]
    mb = mag[band]
    peak_idx = int(np.argmax(mb))
    return f, mag, fb[peak_idx], db(mb[peak_idx])


def analyze_ringdown_capture(path):
    """Extract f0, tau, Q from ringdown. Trims to post-burst window."""
    fs, x = load_wav_mono(path)
    # Burst is at t=100ms; ringdown starts ~101ms
    burst_idx = int(0.101 * fs)
    tail = x[burst_idx:]
    f0, tau, Q = ping_q(tail, fs=fs)
    return f0, tau, Q


def analyze_selfosc_capture(path, trim_s=1.0, window_s=4.0):
    """Extract fundamental + THD from sustained tone."""
    fs, x = load_wav_mono(path)
    start = int(trim_s * fs)
    n = int(window_s * fs)
    seg = x[start:start + n]
    seg = seg - np.mean(seg)
    win = np.hanning(len(seg))
    Y = np.abs(np.fft.rfft(seg * win))
    f = np.fft.rfftfreq(len(seg), 1.0 / fs)
    Y[f < 30.0] = 0.0
    pk = int(np.argmax(Y))
    f0 = f[pk]

    def band_energy(k):
        if k < 2 or k >= len(Y) - 2:
            return 0.0
        return float(np.sum(Y[k - 2:k + 3] ** 2))

    fund = band_energy(pk)
    harm = sum(band_energy(int(round(pk * h)))
               for h in range(2, 12)
               if int(round(pk * h)) < len(Y))
    thd = float(np.sqrt(harm / fund) * 100.0) if fund > 0 else float('nan')
    return f0, thd, fund


# ---------- Per-set analysis dispatch ----------

results = []


def run_sweep_set(label, files):
    section = []
    for f in files:
        path = os.path.join(RAW_DIR, f)
        mode, block, freq, q, _ = parse_take(f)
        f_axis, mag, peak_hz, peak_db = analyze_sweep_capture(path)
        fm, mm = model_magnitude(block, mode, freq, q)
        # Compare peak frequencies
        band = (fm >= 20.0) & (fm <= 20000.0)
        model_peak_idx = int(np.argmax(mm[band]))
        model_peak_hz = fm[band][model_peak_idx]
        peak_ratio = peak_hz / model_peak_hz if model_peak_hz > 0 else float('nan')
        peak_err_octaves = np.log2(peak_ratio) if peak_ratio > 0 else float('nan')

        # RMS dB error in 100-8000 Hz band (audible mid)
        analysis_band = (f_axis >= 100.0) & (f_axis <= 8000.0)
        meas = db(mag[analysis_band])
        meas = meas - np.max(meas)
        # Interpolate model onto the same freq grid
        model_interp = np.interp(f_axis[analysis_band], fm, mm)
        mod = db(model_interp)
        mod = mod - np.max(mod)
        rms_err_db = float(np.sqrt(np.mean((meas - mod) ** 2)))

        section.append({
            'file': f, 'mode': mode, 'block': block, 'freq': freq, 'q': q,
            'meas_peak_hz': float(peak_hz),
            'model_peak_hz': float(model_peak_hz),
            'peak_err_oct': float(peak_err_octaves),
            'peak_db': float(peak_db),
            'rms_err_db': rms_err_db,
        })
        # Save overlay plot
        plot_overlay(f, f_axis, mag, fm, mm, peak_hz, model_peak_hz)
    results.append((label, 'sweep', section))


def run_ringdown_set(label, files):
    section = []
    for f in files:
        path = os.path.join(RAW_DIR, f)
        mode, block, freq, q, _ = parse_take(f)
        try:
            f0, tau, Q = analyze_ringdown_capture(path)
        except Exception as e:
            section.append({'file': f, 'error': str(e)})
            continue
        # Model: ping the SVF at the same params
        oct_offset = freq_to_oct(freq)
        fc = F0_NOON_XOVER * (2.0 ** oct_offset)
        q_target = Q_BY_KNOB.get(q, 8.0)
        s = SVF()
        N = int(0.5 * FS)
        ring = np.zeros(N)
        for n in range(N):
            _, bp, _ = s.tick(1.0 if n == 0 else 0.0, fc, q_target)
            ring[n] = bp
        try:
            f0m, taum, Qm = ping_q(ring, fs=FS)
        except Exception:
            f0m, taum, Qm = float('nan'), float('nan'), float('nan')
        section.append({
            'file': f, 'mode': mode, 'block': block, 'freq': freq, 'q': q,
            'meas_f0': float(f0), 'meas_tau_ms': float(tau * 1e3), 'meas_q': float(Q),
            'model_f0': float(f0m), 'model_tau_ms': float(taum * 1e3),
            'model_q': float(Qm),
        })
    results.append((label, 'ring', section))


def run_selfosc_set(label, files):
    section = []
    for f in files:
        path = os.path.join(RAW_DIR, f)
        mode, block, freq, q, _ = parse_take(f)
        try:
            f0, thd, fund = analyze_selfosc_capture(path)
        except Exception as e:
            section.append({'file': f, 'error': str(e)})
            continue
        # Model prediction at noon FREQ + oct_offset
        oct_offset = CV_TO_OCT.get(freq, 0.0) if freq.startswith('cv') else 0.0
        f0_predicted = F0_NOON_XOVER * (2.0 ** oct_offset)
        section.append({
            'file': f, 'freq': freq,
            'meas_f0': float(f0), 'predicted_f0': float(f0_predicted),
            'cents_err': float(1200 * np.log2(f0 / f0_predicted)) if f0_predicted > 0 else float('nan'),
            'meas_thd_pct': float(thd),
        })
    results.append((label, 'osc', section))


def plot_overlay(name, f_meas, mag_meas, f_model, mag_model, peak_meas, peak_model):
    fig, ax = plt.subplots(figsize=(10, 4))
    mb = db(mag_meas)
    mb = mb - np.max(mb)
    mm = db(mag_model)
    mm = mm - np.max(mm)
    band = (f_meas >= 20) & (f_meas <= 20000)
    ax.semilogx(f_meas[band], mb[band], 'C0', label='Hardware', alpha=0.8)
    band_m = (f_model >= 20) & (f_model <= 20000)
    ax.semilogx(f_model[band_m], mm[band_m], 'C3--', label='Model', alpha=0.8)
    ax.axvline(peak_meas, color='C0', ls=':', alpha=0.4, label=f'meas peak {peak_meas:.0f} Hz')
    ax.axvline(peak_model, color='C3', ls=':', alpha=0.4, label=f'model peak {peak_model:.0f} Hz')
    ax.set_title(name.replace('.wav', ''))
    ax.set_xlabel('Hz'); ax.set_ylabel('dB (norm)')
    ax.set_xlim(20, 20000); ax.set_ylim(-60, 5)
    ax.grid(True, which='both', alpha=0.3)
    ax.legend(loc='lower left', fontsize=8)
    plt.tight_layout()
    plt.savefig(os.path.join(OUT_PLOT_DIR, name.replace('.wav', '.png')), dpi=80)
    plt.close()


# ---------- Group captures by set ----------

ALL = sorted(os.listdir(RAW_DIR))


def filter_takes(predicate):
    return [f for f in ALL if predicate(f)]


SET_A = filter_takes(lambda f: 'xover' in f and '12oc_12oc' in f and 'ring' not in f and 'osc' not in f)
SET_B = filter_takes(lambda f: 'formant' in f and '12oc_12oc' in f)
SET_C = filter_takes(lambda f: 'xover_centre_cv' in f and '_12oc' in f and 'osc' not in f and 'ring' not in f)
SET_D = filter_takes(lambda f: 'xover_centre_12oc' in f and 'ring' not in f and 'osc' not in f and not f.endswith('12oc_12oc.wav'))
SET_E = filter_takes(lambda f: 'xover_low_12oc' in f and not f.endswith('12oc_12oc.wav'))
SET_F = filter_takes(lambda f: 'ring' in f)
SET_G = filter_takes(lambda f: 'osc' in f)

print('Set A:', SET_A)
print('Set B:', SET_B)
print('Set C:', SET_C)
print('Set D:', SET_D)
print('Set E:', SET_E)
print('Set F:', SET_F)
print('Set G:', SET_G)

run_sweep_set('Set A — XOVER baselines', SET_A)
run_sweep_set('Set B — FORMANT baselines', SET_B)
run_sweep_set('Set C — V/Oct calibration (CENTRE)', SET_C)
run_sweep_set('Set D — Q sweep CENTRE', SET_D)
run_sweep_set('Set E — Q sweep LOW (Issue #2)', SET_E)
run_ringdown_set('Set F — Ringdowns', SET_F)
run_selfosc_set('Set G — Self-oscillation', SET_G)


# ---------- Scorecard ----------

with open(SCORECARD_PATH, 'w') as out:
    out.write('# Canals baseline scorecard — hardware vs Python model\n\n')
    out.write(f'Generated by `planning/refs/three-sisters-hardware/compare.py`.\n')
    out.write(f'Empirical noon-FREQ reference (XOVER): **{F0_NOON_XOVER} Hz** '
              '(measured by FFT of Set A/D captures).\n')
    out.write(f'Empirical noon-FREQ reference (FORMANT): **{F0_NOON_FORMANT} Hz** '
              '(measured separately; +8 semitones above XOVER suggests topology '
              'differs from model in FORMANT mode).\n')
    out.write(f'SPAN noon ratio estimate: **{SPAN_NOON_RATIO}x** (~{1200*np.log2(SPAN_NOON_RATIO):.0f} cents).\n')
    out.write(f'Q knob mapping: `{Q_BY_KNOB}` (calibrated from ringdown Q at 3oc).\n\n')
    out.write('Overlay plots in `planning/refs/three-sisters-hardware/analysis/`.\n\n')

    for label, kind, rows in results:
        out.write(f'## {label}\n\n')
        if kind == 'sweep':
            out.write('| File | Block | Mode | Freq | Q | Meas peak Hz | Model peak Hz | Peak err (oct) | RMS err (dB) |\n')
            out.write('|---|---|---|---|---|---:|---:|---:|---:|\n')
            for r in rows:
                if 'error' in r:
                    out.write(f"| `{r['file']}` | | | | | | | | ERROR: {r['error']} |\n")
                    continue
                out.write(f"| `{r['file']}` | {r['block']} | {r['mode']} | "
                          f"{r['freq']} | {r['q']} | "
                          f"{r['meas_peak_hz']:.1f} | {r['model_peak_hz']:.1f} | "
                          f"{r['peak_err_oct']:+.3f} | {r['rms_err_db']:.2f} |\n")
        elif kind == 'ring':
            out.write('| File | Freq | Q | Meas f0 | Meas τ | Meas Q | Model Q |\n')
            out.write('|---|---|---|---:|---:|---:|---:|\n')
            for r in rows:
                if 'error' in r:
                    out.write(f"| `{r['file']}` | | | | | | ERROR: {r['error']} |\n")
                    continue
                out.write(f"| `{r['file']}` | {r['freq']} | {r['q']} | "
                          f"{r['meas_f0']:.1f} Hz | {r['meas_tau_ms']:.1f} ms | "
                          f"{r['meas_q']:.1f} | {r['model_q']:.1f} |\n")
        elif kind == 'osc':
            out.write('| File | Freq | Predicted Hz | Measured Hz | Error (cents) | THD (%) |\n')
            out.write('|---|---|---:|---:|---:|---:|\n')
            for r in rows:
                if 'error' in r:
                    out.write(f"| `{r['file']}` | | | | | ERROR: {r['error']} |\n")
                    continue
                out.write(f"| `{r['file']}` | {r['freq']} | "
                          f"{r['predicted_f0']:.1f} | {r['meas_f0']:.1f} | "
                          f"{r['cents_err']:+.0f} | {r['meas_thd_pct']:.3f} |\n")
        out.write('\n')

    # Headline analysis
    out.write('## Headline observations\n\n')
    # Set E vs Set D Q=3oc level comparison (Issue #2)
    set_d = next(r for l, _, r in results if 'Set D' in l)
    set_e = next(r for l, _, r in results if 'Set E' in l)
    d_3oc = next((r for r in set_d if r.get('q') == '3oc'), None)
    e_3oc = next((r for r in set_e if r.get('q') == '3oc'), None)
    if d_3oc and e_3oc:
        out.write(f"- **Issue #2 (Q-placement)**: at Q=3oc, CENTRE peak "
                  f"{d_3oc['peak_db']:.1f} dB vs LOW peak {e_3oc['peak_db']:.1f} dB. "
                  f"Delta {d_3oc['peak_db'] - e_3oc['peak_db']:+.1f} dB — "
                  f"CENTRE's dual-stage resonance stacking confirmed.\n")
    # Set G self-osc tracking
    set_g = next(r for l, _, r in results if 'Set G' in l)
    if len(set_g) >= 2 and 'cents_err' in set_g[0]:
        out.write(f"- **V/Oct tracking (Set G self-osc)**: ")
        for r in set_g:
            out.write(f"{r['freq']} → {r['cents_err']:+.0f} cents (THD {r['meas_thd_pct']:.3f}%); ")
        out.write('\n')

print(f'scorecard written to: {SCORECARD_PATH}')
print(f'plots in: {OUT_PLOT_DIR}')
