# Larets — true stereo

Status: planning. Spreadsheet package version bump TBD.

## Problem

Larets currently runs mono only. On a stereo chain it accepts In1,
discards In2, processes mono through one Internal pipeline, and
duplicates the mono result to Out1+Out2. R-channel content is
dropped entirely. For a stutter / reverse / shuffle / pitch / comb
effect on stereo material this is unacceptable — those effects
need to preserve channel identity through the cuts.

User goal: true stereo operation.

## Branching patterns surveyed in habitat

### Pattern A: dual-instance mirroring

Used by: Pecto, Canals, Discont, Filterbank, Helicase. The Lua
side instantiates a second C++ Object (`opR`) conditionally when
`channelCount > 1`, mirrors every connect/tie:

```lua
function X:onLoadGraph(channelCount)
  local op = self:addObject("op", libX.X())
  connect(self, "In1", op, "In"); connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    local opR = self:addObject("opR", libX.X())
    connect(self, "In2", opR, "In"); connect(opR, "Out", self, "Out2")
  end
  tie(op, "Param", adapter, "Out")
  if channelCount > 1 then tie(self.objects.opR, "Param", adapter, "Out") end
end
```

C++ object stays mono. Two independent instances run in parallel
with independent state.

Works for: units whose state is genuinely independent on L vs R —
no shared sequencer, no shared envelope, no shared random source,
no cross-channel coupling that needs to stay in lockstep.

Pecto fits (per-sample comb state is naturally independent).
Canals / Discont / Filterbank / Helicase fit for the same reason.

### Pattern B: internal stereo Object

Used by: multi-band stages that need linked sidechain (Impasto's
opR sync issue per `MEMORY.md` is a symptom of forcing pattern A
where pattern B would have been cleaner).

C++ exposes In L / In R / Out L / Out R inlets/outlets. Internal
state has a shared section (sequencer / envelope / band-split
filter outputs) and a paired per-channel section. Lua only adds
the R wiring when `channelCount > 1`; the C++ either always
processes both channels (cheap, R-out writes to a zero-aliased
buffer when disconnected) or gates R work behind an `mIsStereo`
flag set at first `process()`.

Works for: units with cross-channel state that must be coherent
to preserve the audio result (linked compressors, shared
sequencers, shared randomness).

## Why Larets needs pattern B

Cross-channel state that must be shared in stereo:
- `mStep`, `mTickCount`, `mDivCount` — sequencer position
- `mClockPeriodSamples`, `mSamplesSinceLastClock` — clock period
  tracking
- `type[]`, `param[]`, `ticks[]` — the 16-step program
- `compDetector` — CPR envelope follower
- `shuffleOffset` — random pick per loop wrap (`FX_SHUFFLE`)
- `lRandFloat()` calls in `applyTransform()` — randomized step
  data

Per-channel state that is legitimately independent on L vs R:
- `buffer[kBufferSize]` — audio circular buffer
- `writePos`, `readPos`
- `ic1eq`, `ic2eq` — SVF (FX_FILTER)
- `holdSample`, `decimCounter` — FX_DOWNSAMPLE
- `pitchPhase` — FX_PITCHSHIFT
- `prevOutput`, `crossfadeCounter` — step-boundary output
  crossfade

If we use pattern A (dual-instance), three concrete failures:

1. **CPR compressor stereo-image smear.** Two independent
   `compDetector`s tracking L and R independently produce
   asymmetric gain reduction on transients. Whole point of
   stereo is image preservation; this destroys it. Fix needs
   linked detection (max(|L|, |R|) drives one envelope, same
   gain applied to both channels) — impossible across two
   independent C++ instances without a cross-link parameter the
   dual-instance pattern doesn't support.

2. **xform randomization drift.** `applyTransform()` mutates
   step data via `lRandFloat()` (thread-local LCG stream).
   Same Transform gate edge → both instances call
   `applyTransform()` → each draws independent randoms → step
   programs diverge → L and R run different effects after the
   first xform fire.

