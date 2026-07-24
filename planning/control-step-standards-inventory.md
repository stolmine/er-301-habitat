# Control-Step / Dial-Map Standardization Inventory

Cross-references every dial map in the habitat unit collection against the firmware's
built-in standard maps, so we can adopt the built-in standards where habitat diverges.

- Ledger item: `control-step-standards` (this doc is its inventory deliverable). Subset already tracked: `mix-control-standards`.
- Source of truth: `/home/bram/repos/er-301-stolmine/xroot/Encoder.lua`, the `dialMaps` table (lines 153-194) plus the map constructors (lines 11-151).
- Scope: prioritized the PUBLISHED packages (biome, catchall, mi, peaks, scope, spreadsheet). Unpublished packages (anamnesis, house, kryos, porcelain, stolmine, zaum) surveyed briefly at the end.

---

## 1. The framework standard (verified from Encoder.lua)

`linMap(min, max, superCoarse, COARSE, fine, superFine)` builds a `LinearDialMap` and calls
`setSteps(superCoarse, COARSE, fine, superFine)`. **The encoder's default turn uses the 2nd
(COARSE) arg.** Key named maps (`Encoder.getMap("name")`):

| name | definition | COARSE step |
|---|---|---|
| `default` / `[-1,1]` | `linMap(-1,1, 0.1, 0.01, 0.001, 0.0001)` | **0.01** |
| `[0,1]` / `unit` | `linMap(0,1, 0.1, 0.01, 0.001, 0.0001)` | **0.01** |
| `[0,2]` | `linMap(0,2, 0.1, 0.01, 0.001, 0.0001)` | 0.01 |
| `[0,0.25]` / `[0,0.1]` | `linMap(..., 0.05, 0.01, 0.001, 0.0001)` | 0.01 |
| `[0,10]` | `linMap(0,10, 1, 0.1, 0.01, 0.001)` | 0.1 |
| `[0,36]` `[-20,20]` `[-10,10]`(=`gain`) `[-5,5]`(=`bias`) `[1,32]` | `linMap(..., 1, 0.1, 0.01, 0.001)` | 0.1 |
| `percent` | `linMap(0,100, 10, 1, 0.1, 0.1)` | 1 |
| `speed` | `linMap(-3,3, 1, 0.1, 0.01, 0.001)` | 0.1 |
| `cents` | `linMap(-3600,3600, 1200, 100, 10, 1)` | 100 |
| `tempo` | `linMap(1,501, 10, 1, 0.1, 0.01)` | 1 |
| `volume` | `decibelMap(-60,12)` = `LinearDialMap(-60,12)`, `setZero(-160)`, `setSteps(6,1,0.1,0.01)` | 1 dB |
| `gain36dB` / `decibel36` | `decibelMap(0,36)` / `decibelMap(-36,36)` | 1 dB |
| `feedback` | `LinearDialMap(-36,0)`, `setZero(-160)`, `setSteps(6,1,0.1,0.01)` (**dB**) | 1 dB |
| `filterFreq` | `octaveMapWithZero(-5,5, 440, 1/12)` (LUT, octave) | octave |
| `oscFreq` | `octaveMapWithZero(-10,10, 27.5, 1/12)` (LUT, octave) | octave |
| `clockFreq` | `octaveMapWithZero(-5,5, 2, 1)` (LUT, octave) | octave |
| `slewTimes` | `timeOctaveMap(0.003,1000)` (LUT, octave time) | octave |
| `int[...]` | `intMap(min,max)` = `setSteps(5, 1, 0.25, 0.25)` + `setRounding(1)` | 1 |
| `ADSR` | `bilinearMap(0,0.99,99, 1,10,99)` | LUT |
| `rate` / `timeFactors` / `speedFactors` | LUT enumerations | LUT |

**The rule the user cares about most:** any normalized 0..1 or -1..1 control should have
COARSE step **0.01** (superCoarse 0.1, fine 0.001, superFine 0.0001).

---

## 2. Systemic divergences (summary)

Divergence codes used throughout:

- **D1** = normalized (0..1 / -1..1) control built with COARSE **0.1** instead of 0.01. Dial moves 10x too fast on the default turn. **The highest-value fix.**
- **D2** = normalized COARSE 0.01 correct, but **superFine 0.001 instead of 0.0001** (minor; one detent too coarse only in the deepest fine mode).
- **D3** = **linear-Hz** map where the framework standard is an OCTAVE LUT (`filterFreq` / `oscFreq` / `clockFreq`).
- **D4** = **feedback as linear 0..1 / 0..0.95**, where the framework `feedback` map is dB (`LinearDialMap(-36,0)`, `setZero(-160)`).
- **D5** = **level/volume as linear** 0..1 / 0..N, where framework `volume` / `gain36dB` are decibel maps.
- **Ddup** = private `LinearDialMap` that exactly (or near-exactly) duplicates a named framework map; should use `Encoder.getMap(...)`.
- **Dint** = integer/index map uses `(1,1,1,1)` or `(4,1,0.25,0.25)` vs framework `intMap = (5,1,0.25,0.25)+setRounding(1)`. Rounding is on, so cosmetic-only (superCoarse jump size). Treated OK.
- **Dmix** = same parameter reads at two different step granularities between its main expansion fader and its shift-sub readout.

