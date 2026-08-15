# Design note: Mid Side - mid/side splitter with per-side branches

Status: design note / not started. Ledger item `diptych-mid-side`.

User request 2026-08-14. Stereo-only unit. Two branch meter controls, one for
Mid and one for Side, so each side of the matrix has its own insertable chain.

Fills the stereo/imaging gap.

Name: **Mid Side**. Units outside spreadsheet take descriptive, factual names (user direction 2026-08-14), matching how biome already names things (Fade Mixer, Tilt EQ, Gated Slew, Spectral Follower). The poetic working title *Diptych* is retired; evocative names stay in spreadsheet.

*Corrected 2026-08-14*: this note originally said stereo/imaging was "a total
blank in the collection, and the firmware has nothing either." Wrong on both
counts - see `nacre-quadrature-width.md`. Sujet's Space is a full spectral
decorrelator, `er-301/mods/core/objects/Spread.{h,cpp}` is a firmware widener
object that is simply never exposed as a unit, and Filament, Fabula, Galactic,
Network and Anamnesis all carry width mechanisms. What is missing is a
*reachable* M/S tool, which is still what Mid Side is for.

Package: **biome**, with the other utility and mixing units. Fade Mixer is the
closest precedent - it is the existing branch-meter unit in the collection.

## The unit

M/S encode on the way in, decode on the way out, with a branch in each leg:

```
L ─┬──> (L+R)*0.5 = M ──> [mid branch] ──> Mg ─┬──> M+S = L
   │                                            │
R ─┴──> (L-R)*0.5 = S ──> [side branch] ─> Sg ─┴──> M-S = R
```

The two BranchMeter faders are the M and S gains, which means **width comes for
free**: Side fader down is narrower, up is wider, all the way down is mono. No
separate Width control needed, and the fader is already the right gesture.

Controls: `mid` (BranchMeter), `side` (BranchMeter). That is the whole surface.

## Implementation: probably no C++ at all

M/S is four adds and two gains. `app.Sum` and `app.ConstantGain(-1)` for the
negation give the entire matrix in Lua, wired the way the FDN design note
describes Erbe's butterflies being built from `app.Sum`. No DSP object, no SWIG
binding, no NEON, no am335x port risk. If the native-object overhead measures
badly on hardware, a trivial C++ atom is the fallback, but start without one.

**Null test is the acceptance gate**: branches empty, both faders at unity, and
the output must be bit-identical to the input. Encode/decode with 0.5 on the
forward pass and unity on the return reconstructs exactly; if it doesn't, the
wiring is wrong.

## Stereo-only

Two mechanisms, both needed:

1. `channelCount = 2` in `mods/biome/assets/toc.lua`, the way `Stereo Mix` does
   it in `er-301/xroot/builtins/toc.lua`.
2. A runtime guard in `onLoadGraph`, following `SpreadDelayUnit`
   (`er-301/mods/core/assets/Delay/SpreadDelayUnit.lua:21`):

```lua
function Mid Side:onLoadGraph(channelCount)
  if channelCount ~= 2 then
    app.logError("%s: can only load into a stereo chain.")
  end
```

The user's point stands - a mono chain cannot host this, and the unit should say
so rather than half-load.

## The one real unknown: how signal gets INTO a branch

This is worth stating plainly before anyone starts building, because the obvious
mental model is wrong.

**An ER-301 Branch is a source sub-chain, not an insert.** Read
`er-301/xroot/Unit/init.lua:226` and `er-301/xroot/Chain/Branch.lua`:

```lua
function Unit:addMonoBranch(name, inObject, inletName, outObject, outletName)
  local branch = Branch {
    leftDestination = inObject:getInput(inletName),   -- where branch OUTPUT goes
    leftOutObject = outObject, leftOutletName = outletName,  -- monitoring only
```

`leftDestination` is where the branch's output lands inside the parent. The
`outObject`/`outletName` pair is used only by `Branch:getOutput` for the meter's
scope. **There is no parameter that feeds the branch's input.** A branch's input
is a `Chain` input, set through `Chain:setInputSource(i, src)`, and the only
caller in the tree is `Chain/InputControl.lua:74` - the user picking a source
from the picker. In FadeMixer's `addMonoBranch("ch1", gain1, "In", gain1, "Out")`
the branch is a *source* for a mixer channel; nothing flows out to it.

So "M goes out to the branch, gets processed, comes back" needs a mechanism.
Three candidates, in preference order:

1. **Pre-wire the branch input to the unit's own sub-output.** Expose Mid and
   Side as unit outputs via the multi-out framework
   (`docs/multi-output-units-author-guide.md`, `args.subOutLabels`), then at
   graph-load call `branch:setInputSource(1, Source.Internal("local", self, 3))`.
   `Source.Internal` (`er-301/xroot/Source/Internal.lua`) wraps any object with
   `getOutput`/`getInstanceKey`/`getOutputDisplayName`, and a Unit has all three.
   If this works, the round trip is seamless and the user sees an ordinary
   branch. **Untested - this is the spike.**

   **Caution added 2026-08-14 from `sill-window-comparator` research**: the
   multi-out author guide explicitly warns against `chain:setInputSource(j, ...)`
   for sub-chain wiring - "inlet-buffer aliasing on the audio thread" and
   "engine-visible source structure becomes a lie". Its objection is aimed at
   *dynamic* rebinding on every patch change, which is not what this candidate
   does (one binding, at graph load, to a real source that genuinely exists). But
   the spike must confirm the binding survives serialization and does not race
   the audio thread on insert, and if it looks at all fragile, fall to candidate
   2 rather than arguing with the guide.
2. **Same exposure, manual wiring.** Same sub-outs, but the user picks "mid" as
   the mid branch's input themselves. Works with facilities that certainly
   exist; costs one picker action per branch and needs saying in the docs.
3. **Direct `pChain:setInput`.** `Chain.init` already does
   `self.pChain:setInput(0, outlet)` with a raw outlet, so the C++ side accepts
   one. Bypasses the Source abstraction entirely, which means serialization
   almost certainly won't round-trip. Last resort.

**Vanilla-firmware caveat for 1 and 2**: the unit's Out1/Out2 are L/R, so Mid and
Side are sub-outs 3 and 4, and the author guide is explicit that vanilla's
`getOutputSource(i)` is hardcoded to stereo - sub-outs 3+ are invisible there.
That makes the per-branch processing a **stolmine-fork feature**. On vanilla the
unit still loads and still passes audio; the branches just have nothing to
select. Decide whether that is acceptable or whether biome should stay
vanilla-clean before building.

## Phases

1. **Spike the branch feed.** Candidate 1 above, in isolation, before any unit
   work. Everything else is trivial; this is the only thing that can fail. If all
   three candidates fail, the unit degrades to a plain M/S width utility with two
   faders and no branches - still worth having, but say so and re-ledger.
2. **Matrix + stereo guard.** Native objects, toc `channelCount = 2`, runtime
   guard, null test at unity.
3. **Branch meters.** Two BranchMeters bound to the M and S gain parameters,
   pattern from `mods/stolmine/assets/FadeMixer.lua:80`.
4. **Hardware.** Insert/delete, serialization round-trip of branch contents and
   fader positions, CPU (should be negligible), and a listening check that the
   Side fader reads as width.
