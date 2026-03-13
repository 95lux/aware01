import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import plotstyle

script_dir = Path(__file__).resolve().parent

STYLES = [
    {"color": "#E53935", "linestyle": "-",      "linewidth": 1.5, "alpha": 0.8},  # ZOH
    {"color": "#C58610", "linestyle": "-", "linewidth": 1.5, "alpha": 0.8},  # Linear
    {"color": "#2196F3", "linestyle": "-",       "linewidth": 2.0, "alpha": 1.0},  # Catmull-Rom
]

N = 4096
f_norm = np.linspace(0, 16, N)

# ── ZOH ──────────────────────────────────────────────────────────────────────
H_zoh = np.abs(np.sinc(f_norm / 2))

# ── Linear ────────────────────────────────────────────────────────────────────
H_lin = np.abs(np.sinc(f_norm / 2)) ** 2

# ── Catmull-Rom kernel ────────────────────────────────────────────────────────
def catmull_rom_kernel(t):
    t = np.abs(t)
    if t <= 1:
        return 1-2.5*t**2+1.5*t**3
    elif t <= 2:
        return 2-4*t+2.5*t**2-0.5*t**3
    elif t>= 2:
        return 0
    return -t

kernel_t = np.linspace(-2, 2, 4000)
kernel_vals = np.array([catmull_rom_kernel(t) for t in kernel_t])
dt = kernel_t[1] - kernel_t[0]

H_cr = np.array([
    np.sum(kernel_vals * np.exp(-1j * np.pi * fk * kernel_t)) * dt
    for fk in f_norm
])
H_cr = np.abs(H_cr)
H_cr = H_cr / H_cr[0]

# clip to avoid log(0)
H_zoh = np.clip(H_zoh, 1e-9, None)
H_lin = np.clip(H_lin, 1e-9, None)
H_cr  = np.clip(H_cr,  1e-9, None)

# ── Plot ──────────────────────────────────────────────────────────────────────
COLOR_ZOH = "#E53935"
COLOR_LIN = "#FB8C00"
COLOR_CR  = "#2196F3"
COLOR_NYQ = "#4E4E4E"

fig, ax = plt.subplots(1, 1, figsize=(12, 5))


ax.set_xlim(0, 16)
ax.set_ylim(-80, 5)

f_plot = f_norm / 2  # now 1.0 = Nyquist = f_s/2

ax.plot(f_plot, 20 * np.log10(H_zoh), **STYLES[0], label="(1) Zero-Order Hold (ZOH)")
ax.plot(f_plot, 20 * np.log10(H_lin), **STYLES[1], label="(2) Linear Interpolation")
ax.plot(f_plot, 20 * np.log10(H_cr),  **STYLES[2], label="(3) Catmull-Rom / Hermite")

ax.axvline(0.5, color=COLOR_NYQ, linewidth=0.8, linestyle='--')
ax.text(0.52, 4, '$f_s/2$', color=COLOR_NYQ, fontsize=10, va='top')

ax.set_xlim(0, 8)
ax.set_ylim(-80, 5)
ax.set_xlabel("Normalized frequency ($f / f_s$)")
ax.set_ylabel("Magnitude (dB)")
ax.legend(frameon=True, fancybox=False, edgecolor='black', facecolor='white')

plt.tight_layout()
plt.savefig(script_dir / "../images/interpolation/fig_interpolation_freq_response.pdf",
            bbox_inches="tight")
plt.savefig(script_dir / "fig_interpolation_freq_response.png",
            bbox_inches="tight")
plt.show()
print("Saved.")