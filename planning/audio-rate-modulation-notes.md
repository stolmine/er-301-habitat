# Audio-rate (per-sample) modulation on the ER-301: canonical reference

Status: research notes, 2026-07-15. Written for the switched-capacitor filter
clone (planning/bionic-lester-clone-design.md, ledger `bionic-lester-clone`),
which requires cutoff AND resonance modulatable at audio rate from day one.

Repos cited:
- SDK / firmware: `/home/bram/repos/er-301-stolmine/` (C++ `od::` in `od/`,
  Lua framework in `xroot/`, built-in units in `mods/core/`)
- Habitat packages: `/home/bram/repos/er-301-habitat/mods/`

Every claim below carries a file:line reference.

---

## 1. The core rule

**A modulatable target is audio-rate if and only if it is an `od::Inlet`
whose buffer is read per sample inside `process()`. An `od::Parameter` is a
scalar sampled once per frame (block).** The Lua wiring determines which one
a control feeds:

| Rate | C++ side | Lua wiring | Update rate |
|---|---|---|---|
| audio-rate | `od::Inlet`, `addInput()`, read `buf[i]` per sample | `app.GainBias()` object + `connect(gb, "Out", op, "X")` | every sample (48 kHz) |
| block-rate | `od::Parameter`, `addParameter()`, read `.value()` once | `app.ParameterAdapter()` + `tie(op, "X", adapter, "Out")` | once per frame (375 Hz at defaults) |

The frame ("block") is 128 samples at 48 kHz by default, i.e. block-rate
control updates at 375 Hz. Modulating a block-rate Parameter with audio-rate
CV stair-steps at 375 Hz: the "flappy" artifact.

---

## 2. C++ SDK layer: frames, Inlet, Outlet, Parameter

### 2.1 Frame size and rates

- Default frame length 128 samples, default sample rate 48 kHz:
  `od/config.c:38-39` (`globalConfig.frameLength = 128;`,
  `globalConfig.sampleRate = 48000;`). Frame rate derived at `od/config.c:21`
  (`frameRate = sampleRate / frameLength` = 375 Hz).
- Hot loops must use the `FRAMELENGTH` macro, `od/config.h:33`:
  `#define FRAMELENGTH ((int)(4 * (globalConfig.frameLength / 4)))` with the
  comment at `od/config.h:28-32`: the macro convinces the compiler the count
  is a multiple of 4 so it can vectorize without a scalar tail.
- Hard cap `MAX_AUDIO_FRAME_LENGTH (128)` at `hal/constants.h:24`.
  `FULLSCALE_IN_VOLTS (10.0f)` at `hal/constants.h:33` (unit-value 1.0 = 10 V,
  so V/Oct math multiplies by `FULLSCALE_IN_VOLTS * logf(2.0f)`).

### 2.2 Execution model: process() is called once per frame

`od::Unit::process()` walks the compiled object graph once per audio frame;
for each object it first updates Parameters, then runs DSP
(`od/units/Unit.cpp:200-213`):

```cpp
void Unit::process()
{
  for (Object *o : mProcessingOrder)
  {
    o->updateParameters();   // Unit.cpp:204
    o->process();            // Unit.cpp:207/210
  }
}
```

Same pattern for globally scheduled objects (`od/tasks/ObjectList.cpp:59-60`)
and the chain task driving units (`od/tasks/UnitChain.cpp:192-200`).
`Object::updateParameters()` calls `param->update()` on every registered
Parameter (`od/objects/Object.cpp:389-394`). So: **one `process()` call = one
128-sample frame; Parameters advance exactly once per frame.**

### 2.3 od::Inlet / od::Outlet: per-sample audio buffers

- `Inlet::buffer()` returns a `float*` of FRAMELENGTH samples: the connected
  Outlet's buffer, or the global constant-zero buffer when unconnected
  (`od/objects/Inlet.h:26`, `od/objects/Inlet.cpp:30-39`; `ZeroOutput` /
  `OneOutput` globals at `od/objects/Outlet.h:42-44`).