### The dominant issue: the `floatMap` split (D1)

Almost every habitat unit defines a file-local `local function floatMap(min,max)`. Two
incompatible definitions have been copy-pasted around:

- **"fine" floatMap** `setSteps(0.1, 0.01, 0.001, 0.001)` -- COARSE 0.01 (correct; only D2). Used by: catchall (Flakes/Lambda/Sfera/Som), Helicase, Larets, Mirror, Network, Rauschen, Fabula, Colmatage, DrumVoice, plus house/anamnesis/zaum.
- **"coarse" floatMap** `setSteps(1, 0.1, 0.01, 0.001)` -- COARSE **0.1** (D1). Used by: **Pecto, Petrichor (MultitapDelay), Parfait (MultibandSaturator), Impasto (MultibandCompressor), Etcher, Filterbank (Tomograph), StepListControl, porcelain/Chime**, and stolmine (Etcher/Filterbank/MultitapDelay).

Because each `floatMap` is file-local (not a shared module), fixing D1 means editing each
offending unit file's one `setSteps` line. Within a file, `floatMap` feeds many controls
(including some non-normalized level/gain controls that legitimately want a coarser step),
so a blanket re-step of the shared helper is NOT always safe -- see the fix mechanism note (Section 6).

### Count of divergences across published packages

| divergence | where | severity |
|---|---|---|
| **D1** normalized COARSE 0.1 | Pecto, Petrichor, Parfait, Impasto, Etcher, Filterbank (all spreadsheet); biome DJFilter/FadeMixer/Integrator/TiltEQ/CodescanFilter mix; ChaselightControl/GateSeqInfoControl gateWidth | **HIGH** |
| **D2** superFine 0.001 vs 0.0001 | near-universal in catchall + spreadsheet private maps (Network, Larets, Mirror, Helicase, Rauschen, Colmatage, DrumVoice, Fabula, all catchall) | LOW |
| **D3** linear-Hz vs octave | Lambda/Sfera cutoff, Mirror/Helicase f0+formant, Fabula HPF, Filterbank band freq, Impasto/Parfait/Rauschen tone/cutoff, biome CodescanOsc/VarishapeOsc/VarishapeVoice f0 + SpectralFollower freq | MED |
| **D4** linear feedback vs dB | Som feedback, Clouds feedback, Mirror/Petrichor/Pecto feedback, Helicase modFeedback | MED |
| **D5** linear level vs dB | most `level`/`inputLevel`/`outputLevel`/`bandLevel` faders across mi + spreadsheet + catchall | LOW (house style) |
| **Ddup** private map == framework map | biome (many), catchall floatMap family, most spreadsheet private `[0,1]`/`[-1,1]`/`[0,2]` maps | LOW-MED |
| **Dmix** main vs sub readout mismatch | Parfait, Impasto, Petrichor, Colmatage, Filterbank, Helicase, GateSeq | MED (mostly auto-fixed by D1 fix) |

### Package compliance at a glance

- **peaks** -- fully compliant. Every control uses `Encoder.getMap("[0,1]")` / `("[-1,1]")`. Zero divergences. Reference pattern.
- **scope** -- no dial maps at all (index selectors via encoder deltas). Nothing to standardize.
- **mi** -- highly compliant. All continuous params use `Encoder.getMap`. Only feedback-as-[0,1] (Clouds) and inline int selectors (Dint, cosmetic). No D1.
- **catchall** -- clean coarse step (fine floatMap), but D2 everywhere, plus D3 (Lambda/Sfera cutoff), D4 (Som feedback), one hard D1 (AlembicReagentControl coarse 0.05).
- **biome** -- mixed: 6 D1 controls, several Ddup, D3 on all oscillators, D2/Dint scattered.
- **spreadsheet** -- the bulk of the work. JF + Visadhara are compliant references; Network/Larets/Mirror/Helicase/Rauschen/Fabula/Colmatage/DrumVoice/Blanda get coarse right (D2 only); Pecto/Petrichor/Parfait/Impasto/Etcher/Filterbank carry D1.

---

## 3. Per-package tables

Legend: steps shown as `sc/c/f/sf` (superCoarse / COARSE / fine / superFine). "getMap X" = uses
`Encoder.getMap("X")` (compliant). Gate/Pitch/OptionControl/list/viz controls with no dial map are omitted.

### 3.1 peaks -- COMPLIANT (no action)

Every unit exposes up to four `GainBias` param knobs, each `Encoder.getMap("[0,1]")` or
`("[-1,1]")` (coarse 0.01). Applies to BassDrum, BouncingBall, ByteBeats, FmDrum, FmLfo,
MiniSequencer, ModSequencer, NumberStation, Plo, RandomisedEnvelope, SnareDrum, TapLfo, WsmLfo
(HighHat has only a Gate). Files `mods/peaks/assets/*.lua` lines ~59-88.

