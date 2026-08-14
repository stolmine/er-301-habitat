# Airwindows batch 2: 11-plugin port plan

Status: **planning, hotspot-reviewed** (2026-08-11). User-selected batch, surveyed against the
local repo at `~/repos/Open303`'s sibling `~/repos/airwindows` (current to
2026-05-30, commit 1bac4c238).

Companion docs, read both before executing:
- `planning/house-atom-architecture.md` - the atom/harness/Lua-composition
  pattern these land in.
- `planning/house-ports-optimization.md` - the previous pass, including the
  2026-07-22 OUTCOME that must not be re-litigated (see §3 below).

Ledger: `aw-batch2-ports` (this batch), `house-atom-library`, `port-mit-direct`.
License: Airwindows is MIT throughout; attribution to Chris Johnson stays in
every vendored file and in the README, per `branding-attribution-policy`.

## 1. Scope and name resolution

Requested: Mackity, Suzan, Dynamics3, TakeCare, Shortbuss, cloudcoat, subtight
redux, creature, everyslew, orbitkick, powersag2.

Ten resolve exactly. **"SubTight Redux" does not exist** - there is no plugin
with "Redux" in its name anywhere in the Airwindows repo. `SubTight` is
surveyed in its place; confirm before building whether that was the intent.

## 2. Two cross-cutting findings that shape everything else

### 2a. Per-sample `sin()` is SAFE on am335x, and it is the cost story