- Outlets own the actual storage: `float *mBuffer` at `od/objects/Outlet.h:36`,
  accessor `float *__restrict__ buffer()` at `od/objects/Outlet.h:20`.
- `Inlet::connect(Outlet*)` is the primitive behind Lua `connect()`
  (`od/objects/Inlet.cpp:41-47`).
- Objects register ports with `addInput(Inlet&)` / `addOutput(Outlet&)`
  (`od/objects/Object.h:81-85`). Note the standing habitat rule: every Outlet
  must be `addOutput()`ed or the buffer is null and the unit is silently
  silent (memory: feedback_addoutput_required_for_multiout).
- Gotcha: an unconnected Inlet reads constant zero. There is no built-in
  bias on an Inlet; the knob offset must be summed upstream, which is exactly
  what the `app.GainBias` object provides (section 2.5).

### 2.4 od::Parameter: block-rate scalar with a frame-rate ramp

`od/objects/Parameter.h:26-31,62-64`: a Parameter holds `mValue`/`mTarget`;
`value()` returns the scalar (or forwards to a tied leader,
`od/objects/Parameter.cpp` `value()` body). Setters:

- `hardSet(x)` jumps value and target instantly
  (`od/objects/Parameter.cpp:77-88`).
- `softSet(x)` ramps: `rampTo()` sets `mCount = 50`, `mStep = diff * 0.02`
  (`od/objects/Parameter.cpp:62-75`), and each `update()` (once per frame,
  section 2.2) advances one step (`od/objects/Parameter.cpp:183-201`). 50
  frames at 375 Hz = a ~133 ms smoothing ramp, applied at frame rate only.
- `tie(Followable&)` (`od/objects/Parameter.h:22`,
  `od/objects/Parameter.cpp:17-23`) makes `value()` delegate to another
  Parameter/Expression. This is what Lua `tie()` calls.

**Nothing about a Parameter ever moves inside a frame.** Per-sample smoothness
must come from the reading object interpolating (as GainBias does) or from
using an Inlet instead.

### 2.5 The two adapter objects (where the rate is actually decided)

**`app.GainBias` = audio-rate path** (`od/objects/math/GainBias.h:15-19`):
Inlet `In`, Outlet `Out`, Parameters `Gain`/`Bias`. `process()`
(`od/objects/math/GainBias.cpp:22-63`) computes per sample
`out[i] = in[i] * gain + bias` (line 43). When Gain/Bias changed since the
previous frame it crossfades them across the frame using
`LookupTables::FrameOfLinearRamp` (lines 50-55): free anti-zipper for knob
moves, while CV arriving on `In` passes through at full audio rate.

