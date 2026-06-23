"""
Generate XY mapper figure for the colloquium presentation.
Left panel: 2D joystick space diagram.
Right panels: response curves for each mapped parameter.
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from pathlib import Path

plt.rcParams.update({
    "font.family": "sans-serif",
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.prop_cycle": plt.cycler("color", ["#2a6ebb","#e07b39","#5aa469","#c44e52","#8172b3"]),
    "figure.dpi": 150,
})

OUTPUT = Path(__file__).resolve().parents[2] / "colloqium/images/fig_xy_mapper.png"


def piecewise_map(t, min_neg, max_neg, exp_neg, min_pos, max_pos, exp_pos):
    t = np.asarray(t, dtype=float)
    out = np.empty_like(t)
    neg = t < 0
    v_neg = np.abs(t[neg]) ** exp_neg
    out[neg] = min_neg + v_neg * (max_neg - min_neg)
    v_pos = t[~neg] ** exp_pos
    out[~neg] = min_pos + v_pos * (max_pos - min_pos)
    return out


t = np.linspace(0, 1, 500)          # CV input 0..1 (magnitude per half)
t_signed = np.linspace(-1, 1, 500)  # for symmetric plots

# X-axis mappings (from xy_mapper.c)
x_curves = [
    {
        "label": "Feedback (RT60)",
        "neg": (0.02, 1.0, 0.15),
        "pos": (0.02, 1.0, 0.15),
        "ylabel": "Feedback α",
        "color": "#2a6ebb",
    },
    {
        "label": "Wet mix",
        "neg": (0.0, 1.0, 0.7),
        "pos": (0.0, 1.0, 0.7),
        "ylabel": "Wet",
        "color": "#e07b39",
    },
    {
        "label": "LP cutoff α",
        "neg": (0.0, 0.0, 1.3),   # negative half → 0 (no effect)
        "pos": (0.0, 0.4, 1.3),
        "ylabel": "LP α",
        "color": "#5aa469",
    },
]

# Y-axis mapping
y_curve = {
    "label": "Room size",
    "neg": (0.05, 1.0, 1.0),
    "pos": (0.05, 1.0, 1.0),
    "ylabel": "Size",
    "color": "#c44e52",
}

fig = plt.figure(figsize=(14, 5))
gs = gridspec.GridSpec(
    2, 4,
    figure=fig,
    width_ratios=[1.2, 1, 1, 1],
    wspace=0.55,
    hspace=0.6,
    left=0.07, right=0.97, top=0.88, bottom=0.13,
)

# ── Left panel: 2D joystick diagram ──────────────────────────────────────────
ax_xy = fig.add_subplot(gs[:, 0])
ax_xy.set_xlim(-1, 1)
ax_xy.set_ylim(-1, 1)
ax_xy.set_aspect("equal")
ax_xy.axhline(0, color="#aaa", linewidth=0.8, zorder=1)
ax_xy.axvline(0, color="#aaa", linewidth=0.8, zorder=1)
ax_xy.set_xlabel("X CV", labelpad=4)
ax_xy.set_ylabel("Y CV", labelpad=4)
ax_xy.set_title("XY Control Space", fontsize=12)

# Directional labels
kw = dict(ha="center", va="center", fontsize=9.5)
ax_xy.text( 0,  0.82, "large room", **kw)
ax_xy.text( 0, -0.82, "small room", **kw)
ax_xy.text( 0.78,  0, "long · wet\ndark", **kw)
ax_xy.text(-0.78,  0, "short · dry\nbright", **kw)

# Center dot
ax_xy.plot(0, 0, "o", color="#333", markersize=6, zorder=3)

ax_xy.set_xticks([-1, 0, 1])
ax_xy.set_yticks([-1, 0, 1])

# ── X-axis curve plots (top row, cols 1-3) ───────────────────────────────────
for col, crv in enumerate(x_curves):
    ax = fig.add_subplot(gs[0, col + 1])
    y_neg = piecewise_map(
        t, crv["neg"][0], crv["neg"][1], crv["neg"][2],
            crv["pos"][0], crv["pos"][1], crv["pos"][2],
    )
    # Negative half: mirror t
    y_neg_half = piecewise_map(
        np.concatenate([-t[::-1], t]),
        crv["neg"][0], crv["neg"][1], crv["neg"][2],
        crv["pos"][0], crv["pos"][1], crv["pos"][2],
    )
    ax.plot(
        np.concatenate([-t[::-1], t]),
        y_neg_half,
        color=crv["color"],
        linewidth=2,
    )
    ax.axvline(0, color="#ccc", linewidth=0.7, linestyle="--")
    ax.set_xlim(-1, 1)
    ax.set_xticks([-1, 0, 1])
    ax.set_xlabel("X CV", fontsize=10, labelpad=2)
    ax.set_ylabel(crv["ylabel"], fontsize=10, labelpad=4)
    ax.set_title(crv["label"], fontsize=10)
    ax.tick_params(labelsize=9)

# ── Y-axis curve plot (bottom row, col 1, spanning remaining cols) ───────────
ax_y = fig.add_subplot(gs[1, 1:])
y_vals = piecewise_map(
    np.concatenate([-t[::-1], t]),
    y_curve["neg"][0], y_curve["neg"][1], y_curve["neg"][2],
    y_curve["pos"][0], y_curve["pos"][1], y_curve["pos"][2],
)
ax_y.plot(np.concatenate([-t[::-1], t]), y_vals, color=y_curve["color"], linewidth=2)
ax_y.axvline(0, color="#ccc", linewidth=0.7, linestyle="--")
ax_y.set_xlim(-1, 1)
ax_y.set_xticks([-1, 0, 1])
ax_y.set_xlabel("Y CV", fontsize=10, labelpad=2)
ax_y.set_ylabel(y_curve["ylabel"], fontsize=10, labelpad=4)
ax_y.set_title(y_curve["label"], fontsize=10)
ax_y.tick_params(labelsize=9)

# ── Row labels ───────────────────────────────────────────────────────────────
fig.text(0.34, 0.93, "X axis mappings", ha="center", fontsize=11, fontweight="bold")
fig.text(0.34, 0.47, "Y axis mapping",  ha="center", fontsize=11, fontweight="bold")

OUTPUT.parent.mkdir(parents=True, exist_ok=True)
plt.savefig(OUTPUT, dpi=150, bbox_inches="tight")
print(f"saved → {OUTPUT}")
plt.show()