Eight of the eleven call `sin()`/`cos()` per sample. That looked like a blocker
under `feedback_package_trig_lut`, which is why it was checked first. It is not
a blocker: **Galactic already ships with 4 double `sin()` per sample and runs on
hardware.** That memory is explicitly `sinf`/`cosf`-specific ("only sinf/cosf
are confirmed bad") and these are all double `sin()`. Correctness risk closed by
evidence, not by argument.

What remains is CPU. The repo's own hardware-anchored figure is in `Galactic.h`:
4 sin/sample carries a **~10% CPU floor**, and `house-ports-optimization.md`
step 5 projects the polynomial substitution taking that to ~2%.
- CAVEAT, stated because this repo has been burned by exactly this: that ~10% is
  an ESTIMATE, and the previous pass proved static estimates can oversell by an
  order of magnitude (§3). It is more credible than the f64-op count was - a
  libm call is a serialized call with internal branching, which load latency
  cannot hide the way it hides inline double math - but it is still owed a
  hardware measurement before anyone budgets against it.

**Consequence: one shared fast-sin atom is the gate on six of these eleven, and
it retro-improves two shipped units** (Galactic step 5, BrightAmbience3 step 7,
both already flagged in the previous pass). Build it once, price it once.

Three validated in-repo precedents to lift from rather than inventing:
`field::fastSin` (anamnesis, templated exact/fast twins off one formula source),
`DrumVoiceSineLUT.h` (spreadsheet), and `spiralFastSaturate` (already shipped in
ChromeOxide specifically as a libm-sin replacement, 0.45% error, called
inaudible). The anamnesis twin-template pattern is the one to copy: it makes the
exact-vs-fast A/B a single compile flag.

### 2b. Strip the dither, unconditionally, from all eleven

Every Airwindows plugin ends with a 32-bit float dither block: `frexpf` plus
`pow(2, expon+62)` per sample per channel. It exists to dither a VST's output
word length. Inside the ER-301's internal float chain it is pure waste. The
shipped house ports already document removing it. This deletes a `pow()` and a
`frexpf()` per sample per channel from every unit in the batch before any real
optimization starts, and it is not a tone change in this context.

## 3. Why the 2026-07-22 "hybrid float is low-value" outcome does NOT transfer

`house-ports-optimization.md` closes with a hard-won result: kWoodRoom dropped
only ~1% CPU mono despite its static f64-op count falling 1087 -> 270, because
the AW reverbs are **gather-bound** (scattered single-element reads across
dozens of delay arrays), not FLOP-bound. The lesson recorded there is "do not
chase f64-op counts on gather-bound delay/reverb DSP."

That lesson is correct and stands. It also **does not apply to most of this
batch**, and the distinction is the single most useful thing in this document:

| class | plugins | dominant cost | does the 2026-07-22 lesson apply? |
|---|---|---|---|
| **Buffer-less, compute-bound** | Dynamics3, EverySlew, Mackity, ShortBuss, Suzan, SubTight, Creature, OrbitKick | ALU + libm calls; state is a handful of scalars or <1 KB | **NO.** There is no gather to hide behind. libm and double math ARE the loop |
| **Gather-bound** | CloudCoat (16 lines, 160 KB), TakeCare (18 lines, 2.25 MB) | scattered delay reads | **YES.** Treat exactly like the six reverbs: measure first, expect FLOP work to disappoint |
| **Streaming-bound** | PowerSag2 (131 KB, two sequential running-sum streams) | sequential bandwidth, not scatter | Partly. Sequential access prefetches well; cheap CPU either way |

So: the fast-sin work is expected to pay on the compute-bound eight in a way it
did not on the reverbs. That expectation is still a hypothesis and step 0 below
exists to test it before the batch is committed to.

## 4. The survey

`sin/smp` is per output sample, stereo, before any polynomial substitution.
Buffer sizes are float, stereo.

| plugin | what the code actually does | params | sin/smp | buffers | class |
|---|---|---|---|---|---|
| **Dynamics3** | Compressor with a Bezier-smoothed control signal. No libm at all - `fmin`/`fmax`/`fabs` only | Thresh, Attack, Release, Dry/Wet | 0 | tiny | compute |
| **EverySlew** | ~10 cascaded slew-clip stages with golden-ratio prediction, pure arithmetic | Slew, Depth, Halo, Inv/Wet | 0 | small fixed | compute |
| **Mackity** | Mackie preamp: 2 one-pole HPFs, 2 DF1 biquads, `pow(x,5)*0.1768` saturation | In Trim, Out Pad | 0 | 30 doubles | compute |
| **PowerSag2** | Power-supply sag: 16384-sample running sum of abs(x) drives a gain collapse | Range, Inv/Wet | 0 (2 sqrt) | 131 KB | streaming |
| **OrbitKick** | **A kick-drum synth.** Input transient over Thresh triggers a falling-frequency sine orbit that REPLACES the signal | Drop, Shape, Start, Finish, Thresh, Dry/Wet | 1 (+cbrt) | none | compute |
| **ShortBuss** | Subharmonic generator, feedback sin shaper | ShortBs, Dry/Wet | 4 | none | compute |
| **Suzan** | 3 cascaded Chamberlin SVFs with `sin()` cross-injection between stages | Freq, Reso, Output | 6 | none | compute |
| **SubTight** | Subharmonic: up to 16 sin stages, clipped, boosted 24 dB, subtracted from dry | Trim, Steep | up to 32 | 44 doubles | compute |
| **Creature** | Up to 33 cascaded sin slew stages | Drive, Depth, Inv/Wet | up to 66 | 204 doubles | compute |
| **CloudCoat** | 16-line reverb with an undersampled core | Select, Sustain, Dry/Wet | 8 | 160 KB | gather |
| **TakeCare** | Randomized chorus/vibrato over a derez (undersampled) core | Speed, Rando, Depth, Regen, Derez, Buffer, Output, Dry/Wet | 11 (derez-gated) | **2.25 MB** | gather |

Two structural notes from reading the source:
- **SubTight and Creature are the same algorithm.** SubTight's own comment says
  it is "modified Creature code" - a cascade of `sin`-based slew stages, with
  SubTight clipping the result and subtracting it from dry to make subs. Port
  ONE engine and expose two voicings rather than two independent atoms.
- **OrbitKick is a generator, not a processor.** It discards the input except as
  a trigger. Per `feedback_atoms_as_components` that clears the promotion bar
  for a standalone unit; the rest are atoms first.

## 5. Catalog overlap

- **CloudCoat would be the seventh Airwindows reverb** (kWoodRoom, WoodenBox,
  CreamCoat, BrightAmbience3, Verbity, Galactic) and sits in the same "Coat"
  family as CreamCoat. Weakest catalog case in the batch. Recommend deferring
  until someone A/Bs it against CreamCoat and wants it anyway.
- **Dynamics3** vs Impasto: Impasto is 3-band; this is a single-band character
  compressor. Distinct enough to earn a slot.
- **Suzan** joins a crowded filter shelf (Canals, Vitrail, DJ Filter, Tilt EQ,
  Filterbank, Rings) but the sin cross-injection is a character none of them
  have.
- **OrbitKick** has zero overlap with the AW set and lands next to the drum line
  (Ngoma, Visadhara, Peaks BD). Strongest novelty case.

## 6. Execution order

**Step 0 - measure before committing (blocking).** Port ONE compute-bound unit
with a `sin()` in it (ShortBuss, the smallest at 118 LOC and 4 sin/sample) as an
exact double port, hardware-measure it, then swap in the fast sin and measure
again. That single number tests §2a's ~10% anchor and §3's compute-bound
hypothesis at once. Everything after this is sized off that measurement. Do not
batch-port on the estimate.

**Phase 1 - the free tier (no new machinery).** Dynamics3, EverySlew, Mackity.
Zero libm on the sample path between them once Mackity's `pow(x,5)` becomes five
multiplies. Ship as `house` atoms plus thin Lua units.

**Phase 2 - the shared fast-sin atom.** Twin-templated exact/fast off one
formula source, per the anamnesis pattern, so the A/B is one flag. Retro-apply
to Galactic (step 5) and BrightAmbience3 (step 7) from the previous pass, which
is where its cost is amortized.

**Phase 3 - the sin-dependent compute units.** ShortBuss, Suzan, then the shared
Creature/SubTight engine with two voicings.

**Phase 4 - OrbitKick** as a standalone unit rather than an atom.

**Phase 5 - deferred.** PowerSag2 (cheap but the 131 KB buffer wants a look),
CloudCoat (overlap), TakeCare (2.25 MB, wants the Petrichor/Pecto 3-pass gather
treatment before it is viable).

## 7. Character-changing items needing sign-off

Per `house-ports-optimization.md` §4, approximations get flagged, not assumed:

1. **Fast sin everywhere it replaces a libm sin in a FEEDBACK path.** In Suzan,
   SubTight and Creature the `sin()` is not an LFO - it is the nonlinearity
   inside a recursive slew/filter stage, so a polynomial changes the curve
   exactly where the feedback is hottest. Precedent exists (ChromeOxide shipped
   this trade at 0.45% error) but it is a tone decision, not a free win. A/B
   required per unit, and the exact twin stays available.
2. **Default remapping.** Per `feedback_aw_param_default_subtle`, several
   default to a subtle setting (Creature Drive 0.26, PowerSag2 Range 0.3,
   Mackity In Trim 0.1). Remap so the audible regime fills the travel.
3. **Mono vs stereo.** Most of these are stereo; the ER-301 convention is a mono
   atom with a stereo composition where it matters. SubTight/Creature/Suzan have
   independent per-channel state and mono-ize cleanly. OrbitKick already
   collapses to a single orbit shared by both channels.

## 8. Verification protocol (every unit, non-negotiable)

- Bump PKGVERSION 4th digit per dev build (the device only re-extracts on a
  version change).
- Build BOTH arches; auto-install the linux pkg to `~/.od/rear/`.
- All three lints: `check-graphic-virtual-defs.sh`, `check-neon-hints.sh`,
  `check-audio-stack.sh`.
- Offline A/B against the unmodified upstream plugin before claiming fidelity -
  the Open303 port's harness pattern (`/tmp/o3ref` vs `/tmp/o3test`, correlation
  after group-delay alignment plus per-bin spectral comparison) is the model.
