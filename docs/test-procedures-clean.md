# Habitat Unit Test Procedures

Manual QA checklist for each custom unit in the habitat packages. Runnable pre-release against a fresh emulator (and optionally hardware) to catch regressions.

## How to use

For each unit section:

1. **Fresh chain** -- start from a new patch, no saved state bleed.
2. Walk the checkboxes top to bottom. Mark each pass / fail.
3. For *sub-display / expansion* items, use **shift** on the focus button to toggle into param mode (most controls) and the three sub-buttons (`SubButton 1/2/3`) to access readouts.
4. For *CV input behavior*, patch an LFO onto every continuous parameter branch in turn and confirm smooth modulation (no zipper, no clicks). For gate/trigger inputs use a 5 V pulse source.
5. For *save / load round-trip*, quicksave after setting every parameter to a non-default value, then delete the chain and reload the quicksave. Confirm every piece of state returned.
6. For *edge cases*, sweep the listed parameter combinations and listen for instability, runaway gain, or visual breakage.

Bugs found go into `todo.md` under the owning unit's section, not here.

## Conventions

- **Stereo mode descriptors**
  - `mono`: single op, no L/R split.
  - `native-stereo`: single op that internally processes both channels.
  - `dual-instance`: two C++ op instances (`op`, `opR`), one per channel, with parameters tied to both.
- **Mnemonic**: the 2-char mnemonic shown in the chain view.
- **Category**: where the unit lives in the unit browser.
- If a section doesn't apply to a unit (e.g. no menu), it's noted with a one-liner rather than dropped entirely.

---

# spreadsheet package

### Excel (spreadsheet)

**Module**: `TrackerSeq` &middot; **Mnemonic**: `Ex` &middot; **Category**: `Sequencer` &middot; **Stereo**: `native-stereo`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default state: 16-step sequence, each step offset 0, length 1, deviation 0; clock triggers advance; reset resets to step 0.
- [ ] Hot-plug behavior: sequence resumes from current step without discontinuity.

#### 2. Top-level row (expanded view)
Order: `steps`, `info`, `clock`, `reset`, `slew`, `xform`.
- [ ] **steps** (StepListControl) -- 64-step grid display with current-step editing; rotate: step highlight moves, paging when at boundaries.
- [ ] **info** (SeqInfoControl) -- visual sequence info readout; rotate: cycle focus between seqLength, loopLength, xformScope sub-controls.
- [ ] **clock** (Gate) -- clock input jack + comparison threshold; rotate: adjust threshold level.
- [ ] **reset** (Gate) -- reset input jack; rotate: adjust threshold.
- [ ] **slew** (GainBias) -- CV-controlled slew time, range [0, 10] s; rotate: adjust slew time.
- [ ] **xform** (TransformGateControl) -- transform/snapshot gate + function selector + factor dial; rotate: cycle through 9 functions (add, sub, mul, div, mod, reverse, rotate, invert, random) or adjust factor.

#### 3. Sub-displays
- [ ] **steps** sub-display shows three readouts: `offset` (EditOffset param, mapped ±5V or ±1V), `length` (EditLength, 1-16 ticks), `dev` (EditDeviation, 0-1). Each SubButton "offset", "length", "dev" focuses that readout for numeric entry.
- [ ] **info** sub-display shows `length` (seqLength 1-64), `loop` (loopLength 0-64, 0=disabled), `scope` (xformScope: ofst/len/dev/all). Each SubButton "length", "loop", "scope" focuses that readout.
- [ ] **xform** sub-display shows gate threshold readout, mini waveform scope, function label, and factor readout; SubButtons "input", "thresh", "fire" toggle sub-display mode between gate view and math fire.

#### 4. Expansion views
- [ ] **info** focus -> `seqLen`, `loopLen`, `xformScope` readout controls.
- [ ] **xform** focus -> function label, factor dial, mode toggle.

#### 5. Menu (onShowMenu)
- [ ] **Set All Step Lengths** -- tasks "1 tick", "2 ticks", "4 ticks".
- [ ] **Offset Range** -- "10Vpp (-5 to +5)" and "2Vpp (-1 to +1)"; re-maps readout.
- [ ] **Offsets** -- "Randomize all offsets", "Clear all offsets".
- [ ] **Snapshot** -- "Save snapshot" / "Restore snapshot" (all 64 triplets).

#### 6. CV input behavior
- [ ] clock rising edge advances by 1 step.
- [ ] reset rising edge returns to step 0.
- [ ] slew 0-10 s smooth on continuous CV.
- [ ] seqLength / loopLength / xformScope CV modulates over ranges.

#### 7. Stereo routing
- [ ] Mono chain: single CV output.
- [ ] Stereo chain: output duplicated to L/R (native-stereo).
- [ ] Transform and slew affect both channels identically.

#### 8. Save / load round-trip
- [ ] Quicksave + reload: all 64 (offset, length, deviation) tuples, offsetRange10v flag, xformFunc/Factor/Scope restored.
- [ ] Two back-to-back cycles identical.
- [ ] `loadStep(0)` called on deserialize so edit buffer matches step 0.

#### 9. Edge cases
- [ ] Seq length 1 + no loop: clock triggers same step repeatedly.
- [ ] Loop length < seq length: playback wraps at loop boundary, then continues.
- [ ] Divide-by-zero / mod-0 in transform factor handled gracefully.
- [ ] Offset range change mid-sequence: readout remaps without touching stored values.
- [ ] Max step length + max slew: clock period must exceed slew time or output queues.

---

### Ballot (spreadsheet)

**Module**: `GateSeq` &middot; **Mnemonic**: `Bl` &middot; **Category**: `Gate Sequencer` &middot; **Stereo**: `native-stereo`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: 16-step sequence, gates OFF, lengths 1 tick, velocity 1.0.
- [ ] Hot-plug: gate state persists, resumes at current step.

#### 2. Top-level row (expanded view)
Order: `steps`, `info`, `clock`, `reset`, `ratchet`, `xform`.
- [ ] **steps** (ChaselightControl) -- chaselight grid of lit/unlit dots.
- [ ] **info** (GateSeqInfoControl) -- rotate cycles focus through seqLength, loopLength, gateWidth.
- [ ] **clock** (Gate) -- clock input + threshold.
- [ ] **reset** (Gate) -- reset input + threshold.
- [ ] **ratchet** (RatchetControl) -- ratchet gate + multiplier + RatchetLen/RatchetVel toggle options.
- [ ] **xform** (TransformGateControl) -- function selector + paramA/paramB (euclidean, NR, grids, necklace, invert, rotate, density).

#### 3. Sub-displays
- [ ] **steps** sub-display: gate ON/OFF toggle, length readout (1-16 ticks), velocity (0-1). SubButtons "on/off", "length", "vel".
- [ ] **info** sub-display: seqLength, loopLength, gateWidth readouts. SubButtons "length", "loop", "width".
- [ ] **ratchet** sub-display: multiplier readout plus RatchetLen and RatchetVel BinaryIndicator toggles. Shift+SubButton toggles each option.
- [ ] **xform** sub-display: paramA, paramB, function label, gate scope. SubButtons "input", "paramA", "paramB".

#### 4. Expansion views
- [ ] **info** focus -> seqLen, loopLen, gateWidthFader.

#### 5. Menu (onShowMenu)
- [ ] **Set All Gate Lengths** -- "1 tick", "2 ticks", "4 ticks".
- [ ] **Set All Velocities** -- "25%", "50%", "100%".
- [ ] **Randomize** -- gates / lengths / velocities.
- [ ] **Clear / Reset** -- gates / lengths / velocities to defaults.
- [ ] **Snapshot** -- Save / Restore.

#### 6. CV input behavior
- [ ] clock rising edge advances step.
- [ ] reset rising edge returns to step 0.
- [ ] ratchet gate splits current step into N sub-gates.
- [ ] gateWidth CV modulates duty cycle.
- [ ] xformScope CV selects gate/length/velocity/all.

#### 7. Stereo routing
- [ ] Mono chain: single gate output.
- [ ] Stereo chain: duplicated to L/R.
- [ ] RatchetLen / RatchetVel options affect both channels.

#### 8. Save / load round-trip
- [ ] Quicksave + reload: all 64 (gate, length, velocity) tuples, ratchetLen/Vel options, xformFunc/paramA/paramB/scope restored.
- [ ] Two back-to-back cycles identical.
- [ ] **Known-fragile**: RatchetLen and RatchetVel options have regressed in the past -- confirm both survive a full cycle.

