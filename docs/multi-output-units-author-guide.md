# Multi-Output Unit Author Guide

Reference for habitat unit authors who want to expose more than two semantically-bound outputs from a single unit (Just Friends Geode taps, quadrature LFO phases, paired CV+gate envelopes, multichannel sequencers).

The framework lives in the **stolmine** firmware fork. The design rationale is in `planning/redesign/07-multi-output-units.md`. This guide is the **how**: what to do as a unit author so your unit works correctly on stolmine and degrades gracefully on vanilla ER-301 firmware.

## When to use it

Apply the **"derivable at destination" test**: can the relationship between the outputs be reconstructed downstream?

- **Phase offset between two oscillators** — fails superficially; a delay at the destination reconstructs phase. → use parallel chains.
- **Quadrature LFO (4 phases at 0/90/180/270)** — passes; reconstructing exact phase lock requires the same internal state. → multi-out unit.
- **Just Friends Geode (sympathetic taps)** — passes; the taps' relationship is the unit's whole point. → multi-out unit.
- **Paired CV+gate envelope** — passes; gate timing is internal to the envelope's evolution. → multi-out unit.
- **Multichannel sequencer** — passes; channels share a step pointer. → multi-out unit.

If outputs are independent and could just as well be parallel mono units, don't make them multi-out. The friction of subscribing two consumers separately *is* the choice to keep them independent.

## Vanilla compatibility — the load-bearing principle

A multi-out package **must work** on vanilla ER-301 firmware (degraded but non-crashing) as well as stolmine. The contract:

1. The unit's `.so` calls only **standard upstream APIs**: `od::Object`, `od::Inlet`, `od::Outlet`, `od::Parameter`. No symbols introduced by stolmine.
2. The Lua wrapper sets `args.subOutLabels` (vanilla ignores unknown args fields — harmless).
3. **Sub-out 1 is the primary** by convention. Vanilla's `Unit:getOutputSource(i)` is hardcoded to stereo (only `i==1` and `i==2` resolve); sub-outs ≥3 are invisible. So sub-out 1 must be the most useful default — chain auto-wires to it on insertion.

What this guarantees on vanilla:
- Package `.pkg` loads (no missing symbols).
- Inserting the unit works; primary (sub-out 1) auto-wires to the chain's L input.
- Sub-out 2 also resolves (it's the chain's R input on stereo chains).
- Sub-outs 3+ are silently inaccessible in vanilla's picker. No crash.