Design note (not a bug): controls that are conceptually freq/pitch/rate/time (FmDrum freq,
Plo ptch, FmLfo rate, RandomisedEnvelope atk/dec) are exposed as normalized 0..1 because the
Peaks DSP does the internal scaling. The octave/dB/ADSR standards deliberately do not apply.

### 3.2 scope -- NO DIAL MAPS (no action)

`ScopeView.lua` Time (idx 1..7 over `{1,2,4,8,16,32,64}`, :22-23/:145-153) and Gain
(idx 1..5 over `{0.25,0.5,1,2,4}`, :24-25/:155-163) are integer-index LUT selectors driven
directly by encoder deltas (encoder locked to Coarse). Volt is a read-only readout. Intentional
non-DialMap pickers -- no framework analog is a clean fit. OK.

### 3.3 mi

| unit | control | range | steps / getMap | map def file:line | type | framework std | divergence |
|---|---|---|---|---|---|---|---|
| Clouds | position/size/texture/drywet/spread | 0..1 | getMap [0,1] | Clouds.lua:152-222 | norm | [0,1] | OK |
| Clouds | density | -1..1 | getMap [-1,1] | Clouds.lua:172 | norm | [-1,1] | OK |
| Clouds | pitch | -48..48 | 1/1/0.1/0.01 | Clouds.lua:18-22 | pitch (semitone) | cents (no semitone map) | (c) no analog, OK |
| Clouds | feedback | 0..1 | getMap [0,1] | Clouds.lua:212 | feedback | `feedback` dB | **D4** |
| Clouds | mode | 0..2 | 1/1/1/1 r1 | Clouds.lua:11-16 | index | intMap | Dint (OK) |
| Commotio | 11 knobs (bow/blow/strike/env/damp/bright) | 0..1 | getMap [0,1] | Commotio.lua:104-204 | norm | [0,1] | OK |
| Grids | mapx/mapy/density/chaos/width | 0..1 | getMap [0,1] | Grids.lua:102-142 | norm | [0,1] | OK |
| Grids | channel | 0..2 | 1/1/1/1 r1 | Grids.lua:152-157 | index | intMap | Dint (OK) |
| MarblesT | jitter/dejavu/output | 0..1 | getMap [0,1] | MarblesT.lua:109-145 | norm | [0,1] | OK |
| MarblesT/X | length | 1..16 | 1/1/1/1 r1 | MarblesT.lua:129 / MarblesX.lua:151 | index | intMap | Dint (OK) |
| MarblesX | spread/bias/steps/dejavu/output | 0..1 | getMap [0,1] | MarblesX.lua:111-167 | norm | [0,1] | OK |
| Plaits | harmonics/timbre/morph/decay/lpg | 0..1 | getMap [0,1] | Plaits.lua:300-344 | norm | [0,1] | OK |
| Plaits | freq | -48..48 | 1/1/0.1/0.01 | Plaits.lua:18-19 | pitch (semitone) | cents (no analog) | (c) OK |
| Plaits | engine | 0..23 | 1/1/1/1 r1 | Plaits.lua:14-16 | index | intMap | Dint (OK) |
| Rings | struct/bright/damp/pos | 0..1 | getMap [0,1] | Rings.lua:249-282 | norm | [0,1] | OK |
| Rings | mix | -1..1 | getMap [-1,1] | Rings.lua:293 | norm (balance) | [-1,1] | OK |
| Rings | freq | -48..48 | 12/1/0.1/0.01 | Rings.lua:27-28 | pitch (semitone) | cents (no analog) | (c) OK (octave superCoarse, best variant) |
| Rings | model | 0..5 | 1/1/1/1 r1 | Rings.lua:14-16 | index | intMap | Dint (OK) |
| Stratos | amount/time/diffusion/damping | 0..1 | getMap [0,1] | Stratos.lua:71-101 | norm | [0,1] | OK |
| Stratos | gain | 0..1 | getMap [0,1] | Stratos.lua:111 | level | volume dB | D5 (minor) |
| Warps | timbre/drive | 0..1 | getMap [0,1] | Warps.lua:146-157 | norm | [0,1] | OK |
| Warps | algo | 0..0.625 | 0.125/0.01/0.001/0.0001 | Warps.lua:14-15 | morph | none (intentional range) | OK |

mi shared selector controls (ModeSelector/EngineSelector/AlgoSelector/MixControl) hold no map
logic -- range/steps supplied by the host unit. MixControl (Rings mix) uses getMap [-1,1], OK.

### 3.4 catchall