#### 9. Edge cases
- [ ] All gates OFF: ratchet on a zero step produces no output.
- [ ] Velocity 0 + ratchet 8: sub-gates are zero amplitude.
- [ ] Loop length 1: only step 0 plays.
- [ ] Gate width 0: no pulse -- verify graceful handling.
- [ ] Euclidean with 0 hits: no gates fire.
- [ ] RatchetLen toggle + gate length 1: sub-gates compressed into 1-tick window.

---

### Etcher (spreadsheet)

**Module**: `Etcher` &middot; **Mnemonic**: `Et` &middot; **Category**: `Transfer Function` &middot; **Stereo**: `native-stereo`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: 32-segment linear ramp, input 0, level 1, skew 0.
- [ ] Hot-plug: transfer function stable; input CV mapped immediately.

#### 2. Top-level row (expanded view)
Order: `input`, `segments`, `curve`, `skew`, `xform`, `level`.
- [ ] **input** (GainBias) -- input CV + gain/bias.
- [ ] **segments** (SegmentListControl) -- per-segment offset/curve/weight spreadsheet.
- [ ] **curve** (TransferCurveControl) -- live transfer curve plot.
- [ ] **skew** (GainBias) -- pre-transfer input skew (-1..+1).
- [ ] **xform** (TransformGateControl) -- 8 functions: random, rotate, invert, reverse, smooth, quantize, spread, fold.
- [ ] **level** (GainBias) -- output gain.

#### 3. Sub-displays
- [ ] **segments** sub-display: `offset` (-1..+1), `curve` (none/linear/cubic), `weight` (0-1). SubButtons "offset", "curve", "weight".
- [ ] **curve** sub-display: deviation amplitude + devScope. Shift-toggle cycles through the deviation scope (offset/curve/weight/all).

#### 4. Expansion views
- [ ] **curve** focus -> deviation, devScope, segCount readouts.

#### 5. Menu (onShowMenu)
- [ ] **Presets** -- Linear / S-curve / Staircase / Random.
- [ ] **Clear** -- Reset all segments.

#### 6. CV input behavior
- [ ] input / skew / level CV modulate those parameters.
- [ ] segCount CV modulates active segment count (4-32).
- [ ] deviation / devScope CV modulate random-deviation depth and target.

#### 7. Stereo routing
- [ ] Transfer function applied identically to L/R (native-stereo).

#### 8. Save / load round-trip
- [ ] Quicksave + reload: all 32 (offset, curve type, weight) tuples, skew, level, segCount, deviation, devScope restored.
- [ ] Two back-to-back cycles identical.
- [ ] Edit buffer reloaded via `loadSegment(0)` after deserialize.

#### 9. Edge cases
- [ ] segCount 4: coarse banding in the curve.
- [ ] Deviation scope "all" + max depth: all properties randomize; curve may look chaotic.
- [ ] Input skew ±1: input pre-warped toward extremes.
- [ ] All offsets 0: output flat regardless of input.

---

### Tomograph (spreadsheet)

**Module**: `Filterbank` &middot; **Mnemonic**: `Tm` &middot; **Category**: `Filter Bank` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: 8 bands log-spaced 100 Hz -- 10 kHz, peak type, unity gain, no saturation, 50% mix.
- [ ] Hot-plug: filter bank processes input immediately.

#### 2. Top-level row (expanded view)
Order: `bands`, `overview`, `scale`, `rotate`, `macroQ`, `mix`.
- [ ] **bands** (BandListControl) -- per-band freq/gain/filterType editor.
- [ ] **overview** (FilterResponseControl) -- magnitude response plot.
- [ ] **scale** (ModeSelector) -- 12 built-in scales + user `.scl` slots.
- [ ] **rotate** (GainBias) -- rotate band frequencies by ±16 semitones.
- [ ] **macroQ** (GainBias) -- global Q scaling (0-1).
- [ ] **mix** (MixControl) -- dry/wet + shift-toggle input/output level + tanh.

#### 3. Sub-displays
- [ ] **bands** sub-display: `freq` (Hz, 20-10k), `gain` (0-3), `type` (peak/LP/resonator). SubButtons "freq", "gain", "type".
- [ ] **overview** sub-display: legend + per-band freq response readouts; shift-toggle cycles band/response views.
- [ ] **scale** sub-display: current scale name label; shift-toggle to rescan `/scales/` for Scala files.
- [ ] **mix** sub-display: mix, inputLevel, outputLevel, tanhAmt readouts via shift-toggle.

#### 4. Expansion views
- [ ] **overview** focus -> bandCount, vOctOffset, slew readouts.
- [ ] **mix** focus -> inputLevel, outputLevel, tanhAmt readouts.

#### 5. Menu (onShowMenu)
- [ ] **Bands** -- "Init bands (log spacing)", "Randomize bands", "Rescan .scl files".
- [ ] **Macro Filter Type** -- "All peaking" / "All lowpass" / "All resonator".

#### 6. CV input behavior
- [ ] scale / rotate / macroQ / mix CV modulates those parameters.
- [ ] bandCount CV changes active band count (2-16), frequencies redistributed.
- [ ] vOctOffset 1V = 1 octave shift across all bands.
- [ ] slew CV 0-5 s smoothing on band frequency changes.
- [ ] inputLevel / outputLevel / tanhAmt CV controls input/output gain and saturation.

#### 7. Stereo routing
- [ ] Mono: single Filterbank instance.
- [ ] Stereo: dual-instance; L/R process identical bank.
- [ ] All parameters tied to both instances.

#### 8. Save / load round-trip
- [ ] Quicksave + reload: all (freq, gain, filterType) per band, mix, macroQ, bandCount, rotate, vOctOffset, slew, inputLevel/outputLevel/tanhAmt restored.
- [ ] Two back-to-back cycles identical.
- [ ] Edit buffer reloaded via `loadBand(0)`.

#### 9. Edge cases
- [ ] bandCount 2: large gaps in spectrum, wide response per band.
- [ ] bandCount 16: dense coverage, high CPU.
- [ ] vOctOffset +2: frequencies can exceed Nyquist -- verify clamp / aliasing handling.
- [ ] rotate ±16: band pattern wraps; lowest bands may fold into lowest octave.
- [ ] macroQ 1: narrow peaks, self-oscillation risk at high input.
- [ ] Tanh + high input: output compresses / clips; listen for aliasing.
- [ ] Scale change mid-audio: band frequencies jump -- audible click expected.
- [ ] **Known issue**: one lobe may appear stuck in the center-bottom of the overview viz; does not affect audio.

---

### Petrichor (spreadsheet)

**Module**: `MultitapDelay` &middot; **Mnemonic**: `Pt` &middot; **Category**: `Delay` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: 4 taps, 20 s buffer, each tap time 0.5 s, level 1, pan 0, pitch 0, filter off; feedback 0.3, mix 0.5, grid 1x.
- [ ] Hot-plug: delay buffer stable; audio processed without pops.

#### 2. Top-level row (expanded view)
Order: `tune`, `taps`, `overview`, `masterTime`, `feedback`, `xform`, `mix`.
- [ ] **tune** (Pitch) -- V/Oct global pitch (-24..+24 semitones).
- [ ] **taps** (TapListControl) -- per-tap time/level/pan/pitch editor (up to 8).
- [ ] **overview** (DelayInfoControl / RaindropControl) -- buffer waveform + grain animation + tap timeline.
- [ ] **masterTime** (TimeControl) -- global delay time scaling; shift-toggle grid/reverse/skew.
- [ ] **feedback** (FeedbackControl) -- feedback amount + shift-toggle feedbackTone.
- [ ] **xform** (TransformGateControl) -- 21+ target options (all/taps/delay/filters/...) + depth + spread.
- [ ] **mix** (MixControl) -- dry/wet + shift-toggle input/output/tanh.

#### 3. Sub-displays
- [ ] **taps** sub-display: time (s), level (0-1), pan (-1..+1), pitch (semitones). SubButtons "time", "level", "pan", "pitch".
- [ ] **filters** (FilterListControl, via taps expansion): cutoff (Hz), Q (0-1), type (off/LP/BP/HP/notch). SubButtons "cut", "Q", "type".
- [ ] **overview** sub-display: buffer waveform, grainSize indicator, tapCount, stack mode.
- [ ] **masterTime** sub-display: time, grid selector, reverse probability, skew. SubButtons "time", "grid", "reverse", "skew".
- [ ] **feedback** sub-display: feedback, feedbackTone. SubButtons "fdbk", "tone".
- [ ] **xform** sub-display: target name, depth, spread. SubButtons "tgt", "depth", "sprd".
- [ ] **mix** sub-display: mix, input/output level, tanh amount.

