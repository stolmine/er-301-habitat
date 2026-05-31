# Airwindows reverb port research

Status: research complete 2026-05-30. Decision-ready shortlist below.

## Context

ER-301 / Cortex-A8 / NEON / 48 kHz / FRAMELENGTH=256. CPU + memory budget is tight. Prior CloudSeedCore port hangs the audio thread on first-frame DSP (see `cloudseed-archive` branch). Network unit already covers the heavy / glitch / FDN space. Looking for low-CPU "musical" reverbs across three aesthetic buckets:

1. **Pocket** — small / spring / plate, smear and color
2. **Small + wooden room** — natural, drum-booth, glue
3. **Bombastic / lush** — big halls, shimmer, drone

## Top 3 to port first

| Rank | Reverb | Bucket | Stereo state | Port risk | Why |
|---|---|---|---|---|---|
| 1 | **Galactic** | bombastic | ~488 KB | LOW | Cleanest port in the catalog. Pure 3-stage cascaded 4x4 FDN with 12 fixed-tap delays + tiny 256-sample modulated pre-delay. No per-sample topology branching, no `std::vector`, no host deps, no init-order DSP. Years-mature, catalog favorite. |
| 2 | **kWoodRoom** | small / wooden | ~220 KB | MEDIUM | Literally named for the bucket. Dual 3x3 / 6x6 nested delay-matrix banks with Bezier undersampling (which actually *reduces* per-sample cost). Algorithmic but well-bounded. kVerb-family code style is dense — extra audit warranted. |
| 3 | **Galactic2** | pocket | ~216 KB | LOW | Half the memory of Galactic, same proven topology family. 16 prime-sized delays (83..3389 samples), tighter and more reflective character. After Galactic is ported, Galactic2 is essentially a parameter + buffer-size respin. |

Total: ~1.1 MB combined state, three buckets covered, two LOW-risk ports + one MEDIUM. Family-similarity between Galactic and Galactic2 means engineering work amortizes.

## Per-bucket ranking

**Pocket (smear / color)**
1. **Galactic2** — smallest true-FDN in the catalog at 216 KB/ch
2. VerbTiny — more aggressive ringing chamber character, but higher port risk (Bezier + dual a4/b4 banks)
3. (No Airwindows spring algo exists outside PocketVerbs' Spring switch, and PocketVerbs is non-startable on AM335x — see DON'T-PORT)

**Small / wooden room**
1. **kWoodRoom** — literally named for it, algorithmic, undersampled
2. Galactic2 — already short and bright, can pass as a room
3. kCathedral (smallest setting) — overkill but workable

**Bombastic / lush**
1. **Galactic** — catalog favorite, mature, NEON-friendly fixed-tap, low branch density
2. kCathedral — honest cathedral character at ~190 KB/ch (~380 KB stereo)
3. kGuitarHall — medium-hall option if Galactic feels too washy

## Per-candidate writeups

**Galactic** — 3-stage cascaded 4x4 FDN (delays I/J/K/L → A/B/C/D → E/F/G/H), 12 delay lines + 256-sample pre-delay with sine-modulated linear interpolation. ~50-80 mul / 40-60 add per sample (~150 ops once FDN matrix is amortized over `cycleEnd`). State: ~488 KB stereo (62,527 doubles/ch). Fixed-tap reads except the small pre-delay vibrato; no per-sample switch on topology; self-contained. **Port risk: LOW.** Mature (years in catalog), highly regarded, no CloudSeed-shaped traps.

**Galactic2** — Tighter, more reflective variant. 16 delay lines, prime-sized 83-3389 samples. ~216 KB/ch (~432 KB stereo). Same general op shape as Galactic. **Bucket: small-room / pocket.** **Port risk: LOW.**

**Galactic3** — Same buffer sizes as Galactic with modulation tweaks. ~528 KB/ch. Bucket and risk identical to Galactic — Galactic is the safer reference of this pair.

**GalacticVibe** — Galactic minus the FDN, just the modulated pre-delay → vibrato/chorus. **Not a reverb. Skip.**

**Verbity** — Structurally identical to Galactic (same 12-line 3-stage FDN, same buffer sizes 6480/3660/.../15220/8460/4540/3200, ~488 KB stereo). Differences are parameter ranges and a different lowpass arrangement. **Port risk: LOW**, but redundant with Galactic — pick one.

**Verbity2** — 25 delay lines (5x5 hierarchy), ~749 KB/ch (~1.5 MB stereo). **Port risk: MEDIUM-HIGH** on memory grounds.

**VerbTiny** — Bezier-undersampled 4-stage 4x4 cross-matrix with parallel `a4`/`b4` banks. 131 KB per a4 set, 262 KB/ch total = ~525 KB stereo. **Despite the name, NOT actually small** — doubled banks negate per-array savings. ~250-300 ops in active-cycle frame. One major branch on `bez[bez_cycle] > 1.0`. **Port risk: MEDIUM** — Bezier undersampling + dual banks is subtle and slightly CloudSeed-shaped.

**VerbSixes / VerbThic** — Larger VerbTiny siblings (3x3 through 6x6), hundreds of KB more state, same Bezier+dual-bank shape, no clear aesthetic advantage. **Skip** in favor of Galactic2 / VerbTiny.

**MatrixVerb** — 8-line FDN + 4 allpass diffusers + 3 biquads + vibrato-modulated reads on all 8 lines + crossfade between interpolated/non-interpolated taps. ~170 ops/sample, ~633 KB/ch (~1.24 MB stereo). The "vibrato on all 8 delay-line reads with linear interp + crossfade" is **structurally similar to CloudSeed's ModulatedAllpass chain**. **Port risk: HIGH.**