| unit | control | range | steps / getMap | map def file:line | type | framework std | divergence |
|---|---|---|---|---|---|---|---|
| Alembic | freq | oscFreq | getMap oscFreq + freqGain | AlembicVoice.lua:109,113 | freq | oscFreq | OK (uses octave map) |
| Alembic | scan/reagent/comb/ferment | 0..1 | getMap [0,1] | AlembicVoice.lua:129-174 | norm | [0,1] | OK |
| Alembic | level | -1..1 | getMap [-1,1] | AlembicVoice.lua:186 | level | volume dB | D5 (minor) |
| AlembicScanControl | K | 2..6 | 1/1/1/1 r1 | AlembicScanControl.lua:46-50 | index | intMap | Dint (OK) |
| AlembicReagentControl | Amount | 0..1 | 0.1/**0.05**/0.01/0.001 | AlembicReagentControl.lua:38-43 | norm | [0,1] | **D1-hard: coarse 0.05** |
| Flakes | depth/delay/warble/noise/dryWet | 0..1 | 0.1/0.01/0.001/0.001 | Flakes.lua:11 (via floatMap) | norm | [0,1] | D2 + Ddup |
| Lambda | scan | 0..1 | 0.1/0.01/0.001/0.001 | Lambda.lua:15 | norm | [0,1] | D2 + Ddup |
| Lambda | level | 0..1 | 0.1/0.01/0.001/0.001 | Lambda.lua:20 | level | volume dB | D2/D5 |
| Lambda | f0 | 0.1..2000 | 100/10/1/0.1 | Lambda.lua:22-26 | freq | oscFreq | **D3** |
| Lambda | cutoff | 20..20000 | 1000/100/10/1 | Lambda.lua:28-32 | cutoff | filterFreq | **D3** |
| Lambda | seed | 0..999 | 1/1/1/1 r1 | Lambda.lua:34-39 | index | intMap | Dint (OK) |
| Sfera | paramX/paramY | 0..1 | 0.1/0.01/0.001/0.001 | Sfera.lua:20 | norm | [0,1] | D2 + Ddup |
| Sfera | cutoff | 20..20000 | 1000/100/10/1 | Sfera.lua:28-32 | cutoff | filterFreq | **D3** |
| Sfera | level | 0..2 | 0.1/0.01/0.001/0.001 | Sfera.lua:21 | level | [0,2]/volume | D2/D5 |
| Sfera | spin | -2..2 | 0.1/0.01/0.001/0.001 | Sfera.lua:22-26 | other | none | OK (D2 superFine) |
| SferaCutoffControl | Q Scale | 0.25..4 | 0.25/0.05/0.01/0.01 | SferaCutoffControl.lua:24-28 | Q | none | OK |
| Som | scan/plasticity/mix/parallax/mod/modShape | 0..1 (parallax -1..1) | 0.1/0.01/0.001/0.001 | Som.lua:15-18,24,208 | norm | [0,1]/[-1,1] | D2 + Ddup |
| Som | neighborhood/rate | 0.05..0.5 / 0.01..1 | 0.05/0.01/0.001/0.001 | Som.lua:27-36 | other | none | OK |
| Som | modRate | 0.001..20 | 1/0.1/0.01/0.001 | Som.lua:19-23 | rate (Hz) | clockFreq | D3-ish |
| Som | modFeedback | 0..0.95 | 0.1/0.01/0.001/0.001 | Som.lua:25 | feedback | `feedback` dB | **D4** |
| Som | feedback | 0..1 | 0.1/0.01/0.001/0.001 | Som.lua:259 | feedback | `feedback` dB | **D4** + D2 |
| Som | level | 0..2 | 0.1/0.01/0.001/0.001 | Som.lua:26 | level | [0,2]/volume | D2/D5 |
| SomScanControl | decay/nbr/rate readouts | -- | duplicate Som maps | SomScanControl.lua:42-58 | other | none | Ddup (internal) |
| SomModControl | rate/shape/fb readouts | -- | duplicate Som modRate/shape/fb | SomModControl.lua:35-49 | -- | -- | Ddup + D4 (fb) |

catchall note: only AlembicVoice uses `Encoder.getMap`; the other four units re-roll a private
floatMap, which is the root of nearly all their divergences. Som's shift-sub controls duplicate
the main maps (can drift out of sync).

### 3.5 biome

