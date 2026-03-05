import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

# ── Interpolation functions ───────────────────────────────────────────────────

def linear(x0, x1, t):
    return x0 + (x1 - x0) * t


def hermite(x0, x1, m0, m1, t):
    """Generic cubic Hermite given explicit tangents m0, m1."""
    a =  2*x0 - 2*x1 +   m0 + m1
    b = -3*x0 + 3*x1 - 2*m0 - m1
    c =  m0
    d =  x0
    return ((a * t + b) * t + c) * t + d


def catmull_rom(xm1, x0, x1, x2, t):
    """Hermite with central-difference tangents."""
    m0 = (x1 - xm1) / 2.0
    m1 = (x2 - x0)  / 2.0
    return hermite(x0, x1, m0, m1, t)


# ── Source signal ─────────────────────────────────────────────────────────────

N_SAMPLES = 6
sample_idx  = np.arange(N_SAMPLES)

# One full period of sine, sampled at N points
freq        = 2 * np.pi / (N_SAMPLES - 1)
sample_vals = np.sin(freq * sample_idx)
sample_ders = np.cos(freq * sample_idx) * freq   # exact derivative of sine

# Dense reference
t_dense = np.linspace(0, N_SAMPLES - 1, 800)
y_true  = np.sin(freq * t_dense)

# ── Build interpolated curves ─────────────────────────────────────────────────

STEPS = 50

def build_curve(interp_fn):
    ts, ys = [], []
    for n in range(N_SAMPLES - 1):
        for t in np.linspace(0, 1, STEPS, endpoint=False):
            ts.append(n + t)
            ys.append(interp_fn(n, t))
    return np.array(ts), np.array(ys)

_, y_linear = build_curve(
    lambda n, t: linear(sample_vals[n], sample_vals[n + 1], t)
)

# Catmull-Rom: needs n-1 and n+2, so only interior intervals are reliable
t_cr, y_cr = [], []
for n in range(1, N_SAMPLES - 2):
    xm1, x0, x1, x2 = sample_vals[n-1], sample_vals[n], sample_vals[n+1], sample_vals[n+2]
    for t in np.linspace(0, 1, STEPS, endpoint=False):
        t_cr.append(n + t)
        y_cr.append(catmull_rom(xm1, x0, x1, x2, t))
t_cr, y_cr = np.array(t_cr), np.array(y_cr)

_, y_hermite = build_curve(
    lambda n, t: hermite(
        sample_vals[n], sample_vals[n + 1],
        sample_ders[n], sample_ders[n + 1],
        t
    )
)

t_all = np.linspace(0, N_SAMPLES - 1, len(y_linear))

# ── Style ─────────────────────────────────────────────────────────────────────

plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Latin Modern Roman"],
    "mathtext.fontset": "cm",
    "font.size":         13,
    "axes.spines.top":   False,
    "axes.spines.right": False,
    "figure.dpi":        150,
})

COLOR_TRUE   = "#b0b0b0"
COLOR_INTERP = "#2a6ebb"
COLOR_SAMPLE = "#1a1a1a"
COLOR_TANG   = "#e07b39"

TITLES = [
    "Linear Interpolation",
    "Catmull-Rom \n(finite-difference tangents)",
    "Cubic Hermite \n(exact derivatives)",
]

curves   = [y_linear, y_cr,  y_hermite]
t_curves = [t_all,    t_cr,  t_all    ]

# Catmull-Rom valid sample range for scatter
cr_sample_range = range(N_SAMPLES)

fig, axes = plt.subplots(1, 3, figsize=(13, 4), sharey=True)

for ax, title, t_c, y_c in zip(axes, TITLES, t_curves, curves):

    # Reference sine
    ax.plot(t_dense, y_true,
            color=COLOR_TRUE, linewidth=1.2, linestyle="--", zorder=1)

    # Interpolated curve
    ax.plot(t_c, y_c,
            color=COLOR_INTERP, linewidth=2.0, zorder=2)

    # Samples
    ax.scatter(sample_idx, sample_vals,
               color="white", edgecolors=COLOR_SAMPLE,
               s=55, linewidths=1.5, zorder=4)

    ax.set_title(title, pad=8)
    ax.set_xticks(sample_idx)
    for i, (ax, title, t_c, y_c) in enumerate(zip(axes, TITLES, t_curves, curves), 1):
    # add number in parentheses before title
        ax.set_title(f"({i}) {title}", pad=8)


axes[0].set_ylabel("Amplitude")

# Tangent arrows on Hermite panels (Catmull-Rom and True Hermite)
for ax, tang_vals, t_range in [
    (axes[1], [(n, (sample_vals[n+1] - sample_vals[n-1]) / 2.0)
               for n in range(1, N_SAMPLES - 2)], None),
    (axes[2], [(n, sample_ders[n])
               for n in range(N_SAMPLES)], None),
]:
    for n, m in tang_vals:
        ax.annotate("",
                    xy=(n + 0.22, sample_vals[n] + m * 0.22),
                    xytext=(n, sample_vals[n]),
                    arrowprops=dict(arrowstyle="-|>",
                                    color=COLOR_TANG,
                                    lw=1.1,
                                    mutation_scale=7),
                    zorder=3)

# Shared legend below all panels
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

legend_elements = [
    Line2D([0], [0], color=COLOR_TRUE,   linewidth=1.2, linestyle="--", label="True sine"),
    Line2D([0], [0], color=COLOR_INTERP, linewidth=2.0,                 label="Interpolated curve"),
    Line2D([0], [0], marker="o", color="w",
           markerfacecolor="white", markeredgecolor=COLOR_SAMPLE,
           markersize=7, label=r"Discrete samples $y[n]$"),
    Patch(facecolor=COLOR_TANG, label="Tangent estimates"),
]

fig.legend(handles=legend_elements,
           loc="lower center", ncol=5,
           frameon=True, fontsize=13,
           bbox_to_anchor=(0.5, -0.18))

fig.supxlabel("Sample index $n$", fontsize=13, y=-0.05)

script_dir = Path(__file__).resolve().parent

# plt.tight_layout()
plt.savefig(script_dir / "../../images/interpolation/fig_interpolation_comparison.pdf", bbox_inches="tight")
plt.savefig(script_dir / "fig_interpolation_comparison.png", bbox_inches="tight")
print("Saved.")