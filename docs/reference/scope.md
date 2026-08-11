# Scope (`scope`) — v1.2.7

Inline signal visualization. Every unit in this package is a transparent passthrough
that draws what is flowing through it — three oscilloscope variants and four
spectrum-analyzer variants of increasing display width. Nothing here alters the
signal; drop one anywhere in a chain to look at it.

---

## Scope

*mnemonic: Sc* · Category: Measurement

A one-ply oscilloscope that sits inline and draws the waveform passing through it.
The trace is auto-triggered on a zero-ish crossing (the threshold tracks a slow
moving average of the signal) so steady tones stand still instead of sliding.
The sub-display carries a timebase selector, a Y-gain selector, and a read-only
voltmeter, which makes it usable for DC offsets and V/Oct checks as well as
audio-rate waveform inspection.

**Controls**

The main display is the scope trace itself — there are no dial controls. All
adjustment happens on the sub-display.

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `time` | stepped selector (sub-display button 1) | `1x`, `2x`, `4x`, `8x`, `16x`, `32x`, `64x` | `2x` | Timebase. Sets the capture probe's decimation, so higher values show a longer window of time in the same width. `2x` matches the firmware's built-in scope. Changing it briefly blanks the trace while the buffer refills at the new rate. |
| `gain` | stepped selector (sub-display button 2) | `0.25x`, `0.5x`, `1x`, `2x`, `4x` | `1x` | Vertical scale on the trace only. Purely cosmetic — it does not touch the audio. |
| `volt` | read-only readout (sub-display button 3) | −9.999 … 9.999 V, 3 decimals | — | Rolling mean of the visible buffer, scaled to volts (full scale = 10 V). Stable for DC and V/Oct, sits near 0 for AC signals. Pressing button 3 does nothing — it is not editable. |

**Sub-display / expanded** — Three columns: `time`, `gain`, `volt`. The two
editable values sit inside a border that renders dotted when idle and solid when
that slot has the encoder; the voltmeter has no border, which is how read-only
values are distinguished. Press button 1 or 2 to move the encoder to that slot,
then turn to step through the choices. Both selections are saved with the preset.

On Scope Stereo one `time` / `gain` pair drives both traces, and the voltmeter
follows the left channel.

**Menu** — none.

**I/O** — Stereo-aware passthrough. `In1` → `Out1`; on a stereo chain `In2` →
`Out2` as well. The single trace always displays the left channel. Integration
window for the voltmeter is buffer depth × decimation, roughly 0.33 s at `2x` and
about 10 s at `64x`. No V/Oct, gate, or trigger inlets — triggering is internal.

---

## Scope 2x

*mnemonic: S2* · Category: Measurement

Identical to Scope in every respect except that the trace is drawn two plies wide
instead of one, for twice the horizontal detail. Same passthrough, same
`time` / `gain` / `volt` sub-display, same defaults.

---

## Scope Stereo

*mnemonic: SS* · Category: Measurement

Two plies wide, but split into two half-width traces side by side — left channel on
the left, right on the right, each labelled `L` and `R`. Use it to eyeball
correlation, channel balance, or a stereo widener's effect. On a mono chain it
degrades gracefully to a single full-width trace of the left channel.

Differences from Scope: two independent graphics rather than one; a single
`time` / `gain` pair applies to both; the `volt` readout follows the left trace
only. `In1`/`In2` → `Out1`/`Out2` passthrough on a stereo chain.

---

## Spectrogram

*mnemonic: Sg* · Category: Measurement

A two-ply real-time spectrum display. Audio passes through untouched while the unit
mono-sums it and runs a windowed FFT, drawing a filled RMS body with a bright
peak-hold outline on top. The horizontal axis spans 20 Hz to Nyquist, log-scaled by
default so octaves are evenly spaced like an EQ. The sub-display switches the
frequency and amplitude mappings and reports the loudest frequency it can see.

**Controls**

The main display is the spectrum itself — there are no dial controls.

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `freq` | stepped selector (sub-display button 1) | `log`, `lin` | `log` | Frequency axis. `log` gives equal octaves per pixel (EQ-style); `lin` gives equal Hz per pixel, which crowds everything musical into the left edge but is useful for harmonic-series inspection. |
| `amp` | stepped selector (sub-display button 2) | `log`, `lin`, `exp` | `log` | Vertical mapping. `log` is a 60 dB dB-scale; `lin` is raw magnitude, where loud peaks dominate; `exp` is a square-root curve that lifts quiet detail into view. |
| `peak` | read-only readout (sub-display button 3) | — | — | The greatest-energy frequency (Hz, or `x.xxk` above 1 kHz) on the upper line and its level in dB below. Interpolated between bins for sub-bin accuracy. DC is excluded. Pressing button 3 does nothing. |

