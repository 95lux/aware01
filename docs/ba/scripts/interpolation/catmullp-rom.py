import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

# ── Catmull-Rom interpolation ─────────────────────────────────────────────────

def catmull_rom(xm1, x0, x1, x2, t):
    """Cubic Hermite interpolation with central-difference tangents (Catmull-Rom).
    Interpolates between x0 and x1 for t in [0, 1].
    """
    m0 = (x1 - xm1) / 2.0   # tangent at x0
    m1 = (x2 - x0)  / 2.0   # tangent at x1

    # Hermite basis coefficients
    a =  2*x0 - 2*x1 +   m0 + m1
    b = -3*x0 + 3*x1 - 2*m0 - m1
    c =  m0
    d =  x0

    # Horner form: ((a*t + b)*t + c)*t + d
    return ((a * t + b) * t + c) * t + d


# ── Source signal: one period of a sine wave, sparsely sampled ───────────────

N_SAMPLES   = 5                          # discrete samples
sample_idx  = np.arange(N_SAMPLES)
sample_vals = np.sin(2 * np.pi * sample_idx / (N_SAMPLES - 1))

# Dense "true" sine for reference
t_dense  = np.linspace(0, N_SAMPLES - 1, 800)
y_true   = np.sin(2 * np.pi * t_dense   / (N_SAMPLES - 1))

# ── Run Catmull-Rom over every interior interval ──────────────────────────────

STEPS_PER_INTERVAL = 100

t_interp = []
y_interp = []

for n in range(1, N_SAMPLES - 2):          # need n-1 and n+2 → skip edges
    xm1 = sample_vals[n - 1]
    x0  = sample_vals[n]
    x1  = sample_vals[n + 1]
    x2  = sample_vals[n + 2]

    ts = np.linspace(0, 1, STEPS_PER_INTERVAL, endpoint=False)
    for t in ts:
        t_interp.append(n + t)
        y_interp.append(catmull_rom(xm1, x0, x1, x2, t))

t_interp = np.array(t_interp)
y_interp = np.array(y_interp)

# ── Plot ──────────────────────────────────────────────────────────────────────

plt.rcParams.update({
    "font.family":      "serif",
    "font.size":        11,
    "axes.spines.top":  False,
    "axes.spines.right":False,
    "figure.dpi":       150,
})

fig, ax = plt.subplots(figsize=(8, 4))

# True sine
ax.plot(t_dense, y_true,
        color="#b0b0b0", linewidth=1.2, linestyle="--",
        label="True sine (reference)", zorder=1)

# Catmull-Rom curve
ax.plot(t_interp, y_interp,
        color="#2a6ebb", linewidth=2.0,
        label="Catmull-Rom interpolation", zorder=2)

# Discrete samples
ax.scatter(sample_idx, sample_vals,
           color="white", edgecolors="#1a1a1a", s=60, linewidths=1.5,
           zorder=3, label="Discrete samples $y[n]$")

# Tangent arrows at interpolation endpoints (n=1 … n=N-3)
for n in range(1, N_SAMPLES - 2):
    xm1 = sample_vals[n - 1]
    x0  = sample_vals[n]
    x1  = sample_vals[n + 1]
    x2  = sample_vals[n + 2]
    m0  = (x1 - xm1) / 2.0
    ax.annotate("",
                xy=(n + 0.25, x0 + m0 * 0.25),
                xytext=(n, x0),
                arrowprops=dict(arrowstyle="-|>",
                                color="#e07b39",
                                lw=1.2,
                                mutation_scale=8))

tangent_patch = mpatches.Patch(color="#e07b39", label="Estimated tangents $m_0$")

ax.set_xlabel("Sample index $n$")
ax.set_ylabel("Amplitude")
ax.set_title("Catmull-Rom Spline Interpolation of a Sine Wave")
ax.legend(handles=[
    plt.Line2D([0], [0], color="#b0b0b0", linewidth=1.2, linestyle="--"),
    plt.Line2D([0], [0], color="#2a6ebb", linewidth=2.0),
    plt.Line2D([0], [0], marker="o", color="w",
               markerfacecolor="white", markeredgecolor="#1a1a1a", markersize=7),
    tangent_patch,
], labels=[
    "True sine (reference)",
    "Catmull-Rom interpolation",
    r"Discrete samples $y[n]$",
    r"Estimated tangents $m_0$",
], frameon=False)

plt.tight_layout()
plt.savefig("./catmull_rom_plot.pdf", bbox_inches="tight")
plt.savefig("./catmull_rom_plot.png", bbox_inches="tight")
plt.show()
print("Saved: catmull_rom_plot.pdf / .png")