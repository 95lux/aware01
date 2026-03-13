from scipy.io import wavfile
from scipy.signal import hilbert
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import plotstyle
from scipy.ndimage import uniform_filter1d

wav_dir = Path(__file__).resolve().parent / "reverb_measurements/decay_meas"
files = {
    1.0: {
        0.442: wav_dir / "rev_decay_20ms_whitenoise_fb=comb0.442,ap=0.309_size=1-bounce-1.wav",
        0.879: wav_dir / "rev_decay_20ms_whitenoise_fb=comb0.879,ap=0598_size=1-bounce-2.wav",
        0.975: wav_dir / "rev_decay_20ms_whitenoise_fb=comb0.975,ap=0.679,_size=1-bounce-1.wav",
    },
    0.3: {
        0.442: wav_dir / "rev_decay_20ms_whitenoise_fb=comb0.442,ap=0.309_size=0.3-bounce-1.wav",
        0.879: wav_dir / "rev_decay_20ms_whitenoise_fb=comb0.879,ap=0598_size=0.3-bounce-1.wav",
        0.975: wav_dir / "rev_decay_20ms_whitenoise_fb=comb0.975,ap=0.679,_size=0.3-bounce-1.wav",
    },
}

size_labels = {1.0: "Room Size 1.0 (max)", 0.3: "Room Size 0.3 (min)"}
fb_labels = {0.442: "fb=0.442 (min)", 0.879: "fb=0.879", 0.975: "fb=0.975 (max)"}

linestyles = ['-', '--', ':']
fig, axes = plt.subplots(1, 2, figsize=(10, 4), sharey=True)

def smooth_envelope(envelope, sr, cutoff_hz=10):
    alpha = 1 - np.exp(-2 * np.pi * cutoff_hz / sr)
    out = np.zeros_like(envelope)
    state = 0.0
    for i, x in enumerate(envelope):
        state = state + alpha * (x - state)
        out[i] = state
    return out

def trim_to_noise_floor(envelope_db, threshold_db=-60):
    above = np.where(envelope_db > threshold_db)[0]
    if len(above) == 0:
        return slice(0, len(envelope_db))
    return slice(0, above[-1])


linestyles = [
    (0, ()),           # solid
    (0, (8, 10)),       # dashed  — longer dash, bigger gap
    (0, (1, 10)),       # dotted  — dot, bigger gap
]

for ax, (size, size_files) in zip(axes, files.items()):
    for (fb, fname), ls in zip(size_files.items(), linestyles):
        sr, data = wavfile.read(fname)
        if data.dtype != np.float32:
            data = data.astype(np.float32) / np.max(np.abs(data))
        if data.ndim == 2:
            data = data[:, 0]

        envelope = np.abs(hilbert(data))
        envelope = envelope / np.max(envelope)  # normalize BEFORE smoothing
        envelope = smooth_envelope(envelope, sr, cutoff_hz=3)
        envelope = np.clip(envelope, 10**(-60/20), 1.0)
        envelope_db = 20 * np.log10(envelope)

        envelope = np.abs(hilbert(data))
        envelope = smooth_envelope(envelope, sr, cutoff_hz=3)


        # trim at -60 dB relative to 0 dB peak
        sl = trim_to_noise_floor(envelope_db, threshold_db=-60)
        envelope_db = envelope_db[sl]
        t = np.arange(len(envelope_db)) / sr

        ax.plot(t, envelope_db, linestyle=ls, color='black',
        linewidth=0.9, label=fb_labels[fb])

    ax.set_xlabel("Time (s)")
    ax.set_title(size_labels[size])
    ax.set_ylim(-65, 2)
    ax.legend(frameon=True, fancybox=False, edgecolor='black', facecolor='white')

axes[0].set_ylabel("Amplitude (dB)")
plt.tight_layout()


plt.tight_layout()
script_dir = Path(__file__).resolve().parent
plt.savefig(script_dir / "../images/schroeder_reverb/fig_reverb_envelope.pdf", bbox_inches="tight")
plt.savefig(script_dir / "output/fig_reverb_envelope.png", bbox_inches="tight")
plt.show()