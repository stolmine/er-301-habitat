# Open303 Port Scoping

Status: **scoping**, 2026-04-30. Source cloned to `~/repos/Open303`
(commit `HEAD` at clone time; `ReadMe.md` notes this is the
continuation of the SourceForge SVN repo).

License: **MIT** (https://github.com/RobinSchmidt/Open303,
`License.txt` confirms `Copyright (c) 2009 Robin Schmidt`). Direct
port path is open.

## Source overview

`Source/DSPCode/` holds the entire DSP engine. ~9.7 kLOC across
~40 files. Well-structured: each component is a `rosic_<Name>.{h,cpp}`
pair under `namespace rosic`.

`Source/VSTPlugIn/` is the VST host shell — not relevant for ER-301
(we'll write our own od::Object wrapper).

## Files needed for ER-301 port

### Required (audio path + envelopes + filter):

| File | LOC | Role |
|---|---|---|
| `rosic_Open303.{h,cpp}` | 750 | Top-level voice; main `getSample()` entry |
| `rosic_BlendOscillator.{h,cpp}` | 270 | Saw↔square morphing oscillator |
| `rosic_MipMappedWaveTable.{h,cpp}` | 567 | Wavetable infrastructure for the oscillator |
| `rosic_TeeBeeFilter.{h,cpp}` | 480 | The 303-style ladder filter (Moog ladder + HPF in feedback path + input HPF/allpass pre-shaping) |
| `rosic_BiquadFilter.{h,cpp}` | 297 | Pre/post biquads (highpass, allpass, notch in the chain) |
| `rosic_OnePoleFilter.{h,cpp}` | 286 | The `rc1`/`rc2` envelope smoothers + ampDeClicker |
| `rosic_AnalogEnvelope.{h,cpp}` | 412 | Amp envelope (ADSR-shaped exponential) |
| `rosic_DecayEnvelope.{h,cpp}` | ~120 | Filter envelope (decay-only) |
| `rosic_LeakyIntegrator.{h,cpp}` | ~80 | Pitch-slide slew limiter |
| `rosic_EllipticQuarterBandFilter.{h,cpp}` | 123 | 12th-order elliptic anti-alias filter for the 4× oversampled inner loop |
| `rosic_MidiNoteEvent.{h,cpp}` | 147 | Tiny note-event struct; can either keep or replace with our own trigger struct |
| `GlobalDefinitions.h` | small | Constants |
| `GlobalFunctions.{h,cpp}` | ~440 | Helpers (clip, exp tables, etc.) |
| `rosic_RealFunctions.{h,cpp}` | 211 | Math helpers |
| `rosic_NumberManipulations.{h,cpp}` | small | Bit-level helpers |
| `rosic_FunctionTemplates.{h,cpp}` | ~390 | Templated math (some inline) |

Subtotal: ~14 source files, ~4.5–5 kLOC.

### Indirect requirement (FFT, used by `MipMappedWaveTable` for one-shot wavetable generation at construction):

| File | LOC | Role |
|---|---|---|
| `rosic_FourierTransformerRadix2.{h,cpp}` | 473 | Wavetable mip-map generator |
| `rosic_Complex.{h,cpp}` | 245 | Complex number math for FFT |
| `fft4g.c` | small | Ooura's FFT (used inside the FourierTransformer) |

Subtotal: ~3 files, ~720 LOC. **Construction-time only**, not in the
hot path. ER-301 already ships `pffft` in spreadsheet; we could
substitute that and drop these three files entirely.

### Drop entirely (not relevant to ER-301):

- `rosic_AcidPattern.{h,cpp}`, `rosic_AcidSequencer.{h,cpp}` — TB-303
  step sequencer. ER-301 has its own sequencer units (Excel, Ballot,
  TrackerSeq). The voice should expose pure trigger + V/Oct + accent
  inlets and let users drive it with whatever sequencer they want.
- `Source/VSTPlugIn/*` — VST shell.
- `Build/`, `Notes/` — packaging / build assets.

### Key strip / refactor:

`rosic_Open303::getSample()` has a chunk that runs the AcidSequencer
inline. Strip that block; keep only the oscillator → filter → envelope
audio chain. Replace `noteOn(int noteNumber, int velocity, double
detune)` calls from sequencer logic with calls driven by our trigger
inlet rising-edge detection (matches Pecto/Ngoma's `xform gate` /
`trig` patterns).

## Audio path summary (per `rosic_Open303.h:317-410`)

Per output sample:
1. **Pitch slew**: `pitchSlewLimiter.getSample(oscFreq)` — simulates
   the 303's slide.
2. **Filter envelope smoothing**: `mainEnv.getSample()` →
   `rc1.getSample()` and `rc2.getSample()` (one-pole RC smoothers)
   → instantaneous cutoff.
3. **Amp envelope**: `ampEnv.getSample()` (analog-style ADSR) +
   `ampDeClicker` (one-pole smoother).
4. **Oversampled inner loop (4× by default)**:
   - `oscillator.getSample()` (mipmapped wavetable saw↔square blend)
   - `highpass1.getSample()` (pre-filter HPF)
   - `filter.getSample()` (the TeeBee ladder filter — *the* sound)
   - `antiAliasFilter.getSample()` (12th-order elliptic decimator)
5. **Post-decimation** (1× rate):
   - `allpass.getSample()`
   - `highpass2.getSample()`
   - `notch.getSample()`
   - `*= ampEnvOut * ampScaler`

CPU estimate on Cortex-A8: TeeBee filter is the heavy item. 4×
oversampling × ladder-filter math → roughly 10-15% CPU for one
voice instance is plausible (Pecto's NEON 24-tap multitap was 6%
stereo, this is a different shape). Wavetable mipmap reads are
cache-friendly. Realistic to ship monophonic at 4×; could need to
drop to 2× oversampling for headroom if we add custom voicing on top.

## Major porting concerns

### 1. `double` precision throughout
The whole `rosic_*` codebase uses `double`. ER-301's audio path is
`float`. **Either**:
- (A) Convert to `float` at port time. Faster on Cortex-A8 (NEON is
  float-only), but might subtly alter the filter resonance + pitch
  slide behaviour; the 303 sound is partly a function of numerical
  precision in the feedback path.
- (B) Keep `double` for the filter + envelope math, convert at the
  i/o boundaries. Slower (no NEON), but bit-faithful to the original.

Recommendation: start with **B** (double-internal, float at the
boundary), measure CPU, drop to A if needed. The filter is the
sound; preserving its numerics is the priority.

### 2. Oversampling + anti-alias decimation
The 4× oversampling and 12th-order elliptic decimator are non-trivial.
We could instead use the existing `feedback_neon_delay_gather` /
halfband-decimator pattern from Helicase, but that changes the
filter's effective response (the elliptic has a different transition
band shape). Keep the elliptic for the v1 port; revisit if CPU is
too tight.