#### 4. Expansion views
- [ ] **taps** focus -> filters, tapCount, volume/pan/cutoff/Q/type macros.
- [ ] **overview** focus -> grainSize, tapCount, stack.
- [ ] **masterTime** focus -> grid, drift, reverse, skew.
- [ ] **feedback** focus -> feedbackTone.
- [ ] **mix** focus -> inputLevel, outputLevel, tanhAmt.

#### 5. Menu (onShowMenu)
- [ ] **Buffer Size** -- "2 sec", "5 sec", "10 sec", "20 sec". Tap times clipped to new buffer.

#### 6. CV input behavior
- [ ] tune / masterTime / feedback / feedbackTone / mix CV modulates smoothly.
- [ ] tapCount 1-8, redistributes times on change.
- [ ] grid / drift / reverse / stack / grainSize / skew CV modulates.
- [ ] xformGate trigger fires randomization on selected target.
- [ ] inputLevel / outputLevel / tanhAmt CV modulates.

#### 7. Stereo routing
- [ ] Dual-instance: separate C++ ops per channel; tap list and filter list shared.
- [ ] Pan param per tap: -1=left, 0=center, +1=right; applied on both instances.
- [ ] grid / drift / reverse / skew tied to both instances.

#### 8. Save / load round-trip
- [ ] Quicksave + reload: all 8 tap (time, level, pan, pitch, filterCutoff, filterQ, filterType) tuples, bufferSeconds, top-level params (masterTime, feedback, feedbackTone, mix, tapCount, skew, drift, reverse, stack, grid, input/output level, tanhAmt, vOctPitch) restored.
- [ ] Two back-to-back cycles identical.
- [ ] Edit buffers reloaded via `loadTap(0)`, `loadFilter(0)`.
- [ ] **Known-fragile**: tap states, tap count, and xform target/depth/spread are currently NOT captured in Lua deserialize. Verify each item explicitly; this is the primary bug surface to watch.

#### 9. Edge cases
- [ ] tapCount 1: only first tap; feedback from single tap.
- [ ] tapCount 8 + grid 16: dense, CPU-heavy.
- [ ] Buffer 2 s + masterTime 20 s: tap times > 2 s clipped.
- [ ] feedback 0.95 + high input: ringing / runaway without tanh.
- [ ] feedbackTone -1 (HP): feedback loop thin; +1 (LP): loop darkens each iteration.
- [ ] reverse probability 1.0: all taps reverse.
- [ ] stack 16: each tap subdivides into 16 grains.
- [ ] drift > 0: tap times wander.
- [ ] grainSize 0: glitchy; 1: blurred time-stretch.
- [ ] Volume macro "off": all taps mute.
- [ ] Apply xform randomize, quicksave, reload -- **explicitly confirm tap parameters persist** (this is the known regression).

---

### Parfait (spreadsheet)

**Module**: `MultibandSaturator` &middot; **Mnemonic**: `Pf` &middot; **Category**: `Saturator` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error; both L/R inputs accept audio.
- [ ] Default: 3-band (lo/mid/hi), drive 1.0, band level 1.0, amount 0.5, type = Off. Unity passthrough.
- [ ] Hot-plug: no audio pops.

#### 2. Top-level row (expanded view)
Order: `drive`, `bandlo`, `bandmid`, `bandhi`, `skew`, `mix`.
- [ ] **drive** (DriveControl, 0-16, default 1.0) -- pre-saturation gain.
- [ ] **bandlo / bandmid / bandhi** (BandControl, 0-2, default 1.0) -- per-band output level.
- [ ] **skew** (GainBias, -1..+1) -- saturation curve asymmetry.
- [ ] **mix** (ParfaitMixControl, 0-1) -- dry/wet.

#### 3. Sub-displays
- [ ] **drive** shows drive, toneAmount, toneFreq readouts. SubButtons focus each.
- [ ] **band controls** show band level, amount (0-1), bias (-1..1), type (0-7: Off/Tube/Diode/Fold/Half/Crush/Sine/Fractal), weight (0.1-4). SubButtons cycle focus.
- [ ] **mix** shows mix, compress amount, output level (0-4), tanh saturation.

#### 4. Expansion views
- [ ] **drive** -> drive, toneAmount, toneFreq.
- [ ] **bandlo / mid / hi** -> band, amount, bias, type, weight, freq, morph, Q.
- [ ] **mix** -> mix, compAmt, scHpf, outputLevel, tanhAmt.

#### 5. Menu (onShowMenu)
No explicit menu.

#### 6. CV input behavior
- [ ] All macro params (drive, tone, skew, mix, compress) accept CV.
- [ ] Per-band amount/bias/type/weight/freq/morph/Q accept CV.
- [ ] Per-band level accepts CV.

#### 7. Stereo routing
- [ ] Dual-instance: each channel has its own op.
- [ ] All parameters tied to both.

#### 8. Save / load round-trip
- [ ] Quicksave with all three bands at varied levels (0.5/1.5/2.0), amounts (0.2/0.5/0.9), biases (-0.5/0/+0.5), types (0-7), weights (0.5/1.0/2.0), filter freq/morph/Q varied. Drive, skew, tone, mix, compress, output level, tanh all non-default. Reload -- every value matches.
- [ ] Two back-to-back cycles identical.

#### 9. Edge cases
- [ ] Drive 16 + all bands type=Fractal: extreme saturation; verify no runaway.
- [ ] Compress 1.0 with transient input: dynamics reduction audible.
- [ ] Tone freq sweep across 3 bands at different frequencies.
- [ ] Band types: Crush / LP / Off combination.

---

### Rauschen (spreadsheet)

**Module**: `Rauschen` &middot; **Mnemonic**: `Rn` &middot; **Category**: `Noise Generator` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: White noise, X/Y 0.5, cutoff 10 kHz, level 0.5. Produces noise at output (generator, chain input sunk).
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `algo`, `viz`, `paramX`, `paramY`, `cutoff`, `level`.
- [ ] **algo** (ModeSelector) -- 0-10: White, Pink, Dust, Particle, Crackle, Logistic, Henon, Clocked, Velvet, Gendy, Lorenz. Label shows algorithm name.
- [ ] **viz** -- live phase-space plot (2D/3D attractor depending on algorithm).
- [ ] **paramX** (GainBias, 0-1) -- algo-dependent first parameter.
- [ ] **paramY** (GainBias, 0-1) -- algo-dependent second parameter.
- [ ] **cutoff** (RauschenCutoffControl) -- post-generator filter cutoff.
- [ ] **level** (GainBias, 0-1) -- output amplitude.

#### 3. Sub-displays
- [ ] **algo** label shows current algorithm; encoder cycles through 11 options.
- [ ] **cutoff** sub-display: cutoff (20-20000 Hz), morph (0-1 with threshold labels off/LP/L>B/BP/B>H/HP/H>N/ntch), Q (0.5-20). SubButtons "cutoff", "morph", "Q".

#### 4. Expansion views
- [ ] **cutoff** focus -> cutoff, morph, filterQ.

#### 5. Menu (onShowMenu)
No explicit menu.

#### 6. CV input behavior
- [ ] algo CV cycles algorithms (discrete quantized).
- [ ] paramX / paramY / cutoff / morph / Q / level accept continuous CV.

#### 7. Stereo routing
- [ ] Dual-instance; both channels run the same algorithm with same params.

#### 8. Save / load round-trip
- [ ] Quicksave with algorithm = Lorenz, paramX 0.3, paramY 0.8, cutoff 2 kHz, morph 0.33 (BP), Q 5.0, level 0.75. Reload -- all restored.
- [ ] Two back-to-back cycles identical.

#### 9. Edge cases
- [ ] Lorenz with paramX/Y near 1: extreme chaos, verify stability.
- [ ] cutoff 20 Hz + level 1.0: sub-bass tested without clipping.
- [ ] Morph sweep LP→BP→HP in realtime: smooth transitions.
- [ ] paramX/Y CV at audio rate: stability, no zipper or clicks.

---

### Impasto (spreadsheet)

**Module**: `MultibandCompressor` &middot; **Mnemonic**: `Im` &middot; **Category**: `Compressor` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: 3-band (lo/mid/hi), drive 1.0, threshold 0.5, ratio 2.0, attack 1 ms, release 50 ms, band level 1.0. Light compression at unity output.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `drive`, `sidechain`, `lo`, `mid`, `hi`, `skew`, `mix`.
- [ ] **drive** (DriveControl, 0-4).
- [ ] **sidechain** (CompSidechainControl) -- input gain fader, sidechain enable toggle, external SC input jack.
- [ ] **lo / mid / hi** (CompBandControl) -- band level, threshold, ratio, speed readouts.
- [ ] **skew** (GainBias, -1..+1).
- [ ] **mix** (CompMixControl, 0-1).

