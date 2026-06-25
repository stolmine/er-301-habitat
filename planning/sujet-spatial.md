# Sujet — spatial envelopment: research foundation + novel design

Status: design exploration. Motivated by the user's critique that Sujet "lays on top
of its input rather than enveloping it in space, and tends toward noise."
Research basis: deep-research pass (19/25 claims verified 3-0), full report in the
session task output; key cited sources at the bottom.

---

## 1. What the literature establishes (verified)

- **Envelopment (LEV) = fluctuations in interaural time/intensity differences (ITD/IID)
  produced by DECORRELATED late lateral energy on opposite sides of the listener.**
  Below 2 kHz this is the dominant mechanism; fluctuations following the *ends* of sounds
  by **≥160 ms** are most effective → it lives in the **late/decaying** tail. (Griesinger)
- **ASW and LEV are separable and time-windowed:** EARLY (0–80 ms) decorrelation →
  Apparent Source Width; LATE (80–750 ms) decorrelation **+ adequate ENERGY** → envelopment.
  (Barron; Bradley & Soulodre; Beranek/Hidaka) → a per-bin engine can target onset for
  width and tail for envelopment, separately.
- **Target NEAR-ZERO inter-channel correlation, not anti-phase.** Width ∝ 1/|correlation|;
  widest at correlation ≈ 0; both +1 and −1 read narrow. (Kendall)
- **Decorrelation is FREQUENCY-DEPENDENT:** strongest below 1 kHz (peak ~400–800 Hz),
  weak above 3 kHz. Apply the most decorrelation in low/low-mid bins, taper HF.
  (Kurozumi & Ohgushi via Kendall; Okano/Beranek/Hidaka — the BQI uses 500/1k/2k Hz.)
- **Below ~300 Hz IACC saturates to 1** (decorrelation buys little), **yet low-frequency
  late lateral ENERGY is the great-hall differentiator** → below 300 Hz supply late
  low-freq *energy/level*, don't chase IACC. (Griesinger)
- **Decorrelation is the canonical TF-domain diffuse-field synthesis; DirAC** models each
  TF tile as direction + **diffuseness (0–1)**, splitting direct vs diffuse streams, the
  diffuse stream synthesized via decorrelation. Maps almost natively onto per-bin
  magnitude+phase. (Pulkki et al.)

**Refuted / cautions (3-0 / 1-2 kills):** "pure per-bin random-phase decorrelation is a
timbre-preserving free lunch" was REFUTED — **metallic coloration is a real risk**; use
*structured/smoothed* decorrelation, taper HF, verify by ear. Also refuted: "the decaying
tail can't carry decorrelation" — it **can and should**.

---

## 2. Diagnosis — why Sujet lays on top (sharpened by the research)

1. **Sujet conflates two things on one knob.** The only stereo cue is the per-channel
   phase PRNG, active only via **Diffuse (V)** — and Diffuse *simultaneously* randomizes
   phase *within* each channel (line → noise). So **inter-channel decorrelation (space)
   and intra-channel diffuseness (noise) are the same control** → you can't get space
   without turning the whole thing to noise. That is precisely "tends toward noise" +
   "lays on top."
2. **It's flat in frequency and static over the decay.** Envelopment wants
   frequency-weighted (strong low/low-mid) and **decay-evolving** (later = more
   decorrelated) inter-channel decorrelation, with adequate late energy. Sujet's is
   neither.
3. **Both channels share the same magnitude** (mono source) so the field can never spread.

---

## 3. The leap — a dedicated spatial engine, separate from Diffuse

A new **Space** axis that decorrelates L↔R *phase only* (magnitude identical between
channels), built from three mechanisms — the first two literature-grounded, the last two
the novel "jump":

1. **Inter-channel phase decorrelation (the Space lever).** Independent per-bin L/R phase
   offsets driving inter-channel correlation → 0 (NOT anti-phase). Width/envelopment with
   NO intra-channel noise. Decoupled from Diffuse.
