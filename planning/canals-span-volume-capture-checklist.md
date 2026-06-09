# Canals — SPAN + Volume Characteristics Capture Checklist

Profile two open questions identified during audition of 2.7.1.20:

1. **Volume modulation across cutoff sweep** — our LOW block shows
   ~6× RMS swing as fc moves from 80 Hz to 3 kHz (per Python sim).
   Hardware reportedly stays flatter. Capture pink-noise V/Oct
   sweeps at two Q levels to quantify.

2. **SPAN control character** — user audition feedback that our
   SPAN feels measurably different from hardware. Profile by
   capturing CENTRE output at five SPAN positions using sweep.wav
   as input; derive actual LOW/HIGH cutoff positions and the
   knob-to-octave mapping.

Two excitation sources used:

- **sweep.wav** (already at /tmp/sweep.wav, copy to ER-301 SD if
  not present) — for SPAN profiling
- **pink.wav** — NEW; generate fresh, see Pre-flight below

## Calibration first (5 min)

Per `planning/canals-internal-external-calibration.md`, run the
internal-vs-external calibration capture pair BEFORE the rest of
the session. Captures B-via-MOTU and C-via-MOTU below need the
attenuation factor to be normalized onto the internal scale.

If the entire session stays on the internal record path (3S →
ER-301 chain → File Recorder, no MOTU touched), skip calibration
— all captures will already be on the internal scale.

- [ ] `cal_internal.wav` recorded via File Recorder
- [ ] `cal_external.wav` recorded via MOTU (if any session captures
      will go via MOTU)
- [ ] Attenuation factor computed and noted

## Pre-flight

- [ ] Three Sisters patched as before: ER-301 chain output → 3S
      ALL IN, capture from 3S output → ER-301 input → File Recorder
