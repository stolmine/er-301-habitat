# Loopback level calibration - locked standard

Established 2026-07-17 for the compound DSP module profiling. This is the fixed
signal chain and reference level every capture is taken against.

## Rig

- Interface: MOTU M4 (USB, 48 kHz, 24-bit), HiFi profile.
  - Send: `HiFi__Line2__sink` = MOTU outputs 3-4; **out 3 -> 301 input**.
  - Return: `HiFi__Line5__source` = MOTU inputs 3-4; **euro->line converter -> in 3-4**.
- 48 kHz throughout (the 301's native rate). No SR conversion in the loop.
- The euro->line converter drives **both legs as a balanced stereo pair**, so L
  and R read the same; analysis meters channel 1.
- Send chain: MOTU out 3 -> 301 input -> 301 gain (VCA) -> 301 output ->
  euro->line converter -> MOTU in 3-4.

## Locked operating point

| | value |
|---|---|
| Reference tone | 1 kHz sine, **-1 dBFS peak / -4 dBFS RMS** (`cal_1k_hot.wav`) |
| 301 input VCA | **x5** (provisional - see note) |
| Return level | **-21.3 dBFS RMS** / -18.3 dBFS peak |
| Round-trip offset | **-17.3 dB** (return below send) |
| Noise floor | **~-78 dBFS RMS** |
| SNR | **~56.6 dB** single-shot |
| Headroom on return | ~18 dB to 0 dBFS |

The -17.3 dB round-trip offset is the **euro->line converter's fixed
attenuation** (it knocks euro level down to line level). It is a known,
documented gain, not a defect. Absolute DUT-output level = return level + this
offset.

## Gain-staging lesson (why these settings)

- **Send hot, amplify minimally.** 301 noise scales with the VCA but not with
  send level, so a loud sine buys SNR dB-for-dB while extra VCA gain just
  amplifies the 301's own noise. Boosting the VCA to x20 to chase a level made
  the noise floor jump; a hot send at x5 fixed it.
- **Unity round-trip is NOT the target.** Equal send/return RMS would need ~x35
  VCA (deep into the noise) purely to fight the converter's -17 dB attenuation.
  Abandoned. A known, clean, repeatable level with headroom is what matters.
- **The VCA's real job is euro drive to the DUT, not this loopback number.** x5
  is provisional; finalize it when the module is patched (drive it at a sensible
  euro level, watch how hard it pushes the onboard distortion) and re-record the
  number here.

## Noise fingerprint (keep in mind during analysis)

The residual noise is **narrowband interference, not broadband hiss** (broadband
floor sits ~40 dB below these peaks). Not chased - the user's call 2026-07-17,
since the ESS-sweep deconvolution rejects stationary interference and the
harmonic bins we read for distortion sit clear of these frequencies.

- **50 Hz mains hum at ~-1 dB** (+ 120 / 240 Hz) = ground loop between MOTU and
  the eurorack.
- **~5.2 kHz + 7.7 kHz digital whine** = USB / switching-supply / digital hash.

Analysis hygiene: notch or disregard 50 / 120 / 240 Hz and ~5.2 kHz; read
distortion harmonics at their exact bins (2k, 3k, 4k...) which fall between the
interference. If a future capture needs it clean: ground-loop isolator on the
euro->line return for the hum, different USB port / ferrite for the whine.

## Harness

- `gen_tones.sh` - regenerate calibration tones (48k stereo, gitignored WAVs).
- `meter.py` / `meter.sh` - live Line5 peak/RMS meter (robust; not ecasignalview).
- `calibrate.sh <tone.wav>` - play tone -> capture return -> print stats.
- `thd.py <return.wav>` - THD + harmonic breakdown (chain distortion floor check).
- All target the MOTU nodes explicitly (WirePlumber's default source is unreliable).