#### 3. Sub-displays
- [ ] **drive** shows drive, tone amount, tone freq.
- [ ] **sidechain** shows input gain readout + sidechain BinaryIndicator. SubButton 2 toggles sidechain enable (affects both channels after recent fix).
- [ ] **band controls** show band level, threshold, ratio, speed. SubButtons cycle focus.
- [ ] **mix** shows mix, auto-makeup BinaryIndicator, output level (0-2). SubButton 1 toggles auto-makeup (affects both channels after recent fix).

#### 4. Expansion views
- [ ] **lo / mid / hi** -> band, threshold, ratio, attack, release.
- [ ] **drive** -> drive, toneAmount, toneFreq.
- [ ] **mix** -> mix, mixOutput.

#### 5. Menu (onShowMenu)
No explicit menu.

#### 6. CV input behavior
- [ ] drive / band threshold / ratio / speed / attack / release / mix all accept CV.
- [ ] sidechain input gain accepts CV; external SC jack accepts audio.

#### 7. Stereo routing
- [ ] Dual-instance: separate compressors on L/R.
- [ ] Sidechain: mono (both channels keyed off same) or stereo (independent).
- [ ] AutoMakeup and SidechainEnable options **both channels after fix** (`CompMixControl` and `CompSidechainControl` sync both ops on toggle).
- [ ] `MultibandCompressor:deserialize` syncs opR options to op on load (fixes legacy patches).

#### 8. Save / load round-trip
- [ ] Quicksave with band thresholds (0.2/0.5/0.8), ratios (1.5/4.0/10.0), speeds (0.1/0.5/0.9), attack (0.001/0.01/0.05 s), release (0.01/0.1/1.0 s). Drive, skew, tone, mix, sidechain enable/gain non-default. Reload -- all values and option states match, **and opR's options match op's**.
- [ ] Two back-to-back cycles identical.

#### 9. Edge cases
- [ ] Ratio 20 + threshold 0.1 + attack 0.0001 s: extreme fast limiting.
- [ ] Sidechain enabled with external low-freq tone: verify band-aware keying.
- [ ] Release 1.0 s + continuous compression: smooth recovery.
- [ ] All bands ratio=1: passthrough verification.
- [ ] Toggle AutoMakeup in stereo: confirm both channels react.

---

### Helicase (spreadsheet)

**Module**: `Helicase` &middot; **Mnemonic**: `Hx` &middot; **Category**: `Synthesizer / FM` &middot; **Stereo**: `native-stereo`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: f0 110 Hz, carrierShape 0, modMix 0.5, modIndex 1.0, feedback 0, level 0.5. Moderately modulated tone at ~110 Hz.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `tune`, `f0`, `overview`, `shaping`, `modulator`, `sync`, `level`.
- [ ] **tune** (Pitch) -- V/Oct, 10x internal gain so 1 V = 1 octave.
- [ ] **f0** (GainBias, 0.1-2000 Hz) -- base frequency.
- [ ] **overview** (HelicaseOverviewControl) -- phase-space viz + modMix fader.
- [ ] **shaping** (HelicaseShapingControl) -- transfer curve preview + modIndex fader.
- [ ] **modulator** (HelicaseModControl) -- circular modulator ribbon + ratio fader.
- [ ] **sync** (HelicaseSyncControl) -- comparator scope + phase threshold.
- [ ] **level** (GainBias, 0-1) -- output amplitude.

#### 3. Sub-displays
- [ ] **overview** shows modMix readout, carrierShape readout (0-7), FM mode toggle (lin/exp). Three sub-buttons cycle focus.
- [ ] **shaping** shows modIndex (0-10), discIndex (0-1), discType (0-15 label).
- [ ] **modulator** shows ratio (0.5-16), feedback (0-1), modShape (0-7 label).
- [ ] **sync** shows phase threshold readout + mini sync-input scope. SubButton "input" (branch show), "phase" (focus threshold), "fire" (simulate rising edge).

#### 4. Expansion views
- [ ] **overview** -> overview, overModMix, overCarrierShape, overLinExpo.
- [ ] **shaping** -> shaping, shapModIndex, shapDiscIndex, shapDiscType.
- [ ] **modulator** -> modulator, modRatio, modFeedback, modShape, modFine.
- [ ] **sync** -> sync, syncPhase. Confirm phase-threshold fader has its own CV input jack.

#### 5. Menu (onShowMenu)
- [ ] **Mode** header; **Quality** OptionControl: "lo-fi" / "hi-fi".

#### 6. CV input behavior
- [ ] V/Oct 1 V = 1 octave (note 10x internal gain).
- [ ] f0 / modMix / carrierShape / modIndex / discIndex / discType / ratio / feedback / modShape / fine / level accept CV.
- [ ] Sync gate: rising edge causes phase-latched sync at the selected threshold.
- [ ] syncPhase expansion fader accepts CV (verified via new sync expansion row).

#### 7. Stereo routing
- [ ] Native stereo; output duplicated to L/R.
- [ ] LinExpo and HiFi options on single op.

#### 8. Save / load round-trip
- [ ] Quicksave with f0 220 Hz, carrierShape 3, modMix 0.7, modIndex 2.0, feedback 0.3, ratio 3.5, modShape 2, discIndex non-zero, discType 5, fine ±50 cents, level 0.6, Quality=hi-fi, phase threshold non-zero. Reload -- all restored.
- [ ] Two back-to-back cycles identical.

#### 9. Edge cases
- [ ] Feedback 1.0 + modIndex 10: extreme FM complexity; verify modulator feedback soft clip tames runaway.
- [ ] Ratio 0.5: sub-harmonic modulation.
- [ ] Hard sync at audio rate: verify phase reset behavior.
- [ ] Quality = lo-fi: audible bit crush vs hi-fi.
- [ ] Discontinuity shapes 7/12/15: listen for known pops at zero crossings (tracked todo).

---

### Larets (spreadsheet)

**Module**: `Larets` &middot; **Mnemonic**: `Lr` &middot; **Category**: `Stepped Multi-Effect` &middot; **Stereo**: `native-stereo`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: 16 steps, all types 0 (off), param 0.5, ticks 4. Passes through at unity mix.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `clock`, `steps`, `overview`, `offset`, `xform`, `mix`.
- [ ] **clock** (LaretClockControl) -- clock input + divider selector + manual reset trigger.
- [ ] **steps** (LaretStepListControl) -- step index select + type/param/ticks readouts (type uses `addName` for abbreviations: off/stt/rev/bit/dec/flt/pch/drv/shf/dly/cmb).
- [ ] **overview** (LaretOverviewControl) -- live waveform viz + step count + loop length faders.
- [ ] **offset** (GainBias, -1..+1) -- bipolar global param offset.
- [ ] **xform** (TransformGateControl) -- gate + function (all/t+p/type/prm/tick/rot/rev) + depth.
- [ ] **mix** (LaretsMixControl) -- dry/wet + shift-toggle Output/Comp/Auto makeup.

#### 3. Sub-displays
- [ ] **clock** sub-display: clockDiv readout. SubButtons "clock" (focus div), "reset" (manual reset).
- [ ] **steps** sub-display: type readout (name via addName), param readout, ticks readout. SubButtons "type", "param", "ticks". Readouts use `useHardSet` so target and value stay in sync.
- [ ] **overview** sub-display: stepCount fader (1-16), loopLength fader (1-16).
- [ ] **mix** sub-display (Parfait-style): shift-toggle reveals Output / Comp / Auto. SubButton 3 toggles auto-makeup BinaryIndicator.

#### 4. Expansion views
- [ ] **clock** -> clock, reset, clockDiv.
- [ ] **overview** -> overview, skew, stepCount, loopLength.

#### 5. Menu (onShowMenu)
- [ ] **Set All Tick Lengths** header; tasks "1 tick" / "2 ticks" / "4 ticks" / "8 ticks" / "16 ticks".

#### 6. CV input behavior
- [ ] clock / reset / xformGate accept triggers.
- [ ] stepCount / loopLength / offset / skew / mix / compressAmt / outputLevel accept CV.
- [ ] Offset CV must track smoothly sample-to-sample (filter and pitch were moved off step-transition caches for this).

#### 7. Stereo routing
- [ ] Native stereo; output duplicated.
- [ ] AutoMakeup option on single op.

#### 8. Save / load round-trip
- [ ] Quicksave with 16 steps varied: types across 0-10, params 0.1/0.5/0.9, ticks 1/2/4/8/16. Step count, loop length, skew, offset, mix, output level, compressAmt non-default, auto-makeup on. Reload -- all step data and globals restored.
- [ ] Two back-to-back cycles identical.
- [ ] Legacy patches with loopLength=0 migrate to 16 on load.