- Emu insert smoke test per unit, modelled on
  `er-301/tests/emu/91-mordant-insert-smoke.test`.
- Hardware CPU% before claiming any optimization number. The 2026-07-22 outcome
  is the standing reminder of what happens otherwise.

## 8b. HOTSPOT REVIEW (2026-08-11) - corrections to the above

A pre-implementation hotspot pass ran against this plan before any code. Four of
its corrections were independently re-verified against the source; they change
the numbers and the sequencing, so they are recorded here rather than silently
folded in. **Where this section conflicts with §2 or §4, this section wins.**

### The sin anchor in §2a was wrong in both directions

1. **Galactic has 2 `sin()` per sample, not 4.** VERIFIED: `Galactic.h:215-216`
   are the only call sites and they sit in one stereo loop (`:192`). The
   header's own comment at `Galactic.h:53` ("2 per sample per channel = 4
   total") mis-describes its own code, and its "~50ns each" is arithmetically
   inconsistent with the "~10% floor" on the next line (4 x 50ns is ~1% of a
   20.8us sample period, not 10%). That comment should not anchor anything and
   is worth correcting in place when someone next touches the file.
2. **A double `sin()` costs far more than 50ns.** STATIC disassembly of the
   newlib fdlibm the am335x build links: `__kernel_sin` is 13 f64 ops, 9 of them
   FMAC-class, and the fast path only applies for |x| <= pi/4 - the feedback
   shapers in this batch routinely exceed that and take `__ieee754_rem_pio2` on
   top. Against Cortex-A8 VFPLite timings (non-pipelined, FMACD ~18-19 cy) that
   is **~250-300 cycles small-arg, ~450-550 with reduction, i.e. ~300-500ns**.
   Cross-check: ChromeOxide's shipped poly swap is recorded at "~20x faster than
   libm sin" (`ChromeOxide.h:17`), consistent with ~300 cycles against a ~15
   cycle poly.

