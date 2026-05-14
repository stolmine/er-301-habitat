#pragma once

// Visadhara — clean-room percussion macro voice based on the public BIA
// technical manual (manuals.noiseengineering.us/bia). Six tonal + one noise
// oscillator across three modes (Skin / Liquid / Metal). Phase 2: Skin mode
// + threshold-reflection folder + tri-mode Attack + LCG noise oscillator.
//
// All virtual implementations defined inline in this header per
// feedback_no_out_of_line_virtuals — vtable must be COMDAT-linked, immune
// to firmware-vs-package vtable drift. Internal struct is fully inlined
// here as well so process() can see it.
//
// Architecture: see planning/bia-clone-scoping.md.
// Implementation plan: see planning/visadhara-initial-pass.md.

#include <od/objects/Object.h>
#include <od/config.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#else
#include "jf/neon_shim.h"
#endif

#include "visadhara/voice.h"
#include "visadhara/morph.h"
#include "visadhara/folder.h"
#include "visadhara/pmm.h"

namespace stolmine
{

  class Visadhara : public od::Object
  {
  public:
#ifndef SWIGLUA
    struct Internal
    {
      float phase[8];
      float env[8];
      float ampScale[8];
      float decayScale[8];
      float freqMult[8];

      float prevTrig = 0.0f;

      // Trigger event counter — bumped on every rising edge in
      // process(). Polled by VisadharaCoronaGraphic each frame to
      // detect new triggers and fire the Phase-3d shockwave bands.
      int triggerCount = 0;

      float slowAttack = 0.0f;
      float slowAttackInc = 0.0f;

      // Post-fold final envelope. Mirrors the per-voice AR shape with
      // decayScale=1 (independent of harmonic-position decay scaling).
      // Per BIA designer note: re-applying a master envelope after the
      // folder restores the dynamics that folding compresses. Tracked
      // separately from the voice envelopes so future tuning (separate
      // decay coeff, different attack shape) can target the post-fold
      // amplitude without affecting the per-voice bus.
      float finalEnv = 0.0f;

      // Liquid mode: per-VOICE pitch envelope. Replaced the prior
      // global scalar `pitchEnv` so each voice can bend with its own
      // depth (kSweepWeight) and time constant (kSweepTauMs) — see
      // visadhara/voice.h. Per-voice asymmetric sweep gives the
      // fundamental a clear bend gesture while upper voices stay
      // timbrally stable: punch from figure/ground contrast rather
      // than lockstep harmonic-block shift.
      //
      // pitchEnvLanes[i]:        per-voice envelope state, 1.0 on
      //                          trigger, decays toward 0
      // pitchEnvCoeffLanes[i]:   per-voice decay multiplier, block-
      //                          rate from kSweepTauMs (0 if no bend)
      // pitchSweepGainLanes[i]:  block-rate liquidAmt *
      //                          kSweepWeight[i] * kPitchSweepPeak
      // Per-sample: pitchEnvLanes *= pitchEnvCoeffLanes (NEON), then
      // sweepLanes = 1 + pitchSweepGainLanes * pitchEnvLanes,
      // then phase += freqMult * sweepLanes (per voice).
      float pitchEnvLanes[8];
      float pitchEnvCoeffLanes[8];
      float pitchSweepGainLanes[8];

      // (Audio-waveform capture removed at 2.6.2.23 when Corona
      // pivoted from transient oscilloscope to spirograph/arabesque
      // geometric viz. If audio-derived viz returns — envelope
      // tracker, peak follower, etc. — restore vizBuf[N] + counter
      // here and capture in process(). For now the geometric viz
      // reads param state directly from public Parameter members.)

      // Metal mode PMM state, NEON-friendly layout (replaces former
      // visadhara_pmm::Voice pmm1/pmm2 scalar structs). Op index outer,
      // pair-lane inner. Lanes 0/1 carry pair A/B; lanes 2/3 are
      // padding (sized [4] not [2] for natural NEON 16-byte contiguity
      // and to satisfy vld1q_f32). Coefficients (incs + fb/mod) are
      // packed by the block-rate setup so the per-sample tick2() just
      // loads them — no scalar-to-lane moves in the hot path.
      //
      // Always ticked every sample so smooth Mode crossfade works
      // without per-sample dispatch (feedback_runtime_branched_dsp_dispatch).
      float pmmPhase[3][4];
      float pmmLastOut[3][4];
      float pmmIncPacked[3][4];
      float pmmFbModPacked[3][4];