**`app.ParameterAdapter` = block-rate path**
(`od/objects/adapters/ParameterAdapter.h:9-24`, header comment: "Imitates a
combined GainBias and MinMax object while sampling Inlet to a Parameter").
The decimation is one line, `od/objects/adapters/ParameterAdapter.cpp:30`:

```cpp
float x = *(mInput.buffer() + globalConfig.frameLength - 1);
```

It reads only the LAST sample of the incoming CV frame, applies gain/bias/
clamp, then `mOutput.hardSet(x)` (line 40). `hardSet` = no ramp. Result: the
tied target Parameter is a 375 Hz sample-and-hold of the CV. That is the
"flappy" mechanism, verbatim.

---

## 3. Lua wiring layer (xroot)

- `connect(fromObject, fromPort, toObject, toPort)` is a global defined at
  `xroot/boot/globals-setup.lua:50-60`; it dispatches to `app.connect`, which
  lands on `Inlet::connect` (audio graph edge).
- `tie(slaveObj, slaveParamName, masterObj, masterParamName)` at
  `xroot/boot/globals-setup.lua:114-123`, implemented by `tieParameter`
  ending in `slave:tie(master)` (`xroot/boot/globals-setup.lua:111`). There
  is also an expression form (`tieExpression`, lines 63-92) for tying a
  Parameter to a compiled `app.Expression` over other Parameters: still
  block-rate, since it is still a Parameter.
- `Unit:addObject(name, o)` registers a DSP object into the unit's graph
  (`xroot/Unit/init.lua:218-224`, calls `self.pUnit:addObject(o)`).
- `Unit:addMonoBranch(name, inObject, inletName, outObject, outletName)`
  (`xroot/Unit/init.lua:226-242`) creates the modulation sub-chain; its
  output is wired into `inObject:getInput(inletName)`
  (`xroot/Unit/init.lua:233`). So the branch always terminates in an INLET of
  some object; whether the modulation reaches the DSP at audio rate depends
  entirely on whether that object is an `app.GainBias` (connected onward to a
  DSP Inlet) or an `app.ParameterAdapter` (tied onward to a DSP Parameter).
- The `Unit.ViewControl.GainBias` UI control
  (`xroot/Unit/ViewControl/GainBias.lua:86-128`) only requires that its
  `gainbias` object expose `Gain` and `Bias` Parameters (lines 111-121); it
  works identically over an `app.GainBias` or an `app.ParameterAdapter`.
  **The UI looks the same either way; the rate is fixed by the graph wiring,
  not the view control.** (Habitat gotcha: C++ code that READS a value from
  an adapter must reference "Out" not "Bias", memory
  feedback_subparam_out_vs_bias_modulation.)

### 3.1 Canonical Lua sketches

Audio-rate control (pattern from the built-in Ladder LPF,
`mods/core/assets/LadderFilterUnit.lua:40-58`):

```lua
local res = self:addObject("res", app.GainBias())
local resRange = self:addObject("resRange", app.MinMax())
connect(res, "Out", filter, "Resonance")   -- Resonance is an od::Inlet
connect(res, "Out", resRange, "In")
self:addMonoBranch("Q", res, "In", res, "Out")
-- view: GainBias { branch = branches.Q, gainbias = objects.res, range = objects.resRange, ... }
```

Block-rate control (pattern from habitat Canals,
`mods/spreadsheet/assets/Canals.lua:122-131`):

```lua
local span = self:addObject("span", app.ParameterAdapter())
tie(op, "Span", span, "Out")               -- Span is an od::Parameter
self:addMonoBranch("span", span, "In", span, "Out")
-- view: GainBias { branch = branches.span, gainbias = objects.span, range = objects.span, ... }
```

---

## 4. DSP-side per-sample read pattern (C++)

Skeleton distilled from the built-ins (section 5):

```cpp
// header
od::Inlet  mIn{"In"};
od::Inlet  mVOct{"V/Oct"};
od::Inlet  mFundamental{"Fundamental"};
od::Inlet  mResonance{"Resonance"};
od::Outlet mOut{"Out"};
// ctor: addInput(mIn); addInput(mVOct); addInput(mFundamental);
//       addInput(mResonance); addOutput(mOut);

void process()
{
  float *in   = mIn.buffer();
  float *voct = mVOct.buffer();
  float *f0   = mFundamental.buffer();
  float *res  = mResonance.buffer();
  float *out  = mOut.buffer();
  for (int i = 0; i < FRAMELENGTH; i++)
  {
    // derive coefficients from voct[i], f0[i], res[i]  (cheap approx!)
    // run filter state update
    // out[i] = ...
  }
}
```

Scratch buffers for a separate coefficient pass come from the audio thread's
constant-time frame pool: `AudioThread::getFrame()` / `releaseFrame()`
(`od/AudioThread.h:35-39`, explicitly documented as non-locking and safe in
the audio thread).

---

## 5. Built-in units that do audio-rate modulation (evidence)

### 5.1 StereoLadderFilter (the built-in filter, the direct precedent)

`mods/core/objects/filters/StereoLadderFilter.h:17-21`: **every modulatable
input is an Inlet** - `Left In`, `Right In`, `V/Oct`, `Resonance`,
`Fundamental`. No Parameters at all.

`mods/core/objects/filters/StereoLadderFilter.cpp:49-180`, structure:

1. Grab all inlet buffers (lines 51-57): `octave`, `res`, `freq` alongside
   audio.
2. **Coefficient pass, per sample, NEON 4-wide** (lines 78-118, comment at
   line 78: "calculate filter coefficients (sample-by-sample)"): pitch =
   `f0 * simd_exp(voct * 10*ln2)` (line 84), clamp (85-86), `simd_sin` for
   the tuning polynomial (89), then P (pole coefficient), K, and R
   (resonance feedback = `res[i] * (t2+6t1)/(t2-6t1)`, line 112) computed
   with NEON multiply-accumulate plus a `vrecpeq_f32` + 3 Newton-Raphson
   steps for the division (lines 106-110). Results stored to three scratch
   frames P/K/R obtained from `AudioThread::getFrame()` (lines 60-62,
   released 177-179).
3. **Filter pass, per sample** (lines 135-165): 4 cascaded one-pole stages in
   `float32x2_t` (stereo in one register), reading `P[i]/K[i]/R[i]` per
   sample, band-limited cubic saturation macro (lines 14-20).

So the stock filter recomputes ALL coefficients every sample, including the
`exp2` pitch map and a division; it costs 1.28% CPU on the ER-301
(profiling comment, line 48). There is no block-rate-coefficient +
interpolation scheme anywhere in it; the per-sample transcendental work is
made cheap with `simd_exp` / `simd_sin` (`hal/simd.h:13-16`) and NR
reciprocals instead.

### 5.2 SineOscillator (linear-through-zero FM + PM reference)

`mods/core/objects/oscillators/SineOscillator.h:24-29`: Inlets `V/Oct`,
`Sync`, `Fundamental`, `Phase`, `Feedback`. The only Parameter is internal
phase state for preset persistence
(`mods/core/objects/oscillators/SineOscillator.cpp:24-25`).

`SineOscillator.cpp:51-86`: per sample (NEON 4-wide) it computes the phase
increment `dP = samplePeriod * freq[i]` scaled by `simd_exp(voct[i] * 10*ln2)`
(lines 55-59), accumulates phase with sample-accurate sync (lines 62-75), and
sums the Phase inlet buffer directly into the sine argument with feedback
(line 84: `last = simd_sin((q + p + last * f) * pi2)`). This is why its PM/FM
sidebands are exact: the modulation enters the math at every sample. Cost:
0.97% (comment, line 32).

---

## 6. Habitat evidence: Canals (both patterns in one unit, plus a cautionary tale)

Current shipped state (`mods/spreadsheet/Canals.h:22-35`):

- Audio-rate: `mIn`, `mLowIn/mCentreIn/mHighIn` (normalling inputs), and
  **`mVOct`** are Inlets (lines 22-26).
- Block-rate: `mFundamental`, **`mSpan`**, **`mQuality`**, `mOutput`, `mMode`
  are Parameters (lines 31-35), sampled once per frame at
  `mods/spreadsheet/Canals.cpp:164-168` and wired in Lua via
  ParameterAdapter + tie (`mods/spreadsheet/assets/Canals.lua:116-131`).

The audio-rate cutoff path: the per-sample loop
(`mods/spreadsheet/Canals.cpp:322`) reads `voct[i]` (line 350) and calls
`innerStep` twice per output sample (2x oversampling, lines 365/369). Inside
`innerStep` (lines 249-320) the cutoff is derived per internal sample:
semitones -> Hz via the stmlib `SemitonesToRatio` LUT (lines 255-257), then
`SistersSvf::setFreq()` per SVF per internal sample.

`SistersSvf::setFreq` shows the cheap per-sample coefficient recipe
(`mods/spreadsheet/SistersSvf.h:39-54`):

```cpp
float pif = pi * normalizedFreq;
g = pif * (1.0f + pif * pif * 0.333333f);   // tan(pi f) ~ x + x^3/3
r = damping;
h = 1.0f / (1.0f + r * g + g * g);          // one fdiv per SVF per sample
```

A cubic Taylor for tan (valid to f ~ 0.4, "<1% in audio range" per the
comment at lines 41-44) plus one reciprocal. Six SVFs run this at 96 kHz
internal rate in Canals and it holds up on am335x (scalar, no NEON).

### 6.1 The Span/Quality audio-rate promotion and FULL REVERT (must read)

History (habitat git, `planning/canals-audio-rate-mod.md` on main):

- 2.8.1.6 (commit aaf3cd7): Span + Quality promoted from Parameter to Inlet
  (GainBias + connect), derived coefficients moved into the per-sample
  innerStep - the exact promotion recipe in memory
  feedback_inlet_vs_parameter_audio_rate_mod.
- 2.8.1.8 (Phase 5f): audio-rate Span rolled back - its wide exponential
  cutoff leverage popped under modulation and per-detent knob steps.
- 2.8.1.12 (commit 58705ce, Phase 5g): FULL revert to the released 2.8.1
  after a hardware A/B showed the "kept improvements" (interpolated LUT,
  soft-knee clamps) themselves added audible V/Oct/Span stepping. Canals is
  back to block-rate Span+Quality; per-sample V/Oct + 2x OS predate that work
  and remain.

Lessons that transfer to the SCF clone:

1. The Inlet-per-sample architecture itself was never the problem (the native
   Sine Osc and Ladder prove it); the pops came from (a) a control with huge
   exponential leverage being stepped by knob detents through a fast GainBias
   ramp, and (b) auxiliary "smoothing" changes that altered voicing.
2. Hardware A/B is the only acceptance test for modulation smoothness
   (analysis said the reverted code was smoother; ears said otherwise).
3. Audio-rate CV and knob-detent stepping are different problems. The
   GainBias frame-ramp (section 2.5) covers knobs for well-scaled controls;
   controls with octaves-per-detent leverage may need their own slew, tuned
   on hardware.

---

## 7. Gotchas, filter-specific costs, am335x notes

### 7.1 The block-rate trap

If a control "sounds flappy / stepped" under CV, grep the unit's Lua for
`ParameterAdapter` + `tie` on that control: that is the smoking gun
(the 375 Hz sample-and-hold at `ParameterAdapter.cpp:30` + `hardSet` at
line 40). The C++ fix is mechanical: Parameter -> Inlet, `addParameter` ->
`addInput`, `.value()` once -> `buf[i]` in the loop; Lua: adapter+tie ->
GainBias+connect (+MinMax for the readout). Serialized value lives in the
Lua-side GainBias Bias, so no C++ serialization change. Editing the header
regenerates the SWIG wrapper via mod.mk SWIG_HEADER_DEPS, but force-clean the
wrapper if in doubt (memory feedback_swig_header_dep).

### 7.2 Coefficient recompute strategy for a filter

What existing code actually does:

- **Stock ladder: full per-sample recompute** with SIMD polynomial
  approximations (`simd_exp`, `simd_sin`) and NR reciprocals, staged as a
  separate 4-wide coefficient pass into pool scratch frames, then a scalar-ish
  state loop (StereoLadderFilter.cpp:78-118 / 135-165). 1.28% CPU.
- **Canals: per-sample recompute, scalar**, LUT for semitone->ratio + cubic
  tan approx + one fdiv per SVF (SistersSvf.h:39-54). Six SVFs at 2x OS,
  acceptable on am335x.
- **Nobody** in the examined code does block-rate coefficients with
  per-sample interpolation for a modulatable cutoff; interpolation appears
  only where the source is inherently block-rate (GainBias crossfading its
  scalars across the frame, GainBias.cpp:50-55; same trick in
  ParameterAdapter's probe output).

Cost rules for the per-sample path on Cortex-A8:

- Avoid `tanf`/`expf` libm calls per sample; use polynomial approximations
  (cubic tan is fine below f ~ 0.4) or `simd_exp` when NEONized.
- One reciprocal per sample per filter is proven affordable (SistersSvf); in
  NEON use `vrecpeq_f32` + NR steps instead of division
  (StereoLadderFilter.cpp:106-110).
- LUT-based per-sample derivation works (stmlib SemitonesToRatio in Canals)
  but LUT gathers block cross-voice NEON (memory
  feedback_neon_no_gather_lut_dsp) and interpolated-vs-truncated LUT choices
  are voicing-sensitive (Canals revert). Keep the pitch LUT scalar or use a
  polynomial.
- Standing am335x rules apply: `-fno-tree-vectorize` on am335x (top-priority
  memory), no doubles in hot loops, NEON state in class members not stack
  locals, no per-sample work inside runtime-tier branches, `FRAMELENGTH` not
  `globalConfig.frameLength` in loops (od/config.h:28-33).

### 7.3 Resonance at audio rate

Resonance is cheap to make audio-rate: in both the stock ladder (R computed
from `res[i]` each sample, StereoLadderFilter.cpp:112) and the SistersSvf
(`r = damping` is just stored; the cost is in `h`'s reciprocal which cutoff
already forces) the resonance inlet adds essentially nothing beyond the
per-sample cutoff math already being paid. A stability clamp belongs in-loop:
the ladder clips resonance upstream with a Clipper object in Lua
(LadderFilterUnit.lua:43-45,53-54), Canals clamps damping in C++.

---

## 8. Recommendations for the SCF clone (audio-rate cutoff + resonance)

1. **Day-one port list, C++**: `In` (x2 sides), `V/Oct` (x2), `Resonance`,
   and the switch-clock / aliasing-relevant target as `od::Inlet` +
   `addInput()`. Reserve `od::Parameter` for genuinely block-rate/discrete
   config: mode select, aliasing min/max (N=50/100), output picker, clock
   routing. Follow the SineOscillator/StereoLadderFilter port map as the
   template (every continuously-audible control = Inlet).
2. **Lua**: GainBias object + `connect` + MinMax + `addMonoBranch` for each
   audio-rate control (copy LadderFilterUnit.lua:40-58); ParameterAdapter +
   `tie` only for the discrete/config controls. The view layer is identical
   either way, so there is no UI cost to doing it right from the start.
3. **Coefficient path**: recompute per (internal) sample from the inlet
   buffers, Canals-style: pitch map via LUT or polynomial, cubic Taylor
   tan for g, one reciprocal for h. Do NOT build a block-rate-coefficient +
   interpolation scheme; no precedent exists and the stock units prove
   per-sample recompute fits the budget (1.28% for the stereo ladder).
   If NEONizing later, stage a 4-wide coefficient pass into
   `AudioThread::getFrame()` scratch frames like the ladder.
4. **The SCF's variable internal rate helps**: coefficients only need
   recomputing at the modeled switch-clock rate (clamped to host rate per the
   design note), so the per-sample coefficient cost shrinks exactly where the
   filter is gritty/cheap and is at most host-rate elsewhere. Audio-rate
   inlets still get read at 48 kHz; consume them at the internal step times
   (Canals' innerStep interpolation pattern, Canals.cpp:249-320, is the
   worked example, just downsampling instead of oversampling).
5. **Heed the Canals revert** (section 6.1): budget for hardware A/B of
   modulation smoothness early; treat knob-detent stepping on
   high-exponential-leverage controls as a separate, tunable slew concern
   (GainBias's built-in frame ramp may or may not suffice for cutoff);
   resist "smoother on paper" cutoff-path changes without an ear test. The
   Lester's bounded Q (no self-osc) makes its resonance a much weaker pop
   amplifier than Canals' self-oscillating SVF, which is in our favor.
6. **Verify with the checklist**: every Outlet `addOutput()`ed; SWIG wrapper
   clean after header edits; both arches built; objdump for NEON alignment
   hints if NEON is used; `-ftree-vectorize` disabled on am335x.
