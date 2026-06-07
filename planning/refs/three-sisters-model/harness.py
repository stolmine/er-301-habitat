import numpy as np
import matplotlib.pyplot as plt

FS = 48000.0

class SVF:
    def __init__(self): self.ic1 = 0.0; self.ic2 = 0.0
    def tick(self, v0, fc, Q=None, nl=False, k=None):
        g = np.tan(np.pi * fc / FS)
        if k is None: k = 1.0 / Q
        a1 = 1.0 / (1.0 + g * (g + k))
        a2 = g * a1; a3 = g * a2
        v3 = v0 - self.ic2
        v1 = a1 * self.ic1 + a2 * v3
        v2 = self.ic2 + a2 * self.ic1 + a3 * v3
        if nl:
            self.ic1 = np.tanh(2.0 * v1 - self.ic1)
        else:
            self.ic1 = 2.0 * v1 - self.ic1
        self.ic2 = 2.0 * v2 - self.ic2
        return v2, v1, v0 - k * v1 - v2          # lp, bp, hp

def run_block(block, mode, x, fc_low, fc_high, q, q2=0.7071, anti=0.0, nl=False, fc_freq=None):
    if fc_freq is None: fc_freq = np.sqrt(fc_low * fc_high)   # geometric FREQ center
    s1, s2 = SVF(), SVF()
    y = np.zeros_like(x)
    for n, v in enumerate(x):
        if block == 'LOW':
            lp1, bp1, hp1 = s1.tick(v, fc_low, q, nl)
            lp2, bp2, hp2 = s2.tick(lp1, fc_low, q2)
            main = lp2 if mode == 'XOVER' else hp2; comp = hp1
        elif block == 'HIGH':
            lp1, bp1, hp1 = s1.tick(v, fc_high, q, nl)
            lp2, bp2, hp2 = s2.tick(hp1, fc_high, q2)
            main = hp2 if mode == 'XOVER' else lp2; comp = lp1
        else:  # CENTRE: both resonant. xover -> hp@low, lp@high. formant -> both @FREQ
            if mode == 'XOVER':
                clo, chi = fc_low, fc_high
            else:
                clo = chi = fc_freq
            lp1, bp1, hp1 = s1.tick(v, clo, q, nl)
            lp2, bp2, hp2 = s2.tick(hp1, chi, q, nl)
            main = lp2; comp = lp1 + hp2
        y[n] = main + anti * comp
    return y

def mag_response(block, mode, fc_low, fc_high, q, **kw):
    N = 1 << 16
    x = np.zeros(N); x[0] = 1.0                  # impulse
    y = run_block(block, mode, x, fc_low, fc_high, q, **kw)
    H = np.fft.rfft(y)
    f = np.fft.rfftfreq(N, 1.0 / FS)
    return f, 20 * np.log10(np.abs(H) + 1e-9)

# ---- Figure 1: LOW/HIGH/CENTRE magnitude responses, xover + formant ----
fig, ax = plt.subplots(2, 2, figsize=(13, 8))
fc, span = 800.0, 2.2                            # span as a freq ratio factor
flo, fhi = fc / span, fc * span

for mode, col in zip(['XOVER', 'FORMANT'], [0, 1]):
    for blk, c in zip(['LOW', 'CENTRE', 'HIGH'], ['C0', 'C2', 'C3']):
        f, m = mag_response(blk, mode, flo, fhi, q=6.0)
        ax[0][col].semilogx(f, m, c, label=blk)
    ax[0][col].set_title(f'{mode}  (FREQ=800Hz, SPAN x2.2, Q=6)')
    ax[0][col].set_ylim(-48, 18); ax[0][col].set_xlim(20, 20000)
    ax[0][col].grid(True, which='both', alpha=0.3); ax[0][col].legend()
    ax[0][col].set_ylabel('dB')