      Internal()
      {
        memset(phase, 0, sizeof(phase));
        memset(env, 0, sizeof(env));
        memset(ampScale, 0, sizeof(ampScale));
        memset(decayScale, 0, sizeof(decayScale));
        // 8 voices now active. memset to zero is safe — the first
        // block-rate freqMult assignment in process() overwrites with
        // the bake-in per-sample freq increment (baseFreq * ratio *
        // detune * invSrOs).
        memset(freqMult, 0, sizeof(freqMult));
        memset(pmmPhase, 0, sizeof(pmmPhase));
        memset(pmmLastOut, 0, sizeof(pmmLastOut));
        memset(pmmIncPacked, 0, sizeof(pmmIncPacked));
        memset(pmmFbModPacked, 0, sizeof(pmmFbModPacked));
        memset(pitchEnvLanes, 0, sizeof(pitchEnvLanes));
        // Coeffs and gains are block-rate computed before first use.
        memset(pitchEnvCoeffLanes, 0, sizeof(pitchEnvCoeffLanes));
        memset(pitchSweepGainLanes, 0, sizeof(pitchSweepGainLanes));
      }
    };
#endif

    Visadhara()
    {
      addInput(mTrigger);
      addInput(mVOct);
      addOutput(mOut);
      addParameter(mHarmonic);
      addParameter(mSpread);
      addParameter(mMorph);
      addParameter(mFold);
      addParameter(mAttack);
      addParameter(mDecay);
      addParameter(mLevel);
      addParameter(mPitch);
      addParameter(mMode);
      addOption(mModeSnap);
      addParameter(mOctave);
      mModeSnap.enableSerialization();
      mpInternal = new Internal();
    }

    virtual ~Visadhara()
    {
      delete mpInternal;
    }

#ifndef SWIGLUA
    od::Inlet mTrigger{"Trigger"};
    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};

    od::Parameter mHarmonic{"Harmonic", 0.5f};
    od::Parameter mSpread{"Spread", 0.0f};
    od::Parameter mMorph{"Morph", 0.0f};
    od::Parameter mFold{"Fold", 0.0f};
    od::Parameter mAttack{"Attack", 0.0f};
    od::Parameter mDecay{"Decay", 0.5f};
    od::Parameter mLevel{"Level", 0.7f};
    od::Parameter mPitch{"Pitch", 110.0f};

    od::Parameter mMode{"Mode", 0.0f};
    od::Option    mModeSnap{"ModeSnap", 1};
    // Octave became a Parameter (was od::Option) so it can be CV-modulated
    // and round-tripped via ParameterAdapter Bias. Integer values 1/2/3
    // map to Bass / Alto / Tenor; the Lua sub-readout uses DialMap integer
    // snap + addThresholdLabel for text display. Rounded via clamp+cast
    // at the use site.
    od::Parameter mOctave{"Octave", 2.0f};

    // Trigger event counter for the Corona viz (Phase-3d shockwave
    // bands). Incremented on every rising edge in process(); the
    // graphic polls this each frame to detect new triggers. Plain
    // int — a missed or double-counted frame is visually irrelevant.
    int vizTriggerCount() const { return mpInternal->triggerCount; }

    // Post-fold master envelope level (0..1) for the Corona viz —
    // 1.0 on trigger, decays toward 0. Drives the Fold contour
    // field's live "breathing": creases dig deeper as it rises,
    // relax as it decays. Same PIMPL-read pattern as vizTriggerCount.
    float vizEnvLevel() const { return mpInternal->finalEnv; }

