# Sujet — depth, distance, and scale: psychoacoustics primer + mechanism

Status: **research foundation + design recommendation**. No code changed.
Companion docs: `sujet-design.md`, `sujet-spatial.md`.

---

## 1. Why this matters

Sujet's existing controls give decay (Decay/Damp), noise character (Diffuse/Spray),
temporal envelope (Smear), infinite-hold (Freeze), and lateral width/envelopment
(Space). What they do not give is a controlled sense of the source being NEAR or FAR
within the space, or of the space itself being INTIMATE or VAST. The user correctly
observes that Freeze is the most effective "size" knob because a dense infinite field
reads perceptually as a vast space — but that conflates scale-as-size with
density-as-infinity. A dedicated Distance/Scale axis is the gap.

---

## 2. Distance perception — the cue hierarchy

### 2.1 Primary cue: direct-to-reverberant energy ratio (D/R, DRR)

The dominant ecological cue for distance inside a room is the ratio of direct-sound
energy to reverberant-field energy. As a source moves away from the listener, the
direct sound drops ~6 dB per doubling of distance (inverse-square law in free field;
~4 dB per doubling in a reverberant room due to partial compensation from
reflections), while the diffuse reverberant field remains nearly constant (~1 dB
per doubling). The DRR therefore falls monotonically with distance, providing a
reliable ecological cue from roughly 0.5 m to ~15 m.

**Quantitative sensitivity:** JNDs for DRR discrimination are 2–3 dB when DRR is
near 0 dB (critical distance, where direct = reverberant energy), widening to
6–8 dB at extreme positive or negative DRR values. A 5 dB change in DRR
corresponds roughly to a distance change factor of ~2.5.
(Zahorik 2002a/b, JASA; Zahorik, Brungart & Bronkhorst 2005, JASA review;
Bronkhorst & Houtgast 1999, Nature — all cited below.)

**Critical distance formula:**

```
Dc = 0.057 · sqrt(Q · V / RT60)      [meters]
```

where Q is source directivity, V is room volume in m³, RT60 in seconds. At Dc the
DRR = 0 dB; sources perceived at DRR >> 0 dB feel close (nearer than Dc); sources
at DRR << 0 dB feel distant (beyond Dc). Griesinger's concert-hall research shows
DRR ≈ −2 dB to 0 dB for the clarity/distance boundary where the direct sound first
becomes perceptually separable from the reverberant field.

**Perceptual compression:** Perceived distance follows a compressive power law:
`r' = k · r^a` where a < 1, meaning listeners underestimate far distances and
overestimate near ones. The DRR provides calibrated relative distance information
even when absolute distance is uncertain.

### 2.2 Loudness / level

Level functions as a distance cue primarily in anechoic conditions or at close range
(<1 m). In reverberant rooms it is confounded by source-level uncertainty (you do
not know how loud the source "should" be). It is a useful but ambiguous secondary
cue. Discrimination threshold for level is ~0.4 dB for broadband noise.
(PMC4744263 review; Zahorik et al. 2005)

### 2.3 Spectral change — air absorption

Above roughly 15 m of propagation path, atmospheric absorption progressively
removes high-frequency energy. The absorption coefficient α (dB/m) increases
strongly with frequency; at 20°C, 50% RH, typical values are approximately:

```
1 kHz  → ~0.005 dB/m   (negligible indoors)
4 kHz  → ~0.039 dB/m   (~4 dB per 100 m)
8 kHz  → ~0.143 dB/m   (~14 dB per 100 m)
16 kHz → ~0.40 dB/m    (~40 dB per 100 m)
```

(ISO 9613-1; Harris JASA 1966; acoustics-engineering.com air-absorption study)

