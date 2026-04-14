# Microsound / electroacoustic DSP research

Notes from a 2026-04-14 research pass on Raster-Noton production methods and Peter Blasser's Plumbutter circuit design. Purpose: identify DSP kernels that map cleanly to ER-301 custom units for a future clicks-and-cuts / electroacoustic family in the habitat package.

URLs listed in sources sections are canonical starting points -- verify before citing in release notes.

## Raster-Noton techniques

Artists of interest: Alva Noto (Carsten Nicolai), Ryoji Ikeda, Frank Bretschneider, the Cyclo. project (Nicolai + Ikeda collaboration). Common tooling across the catalog: Max/MSP (all three), Reaktor (Nicolai), bespoke file-to-audio readers (Nicolai "Unitxt"). Ikeda historically runs a minimal laptop + interface setup.

### Unit candidates

**Sine-pulse percussion**
- Short-gated pure sine bursts (1-4 kHz) shaped with sub-10 ms AR envelopes. Optional DC-offset pre-envelope to emphasize the click transient from speaker cone inertia.
- Character: clinical pitched percussion, indistinguishable from test tones.
- DSP: sine osc + fast AR + pre-envelope DC kick. Trivial CPU.
- Lineage: Ikeda, Cyclo.

**DC-click impulse generator**
- Single-sample DC step or band-limited impulse. "The speaker cone movement IS the sound."
- DSP: trigger-to-impulse with DC-coupled output, configurable width (1 to ~32 samples) and polarity.
- Lineage: Ikeda `+/-`, `dataplex`.

**Generation-loss processor**
- Iterative lossy degradation modeled on photocopier-generation decay.
- DSP: bit-reducer + SR-reducer + feedback loop with tape-LPF, self-feeding mode.
- Caveat: overlaps somewhat with existing bitcrush units. The new territory is the iterative / self-feeding path.
- Lineage: Alva Noto `Xerrox`, `Unitxt`.

**Trigger-locked grain burst**
- Very short (5-30 ms) grains of noise or inharmonic material fired as percussion.
- Distinct from Clouds (continuous granular) and Flakes (looper/freeze): this is a trigger-locked one-shot player.
- DSP: live buffer + trigger-driven slice playback with randomized position/length.
- Lineage: Bretschneider `Rhythm` (2007), Max/MSP grain-scheduling patches.

### Patching-only (don't build a unit)

- Click + drone layering: two chains + crossover.
- Spectral masking / notch carving: existing filters in notch mode.
- Test-tone glissandi: sine osc + S/H grid.

### Sources

- Cyclo. project (cyclo.info, book ISBN 978-3-89955-330-0)
- *The Wire* issues 218 (2002, Ikeda), 263
- *de:bug* interview archive (Bretschneider ~2007, Nicolai re: Unitxt 2008)
- Native Instruments interviews re: Reaktor use (Nicolai ~2006)

## Ciat-Lonbarde Plumbutter

Peter Blasser's paper-circuits designs are public at ciat-lonbarde.net. Plumbutter is two mirrored channels, each with four subsystems: Rollz (rhythm), Gongs (tuned resonators), Lattice (cross-coupled LFOs), and Butter (electroacoustic stereo smear).

### Subsystem 1: Rollz (rhythm generator)

Signal path: a master slow ramp oscillator drives a ladder of pulse dividers. Each divider is a charge-bucket: cap charges via diode on each incoming pulse, when it crosses a comparator threshold it fires and resets. Several buckets in parallel with different thresholds produce rates that slip against each other instead of hard integer-dividing.

DSP kernel: bank of N leaky integrators with threshold-reset.

```
v[n] = a*v[n-1] + k*pulse_in;
if v >= thr: fire; v -= thr;  // or v = 0 for hard reset
```

Cross-coupling: each bucket's fire-pulse injects (weakly) into neighbours.

Gotchas for "organic" feel:
- Threshold jitter (per-bucket noise on `thr`).
- Leak term `a < 1`.
- Weak all-to-all injection matrix -- otherwise integer-ratio divides sound sterile.

### Subsystem 2: Gongs (tuned resonators)

Signal path: twin-T or multiple-feedback high-Q BPFs impulsed by Rollz pulses through a differentiator / RC pluck. Blasser's schematics show op-amp MFBPFs with a FET or diode switch injecting the pulse. Tuning is manual, typically non-equal intervals (pelog-ish).

DSP kernel: 2nd-order resonator (biquad BPF or state-variable, Q ~30-100) excited by a short impulse.