    __attribute__((optimize("no-tree-vectorize")))
    virtual void process()
    {
      Internal &s = *mpInternal;
      const int frames = FRAMELENGTH;
      float *outBuf = mOut.buffer();
      float *trigBuf = mTrigger.buffer();
      float *vOctBuf = mVOct.buffer();

      // ---- Block-rate parameter reads ----
      // Raw user harmonic position (0..1). Used directly across all
      // voice-distribution / detune / sweep-weight lerps below — the
      // new Harmonic semantics (post-2.6.2.12) are linear from
      // fundamental-cluster to harmonic-series with correlated
      // detune collapse, so no remap compression is wanted. PMM
      // Metal mode keeps its own use of harmonicPosUser unchanged.
      const float harmonicPosUser = mHarmonic.value();
      const float harmonicPos = harmonicPosUser;
      const float spreadPos   = mSpread.value();
      const float morphPos    = mMorph.value();
      const float foldPos     = mFold.value();
      const float attackPos   = mAttack.value();
      const float decayPos    = mDecay.value();
      const float level       = mLevel.value();
      const float basePitch   = mPitch.value();

      // Round + clamp the Parameter value to integer 1..3 (Bass / Alto /
      // Tenor). CV modulation can push outside the nominal range; clamp
      // saturates to the nearest valid octave rather than wrapping.
      int octIdx = (int)(mOctave.value() + 0.5f);
      if (octIdx < 1) octIdx = 1;
      if (octIdx > 3) octIdx = 3;
      const float octShift = (octIdx == 1) ? -2.0f
                           : (octIdx == 3) ? +2.0f
                           : 0.0f;

      const float voctV = vOctBuf[0];
      const float pitchInOct = voctV + octShift;
      const float baseFreq = basePitch * powf(2.0f, pitchInOct);

      // Time constants integrate per-half-sample at 2× internal rate
      // (see per-sample loop below — DSP runs in a 2× k-iteration shell
      // with a 2-tap MA decimator at output, per Ngoma / Helicase
      // precedent). All sample-count denominators double; per-step phase
      // increments halve via invSrOs.
      const float decayTimeSamples = (decayPos * decayPos * 96000.0f + 480.0f) * 2.0f;
      const float decayCoeff = expf(-1.0f / decayTimeSamples);

      const float invSr   = 1.0f / globalConfig.sampleRate;
      const float invSrOs = invSr * 0.5f;     // per-half-sample at 2× rate

      // 8 voices active. NEON 4-lane × 2 processes all 8 lanes.
      //
      // Voice-distribution semantics (2.6.2.12 redesign):
      //   harmonicPos = 0: all 8 voices land on 1× (fundamental
      //                    cluster) with FULL detune across them
      //                    — fat chorused sub, maximum kick weight.
      //   harmonicPos = 1: voices land on harmonic / prime series
      //                    (via spread_mult), detune COLLAPSED to
      //                    zero — clean integer ratios, harmonically
      //                    pure timbre.
      //   intermediate:    linear lerp on both axes.
      //
      // All voices always at amp=1 / decayScale=1 — Harmonic doesn't
      // gate voice activation, just distribution + detune amount.
      // Punch naturally falls as Harmonic rises (voices spreading
      // away from fundamental → smaller coherent peak at the sub).
      //
      // freqMult is baked at block-rate to hold the full per-sample
      // freq INCREMENT (baseFreq × ratio × detune × invSrOs).
      const float baseFreqInvSrOs = baseFreq * invSrOs;
      const float detuneAmt = 1.0f - harmonicPos;
      for (int i = 0; i < 8; i++)
      {
        // Effective ratio: lerp from 1.0 (cluster) to spread-driven
        // harmonic / prime series.
        const float seriesRatio = visadhara::spread_mult(i, spreadPos);
        const float effectiveRatio = 1.0f + harmonicPos * (seriesRatio - 1.0f);
        // Effective detune: full kVoiceDetune at H=0, collapses to 1.0
        // (no detune) at H=1.
        const float effectiveDetune =
          1.0f + (visadhara::kVoiceDetune[i] - 1.0f) * detuneAmt;
        s.freqMult[i]   = baseFreqInvSrOs * effectiveRatio * effectiveDetune;
        s.ampScale[i]   = 1.0f;
        s.decayScale[i] = 1.0f;
      }

      // Block-rate morph crossfade weights. Equal-power (sqrt) curves
      // for level-flat sweep across the four shape anchors. Computed
      // once here; per-sample sample_w() is branchless.
      const visadhara_morph::Weights morphW =
        visadhara_morph::compute_weights(morphPos);

      // ---- Phase 2 block-rate setup ----
      // Folder is drive-based (Buchla 259/281 topology): fixed threshold,
      // variable input drive. Drive curve in folder.h.
      const float foldDrive    = visadhara_folder::drive_from_fold(foldPos);
      const float foldThreshold = 1.0f;
      const float postFoldGain = visadhara_folder::post_fold_gain(foldPos);

      // ---- Phase 3 block-rate setup: Mode dispatch + Liquid sweep ----
      // Mode parameter is continuous 0..2: 0=Skin, 1=Liquid, 2=Metal.
      // Block-rate dispatch builds three mode-amount masks; the Liquid
      // path's pitch-envelope-driven frequency boost is gated by
      // liquidAmt, and the bus output is gated by (skinAmt + liquidAmt)
      // so Metal territory currently silences (Phase 4 wires Metal in).
      const float modeRaw = mMode.value();
      const int modeSnapVal = mModeSnap.value();        // 1=smooth, 2=snap
      const bool snapMode = (modeSnapVal == 2);
      float skinAmt, liquidAmt, metalAmt;
      if (snapMode)
      {
        // Hard snap: nearest-mode dispatch.
        int m = (int)(modeRaw + 0.5f);
        if (m < 0) m = 0; if (m > 2) m = 2;
        skinAmt   = (m == 0) ? 1.0f : 0.0f;
        liquidAmt = (m == 1) ? 1.0f : 0.0f;
        metalAmt  = (m == 2) ? 1.0f : 0.0f;
      }
      else
      {
        // Smooth: tent function across adjacent modes.
        float c = modeRaw < 0.0f ? 0.0f : (modeRaw > 2.0f ? 2.0f : modeRaw);
        skinAmt   = (c < 1.0f) ? (1.0f - c) : 0.0f;
        liquidAmt = (c < 1.0f) ? c : ((c < 2.0f) ? (2.0f - c) : 0.0f);
        metalAmt  = (c > 1.0f) ? (c - 1.0f) : 0.0f;
      }
      // Liquid pitch-sweep peak: +2 octaves at full weight (voice 0).
      // Combined with the tight per-voice time constants in voice.h
      // (kSweepTauMs ~25 ms on voice 0), this gives a clicky
      // percussive bend that settles fast and deep — punchy transient
      // gesture, not a sustained slide. Classic kick-drum pitch
      // sweeps in literature range 2-3 octaves; we sit at the lower
      // end to keep the bend musical against the harmonic series.
      const float kPitchSweepPeak = 2.0f;
      const float skinLiquidPresence = skinAmt + liquidAmt;

      // Per-voice pitch-envelope coefficients and gains. Block-rate
      // precompute into Internal arrays so the per-sample NEON loop
      // just vld1q_f32s them (feedback_neon_voice_bus_template Layer 9).
      //
      // Voice-distribution-aware (2.6.2.12): at harmonicPos = 0 all
      // voices bend in lockstep at the fundamental (weight = 1.0
      // and unified tau), since they're all clustered at 1×. At
      // harmonicPos = 1 the asymmetric kSweepWeight + kSweepTauMs
      // arrays apply (voice 0 carries the gesture, upper voices
      // tapered). Linear lerp between.
      //
      //   effectiveWeight = lerp(1.0, kSweepWeight[i], harmonicPos)
      //   effectiveTau    = lerp(kUnifiedTauMs, kSweepTauMs[i], harmonicPos)
      const float kUnifiedTauMs = 25.0f;   // tau at H=0 (fundamental cluster)
      const float sr2 = globalConfig.sampleRate * 2.0f;
      for (int i = 0; i < 8; i++)
      {
        const float effectiveWeight =
          1.0f + harmonicPos * (visadhara::kSweepWeight[i] - 1.0f);
        s.pitchSweepGainLanes[i] =
          liquidAmt * effectiveWeight * kPitchSweepPeak;

        const float effectiveTauMs =
          kUnifiedTauMs + harmonicPos * (visadhara::kSweepTauMs[i] - kUnifiedTauMs);
        s.pitchEnvCoeffLanes[i] = (effectiveTauMs > 0.0f)
          ? expf(-1.0f / (effectiveTauMs * 0.001f * sr2))
          : 0.0f;
      }

      // ---- Phase 4 block-rate setup: Metal mode PMM ----
      // Two 3-op PMM pairs. Each pair's operator ratios are
      // continuously blended between a clean integer-harmonic anchor
      // (Harmonic CCW) and the natural inharmonic / metallic values
      // (Harmonic CW). Spread additionally boosts pair 2's FM indices
      // for more chaotic character at higher Spread settings.
      //
      //   Pair 1 anchors:  [1, 2, 3]      natural: [1.0, 1.5, 2.0]
      //   Pair 2 anchors:  [1, 2, 4]      natural: [1.7, 2.3, 3.5]
      const float h = harmonicPosUser;     // 0..1 raw
      const float pmm1_r1 = 1.0f + (1.0f - 1.0f) * h;       // ratio op1 (always 1)
      const float pmm1_r2 = 2.0f + (1.5f - 2.0f) * h;       // 2.0 → 1.5
      const float pmm1_r3 = 3.0f + (2.0f - 3.0f) * h;       // 3.0 → 2.0
      const float pmm2_r1 = 1.0f + (1.7f - 1.0f) * h;       // 1.0 → 1.7
      const float pmm2_r2 = 2.0f + (2.3f - 2.0f) * h;       // 2.0 → 2.3
      const float pmm2_r3 = 4.0f + (3.5f - 4.0f) * h;       // 4.0 → 3.5

      const float pmm1_inc1 = baseFreq * pmm1_r1 * invSrOs;
      const float pmm1_inc2 = baseFreq * pmm1_r2 * invSrOs;
      const float pmm1_inc3 = baseFreq * pmm1_r3 * invSrOs;
      const float pmm1_fb    = 0.30f;
      const float pmm1_mod12 = 0.60f;
      const float pmm1_mod23 = 0.80f;

      const float pair2Boost = 1.0f + spreadPos * 1.5f;
      const float pmm2_inc1 = baseFreq * pmm2_r1 * invSrOs;
      const float pmm2_inc2 = baseFreq * pmm2_r2 * invSrOs;
      const float pmm2_inc3 = baseFreq * pmm2_r3 * invSrOs;
      const float pmm2_fb    = 0.30f * pair2Boost;
      const float pmm2_mod12 = 0.60f * pair2Boost;
      const float pmm2_mod23 = 0.80f * pair2Boost;

      // Pack PMM coefficients into NEON-friendly layout for tick2().
      // Lanes 0,1 = pair A,B; lanes 2,3 stay zero (initial from ctor).
      // Done once per block; per-half-sample tick2() just vld1q_f32s
      // these arrays — no scalar-to-lane moves on the hot path.
      s.pmmIncPacked[0][0] = pmm1_inc1;  s.pmmIncPacked[0][1] = pmm2_inc1;
      s.pmmIncPacked[1][0] = pmm1_inc2;  s.pmmIncPacked[1][1] = pmm2_inc2;
      s.pmmIncPacked[2][0] = pmm1_inc3;  s.pmmIncPacked[2][1] = pmm2_inc3;
      s.pmmFbModPacked[0][0] = pmm1_fb;     s.pmmFbModPacked[0][1] = pmm2_fb;
      s.pmmFbModPacked[1][0] = pmm1_mod12;  s.pmmFbModPacked[1][1] = pmm2_mod12;
      s.pmmFbModPacked[2][0] = pmm1_mod23;  s.pmmFbModPacked[2][1] = pmm2_mod23;

      // Metal bus perceptual gain: PMM produces a denser waveform with
      // higher peak-to-RMS ratio than the Skin additive bus, so it
      // measures perceptually louder at the same nominal peak. Scale
      // down to match Skin/Liquid loudness.
      const float kMetalBusGain = 0.35f;

      // Attack semantic:
      //   attackPos > +0.05 → slow ramp (linear AR rise time)
      //   |attackPos| ≤ 0.05 → instant attack
      //   attackPos < -0.05 → instant attack + cross-mode injection
      //                       (Metal bus into Skin/Liquid, additive bus
      //                       into Metal), amount proportional to
      //                       |attackPos|. See injection block below.
      const bool attackSlow  = (attackPos > +0.05f);
      // 2× sample count so the ramp's wall-clock fill time matches the
      // single-rate value when stepped per-half-sample.
      const float slowAttackTimeSamples =
        attackSlow ? (attackPos * 0.2f * globalConfig.sampleRate * 2.0f) : 0.0f;
      const float injectMix = (attackPos < 0.0f) ? -attackPos : 0.0f;

      const float useSlowMask  = (s.slowAttackInc > 0.0f) ? 1.0f : 0.0f;
      const float useDecayMask = 1.0f - useSlowMask;

      // Voice-bus perceptual gain. 8 voices sum unchecked into the bus
      // for the additive "large" character. 0.375 (= 0.5 * 6/8) brings
      // the 8-voice sum into the same pre-drive range the 6-voice scalar
      // version targeted at voiceGain=0.5. Tunable by audition.
      const float voiceGain = 0.375f;

      // ---- Per-sample inner loop ----
      for (int i = 0; i < frames; i++)
      {
        const float trigNow = trigBuf[i];
        const bool risingEdge = (trigNow > 0.5f) && (s.prevTrig <= 0.5f);
        s.prevTrig = trigNow;

        if (risingEdge)
        {
          // Viz: report this trigger to the Corona graphic.
          s.triggerCount++;

          // Phase reset to 0 — coherent quarter-cycle peak preserved
          // for full loudness (peak N, RMS N/√2). The "splatty"
          // artifact at coherent settings is NOT a phase issue but
          // a folder-overdrive issue: the unbounded coherent peak
          // drove fold-stage harmonic generation past Nyquist.
          // Solution lives at the folder input via pre_fold_cap()
          // — see below. Phase decoherence (golden-ratio offsets in
          // voice.h) is preserved as a code path but unused; the
          // RMS √N vs N/√2 loudness gap (2× perceptually) is too
          // expensive to pay if we can fix the symptom upstream.
          for (int n = 0; n < 8; n++) s.phase[n] = 0.0f;

          if (attackSlow)
          {
            for (int n = 0; n < 8; n++) s.env[n] = 0.0f;
            s.slowAttack = 0.0f;
            s.slowAttackInc = (slowAttackTimeSamples > 0.0f)
                                ? (1.0f / slowAttackTimeSamples)
                                : 1.0f;
            // Final env rides the slowAttack ramp alongside voices.
            s.finalEnv = 0.0f;
          }
          else
          {
            for (int n = 0; n < 8; n++) s.env[n] = 1.0f;
            s.slowAttack = 1.0f;
            s.slowAttackInc = 0.0f;
            s.finalEnv = 1.0f;
          }

          // Liquid mode pitch envelope: kick all 8 lanes to 1.0 on
          // rising edge. Each lane decays at its own per-voice rate
          // (pitchEnvCoeffLanes) toward 0; per-voice gain
          // (pitchSweepGainLanes) controls how much that lane's
          // pitchEnv translates into actual frequency bend.
          for (int n = 0; n < 8; n++) s.pitchEnvLanes[n] = 1.0f;

          // Metal mode PMM phase reset — same coherent-attack discipline
          // as Skin/Liquid voices. Zero phase + lastOut so feedback
          // doesn't hang carryover from the previous trigger.
          memset(s.pmmPhase, 0, sizeof(s.pmmPhase));
          memset(s.pmmLastOut, 0, sizeof(s.pmmLastOut));
        }

        // 2× oversampling shell: the full per-half-sample DSP body runs
        // twice per output sample, generating osSamp[0] and osSamp[1].
        // 2-tap MA decimator at the bottom averages them into outBuf[i].
        // Matches Ngoma / Helicase precedent. The folder + voice morph
        // generate substantial HF content; oversampling pushes the
        // resulting harmonics' alias mirror out of audible band.
        float osSamp[2];
        for (int k = 0; k < 2; k++)
        {

        // Slow-attack ramp: always advance, clamp at 1, reset Inc on hit
        // (single one-shot store; not differential heavy work). Once Inc
        // is 0 the next block flips useSlowMask to 0 and the env
        // transitions cleanly to the decay path.
        s.slowAttack += s.slowAttackInc;
        if (s.slowAttack > 1.0f)
        {
          s.slowAttack = 1.0f;
          s.slowAttackInc = 0.0f;
        }

        // Per-voice Liquid pitch-sweep is computed in each voice-loop
        // NEON pass (sweepLanes = 1 + gain × pitchEnv, where both
        // gain and pitchEnv are per-voice). pitchEnv lanes decay
        // there too. No scalar pitchSweep value at this scope —
        // each pass loads its own 4-lane vector.

        // ---- 8-voice bus, NEON 4-lane × 2 passes ----
        // Phase advance + wrap + env update + morph eval + accumulate,
        // all in parallel across 4 voices per pass. Two passes cover
        // all 8 lanes. Horizontal sum at the end produces the scalar
        // `sample` value the rest of the per-half-sample chain expects.
        //
        // NEON discipline (per feedback_neon_intrinsics_drumvoice +
        // feedback_neon_hint_surfaces):
        //   - All loaded arrays (phase, freqMult, decayScale, ampScale,
        //     env) are heap-allocated class members on Internal. No
        //     stack-locals, no alignas.
        //   - sample_w_4 is inlined; weights broadcast inside it so
        //     block-rate scalars don't have to remain live across calls.
        //   - Two passes are structurally identical; no runtime branch
        //     across them.
        float sample;
        {
          // sampleAcc is the only quad that survives across both passes
          // (accumulator) and across the sample_w_4 call within each
          // pass. Single quad spill at most — well below the trap
          // threshold. All other broadcasts are pass-local so they die
          // at scope close before sample_w_4 is invoked, per
          // feedback_neon_hint_surfaces "rebroadcast inside each pass
          // body" guidance.
          float32x4_t sampleAcc = vdupq_n_f32(0.0f);

          // Pass 1: lanes 0-3. Block-rate scalars named locally inside
          // the pass scope (not hoisted across the morph call). With
          // freqMult baked at block-rate to hold the full per-sample
          // freq increment, the phase advance collapses to a single
          // vmla.
          //
          // Per-voice pitch sweep (Liquid mode):
          //   pe_lanes *= coeff_lanes              (per-voice decay)
          //   sweepLanes = 1 + gain_lanes * pe_lanes
          //   p += fm * sweepLanes
          // Each lane bends at its own depth (kSweepWeight) and tau
          // (kSweepTauMs) — fundamental carries the gesture, upper
          // voices stay timbrally stable.
          {
            const float32x4_t decayCoeffV   = vdupq_n_f32(decayCoeff);
            const float32x4_t oneV          = vdupq_n_f32(1.0f);
            const float32x4_t zeroV         = vdupq_n_f32(0.0f);
            const float32x4_t slowAttackV   = vdupq_n_f32(s.slowAttack);
            const float32x4_t useSlowMaskV  = vdupq_n_f32(useSlowMask);
            const float32x4_t useDecayMaskV = vdupq_n_f32(useDecayMask);

            float32x4_t p  = vld1q_f32(&s.phase[0]);
            float32x4_t fm = vld1q_f32(&s.freqMult[0]);
            float32x4_t ds = vld1q_f32(&s.decayScale[0]);
            float32x4_t as = vld1q_f32(&s.ampScale[0]);
            float32x4_t e  = vld1q_f32(&s.env[0]);

            // Per-voice pitch envelope decay + sweep computation.
            float32x4_t pe = vmulq_f32(vld1q_f32(&s.pitchEnvLanes[0]),
                                        vld1q_f32(&s.pitchEnvCoeffLanes[0]));
            vst1q_f32(&s.pitchEnvLanes[0], pe);
            float32x4_t sweepLanes = vmlaq_f32(
              oneV,
              vld1q_f32(&s.pitchSweepGainLanes[0]),
              pe);

            // Phase advance: p += freqMult * sweepLanes
            p = vmlaq_f32(p, fm, sweepLanes);
            uint32x4_t wmask = vcgeq_f32(p, oneV);
            float32x4_t wrap = vbslq_f32(wmask, oneV, zeroV);
            p = vsubq_f32(p, wrap);
            vst1q_f32(&s.phase[0], p);

            float32x4_t voiceCoeff = vmulq_f32(decayCoeffV, ds);
            float32x4_t decayPath  = vmulq_f32(e, voiceCoeff);
            e = vaddq_f32(vmulq_f32(slowAttackV, useSlowMaskV),
                          vmulq_f32(decayPath, useDecayMaskV));
            vst1q_f32(&s.env[0], e);

            // Pre-multiply e*as so neither has to survive the morph
            // call. Across-call live quads then reduce to {p (arg),
            // eAs, sampleAcc} — within Cortex-A8 callee-saved budget.
            float32x4_t eAs = vmulq_f32(e, as);
            float32x4_t shaped = visadhara_morph::sample_w_4(p, morphW);
            sampleAcc = vmlaq_f32(sampleAcc, shaped, eAs);
          }

          // Pass 2: lanes 4-7 — same body, rebroadcast block-rate
          // scalars locally so they die at scope close.
          {
            const float32x4_t decayCoeffV   = vdupq_n_f32(decayCoeff);
            const float32x4_t oneV          = vdupq_n_f32(1.0f);
            const float32x4_t zeroV         = vdupq_n_f32(0.0f);
            const float32x4_t slowAttackV   = vdupq_n_f32(s.slowAttack);
            const float32x4_t useSlowMaskV  = vdupq_n_f32(useSlowMask);
            const float32x4_t useDecayMaskV = vdupq_n_f32(useDecayMask);

            float32x4_t p  = vld1q_f32(&s.phase[4]);
            float32x4_t fm = vld1q_f32(&s.freqMult[4]);
            float32x4_t ds = vld1q_f32(&s.decayScale[4]);
            float32x4_t as = vld1q_f32(&s.ampScale[4]);
            float32x4_t e  = vld1q_f32(&s.env[4]);

            float32x4_t pe = vmulq_f32(vld1q_f32(&s.pitchEnvLanes[4]),
                                        vld1q_f32(&s.pitchEnvCoeffLanes[4]));
            vst1q_f32(&s.pitchEnvLanes[4], pe);
            float32x4_t sweepLanes = vmlaq_f32(
              oneV,
              vld1q_f32(&s.pitchSweepGainLanes[4]),
              pe);

            p = vmlaq_f32(p, fm, sweepLanes);
            uint32x4_t wmask = vcgeq_f32(p, oneV);
            float32x4_t wrap = vbslq_f32(wmask, oneV, zeroV);
            p = vsubq_f32(p, wrap);
            vst1q_f32(&s.phase[4], p);

            float32x4_t voiceCoeff = vmulq_f32(decayCoeffV, ds);
            float32x4_t decayPath  = vmulq_f32(e, voiceCoeff);
            e = vaddq_f32(vmulq_f32(slowAttackV, useSlowMaskV),
                          vmulq_f32(decayPath, useDecayMaskV));
            vst1q_f32(&s.env[4], e);

            float32x4_t eAs = vmulq_f32(e, as);
            float32x4_t shaped = visadhara_morph::sample_w_4(p, morphW);
            sampleAcc = vmlaq_f32(sampleAcc, shaped, eAs);
          }

          // Horizontal sum: 4 lanes -> scalar via the standard pairwise
          // cascade. vpadd_f32 sums adjacent lanes; two cascaded passes
          // collapse a 4-lane vector to a scalar (lane 0 of the final
          // 2-lane vector). Same pattern Pecto + JF use.
          float32x2_t pairSum = vpadd_f32(vget_low_f32(sampleAcc),
                                           vget_high_f32(sampleAcc));
          float32x2_t total   = vpadd_f32(pairSum, pairSum);
          sample = vget_lane_f32(total, 0);
        }

        // Master post-fold envelope: same AR shape as the voices but
        // decayScale=1, decoupled from the per-voice harmonic-position
        // decay scaling. Per BIA designer note: re-applied to the post-
        // fold signal to restore dynamics that folding compresses.
        const float finalDecayPath = s.finalEnv * decayCoeff;
        const float finalSlowPath  = s.slowAttack;
        s.finalEnv = finalSlowPath * useSlowMask + finalDecayPath * useDecayMask;

        // Metal PMM tick — NEON 2-lane-across-pairs. Always run so
        // smooth mode crossfade works without per-sample dispatch
        // (heavy work outside conditionals). Morph sweeps each
        // operator's waveshape (sine→tri→saw→sq) through the FM chain
        // — saw-FM and square-FM produce dramatically different
        // timbres than sine-FM.
        float pmmA, pmmB;
        visadhara_pmm::tick2(s.pmmPhase, s.pmmLastOut,
                              s.pmmIncPacked, s.pmmFbModPacked,
                              morphW.w_sin, morphW.w_tri,
                              morphW.w_saw, morphW.w_sq,
                              pmmA, pmmB);
        const float metalBus = (pmmA + pmmB) * kMetalBusGain;

        // Mode-blended source. Skin/Liquid additive bus uses voiceGain
        // to bring its 6-voice sum (peak ~6) into ~peak 2; Metal PMM
        // bus is loudness-normalized (kMetalBusGain) before mixing.
        const float skinLiquidPart = sample * voiceGain;
        const float modeBus = skinLiquidPresence * skinLiquidPart
                            + metalAmt * metalBus;

        // Cross-mode injection (negative-Attack region). Replaces the
        // simple white-noise injection: when in Skin/Liquid territory
        // we inject the Metal bus (PMM at the current panel settings);
        // when in Metal we inject the Skin/Liquid bus. Smooth blend
        // through the crossfade region. Free CPU since both buses are
        // already computed branchlessly.
        const float injectionSignal = skinLiquidPresence * metalBus
                                    + metalAmt * skinLiquidPart;

        // Mix: mode bus + cross-mode injection, then drive into folder.
        const float bussed = modeBus + injectionSignal * injectMix;

        // Pre-fold soft cap. Bounds coherent-peak transients so the
        // folder doesn't generate splatty / aliased harmonic content
        // when 8 voices stack near the fundamental and align at the
        // quarter-cycle peak. Asymptotic form (no discontinuity, no
        // hard-clip aliasing). Models the natural drive-stage
        // saturation in analog wavefolder circuits. Inactive on
        // small signals (linear pass-through below ~T/2).
        const float bussed_capped = visadhara_folder::pre_fold_cap(bussed);
        const float preFold = bussed_capped * foldDrive;

        const float folded = visadhara_folder::fold(preFold, foldThreshold);
        const float pulse = visadhara_folder::pulse_mix(folded, foldPos, foldThreshold);
        const float foldedSig = folded + pulse;

        // Post-fold compensation gain: brings unfolded signal up to
        // perceived parity with folded signal (gentle, max 2.5× at
        // fold=0, unity at fold=1).
        const float compensated = foldedSig * postFoldGain;

        // Re-apply global envelope per the BIA designer note about
        // restoring post-fold dynamics. Uses the dedicated finalEnv
        // tracked separately from the per-voice envelope bus.
        const float postEnv = compensated * s.finalEnv;

        // Master soft saturator: catches residual peaks at extreme
        // settings (Metal + max fold + cross-injection) without
        // introducing dynamics artifacts. Output bounded ±1.
        osSamp[k] = visadhara_folder::master_sat(postEnv * level);
        }   // end 2× k-loop

        // 2-tap MA decimator. Matches Ngoma's pattern from
        // project_ngoma_codex; sufficient for percussion content. A
        // higher-order halfband FIR is a future refinement only if
        // residual aliasing is audible after 2×.
        outBuf[i] = 0.5f * (osSamp[0] + osSamp[1]);
      }
    }
#endif

  private:
    Internal *mpInternal = nullptr;
  };

} // namespace stolmine
