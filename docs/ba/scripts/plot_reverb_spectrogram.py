from importlib.metadata import files
from scipy.io import wavfile
from scipy.signal import spectrogram
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import plotstyle

wav_dir = Path(__file__).resolve().parent / "reverb_measurements/decay_meas"
files = {
    "LP enabled":  wav_dir / "rev_decay_20ms_whitenoise_fb=comb0.975,ap=0.679,_size=1-bounce-1.wav",
    "LP disabled": wav_dir / "rev_decay_20ms_whitenoise_fb=comb0.975,ap=0.679,_size=1_lp_disabled-bounce-1.wav",
}


fig, axes = plt.subplots(1, 2, figsize=(12, 4), sharey=True)

for ax, (label, fpath) in zip(axes, files.items()):
    sr, data = wavfile.read(fpath)
    if data.dtype != np.float32:
        data = data.astype(np.float32) / np.max(np.abs(data))
    if data.ndim == 2:
        data = data[:, 0]
    data = data[:5 * sr]

    f, t, Sxx = spectrogram(data, fs=sr, nperseg=1024)
    Sxx_db = 10 * np.log10(Sxx + 1e-10)
    Sxx_db = Sxx_db - np.max(Sxx_db) - 20  # shift down 10 dB
    pcm = ax.pcolormesh(t, f, Sxx_db,
                        cmap='inferno', shading='gouraud',
                        vmin=-80, vmax=0)
    ax.set_ylim(0, sr/2)
    ax.set_xlabel("Time (s)")
    ax.set_title(f"$fb=0.975$, size$=1.0$ — {label}")

axes[0].set_ylabel("Frequency (Hz)")

cbar = fig.colorbar(pcm, ax=axes[1], label='Power (dB)', shrink=1.0, pad=0.02)
cbar.set_ticks([-80, -60, -40, -20, 0])
cbar.set_ticklabels(['-80 dB', '-60 dB', '-40 dB', '-20 dB', '0 dB'])

plt.tight_layout()


script_dir = Path(__file__).resolve().parent
plt.savefig(script_dir / "../images/schroeder_reverb/fig_reverb_spectrogram.pdf", bbox_inches="tight")
plt.savefig(script_dir / "../images/schroeder_reverb/fig_reverb_spectrogram.png", 
            bbox_inches="tight", dpi=300)
plt.show()