"""Kryos phase-vocoder freeze - offline prototype.

Step 0 of planning/kryos-spectral-freeze.md. Settles the SOUND questions before
any build cycle: does regenerated phase sustain convincingly, do the movement
modes differ, and does Etherization behave as the reference describes.

Mirrors the intended C++: kFFT 1024, kHop 256, Hann, COLA overlap-add,
magnitude-only history of 32 frames, per-bin CONSTANT complex rotation for
phase (no trig in the synthesis loop).
"""
import numpy as np

SR   = 48000
NFFT = 1024
HOP  = NFFT // 4
BINS = NFFT // 2 + 1
HIST = 32                      # frames of magnitude history = 64 KB in C++
WIN  = np.hanning(NFFT + 1)[:NFFT]
COLA = 1.5                     # sum of Hann^2 at 75% overlap

def stft(x):
    n = 1 + (len(x) - NFFT) // HOP
    return np.array([np.fft.rfft(x[i*HOP:i*HOP+NFFT] * WIN) for i in range(n)])

def source(dur=3.0):
    """Steady harmonics + repeated transients, so Ether has both to sort."""
    t = np.arange(int(dur*SR)) / SR
    x = sum(np.sin(2*np.pi*220*k*t + k) / k for k in (1,2,3,4,5,6,7,9,11)) * 0.22
    rng = np.random.default_rng(4)
    # sustained broadband bed + scrape, so the window genuinely contains
    # non-tonal content for Ether to reject. Without it the test is vacuous.
    x += rng.normal(0, 0.10, len(t))
    x += 0.12*np.sin(2*np.pi*np.cumsum(700+600*np.sin(2*np.pi*3.1*t))/SR)
    for on in range(0, len(t), int(0.21*SR)):
        L = min(int(0.04*SR), len(t)-on)
        x[on:on+L] += np.exp(-np.arange(L)/(0.004*SR)) * rng.normal(0,.6,L)
    return x / np.max(np.abs(x)) * 0.7

