#!/usr/bin/env python3
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import csv

script_dir = Path(__file__).resolve().parent
CSV_FILE = script_dir / "input_output_stage_simulation/trans_input_stage.csv"

# === config ===
VCM      = 0.9    # V — expected common-mode voltage
VFS_PP   = 1.06   # V — ADC full-scale peak-to-peak
XLIM_US  = 2000   # µs — plot window


def load_csv(path):
    time, v_codec, v_jack = [], [], []
    with open(path, newline='') as f:
        reader = csv.reader(f, delimiter=';')
        next(reader)  # skip header
        for row in reader:
            if len(row) < 3:
                continue
            try:
                time.append(float(row[0].replace(',', '.')))
                v_codec.append(float(row[1].replace(',', '.')))
                v_jack.append(float(row[2].replace(',', '.')))
            except ValueError:
                continue
    return (np.array(time) * 1e6,   # convert s -> µs
            np.array(v_codec),
            np.array(v_jack))


def plot(time_us, v_codec, v_jack):
    # clip to XLIM_US
    mask      = time_us <= XLIM_US
    time_us   = time_us[mask]
    v_codec   = v_codec[mask]
    v_jack    = v_jack[mask]

    vpp_codec = v_codec.max() - v_codec.min()
    vpp_jack  = v_jack.max()  - v_jack.min()
    atten     = vpp_codec / vpp_jack
    vfs_hi    = VCM + VFS_PP / 2
    vfs_lo    = VCM - VFS_PP / 2

    fig, ax = plt.subplots(figsize=(11, 4))

    # --- signals — single shared y axis ---
    ax.plot(time_us, v_codec, color="#1a1a1a", linewidth=1.2,
            label=f"V(IN_CODEC_BUF)  Vpp = {vpp_codec*1000:.0f} mV")
    ax.plot(time_us, v_jack,  color="#2a6ebb", linewidth=1.2, linestyle="--",
            label=f"V(IN_JACK_BUF)   Vpp = {vpp_jack:.1f} V")

    # --- ADC full-scale band ---
    ax.axhspan(vfs_lo, vfs_hi, color="#e07b39", alpha=0.08, zorder=0)
    ax.axhline(vfs_hi, color="#e07b39", linewidth=0.8, linestyle=":",
               label=f"ADC FS limits  (±{VFS_PP/2*1000:.0f} mV)")
    ax.axhline(vfs_lo, color="#e07b39", linewidth=0.8, linestyle=":")

    # --- Vcm reference ---
    ax.axhline(VCM, color="#aaaaaa", linewidth=0.6, linestyle="--",
               label=f"Vcm = {VCM*1000:.0f} mV")

    # --- Vpp bracket on codec signal ---
    x_brk = XLIM_US * 0.72
    ax.annotate('', xy=(x_brk, v_codec.max()), xytext=(x_brk, v_codec.min()),
                arrowprops=dict(arrowstyle='<->', color="#1a1a1a", lw=1.2))
    ax.text(x_brk + XLIM_US * 0.01,
            (v_codec.max() + v_codec.min()) / 2,
            f"{vpp_codec*1000:.0f} mV pp",
            color="#1a1a1a", fontsize=8, va='center')

    # --- Vpp bracket on jack signal ---
    x_brk2 = XLIM_US * 0.87
    ax.annotate('', xy=(x_brk2, v_jack.max()), xytext=(x_brk2, v_jack.min()),
                arrowprops=dict(arrowstyle='<->', color="#2a6ebb", lw=1.2))
    ax.text(x_brk2 + XLIM_US * 0.01,
            (v_jack.max() + v_jack.min()) / 2,
            f"{vpp_jack:.1f} V pp",
            color="#2a6ebb", fontsize=8, va='center')

    # --- attenuation annotation ---
    ax.text(0.02, 0.04,
            f"a = {vpp_codec*1000:.0f} mV / {vpp_jack:.1f} V = {atten:.4f}",
            transform=ax.transAxes, fontsize=8, color="#555555",
            va='bottom')

    # --- grid and axes ---
    ax.grid(True, which='both', color='#e0e0e0', linewidth=0.5)
    ax.set_xlim(0, XLIM_US)
    ax.set_xlabel("Time (µs)")
    ax.set_ylabel("Voltage (V)")
    ax.set_title("Input Stage — Transient Analysis  (100 Hz, 10 Vpp input)")

    ax.legend(frameon=True, fancybox=False, edgecolor='black',
              facecolor='white', fontsize=8)

    ax.format_coord = lambda x, y: f't={x:.2f} µs, V={y:.4f} V'

    plt.tight_layout()
    plt.savefig(script_dir / "fig_trans_input_stage.pdf", bbox_inches="tight")
    plt.savefig(script_dir / "fig_trans_input_stage.png", bbox_inches="tight", dpi=150)
    plt.show()


def main():
    time_us, v_codec, v_jack = load_csv(CSV_FILE)
    plot(time_us, v_codec, v_jack)


if __name__ == "__main__":
    main()