At indoor distances (1–10 m) absorption is perceptually negligible for source
distance cues. However it becomes significant for the REVERBERANT TAIL: sound
bouncing around a 20 m hall travels hundreds of meters before dying, accumulating
substantial HF loss. This is the physical basis for Sujet's Damp parameter and for
why "distance-correlated HF damping" feels perceptually correct even if the
underlying physics is only strictly active at outdoor scales.
**For Sujet:** rolling off HF in proportion to a Distance parameter is a
perceptually valid (if somewhat stylized) cue, especially at high Distance values.

### 2.4 Binaural vs. monaural cues

Interaural level differences (ILD) add distance information at very close range
(<1 m, the acoustic near-field) because ILD varies with distance as well as azimuth.
ITD is approximately independent of distance. Both binaural cues are irrelevant for
stereo reverb (Sujet is not binaural rendering). The relevant monaural/two-channel
cues are DRR, loudness, and HF spectral tilt — all of which Sujet can address.
Binaural coherence cues (IACC) used for envelopment are handled by Space/Diffuse
as already designed.

---

## 3. Room SIZE / SCALE perception

### 3.1 ITDG — Initial Time Delay Gap

Beranek (1962, 1996, 2004) established the Initial Time Delay Gap — the time between
the direct sound and the first significant lateral reflection — as the primary cue
for perceived room SIZE or "intimacy". A short ITDG (≤20 ms) feels intimate and
small; a long ITDG (>50 ms) feels arena-like and remote. Physically, ITDG scales
with room dimensions: a 20 m wide hall produces a first sidewall reflection at
~58 ms; a 5 m room produces ~14 ms. (Beranek MDPI 2019 discussion; JASA
"Subjective rank-orderings" 1996)

**In a reverb processor:** predelay is a direct controllable ITDG. Sujet's Predelay
is currently inert — it exists as declared state but does nothing. Activating it is
the single highest-impact action for scale perception.

### 3.2 RT60 and Sabine's formula

Wallace Clement Sabine (1900) established:

```
RT60 = 0.161 · V / A       [seconds, metric]
where A = Σ(Si · αi)       [m², total absorption area]
```

RT60 scales with room volume directly and inversely with absorption. A small
absorptive room (bedroom, ~40 m³, α≈0.3) gives RT60 ≈ 0.5 s; a cathedral
(~50,000 m³) gives RT60 ≈ 7–10 s. Listeners use RT60 as a strong scale cue but
only when it is consistent with the other cues — a very long decay in a very dry
mix reads as a plate or chamber, not a huge room, without adequate density.

### 3.3 Echo density and density build-up time

The mixing time Tm is the point at which the impulse response becomes perceptually
diffuse (indistinct individual reflections). Tm scales with room volume; large rooms
have late, sparse early reflections that build slowly into a dense tail. Small rooms
reach full density rapidly. Density is measured in reflections per second; at
~1000 reflections/s the perceptual threshold for diffuseness is crossed
(Kuttruff room acoustics; FDN literature). Below this threshold, distinct echoes
are audible and the space reads as small or metallic.

For Sujet's STFT magnitude accumulator: spectral density is controlled by Diffuse
(intra-channel phase randomization). The temporal density build-up profile (slow
onset of diffuse texture vs. instant density) is not currently shaped — there is
no early-reflection analog. This is a gap for a Scale axis to address.

### 3.4 Low-frequency reverberation and "reverberance"

Griesinger's research on listener perceptions in concert halls shows that
**low-frequency late reverberant energy is the primary carrier of "reverberance"
and the sense of vast space**. The sujet-spatial.md research confirms that below
~300 Hz, inter-channel coherence is already near 1.0 so decorrelation buys little
lateral width, but supplying abundant late low-freq energy IS the distinguishing
cue of great halls. This is why Freeze reads as "huge" — it keeps full-spectrum
energy alive indefinitely, including the low end that physically carries scale.

**Key practical insight:** room size perception is dominated by ITDG + RT60 together,
but the emotional sense of VASTNESS is carried disproportionately by low-frequency
late energy. The diffuseness-by-prominence mechanism already in Space planning will
preserve tonal (low-mid) content front-and-center while releasing diffuse energy
into the field — a direct enabler of this.

