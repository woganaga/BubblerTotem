"""Offline analysis of a BubblerTotem mic capture (WAV + DSP metadata CSV).

Replicates the firmware's beat-detection pipeline (AudioInput.cpp) on the
recorded audio and compares it against what the device logged frame by
frame, so divergences point at the exact broken stage:
  signal quality -> onset envelope -> autocorrelation -> tempo pick -> PLL.

Usage:
  py analyze.py captures/20260710-183000 [--bpm 160]

--bpm is the ground-truth tempo of the test material, if known; the report
then says how far both the device and the offline replica landed from it.

Outputs a text report to stdout and plots (envelope, ACF, bpm/conf traces,
spectrogram) as PNGs into the capture folder.
"""

import argparse
import csv
import math
import sys
import wave
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---- constants mirroring AudioInput.cpp ------------------------------------
FFT_SAMPLES = 512
FLUX_HI_BIN = 64
ONSET_HIST = 256
MIN_BPM, MAX_BPM = 60.0, 180.0


def load_wav(path):
    with wave.open(str(path), "rb") as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2
        rate = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    return rate, data.astype(np.float64)


def load_meta(path):
    header = {}
    rows = []
    acf_rows = []
    section = "rows"
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#ACF"):
                section = "acf"
                continue
            if line.startswith("#"):
                for kv in line[1:].split():
                    if "=" in kv:
                        k, v = kv.split("=")
                        header[k] = float(v)
                continue
            cells = line.split(",")
            if cells[0] in ("ms", "lag"):
                continue
            (rows if section == "rows" else acf_rows).append([float(c) for c in cells])
    return header, np.array(rows), np.array(acf_rows)