| unit | control | range | steps / getMap | map def file:line | type | framework std | divergence |
|---|---|---|---|---|---|---|---|
| Station X (CodescanFilter) | scan | 0..1 | 0.1/0.01/0.001/0.0001 | CodescanFilter.lua:132 | norm | [0,1] | Ddup (== [0,1]) |
| Station X | taps | 4..64 | 8/4/1/1 r1 | CodescanFilter.lua:138 | index | intMap | Dint (OK) |
| Station X | mix | 0..1 | 0.25/**0.1**/0.01/0.001 | CodescanFilter.lua:145 | norm | [0,1] | **D1** |
| Bletchley Park (CodescanOsc) | scan | 0..1 | 0.1/0.01/0.001/0.0001 | CodescanOsc.lua:133 | norm | [0,1] | Ddup |
| Bletchley Park | f0 | 0.1..2000 | 100/10/1/0.1 | CodescanOsc.lua:139 | freq | oscFreq | **D3** |
| Bletchley Park | level | -1..1 | getMap [-1,1] | CodescanOsc.lua:193 | level | volume dB | D5 (minor) |
| Constant Random | rate | 0.01..100 | 10/1/0.1/0.01 | ConstantRandom.lua:48 | rate (Hz) | `rate` LUT | (c) no clean analog |
| Constant Random | slew/level | 0..1 | 0.1/0.01/0.001/0.001 | ConstantRandom.lua:53,59 | norm | [0,1] | D2 + Ddup |
| 94 Discont | amount | 0..10 | getMap [0,10] | Discont.lua:83 | other | [0,10] | OK |
| 94 Discont | mix | 0..1 | getMap [0,1] | Discont.lua:93 | norm | [0,1] | OK |
| DJ Filter | cut | -1..1 | 0.25/**0.1**/0.01/0.001 | DJFilter.lua:48 | norm | [-1,1] | **D1** |
| DJ Filter | q | 0..1 | 0.25/**0.1**/0.01/0.001 | DJFilter.lua:54 | norm | [0,1] | **D1** |
| Fade Mixer | fade | 0..1 | 0.25/**0.1**/0.01/0.001 | FadeMixer.lua:67 | norm | [0,1] | **D1** |
| Fade Mixer | level | 0..4 | 1/0.1/0.01/0.001 | FadeMixer.lua:73 | level | [0,10]/volume | (b/c) linear level |
| Gated Slew | time | slewTimes LUT | getMap slewTimes + gain | GatedSlewLimiter.lua:90,94 | time | slewTimes | OK (matches exactly) |
| Gesture | offset | -1..1 | getMap [-1,1] | GestureSeq.lua:265 | norm | [-1,1] | OK |
| Gesture | slew | 0..10 | getMap [0,10] | GestureSeq.lua:275 | time | [0,10] (slewTimes ideal) | OK (framework map) |
| Gridlock | val1/2/3 | -5..5 | 1/0.1/0.01/0.001 | Gridlock.lua:66 | bias | [-5,5]/bias | Ddup (== bias) |
| Integrator | rate | 0..100 | 10/1/0.1/0.01 | Integrator.lua:50 | other | percent | Ddup-ish (superFine 0.01 vs 0.1) |
| Integrator | leak | 0..1 | 0.25/**0.1**/0.01/0.001 | Integrator.lua:56 | norm | [0,1] | **D1** |
| Latch Filter | fundamental | -48..48 | 1/1/0.1/0.01 | LatchFilter.lua:13 | pitch (semitone) | cents (no analog) | (c) OK |
| Latch Filter | resonance | 0..1 | getMap [0,1] | LatchFilter.lua:102 | norm | [0,1] | OK |
| NR / NRCircle | prime/mask/factor/length | 0..31 etc | 2/1/0.25/0.25 r1 | NR.lua:11-20 / NRCircle.lua:16 | index | intMap | Dint (superCoarse 2 vs 5) |
| NR | width | 0..1 | getMap [0,1] | NR.lua:153 | norm | [0,1] | OK |
| PSR | scale | 0..5 | 1/0.1/0.01/0.001 | PingableScaledRandom.lua:53 | other | [0,10] shape | OK |
| PSR | offset | -5..5 | 1/0.1/0.01/0.001 | PingableScaledRandom.lua:59 | bias | [-5,5]/bias | Ddup (== bias) |
| PSR / Quantoffset | levels | 0..128 / 2..128 | 12/1/1/1 r1 | PingableScaledRandom.lua:65 / Quantoffset.lua:44 | index | intMap | Dint (OK) |
| Quantoffset | offset | -1..1 | getMap default | Quantoffset.lua:57 | norm | [-1,1] | OK |
| Spectral Follower | freq | 20..20000 | 1000/100/10/1 | SpectralFollower.lua:53 | cutoff | filterFreq | **D3** |
| Spectral Follower | bandwidth | 0.1..4 | 1/0.5/0.1/0.01 | SpectralFollower.lua:59 | other | none | OK |
| Spectral Follower | attack/decay | 0.0001..0.5 / ..5 | 0.1/0.01/... , 0.5/0.1/... | SpectralFollower.lua:65,71 | time | slewTimes/ADSR | (b) linear time |
| Tilt EQ | tilt | -1..1 | 0.5/**0.1**/0.01/0.001 | TiltEQ.lua:43 | norm | [-1,1] | **D1** |
| Transport | bpm | 1..300 | 10/1/0.1/0.1 | Transport.lua:41 | tempo | `tempo` | (c/e) near-dup, superFine 0.1 vs 0.01 |
| Varishape Osc | shape | 0..1 | 0.1/0.01/0.001/0.0001 | VarishapeOsc.lua:12 | norm | [0,1] | Ddup (== [0,1]) |
| Varishape Osc | f0 | 0.1..2000 | 100/10/1/0.1 | VarishapeOsc.lua:18 | freq | oscFreq | **D3** |
| Varishape Osc | level | -1..1 | getMap [-1,1] | VarishapeOsc.lua:115 | level | volume | D5 (minor) |
| Varishape Voice | shape/decay | 0..1 | 0.1/0.01/0.001/0.0001 | VarishapeVoice.lua:85,97 | norm | [0,1] | Ddup |
| Varishape Voice | f0 | 0.1..2000 | 100/10/1/0.1 | VarishapeVoice.lua:91 | freq | oscFreq | **D3** |
| Varishape Voice | level | -1..1 | getMap [-1,1] | VarishapeVoice.lua:161 | level | volume | D5 (minor) |