---

## 4. DEPTH — front-to-back layering

### 4.1 The precedence / Haas effect and predelay

When a reflected copy arrives within ~40 ms of the direct sound, the auditory system
fuses them into a single percept dominated by the direct sound (precedence effect,
Haas 1951). The direct sound remains "in front" and the space sits behind it.
When the gap is longer than ~40 ms the reflection becomes perceptible as a distinct
echo and begins to feel spatially behind and separate.

Predelay exploits this: a 0–20 ms gap keeps the reverberant field fused behind the
direct sound, reinforcing depth. Longer predelay (20–80 ms) increases the apparent
distance to the room's walls, creating a sense of the source being far inside a
large space (Lexicon PCM: "shorter predelay = smaller space, longer = larger"). Very
long predelay (>80 ms) detaches the reverb perceptually and can create a distinct
slapback+tail character.

### 4.2 D/R and depth — the layering mechanism

Depth (front-to-back layering within a mix) is perceptually different from lateral
width. Width is achieved via inter-channel decorrelation (Space). Depth is achieved
by the ratio of the focused direct sound to the diffuse reverberant field:

- High D/R (dry wet mix skewed dry, reverb low): source appears close and forward,
  space is ambient and behind
- Low D/R (wet dominant): source appears recessed, blended into the field
- D/R ≈ 0 dB with no direct signal at all: source fully embedded in the reverberant
  field = maximum perceived distance

Griesinger's formulation: "as D/R gets smaller the source is perceived to be farther
away; the spatial nature of the reverberation adds the perception of depth, where
the reverberant energy arrives from many directions." He warns: "you can make a
recording that sounds both too close and too far away at the same time" if late
energy is high but early energy is absent — the precedence window anchor is missing.

### 4.3 Diffuseness-by-prominence as a depth separator

The "diffuseness-by-prominence" idea already documented in sujet-spatial.md has a
direct depth interpretation beyond its spatial/envelopment function: strong bins
(high magnitude) have high DRR (their direct component dominates), while weak/diffuse
bins have low DRR (they are dominated by the reverberant accumulation). The ear
reads this as: tonal partials feel NEAR and FORWARD; the residual noise floor feels
FAR and SURROUNDING. This is front-to-back depth implemented in the spectral domain.

No time-domain reverb can do this because it works on the whole signal; in SMD,
per-bin g(k) and prominence weighting makes it natural.

### 4.4 Lateral width vs. depth

These are perceptually orthogonal axes:
- Width (ASW/LEV): controlled by inter-channel decorrelation, frequency-weighted
  below 1 kHz, strongest in the late tail → Space
- Depth: controlled by DRR, ITDG/predelay, HF spectral tilt → proposed Distance
The error in naive reverb design is treating wet/dry mix alone as both. The Space
parameter already separates width correctly; Distance needs to be a separate axis.

---

## 5. How reverbs implement distance/size/depth

### 5.1 Standard mechanisms

**Predelay = ITDG.** Every serious algorithmic reverb exposes predelay (Lexicon 224,
480L, Valhalla Room, Eventide reverbs). Subjectively: short predelay → intimate
small room; long predelay → large distant space. Ranges of 0–100 ms are typical.

**Mix (dry/wet) = D/R control.** The mix is the broadest distance control. Fully wet
= fully distant. However, wet/dry alone is a blunt instrument — it cannot independently
control where-in-the-room-am-I vs how-large-is-the-room.

**Early/late balance = depth structure.** Lexicon's "Depth" parameter (PCM Native)
"manipulates reverb build-up to change the subjective distance between source and
listener." Concretely: boosting early reflections vs late tail pushes the source
forward (more ITDG-bound energy); reducing early and boosting late pushes back.