Net: 4 sin/sample stereo is **~6-10% CPU** (ESTIMATED, medium confidence), and
**Galactic's own sin floor is only ~3-5%**, not ~10%.

**Consequence for Phase 2: the retrofit justification is withdrawn.** Galactic
and BrightAmbience3 stand to gain ~2-4 points each, not ~8, and the 2026-07-22
record shows the sin-to-poly swap was already declined on both as a character
change. Re-opening that needs fresh sign-off and is now optional appendix work.
**The real customer for the fast-sin atom is Creature and SubTight, where it is
a feasibility gate rather than an optimization** - see below.

### Creature is not portable as an exact double port

At 48k `stages = B^2*32*sqrt(overallscale)+1` reaches 34, so 68 libm sin per
sample stereo at max Depth: **~90-150% CPU, i.e. a full core** (ESTIMATED).
Default Depth 0.26 is only ~3 stages and lands ~8-13%. A unit that is fine at
default and unusable at max is a design defect, not a tuning issue. Options are
(a) ship it only behind the fast sin, which collapses max to ~10-20%, or (b) cap
the Depth range, which is a character decision needing sign-off. SubTight has
the same shape, less severely (max ~45-70%).

### Three defaults are wrong in ways §7 missed

- **ShortBuss is a bit-exact IDENTITY at its default.** VERIFIED:
  `sbScale = (pow((A*2)-1, 3)*0.001)/sqrt(overallscale)` (`ShortBussProc.cpp:21`)
  and A defaults to 0.5, so the cube of zero makes sbScale exactly 0 and the
  state never accumulates. The unit passes audio through untouched at its
  default setting. This is the worst default in the batch and §7 missed it.
- **EverySlew's default is its MAXIMUM cost.** VERIFIED: `stages = (1.0-B)*9.99`
  (`EverySlewProc.cpp:22`) is the loop's START index
  (`for (x = stages; x < gslew_total; x += 5)`), so B defaulting to 1.0 gives
  stages = 0 and runs every stage. The knob adds stages as it turns DOWN. Budget
  EverySlew at ~8-15%, not "free", and remap the sense.
- **Neither CloudCoat nor TakeCare gets any undersampling relief at 48k.**
  VERIFIED: CloudCoat's `cycleEnd = floor(overallscale)` = floor(48000/44100) =
  floor(1.088) = **1** (`CloudCoatProc.cpp:17-20`), so its core runs every
  sample. TakeCare at its default Derez (E=0.5) resolves to derezB = 1.0 and
  bezFraction = 1, so its bez gate opens every sample too. The §4 table's
  "derez-gated" reads as mitigation; at defaults the gate is wide open.
  TakeCare re-estimated at ~40-80% with a 4.7 MB double working set as written.

### Landmines to carry into implementation

1. **TakeCare's Rando knob needs the fpd RNG.** `TakeCareProc.cpp:78` derives
   its randomization from `fpdL`. The shipped port template drops fpd wholesale
   (it is only dither noise elsewhere), which would silently turn Rando into a
   constant. Keep the xorshift, drop only the dither block.
2. **NaN persists on an ER-301 in a way it does not in a DAW.** The AW denormal
   guard `fabs(x) < 1.18e-23` does not catch NaN (the comparison is false), and
   Suzan, ShortBuss, Creature and SubTight all hold recursive sin-feedback
   state, so one NaN silences the unit permanently where a plugin user would
   just reload. Suzan is the corner to prove: `freqB = pow(A, os+1)*1.22`
   (`SuzanProc.cpp:23`) with damping floored at 0.001, and a stage-C integrator
   cross-wired to bandB (`:46`, verbatim from upstream). Sweep Freq=1.0 /
   Reso=1.0 with hot input in the offline harness before hardware. A block-rate
   NaN flush (4 compares per block) is worth proposing as a flagged divergence.
