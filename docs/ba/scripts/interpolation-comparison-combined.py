import numpy as np
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from pathlib import Path

OUTPUT = Path(__file__).resolve().parents[2] / "colloqium/images/fig_interpolation_combined.png"

# ── Interpolation functions ───────────────────────────────────────────────────

def linear(x0, x1, t):
    return x0 + (x1 - x0) * t

def hermite(x0, x1, m0, m1, t):
    a =  2*x0 - 2*x1 +   m0 + m1
    b = -3*x0 + 3*x1 - 2*m0 - m1
    return ((a * t + b) * t + m0) * t + x0

def catmull_rom(xm1, x0, x1, x2, t):
    m0 = (x1 - xm1) / 2.0
    m1 = (x2 - x0)  / 2.0
    return hermite(x0, x1, m0, m1, t)

# ── Source signal ─────────────────────────────────────────────────────────────

N_SAMPLES = 9
sample_idx = np.arange(N_SAMPLES)

freqs = [2.0, 1.0, 2.7]
amps  = [0.8, 0.6, 0.4]

sample_vals = np.zeros(N_SAMPLES)
sample_ders = np.zeros(N_SAMPLES)
for a, f in zip(amps, freqs):
    sample_vals += a * np.sin(f * sample_idx)
    sample_ders += a * f * np.cos(f * sample_idx)

t_dense = np.linspace(0, N_SAMPLES - 1, 800)
y_true = np.zeros_like(t_dense)
for a, f in zip(amps, freqs):
    y_true += a * np.sin(f * t_dense)

# ── Build interpolated curves ─────────────────────────────────────────────────

STEPS = 50

def build_curve(interp_fn):
    ts, ys = [], []
    for n in range(N_SAMPLES - 1):
        for t in np.linspace(0, 1, STEPS, endpoint=False):
            ts.append(n + t)
            ys.append(interp_fn(n, t))
    return np.array(ts), np.array(ys)

_, y_linear = build_curve(lambda n, t: linear(sample_vals[n], sample_vals[n + 1], t))
_, y_zoh    = build_curve(lambda n, t: sample_vals[n])

t_cr, y_cr = [], []
for n in range(1, N_SAMPLES - 2):
    xm1, x0, x1, x2 = sample_vals[n-1], sample_vals[n], sample_vals[n+1], sample_vals[n+2]
    for t in np.linspace(0, 1, STEPS, endpoint=False):
        t_cr.append(n + t)
        y_cr.append(catmull_rom(xm1, x0, x1, x2, t))
t_cr, y_cr = np.array(t_cr), np.array(y_cr)

t_all = np.linspace(0, N_SAMPLES - 1, len(y_zoh))

# ── Colors ────────────────────────────────────────────────────────────────────

COLOR_TRUE   = "#b0b0b0"
COLOR_SAMPLE = "#1a1a1a"
COLOR_STEM   = "#aaaaaa"

COLORS = {
    "zoh":    "#2a6ebb",
    "linear": "#c0392b",
    "cr":     "#2ab87a",
}

# ── Shared layout constants ───────────────────────────────────────────────────

FIGSIZE    = (10, 8)
DPI        = 150
FONT_SIZE  = 16
XLIM       = (-0.3, N_SAMPLES - 0.7)
LEG_NCOL   = 2
LEG_FS     = 15

plt.rcParams.update({
    "font.size":         FONT_SIZE,
    "axes.spines.top":   False,
    "axes.spines.right": False,
    "figure.dpi":        DPI,
})

fig, ax = plt.subplots(figsize=FIGSIZE)

curves_data = [
    (t_all, y_zoh,    COLORS["zoh"],    "--", 1.0),
    (t_all, y_linear, COLORS["linear"], "-",  1.8),
    (t_cr,  y_cr,     COLORS["cr"],     "-",  2.0),
]
for t_c, y_c, color, ls, lw in curves_data:
    ax.plot(t_c, y_c, color=color, linewidth=lw, linestyle=ls)

ax.plot(t_dense, y_true, color=COLOR_TRUE, linewidth=1.2, linestyle="--", zorder=1)
ax.axhline(0, color=COLOR_STEM, linewidth=0.6, zorder=1)
ax.scatter(sample_idx, sample_vals,
           color="white", edgecolors=COLOR_SAMPLE,
           s=55, linewidths=1.5, zorder=4)

ax.set_xlabel("Sample index $n$")
ax.set_ylabel("Amplitude")
ax.set_xticks(sample_idx)
ax.set_xlim(XLIM)

legend_elements = [
    Line2D([0], [0], color=COLOR_TRUE,        linewidth=1.2, linestyle="--", label="True signal"),
    Line2D([0], [0], color=COLORS["zoh"],     linewidth=1.0, linestyle="--", label="ZOH"),
    Line2D([0], [0], color=COLORS["linear"],  linewidth=1.8, linestyle="-",  label="Linear"),
    Line2D([0], [0], color=COLORS["cr"],      linewidth=2.0, linestyle="-",  label="Hermite (Catmull-Rom)"),
    Line2D([0], [0], marker="o", color="w", markerfacecolor="white",
           markeredgecolor=COLOR_SAMPLE, markersize=7, linewidth=0, label=r"Samples $y[n]$"),
]
ax.legend(handles=legend_elements, loc="upper center", frameon=True, fontsize=LEG_FS, ncol=LEG_NCOL)

plt.tight_layout()
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
plt.savefig(OUTPUT, bbox_inches="tight")
plt.savefig(Path(__file__).parent / "fig_interpolation_combined.png", bbox_inches="tight")
print(f"saved → {OUTPUT}")
plt.show()