**Size = delay lengths + RT60.** Valhalla Room's Size control scales all internal
delay lengths, changing both ITDG implicitly and mixing time. A larger Size →
longer delays → later ITDG → larger-feeling space, and (assuming same Decay) higher
RT60/V ratio feeling. Size and Decay together suggest specific room volumes via the
Sabine relationship: a large Size with long Decay = cathedral; large Size with short
Decay = large open outdoor space.

**HF damping with distance.** Algorithms like Valhalla model per-shelf or per-band
absorption increasing with apparent distance. Physically analogous to air absorption
and surface absorption accumulation over longer path lengths.

### 5.2 What Sujet already has vs. what is missing

Already present:
- Decay/Damp: RT60 and HF tilt (analogous to RT60 + frequency-dependent damping)
- Diffuse: intra-channel noise (increases density, reduces clarity → push back)
- Space: inter-channel decorrelation (width/envelopment)
- Freeze: DRR → 0 (infinite reverberant field → maximum scale effect)
- Mix: broadband D/R

Missing:
- Predelay (declared, inert) — no ITDG
- No coordinated DRR sculpting (just wet level, no front/back structure)
- No explicit density build-up shaping (no early-reflection analog)
- No distance-correlated HF rolloff independent of Damp

---

## 6. Recommended Distance/Scale mechanism for Sujet

### 6.1 Design principle

A single Distance parameter should simultaneously embody the three physically and
perceptually coherent transformations that together shift a source from NEAR to FAR:

1. **DRR decrease**: the reverberant field rises relative to the direct signal
2. **ITDG increase**: the gap before the reverberant field grows (predelay)
3. **HF spectral softening of the reverberant field**: accumulated high-path-length
   air/surface absorption

These three are correlated in nature and should move together on one axis.

### 6.2 Concrete mechanism

**Parameter:** `Distance` ∈ [0, 1] (surface label); maps to three sub-parameters:

#### Sub-parameter A: wet-level bias (DRR sculpting)

A small gain applied to the reverberant output that is SEPARATE from the existing
Mix (which stays as user-controllable dry/wet). Distance boosts the wet-out magnitude
by a factor `wetBias`:

```
wetBias(d) = 10^( d · W_max / 20 )       // e.g. W_max = 6 dB at d=1
```

This is NOT the same as turning up Mix: Mix also reduces the dry signal (or increases
it). Distance keeps the dry path intact and raises the reverberant floor. The
perceptual effect is the source becoming more embedded in the field without going
quiet. The user controls the overall dry/wet with Mix; Distance controls the
source-within-space placement independently.

**Alternative formulation** more faithful to DRR theory: express Distance as a
shift in the apparent DRR:

```
DRR_shift(d) = −d · 12    // dB; 0 dB at d=0 (near), −12 dB at d=1 (far)
wetBias_linear = 10^(−DRR_shift/20) = 10^(d · 0.6)
```

At d=0.5 this produces ~6 dB boost to wet — perceptually roughly doubling the
apparent reverberant-field level relative to the incoming direct signal.

#### Sub-parameter B: predelay activation (ITDG)

Map Distance to predelay in the inert-but-declared Predelay parameter:

```
predelay_ms(d) = d^2 · P_max_ms        // squared for perceptual linearity
                                        // P_max_ms ≈ 80 ms (cathedral scale)
```

The squared mapping allocates resolution at small values (intimate rooms, short ITDG)
where perceptual sensitivity is highest. Values:
- d=0: 0 ms (no gap, maximum intimacy)
- d=0.3: ~7 ms (small room)
- d=0.6: ~29 ms (medium hall)
- d=1.0: 80 ms (large space)

Implementation uses the existing declared Predelay state. If Predelay is already
exposed as a user-visible control, Distance modulates it as a bias (Distance +
user-set predelay). If Predelay is promoted from inert to live, Distance can drive
it directly.

