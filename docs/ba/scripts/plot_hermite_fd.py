"""
Figure for "How Hermite Works" slide.
Same layout as fig_interpolation_combined — only Hermite curve + FD tangent arrows.
"""

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from pathlib import Path

OUTPUT = Path(__file__).resolve().parents[2] / "colloqium/images/fig_hermite_fd.png"

# ── Interpolation functions ───────────────────────────────────────────────────

def hermite(x0, x1, m0, m1, t):
    a =  2*x0 - 2*x1 +   m0 + m1
    b = -3*x0 + 3*x1 - 2*m0 - m1
    return ((a * t + b) * t + m0) * t + x0

def catmull_rom(xm1, x0, x1, x2, t):
    m0 = (x1 - xm1) / 2.0
    m1 = (x2 - x0)  / 2.0
    return hermite(x0, x1, m0, m1, t)

# ── Source signal (identical to comparison figure) ────────────────────────────

N_SAMPLES = 9
sample_idx = np.arange(N_SAMPLES)

freqs = [2.0, 1.0, 2.7]
amps  = [0.8, 0.6, 0.4]

sample_vals = np.zeros(N_SAMPLES)
for a, f in zip(amps, freqs):
    sample_vals += a * np.sin(f * sample_idx)

t_dense = np.linspace(0, N_SAMPLES - 1, 800)
y_true = np.zeros_like(t_dense)
for a, f in zip(amps, freqs):
    y_true += a * np.sin(f * t_dense)

STEPS = 50
t_cr, y_cr = [], []
for n in range(1, N_SAMPLES - 2):
    xm1, x0, x1, x2 = sample_vals[n-1], sample_vals[n], sample_vals[n+1], sample_vals[n+2]
    for t in np.linspace(0, 1, STEPS, endpoint=False):
        t_cr.append(n + t)
        y_cr.append(catmull_rom(xm1, x0, x1, x2, t))
t_cr, y_cr = np.array(t_cr), np.array(y_cr)

# ── Colors ────────────────────────────────────────────────────────────────────

COLOR_TRUE   = "#b0b0b0"
COLOR_CR     = "#2ab87a"
COLOR_SAMPLE = "#1a1a1a"
COLOR_TANG   = "#e07b39"
COLOR_STEM   = "#aaaaaa"

# ── Shared layout constants (must match interpolation-comparison-combined.py) ─

FIGSIZE   = (10, 8)
DPI       = 150
FONT_SIZE = 16
XLIM      = (-0.3, N_SAMPLES - 0.7)
LEG_NCOL  = 2
LEG_FS    = 15

plt.rcParams.update({
    "font.size":         FONT_SIZE,
    "axes.spines.top":   False,
    "axes.spines.right": False,
    "figure.dpi":        DPI,
})

fig, ax = plt.subplots(figsize=FIGSIZE)

ax.plot(t_dense, y_true, color=COLOR_TRUE, linewidth=1.2, linestyle="--", zorder=1)
ax.axhline(0, color=COLOR_STEM, linewidth=0.6, zorder=1)
ax.plot(t_cr, y_cr, color=COLOR_CR, linewidth=2.2, zorder=2)
ax.scatter(sample_idx, sample_vals,
           color="white", edgecolors=COLOR_SAMPLE,
           s=55, linewidths=1.5, zorder=4)

# Highlighted segment [n0, n0+1]
n0 = 3
COLOR_HL = "#1a6e3a"
t_seg = np.linspace(0, 1, STEPS)
xm1, x0, x1, x2 = sample_vals[n0-1], sample_vals[n0], sample_vals[n0+1], sample_vals[n0+2]
m0_val = (x1 - xm1) / 2.0
m1_val = (x2 - x0)  / 2.0
y_seg = [hermite(x0, x1, m0_val, m1_val, t) for t in t_seg]
ax.plot(t_seg + n0, y_seg, color=COLOR_HL, linewidth=3.5, zorder=3)

# FD tangent arrows at all interior samples
ARROW_LEN = 0.38
for n in range(1, N_SAMPLES - 2):
    m = (sample_vals[n + 1] - sample_vals[n - 1]) / 2.0
    ax.annotate("",
                xy=(n + ARROW_LEN, sample_vals[n] + m * ARROW_LEN),
                xytext=(n, sample_vals[n]),
                arrowprops=dict(arrowstyle="-|>", color=COLOR_TANG, lw=1.3, mutation_scale=9),
                zorder=5)

# m0/m1 labels at the highlighted interval endpoints
for n, label in [(n0, "$m_0$"), (n0 + 1, "$m_1$")]:
    m = (sample_vals[n + 1] - sample_vals[n - 1]) / 2.0
    tip_x = n + ARROW_LEN
    tip_y = sample_vals[n] + m * ARROW_LEN
    offset = 0.13 if m >= 0 else -0.13
    ax.text(tip_x + 0.08, tip_y + offset, label,
            color=COLOR_TANG, fontsize=15, ha="left", va="center", fontweight="bold")

ax.set_xlabel("Sample index $n$")
ax.set_ylabel("Amplitude")
ax.set_xticks(sample_idx)
ax.set_xlim(XLIM)

legend_elements = [
    Line2D([0], [0], color=COLOR_TRUE, linewidth=1.2, linestyle="--", label="True signal"),
    Line2D([0], [0], color=COLOR_CR,   linewidth=2.2, linestyle="-",  label="Hermite interpolation"),
    Line2D([0], [0], marker=">", color=COLOR_TANG, markersize=7, linewidth=0, label="FD tangent estimates"),
    Line2D([0], [0], marker="o", color="w", markerfacecolor="white",
           markeredgecolor=COLOR_SAMPLE, markersize=7, linewidth=0, label=r"Samples $y[n]$"),
]
ax.legend(handles=legend_elements, loc="upper center", frameon=True, fontsize=LEG_FS, ncol=LEG_NCOL)

plt.tight_layout()
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
plt.savefig(OUTPUT, bbox_inches="tight")
print(f"saved → {OUTPUT}")
plt.show()