#### 9. Edge cases
- [ ] Step count 1 + loop length 1: single-step hold (momentary-hold gesture).
- [ ] clockDiv 16 + 1-tick steps: very slow sequencing.
- [ ] All steps type=0, mix=1: passthrough confirmation.
- [ ] Transform scope "all" + depth 1.0: uniform randomization.
- [ ] Distortion step at drive=1.0 (param=1) with comp=1.0 and auto-makeup on: hard clipping with managed ceiling.

---

# biome package

### NR (biome)

**Module**: `NR` &middot; **Mnemonic**: `NR` &middot; **Category**: `Sequencers` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: Prime=0, Mask=0, Factor=1, Length=16, Width=0.5 produces rhythmic gate output on clock input.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `circle`, `reset`, `prime`, `mask`, `factor`, `length`, `width`.
- [ ] **circle** -- visualization showing current step (1-16), Prime pattern, Mask bits, Factor index.
- [ ] **reset** (Gate) -- rising edge resets step counter.
- [ ] **prime** (GainBias, 0-31) -- selects one of 32 rhythm patterns.
- [ ] **mask** (GainBias, 0-3) -- AND-masks pattern bits.
- [ ] **factor** (GainBias, 0-16) -- scales step increment.
- [ ] **length** (GainBias, 1-16) -- sets total steps.
- [ ] **width** (GainBias, 0-1) -- gate pulse width.

#### 3. Sub-displays
Not applicable; circle view is integrated.

#### 4. Expansion views
- [ ] **circle** focus shows full 16-step ratchet layout with real-time indicator.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] reset gate: rising edge resets step counter.
- [ ] prime / mask / factor / length / width accept CV (prime/length clamped to integer ranges).

#### 7. Stereo routing
Mono only.

#### 8. Save / load round-trip
- [ ] Quicksave with Prime=17, Mask=2, Factor=8, Length=12, Width=0.75. Reload -- all restored.
- [ ] Clock input produces same pattern pre/post-save.

#### 9. Edge cases
- [ ] Factor=0: step increment zero, stuck step.
- [ ] Length=1: single-step cycle.
- [ ] Width=0: silent; Width=1: continuous gate.
- [ ] Rapid Prime changes update pattern immediately.

---

### 94 Discont (biome)

**Module**: `Discont` &middot; **Mnemonic**: `Dc` &middot; **Category**: `Distortion` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: Mode=0 (Fold), Amount=1.0, Mix=1.0.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `mode`, `amount`, `mix`.
- [ ] **mode** -- ModeSelector: Fold, Tanh, Soft, Clip, Sqrt, Rect, Crush.
- [ ] **amount** (GainBias, 0-10) -- distortion intensity.
- [ ] **mix** (GainBias, 0-1) -- dry/wet.

#### 3. Sub-displays
- [ ] **mode** ModeSelector cycles seven modes via SubButton 2.

#### 4. Expansion views
Mode is available directly on the main row.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] mode CV clamped to [0,6] (integer); fast modulation switches modes audibly.
- [ ] amount / mix accept continuous CV.

#### 7. Stereo routing
- [ ] Dual-instance: opL, opR; Mode / Amount / Mix shared.
- [ ] Mode toggle affects both channels symmetrically.

#### 8. Save / load round-trip
- [ ] Quicksave with Mode=3 (Clip), Amount=7.5, Mix=0.6. Reload -- all restored.
- [ ] Audio output matches pre-save.

#### 9. Edge cases
- [ ] Amount=0: unaffected audio regardless of Mix.
- [ ] Amount=10: maximum distortion; verify no buffer overflow.
- [ ] Mix=0: pure dry.
- [ ] Rapid Mode switching via CV: no clicks.

---

### Latch Filter (biome)

**Module**: `LatchFilter` &middot; **Mnemonic**: `LF` &middot; **Category**: `Filters` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: V/Oct=0, Fundamental=0, Resonance=0.5, Mode=0 (LP).
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `tune`, `fundamental`, `resonance`, `mode`.
- [ ] **tune** (Pitch) -- V/Oct.
- [ ] **fundamental** (GainBias, -48..+48 semitones).
- [ ] **resonance** (GainBias, 0-1).
- [ ] **mode** (ModeSelector) -- LP / HP.

#### 3. Sub-displays
- [ ] **mode** ModeSelector cycles {LP, HP}.

#### 4. Expansion views
All top-level.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] tune V/Oct exponential.
- [ ] fundamental / resonance / mode accept CV.

#### 7. Stereo routing
- [ ] Dual-instance; V/Oct, Fundamental, Resonance, Mode shared.

#### 8. Save / load round-trip
- [ ] Quicksave with V/Oct=2, Fundamental=12, Resonance=0.8, Mode=1 (HP). Reload -- all restored.

#### 9. Edge cases
- [ ] Resonance=1.0 at high Fundamental: self-oscillation stability.
- [ ] Mode toggle at extreme Resonance: no impulse.
- [ ] V/Oct LFO sweep: smooth frequency tracking.

---

### Canals (biome)

**Module**: `Canals` &middot; **Mnemonic**: `Ca` &middot; **Category**: `Filters` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: V/Oct=0, Fundamental=0, Span=0.25, Quality=0.0, Output=0 (LOW), Mode=0 (Crossover).
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `mode`, `tune`, `fundamental`, `span`, `quality`, `output`.
- [ ] **mode** (ModeSelector) -- Crossover / Formant.
- [ ] **tune** (Pitch) -- V/Oct.
- [ ] **fundamental** (GainBias, -48..+48 semitones).
- [ ] **span** (GainBias, 0-1) -- band separation.
- [ ] **quality** (GainBias, -1..+1) -- Q boost/cut.
- [ ] **output** (ModeSelector) -- LOW / CTR / HIGH / ALL.

#### 3. Sub-displays
- [ ] **mode** cycles {Xover, Formnt}.
- [ ] **output** cycles {LOW, CTR, HIGH, ALL}.

#### 4. Expansion views
All top-level.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] tune / fundamental / span / quality / output / mode accept CV.

#### 7. Stereo routing
- [ ] Dual-instance; all params shared; distinct L/R behavior when inputs differ.

#### 8. Save / load round-trip
- [ ] Quicksave with V/Oct=1, Fundamental=5, Span=0.6, Quality=0.3, Output=3 (ALL), Mode=1 (Formant). Reload -- all restored.

#### 9. Edge cases
- [ ] Span=0: all bands collapse to single freq.
- [ ] Quality=1 + high Span: strong resonant peaks; stability.
- [ ] Output toggle LOW/CTR/HIGH/ALL: no pops.
- [ ] Mode Xover→Formnt at non-zero Span: smooth transition.

---

### Gesture (biome)

**Module**: `GestureSeq` &middot; **Mnemonic**: `Gs` &middot; **Category**: `Samplers` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error; allocates 5 s default buffer.
- [ ] Default: Run=off, Reset=ready, Offset=0, Slew=0, Erase off. Silent until Run gate.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `run`, `reset`, `offset`, `slew`, `erase`, `write`.
- [ ] **run** (Gate, toggle) -- high holds position, low pauses.
- [ ] **reset** (Gate, trigger) -- rising edge returns playhead to start.
- [ ] **offset** (GainBias, -1..+1) -- shifts playhead position.
- [ ] **slew** (GainBias, 0-10 s) -- output smoothing.
- [ ] **erase** (Gate) -- high zeros buffer under playhead.
- [ ] **write** -- display-only; shows WriteActive state.

#### 3. Sub-displays
- [ ] **wave** WaveView shows horizontal waveform display.
- [ ] SubButton "|<<" resets playhead to start.
- [ ] SubButton "> / ||" toggles play/pause.

#### 4. Expansion views
- [ ] **offset / slew / erase** focus shows waveform + respective slider/indicator.

#### 5. Menu (onShowMenu)
- [ ] **Buffer** header; tasks: "5 sec" / "10 sec" / "20 sec".
- [ ] **Clear Buffer** task zeros all samples.
- [ ] **Write Sensitivity** header; OptionControl {Low, Medium, High}.

#### 6. CV input behavior
- [ ] run toggle / reset trigger / offset CV / slew CV / erase gate all behave as specified.

#### 7. Stereo routing
Mono only.

#### 8. Save / load round-trip
- [ ] Quicksave with 10 s buffer, Offset=0.3, Slew=2.0 s, Sensitivity=High. Reload -- buffer contents, params, sensitivity restored.
- [ ] Audio resumes from same position with same slew.