2. **Frequency-weighted.** Strongest decorrelation in low/low-mid bins (peak ~400–800 Hz),
   tapering above ~3 kHz (where it helps less and smears transients). Keep low-freq late
   energy intact (don't over-damp lows) for the great-hall LEV cue.
3. **Coherence-collapses-over-decay (novel, per-bin).** Each bin's inter-channel coherence
   is HIGH at onset (focused/frontal) and DROPS as its magnitude decays
   (decorrelated/enveloping). A per-bin `cohEnv[k]` that rises on fresh input and falls
   over the decay → the reverb **arrives focused and opens into surrounding space as it
   decays**. This implements the early-ASW / late-LEV split AND the "≥160 ms after
   sound-end" fluctuation criterion *continuously, per bin* — something a time-domain
   reverb cannot do.
4. **Diffuseness-by-prominence (novel, inverted DirAC).** Strong/tonal bins (partials)
   stay COHERENT/frontal — the **source stays present**; weak/noise bins go fully
   DECORRELATED/diffuse — the **space wraps around**. Directly cures "lays on top":
   partials in front, diffuse field around. (Reuses the per-band reference idea from the
   gap-fill experiment — that machinery finds its real purpose here.)

**Artifact control:** structured/smoothed decorrelation phase (smooth across frequency and
slowly over time — NOT white per-bin-per-hop), HF taper, magnitude identical L/R.

---

## 4. Implementation sketch (in the SMD synth pass)

Currently synth does, per bin, both channels from the same `bloomState[k]` with
`φ = phaseScratch[k] + V·xi` (xi independent per channel). Add a **between-channel
decorrelation offset** `δ[k]`:

```
spaceAmt[k] = Space · freqWeight[k] · (1 − cohEnv[k]) · diffuseness[k]
              // freqWeight: strong <1kHz, taper >3kHz (precomputed table)
              // cohEnv[k]:  per-bin coherence env (high at onset, decays) → late = more space
              // diffuseness[k]: weak/noise bins → 1 (diffuse), strong partials → ~0 (frontal)
δ[k]        = spaceAmt[k] · smoothedDecorrPhase[k]   // structured per-bin phase, smoothed in freq/time
φ_L         = phaseScratch[k] + V·xiL + δ[k]
φ_R         = phaseScratch[k] + V·xiR − δ[k]          // opposite offset → inter-channel decorrelation toward 0
```

`Space=0` → δ=0 → bit-identical. Magnitude (`bloomState[k]`) identical L/R — only phase
differs between channels. Diffuse (V) stays a SEPARATE intra-channel control. New state:
`freqWeight[513]` (static), `cohEnv` per bin per... (shared or per-channel), a smoothed
per-bin decorrelation-phase field. One new **Space** param (the surface has room after the
Smear consolidation).

The two novel behaviors (collapse-over-decay, diffuseness-by-prominence) are what make this
*more than a width knob* — they're the spectral-domain spatial primitives a time-domain
reverb can't reach, and they target the source-stays-present / space-wraps-around quality
the user is after.

---

## Sources (verified primaries)
- Griesinger, "Objective Measures of Spaciousness and Envelopment"; "Spaciousness and
  Envelopment in Musical Acoustics."
- Barron; Bradley & Soulodre (late lateral energy = LEV), Applied Acoustics.
- Okano, Beranek & Hidaka, JASA 104(1) 1998 (BQI, 500/1k/2k Hz + Glow).
- Kendall, "The Decorrelation of Audio Signals and Its Impact on Spatial Imagery," CMJ
  19:4, 1995.
- Pulkki, Delikaris-Manias & Politis, *Parametric Time-Frequency Domain Spatial Audio*
  (Wiley/IEEE); Politis & Pulkki, DAFx-12 "Parametric Spatial Audio Effects"; Vilkamo &
  Pulkki (covariance-domain decorrelator-artifact minimization).
