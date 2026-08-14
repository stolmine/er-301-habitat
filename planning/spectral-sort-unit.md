# Spectral sort unit ("pixel sorting" for audio) - design

Status: **design** (2026-08-11). User idea + two architecture calls the same day.

Architecture: **sample-file player with spectral sorting, both axes.** File
handling follows the built-in sample players, with
`er-301/mods/core/assets/Player/VariSpeed.lua` as the explicit reference (user
call). NOT a record-into-buffer unit - the earlier draft of this doc assumed
that and its memory model was wrong; see §4.

Related open ledger items - this is arguably the synthesis of both rather than a
third thing: `units-spectral-processing` (Spectral Mask/Gate, already scoped to
reuse pffft), `units-buffer-shuffler` (BBCut-style slicing).

## 1. What pixel sorting actually is, and why the spectrogram is the right target

Pixel sorting is three things: a **mask** (a predicate that defines contiguous
spans of eligible pixels), a **key** (what you sort by), and a **direction**. It
reads as melting rather than noise because spans are bounded by pixels that FAIL
the mask, so unsorted regions anchor the image. It is a **permutation**: nothing
is created or destroyed and the histogram is preserved exactly.

The STFT magnitude matrix is a 2D array of brightness values. **The spectrogram
IS the image**, so every pixel-sorting control maps 1:1 with no analogy-stretching.

### The trap: do NOT sort raw samples

Sorting time-domain samples by amplitude within a span yields a monotonic ramp,
which is a sawtooth at the span rate, for ANY input. Audio's perceptual content
is in the ordering and differences between samples, not the value histogram. It
is a waveshaper, not a rearrangement, and it sounds the same forever. Named here
because it is the obvious first implementation and it is a dead end.

## 2. The three axes