#### 9. Edge cases
- [ ] Buffer size change (5→10→20 s) during playback: playhead scales.
- [ ] Clear Buffer during playback: silence on next read.
- [ ] Erase gate held continuously with Run=on: gradual erase.
- [ ] Slew=10 s produces 10 s glide.
- [ ] Sensitivity changes only affect future recordings.

---

### Gated Slew (biome)

**Module**: `GatedSlewLimiter` &middot; **Mnemonic**: `GS` &middot; **Category**: `Utilities` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: Slew Time=1.0 s, Direction=both, Gate=open. Linear rate limiting.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `gate`, `time`, `dir`.
- [ ] **gate** (Gate) -- high enables slewing, low passes input directly.
- [ ] **time** (GainBias) -- slew time (log map).
- [ ] **dir** (OptionControl) -- {up, both, down}.

#### 3. Sub-displays
- [ ] **dir** OptionControl cycles {up, both, down}.

#### 4. Expansion views
All top-level.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] gate 5 V enables/disables slew.
- [ ] time CV modulates slew time.

#### 7. Stereo routing
- [ ] Dual-instance; slew2.Direction tied to slew1.Direction.

#### 8. Save / load round-trip
- [ ] Quicksave with Time=0.5 s, Direction=up. Reload -- restored.

#### 9. Edge cases
- [ ] Gate=high + fast input step: ramps at slew rate.
- [ ] Gate=low: passes input.
- [ ] Direction=up with downward ramp: passes unchanged.
- [ ] Time=0: near-instantaneous even with Gate=high.

---

### Tilt EQ (biome)

**Module**: `TiltEQ` &middot; **Mnemonic**: `TQ` &middot; **Category**: `EQ` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: Tilt=0.0 (flat).
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `tilt`.
- [ ] **tilt** (GainBias, -1..+1) -- dark/bright balance.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
All top-level.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] tilt CV sweeps dark↔bright smoothly.

#### 7. Stereo routing
- [ ] Dual-instance; Tilt shared.

#### 8. Save / load round-trip
- [ ] Quicksave with Tilt=-0.75. Reload -- restored.

#### 9. Edge cases
- [ ] Tilt=-1 (max dark): stability.
- [ ] Tilt=+1 (max bright): no clipping.
- [ ] LFO modulation -1..+1: smooth tonal sweep.

---

### DJ Filter (biome)

**Module**: `DJFilter` &middot; **Mnemonic**: `DJ` &middot; **Category**: `Filters` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: Cut=0.0 (bypass), Q=0.5.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `cut`, `q`.
- [ ] **cut** (GainBias, -1..+1) -- bipolar LP/HP.
- [ ] **q** (GainBias, 0-1) -- resonance.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
All top-level.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] cut CV smooth LP→bypass→HP.
- [ ] q CV modulates resonance.

#### 7. Stereo routing
- [ ] Dual-instance; Cut and Q shared.

#### 8. Save / load round-trip
- [ ] Quicksave with Cut=-0.8, Q=0.9. Reload -- restored.

#### 9. Edge cases
- [ ] Cut=0: full bypass regardless of Q.
- [ ] Cut=±1 + Q=1: resonant peak; verify no instability.
- [ ] Rapid CV sweep on Cut: smooth transition.

---

### Gridlock (biome)

**Module**: `Gridlock` &middot; **Mnemonic**: `GL` &middot; **Category**: `Logic` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: Gates all off, Value1=1.0, Value2=0.0, Value3=-1.0.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `gate1`, `val1`, `gate2`, `val2`, `gate3`, `val3`.
- [ ] **gate1 / gate2 / gate3** (Gate) -- high activates value at priority 1/2/3.
- [ ] **val1 / val2 / val3** (GainBias, -5..+5) -- priority-tiered outputs.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
All top-level.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] Priority logic: Gate1 > Gate2 > Gate3.
- [ ] Value CVs modulate in real-time.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave with Value1=2.5, Value2=-0.5, Value3=-3.5. Reload -- restored.

#### 9. Edge cases
- [ ] All gates low: output holds Value3.
- [ ] All gates high: Gate1 wins.
- [ ] Rapid gate switching: immediate transitions.

---

### Integrator (biome)

**Module**: `Integrator` &middot; **Mnemonic**: `IN` &middot; **Category**: `Utilities` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: Rate=1.0, Leak=0.0, Reset ready.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `rate`, `leak`, `reset`.
- [ ] **rate** (GainBias, 0-100) -- integration speed.
- [ ] **leak** (GainBias, 0-1) -- discharge rate.
- [ ] **reset** (Gate) -- rising edge zeros accumulator.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
All top-level.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] rate / leak CV modulate; reset trigger zeros.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave with Rate=50, Leak=0.1. Reload -- restored.

#### 9. Edge cases
- [ ] Rate=0: no integration.
- [ ] Rate=100 + constant input: rapid ramp; stays clipped ±5.
- [ ] Leak=0 + sustained input: unbounded accumulation.
- [ ] Leak=1 + positive input: exponential decay toward zero.
- [ ] Reset during accumulation: immediate zero.

---

### Spectral Follower (biome)

**Module**: `SpectralFollower` &middot; **Mnemonic**: `SF` &middot; **Category**: `Analysis` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: Freq=1000 Hz, Bandwidth=1.0 oct, Attack=5 ms, Decay=50 ms.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `freq`, `bandwidth`, `attack`, `decay`.
- [ ] **freq** (GainBias, 20-20000 Hz log) -- center frequency.
- [ ] **bw** (GainBias, 0.1-4 oct) -- bandpass Q.
- [ ] **atk** (GainBias, 0.0001-0.5 s) -- rise.
- [ ] **dec** (GainBias, 0.0001-5.0 s) -- fall.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
All top-level.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] freq / bw / attack / decay all accept CV.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave with Freq=500 Hz, BW=2.0, Attack=0.01 s, Decay=0.2 s. Reload -- restored.

#### 9. Edge cases
- [ ] Freq=20 Hz + BW=0.1: narrow low tracker.
- [ ] Freq=20 kHz + BW=4: broad high.
- [ ] Attack=0.0001: nearly instantaneous.
- [ ] Decay=5.0 s: long release.
- [ ] Rapid Freq CV sweep: smooth tracking.
- [ ] Narrow BW with broadband input: output drops.
- [ ] Attack < Decay or Attack > Decay: asymmetric envelope.

---

### Quantoffset (biome)

**Module**: `GridQuantizer` &middot; **Mnemonic**: `QO` &middot; **Category**: `Quantizer` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: offset=0.0, levels=12 (chromatic).
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `offset`, `levels`.
- [ ] **offset** (GainBias) -- rotate continuously.
- [ ] **lvls** (GainBias, 2-128) -- quantization resolution.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] offset / levels accept CV (levels rounded to integers).

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave preserves offset and levels.
- [ ] Two back-to-back cycles identical.

#### 9. Edge cases
- [ ] levels=2: binary quantization.
- [ ] levels=128: fine.
- [ ] Large offset + large levels: wrap behavior.

---

### PSR (biome)

**Module**: `PingableScaledRandom` &middot; **Mnemonic**: `SR` &middot; **Category**: `Random / Sequencer` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: scale=1.0, offset=0.0, levels=0 (continuous).
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `trig`, `scale`, `offset`, `levels`.
- [ ] **trig** (Gate) -- rising edge triggers new sample.
- [ ] **scale** (GainBias, 0-5) -- amplitude.
- [ ] **offset** (GainBias, -5..+5) -- DC.
- [ ] **lvls** (GainBias, 0-128) -- 0=continuous, >0 quantizes.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] trig gate fires new random.
- [ ] scale / offset / levels modulate.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave preserves scale, offset, levels, held value.
- [ ] Same trigger produces same value (deterministic per seed).

#### 9. Edge cases
- [ ] Rapid triggers: each fires fresh random.
- [ ] Levels=0: continuous distribution.
- [ ] Negative scale: inverts sign.

---

### Bletchley Park (biome)

**Module**: `CodescanOsc` &middot; **Mnemonic**: `CO` &middot; **Category**: `Oscillator` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: scan=0.0, f0=110 Hz, level=0.5.
- [ ] Emulator auto-loads `libbiome.so`; hardware requires manual file load via menu.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `scan`, `V/Oct`, `f0`, `sync`, `level`.
- [ ] **scan** (ScanControl, 0-1) -- position in binary data; 256-sample waveform preview.
- [ ] **V/Oct** (Pitch) -- semitone/cent steps.
- [ ] **f0** (GainBias, 0.1-2000 Hz).
- [ ] **sync** (Gate) -- phase reset on rising edge.
- [ ] **level** (GainBias, -1..+1).

