# Fabula downsampled tank (SR/2 recirculating core)

Goal: run the recirculating tank (AP1/D1/AP2/D2 + DC + damp + cross-feed) at
SR/2 = 24 kHz while keeping predelay, ER, input diffusion, and dry/wet mix at the
full 48 kHz host rate. Halves per-sample tank compute (the dominant cost) and
halves tank delay memory. Ledger item `fabula-am335x`, build 0.2.0.21.

## Why it's not a detuning
Halving the delay COUNTS while halving the RATE preserves the delay TIMES:
1087 smp @48k = 22.6 ms; 543 smp @24k = 22.6 ms. Reverb time, echo spacing, and
modal structure are preserved. The only intended sonic change is the tank's HF
bandwidth ceiling dropping to ~12 kHz (Nyquist@24k) - appropriate for a diffuse
tail; the damp filters already roll off highs. Density is preserved (same ratios).

## Signal flow (per host sample)
```
in -> predelay(48k) -> ER(48k) -> input diffusion 4xAP(48k) -> diffIn
diffIn -> [decimate 2:1, half-band] -> tankIn(24k)
if (tank tick, every 2nd host sample):
    tank core(24k): tankIn+feedback -> DC -> AP1 -> D1 -> damp -> AP2 -> D2
                    -> cross-feed saturate -> wet multi-tap sum -> wetL/R(24k)
    store prev/curr
wetL/R(48k) = interpolate(prev, curr)          # 2:1 linear reconstruction
out = dry*(1-mix) + (wet_interp + ER)*mix       # 48k
```
Walks (Brownian mod) tick at tank rate (inside the tick). Size-base smoothers and
the tank's fractional reads are all at tank rate. smPD/smEarly stay full rate.

## Constant scaling (48k -> 24k, halve delay counts, re-oddify, keep pow2 buffers)
| const | 48k | 24k |
|---|---|---|
| kTA1 / kTA1i | 1087 / 367 | 543 / 183 |
| kTA2 / kTA2i | 1471 / 491 | 735 / 245 |
| kTA1_size / kTA2_size | 2048 / 2048 | 1024 / 1024 |
| kD1_L_base / kD2_L_base | 7187 / 5101 | 3593 / 2551 |
| kD1_R_base / kD2_R_base | 6803 / 6343 | 3401 / 3171 |
| kD1_L_maxBase / size | 10781 / 16384 | 5391 / 8192 |
| kD2_L_maxBase / size | 7651 / 8192 | 3827 / 4096 |
| kD1_R_maxBase / size | 10205 / 16384 | 5103 / 8192 |
| kD2_R_maxBase / size | 9515 / 16384 | 4757 / 8192 |
| kMinExcursion / kMaxExcursion | 9 / 72 | 4.5 / 36 |
| kAPModMin / kAPModMax | 0.5 / 6.0 | 0.25 / 3.0 |
| kMinStep / kMaxStep | unchanged (drift-time preserved: excursion halves, step holds) |

Predelay (kPD) and ER (kER) buffers stay full-rate (unchanged).

## Rate-dependent coefficient remaps (pole-frequency preserved)
For a one-pole with pole p, cutoff ~ (1-p)*SR. Same cutoff in Hz at half SR needs
(1-p_24) = 2*(1-p_48):
- DC blocker: kDCBlockR 0.9995 -> 0.999.
- HF damp one-pole: coeff alpha_24 = 1-(1-alpha_48)^2 ~= 2*alpha_48 for small
  alpha. Applied to the mapping via kMinDampCoeff and the dampEff formula.
  TUNING-SENSITIVE: verify tail brightness by ear.

## Anti-alias / reconstruction filters (first cut)
- Decimation: 2-tap half-band average tankIn = 0.5*(diffIn[n]+diffIn[n-1]). Null
  at 24k Nyquist, -3 dB at 12k. Cheap; adequate for a diffused tail. UPGRADE PATH
  if aliasing is heard: 7- or 11-tap half-band FIR (user is aliasing-sensitive).
- Reconstruction: linear interp of the two tank outputs (smooth tail; 1-sample
  group delay). Half-band upsample is the upgrade.

## Open tuning items (need audition)
1. Damp remap brightness (the (1-a)^2 rule is approximate).
2. Decimation filter adequacy vs aliasing at high Size/Mod.
3. Modulation feel after excursion halving + step hold.
4. Wet level: multi-tap sum unchanged; verify perceived level after band-limiting.
