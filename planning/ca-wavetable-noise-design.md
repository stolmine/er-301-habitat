# Design note: cellular-automata granular noise texture (working name "Vivary")

Status: design note / not started. Ledger item `ca-noise-texture`.

A generative NOISE / TEXTURE source built on 1D cellular automata that generate
and clock the grain material. Inspired by Kentaro's "tonemata" M4L device
("a noise generator built on wavetables generated and updated by fast, precise
one-dimensional cellular automaton rules ... latency-free granular processing
locked at sample level to the cellular automaton clock"). We build in the SPIRIT
only -- CA rules are public-domain math (clean-room), and the unit gets a generic
functional name ([[feedback_no_third_party_branding]]).

Emphasis chosen (2026-07-16): **granular noise TEXTURE**, not a clean pitched
oscillator -- grittier, sound-design-first, CA-clocked grains dominate.

## The core idea

A 1D cellular automaton is a row of cells; each generation every cell's next
state = a RULE applied to its neighborhood. **Elementary (Wolfram) CA** -- 2
states, 3-cell neighborhood -- gives **256 rules from one knob**, each an iconic
texture (30 chaos, 90 fractal, 110 complex, 150 XOR, ...). Pure integer/bitwise.

The CA row IS the grain waveform. The intrigue is in HOW FAST the CA advances vs
the read:
- Evolve 0 -> row frozen -> a static (structured) tone/grain.
- Evolve slow -> row morphs -> evolving texture.
- Evolve = 1 gen / grain -> every grain is a different generation -> aperiodic
  but rule-structured -> STRUCTURED NOISE ("wavetables updated by the CA").
That single axis spans tone -> evolving -> tuned-noise; the noise end is why it
reads as *tuned/structured* noise rather than white noise.

## Architecture

- **CA engine**: N=256 cells, elementary rule via an 8-entry LUT unpacked from
  the Rule number (`next[i] = LUT[(c[i-1]<<2)|(c[i]<<1)|c[i+1]]`), toroidal
  edges. Seed = single center cell or a density-controlled random row. Advances
  on the CA clock. Integer/bitwise; run once per generation (~grain rate), not
  per sample.
- **CA-clocked grains** (the tonemata signature): a read head scans the current
  row; the CA advances LOCKED to the read -- one new generation per grain (or an
  Evolve-set multiple / free rate). Each grain is a different generation.
- **Rate** = the primary control: the CA/grain clock, sub-audio (rhythmic CA
  glitch bursts) up to audio-rate (tone-ish grit). A texture-density clock, not
  a clean V/oct pitch (though it can be pitch-tracked as an option).
- **Grain / Window**: clicky/raw (grain-boundary grit) <-> windowed + overlapped
  (2+ heads on successive rows crossfaded = smoother wash).
- **Smooth / Depth**: harsh binary +/-1 <-> history-accumulated grayscale (sum
  of last M generations per cell -> softer, band-limited-ish, bakes the CA's
  time structure into the grain shape).
- **Scatter / Stereo**: decorrelated L/R read heads (offset generations or read
  phases) -> stereo texture.
- **Anti-alias / character**: binary rows alias hard; lean INTO it as grit, with
  a tunable output LP (or optional Mirror/Canals 2x OS + halfband) to tame.

## Controls (v1)

Rate, **Rule** (0-255 = the character macro), Evolve (frozen <-> per-grain),
Grain (raw <-> windowed/overlap), Seed (trigger + density), Smooth (binary <->
grayscale), Scatter (mono <-> stereo). All the important ones as modulatable
inlets where audio-rate matters (Rate especially -> [[feedback_inlet_vs_parameter_audio_rate_mod]]).

## Fit / rails

- **am335x**: trivially cheap -- the CA is bitwise on a 256-int array run once
  per generation; per-sample is a windowed table read + phase. No NEON needed.
- **habitat**: a generative source unit (biome or spreadsheet). Distinct-method
  sibling to Rauschen (noise/gendy/dust source, [[project_spreadsheet_effect_positioning]])
  -- CA wavetables are a different, iconic method; earns its own slot.
- **Clean-room** (public-domain CA math); generic name (Vivary / Lattice /
  Palimpsest / Cendre provisional).

## Build phasing

1. **POC**: CA engine (elementary rule LUT, toroidal, seed) + ONE read head
   scanning the row at Rate, CA advancing per pass. Binary +/-1 out. Get
   structured CA noise audible; sweep Rule + Rate + Evolve. Confirm the
   tone->noise axis and that different rules sound distinct.
2. **Grain shape**: windowing + 2-head overlap/crossfade (raw <-> smooth), Smooth
   grayscale depth.
3. **Stereo + seed**: Scatter decorrelation, Seed trigger + density, input-seed
   option (make it optionally responsive to audio in).
4. **Voicing + anti-alias**: output LP / optional OS; Rate range + modulation;
   rule-space curation (which rules are the good ones).
5. **UI / viz**: a CA space-time visualization (the classic scrolling triangle
   patterns) as the overview graphic -- iconic and cheap; name.

## Open questions / risks

- Rate as pure clock vs optionally V/oct pitch-trackable (probably a Mode/option).
- 256 elementary rules is the base; multi-state or larger-neighborhood CA (more
  than 2 states / wider neighborhood) = richer but less iconic + bigger LUT --
  defer, keep elementary for v1.
- Aliasing is character but can get harsh; the LP/OS balance is voicing.
- The CA can die out (all-0 / all-1 fixed points) for some rules/seeds -> need a
  re-seed / liveliness watchdog so it never goes silent.