The SMD engine requires the predelay to sit BEFORE the magnitude accumulator
(it delays the dry input to the STFT; the reverberant output is then offset). A
simple circular buffer on the dry input path at 48 kHz, maximum size of 4096 samples
(~85 ms), is sufficient. No STFT changes required.

#### Sub-parameter C: distance-correlated HF softening

Apply a frequency-dependent attenuation to the magnitude accumulator output (or
equivalently to g(k)) that scales with Distance. Model air-absorption-style rolloff
as a per-bin gain:

```
airAbs(k, d) = exp(−α_eff(k) · d · D_scale)
```

where `α_eff(k)` is a precomputed frequency envelope (normalized 0→1 across bins,
rising steeply above ~4 kHz), and `D_scale` sets the aesthetic strength. In
practice a simple 1-pole lowpass applied to the output magnitude spectrum, with
cutoff frequency dropping with Distance, is equivalent and cheaper:

```
fc_norm(d) = fc_max · (1 − d · 0.7)      // fc_max ≈ normalized 0.9 (near Nyquist)
                                           // at d=1: fc ≈ 0.27 · Nyquist ≈ 6.5 kHz
```

This is applied to `M_out(u,k)` after the accumulator, not to g(k) — so it shapes
the output spectrum without altering the decay dynamics. It also complements Damp
(which shapes the decay rate per bin) without replacing it.

#### Combined at each output sample

```
// predelay: input to STFT is delayed by predelay_samples(Distance)
// per-bin output:
M_final(u,k) = M_out(u,k) · airAbs(k, Distance) · wetBias(Distance)
Y(u,k)       = M_final(u,k) · e^{j·φ_out(u,k)}
```

The dry path is not touched by Distance (only by Mix). The user hears: pushing
Distance up causes the reverberant field to deepen (DRR drop), the reverberant onset
to be pushed back in time (ITDG), and the reverberant tail to soften at high
frequencies. All three are perceptually consistent with moving the source away from
the listener inside a space.

### 6.3 Interaction with existing parameters

**Decay/Damp:** orthogonal. Decay sets RT60 (how long the space rings); Damp sets the
HF decay rate differential. Distance sets how far inside the space the source sits.
A short Decay + high Distance reads as: far inside a dry room (anechoic at distance).
A long Decay + high Distance: far inside a cathedral. Both are valid and distinct.

**Space (inter-channel decorrelation):** orthogonal. Space gives lateral
envelopment. Distance gives depth. They compound: Space=high + Distance=high →
deeply immersed in a surrounding field = maximum envelopment/immersion.

**Diffuse (intra-channel phase noise):** partially overlapping in perception but
mechanistically separate. Diffuse makes the spectrum noisier (less tonal). Distance
makes the source sit further back without destroying tonality. Diffuse + Distance
together: diffuse texture of a far, large space.

**Freeze:** Distance applies before Freeze conceptually — Freeze with high Distance
gives infinite sustain of a distant, large-space character. Works well.

**Mix:** remains the master dry/wet. Distance operates within the wet signal. Setting
Mix=0.5 + Distance=0 → near source, dry/wet balanced. Mix=0.5 + Distance=1 → same
dry level but the wet is boosted and pushed back in time = source recedes without
disappearing. This is the key distinction from just turning up Mix: Distance moves
the SOURCE within the SPACE; Mix controls how much SPACE is audible at all.

### 6.4 Novelty and the spectral leap

The three sub-parameters above are established technique; the Distance axis
coordinates them reliably. The spectral-domain extensions that Sujet's per-bin engine
uniquely enables, and which no time-domain reverb can do directly:

**[ESTABLISHED] — well-grounded in literature:**

- Per-bin air absorption (airAbs(k,d)): equivalent to frequency-dependent output
  filtering, a standard technique in algorithmic reverbs.
- ITDG via predelay: completely standard.
- wetBias as D/R shift: standard mix engineering, just placed here as a named axis.