### 3.6 spreadsheet -- compliant / coarse-correct units (D2 only)

These get COARSE 0.01 right; the only nit is superFine 0.001 vs 0.0001 (D2) plus Ddup, unless
noted. Fixing them is low priority.

| unit (display) | notable maps | file:line | note |
|---|---|---|---|
| **JF** | all maps `0.1/0.01/0.001/0.0001`; `fm` uses getMap [-1,1] | JF.lua:143-171,268 | **COMPLIANT reference** (matches framework exactly) |
| **Visadhara** | parametrized floatMap default `0.1/0.01/0.001/0.0001`; unitMap/bipolarMap | Visadhara.lua:23-36 | **COMPLIANT** (superFine 0.0001); level linear (D5) |
| Blanda | levelMap/weightMap/offsetMap/scanMap/skewMap/shapeMap all `0.1/0.01/0.001/0.0001` | Blanda.lua:28-35 | coarse+superFine correct; Ddup vs [0,1]/[0,2] |
| Network | glitch/size/density/motion/connectivity/decay/wet `0.1/0.01/0.001/0.001`; seed 0.0001 | Network.lua:15-32 | D2 (seed compliant) |
| Larets | skew/offset/mix `0.1/0.01/0.001/0.001`; stepCount/loopLength/clockDiv int | Larets.lua:15-32 | D2 + Dint |
| Rauschen | paramX/paramY/level/morph `0.1/0.01/0.001/0.001`; cutoff linear-Hz | Rauschen.lua:36-56 | D2 + **D3** (cutoff) |
| Helicase | modMix/syncPhase/level/discIndex `0.1/0.01/0.001/0.001`; f0 linear-Hz; modFeedback | Helicase.lua:19-31 | D2 + **D3** (f0) + **D4** (modFb); modIndex 0..10 coarse 0.01 too fine |
| Mirror | shape/feedback/modDepth/sync/mirror/level `0.1/0.01/0.001/0.001`; f0+formant linear-Hz | Mirror.lua:11-21 | D2 + **D3** (f0/formant) + **D4** (feedback) |
| Fabula | size/predelay/mix/early/freeze/decay/damp/diffusion `0.1/0.01/0.001/0.001`; hpf linear-Hz | Fabula.lua:26-27,274 | D2 + **D3** (HPF 20..500) |
| Colmatage | block/density/texture/mix/ritard/blend/amp `0.1/0.01/0.001/0.001` | Colmatage.lua:15-41 | D2; fade/level Dmix; div readout range bug (see below) |
| Ngoma (DrumVoice) | character/level/charShape/clipper/comp use **getMap [0,1]** (GOOD); readouts `0.1/0.01/0.001/0.001` | DrumVoice.lua:186,226,241-289 | main faders compliant; sub-readouts D2 (Dmix) |

### 3.7 spreadsheet -- D1 units (the priority fixes)

`floatMap = setSteps(1, 0.1, 0.01, 0.001)` -> every normalized control routed through it moves
0.1 per detent. One `setSteps` line per file, but shared by many controls in that file.

| unit (display) | floatMap def | D1 normalized controls (via floatMap) | other divergences |
|---|---|---|---|
| **Pecto** | Pecto.lua:13-17 | feedback (also D4), mix, inputLevel/outputLevel (D5), tanhAmt, xform-depth | size uses dedicated sizeMap (compliant); feedback D4 |
| **Petrichor (MultitapDelay)** | MultitapDelay.lua:20-24 | mix, feedbackTone, inputLevel/outputLevel (D5), tanhAmt, skew, grainSize, drift, reverse, masterTime (also linear time), xform depth/spread | feedback D4 (0..0.95); FilterListControl cutoff D3; Dmix vs MixControl readouts |
| **Parfait (MultibandSaturator)** | MultibandSaturator.lua:24-28 | mix, band{lo/mid/hi} level (D5), {bn}Amt, {bn}Bias, {bn}Morph, toneAmt, compAmt, outputLevel (D5), tanhAmt | freq/toneFreq D3; skew coarse 0.1; **Dmix**: BandControl sub-readouts all coarse 0.01 vs main 0.1 |
| **Impasto (MultibandCompressor)** | MultibandCompressor.lua:14-18 | skew, mix, driveTone, mixOutput (D5) | driveToneFreq D3; **Dmix**: CompMixControl outputReadout coarse 0.01 vs main 0.1; DriveControl freqReadout range 50-5000 != main 20-20000 |
| **Etcher** | Etcher.lua:15-19 | input, skew, level, deviation; SegmentListControl offset; TransferCurveControl deviation | xform depth (factorMap) correctly coarse 0.01 -> inconsistent with unit's own deviation (0.1) |
| **Tomograph (Filterbank)** | Filterbank.lua:15-19 | macroQ, mix, tanhAmt | band EditFreq D3 (20..16000); vOct/slew coarse 0.1; **Dmix**: MixControl tanh readout 0.01 vs fader 0.1 |