What's lost on vanilla:
- A stolmine-saved preset that wires sub-out 3 of your unit into a downstream chain will, on vanilla, drop that connection (vanilla's `Source.deserialize` calls `getOutputSource(3)` → nil → input cleared, with a log warning). Vanilla's behavior here cannot be modified; this is the cost of cross-firmware presets.

Because of (1), **don't introduce new C++ virtual methods on `od::Object`-derived classes** that your unit needs at runtime — vanilla won't have them. Stick to the C++ ABI surface that already exists in upstream.

## Lua wrapper — required pattern

```lua
local app = app
local libmymod = require "mymod.libmymod"
local Class = require "Base.Class"
local Unit = require "Unit"

local MyMultiOut = Class {}
MyMultiOut:include(Unit)

function MyMultiOut:init(args)
  args.title = "My Unit"
  args.mnemonic = "MU"

  -- Force N-channel construction (N > 2 for multi-out).
  args.channelCount = 4

  -- Sub-out labels. Length defines fan-out. Order is author-declared and
  -- semantic — NOT recency. Stolmine's local picker shows label[i] in the
  -- edge indicator overlay; M6 cycles through them.
  --
  -- Labels must be meaningful. Generic "out 1 / out 2" is rejected by the
  -- design doc. Keep them short — the indicator ply is 42px wide.
  args.subOutLabels = {"main", "aux", "cv", "gate"}

  Unit.init(self, args)
end

function MyMultiOut:onLoadGraph(channelCount)
  local dsp = self:addObject("dsp", libmymod.MyDSP())

  -- Wire all N outputs. Sub-out 1 = primary; vanilla auto-wires this to
  -- chain L. Choose the wiring of sub-out 1 carefully — it's the default
  -- a vanilla user gets.
  connect(dsp, "Out1", self, "Out1")  -- primary
  connect(dsp, "Out2", self, "Out2")
  connect(dsp, "Out3", self, "Out3")
  connect(dsp, "Out4", self, "Out4")

  -- ...standard branches/controls...
end
```

The `unitOutputNames` global in `xroot/boot/globals-setup.lua` already maps `"Out1"` through `"Out4"` to channel indices 0-3. If you need more than 4, that table needs extending in stolmine — file an issue.

## C++ DSP — the trig bug

**Critical for hardware:** package-side `sinf`/`cosf` miscompute on am335x at the package→firmware call boundary. Symptoms are silent and geometric — a sine wave looks malformed, a circle distends, indicators point to the wrong place.

If your multi-out DSP uses `sinf` or `cosf` (e.g. quadrature oscillators, phase-shifted LFOs, panners), use a precomputed LUT instead. Reference implementation in this repo:

- `mods/spreadsheet/FilterResponseGraphic.h` — `kLutCos` / `kLutSin` (72-entry, `a = 2*pi*i/N - pi/2`, step 0 = top), with `lutCos(rad)` / `lutSin(rad)` inline helpers using bias-then-cast-then-modulo for arbitrary radians.

Firmware-side code (anything in `app.bin`) is unaffected — the bug is specifically the package `.so` invoking libm trig at runtime. Other libm calls (`logf`, `expf`) are not currently flagged; only `sinf`/`cosf`.

The emulator on Linux is unaffected by this bug — it shows up only on actual hardware. So validate on hardware before declaring a multi-out unit done.

## CPU cost and opt-in compute

Multi-out units can fan out aggressively without pain *if* you follow one convention: **gate sub-out compute on `Outlet::isConnected()`**. Full cost model in stolmine `docs/planning/redesign/14-multi-output-cpu-cost.md`; the practitioner summary follows.

### The cost shape

- **Framework overhead per declared outlet is negligible** — ~0.0015% of one core per active outlet at 96k/64-frame. The outlet buffer is lazy-allocated from a pre-allocated pool, so unused outlets don't even take memory until someone writes into them.
- **DSP cost is what you pay.** It splits into three categories:
  - **Category A (free byproducts):** signals already held as internal state — inverted envelope, EOC trigger, play-position phase, gain-reduction signal, ladder pole taps, SVF multi-mode tap. A write-through of existing state. **<0.1% of parent CPU.**
  - **Category B (derived outputs):** signals requiring trivial extra math — pre-divided clock taps, ZCD direction, counter rollover, quantizer note-changed. **1–5% of parent CPU per sub-out.**
  - **Category C (new signal paths):** signals that duplicate a pipeline or restructure internal flow — early-reflections-only reverb, pre-feedback delay tap, per-band splits. **Highly variable; benchmark individually.**
- **There is no automatic "skip if no consumers" gate.** `Object::process()` runs unconditionally. If you write into an outlet buffer unconditionally, you pay that cost regardless of whether anyone is listening.

### The opt-in compute pattern

```cpp
void MyMultiOut::process() {
  float *primary = getOutput(0);
  float *sub1    = getOutput(1);
  float *sub2    = getOutput(2);

  // Primary is almost always consumed — don't gate it, the branch is waste.
  computePrimary(primary);

  // Gate Category B and C sub-outs. Cost-when-unused drops to ~1 ns/frame.
  if (mSub1Outlet.isConnected()) {
    computeSub1(sub1);
  }
  if (mSub2Outlet.isConnected()) {
    computeSub2(sub2);
  }
}
```

With this gate in place, unused sub-outs cost essentially nothing. Users who don't wire a sub-out pay ~1 ns/frame per unused sub-out, amortized across the whole patch.

### When to gate vs when to skip the gate

| Category | Gate? | Reason |
|---|---|---|
| Primary output (sub-out 1) | **No** | Almost always consumed; the branch is pure waste. |
| Category A on tight-loop parents (osc/filter) | **Yes** | Even 50 ns/frame adds up when the parent runs at sample rate. |
| Category A on coarse parents (envelope/clock) | **Optional** | Compute is cheap enough that the branch can cost as much as the work. |
| Category B | **Yes** | Real compute worth avoiding when unused. |
| Category C | **Yes, always** | The whole point is that this is expensive. |

### Heavy parents warrant more care

If the parent unit is already a significant fraction of core — convolution reverb, granular stretcher, convolution — even small per-sub-out percentages become measurable. For parents >10% of core, benchmark the retrofit on-device rather than trusting the rule of thumb.

### Avoid N-scaling sub-out counts

A unit whose sub-out count scales with state (e.g. "one gate per slice" for a 64-slice sample) multiplies the cost by N. Prefer the **(index CV + boundary trigger)** two-outlet design and reconstruct per-item gates downstream with addressed switches or comparators. The framework today assumes fixed sub-out count declared at Unit init.

## Testing checklist

For each multi-out unit you ship:

**Stolmine emu (build, install, launch):**
1. Unit appears in unit picker under its declared category.
2. Insert into a chain. Verify primary (sub-out 1) auto-connects.
3. From a downstream chain's local picker, navigate to your unit. Edge indicator overlays the scope ply showing label[1] + "1/N".
4. Press **M6** — indicator advances to label[2] + "2/N". Press repeatedly; verify cycling and wraparound.
5. Press enter on a non-primary sub-out — verify the consumer subscribes to that specific sub-out's outlet.
6. Save preset. Restart emu. Reload preset. Verify connection survives.

**Vanilla emu (build vanilla, install your `.pkg`):**
1. Package loads (no missing-symbol errors in log).
2. Insert into a chain. Primary auto-wires (vanilla shows the unit normally with no sub-out picker UI).
3. Local picker shows the unit; only sub-out 1 (and sub-out 2 if stereo chain) is reachable. No crash, no error.
4. Load a stolmine-saved preset that wired sub-out ≥3 — verify connection drops silently with the standard log warning. Rest of preset loads correctly.

**Hardware (am335x, real ER-301):**
1. Same as stolmine emu plus:
2. **Trig sanity check:** if your DSP uses any `sinf`/`cosf`, audit the C++ output against the LUT version. Expect waveform distortion until you switch to LUT.

## Reference implementation

The proving fixture for the framework is **QuadLFO** in stolmine: `er-301-stolmine/mods/multiout/`. Smallest possible useful multi-out unit (4 sine phases at 0/90/180/270). Read it as a template:

- `mods/multiout/QuadLFO.h` / `.cpp` — DSP with 4 outputs in `namespace multiout`, inheriting `od::Object`.
- `mods/multiout/multiout.cpp.swig` — minimal SWIG bindings.
- `mods/multiout/assets/QuadLFO.lua` — Lua wrapper with `subOutLabels`.
- `mods/multiout/assets/toc.lua` — package manifest.
- `scripts/multiout.mk` — one-line build script using `mod-builder.mk`.

Note: QuadLFO's reference uses scalar `sinf` because it's emu-first. For a hardware-shipping habitat unit, swap to LUT per the trig bug section above.

## Naming

Keep `args.subOutLabels` strings short (≤6 chars renders cleanly in the 42px indicator ply at 10pt). Prefer descriptive over numeric — e.g. `{"main", "aux", "cv", "gate"}` over `{"1", "2", "3", "4"}`. Don't include the unit name (the indicator already implies it).

If you need degree symbols or non-ASCII in labels, test on hardware first — the firmware font may or may not have the glyph depending on which font is shipped.

## Out of scope

- **Stereo-paired sub-outs** (e.g. main L+R as one logical sub-out, aux L+R as another). Not supported in v1; treat each sub-out as a single mono channel.
- **Per-sub-out controls.** Multi-out unit controls live only at the top level (macro-style, affecting all sub-outs). No per-sub-out control depth.
- **Sub-views.** No drill-down navigation for selection (the M6 cycle replaces it). The unit's own focused view may surface sub-out topology in future, but it's not committed.
- **Two-digit sub-out counts.** The `X/Y` indicator ply is 42px wide and assumes single-digit X and Y; a sub-out count of 10+ would visually clutter the indicator (and arguably the unit itself should be a sub-chain at that point). Practically there's no hard cap, but if your design calls for ≥10 sub-outs, reconsider.
