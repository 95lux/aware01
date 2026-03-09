from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt
import plotstyle
import plotutils

# parameters
D = 8        # delay in samples
g = 0.7      # allpass gain
N = 512      # number of samples to compute

# impulse signal
x = np.zeros(N)
x[0] = 1

# Schroeder allpass: y[n] = -g*x[n] + x[n-D] + g*y[n-D]
y = np.zeros(N)
for n in range(N):
    x_delayed = x[n - D] if n >= D else 0.0
    y_delayed = y[n - D] if n >= D else 0.0
    y[n] = -g * x[n] + x_delayed + g * y_delayed

# frequency response
NFFT = 4096
H = np.fft.fft(y, n=NFFT)
freq = np.fft.fftfreq(NFFT, d=1)

# ---- plots ----
fig, (ax1, ax2) = plt.subplots(2, 1)

# time domain
nonzero_mask = np.abs(y[:10*D]) > 1e-10
indices = np.arange(10*D)[nonzero_mask]
plotutils.styled_stem(ax1, indices, y[:10*D][nonzero_mask])
ax1.set_xlim(-1, 10*D + 0.5)
tick_positions = np.arange(0, 10*D + D, D)
tick_labels = ["0"] + [f"{i}D" for i in range(1, len(tick_positions))]
ax1.set_xticks(tick_positions)
ax1.set_xticklabels(tick_labels)
ax1.set_title("Impulse Response (Time Domain)")
ax1.set_ylabel("h[n]")

# frequency response
ax2.plot(freq[:NFFT//2], np.abs(H[:NFFT//2]), color='black', linewidth=0.9)
ax2.set_ylim(0, 1.5)
peak_freqs = np.arange(0, 0.5 + 1/D, 1/D)
peak_labels = [f"$\\frac{{{k}}}{{D}}$" if k > 0 else "0" for k in range(len(peak_freqs))]
ax2.set_xticks(peak_freqs)
ax2.set_xticklabels(peak_labels)
ax2.set_xlabel("Normalized Frequency $k/D$, $k \\in \\mathbb{Z}$, $f \\leq 0.5 f_s$")
ax2.set_title("Magnitude Frequency Response")
ax2.set_ylabel("|H(f)|")

plt.tight_layout()

script_dir = Path(__file__).resolve().parent
plt.savefig(script_dir / "../images/schroeder_reverb/fig_allpass_response.pdf", bbox_inches="tight")
plt.savefig(script_dir / "output/fig_allpass_response.png", bbox_inches="tight")
plt.show()
print("Saved: fig_allpass_response.pdf / fig_allpass_response.png")