Shared spreadsheet control classes worth noting for the fix mechanism:

- `TransformGateControl` (Larets, Pecto, TrackerSeq, Petrichor): its default funcMap/factorMap/threshReadout live once at `TransformGateControl.lua:21-33,132`; threshReadout correctly uses `Encoder.getMap("default")`. But each host unit passes its own `factorParam` depth map built from the host's floatMap -> D1 leaks in from the host, not this class.
- `MixControl` (Pecto, Petrichor, Colmatage, Filterbank): internal input/output readouts `0.5/0.1/0.01/0.001`, tanh readout `0.1/0.01/0.001/0.001` (coarse 0.01). Since host main faders use coarse 0.1, this is the Dmix mismatch. `MixControl.lua:37-53`.
- `DriveControl` (Impasto, Parfait): freqReadout range 50..5000 (`DriveControl.lua:41-45`) does not match Impasto's main driveToneFreq range 20..20000 -- latent range mismatch.
- `BandControl` (Parfait): every sub-readout coarse 0.01 vs Parfait main faders coarse 0.1.

Latent range/param bugs found in passing (not step issues, but worth fixing alongside):

- Colmatage `LaretClockControl` div readout map is 1..16 but edits `subdiv` whose fader map is 6..32 (`LaretClockControl.lua:35-40`) -- readout clamps/mis-displays for 17..32.
- Etcher `SegmentListControl` keyboard message says "-5 to 5" but the offset map is +/-1 (`SegmentListControl.lua:21`).
- GateSeq `scopeMap` (`GateSeq.lua:26-31`) is defined but unused (xformScope adapter has no ply).

### 3.8 spreadsheet -- sequencers / other (mostly OK)

- **TrackerSeq (Excel)**: slew uses `getMap("[0,10]")` (compliant); seqLen/loopLen int maps OK; StepListControl offset maps use coarse-floatMap (D1 on the +/-5V step edit-readout, `StepListControl.lua:14-31`).
- **GateSeq (Ballot)**: gateWidthFader uses `getMap("[0,1]")` (GOOD); but ChaselightControl velocity (`ChaselightControl.lua:22-26`) and GateSeqInfoControl gateWidth readout (`GateSeqInfoControl.lua:80-81`) are coarse 0.1 (D1) -> Dmix vs the compliant fader.
- **Canals**: span uses getMap [0,1], quality uses getMap [-1,1] (GOOD); fundamental is a linear semitone map (no analog, OK); output/mode selectors OK.

---

## 4. Unpublished packages (brief)

Inventory only lightly -- not published, lower priority.

- **anamnesis** (`Anamnesis.lua`): shared floatMap `0.1/0.01/0.001/0.001` (:24-26), zeroOneMap. Coarse correct (D2 only). speedMap bespoke. No D1.
- **house** (12 reverb/effect units): every file repeats floatMap `0.1/0.01/0.001/0.001` + zeroOneMap (e.g. Galactic.lua:17-23). Coarse correct (D2 only). No D1. (Package deprioritized indefinitely.)
- **zaum** (`Sujet.lua`): floatMap `0.1/0.01/0.001/0.001` (:22-28). Coarse correct (D2). No D1.
- **kryos** (`Kryos.lua`): uses `Encoder.getMap("[0,1]")` throughout (:87-137); one transpose map `1/1/0.1/0.01` (:10-11). Clean.
- **porcelain** (`Chime.lua`): floatMap `setSteps(1, 0.1, 0.01, 0.001)` -> **D1** on qMap/coupleMap/detuneMap/driveMap/spreadMap (all floatMap(0,1), :27-34). (WIP, not published.)
- **stolmine** (~40 files): two conventions coexist. Correct coarse-0.01 in TapListControl/MixControl/TimeControl/FilterListControl; **D1** coarse-0.1 in Etcher/Filterbank/MultitapDelay (mirror of the spreadsheet copies), plus linear-Hz cutoffs (FilterListControl/BandListControl/SpectralFollower/CodescanOsc) and a linear feedback (MultitapDelay feedbackMap 0..0.95). CodescanFilter/CodescanOsc hit the exact framework standard `0.1/0.01/0.001/0.0001`.

---

## 5. Prioritized recommendations

Highest value, lowest risk first.