#### 3. Sub-displays
- [ ] **scan** sub-display: 256-point waveform preview.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
- [ ] **Load File** -- opens file chooser.
- [ ] **Data Info** -- shows loaded filename + byte size.

#### 6. CV input behavior
- [ ] V/Oct 1 V = 1 octave.
- [ ] scan / f0 modulates.
- [ ] sync rising edge triggers phase reset.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave serializes loaded file path.
- [ ] Reload restores path and reloads data.
- [ ] Cross-file load persistence.

#### 9. Edge cases
- [ ] No data: silence.
- [ ] Scan sweep: rapid changes may alias.
- [ ] File load fails: retains previous data or silence.
- [ ] V/Oct + fundamental multiplicative.

---

### Station X (biome)

**Module**: `CodescanFilter` &middot; **Mnemonic**: `CF` &middot; **Category**: `Filter` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: scan=0.0, taps=32, mix=0.5.
- [ ] Emulator auto-loads `libbiome.so`; hardware requires manual load.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `scan`, `taps`, `mix`.
- [ ] **scan** (ScanControl, 0-1) -- FIR kernel position; 64-sample preview.
- [ ] **taps** (GainBias, 4-64) -- filter length.
- [ ] **mix** (GainBias, 0-1) -- dry/wet.

#### 3. Sub-displays
- [ ] **scan** sub-display: 64-point FIR kernel preview.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
- [ ] **Load File** / **Data Info**.

#### 6. CV input behavior
- [ ] scan / taps / mix modulates.

#### 7. Stereo routing
- [ ] Dual-instance; both load same data; parameters tied.

#### 8. Save / load round-trip
- [ ] Quicksave serializes file path.
- [ ] Reload restores path, reloads to both instances.

#### 9. Edge cases
- [ ] No data: passthrough.
- [ ] taps=4: minimal; taps=64: max ripple/delay.
- [ ] Rapid scan: audible kernel discontinuities.

---

### Fade Mixer (biome)

**Module**: `FadeMixer` &middot; **Mnemonic**: `FM` &middot; **Category**: `Mixer` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: fade=0.0, level=1.0. Outputs input 1 at unity.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `ch1`, `ch2`, `ch3`, `ch4`, `fade`, `level`.
- [ ] **in1..in4** (BranchMeter) -- per-input gain with meters.
- [ ] **fade** (GainBias, 0-1) -- equal-power crossfade across 4 inputs.
- [ ] **level** (GainBias, 0-4) -- output scaling.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] Each input / fade / level accepts CV.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave preserves input gains, fade, output level.

#### 9. Edge cases
- [ ] All inputs 0: silence.
- [ ] Fade=0: input 1 only; fade=1: input 4 only.
- [ ] Equal-power: loudness constant across fade.
- [ ] High input gain: verify clipping behavior.

---

### Varishape Voice (biome)

**Module**: `VarishapeVoice` &middot; **Mnemonic**: `VV` &middot; **Category**: `Oscillator / Voice` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: gate=off. Silent until gate rises.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `gate`, `shape`, `V/Oct`, `f0`, `decay`, `level`.
- [ ] **gate** (Gate) -- sustains while high.
- [ ] **shape** (GainBias, 0-1) -- waveform morph.
- [ ] **V/Oct** (Pitch).
- [ ] **f0** (GainBias, 0.1-2000 Hz).
- [ ] **decay** (GainBias, 0-1) -- release slope.
- [ ] **level** (GainBias, -1..+1).

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] gate sustains; shape / V/Oct / sync / decay modulate.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave preserves shape, f0, decay; envelope state is live-only.

#### 9. Edge cases
- [ ] Gate sustained: envelope at peak.
- [ ] Gate release with decay=0: instant cutoff.
- [ ] Shape=0: pure sine; 1: square/saw blend.
- [ ] Decay=1: ~5 s release.

---

### Varishape Osc (biome)

**Module**: `VarishapeOsc` &middot; **Mnemonic**: `VO` &middot; **Category**: `Oscillator` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: shape=0, f0=110 Hz. Outputs sine wave.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `shape`, `V/Oct`, `f0`, `level`.
- [ ] **shape** (GainBias, 0-1) -- morph.
- [ ] **V/Oct** (Pitch).
- [ ] **f0** (GainBias, 0.1-2000 Hz).
- [ ] **level** (GainBias, -1..+1).

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] V/Oct / shape / sync modulates.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave preserves shape, f0; phase is live-only.

#### 9. Edge cases
- [ ] Rapid sync: stable phase coherence.
- [ ] Shape sweep 0-1: smooth timbre.
- [ ] V/Oct sweep: smooth pitch tracking.

---

### Pecto (biome)

**Module**: `Pecto` &middot; **Mnemonic**: `Pc` &middot; **Category**: `Resonator` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: density=8, comb size=0.1 s, feedback=0.5. Comb-filter resonance.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `V/Oct`, `size`, `density`, `feedback`, `xform`, `mix`.
- [ ] **V/Oct** (Pitch) -- pitch tracking (sitar/clarinet).
- [ ] **size** (GainBias, 0.001-2.0 s) -- comb delay time.
- [ ] **dens** (DensityControl, 1-24) -- tap density.
- [ ] **fdbk** (GainBias, 0-0.99) -- resonance amount.
- [ ] **xform** (TransformGateControl) -- randomize gate with 9 targets + depth.
- [ ] **mix** (MixControl) -- dry/wet + shift-toggle input/output/tanh.

#### 3. Sub-displays
- [ ] **dens** expand -> density fader (1-24), pattern selector (0-15), slope selector (4 shapes), resonator selector (raw / gtr / clar / sitr).
- [ ] **mix** expand -> mix / input level / output level / tanh amount.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] V/Oct 1 V = 1 octave.
- [ ] size / density / feedback / pattern / slope / resonator / mix / inputs / outputs / tanh modulate.
- [ ] xform gate fires randomization on selected target.

#### 7. Stereo routing
- [ ] Dual-instance; all params tied.
- [ ] Xform gate triggers both instances simultaneously.

#### 8. Save / load round-trip
- [ ] Quicksave preserves size / density / feedback / resonator / pattern / slope / mix / input/output/tanh.
- [ ] Delay memory resets.
- [ ] Xform seeds applied during deserialization.

#### 9. Edge cases
- [ ] Density=1: single tap.
- [ ] Density=24 + long size + sitar resonator: listen for **CPU spikes** (known issue).
- [ ] Feedback=0: no resonance.
- [ ] Feedback=0.95: long resonance; tanh needed to prevent runaway.
- [ ] Resonator switch: audible timbral shift.
- [ ] V/Oct step change: **zipper noise** on resize (known issue).
- [ ] Size CV step change: **zipper noise** (known issue).

---

### Transport (biome)

**Module**: `Transport` &middot; **Mnemonic**: `Tr` &middot; **Category**: `Clock / Timing` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: BPM=120, run=off. Silent until run gate.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `bpm`, `run`.
- [ ] **bpm** (GainBias, 1-300).
- [ ] **run** (Gate, toggle) -- rising edge toggles.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] BPM CV modulates tempo.
- [ ] Run toggle on rising edge.

#### 7. Stereo routing
Mono; clock on both outputs.

#### 8. Save / load round-trip
- [ ] Quicksave preserves BPM and run state; phase resets on load.

#### 9. Edge cases
- [ ] BPM=1: very slow.
- [ ] BPM=300: rapid output.
- [ ] Run toggle: phase resets.
- [ ] Rapid BPM CV: smooth tempo transition.

---

# catchall package

### Sfera (catchall)

**Module**: `Sfera` &middot; **Mnemonic**: `Sf` &middot; **Category**: `Filter / Morphing` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: config=0, paramX=0.5, paramY=0.5, cutoff=1000 Hz, spin=0.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `config`, `viz`, `paramX`, `paramY`, `cutoff`, `spin`, `level`.
- [ ] **config** (ModeSelector) -- 32 curated z-plane configs or "g" + generated.
- [ ] **viz** -- ferrofluid sphere with pole/zero positions, spin, lighting.
- [ ] **X / Y** (GainBias, 0-1) -- 2D morphing.
- [ ] **cutoff** (SferaCutoffControl, 20-20000 Hz) -- center frequency with Q coupling.
- [ ] **spin** (GainBias, -2..+2) -- rotation.
- [ ] **level** (GainBias, 0-2) -- output gain.

#### 3. Sub-displays
- [ ] **viz** live pole/zero ferrofluid display.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] V/Oct / config / paramX / paramY / cutoff / qScale / spin / level accept CV.