```
y[n] = 2*r*cos(w)*y[n-1] - r^2*y[n-2] + x[n];   // r just below 1
```

Excitation = one-sample or short burst scaled by trigger velocity.

Gotchas: real resonator has non-linear damping (op-amp saturation at large swing) which gives the "gong clip" character. Port with `tanh()` on the feedback sum, not a linear biquad.

### Subsystem 3: Lattice (cross-coupled LFO bank)

Signal path: grid of slow Schmitt-trigger/RC oscillators, each CV input sums contributions from 2-4 nearest neighbours via resistors. Produces weakly-coupled Kuramoto-style phase-locking with occasional phase slips -- "stability in chaos."

DSP kernel: N phase accumulators with per-oscillator base frequency + coupling term.

```
f_i_eff = f_i + sum_j K_ij * sin(phi_j - phi_i);
// or K_ij * sign(osc_j) to match analog comparator output
```

Output: squared LFOs that FM the Gongs and retrigger Rollz thresholds.

Gotchas:
- Coupling strength must sit just below mode-lock. Too weak: independent; too strong: everything syncs.
- Port with a small random walk on `f_i` to replace thermal drift, else it over-locks.

### Subsystem 4: Butter (stereo output smear)

Signal path: channel sum fed through AC-coupled op-amp allpass chain with feedback, plus a gentle LPF. Spring-reverb-adjacent electroacoustic smear, not a real tank. Stereo = L/R get different allpass taps, quasi-Haas widening.

DSP kernel: Schroeder-style mini-reverb.
- 4-8 first-order allpasses in series with feedback.
- Decay ~200-500 ms.
- 2nd-order LPF at 4-6 kHz.
- Two decorrelated tap sets for L/R.

Nothing physics-critical here; analog behavior translates faithfully.

### Shared kernels across Blasser designs

- Charge-bucket divider (Rollz) also appears in Tocante and Cocoquantus.
- Twin-T resonator (Gongs) is Sidrazzi Organ's tone generator (pelog-tuned).
- Cross-coupled LFO lattice is a simplified Deerhorn theremin-grid.
- Allpass smear recurs in Sidrazzi and Fourses output stages.

So investing in the four kernels above is also investment in future Ciat-Lonbarde-adjacent units.

### Sources to verify

- ciat-lonbarde.net/plumbutter/ (overview + paper-circuits PDF links)
- ciat-lonbarde.net/paper/ (canonical paper-circuits index -- rollz.pdf, gongs.pdf, lattice.pdf, boggs.pdf)
- ciat-lonbarde.net/sidrazzi/ /cocoquantus/ /deerhorn/ (shared-kernel comparisons)
- github.com/pblasser (sporadic board files)

## Synthesis: unit shortlist

Ranked by cost-to-build and musical return:

1. **Sine-pulse + DC-click generator** -- combines Raster-Noton's two cheapest kernels into one percussion unit. Trivial DSP, huge aesthetic return.
2. **Pulse-excited resonator bank** -- Plumbutter's Gongs kernel plus `tanh` non-linear damping. 4-8 tuned resonators, trigger inlet per bank or CV-indexed.
3. **Charge-bucket rhythm generator** -- Plumbutter's Rollz kernel. N leaky integrators with threshold-reset + weak cross-coupling + threshold jitter. Produces polyrhythm from simple counter logic.
4. **Lattice oscillator** -- Plumbutter's Lattice kernel. 6-8 coupled phase accumulators, outputs the sum plus modulated resonance hits. Sits-below-mode-lock coupling parameter is the main dial.
5. **Generation-loss processor** -- Alva Noto's iterative degradation loop. Partial overlap with existing bitcrush; new territory is the self-feeding iterative path.
6. **Trigger-locked grain burst** -- Bretschneider's trigger-driven grain scheduler. Distinct from Clouds and Flakes because it's a one-shot player.
7. **Schroeder allpass smear** -- Plumbutter Butter kernel. Might belong as a utility in the scope or catchall package rather than a standalone.

Items 1-4 together cover both the Raster-Noton *and* Plumbutter aesthetics, share infrastructure where sensible (pulse generators feed resonators feed allpass smear), and open the door to Sidrazzi / Cocoquantus / Deerhorn reimaginings later via the shared kernels.

Prototype order: start with (2) since it's the cheapest standalone audio-producing unit and validates the non-linear biquad approach we'd use in all later work. Then (3) to drive it rhythmically. Then (4) to add the "organic lock" behavior. (1) and (5) can slot in anywhere.
