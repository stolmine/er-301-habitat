# Design note: Palimpsest - spectral cross-synthesis

Status: design note / not started. Ledger item `spectral-cross-synthesis`.

User request 2026-08-14, from the four-idea batch.

Named **Palimpsest**: a manuscript scraped and overwritten, where the earlier
text still shows through. Sits with Fabula, Sujet, Anamnesis in the literary
register rather than the mineral one.

Package: **spreadsheet**.

## What it is

Impose the spectral envelope of one signal on the fine structure of another.
Classic vocoding is one setting of that. The general case is more interesting:

- **A's envelope on B's structure** - the vocoder. Speech shapes a pad.
- **Morph** between the two envelopes rather than taking one outright.
- **Formant shift** - move A's envelope up or down the spectrum before applying
  it, decoupling perceived size from pitch.
- **Envelope from A, structure from A** at morph extremes, so the control sweeps
  continuously from "untouched" to "fully spoken."

There is no vocoder anywhere in the collection. Warps has one - and it is
**deliberately excluded**: the reference audit records that the vocoder region
above 0.7 was cut from the port, and `mi-clouds-warps-improvements` carries
restoring it as an open item. So the capability is absent by decision, not
oversight, and a dedicated unit is the better answer than reopening Warps.

## Mechanism

Two STFTs, or one STFT run over two inputs. Per bin: take the magnitude envelope
of the modulator (smoothed across bins - this smoothing *is* the formant
extraction), divide it by the carrier's own smoothed envelope, and apply the
resulting ratio as a gain to the carrier's bins. The carrier keeps its phase and
its fine structure; it acquires the modulator's gross spectral shape.

Two details decide whether it sounds like a vocoder or like mud:

- **Envelope smoothing width** is the band count. A wide smoothing kernel is a
  few fat bands and reads as a classic 8-band vocoder; a narrow one tracks
  individual partials and reads as spectral morphing. One control, two familiar
  effects, same code - the same structural trick that makes Nacre's Shift/Range
  work.
- **The carrier must be spectrally dense** or there is nothing to shape. Noisy
  or bright carriers work; sine waves do not. That is a documentation problem,
  not a DSP one, but it is the first thing every user will get wrong.

## The second input

The ER-301 gives a unit one chain input. The modulator arrives as a **mono
branch**, following `Warps.lua:49-53`, which does exactly this for its Modulator
inlet:

```lua
local mod = self:addObject("mod", app.ConstantGain())
mod:hardSet("Gain", 1.0)
mod:setClampInDecibels(-59.9)
self:addMonoBranch("mod", mod, "In", mod, "Out")
```

Proven pattern, already in the tree.

## Controls

| control | notes |
|---|---|
| **Morph** | 0 = carrier untouched, 1 = fully shaped by the modulator, CV |
| **Bands** | envelope smoothing width - vocoder at one end, morph at the other |
| **Shift** | formant shift of the modulator envelope, bipolar |
| **Speed** | envelope attack/release per bin |
| **Mix** | linear |

Plus the `mod` branch as its own control.

## Cautions

- **Freeze the modulator?** Holding the modulator's envelope while the carrier
  keeps moving is nearly free once the machinery exists and is a genuinely
  distinct effect. Worth a Hold sub-param, but only after the basic unit works.
- **Latency** is STFT hop latency in the audio path, and both inputs must be
  analysed on the same frame boundary or the result smears.
- **Silence in the modulator** should mean "no shaping," not "gain to zero" -
  decide this explicitly rather than letting the division decide it. Guard the
  divide.
- Morph = 0 must be a bit-identical bypass of the carrier, which again depends on
  a transparent STFT round trip - see `anneal-resonance-suppressor` phase 1 and
  `stft-frontend-atom`.

## Phases

1. **Round-trip transparency** (shared with Anneal, or inherited from the shared
   atom if that lands first).
2. **Modulator branch** and dual analysis on aligned frames.
3. **Envelope extraction and transfer.** Bands and Morph. This is the unit.
4. **Formant shift.**
5. **Speed, Mix, optional Hold.**
6. **Hardware.** Two analyses is roughly twice Anneal's cost; get a number early.