# ---------------------------------------------------------------- capture
def capture(x, at):
    """Freeze at sample `at`. Returns magnitude history + per-bin rotation."""
    S = stft(x)
    f = min(max(at // HOP, HIST), len(S)-2)
    mags = np.abs(S[f-HIST+1 : f+1])                 # (HIST, BINS)

    # Rotation per bin. Two candidates, measured against each other below.
    #  A: bin-centre nominal advance, needs nothing stored.
    #  B: TRUE instantaneous frequency at the freeze instant. Costs one extra
    #     float per bin ONCE (2 KB), not per frame.
    nominal = 2*np.pi*np.arange(BINS)*HOP/NFFT
    dphi = np.angle(S[f]) - np.angle(S[f-1])
    dev  = np.mod(dphi - nominal + np.pi, 2*np.pi) - np.pi
    return mags, nominal, nominal + dev

# ---------------------------------------------------------------- movement
def positions(n, mode, rate, depth, offset):
    """Frame index into the history, per synthesis hop."""
    p = float(offset*(depth-1)); out=[]; d=1.0
    rng = np.random.default_rng(11)
    for i in range(n):
        out.append(p)
        step = rate
        if   mode=='forwards':  p += step
        elif mode=='backwards': p -= step
        elif mode=='alternating':
            p += step*d
            if p>=depth-1 or p<=0: d=-d; p=min(max(p,0),depth-1)
        elif mode=='randomwalk':
            if rng.random()<0.06: d=-d
            p += step*d
        elif mode=='randomskip':
            if i%8==0: p = rng.random()*(depth-1)
        if mode in ('forwards','backwards','randomwalk'):
            p = p % (depth-1) if depth>1 else 0.0
    return np.clip(np.array(out),0,depth-1)

# ---------------------------------------------------------------- ether
def etherize(mags, amount):
    """Drop bins judged transient. amount 1.0 = keep all, 0 = a few pure tones.

    Reference behaviour: lowering it "progressively drops out components of the
    sound which are judged to be 'transient'. The result tends towards a small
    number of pure tones which represent the STRONGEST harmonics."

    Two things a first attempt got wrong, both measured:

    1. Ranking on steadiness ALONE is not enough. 492 of 513 bins are noise
       floor and they score in a band 0.039 wide, so a quantile threshold spends
       its whole travel re-sorting inaudible bins and never reaches the loud
       ones. Score must be steadiness WEIGHTED BY LEVEL - "strongest" is in the
       spec, and a steady inaudible bin must not outrank a loud one.
    2. Thresholding by bin-count quantile is the wrong axis for the same
       reason. Keep the top N by score with N swept GEOMETRICALLY, so the
       control has resolution where it matters (the last few partials).

    Runs ONCE PER FREEZE over HISTxBINS, not per sample.
    """
    if amount >= 1.0: return mags
    m = mags.mean(0); sd = mags.std(0)
    steady = m / (m + sd + 1e-12)
    score  = steady * m
    n = max(2, int(round(2.0 * (BINS / 2.0) ** amount)))
    if n >= BINS: return mags
    thr = np.partition(score, -n)[-n]
    w = (score >= thr).astype(float)
    # 1-bin skirt so a surviving partial keeps its window shape and stays a
    # tone rather than becoming a click train.
    w = np.maximum(w, 0.5*np.roll(w,1)); w = np.maximum(w, 0.5*np.roll(w,-1))
    return mags * w

# ---------------------------------------------------------------- synth
def synth(mags, rot, nhop, mode='forwards', rate=0.0, depth=1,
          offset=0.0, ether=1.0, pitch=0.0):
    mags = etherize(mags, ether)
    rho = 2.0**(pitch/12.0)
    if pitch:
        # A partial that sat at bin k/rho now sits at bin k, and its rotation
        # scales with it. Remap BOTH or the two disagree and it smears.
        k = np.arange(BINS); src = k/rho
        # Bins whose source lies outside the spectrum must go SILENT, not clamp
        # to the edge value. np.interp (and a naive C++ clamp) holds the last
        # sample, which on a downshift fills the whole top half with a constant
        # smear louder than the shifted fundamental - measured as a +29 ST
        # error at -12 ST before this guard.
        valid = (src <= BINS-1) & (src >= 0)
        mags = np.array([np.where(valid, np.interp(src, k, m), 0.0) for m in mags])
        rot  = np.interp(src, k, rot) * rho
    pos  = positions(nhop, mode, rate, depth, offset)
    phase = np.zeros(BINS)
    out = np.zeros(nhop*HOP + NFFT)
    r = rot
    for i in range(nhop):
        f0 = int(np.floor(pos[i])); f1 = min(f0+1, len(mags)-1)
        a  = pos[i]-f0
        M  = mags[f0]*(1-a) + mags[f1]*a              # interpolated, not spliced
        phase += r
        frame = np.fft.irfft(M*np.exp(1j*phase), NFFT) * WIN
        out[i*HOP:i*HOP+NFFT] += frame
    return out/COLA

if __name__ == '__main__':
    x = source()
    mags, rotA, rotB = capture(x, int(1.5*SR))
    ref = np.abs(np.fft.rfft(x[int(1.5*SR)-NFFT:int(1.5*SR)]*WIN))
    ref = ref/ (ref.sum()+1e-12)

    def spec_match(y):
        n=NFFT; S=np.abs(np.fft.rfft(y[len(y)//2:len(y)//2+n]*WIN)); S=S/(S.sum()+1e-12)
        return float(np.sum(np.minimum(S,ref)))       # 1.0 = identical shape
    def rough(y):
        """Amplitude-envelope wobble: a good freeze is smooth, a bad one beats."""
        e=np.convolve(np.abs(y),np.ones(256)/256,'same')[NFFT:-NFFT]
        return float(np.std(e)/(np.mean(e)+1e-12))

    print("PHASE MODEL: bin-centre rotation vs stored instantaneous frequency")
    print(f"  {'model':<34}{'spectral match':>15}{'env wobble':>13}")
    for tag,r in (("A  bin-centre (stores nothing)",rotA),
                  ("B  true inst. freq (+2 KB once)",rotB)):
        y=synth(mags,r,120)
        print(f"  {tag:<34}{spec_match(y):15.3f}{rough(y):13.3f}")