**Sub-display / expanded** — Three columns: `freq`, `amp`, `peak`. Same
dotted/solid border convention as the Scope units — editable slots have a border,
the read-only peak readout does not. Press button 1 or 2 to grab the slot, then
turn the encoder. The encoder accumulates ticks and steps once per threshold, so a
fast spin does not overshoot a two- or three-entry list. Both selections are saved
with the preset.

**Menu** — none.

**I/O** — Stereo-aware passthrough: `In1` → `Out1`, and on a stereo chain
`In2` → `Out2`. Analysis is on the mono sum of both inputs, so a stereo source is
analyzed as a whole. 256-point FFT (128 bins), Hann window, recomputed every 4
audio frames, with per-bin peak decay and smoothed RMS. No V/Oct, gate, or trigger
inlets.

---

## Spectrogram 3

*mnemonic: S3* · Category: Measurement

Three plies wide. Same analysis as the base Spectrogram — a 256-point FFT — drawn
across more horizontal space, so the extra width is smoother interpolation of the
same 128 bins rather than added resolution. Identical `freq` / `amp` / `peak`
sub-display, identical defaults and I/O.

---

## Spectrogram 4

*mnemonic: S4* · Category: Measurement

Four plies wide, and backed by a 512-point FFT (256 bins) so the extra display
columns rest on real bins rather than a stretched image. The trade is the usual
one: twice the frequency resolution, twice the analysis window, so fast transients
smear slightly more than on the 256-point units. Identical `freq` / `amp` / `peak`
sub-display, defaults, and passthrough I/O.

---

## Spectrogram 6

*mnemonic: S6* · Category: Measurement

Six plies wide — nearly the full display — also backed by the 512-point FFT. The
widest and most detailed view in the package; useful when you want to read a
spectrum rather than glance at it. Otherwise identical to Spectrogram 4.

---

<!-- VERIFICATION NOTES

Discrepancies found:

- FFT size claim. The task brief (and a loose reading of RELEASE-2.8.0.md) suggests
  Spectrogram 3/4/6 are all backed by a 512-point FFT. Source says otherwise:
  mods/scope/assets/SpectrogramFactory.lua does
  `op:hardSet("FFT Size", plies >= 4 and 512 or 256)`, so Spectrogram (2-ply) AND
  Spectrogram 3 (3-ply) are 256-point; only Spectrogram 4 and 6 are 512-point.
  RELEASE-2.8.0.md is actually correct on this ("The 4 and 6 ply units use a larger
  FFT"); the brief was not. Documented per source.

- README.md line 118 lists the scope package as containing only "Scope, Scope 2x,
  Scope Stereo" — the four Spectrogram units are missing from the package table
  entirely, even though Spectrogram shipped in v2.3.0 and 3/4/6 in v2.8.0. README
  is stale.

- Sub-display button naming. RELEASE-2.5.1.md describes the Scope selectors as
  driven by "M1 / M2"; the source comments in SpectrogramFactory.lua call the same
  positions "S1 / S2 / S3". Both refer to the three buttons under the sub-display
  (app.SubButton indices 1/2/3). Documented neutrally as "sub-display button 1/2/3"
  to avoid picking a wrong hardware label.

- Encoder behavior differs between the two families and is not mentioned in release
  notes: ScopeView steps one entry per encoder event, while SpectrogramFactory
  accumulates ticks against a threshold of 16 before stepping (the v2.8.0
  "discrete controls now step consistently" change). Scope's time/gain were
  apparently not converted.

Verified from source:
- Timebase 1x-64x and gain 0.25x-4x with defaults 2x / 1x: ScopeView.lua
  DECIMATION / GAIN_VALUES / TIME_DEFAULT / GAIN_DEFAULT. Confirmed.
- Voltmeter: ScopeVoltsReadout.h (3 decimals, clamped to +/-9.9995) and
  ScopeGraphic::getVolts() (rolling mean x 10, FULLSCALE_IN_VOLTS = 10). Confirmed.
- Spectrogram analysis parameters (Hann, every-4-frames, peak decay 0.92, RMS
  smoothing 0.3, DC bin skipped in peak search): mods/scope/Spectrogram.cpp.
- 20 Hz - Nyquist span and the log/lin, log/lin/exp mappings:
  SpectrogramGraphic::draw and ampNorm() in mods/scope/Spectrogram.h. The dB scale
  is a fixed 60 dB range (dbNorm).
- No unit in this package defines onLoadMenu or onShowMenu — confirmed by grep;
  no menus documented.
- Sample rate is read from globalConfig for the peak readout but hardcoded to
  48000 in the graphic's axis math; at 48 kHz these agree, so "Nyquist = 24 kHz"
  is stated only as an example.

Could not verify:
- Exact FifoProbe buffer depth (referenced as ~8000 samples in a ScopeGraphic
  comment) lives in firmware, not this repo; the voltmeter integration-window
  figures (~0.33 s at 2x, ~10 s at 64x) are taken from that comment rather than
  computed independently.
-->