3. **OrbitKick decays into denormals.** `orbit` and `speed` shrink geometrically
   with no guard (`OrbitKickProc.cpp:44-48`), reaching double-denormal ~8-10 s
   after a trigger. Not an am335x hazard (A8 VFPLite handles subnormals in
   hardware and the firmware sets no flush-to-zero), but a classic idle-CPU
   spike on the x86 emu. Apply AW's own idiom, which Mackity already uses on its
   iir states at `MackityProc.cpp:55,83`.
4. **Creature and SubTight flip output polarity as the stage knob crosses a
   boundary** (`if (stages % 2) negate`, `CreatureProc.cpp:42`). Original
   behavior, but ER-301 users will CV-modulate what a plugin user set once, so
   it will click. Document it.
5. **CloudCoat's Select re-tunes 32 lines without clearing them**
   (`CloudCoatProc.cpp:120-135`), so stale audio replays at the new tuning.
   Original behavior; document, do not "fix".
6. **Bezier timing accumulators stay double** in Dynamics3 and TakeCare - the
   same boundary the Lacquer/CreamCoat hybrid work already established.
7. Mackity's `pow(x,5)` is already free: GCC expands a constant integer exponent
   to multiplies under `-ffast-math`. Phase 1's note about it is a no-op.

### Claim D refined: share the formula, not the loop

SubTight and Creature share topology but not their per-stage kernel. Creature
uses a fixed 0.5 and a global `source` gain (`CreatureProc.cpp:37-40`); SubTight
uses a signal-dependent `scale = 0.5 + fabs(sub*0.5)`, no source gain, and a
different wrapper with a pre-trim, a +/-0.25 clip and a x16 subtract-from-dry
(`SubTightProc.cpp:40-45`). Creature's stage count also scales with
`sqrt(overallscale)`; SubTight's does not. So: **one kernel source with two
compile-time instantiations, not one runtime-parameterized loop** - a per-stage
runtime branch would both cost and collide with
`feedback_runtime_branched_dsp_dispatch`.

### Revised step 0 (supersedes §6's step 0)

The original step 0 conflated loop overhead, per-sin cost, and fidelity. Split:

- **0a. Hardware sin-cost curve, before any port.** A throwaway calibration atom
  whose `process()` runs N chained double `sin()` per sample, N selectable
  (0, 2, 4, 8, 16, 32, 68), result fed to the outlet so nothing folds away.
  Measure hardware CPU% at each N; the slope is cycles-per-sin ON THE DEVICE,
  and N=68 prices worst-case Creature before a line of it exists. Repeat with
  the fast-sin twin for the second slope. ~30 lines, and it sizes six units at
  once, converting the estimate above into a measurement.
- **0b. Fidelity offline, never on x86 for CPU.** Extend `tools/house-bench`
  with ShortBuss exact vs upstream (correlation gate) and exact vs fast-sin
  (the §7 tone A/B). Do not read CPU off x86: the Open303 bench proved x86
  inverts the am335x story, where its 4x tier measured slower than upstream.
- **0c. Then port ShortBuss** as the integration smoke, now confirming the
  calibration atom's prediction rather than discovering it. Test at A != 0.5,
  since the default is an identity.

### Revised phase order

Phase 1 (Dynamics3, Mackity, EverySlew) is unchanged and needs no fast sin and
no sign-offs, though EverySlew is ~8-15% rather than free and needs its inverted
default fixed. Phase 2 (fast-sin atom) is now justified by Creature/SubTight
feasibility rather than by the Galactic/BA3 retrofits, which drop to optional.
Phase 3 becomes ShortBuss (done in 0c), then Suzan with the corner-stability
sweep added to its gate, then Creature/SubTight as two instantiations of one
kernel header, shipped only behind the fast sin or a capped stage range. Phase 5
deferrals are confirmed and strengthened by the full-rate findings above.

## 9. NEON

No NEON is proposed anywhere in this batch. The gather-bound two inherit the
existing no-go (Cortex-A8 has no gather load). The compute-bound eight are
serial per-sample recursions - Creature and SubTight are literally cascades
where stage N+1 consumes stage N's output - so there is no cross-sample
parallelism to exploit for a mono voice. Same conclusion, same reasoning, as the
Mordant NEON verdict.