**[NOVEL — per-bin Distance] — exotic, not in literature:**

- **Per-frequency distance:** different bins at different apparent distances, each
  with its own DRR sculpting and airAbs factor. Example: low bins (200 Hz) feel
  closer (less HF-absorbed, stronger in the mix) while high bins (8 kHz) feel far
  (more absorbed). This gives the reverb a frequency-stratified depth that reads as
  spectral richness rather than just distance. Implementation: `airAbs(k,d)` already
  does this implicitly since it is frequency-dependent; exposing a per-bin Distance
  map would go further.
- **Depth that evolves over the decay:** per-bin wetBias could be tied to
  `cohEnv[k]` from the Space design — bins feel close at onset (high DRR) and far as
  they decay (low DRR, fully reverberant). This implements the precedence-window
  effect continuously per bin: every spectral component starts focused and recedes as
  it decays. Requires coupling Distance to the cohEnv tracking already planned for Space.
- **Diffuseness-by-prominence as a per-bin distance map:** the D/R for each bin is
  inversely proportional to its prominence relative to the spectral floor. Strong
  partials: high per-bin DRR → NEAR/FRONT. Weak noise bins: low per-bin DRR → FAR/BACK.
  The result is a continuous depth map derived from the signal itself — spectral
  content and spatial placement become one and the same property. This is the
  "diffuseness-by-prominence" idea from sujet-spatial.md generalized from the
  width/envelopment domain to the depth/distance domain.

---

## 7. Suggested build order for Distance/Scale

The three sub-parameters have independent implementation paths and increasing
complexity:

**Phase A — Predelay (activate the inert parameter):**
Single circular buffer on the dry-input path before the STFT. Zero-dependency
change (no STFT/accumulator changes). Drive it from a Distance parameter OR expose
it as a standalone Predelay control first; either is independently useful.
Complexity: minimal. This is the single highest-leverage action for scale perception.

**Phase B — HF softening (airAbs):**
Per-bin output gain table, precomputed from a frequency envelope, scaled by Distance.
Applied to M_out before IFFT. Cost: one multiply per bin per frame. Pairs with
existing Damp (which modifies g(k); this modifies output). Essentially free
on Cortex-A72.

**Phase C — wetBias (DRR sculpting):**
A single scalar gain on the wet output before summing with dry. Can live in the
existing Mix path as a second gain stage. Requires no FFT-domain changes.

**Phase D — cohEnv coupling (depth-over-decay, novel):**
Ties Distance behavior to the cohEnv[k] tracking being built for Space. Each bin
starts with high effective DRR (near) and transitions to low effective DRR (far)
as it decays. Requires the cohEnv state from Space Phase C (sujet-spatial.md §4).
This should be built after Space's cohEnv infrastructure is in place.

**Phase E — diffuseness-by-prominence depth map (novel, exotic):**
Derives per-bin effective Distance from prominence tracking (spectral floor
comparison). The diffuseness-by-prominence machinery from Space applies here too —
it is the same per-bin prominence measurement with a different downstream use. Can
be developed jointly. This is the most novel and most exotic step; flag as
experimental.

---

## 8. Distinguishing Distance from "just turning up Mix"

This is the key design test. Mix scales the reverberant output vs. dry. Distance
specifically shifts the source's apparent position WITHIN the reverberant space
without requiring the dry signal to disappear:

| Action            | Direct signal | Rev level | Rev onset | HF texture |
|-------------------|--------------|-----------|-----------|------------|
| Mix up            | down         | up        | unchanged | unchanged  |
| Distance up       | unchanged    | up (+bias)| later (ITDG) | softer (airAbs) |
| Mix + Distance up | down         | up (more) | later     | softer     |

At Mix=100% wet (no dry), Distance still operates meaningfully by shaping the
reverberant field's internal structure (ITDG, airAbs, wetBias within wet). This
is the use case where the reverb is a texture-only source (e.g., Freeze with no
dry) and Distance controls the SIZE/SCALE of that texture-space.