### 3. STL `<list>` for note tracking
`rosic_Open303` uses `std::list<MidiNoteEvent> noteList` for
note-stealing logic. Replaceable with a fixed-size circular buffer of
3-4 entries (matches the 303's monophonic behaviour, no real need for
unbounded lists). Avoids std::list allocation overhead in audio thread.

### 4. C++ standard / language
Source is plain C++03 with `using namespace std;` in headers (which
ER-301's build will tolerate but is bad form). Wrap the namespace
include in an `inline namespace` or refactor headers as part of the
port.

### 5. Inlined `INLINE` macro
`GlobalDefinitions.h` defines `INLINE`. Should align with ER-301's
`inline` convention or `__attribute__((always_inline))` for
hot-path functions.

## Suggested port plan

### Phase 0 — surface assessment
- Done (this doc).

### Phase 1 — minimal voice in catchall (1-2 days)
- Drop `mods/catchall/Open303*.{cpp,h}` as the od::Object wrapper.
- Vendor the 14 required `rosic_` files under
  `mods/catchall/open303/` (subdir to keep the namespace separate
  from other catchall units).
- Substitute pffft for `rosic_FourierTransformerRadix2`. (Or carry
  it; cheap.)
- Strip the AcidSequencer inline from `getSample()`. Replace with
  trigger-inlet rising-edge detection (Pecto pattern).
- Convert `double` → `float` at i/o boundaries; keep `double`
  internally. CLAMP / NaN-safe upstream params per
  `feedback_doppler_basedelay_smoother` lessons.
- Single voice, 6 plies: trig, V/Oct, cutoff, resonance, env mod,
  decay, accent, level.
- Inlets: trigger, V/Oct, accent gate (separate from velocity in
  the original; the 303 has a per-step accent toggle).
- Build for am335x. Run Tier 2 (`tools/check-neon-hints.sh`) on the
  resulting `.o` to catch any NEON `:64` traps from the
  ported double-precision math.

### Phase 2 — listening + tuning (1 day)
- Hardware install, drive with Excel sequencer + V/Oct.
- Verify the resonance sweep + accent + slide behaviours match
  reference recordings of the 303.
- If CPU is tight: drop to 2× oversampling; swap elliptic decimator
  for a halfband.
- If sound is wrong: revisit double→float conversion + filter
  topology choice.

### Phase 3 — Devil Fish parameters (optional, post-listening)
The Open303 codebase exposes Devil Fish (303 mod) parameters that
the original 303 didn't have:
- AmpSustain (303 had this fixed at 0)
- Tanh saturation amount
- Pre-filter highpass freq
- Filter feedback HPF freq
- Resonance compensation curves

Could ship as expanded-view aux controls. Decide based on whether
the Phase 1 unit feels complete or if these add musical value.

### Phase 4 — promotion?
Open303 is a single-voice acid synth — strong candidate for
**spreadsheet flagship voice** alongside Ngoma. After Phase 2
listening test, decide whether to promote `mods/catchall/Open303` →
`mods/spreadsheet/Open303` (full release tier) or keep it in
catchall (experimental tier alongside Alembic).

## Files in `mods/catchall/` that this port will create

- `mods/catchall/Open303.{cpp,h}` — od::Object wrapper
- `mods/catchall/open303/` — vendored DSP source (14 files,
  retain `rosic_` prefix internally; the directory keeps namespace
  isolation)
- `mods/catchall/assets/Open303.lua` — control wiring + ply layout
- toc.lua entry under `category = "Experimental"` (like Alembic)

`catchall.cpp.swig` adds `%include "Open303.h"`. Bump catchall
version 0.3.0 → 0.4.0 on first ship.

## Open questions

- What's the target voice count? Monophonic matches the 303 +
  saves CPU; 4-voice paraphonic could be interesting but is a
  significant DSP fork.
- Will users sequence it with built-in patterns (port the
  AcidSequencer's pattern editor as a separate unit?) or rely on
  Excel / Ballot / Tracker? Recommendation: rely on existing
  sequencers; don't port the AcidSequencer.
- Devil Fish parameters: ship in v1 or save for v1.x? Lean v1; add
  in 1.1 once core sound is dialed.
- Phase 6+: pattern memory / preset import? Out of scope for v1.