3. **Shuffle slice drift.** `FX_SHUFFLE` picks a fresh random
   offset on each loop wrap (audio rate). Two instances → two
   independent picks → L and R play different fragments of the
   audio buffer whenever shuffle is active.

Pattern A is the right call for Pecto because Pecto has none of
these. Pattern A is the wrong call for Larets because Larets has
all three.

## Architecture: internal-stereo Larets

### Object surface

```cpp
class Larets : public od::Object {
  od::Inlet  mInL{"In L"};
  od::Inlet  mInR{"In R"};      // NEW
  od::Inlet  mClock{"Clock"};
  od::Inlet  mReset{"Reset"};
  od::Inlet  mTransform{"Transform"};
  od::Outlet mOutL{"Out L"};
  od::Outlet mOutR{"Out R"};    // NEW
  // all Parameter / Option declarations unchanged
};
```

Renaming the old `mIn` / `mOut` to `mInL` / `mOutL` is a wire-name
change that breaks Lua connect() strings — but the Lua change
is in the same commit, so this is fine. Old saved patches
referencing "In" / "Out" by name will not deserialize cleanly;
this is acceptable as a one-time break since the unit was
documented as in-progress (and the package bumps a minor version
to signal the change). Alternative: keep `mIn` / `mOut` aliases
mapped to L for back-compat. Recommendation: clean break, no
aliases — the documented breakage is simpler than maintaining
two name schemes.

### Internal struct split

```cpp
struct Larets::Internal {
  // ----- shared sequencer + step program -----
  int    type[kMaxSteps];
  float  param[kMaxSteps];
  int    ticks[kMaxSteps];
  int    tmpType[kMaxSteps];
  float  tmpParam[kMaxSteps];
  int    tmpTicks[kMaxSteps];
  float  stepProgress;
  float  compDetector;       // LINKED envelope (max-of-L-R)
  int    shuffleOffset;      // shared per-beat random pick
  int    stepStartPos;       // index value, shared
  float  vizRing[128];       // viz tap (drive from L+R mix)
  int    vizPos, vizDecimCounter;

  // ----- per-channel audio state -----
  struct ChannelState {
    float buffer[kBufferSize];
    int   writePos;
    float readPos;
    float ic1eq, ic2eq;
    float holdSample;
    int   decimCounter;
    float prevOutput;
    int   crossfadeCounter;
    float pitchPhase;
  } ch[2];

  void Init() {
    // ... shared init ...
    for (int c = 0; c < 2; c++) {
      memset(ch[c].buffer, 0, sizeof(ch[c].buffer));
      ch[c].writePos = 0;
      ch[c].readPos  = 0.0f;
      ch[c].ic1eq = ch[c].ic2eq = 0.0f;
      ch[c].holdSample = 0.0f;
      ch[c].decimCounter = 0;
      ch[c].prevOutput = 0.0f;
      ch[c].crossfadeCounter = 0;
      ch[c].pitchPhase = 0.0f;
    }
  }
};
```

### processEffect signature change

```cpp
// old: float processEffect(float input, int type, float param, float sp);
// new:
float processEffect(float input, Internal::ChannelState &cs,
                    int type, float param, float sp);
```

Body uses `cs.foo` instead of `s.foo` for every audio-state field.
`stepStartPos` stays read from the shared `s.stepStartPos` (it's
a sequencer-driven index, used to index into `cs.buffer`).
`shuffleOffset` reads from `s.shuffleOffset` (shared per-beat pick).
The random pick in `FX_SHUFFLE` MOVES from `processEffect` to the
sequencer-advance section so it runs once per loop wrap, not per
channel.

### process() shape

