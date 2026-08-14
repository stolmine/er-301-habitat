#!/usr/bin/env python3
"""Imprinting: derive the mask and rank order from input B, apply the resulting
permutation to input A. Order transfer, not spectral transfer.

This is the "sort image X with the mask from image Y" case. The content is
always A's - B only ever supplies WHERE things go, never what they sound like.
"""
import numpy as np
import shear

SR = 48000
SEG_MS = 20.0
LOCAL_S = 1.0


def segment_rms(B, seg):
    n = B.shape[1] // seg
    return np.array([[np.sqrt(np.mean(B[b, s * seg:(s + 1) * seg] ** 2))
                      for s in range(n)] for b in range(B.shape[0])])


def runs_of(mask):
    """(start, end) of each contiguous True run."""
    out = []
    s = None
    for i, v in enumerate(mask):
        if v and s is None: s = i
        elif not v and s is not None: out.append((s, i)); s = None
    if s is not None: out.append((s, len(mask)))
    return out


def build_permutation(rms_src, thresh=1.2, local_s=LOCAL_S, seg_ms=SEG_MS,
                      descending=False, cap=None):
    """From a source's per-band segment RMS, produce, per band, a list of
    (run_slice, order) where order is the rank permutation of that run.

    A RELATIVE mask: absolute thresholds never close on dense material, so runs
    would run to seconds and degenerate into a fixed grid.
    """
    nb, nseg = rms_src.shape
    k = max(1, int(local_s / (seg_ms / 1000.0)))
    ker = np.ones(k) / k
    perms = []
    for b in range(nb):
        loc = np.convolve(rms_src[b], ker, 'same')
        mask = rms_src[b] > thresh * loc
        out = []
        for (a, z) in runs_of(mask):
            if cap: z = min(z, a + cap)
            if z - a < 2: continue
            key = rms_src[b, a:z]
            order = np.argsort(key, kind='stable')
            if descending: order = order[::-1]
            out.append((a, z, order))
        perms.append(out)
    return perms


def apply_permutation(A_bands, perms, seg, xfade_ms=2.0, sr=SR):
    """Emit A's segments in the order the permutation dictates, per band.

    Splices are INSIDE a band, so the signal there is narrowband and a short
    equal-power crossfade suffices - which is the whole reason for a filterbank
    rather than an FFT.
    """
    nb = A_bands.shape[0]
    xf = max(2, int(xfade_ms * 1e-3 * sr))
    w_in = np.sin(np.linspace(0, np.pi / 2, xf))
    w_out = np.cos(np.linspace(0, np.pi / 2, xf))
    out = np.zeros(A_bands.shape[1])

    for b in range(nb):
        lane = A_bands[b]
        acc = lane.copy()                       # untouched outside the runs
        for (a, z, order) in perms[b]:
            # snapshot the run's segments BEFORE writing, so the permutation
            # reads from the original lane and not from partially rewritten data
            src = [lane[(a + i) * seg:(a + i) * seg + seg].copy()
                   for i in range(z - a)]
            for i, oi in enumerate(order):
                d0 = (a + i) * seg
                piece = src[oi]
                acc[d0:d0 + seg] = piece
                if i > 0:                        # blend against what we just emitted
                    prev_tail = src[order[i - 1]][-xf:]
                    acc[d0:d0 + xf] = piece[:xf] * w_in + prev_tail * w_out
        out += acc
    return out
