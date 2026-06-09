# Internal-vs-External Capture Calibration

Two amplitude scales appear in our hardware capture corpus:

- **Internal** (1.0 = ±10V eurorack rail). Used by the ER-301
  File Recorder for the self-oscillation captures
  (`planning/refs/three-sisters-hardware/internal/`). This is
  the authoritative reference for comparing against our software
  Canals (which lives in the same 0..1 normalized range).
- **External** (MOTU line input, unknown gain staging). Used
  for the original Phase 0c sweep captures
  (`planning/refs/three-sisters-hardware/raw/`). Signal path
  went 3S → euro→line conversion → MOTU input trim → WAV. The
  gain is unknown without measuring.

For comparison, **everything must be reduced to the internal
1.0-=-±10V scale**. The earlier MOTU captures are unusable for
absolute amplitude analysis until we know the attenuation factor.

This doc shows how to measure it.

## Quick calibration protocol

Run this **before each capture session** (or once after rig
setup if the chain doesn't change). Takes ~5 minutes.

### Setup

Use the same physical rig you'll use for the capture session.
**No changes to gain staging once calibrated.** If you re-tune
the VCA, the preamp, or the MOTU input trim, redo this
calibration.

### Reference signal

Generate a known reference signal on the ER-301:

1. Insert **Sine Oscillator** on a chain
2. Set frequency to **1 kHz** (any in-band tone works)
3. Set the chain's output level to peak at exactly **0.5**
   (= ±5V = −6 dBFS internal). Use a Constant unit or a VCA
   with hardSet gain = 0.5 to land at exactly this value.
4. Verify on internal scope: peak readout should show ~0.5

### Calibration captures

**Capture A — internal (reference)**: same patch chain, record
the reference signal via ER-301 File Recorder. ~3 seconds.

- [ ] `cal_internal.wav` — internal File Recorder

**Capture B — external (matches your session rig)**: send the
same reference signal through the SAME physical chain you'll use
for hardware Three Sisters captures (preamp, VCA, MOTU input,
all unchanged). Use sox or a DAW to record from MOTU.

- [ ] `cal_external.wav` — MOTU line in, identical chain

### Compute the calibration constant

```bash
python3 - <<'EOF'
import numpy as np
from scipy.io import wavfile

def peak(path):
    fs, x = wavfile.read(path)
    if x.ndim > 1: x = x[:, 0]
    x = x.astype(np.float64)
    if np.issubdtype(x.dtype, np.integer):
        x = x / np.iinfo(x.dtype).max
    return float(np.abs(x).max())

p_int = peak('cal_internal.wav')
p_ext = peak('cal_external.wav')
attenuation_factor = p_int / p_ext
print(f"Internal peak: {p_int:.4f}")
print(f"External peak: {p_ext:.4f}")
print(f"Attenuation factor (multiply external captures by this): {attenuation_factor:.3f}")
print(f"= {20*np.log10(attenuation_factor):+.1f} dB")
EOF
```

This factor is **the number to multiply external (MOTU-path)
captures by to bring them onto the internal scale**.

Example interpretation: if `p_int=0.5` and `p_ext=0.05`, the
chain attenuates by 10× (20 dB). To compare an external
capture's peak of 0.07 to internal scale: 0.07 × 10 = 0.7
(internal equivalent).

## Where to use the factor

- **Re-normalize Phase 0c MOTU captures**: if you save the
  attenuation factor from a session that reproduces the original
  rig, multiply the original `raw/ts_*.wav` peaks by that factor
  to get internal-scale equivalents.
- **Future external captures**: anytime captures go through the
  MOTU, capture and apply the calibration factor.

## When the calibration is invalid

- Any gain stage in the chain changes (preamp setting, VCA gain,
  MOTU input trim, cable swap)
- New cables introduce different impedance or attenuation
- ER-301 reference signal amplitude changes
- A different output on the ER-301 is used (some outputs may
  have different DAC scaling)

When in doubt, redo the calibration. Cheap (5 min) compared to
hours of mis-comparing levels.

## Stash the constant in the session record

After each capture session, write the calibration constant into
the session's README or capture archive notes:

```
calibration:
  reference: ER-301 Sine 1kHz @ 0.5 amplitude
  internal_peak: 0.4998
  external_peak: 0.0512
  attenuation_factor: 9.76
  applies_to: spanvol_*.wav in this archive
```

That way the absolute amplitudes in this archive's WAVs are
recoverable later as `peak × 9.76` for internal-scale comparison.

## Related

- `planning/refs/three-sisters-hardware/README.md` — existing
  capture corpus documentation
- `planning/canals-span-volume-capture-checklist.md` — runs
  this calibration as Step 0 before the SPAN/volume captures
