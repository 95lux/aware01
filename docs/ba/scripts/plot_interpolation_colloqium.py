from scipy.io import wavfile
from scipy.fft import rfft, rfftfreq
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from pathlib import Path
import plotstyle

script_dir = Path(__file__).resolve().parent
wav_dir = script_dir / "interpolation_measurements/saw"

rates = [0.25, 0.33, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0]
methods = ["zoh", "hermite"]
colors = {'hermite': '#2196F3', 'zoh': '#E53935'}
labels = {'hermite': 'Hermite', 'zoh': 'ZOH'}
zorder = {'hermite': 2, 'zoh': 1}

files = {
    method: {
        rate: wav_dir / f"{method}_saw_100Hz_rate{rate}-bounce-1.wav"
        for rate in rates
    }
    for method in methods
}

n_cols = 2
n_rows = len(rates) // n_cols  # 4

fig, axes = plt.subplots(n_rows, n_cols, figsize=(16, 1.4 * n_rows),
                         gridspec_kw={'hspace': 0.55, 'wspace': 0.3})

for idx, rate in enumerate(rates):
    row = idx % n_rows
    col = idx // n_rows
    ax = axes[row, col]

    for method in methods:
        sr, data = wavfile.read(files[method][rate])
        if data.dtype != np.float32:
            data = data.astype(np.float32) / np.iinfo(data.dtype).max
        if data.ndim == 2:
            data = data[:, 0]

        window = np.hanning(len(data))
        spectrum = np.abs(rfft(data * window))
        freqs = rfftfreq(len(data), 1 / sr)
        spectrum = spectrum / np.max(spectrum)
        spectrum_db = 20 * np.log10(spectrum + 1e-9)

        ax.plot(freqs, spectrum_db,
                color=colors[method], linewidth=0.6,
                label=labels[method], alpha=0.8, zorder=zorder[method])

    ax.set_ylim(-80, 5)
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(plt.FuncFormatter(
        lambda x, _: f'{x/1000:.0f}k' if x >= 1000 else f'{x:.0f}'))
    ax.xaxis.set_major_locator(plt.FixedLocator(
        [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]))
    ax.set_xlim(20, sr / 2)
    

    # show x tick labels only on bottom row of each column
    if row != n_rows - 1:
        ax.set_xticklabels([])
    else:
        ax.set_xlabel("Frequency (Hz)", fontsize=14)

    label = r"\textbf{1.0x}" if rate == 1.0 else f"{rate}x"
    ax.set_ylabel(label, rotation=0, labelpad=35, va='center', fontsize=14,
                  usetex=True)
    ax.yaxis.set_label_coords(-0.12, 0.5)

legend_elements = [
    Line2D([0], [0], color=colors['zoh'],     linewidth=1.5, label=labels['zoh']),
    Line2D([0], [0], color=colors['hermite'], linewidth=1.5, label=labels['hermite']),
]
fig.legend(handles=legend_elements,
           loc='upper right', bbox_to_anchor=(0.99, 0.97),
           frameon=True, fancybox=False,
           edgecolor='black', facecolor='white', fontsize=16)

fig.text(0.04, 0.5, 'Amplitude (dB)', va='center', rotation='vertical', fontsize=18)

plt.tight_layout(rect=[0.05, 0, 1, 1])
# plt.savefig(script_dir / "../images/interpolation/fig_interpolation_fft.pdf", bbox_inches="tight")
plt.savefig(script_dir / "../../colloqium/images/fig_interpolation_fft_colloqium.png", bbox_inches="tight", dpi=150)
plt.show()