At Mix=50%, Distance controls source placement: low Distance = dry sits in front of
a present reverb; high Distance = reverb surrounds and recedes behind the source,
which itself feels farther in the field.

---

## 9. Sources

- Zahorik, P. (2002a). "Assessing auditory distance perception using virtual
  acoustics." JASA 111(4). [https://pubmed.ncbi.nlm.nih.gov/12002867/]
- Zahorik, P. (2002b). "Direct-to-reverberant energy ratio sensitivity." JASA.
  [https://www.academia.edu/98833314/Direct_to_reverberant_energy_ratio_sensitivity]
- Zahorik, P., Brungart, D., & Bronkhorst, A. (2005). "Auditory distance perception
  in humans: A summary of past and present research." Acta Acustica.
  [https://www.researchgate.net/publication/229068125]
- Bronkhorst, A. W. & Houtgast, T. (1999). "Auditory distance perception in rooms."
  Nature 397. [https://pubmed.ncbi.nlm.nih.gov/10028966/]
- Griesinger, D. (2009). "The importance of the direct to reverberant ratio in the
  perception of distance, localization, clarity, and envelopment, Parts 1 & 2."
  JASA / ASA. [https://pubs.aip.org/asa/jasa/article/125/4_Supplement/2483/714750/]
- Griesinger, D. (Lexicon). Sound On Sound interview: "Creating Reverb Algorithms
  For Surround Sound." [https://www.soundonsound.com/people/david-griesinger-lexicon-creating-reverb-algorithms-surround-sound]
- Griesinger, D. "How Loud Is My Reverberation." Sussex lecture.
  [http://ecousticsystems.com/sites/default/files/library/howloud.pdf]
- Beranek, L. (1962/1996/2004). "Concert Halls and Opera Houses." Discussion of
  ITDG and intimacy: [https://www.mdpi.com/2624-599X/1/3/32]
- Sabine, W. C. (1900). "Reverberation." The American Architect. Formula reference:
  [https://appliedcalculator.com/physics/acoustics/rt60-reverberation-time-calculator/]
- ISO 9613-1:1993. "Acoustics — Attenuation of sound during propagation outdoors —
  Part 1." Air absorption coefficients. [https://www.iso.org/standard/17426.html]
- PMC4744263 — Kolarik et al. (2016). "Auditory distance perception in humans: a
  review of cues, development, neuronal bases, and effects of sensory loss."
  Frontiers in Psychology. [https://pmc.ncbi.nlm.nih.gov/articles/PMC4744263/]
- PMC5479918 — Abe et al. (2017). "Sound Spectrum Influences Auditory Distance
  Perception of Sound Sources in a Room." Frontiers in Psychology.
  [https://pmc.ncbi.nlm.nih.gov/articles/PMC5479918/]
- PMC5137703 — Traer & McDermott (2016). "Statistics of natural reverberation
  enable perceptual separation of sound and space." PNAS.
  [https://pmc.ncbi.nlm.nih.gov/articles/PMC5137703/]
- Vickers, M. et al. (2006). "Frequency Domain Artificial Reverberation using
  Spectral Magnitude Decay." AES 121st Convention.
  [https://www.academia.edu/64667585/Frequency_Domain_Artificial_Reverberation_using_Spectral_Magnitude_decay]
  [https://ccrma.stanford.edu/~larrywu/files/AES_121.pdf]
- Valhalladsp.com — Size and Mix controls documentation for ValhallaRoom.
  [https://valhalladsp.com/2011/05/02/valhallaroom-the-high-level-sliders/]
- Critical distance formula: [https://strutt.arup.com/help/Electroacoustics/DCritical.htm]
- Precedence effect: [https://en.wikipedia.org/wiki/Precedence_effect]
