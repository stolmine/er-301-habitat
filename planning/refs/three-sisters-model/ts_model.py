"""Three Sisters ZDF SVF model (importable, no side effects).
Mirrors three_sisters_svf.c. Single-SVF resonance on LOW/HIGH, dual on CENTRE."""
import numpy as np

FS = 48000.0

class SVF:
    __slots__ = ('ic1', 'ic2')
    def __init__(self): self.ic1 = 0.0; self.ic2 = 0.0
    def tick(self, v0, fc, Q=None, nl=False, k=None):
        g = np.tan(np.pi * fc / FS)
        if k is None: k = 1.0 / Q
        a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2
        v3 = v0 - self.ic2
        v1 = a1 * self.ic1 + a2 * v3
        v2 = self.ic2 + a2 * self.ic1 + a3 * v3
        self.ic1 = np.tanh(2.0 * v1 - self.ic1) if nl else 2.0 * v1 - self.ic1
        self.ic2 = 2.0 * v2 - self.ic2
        return v2, v1, v0 - k * v1 - v2          # lp, bp, hp

def run_block(block, mode, x, fc_low, fc_high, q,
              q2=0.7071, anti=0.0, nl=False, fc_freq=None):
    if fc_freq is None: fc_freq = np.sqrt(fc_low * fc_high)
    s1, s2 = SVF(), SVF()
    y = np.empty_like(x)
    for n in range(len(x)):
        v = x[n]
        if block == 'LOW':
            lp1, bp1, hp1 = s1.tick(v, fc_low, q, nl)
            lp2, bp2, hp2 = s2.tick(lp1, fc_low, q2)
            main = lp2 if mode == 'XOVER' else hp2; comp = hp1
        elif block == 'HIGH':
            lp1, bp1, hp1 = s1.tick(v, fc_high, q, nl)
            lp2, bp2, hp2 = s2.tick(hp1, fc_high, q2)
            main = hp2 if mode == 'XOVER' else lp2; comp = lp1
        else:  # CENTRE
            clo, chi = (fc_low, fc_high) if mode == 'XOVER' else (fc_freq, fc_freq)
            lp1, bp1, hp1 = s1.tick(v, clo, q, nl)
            lp2, bp2, hp2 = s2.tick(hp1, chi, q, nl)
            main = lp2; comp = lp1 + hp2
        y[n] = main + anti * comp
    return y

def block_magnitude(block, mode, fc_low, fc_high, q, N=1 << 16, **kw):
    x = np.zeros(N); x[0] = 1.0
    y = run_block(block, mode, x, fc_low, fc_high, q, **kw)
    H = np.fft.rfft(y)
    f = np.fft.rfftfreq(N, 1.0 / FS)
    return f, np.abs(H)