- [ ] Knobs cleared: FREQ noon, SPAN noon, Q noon, Mode XOVER
- [ ] V/Oct unpatched initially (we'll add CV source for sweep tests)
- [ ] **pink.wav** generated and on SD. Run the helper:
      ```
      python3 planning/refs/three-sisters-hardware/generate_excitation.py --pink
      ```
      (See "pink noise generator" below if the script doesn't yet
      have a --pink option.)
- [ ] sweep.wav present (re-copy if needed)
- [ ] ER-301 has Constant CV unit available for slow V/Oct ramps
- [ ] ER-301 has LFO / Slow Random / Constant for the slow knob
      sweep alternatives (or you can manually sweep knobs)
- [ ] Recording folder on SD: `recorded/canals-span-volume/`

## Pink noise generator

Add this option to `planning/refs/three-sisters-hardware/generate_excitation.py`
if not present (or generate inline):

```python
def make_pink(path, duration=15.0):
    """Generate 15s of pink noise at -20 dBFS RMS.
    Voss-McCartney algorithm: N octaves of white noise summed."""
    import numpy as np
    from scipy.io import wavfile
    FS = 48000
    N = int(duration * FS)
    # Simple pink approximation via Paul Kellet's filter
    np.random.seed(2026)
    white = np.random.randn(N)
    pink = np.zeros(N)
    b0=b1=b2=b3=b4=b5=b6=0.0
    for i in range(N):
        white_i = white[i]
        b0 = 0.99886*b0 + white_i*0.0555179
        b1 = 0.99332*b1 + white_i*0.0750759
        b2 = 0.96900*b2 + white_i*0.1538520
        b3 = 0.86650*b3 + white_i*0.3104856
        b4 = 0.55000*b4 + white_i*0.5329522
        b5 = -0.7616*b5 - white_i*0.0168980
        pink[i] = b0+b1+b2+b3+b4+b5+b6+white_i*0.5362
        b6 = white_i*0.115926
    # Normalize to -20 dBFS RMS
    rms_target = 10**(-20/20)
    pink = pink * (rms_target / np.sqrt(np.mean(pink**2)))
    stereo = np.stack([pink, pink], axis=1)
    wavfile.write(path, FS, (stereo * 32767 * 0.5).astype(np.int16))
```

## File naming

`spanvol_<test>_<setting>.wav`

Where `test` is one of: `span`, `freq`, `qsweep`
And `setting` is the relevant variable.

Examples:
- `spanvol_span_ccw.wav`, `spanvol_span_9oc.wav`, ...
- `spanvol_freq_qnoon_low.wav` (LOW out at Q noon, V/Oct sweep)
- `spanvol_qsweep_fc200.wav` (Q sweep at fc=200Hz)

## Set A — SPAN profile (5 captures)

Quantify the actual SPAN curve (knob position → cutoff separation
in octaves). Use sweep.wav input (so we can find LOW and HIGH peak
positions in the captured spectrum).

Knobs: FREQ noon, Q noon, V/Oct unpatched, Mode XOVER. Vary SPAN
across 5 positions. **Capture CENTRE output** at each position
(CENTRE shows BOTH peaks since SVF1 at lowF and downstream LP at
highF — gives us both cutoffs from one capture).

- [ ] `spanvol_span_ccw.wav` — SPAN full CCW
- [ ] `spanvol_span_9oc.wav` — SPAN at 9 o'clock
- [ ] `spanvol_span_noon.wav` — SPAN noon (12 o'clock)
- [ ] `spanvol_span_3oc.wav` — SPAN at 3 o'clock
- [ ] `spanvol_span_cw.wav` — SPAN full CW

For each: play sweep.wav through the unit, record ~9s of CENTRE
output. Analysis will FFT and identify the lower and upper peak
frequencies, which give us lowHz and highHz at that SPAN position.

## Set B — Volume across V/Oct sweep (4 captures)

Quantify how much volume modulates with cutoff. Pink noise input,
slow V/Oct ramp from −2V to +2V over ~10s, recording the chosen
block's RMS over time.

Setup: pink.wav playing through ER-301 chain → 3S ALL IN.
V/Oct CV from a slow ER-301 ramp (e.g., LFO at 0.05Hz with ±2V
amplitude, or Constant CV manually swept).

Mode XOVER, SPAN noon.

- [ ] `spanvol_freq_qnoon_low.wav` — LOW output, Q at noon
- [ ] `spanvol_freq_qnoon_centre.wav` — CENTRE output, Q at noon
- [ ] `spanvol_freq_q3oc_low.wav` — LOW output, Q at 3 o'clock
- [ ] `spanvol_freq_q3oc_centre.wav` — CENTRE output, Q at 3 o'clock

Why these 4: LOW + CENTRE are the user's primary concerns;
two Q levels show low-Q vs high-Q behavior. HIGH likely follows
LOW's pattern (symmetric topology) but we can add it if needed.

## Set C — Q sweep at fixed fc (2 captures)

Quantify how the resonance peak grows + whether low-band content
stays intact. Pink noise input, FREQ held, slow knob-sweep of Q
from CCW to CW over ~10s.

Setup: pink.wav into 3S. FREQ knob set to specific position. Mode
XOVER, SPAN noon.

- [ ] `spanvol_qsweep_fc200_low.wav` — LOW out, FREQ set so
      fc_low ≈ 200 Hz. Sweep Q CCW → full CW slowly (~10s).
- [ ] `spanvol_qsweep_fc1k_low.wav` — same but FREQ set so
      fc_low ≈ 1 kHz.

These show whether hardware's LOW band maintains low-frequency
content as Q rises (the user's "doesn't lose lows" observation).

## Total: 11 captures, ~2-3 min of audio

5 SPAN + 4 V/Oct + 2 Q sweeps. ~5-10 sec each capture.

## Analysis (next-session)

A new script `compare_span_volume.py` will:

**Set A analysis** (SPAN profile):
- For each capture: FFT, find lower + upper peaks (lowHz, highHz)
- Plot SPAN knob position vs lowHz/highHz, vs octave separation
- Fit: knob → octave separation curve. Linear? Exponential? Cubic?
- Output: empirical SPAN mapping table, comparison to current
  C++ `span * 48` linear-semitone formula
- If mapping differs significantly from current formula, update
  C++ to match hardware's curve

**Set B analysis** (volume vs cutoff):
- For each capture: compute RMS in 100ms sliding windows over time
- Plot: RMS vs time (proxy for RMS vs cutoff since V/Oct sweeps
  linearly)
- Compare hardware vs Canals (run same input through Canals on
  emulator). Calculate RMS-vs-fc curve for each.
- Identify: is hardware flat? Is Canals's modulation correctable?
- Possible fixes: freq-comp output gain, normalize per-block
  amplitude across cutoff

**Set C analysis** (Q sweep low-band retention):
- For each capture: compute energy in 30-80 Hz band over time
- Plot: 30-80 Hz RMS vs time (proxy for low-band vs Q)
- Compare hardware vs Canals
- Identify: does hardware retain 30-80 Hz at high Q? Does Canals?
- Possible fixes: dynamic output ceiling on resonant content,
  per-band soft limit

Output: `planning/canals-span-volume-scorecard.md` with per-test
findings + actionable changes.

## When you're done

- [ ] All 11 captures saved with correct naming
- [ ] Peak-clipping audit: every recording's peak < 0 dBFS
- [ ] Tar/zip as `canals-span-volume-<YYYY-MM-DD>.tar.gz`
- [ ] Copy archive to `planning/refs/three-sisters-hardware/`
- [ ] Add to existing README.md noting this round
