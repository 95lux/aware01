from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt
import plotstyle
import plotutils

# parameters
D = 8       # delay in samples
alpha = 0.7 # feedback gain
N = 512     # number of samples to compute

# impulse signal
x = np.zeros(N)
x[0] = 1

# feedback comb filter
y = np.zeros(N)
y[0] = 1
for n in range(D, N):
    y[n] = x[n] + alpha * y[n - D]

# frequency response
NFFT = 4096
H = np.fft.fft(y, n=NFFT)
freq = np.fft.fftfreq(NFFT, d=1)

# ---- plots ----
fig, (ax1, ax2) = plt.subplots(2, 1)

# time domain
nonzero_idx = np.nonzero(y)[0]
plotutils.styled_stem(ax1, nonzero_idx, y[nonzero_idx])
tick_positions = np.arange(0, N, D)
tick_labels = ["0"] + [f"{i}D" for i in range(1, len(tick_positions))]
ax1.set_xticks(tick_positions)
ax1.set_xticklabels(tick_labels)
ax1.set_xlim(-0.5, 6*D + 0.5)
ax1.set_title("Impulse Response (Time Domain)")
ax1.set_ylabel("h[n]")


alphas = [0.3, 0.7, 0.90]
linestyles = ['-', '--', ':']

for a, ls in zip(alphas, linestyles):
    y_a = np.zeros(N)
    y_a[0] = 1
    for n in range(D, N):
        y_a[n] = x[n] + a * y_a[n - D]
    H_a = np.fft.fft(y_a, n=NFFT)
    ax2.plot(freq[:NFFT//2], np.abs(H_a[:NFFT//2]),
             color='black', linewidth=0.9, linestyle=ls,
             label=f"$\\alpha={a}$")

ax2.legend(frameon=True, loc='upper right',
           fancybox=False, edgecolor='black',
           facecolor='white')

# frequency response
# ax2.plot(freq[:NFFT//2], np.abs(H[:NFFT//2]), color='black', linewidth=0.9)
peak_freqs = np.arange(0, 0.5 + 1/D, 1/D)
peak_labels = [f"$\\frac{{{k}}}{{D}}$" if k > 0 else "0" for k in range(len(peak_freqs))]
ax2.set_xticks(peak_freqs)
ax2.set_xticklabels(peak_labels)
# ax2.set_xlabel("Normalized Frequency $f/f_s$")
ax2.set_xlabel("Normalized Frequency $k/D$, $k \\in \\mathbb{Z}$, $f \\leq 0.5 f_s$")
ax2.set_title("Magnitude Frequency Response")
ax2.set_ylabel("|H(f)|")


plt.tight_layout()

# save


script_dir = Path(__file__).resolve().parent
plt.savefig(script_dir / "../images/schroeder_reverb/fig_comb_filter_response.pdf", bbox_inches="tight")
plt.savefig(script_dir / "output/fig_comb_filter_response.png", bbox_inches="tight")

plt.show()

print("Saved: comb_filter_response.pdf / comb_filter_response.png")