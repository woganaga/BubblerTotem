# BubblerAnalyzer

Offline analysis of the totem's beat-detection pipeline, using synchronized
captures (raw mic WAV + per-frame DSP metadata CSV) pulled from the device
over WiFi.

## Workflow

1. Make sure the totem is on WiFi (`bubblertotem.local`) and, for musical
   tests, that the test material is playing at show volume.
2. Pull a capture:
   ```powershell
   .\capture.ps1 -Seconds 30          # clamped 5..30 by the firmware
   ```
   This triggers `/action/mic/record`, polls `/api/status`, and downloads
   `mic_recording.wav` + `mic_meta.csv` into `captures\<timestamp>\`.
3. Analyze (requires Python 3 + numpy + matplotlib):
   ```powershell
   py analyze.py captures\<timestamp> --bpm 160   # --bpm = ground truth, if known
   ```

`analyze.py` replicates the firmware pipeline (`AudioInput.cpp`: Hann FFT ->
log-compressed spectral flux -> conditioned onset envelope -> zero-mean
normalized autocorrelation -> harmonic-weighted tempo pick) on the recorded
audio and diffs it against what the device logged for the same instants:

- signal quality (peak/RMS dBFS, clipping, DC)
- onset envelope shape, offline vs device flux correlation
- ACF offline vs the device's final ACF snapshot
- tempo/confidence traces over time vs the device's, against ground truth

Plots land in the capture folder as `analysis.png`. Divergence between the
offline replica and the device log points at the broken stage; agreement
means the algorithm (not the implementation) needs work.

Captures are gitignored (a 30s WAV is ~1MB).
