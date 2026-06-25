# er-301-habitat v2.6.2

Release date: 2026-06-25

Package updates: spreadsheet 2.8.1 -> 2.8.2. biome, catchall, mi, peaks,
porcelain, scope unchanged. Firmware unchanged from v2.6.1.

## Highlights

A small bug-fix release for the two multiband units in spreadsheet.
Both fixes address per-band CV modulation that moved the on-screen
control but not the audio.

- Parfait (multiband saturator): per-band sub-parameter modulation now
  reaches the audio, and the saturation Type selector is easier to dial in.
- Impasto (multiband compressor): per-band sub-parameter modulation now
  reaches the audio.

## Parfait (multiband saturator)

Per-band modulation now affects the sound. Patching CV into a per-band
sub-control (Amount, Bias, Type, Weight, Filter Freq, Filter Morph,
Filter Q) previously moved the on-screen fader but had no effect on the
audio, because the DSP was reading the static knob value instead of the
modulated value. CV now modulates the sound as expected. The knob still
behaves exactly as before when nothing is patched.

Saturation Type selector is easier to target. The per-band Type encoder
was over-sensitive, jumping several shaper types per detent and
overshooting on a fast turn. It now advances one shaper type per a
deliberate number of encoder detents and does not overshoot regardless
of turn speed.

## Impasto (multiband compressor)

Per-band modulation now affects the sound. Same fix as Parfait. Patching
CV into a per-band sub-control (Threshold, Ratio, Speed, Attack, Release,
Weight) now modulates the audio instead of only the display.

## Compatibility

Firmware unchanged from v2.6.1. No patch migration needed; existing
patches load unchanged.