```cpp
void Larets::process() {
  Internal &s = *mpInternal;
  float *inL = mInL.buffer(),  *inR = mInR.buffer();
  float *outL = mOutL.buffer(), *outR = mOutR.buffer();
  float *clock = mClock.buffer(), *reset = mReset.buffer(), *xform = mTransform.buffer();
  // ... existing param reads + comp coefficient prep unchanged ...

  // existing xform-edge sweep unchanged

  // existing effTicks compute unchanged

  for (int i = 0; i < FRAMELENGTH; i++) {
    // write inputs to both channel buffers
    s.ch[0].buffer[s.ch[0].writePos] = inL[i];
    s.ch[0].writePos = (s.ch[0].writePos + 1) % kBufferSize;
    s.ch[1].buffer[s.ch[1].writePos] = inR[i];
    s.ch[1].writePos = (s.ch[1].writePos + 1) % kBufferSize;

    // sequencer advance (clock/reset edge detection, mStep bump,
    // shuffle random pick on wrap) — all shared
    advanceSequencer(i, clock, reset);

    // per-channel effect processing — same effType/effParam/sp
    int effType = s.type[mStep % stepCount];
    float effParam = s.param[mStep % stepCount] + paramOffset;
    // ... clamp effParam ...
    float wetL = processEffect(inL[i], s.ch[0], effType, effParam, s.stepProgress);
    float wetR = processEffect(inR[i], s.ch[1], effType, effParam, s.stepProgress);

    // per-channel step-boundary crossfade
    if (s.ch[0].crossfadeCounter > 0) { /* L crossfade */ }
    if (s.ch[1].crossfadeCounter > 0) { /* R crossfade */ }

    float mixedL = inL[i] * (1.0f - mix) + wetL * mix;
    float mixedR = inR[i] * (1.0f - mix) + wetR * mix;

    // LINKED CPR: max-of-L-R drives the single detector
    if (compActive) {
      float absLevel = fmaxf(fabsf(mixedL), fabsf(mixedR));
      float coeff = absLevel > s.compDetector ? compRiseCoeff : compFallCoeff;
      s.compDetector = coeff * s.compDetector + (1.0f - coeff) * absLevel;
      float levelDb = 20.0f * fast_log10(s.compDetector + 1e-10f);
      float overDb = levelDb - compThresholdDb;
      if (overDb < 0.0f) overDb = 0.0f;
      float reductionDb = overDb * (1.0f - compRatioI);
      float gain = fast_fromDb(-reductionDb) * compMakeupGain;
      mixedL *= gain;
      mixedR *= gain;
    }

    outL[i] = mixedL * outputLevel;
    outR[i] = mixedR * outputLevel;

    // viz tap — drive from L+R sum so the readout reflects both
    if (++s.vizDecimCounter >= 8) {
      s.vizDecimCounter = 0;
      s.vizRing[s.vizPos] = 0.5f * (outL[i] + outR[i]);
      s.vizPos = (s.vizPos + 1) & 127;
    }
  }
}
```

The crossfadeCounter is per-channel because each channel's
prevOutput differs. Step boundary detection is shared (same
sample triggers the crossfade on both), but the blend math runs
twice with each channel's stored prevOutput.

### Lua wiring

```lua
function Larets:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Larets())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  -- remainder unchanged: clock comparator, reset, xform,
  -- all ParameterAdapter ties, all addMonoBranch calls.
end
```

When inserted on a mono chain, mInR reads from the aliased zero
buffer and mOutR writes to a discard buffer. R-channel work
still runs but produces silence. CPU cost in mono: ~1.7-1.9x
the current mono cost (effect bodies double; sequencer / CPR /
xform amortize). Acceptable for first cut. If profiling shows it
matters, add an `mIsStereo` bool detected at first `process()`
by checking `mInR.mInwardConnection != nullptr`, gate R-channel
work behind it.

### Compatibility / breakage

- Lua-side inlet/outlet names change: `"In"` -> `"In L"`,
  `"Out"` -> `"Out L"`, new `"In R"` / `"Out R"`. Old saved
  patches that reference these by name (any `connect(... , "In")`
  in a custom or copied patch script) won't deserialize.
- This is acceptable. The user has not shipped Larets to a wide
  audience in mono-only state, and saved patches that just used
  the unit normally (via menu insert) will reconnect under the
  new chain wiring. The breakage surface is custom patch scripts
  only.
- Spreadsheet package version: bump to next patch (current
  spreadsheet version is in `mods/spreadsheet/mod.mk`; we'll
  read and bump at implementation time).

## Hazards to manage