**Reverb** (namesake) — 13 delay lines (8111 → 3111) + 4 allpasses + 8 parallel delays with Householder matrix + 3 biquads + vibrato on all 8 lines. ~561 KB stereo, ~400-500 ops/sample. Same risk shape as MatrixVerb, somewhat smaller. **Port risk: HIGH.**

**MV** — 26 cascaded allpasses (A-Z) selected via per-sample 26-case switch on `stage`. Asin/sin nonlinearity per sample. **1.81 MB/ch (~3.6 MB stereo).** Per-sample stage-switch is exactly the "runtime-branched DSP dispatch" pattern flagged in memory `feedback_runtime_branched_dsp_dispatch`. **Port risk: HIGH. Do not.**

**PocketVerbs** — 26 dual-stage allpass chains × stereo + 4-way switch (Chamber/Spring/Tiled/Room) at unit level. **~2.1 MB/ch (~4.2 MB stereo).** Despite the "pocket" name this is the **single largest reverb in the catalog** by memory. Per-sample sin distortion. **Port risk: HIGH. DO NOT PORT.**

**Pyewacket** — Cosine-blended waveshaper / exciter. **Not a reverb.** 3 doubles of state. Skip.

**Hombre** — 2-tap modulated delay (~2000 samples). **Not a reverb**, flanger / pitch effect. Skip.

**DustBunny, Acceleration, ResEQ, DubPlate** — not reverbs. Skip.

**kWoodRoom** — Algorithmic, dual 3x3 / 6x6 nested delay-matrix banks with Bezier undersampling, golden-ratio feedback scaling. ~40 KB (3x3) + ~70 KB (6x6) per channel → roughly **220 KB stereo**. Bezier undersampling reduces effective per-sample cost. **Port risk: MEDIUM** — kVerb-family code style is dense, but structure is conventional FDN + early reflections.

**kCathedral** (original) — Early-reflection bank (9 arrays, ~10 KB/ch) + 25 main delay arrays + pre-delay + VLF pre-delay. **~190 KB/ch (~380 KB stereo).** Householder matrices, ~8-12 branches/sample. **Port risk: MEDIUM.** Smaller than Galactic, structurally similar.

**kCathedral5** — Latest entry, dual 3x3/6x6 banks (same architecture as kWoodRoom but tuned for a "2094-seat arena"). ~835 KB per instance. **Port risk: MEDIUM.**

**kGuitarHall** — 25 small delays + 15K pre-delay + 11K VLF pre-delay. ~668 KB stereo. Hall character, guitar-tuned. **Port risk: MEDIUM.**

## DON'T-PORT list

- **PocketVerbs** — 4.2 MB stereo, 4-way unit-level topology switch, per-sample sin distortion. Largest reverb in the catalog. Hard no.
- **MV** — 3.6 MB stereo + 26-case per-sample switch on `stage`. Exact "runtime-branched DSP dispatch" pattern documented as crashing Cortex-A8 even when each branch is individually safe.
- **MatrixVerb** — Vibrato-modulated reads on 8 delay lines with linear interp + crossfade between interpolated/non-interpolated outputs is structurally close to CloudSeed `ModulatedAllpass`. 1.24 MB stereo. **Highest CloudSeed-trap risk in the catalog.**
- **Reverb** (namesake) — Same modulated-multi-tap pattern as MatrixVerb, somewhat smaller; still high crash surface for marginal aesthetic gain over Galactic.
- **Verbity** — Functionally redundant with Galactic at the same memory cost. **Verbity2** — 1.5 MB stereo, no aesthetic gain to justify it.
- **VerbSixes / VerbThic** — Larger VerbTiny siblings, no aesthetic gain.
- **GalacticVibe, Pyewacket, Hombre, DustBunny, Acceleration, ResEQ, DubPlate** — not reverbs.
- **kCathedral2/3/4** — intermediate iterations in the kCathedral family; pick kCathedral or kCathedral5, not the in-between revs.

## CloudSeed-trap audit (top 3 only)

None of the recommended top 3 contain:
- Init-order-dependent first-frame DSP (Airwindows arrays init to zero and only fill on first write; counters start at 0)
- `std::vector` resizes (all arrays are fixed-size C arrays in the `.h`)
- Host-BPM or sample-rate-per-block dependencies (sample rate is read in `processReplacing`, not in a constructor callback)
- Modulated allpass chains (Galactic / Galactic2 / kWoodRoom use fixed-tap reads; only Galactic's tiny 256-sample pre-delay is modulated)

## Implementation notes

- All Airwindows reverbs use `double` internally. Port means converting per-sample work to `float` for AM335x (consistent with our existing units; `double` is murder on Cortex-A8 NEON).
- Source layout: `Plugins/MacVST/<Name>/source/*.cpp` + `*.h`. Per-sample work is in `processReplacing()` and `processDoubleReplacing()`. Take from the float path and replace `double` with `float` where possible.
- Per CloudSeed lesson: **write a hardware smoke-test loop before binding to a real unit** — a minimal Lua + C++ harness that just constructs + calls `process()` for 100 blocks and reports success. Catch first-frame hangs immediately, not after the wrapper is built.
- All Airwindows code is MIT, no licensing concern.

## Recommended port order

1. **Galactic** first — proves the porting harness, gives us the "lush" bucket immediately, lowest risk.
2. **Galactic2** second — minor variation of (1), proves the family-port pattern.
3. **kWoodRoom** third — independent topology, covers the wooden-room bucket, slightly higher risk so worth doing last.

Suggested package home: spreadsheet (musical / decorated tier alongside Pecto, Petrichor, Network, etc.).

## Source

Research conducted 2026-05-30 against `airwindows/airwindows@master` on GitHub. Per-sample DSP read from the actual `.cpp` and `.h` files under `Plugins/MacVST/<Name>/source/`. Memory footprints computed from array declarations.