# LOW: single-core resonance vs rising Q (matches bode_q: one peak at cutoff)
for q, c in zip([0.7, 2, 6, 18], ['C7', 'C0', 'C1', 'C3']):
    f, m = mag_response('LOW', 'XOVER', flo, fhi, q=q)
    ax[1][0].semilogx(f, m, c, label=f'Q={q}')
ax[1][0].axvline(flo, color='k', ls=':', alpha=0.5)
ax[1][0].set_title('LOW xover: single resonant core, rising Q\n(one peak at CF_low, dotted)')
ax[1][0].set_ylim(-60, 24); ax[1][0].set_xlim(20, 20000)
ax[1][0].grid(True, which='both', alpha=0.3); ax[1][0].legend(); ax[1][0].set_ylabel('dB')

# CENTRE xover: TWO resonant peaks (one per edge cutoff)
for q, c in zip([0.7, 2, 6, 18], ['C7', 'C0', 'C1', 'C3']):
    f, m = mag_response('CENTRE', 'XOVER', flo, fhi, q=q)
    ax[1][1].semilogx(f, m, c, label=f'Q={q}')
ax[1][1].axvline(flo, color='k', ls=':', alpha=0.5)
ax[1][1].axvline(fhi, color='k', ls=':', alpha=0.5)
ax[1][1].set_title('CENTRE xover: TWO resonant stages\n(peak at each edge, dotted)')
ax[1][1].set_ylim(-60, 24); ax[1][1].set_xlim(20, 20000)
ax[1][1].grid(True, which='both', alpha=0.3); ax[1][1].legend()

for a in ax[1]: a.set_xlabel('Hz')
plt.tight_layout(); plt.savefig('/home/claude/ts/responses.png', dpi=110)
print('saved responses.png')

# ---- Figure 2: self-oscillation purity (low-distortion signature) ----
# Slightly NEGATIVE damping (loop gain just over unity); tanh on the bp state
# bounds the growth. Small |k| -> small limit cycle -> tanh stays near-linear
# -> near-pure sine. This is the clean self-osc the analog core produces.
def self_osc(fc, k_neg=-2.5e-4, N=1 << 19, kick=0.5):
    s = SVF(); s.ic1 = kick                      # nudge to start oscillation
    y = np.zeros(N)
    for n in range(N):
        lp, bp, hp = s.tick(0.0, fc, nl=True, k=k_neg)
        y[n] = bp                                # resonant state = the sine
    return y

y = self_osc(363.0, k_neg=-1e-3, N=1 << 20)
seg = y[-32768:]; seg = seg - np.mean(seg)
win = np.hanning(len(seg))
Y = np.abs(np.fft.rfft(seg * win))
f = np.fft.rfftfreq(len(seg), 1.0 / FS)
mag = 20 * np.log10(Y / np.max(Y) + 1e-12)

fig2, (a1, a2) = plt.subplots(1, 2, figsize=(13, 4))
t = np.arange(400) / FS * 1000
a1.plot(t, seg[:400], 'C0'); a1.set_title('Self-osc waveform (slightly-neg damping, tanh-bounded)')
a1.set_xlabel('ms'); a1.set_ylabel('amp'); a1.grid(alpha=0.3)
a2.semilogx(f, mag, 'C3'); a2.set_title('Self-osc spectrum: near-pure sine')
a2.set_xlim(20, 20000); a2.set_ylim(-120, 3); a2.set_xlabel('Hz'); a2.set_ylabel('dB')
a2.grid(True, which='both', alpha=0.3)
plt.tight_layout(); plt.savefig('/home/claude/ts/selfosc.png', dpi=110)
print('saved selfosc.png')

# THD via harmonic-band energy (sum +/-2 bins around each harmonic)
pk = np.argmax(Y)
band = lambda k0: np.sum(Y[max(k0 - 2, 0):k0 + 3] ** 2)
fund = band(pk)
harm = sum(band(int(round(pk * h))) for h in range(2, 12) if int(round(pk * h)) < len(Y))
print(f'self-osc fundamental ~{f[pk]:.1f} Hz, THD {np.sqrt(harm / fund) * 100:.3f}%')