- **NEON traps**: per `MEMORY.md` rules, the per-channel state
  loop is the kind of structure GCC `-O3 -ffast-math` can
  auto-vectorize. The `mod.mk` already enforces
  `-fno-tree-vectorize` for am335x globally, which is the durable
  defense. Verify post-build with `tools/check-neon-hints.sh`.
- **AAPCS NEON spill barrier**: not applicable here — no
  engine-switch / runtime branched dispatch added; the channel
  loop is straight-line.
- **SWIG header dep**: Larets.h is `%include`'d via the existing
  spreadsheet.cpp.swig; `mod.mk` tracks `*.h` deps for SWIG. New
  inlets/outlets are visible to Lua automatically.
- **Lua param round-trip**: all existing serialize/deserialize
  for step data, Options, ParameterAdapter biases keeps working
  unchanged — no parameter signatures changed.
- **Step data array sharing**: confirm that step data is
  ONLY mutated via `setStepType`/`setStepParam`/`setStepTicks`
  (UI-side calls) and `applyTransform()` (audio-thread-side).
  Both write to the single shared `type[]`/`param[]`/`ticks[]`
  arrays — no per-channel duplication needed for correctness.

## Phasing

Single phase. The change is self-contained: C++ Object surface
+ Lua wiring + viz tap source. Ship as one commit.

1. Edit `Larets.h`: rename inlets/outlets, add R variants.
2. Edit `Larets.cpp`:
   - Split Internal struct into shared + ch[2].
   - Move shuffleOffset random pick from `processEffect` into
     sequencer-advance (when readPos==0 at loop wrap, do the
     pick once per beat).
   - Reparameterize `processEffect` on `ChannelState&`.
   - Rewrite `process()` with dual-channel buffer writes, shared
     sequencer, dual `processEffect` calls, linked CPR, paired
     crossfade, dual outputs, mixed viz tap.
3. Edit `Larets.lua`: rename `In`/`Out` connect strings, add
   conditional R wiring.
4. Bump `mods/spreadsheet/mod.mk` PKGVERSION.
5. Build linux + am335x; install linux to `~/.od/rear/`.
6. Run `tools/check-neon-hints.sh` on the spreadsheet swig
   wrapper (and on Larets.o if a .o is emitted).
7. Emu smoke test:
   - Insert on mono chain, verify all 10 effects sound identical
     to pre-change.
   - Insert on stereo chain with stereo material, verify L/R are
     processed independently per buffer but step transitions and
     xform behavior are identical.
   - Heavy compression test: feed a stereo transient, verify L
     and R receive identical gain reduction (no image smear).
   - Shuffle test: verify same shuffle slice plays on L and R.
   - Quicksave + reload on both mono and stereo patches.
8. Hardware smoke test (required before ship per emu-vs-hw chain):
   - All 10 effects on mono chain, mono input.
   - All 10 effects on stereo chain, stereo input.
   - CPR aggressive limit on transient stereo material.
   - Insert / delete / quicksave / engine swap stability.

## Open follow-ups

- Per-channel CPR detector option ("link" vs "dual") if anyone
  ever wants the dual-mono character of independent compression.
  Not for v1.
- Stereo width control (M/S decode → process → encode) at the
  output. Not for v1; the channel-independent processing already
  preserves whatever input stereo width was present.
- Cross-channel feedback option for FX_COMB / FX_DELAY (read
  from the other channel's buffer for richer stereo). Not for v1.

## Related memories

- `feedback_disable_tree_vectorize_am335x` — am335x mod.mk
  already enforces; verify still in effect.
- `feedback_no_lazy_paths` — don't strip features to avoid
  per-channel complexity; the linked CPR is the principled
  answer.
- `feedback_neon_voice_bus_template` — not applicable here (no
  cross-voice NEON), but the broader lesson on AoS->SoA when
  the channels share work applies if we ever want to NEON-fuse
  the L/R effect bodies.
- `feedback_always_build_both_arches` — every build is linux +
  am335x.
- `feedback_linux_build_auto_install` — cp to `~/.od/rear/`
  after every linux build.
- `feedback_package_version_bump` — bump spreadsheet PKGVERSION.
