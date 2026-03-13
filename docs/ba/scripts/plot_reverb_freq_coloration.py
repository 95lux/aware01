from scipy.io import wavfile
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import plotstyle

# record white noise input through reverb at different lp_alpha values
files_lp = {
    0.0:  "reverb_lp_0.0.wav",
    0.3:  "reverb_lp_0.3.wav",
    0.6:  "reverb_lp_0.6.wav",
    0.9:  "reverb_lp_0.9.wav",
}

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 4))
linestyles = ['-', '--', ':', '-.']

# lp_alpha sweep
for (lp, fname), ls in zip(files_lp.items(), linestyles):
    sr, data = wavfile.read(fname)
    if data.dtype != np.float32:
        data = data.astype(np.float32) / np.max(np.abs(data))
    if data.ndim == 2:
        data = data[:, 0]
    N = len(data)
    H = np.fft.rfft(data)
    freq = np.fft.rfftfreq(N, d=1/sr)
    ax1.plot(freq, 20 * np.log10(np.abs(H) + 1e-10),
             linestyle=ls, color='black', linewidth=0.9,
             label=f"$d={lp}$")

ax1.set_xlabel("Frequency (Hz)")
ax1.set_ylabel("Magnitude (dB)")
ax1.set_title("Frequency Response — LP Damping")
ax1.set_xlim(0, sr // 2)
ax1.legend(frameon=True, fancybox=False, edgecolor='black', facecolor='white')

# size sweep
files_size = {
    0.3:  "reverb_size_0.3.wav",
    0.6:  "reverb_size_0.6.wav",
    1.0:  "reverb_size_1.0.wav",
}

for (sz, fname), ls in zip(files_size.items(), linestyles):
    sr, data = wavfile.read(fname)
    if data.dtype != np.float32:
        data = data.astype(np.float32) / np.max(np.abs(data))
    if data.ndim == 2:
        data = data[:, 0]
    N = len(data)
    H = np.fft.rfft(data)
    freq = np.fft.rfftfreq(N, d=1/sr)
    ax2.plot(freq, 20 * np.log10(np.abs(H) + 1e-10),
             linestyle=ls, color='black', linewidth=0.9,
             label=f"size$={sz}$")

ax2.set_xlabel("Frequency (Hz)")
ax2.set_ylabel("Magnitude (dB)")
ax2.set_title("Frequency Response — Room Size")
ax2.set_xlim(0, sr // 2)
ax2.legend(frameon=True, fancybox=False, edgecolor='black', facecolor='white')

plt.tight_layout()
script_dir = Path(__file__).resolve().parent
plt.savefig(script_dir / "../images/schroeder_reverb/fig_reverb_spectrum.pdf", bbox_inches="tight")
plt.savefig(script_dir / "output/fig_reverb_spectrum.png", bbox_inches="tight")
plt.show()