1. **Fix D1 on normalized controls (coarse 0.1 -> 0.01).** The user's #1 concern; every one of
   these dials currently moves 10% per detent. Published units:
   - spreadsheet: **Pecto, Petrichor, Parfait, Impasto, Etcher, Filterbank** (change each file's
     `floatMap` setSteps `1,0.1,0.01,0.001` -> `0.1,0.01,0.001,0.0001`, but audit the level/gain
     controls in each file first -- see Section 6).
   - spreadsheet isolated: **ChaselightControl velocity, GateSeqInfoControl gateWidth,
     StepListControl offset/deviation** (give a dedicated coarse-0.01 map or `Encoder.getMap`).
   - biome: **CodescanFilter mix, DJFilter cut+q, FadeMixer fade, Integrator leak, TiltEQ tilt**.
   - catchall: **AlembicReagentControl Amount** (coarse 0.05 -> 0.01).
   - (This subsumes the `mix-control-standards` ledger subset for Pecto/Petrichor/Parfait/Impasto.)

2. **Fix Dmix (main-fader vs shift-sub readout granularity).** Largely auto-resolved by #1 for
   Parfait/Impasto/Petrichor/Filterbank/GateSeq once the main floatMap is corrected. Verify the
   shared `MixControl`/`BandControl`/`CompMixControl` readout steps then agree with the faders.

3. **Adopt octave maps for frequency/cutoff (D3).** Convert linear-Hz oscillator/filter controls
   to `Encoder.getMap("oscFreq")` / `("filterFreq")` (or a matching `octaveMapWithZero`):
   Lambda f0+cutoff, Sfera cutoff, Mirror f0+formant, Helicase f0, Fabula HPF, Filterbank band
   freq, Impasto/Parfait tone/cutoff, Rauschen cutoff, biome CodescanOsc/VarishapeOsc/
   VarishapeVoice f0, SpectralFollower freq. Higher effort (changes feel + possibly voltage
   scaling); do per-unit with listening checks. AlembicVoice already shows the compliant pattern.

4. **Decide feedback convention (D4).** Framework `feedback` is a dB map. Habitat feedback
   controls are linear 0..1 / 0..0.95 (Som, Clouds, Mirror, Petrichor, Pecto, Helicase). Some are
   FM-index rather than delay feedback, so this is a judgment call -- pick one convention per
   control class rather than blanket-converting.

5. **Replace exact-duplicate private maps with `Encoder.getMap` (Ddup).** Mechanical, low-risk,
   removes drift: biome Gridlock/PSR offset (== bias), CodescanFilter/Osc scan + Varishape shape
   (== [0,1]); catchall floatMap normalized knobs; spreadsheet Blanda/Network/etc. Where the only
   difference is superFine 0.001 vs 0.0001 (D2), switching to `getMap` also fixes D2 for free.

6. **Normalize superFine 0.001 -> 0.0001 (D2).** Lowest priority (deepest fine mode only).
   Cleanest done as a side effect of #5 (switch private normalized maps to `getMap`).

7. **Level/volume as dB (D5).** House-style linear levels are widely used and arguably fine;
   only revisit if a unit specifically wants dB-law feel. Low priority.

8. **Fix latent range bugs found alongside**: Colmatage div readout 1..16 vs subdiv 6..32;
   Etcher SegmentList "-5 to 5" message vs +/-1 map; DriveControl freqReadout 50..5000 vs
   Impasto main 20..20000; remove GateSeq unused scopeMap.

Suggested batching: fold #1 + #2 + #5/#6 into one point release (mechanical, well-contained,
high user-visible payoff). Treat #3 + #4 as a separate, listened-through release since they
change parameter feel and possibly calibration.

---

## 6. Fix mechanism (important)

Private `floatMap` helpers are **file-local functions shared across many controls in that file**,
and some of those controls are non-normalized levels/gains (e.g. `inputLevel`/`outputLevel` 0..4,
`bandLevel` 0..2) that may legitimately want a coarser step. So:

- **Do NOT** blindly re-step a shared `floatMap` used for both normalized and level controls if
  the level controls should keep coarse 0.1. Audit each consumer first.
- For a mixed-use file, either (a) split into `normMap` (coarse 0.01) + `levelMap` (coarse 0.1),
  or (b) give the specific normalized control a dedicated map / per-control step override, or
  (c) best: switch the normalized controls to `Encoder.getMap("[0,1]")` / `("[-1,1]")` and leave
  the private floatMap for the genuinely non-standard ranges.
- The D1 units (Pecto/Petrichor/Parfait/Impasto) route BOTH normalized and level/gain controls
  through the same coarse-0.1 floatMap. Correcting the shared helper to coarse 0.01 would also
  make the 0..4 level faders move 0.01/detent -- decide whether that is desired (arguably yes for
  fine level trim; matches framework `[0,10]` which is coarse 0.1, so a dedicated levelMap may be
  preferable). This is exactly why the `mix-control-standards` ledger note says to give the MIX
  control a dedicated map rather than re-step the shared floatMap globally.
- `peaks` and `mi` demonstrate the target end-state: no private maps, everything via
  `Encoder.getMap`. `JF` and `Visadhara` show the acceptable private-map end-state (framework-exact
  steps including superFine 0.0001).