| axis | musical result | needs |
|---|---|---|
| **Frequency** (sort bins within a frame) | Energy piles toward one end. Whooshes, spectral melt, formant collapse | per-frame permutation, computable on the fly |
| **Time** (sort each bin's magnitude trajectory across frames) | Each band's envelope reorders independently. Attacks dissolve per-band, transients smear into swells | a resident STFT matrix (random frame access) |
| **Grain** (sort time-domain grains by feature) | Intra-grain waveform survives so it still sounds like the source; macro time rearranges | no FFT; kept as a possible second unit, not this one |

Buffered gives us frequency and time. Grain-domain is a genuinely different unit
and should not be bolted on.

## 3. The core architecture decision: permutation, not re-render

The naive buffered design re-renders the whole buffer on every macro change -
hundreds of ms for a few seconds of audio, which fits on neither the audio
thread nor the UI thread (and the anamnesis rolling-slice saga is the standing
lesson about what that costs).

**A sort is a permutation. Permutations are small and cheap.** So:

1. **Analysis into a rolling window** that tracks the playhead (§4), giving a
   fixed-size magnitude matrix regardless of file length (plus a phase strategy,
   see §5).
2. **Macro change recomputes INDEX ARRAYS only.** Counting sort over indices. No
   FFT, no audio touched. Cheap enough to run per block, which is what makes the
   macro CV-modulatable.
3. **Resynthesis happens at playback**, one inverse FFT per hop, each bin read
   from wherever the permutation points.

**Consequence: the macro is a real-time, CV-modulatable control rather than a
render button.** That is the difference between an instrument and a novelty, and
it is the reason to build it this way.

### The playback gather, sized (ESTIMATED, needs step-0 confirmation)

Time-axis sorting means each of ~513 bins reads from a DIFFERENT frame, scattered
across megabytes - the same gather shape that produced the NEON no-go and the
"hybrid float is low-value" outcome on the AW reverbs. Sized before committing:
~513 scattered reads = ~513 cache lines per 256-sample hop = ~2 misses per
sample, order 100-200 cycles/sample against a ~20800 cycle budget, so **~1%**.
Affordable, and unlike the reverbs the scatter amortizes over a whole hop instead
of being paid per sample. Confirm on hardware before relying on it.

## 4. Memory: a ROLLING WINDOW, not the whole file

This is the section the VariSpeed decision rewrote, and it is the most important
constraint in the design.

Frame 1024, hop 256, 48k: 187.5 frames/sec, 513 bins.
- magnitudes float: 2052 B/frame -> **~385 KB per second of audio**
- plus phases float: **~770 KB per second**

**"Analyze the whole sample" is therefore dead.** A card-attached file can be any
length; a 3-minute sample would need ~69 MB of magnitude matrix alone. Users
attach whatever they like, so the design cannot scale with file length.

**The fix, which is a better design anyway: analyze a bounded ROLLING WINDOW
around the playhead, and make that window a user control.**

- The analysis matrix is `windowFrames x bins`, fixed size, allocated once. A 2 s
  window is 375 frames -> ~770 KB magnitude-only, ~1.5 MB with phase. Bounded
  regardless of file length.
- Analysis runs continuously as playback advances: one forward FFT per hop to
  push a new frame in, one inverse per hop to synthesize out. Two FFTs per hop
  total, which is the same steady-state cost whatever the sample length.
- **The window IS the sort span**, which is exactly pixel sorting's semantics
  rather than a compromise. Short window (~100 ms) gives local shimmer; long
  window (seconds) reorders whole phrases. It becomes one of the best controls
  on the unit.
- Time-axis sorting operates within the window. That is a real limit and should
  be stated in the docs: you cannot sort across a span longer than the window.

**Slices are the second span source, and they come free.** The built-in sample
editor already slices, VariSpeed already exposes slice-select and slice-shift
with CV addressing, and a slice IS a span. Sorting bounded by slice markers is
the most musically legible version of this whole idea, and users can author the
spans in a tool they already know. Worth supporting as a span mode alongside
threshold spans.

### RESOLVED 2026-08-11: variable window, always analyzed at max

- **Variable costs nothing extra in memory.** You cannot realloc on the audio
  thread, so a runtime-variable window allocates the maximum up front and uses a
  prefix. Fixed only wins if you fix it small; the number you pay is the max
  either way.
- **The cost of variable is discontinuity, and it has a clean fix.** Growing the
  window mid-playback pulls in frames that were never analyzed, so you get a gap
  or a burst of catch-up FFTs plus a jumping permutation. Shrinking is free.
  **Therefore: always analyze at the MAXIMUM, and let the Window control select
  how many of those frames the permutation spans.** Analysis cost becomes
  constant, growing is instant because the data is already there, and Window
  becomes a smooth CV-modulatable control instead of a glitch generator.
- **What variable buys:** the effect's time-scale, which is its most expressive
  axis. ~50-100 ms is grainy spectral shimmer, ~500 ms is phrase smear, seconds
  are section dissolve. Material-dependent, so fixing it means picking one sound.
- **Free win from being file-based:** no latency penalty for lookahead. A live
  version pays half the window to see the future; here the future is on the card.
- **The cost that should set the max: refill on trigger.** Every trigger or
  slice-jump refills the window. A 2 s window at hop 256 is ~375 FFTs, order
  5-8 ms in one burst, and a slice-driven unit retriggers constantly.
  **MVP decision: use a TRAILING window that fills naturally** - no seek-ahead,
  no burst, and the sort span simply grows in over the first window after a
  trigger. Frequency-axis sorting does not care at all (it is per-frame). Revisit
  centred/lookahead windows only if the grow-in is audibly wrong.

### RESOLVED 2026-08-11: store the complex spectrum (re/im), sort on magnitude-squared

**This is not a memory decision at these sizes.** A 4 s max window is 1.5 MB
magnitude-only against 3.1 MB complex, on a 64 MB system whose sample pool
routinely holds more. It is also free CPU-wise, since the FFT hands you phase
whether you keep it or not. Decide on sound.

**And the sound answer is axis-dependent:**
- **Time axis wants magnitude-only behavior.** Move a bin's magnitude from frame
  j to frame i but keep frame i's phase and you get a coherent carrier with a
  permuted envelope - exactly "this band's loudness contour got rearranged",
  smooth, no warble. Storing phase buys nothing here.
- **Frequency axis wants phase.** Move magnitude to a new bin while leaving that
  bin's phase and the relocated energy lands on arbitrary phase: the smeared
  underwater character. Fine as one sound, but it is the ONLY sound available
  without phase.
- Caveat: stored phase is necessary but NOT sufficient for clean tonal
  relocation. A partial moved from bin 40 to bin 12 carries bin 40's phase
  ADVANCE RATE, which is wrong for its new frequency and warbles across frames
  unless phase is also propagated vocoder-style. That propagation is v2.

**Implementation note that matters more than the storage question: store re/im,
never magnitude/phase.** Converting costs a `sqrt` and an `atan2` per bin per
frame, and 513 bins x ~188 frames/sec is ~96k `atan2`/sec, which this platform
will not survive. Keep the complex pair as pffft produces it and **sort on
magnitude-squared** - `sqrt` is monotonic, so `re*re+im*im` gives the identical
ordering with no transcendentals anywhere in the analysis path.

**Settled configuration: variable window, max 2 s, always analyzed at max,
trailing; complex re/im storage; sort key magnitude-squared.**

## 5. Phase strategy - the fork that decides the sound

For frequency-axis sorting, what travels with the magnitude:
1. **Move the whole complex bin.** Partials relocate intact -> inharmonic,
   bell-like, metallic. Most "musical" in a strange way.
2. **Sort magnitudes, leave phase in place.** Smeared, phasey, underwater.
   Cheapest, and needs no stored phase (derive from the frame position).
3. **Re-derive phase**, phase-vocoder style. Cleanest, most expensive.

Recommend shipping 1 and 2 as an option, deferring 3.

## 6. am335x feasibility

- **Do not use a comparison sort.** Branchy, data-dependent, and
  `feedback_runtime_branched_dsp_dispatch` records that runtime-branched DSP has
  crashed this platform. Use **counting sort on quantized magnitudes** (64-128
  levels): O(n+k), comparison-free, nearly branch-free. Quantization is not a
  compromise here - it is what makes the sort cheap AND it coarsens the key in a
  way that groups similar bins, which is musically fine.
- Secondary option: exploit temporal coherence (frame N+1's order is nearly frame
  N's, so insertion sort on nearly-sorted data is ~O(n) amortized).
- **pffft is already proven on device** (Clouds, Spectrogram). One inverse FFT
  per hop is the resynthesis cost; ESTIMATED low single-digit percent, unverified.
- **Energy is conserved for free.** A permutation preserves the per-frame
  histogram, so no makeup gain and no blowups, unlike most spectral effects.

## 6b. File handling and unit surface (VariSpeed pattern)

Reference: `er-301/mods/core/assets/Player/VariSpeed.lua`. Copy the pattern
rather than inventing one - it is what users already know and it is what
serialization expects.

**Lua side, lifted more or less verbatim:**
- `setSample(sample)` with `sample:claim(self)` / `sample:release(self)`
  refcounting, forwarding `sample.pSample` (and `sample.slices.pSlices`) to the
  C++ head.
- `doAttachSampleFromCard()` via `Pool.chooseFileFromCard(self.loadInfo.id, ...)`
  and `doAttachSampleFromPool()` via `SamplePoolInterface(self.loadInfo.id, "choose")`.
- `doDetachSample()`, and `showSampleEditor()` opening a `SlicingView` bound to
  the head so slices can be authored in the built-in editor.
- `serialize`/`deserialize` using `SamplePool.serializeSample` /
  `deserializeSample`, plus our own sort state (axis, threshold, window,
  sortedness) and the playhead position.
- `onRemove` calls `setSample(nil)`.
- Menu shape: `sampleHeader / selectFromCard / selectFromPool / detachBuffer /
  sliceBuffer` then our options. The menu's `sub` block shows the attached
  filename, duration, channel count and memory size.
- `views` table with a `wave` entry per control, so the waveform shows in
  context views rather than at top level (the FeedbackLooper/VariSpeed pattern
  CONTEXT.md calls out).

**C++ side:** a head-style `od::Object` taking `setSample(od::Sample*)`, modelled
on `er-301/mods/core/objects/heads/VariSpeedHead.{h,cpp}`. Inheriting
`od::TapeHead` gets `mpSample` / `mCurrentIndex` / `mEndIndex` and
`TapeHeadDisplay` compatibility for the waveform, per CONTEXT.md.

**Transport scope for v1.** The user's reference is about FILE HANDLING, not the
varispeed transport. Do NOT try to be VariSpeed-plus-sorting in v1: variable
speed through an STFT resynthesis is phase-vocoder time-stretch, a whole
separate feature with its own quality problems. v1 is trigger, slice select and
1x playback. Speed/stretch is a v2 conversation.

## 7. Controls

Canonical pixel-sorting controls, mapped:

| control | pixel sorting | here |
|---|---|---|
| Sort (macro) | - | the headline. Opens the threshold window AND raises sortedness together |
| Threshold | mask lo/hi | which magnitude range is eligible; loud partials fall outside it and become anchors |
| Window | - | length of the rolling analysis window = the maximum sort span (see §4). Short = local shimmer, long = phrase reordering |
| Axis | rows/columns | frequency or time |
| Span mode | - | threshold spans, or slice-bounded spans authored in the built-in sample editor |
| Key | brightness/hue/sat | magnitude (default), spectral flux, bin index |
| Direction | asc/desc | asc / desc / toward-centre |

**Sortedness is the control that makes it musical.** Binary sorted-versus-not is
a gimmick. Implement as either k partial passes or a crossfade between original
and permuted arrays, giving a continuous bypass -> fully-sorted morph. This is
the macro.

Why the threshold is the good control musically: on the frequency axis, spans
break at loud bins, so **tonal peaks stay put and the noise floor between them
sorts** - the harmonic skeleton anchors while everything else melts. On the time
axis, sustained content anchors and transient/quiet regions reorder.

## 8. Risks

1. **Kryos.** The one existing spectral-resynthesis unit in the catalog is
   `blocked`: it hangs hardware on load and was never isolated to emu vs
   hardware. That is an unexplained spectral-resynthesis risk sitting directly
   upstream of this unit. Either resolve `kryos-load-hang` first or go in with
   eyes open and hardware-test early.
2. Multi-MB allocation at record time; confirm the pool behavior and failure mode.
3. Playback gather (§3) is estimated, not measured.
4. Viz temptation: a spectrogram ply with sorted spans highlighted would be
   gorgeous and is very on-brand, but `feedback_viz_encoder_capture_architectural`
   and the whole anamnesis arc say the draw path is where units die. Ship audio
   first, viz second, and use the strip-raster/rolling-slice mechanism if it
   comes.

## 9. Build order

0. **Step 0, offline first.** Prototype the whole thing in Python against a WAV -
   which is now doubly right, since the unit's input is a WAV file anyway:
   STFT, threshold-span-mask, counting sort on both axes, all three phase
   strategies, resynthesis. This is a SOUND-DESIGN question before it is a DSP
   engineering question, and the offline loop is minutes instead of a build
   cycle. Decide the default axis, key and phase strategy from listening.
1. **File handling FIRST, before any DSP.** Stand up the VariSpeed shell: attach
   from card/pool, detach, slicing view, waveform, serialize/deserialize, with
   the head doing nothing but plain 1x playback. Get insert/delete/quicksave
   clean on hardware. This de-risks the boring half and gives a working unit to
   hang DSP on, and it is where sample-refcount and serialization bugs live.
2. C++ core: rolling-window analysis + permutation + playback resynthesis,
   frequency axis only, phase strategy 2 (no stored phase).
3. Time axis (adds the stored-phase decision and the gather).
4. Sortedness morph + macro; slice-bounded spans.
5. Hardware CPU + the Kryos risk check.
6. Viz, only if 1-5 are solid.

## 10. MVP BUILT + REVIEWED (2026-08-12) - spreadsheet 2.8.5.2, unit name "Sediment"

**Scope shipped: frequency axis only.** Sorting bins WITHIN a frame needs only
the current frame, so the MVP has no analysis matrix at all - the rolling window
of §4 turned out to be purely a time-axis feature. That made the MVP far smaller
than this doc implies. Files: `mods/spreadsheet/Sediment.{h,cpp}`,
`assets/Sediment.lua`, emu test `er-301/tests/emu/92-sediment-insert-smoke.test`.

Note the shipped phase behavior is strategy 1 (the whole complex bin moves), not
the strategy 2 the build order named - it is cheaper AND better here, since
moving the pair costs nothing when you are permuting slots anyway.

### Verified before hardware (MEASURED, offline)
- pffft real packing confirmed empirically: `out[0]`=DC, `out[1]`=Nyquist, then
  interleaved re/im. Round trip clean at 1.4e-7.
- **`pffft_transform_ordered` with `work=NULL` uses the STACK** - 4 KB at
  N=1024, against a 2048-byte audio task stack. The work buffer is heap.
- Hann-squared at 4x overlap sums to exactly 1.500000; an offline replica of the
  STFT/OLA path reconstructs at **-140 dBr**, confirming the 1/(N*1.5) constant.
- Counting sort orders **170 dB of range in 96 buckets**, worst inversion
  0.900 dB against the designed 0.753 dB resolution. Ties keep original order,
  which is a deliberately gentle partial sort.
- Both arches clean, three lints clean, `Sediment.o` NEON-clean, `process` and
  `doHop` frames 20 bytes each, emu insert/delete smoke passes.

### Fixes applied from the review
1. **Loop wrap gap.** Reads past EOF were zero-padded and the loop only reset at
   the boundary, so every wrap was a measured ~13 ms fade to -39 dB. The read
   now wraps INSIDE the window gather, making the loop a plain splice.
2. **Control law decoupled.** `peak*macro^2` put 86.7% of bins in one span at
   macro=0.01, so the multi-anchor regime that is the entire point only existed
   below macro~0.05 where the crossfade rendered it inaudible. Threshold is now
   dB-linear (`peak * 10^(-6*(1-macro))`) and the crossfade saturates early
   (`min(1, 3*macro)`). Measured spans across the knob went from 9/6/4/3/2 to
   12/12/12/10/9. **The -60 dB range constant is the listening-pass knob** and
   was deliberately NOT tuned further against a synthetic signal.
3. **Attack pre-roll.** Every trigger faded the attack in over ~13 ms. Playback
   now starts kFFT-kHop samples BEFORE the file so the overlap-add is populated
   when source sample 0 is emitted. Costs ~16 ms of trigger latency, no CPU.
4. **Sample-rate ratio.** There was no compensation, so every 44.1k file played
   8.84% sharp (+1.5 semitones). Reads are now linearly interpolated at
   `fileRate/engineRate`, matching what the built-in heads do.
5. **Option serialization.** Direction and Loop were lost on quicksave;
   `enableSerialization()` added.
6. **setSample override** stops playback, so attaching mid-play no longer
   continues from a stale offset with the old file's OLA tail bleeding through.
7. **Bucket clear/prefix bounded to each span's occupied range.** Clearing all
   1024 buckets per span costs ~680 KB of memset per hop on a noisy frame with
   ~170 spans - worst case exactly when headroom is thinnest.
8. `mReady` guard so a failed allocation cannot be dereferenced on the audio thread.

### REJECTED review finding, recorded so it is not re-raised
The review's top item was "move everything into the header, delete Sediment.cpp,
because out-of-line virtuals on an od::Object subclass crash on hardware." **Not
done, and the memory that prompted it has been corrected.** Evidence: four
shipping hardware-proven units (VarishapeVoice, ConstantRandom, Vitrail, Canals)
define `process()` out of line, and `nm` shows `VarishapeVoice.o`, `Vitrail.o`
and `Sediment.o` emit their vtables with IDENTICAL linkage. Worse, header-only
would drop the DSP into the SWIG TU at `-Os` with no `-ffast-math` - the exact
trap that made anamnesis ship its whole DSP slow. The scar is real for
od::Graphic subclasses only.

### Known, accepted, for the listening test
- Crossfade dips ~1 dB at mid-macro (linear crossfade of decorrelated complex
  values, cf. `feedback_equal_power_drywet_crossfade`). Equal-power would fix it
  but the endpoints must stay exact.
- Per-hop independent permutations mean bins jump at 187.5 Hz, so expect
  boiling/warble at mid macro. The temporal-coherence idea in §6 doubles as the
  stabilizer for this and is the natural next move.
- Bin 0 is never permuted (it packs DC and Nyquist; swapping them is meaningless).
- ESTIMATED CPU 2-4% steady on am335x, arriving as a burst inside one frame.
  Unverified - hardware measurement owed.

### Next
Hardware test, then the listening pass on the threshold constant, then the time
axis (which is where the rolling window, the resident matrix and the gather of
§3 and §4 finally come in).