#### 7. Stereo routing
- [ ] Dual-instance; all params tied.
- [ ] Viz shows combined L+R audio energy.

#### 8. Save / load round-trip
- [ ] Quicksave preserves config / paramX / paramY / cutoff / qScale / spin / level.

#### 9. Edge cases
- [ ] Config=0 (BW LP>HP): paramX/Y at corners = distinct filter types.
- [ ] Cutoff=20 Hz or 20 kHz: band edges.
- [ ] High audio energy: viz expands/dims.
- [ ] Spin param: steady rotation + audio-driven turbulence.

---

### Lambda (catchall)

**Module**: `Lambda` &middot; **Mnemonic**: `Lm` &middot; **Category**: `Oscillator / Wavetable` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: seed=0, scan=0.0, f0=110 Hz, cutoff=1000 Hz, level=0.5.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `seed`, `viz`, `V/Oct`, `scan`, `f0`, `cutoff`, `level`.
- [ ] **seed** (ModeSelector, 0-999) -- selects wavetable variant.
- [ ] **viz** -- 256-sample waveform window display, audio-reactive.
- [ ] **V/Oct** (Pitch).
- [ ] **scan** (GainBias, 0-1) -- position in wavetable (morphing).
- [ ] **f0** (GainBias, 0.1-2000 Hz).
- [ ] **cutoff** (GainBias, 20-20000 Hz) -- filter bank cutoff.
- [ ] **level** (GainBias, 0-1).

#### 3. Sub-displays
- [ ] **viz** live 256-point waveform.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] seed / V/Oct / scan / fundamental / cutoff / level accept CV.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave preserves seed / scan / f0 / cutoff / level.

#### 9. Edge cases
- [ ] Seed wraps at 999.
- [ ] Scan=0 / 1: extremes of wavetable.
- [ ] Cutoff=20 Hz: silence or subsonic.
- [ ] Rapid seed changes: wavetable discontinuity (no smoothing).

---

### Flakes (catchall)

**Module**: `Flakes` &middot; **Mnemonic**: `Fk` &middot; **Category**: `Effect / Delay` &middot; **Stereo**: `dual-instance`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: freeze=off, depth=0.5, delay=0.25, warble=0.24, noise=0.1, dryWet=0.5.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `freeze`, `depth`, `delay`, `warble`, `noise`, `dryWet`.
- [ ] **freeze** (Gate, sustain) -- buffer snapshot while high.
- [ ] **depth** (GainBias, 0-1) -- warble LFO depth.
- [ ] **delay** (GainBias, 0-1) -- time scale up to 10 s.
- [ ] **warble** (GainBias, 0-1) -- LFO rate.
- [ ] **noise** (GainBias, 0-1) -- injection level.
- [ ] **d/w** (GainBias, 0-1) -- dry/wet crossfade.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] freeze gate / depth / delay / warble / noise / dryWet modulate.

#### 7. Stereo routing
- [ ] Dual-instance; all params tied.
- [ ] Freeze gate applies to both buffers simultaneously.

#### 8. Save / load round-trip
- [ ] Quicksave preserves depth / delay / warble / noise / dryWet; buffer resets; freeze state is live-only.

#### 9. Edge cases
- [ ] Delay=0: near-minimum (comb-like).
- [ ] Delay=1: ~10 s buffer.
- [ ] Warble=0: fixed delay.
- [ ] Warble=1: rapid flange.
- [ ] Freeze on: buffer loops.
- [ ] Noise=1: full white injection.

---

# scope package

### Scope / Scope 2x / Scope Stereo

**Module**: `Scope` (shared DSP) &middot; **Mnemonic**: `Sc` / `S2` / `SS` &middot; **Category**: `Visualization` &middot; **Stereo**: `native-stereo (passthrough)`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] **Scope**: 1-ply MiniScope of left channel.
- [ ] **Scope 2x**: 2-ply wider MiniScope.
- [ ] **Scope Stereo**: 1-ply each L and R displayed side-by-side.
- [ ] All pass audio unchanged.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `scope`.
- [ ] **scope** -- live oscilloscope view.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
No CV inputs; passthrough only.

#### 7. Stereo routing
- [ ] **Scope / Scope 2x**: watch left channel.
- [ ] **Scope Stereo**: dual L/R display.

#### 8. Save / load round-trip
- [ ] No parameters; passthrough identical.

#### 9. Edge cases
- [ ] Zero input: flat line.
- [ ] Clipping input: saturated display.
- [ ] High freq: aliasing visible (>12 kHz at 48 kHz).
- [ ] Mono insert of Scope Stereo: left channel only.

---

### Spectrogram (scope)

**Module**: `Spectrogram` &middot; **Mnemonic**: `Sg` &middot; **Category**: `Visualization` &middot; **Stereo**: `native-stereo (passthrough)`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: 256-point FFT, log frequency scale 20 Hz -- 24 kHz, peak hold + RMS gradient.
- [ ] Passthrough stereo.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `spectrum`.
- [ ] **spectrum** -- live FFT display.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
No CV inputs.

#### 7. Stereo routing
- [ ] Native stereo passthrough; FFT on combined stereo pair.

#### 8. Save / load round-trip
- [ ] No parameters; passthrough identical.

#### 9. Edge cases
- [ ] Zero input: flat spectrum.
- [ ] Pure sine: single peak.
- [ ] Broadband noise: filled spectrum.
- [ ] DC offset: energy at 0 Hz.
- [ ] Aliasing: peaks above 12 kHz.
- [ ] Peak hold: ~1 s decay.

---

# stolmine firmware package (TXo i2c)

### TXo CV (stolmine)

**Module**: `TXoCV` &middot; **Mnemonic**: `TCV` &middot; **Category**: `I2C Expander / CV Output` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error (requires TXo hardware or emulator i2c dispatcher).
- [ ] Default: port=0, gain=1.0, mode=Normal.
- [ ] Passthrough audio to output.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `port`, `gain`.
- [ ] **port** (GainBias, 0-3) -- TXo output selector.
- [ ] **gain** (GainBias, 0-2) -- output scaling.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
- [ ] **Mode** -- Normal (0-10 V) or V/Oct.

#### 6. CV input behavior
- [ ] input passthrough; port / gain modulate.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave preserves port / gain / mode.

#### 9. Edge cases
- [ ] No TXo: i2c silently fails.
- [ ] Port 0-3 routes to specific TXo CV.
- [ ] Gain=0: muted.
- [ ] Gain=2: may clip TXo input.
- [ ] Mode=V/Oct: exponential pitch scaling.

---

### TXo TR (stolmine)

**Module**: `TXoTR` &middot; **Mnemonic**: `TTR` &middot; **Category**: `I2C Expander / Trigger Output` &middot; **Stereo**: `mono`

#### 1. Insert / load
- [ ] Inserts without error.
- [ ] Default: port=0, threshold=0.1 V.
- [ ] Passthrough.
- [ ] Hot-plug: no pops.

#### 2. Top-level row (expanded view)
Order: `port`, `threshold`.
- [ ] **port** (GainBias, 0-3) -- TXo trigger selector.
- [ ] **thresh** (GainBias, 0-1) -- gate threshold.

#### 3. Sub-displays
Not applicable.

#### 4. Expansion views
Not applicable.

#### 5. Menu (onShowMenu)
No menu items.

#### 6. CV input behavior
- [ ] input analyzed for gate; port / threshold modulate.

#### 7. Stereo routing
Mono.

#### 8. Save / load round-trip
- [ ] Quicksave preserves port / threshold.

#### 9. Edge cases
- [ ] No TXo: silently fails.
- [ ] Threshold=0: always fires.
- [ ] Threshold=1: nearly never fires.
- [ ] Rapid gates: TXo hardware timing jitter.

---

## Global sanity pass

After walking every unit's section, run these checks once:

- [ ] Load a multi-unit patch with one of each new Larets / Helicase / Impasto instance; quicksave; close; reload. All units reinstate correctly.
- [ ] Swap between stereo and mono chain mid-session for each dual-instance unit (Canals, Discont, LatchFilter, Pecto, Flakes, Sfera, Filterbank, MultibandCompressor, MultibandSaturator, CodescanFilter); no crashes, channels route appropriately.
- [ ] Insert and remove every unit in the new catchall package without a reboot; no memory leak indicators on the emulator.
- [ ] With a patch playing audio, hot-unplug the SD card (hardware); unit state preserved, no crash.
- [ ] Confirm firmware hash matches build target (`./scripts/verify-build.sh`) before flagging a bug as hardware-specific.