def firmware_pipeline(samples, rate, gain):
    """Replicate processFrame(): Hann FFT -> log-compressed flux -> onset env.

    The WAV stores raw>>16 while the firmware works on raw>>8, so scale
    samples x256 to be in firmware units.
    """
    scale = (2.0 ** ((gain - 1) / 14.0)) / 8388608.0
    x = samples * 256.0
    n_frames = len(x) // FFT_SAMPLES
    window = np.hanning(FFT_SAMPLES)
    flux = np.zeros(n_frames)
    prev = np.zeros(FFT_SAMPLES // 2)
    spec = np.zeros((n_frames, FFT_SAMPLES // 2))
    for i in range(n_frames):
        frame = x[i * FFT_SAMPLES:(i + 1) * FFT_SAMPLES]
        frame = frame - frame.mean()
        mag = np.abs(np.fft.rfft(frame * window))[: FFT_SAMPLES // 2]
        spec[i] = mag
        m = np.log1p(mag * scale * 10.0)
        d = m[1:FLUX_HI_BIN] - prev[1:FLUX_HI_BIN]
        flux[i] = d[d > 0].sum()
        prev = m
    # local-mean subtract + rectify (EMA alpha 0.05)
    flux_mean = np.zeros(n_frames)
    fm = 0.0
    for i in range(n_frames):
        fm = fm * 0.95 + flux[i] * 0.05
        flux_mean[i] = fm
    onset = np.maximum(flux - flux_mean, 0.0)
    return flux, flux_mean, onset, spec


def normalized_acf(env, fps):
    """Zero-mean, variance-normalized ACF over the tempo lag range (as computeTempo)."""
    env = env[-ONSET_HIST:] if len(env) >= ONSET_HIST else env
    n = len(env)
    mu = env.mean()
    var = ((env - mu) ** 2).mean()
    min_lag = max(2, round(fps * 60.0 / MAX_BPM))
    max_lag = min(round(fps * 60.0 / MIN_BPM), (n - 1) // 2)
    hi = min(max_lag * 4 + 2, n - 1)  # through 4x the beat lag, for comb harmonics
    r = np.zeros(hi + 1)
    if var < 1e-12:
        return r, min_lag, max_lag
    d = env - mu
    for lag in range(max(1, min_lag - 1), hi + 1):
        r[lag] = (d[lag:] * d[:-lag]).mean() * len(d) / (len(d) - lag) / var
    return r, min_lag, max_lag


def pick_tempo(r, min_lag, max_lag, fps):
    """Harmonic-weighted pick + parabolic interpolation (as computeTempo)."""
    best, best_lag = -1e9, 0
    for lag in range(min_lag, max_lag + 1):
        score = r[lag] + 0.5 * r[lag * 2]
        lg = math.log2((fps * 60.0 / lag) / 120.0)
        score *= math.exp(-0.5 * lg * lg)
        if score > best:
            best, best_lag = score, lag
    if best_lag <= 0:
        return 0.0, 0.0
    y0, y1, y2 = r[best_lag - 1], r[best_lag], r[best_lag + 1]
    denom = y0 - 2 * y1 + y2
    frac = 0.5 * (y0 - y2) / denom if abs(denom) > 1e-9 else 0.0
    frac = max(-0.5, min(0.5, frac))
    peak_r = y1 - 0.25 * (y0 - y2) * frac
    bpm = fps * 60.0 / (best_lag + frac)
    while bpm < MIN_BPM:
        bpm *= 2
    while bpm > MAX_BPM:
        bpm *= 0.5
    return bpm, max(0.0, min(1.0, peak_r))


def pick_tempo_comb(r, fps, hi=None):
    """Proposed picker: comb over a continuous BPM grid with interpolated ACF.

    Avoids integer-lag snapping entirely: candidate tempos whose beat period
    is fractional (160 BPM = lag 11.72 at 31.25fps) still get full credit
    for their 2/3/4-beat correlation peaks.
    """
    if hi is None:
        hi = len(r) - 1
    lags = np.arange(len(r))

    def r_lin(x):
        if x < 1 or x > hi:
            return None
        return float(np.interp(x, lags, r))

    weights = (1.0, 0.7, 0.5, 0.35)
    best_bpm, best_score, best_conf = 0.0, -1e9, 0.0
    for bpm in np.arange(MIN_BPM, MAX_BPM + 0.25, 0.5):
        tau = fps * 60.0 / bpm
        num, den = 0.0, 0.0
        for k, w in enumerate(weights, start=1):
            v = r_lin(k * tau)
            if v is None:
                break
            num += w * v
            den += w
        if den < 1.5:  # require at least the 1- and 2-beat lags
            continue
        score = num / den
        # Half-lag penalty: strong correlation at tau/2 means the envelope
        # also pulses between this candidate's beats - i.e. the true tempo
        # is probably 2x this candidate (the classic half-time impostor,
        # which for impulse-train input has perfectly aligned harmonics and
        # can't be told apart by the comb alone).
        half = r_lin(tau / 2.0)
        if half is not None and half > 0:
            score -= 0.5 * half
        lg = math.log2(bpm / 120.0)
        score *= math.exp(-0.5 * (lg / 1.5) ** 2)  # very wide prior, tie-break only
        if score > best_score:
            best_score, best_bpm, best_conf = score, bpm, num / den
    return best_bpm, max(0.0, min(1.0, best_conf))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture_dir", type=Path)
    ap.add_argument("--bpm", type=float, default=None, help="ground-truth BPM of the test material")
    args = ap.parse_args()

    wav_path = args.capture_dir / "mic_recording.wav"
    meta_path = args.capture_dir / "mic_meta.csv"
    rate, samples = load_wav(wav_path)
    header, meta, dev_acf = load_meta(meta_path)
    fps = rate / FFT_SAMPLES
    gain = header.get("gain", 50)

    print(f"=== Capture: {args.capture_dir} ===")
    print(f"settings: {', '.join(f'{k}={v:g}' for k, v in header.items())}")

    # ---- 1. signal quality ----
    peak = np.abs(samples).max()
    rms = math.sqrt((samples ** 2).mean())
    db = lambda v: 20 * math.log10(max(v, 1e-9) / 32768.0)
    clip_pct = 100.0 * np.mean(np.abs(samples) >= 32700)
    print("\n-- Signal quality (16-bit WAV domain) --")
    print(f"peak {peak:.0f} ({db(peak):+.1f} dBFS)   rms {rms:.1f} ({db(rms):+.1f} dBFS)   "
          f"clipping {clip_pct:.2f}%   DC {samples.mean():+.1f}")

    # ---- 2. offline pipeline replica ----
    flux, flux_mean, onset, spec = firmware_pipeline(samples, rate, gain)
    r, min_lag, max_lag = normalized_acf(onset, fps)
    bpm_off, conf_off = pick_tempo(r, min_lag, max_lag, fps)
    print("\n-- Offline replica of the firmware pipeline --")
    print(f"onset envelope: mean {onset.mean():.4f}  p95 {np.percentile(onset, 95):.4f}  "
          f"nonzero {100 * np.mean(onset > 0):.0f}%")
    print(f"tempo pick: {bpm_off:.1f} BPM  confidence {conf_off:.2f}")
    if args.bpm:
        print(f"ground truth {args.bpm:.0f} BPM -> offline error {bpm_off - args.bpm:+.1f}")

    bpm_comb, conf_comb = pick_tempo_comb(r, fps)
    print(f"proposed comb picker: {bpm_comb:.1f} BPM  confidence {conf_comb:.2f}")
    if args.bpm:
        print(f"ground truth {args.bpm:.0f} BPM -> comb error {bpm_comb - args.bpm:+.1f}")

    # sliding-window tempo traces (how each picker settles over time)
    trace_t, trace_bpm, trace_conf, trace_cbpm, trace_cconf = [], [], [], [], []
    for end in range(ONSET_HIST // 4, len(onset), 8):
        rw, lo, hi_l = normalized_acf(onset[:end], fps)
        b, c = pick_tempo(rw, lo, hi_l, fps)
        cb, cc = pick_tempo_comb(rw, fps)
        trace_t.append(end / fps)
        trace_bpm.append(b)
        trace_conf.append(c)
        trace_cbpm.append(cb)
        trace_cconf.append(cc)

    # ---- 3. device vs offline comparison ----
    print("\n-- Device (logged during capture) --")
    if len(meta):
        cols = "ms,flux,fluxMean,onset,bass,mid,treb,vol,beat,bpm,conf,phase".split(",")
        m = {c: meta[:, i] for i, c in enumerate(cols)}
        n_cmp = min(len(flux), len(m["flux"]))
        # frames may be offset by a frame or two; align by best cross-correlation of flux
        best_off, best_corr = 0, -2
        for off in range(-3, 4):
            a = flux[max(0, off):n_cmp + min(0, off)]
            b = m["flux"][max(0, -off):n_cmp - max(0, off)]
            k = min(len(a), len(b))
            if k > 16:
                c = np.corrcoef(a[:k], b[:k])[0, 1]
                if c > best_corr:
                    best_corr, best_off = c, off
        print(f"frames logged {len(meta)}  flux corr(device,offline) {best_corr:.3f} at offset {best_off}")
        print(f"device bpm: start {m['bpm'][0]:.0f} -> end {m['bpm'][-1]:.0f}  "
              f"(median {np.median(m['bpm']):.0f})")
        print(f"device conf: median {np.median(m['conf']):.2f}  max {m['conf'].max():.2f}")
        print(f"device beat flags: {int(m['beat'].sum())} in {len(meta) / fps:.1f}s")
        if args.bpm:
            print(f"ground truth {args.bpm:.0f} BPM -> device end error {m['bpm'][-1] - args.bpm:+.1f}")
    else:
        print("(no per-frame rows logged!)")

    # ---- 4. plots ----
    t = np.arange(len(flux)) / fps
    fig, axes = plt.subplots(4, 1, figsize=(12, 12))
    axes[0].imshow(np.log1p(spec.T[:FLUX_HI_BIN]), origin="lower", aspect="auto",
                   extent=[0, t[-1] if len(t) else 1, 0, FLUX_HI_BIN * rate / FFT_SAMPLES])
    axes[0].set_title("Spectrogram (flux band)")
    axes[0].set_ylabel("Hz")

    axes[1].plot(t, flux, lw=0.7, label="flux (offline)")
    axes[1].plot(t, flux_mean, lw=0.7, label="local mean")
    axes[1].plot(t, onset, lw=0.7, label="onset (rectified)")
    if len(meta):
        tm = (m["ms"] - m["ms"][0]) / 1000.0
        axes[1].plot(tm, m["flux"], lw=0.7, ls="--", label="flux (device)")
        for i in np.where(m["beat"] > 0)[0]:
            axes[1].axvline(tm[i], color="r", alpha=0.15)
    if args.bpm and len(t):
        for k in range(int(t[-1] * args.bpm / 60) + 1):
            axes[1].axvline(k * 60 / args.bpm, color="g", alpha=0.12, ls=":")
    axes[1].legend(fontsize=8)
    axes[1].set_title("Onset envelope (red=device beat flags, green dots=ground-truth grid)")

    lags = np.arange(len(r))
    valid = lags >= 1
    axes[2].plot(lags[valid], r[valid], marker="o", ms=3, lw=0.8, label="offline ACF")
    if len(dev_acf):
        axes[2].plot(dev_acf[:, 0], dev_acf[:, 2], marker="x", ms=4, lw=0.8, label="device ACF")
    axes[2].axvspan(min_lag, max_lag, color="g", alpha=0.08)
    for mult, style in ((1.0, "-"), (2.0, "--")):
        if args.bpm:
            axes[2].axvline(fps * 60 / (args.bpm / mult), color="g", ls=style, alpha=0.5)
    axes[2].set_title("Normalized ACF vs lag (green lines: ground-truth beat + 2-beat lags)")
    axes[2].legend(fontsize=8)

    axes[3].plot(trace_t, trace_bpm, label="offline bpm (current fw)")
    axes[3].plot(trace_t, np.array(trace_conf) * 100, label="offline conf x100 (current fw)")
    axes[3].plot(trace_t, trace_cbpm, lw=2, label="offline bpm (proposed comb)")
    axes[3].plot(trace_t, np.array(trace_cconf) * 100, lw=1, label="offline conf x100 (comb)")
    if len(meta):
        axes[3].plot(tm, m["bpm"], ls="--", label="device bpm")
        axes[3].plot(tm, m["conf"] * 100, ls="--", label="device conf x100")
    if args.bpm:
        axes[3].axhline(args.bpm, color="g", alpha=0.4)
    axes[3].set_title("Tempo & confidence over time")
    axes[3].set_xlabel("s")
    axes[3].legend(fontsize=8)

    fig.tight_layout()
    out = args.capture_dir / "analysis.png"
    fig.savefig(out, dpi=110)
    print(f"\nplots -> {out}")


if __name__ == "__main__":
    main()
