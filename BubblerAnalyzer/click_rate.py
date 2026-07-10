"""Measure a metronome capture's true click rate at sample precision,
independent of the beat-detection pipeline: rectified envelope -> peak pick
with a refractory window -> inter-click intervals.

Usage: py click_rate.py captures/<dir>
"""

import sys
import wave
from pathlib import Path

import numpy as np


def main():
    path = Path(sys.argv[1]) / "mic_recording.wav"
    with wave.open(str(path), "rb") as w:
        rate = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64)

    env = np.abs(x)
    # smooth with a 2ms box so a click is one hump
    k = int(rate * 0.002)
    env = np.convolve(env, np.ones(k) / k, mode="same")

    thresh = np.percentile(env, 99.5) * 0.5
    refractory = int(rate * 0.15)  # no metronome we care about exceeds 400 BPM

    clicks = []
    i = 0
    while i < len(env):
        if env[i] > thresh:
            j = min(i + refractory, len(env))
            peak = i + int(np.argmax(env[i:j]))
            clicks.append(peak)
            i = peak + refractory
        else:
            i += 1

    t = np.array(clicks) / rate
    iv = np.diff(t)
    iv = iv[(iv > 0.15) & (iv < 2.0)]
    if len(iv) < 4:
        print(f"only {len(clicks)} clicks found - not a clean metronome capture")
        return
    med = np.median(iv)
    # drop outliers (missed/double-detected clicks) before the mean
    good = iv[np.abs(iv - med) < 0.25 * med]
    bpm_med = 60.0 / med
    bpm_mean = 60.0 / good.mean()
    jitter_ms = 1000.0 * good.std()
    print(f"clicks detected: {len(clicks)} over {t[-1] - t[0]:.1f}s")
    print(f"interval median {med * 1000:.1f}ms -> {bpm_med:.2f} BPM")
    print(f"interval mean   {good.mean() * 1000:.1f}ms -> {bpm_mean:.2f} BPM  (jitter +/-{jitter_ms:.1f}ms)")
    print(f"intervals used {len(good)}/{len(iv)}")


if __name__ == "__main__":
    main()
