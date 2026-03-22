#!/usr/bin/env python3
import numpy as np
import sounddevice as sd
import matplotlib.pyplot as plt
from pathlib import Path
import argparse

import plotstyle

script_dir = Path(__file__).resolve().parent
REF_FILE = script_dir / "rme_reference_fr.npy"

# === config ===
FS       = 48000
DURATION = 10.0
SWEEP_F0 = 5
SWEEP_F1 = 23900
PLOT_F0  = 20
PLOT_F1  = 20000

# === sweep ===
t = np.linspace(0, DURATION, int(FS * DURATION), endpoint=False)
k = (SWEEP_F1 / SWEEP_F0) ** (1.0 / DURATION)
sweep = np.sin(2 * np.pi * SWEEP_F0 * (k**t - 1) / np.log(k)).astype(np.float32)
fade_len = int(FS * 0.02)
sweep[-fade_len:] *= np.linspace(1, 0, fade_len)


def find_device(name):
    for i, dev in enumerate(sd.query_devices()):
        if name in dev['name']:
            return i
    raise RuntimeError(f"Device '{name}' not found")


def measure_fr(dev, in_ch, out_ch):
    print("Playing sweep...")
    recorded = sd.playrec(sweep,
                          samplerate=FS,
                          channels=1,
                          device=(dev, dev),
                          dtype='float32',
                          latency=0.2,
                          output_mapping=[out_ch],
                          input_mapping=[in_ch])
    sd.wait()
    print("Done.")

    recorded = recorded[:, 0]
    n = min(len(sweep), len(recorded))
    window = np.hanning(n)
    S_in = np.fft.rfft(sweep[:n] * window)
    S_out = np.fft.rfft(recorded[:n] * window)
    freqs = np.fft.rfftfreq(n, 1.0 / FS)
    S_in_safe = np.where(np.abs(S_in) < 1e-10, 1e-10, S_in)
    FR = 20 * np.log10(np.abs(S_out) / np.abs(S_in_safe))

    mask = (freqs >= PLOT_F0) & (freqs <= PLOT_F1)
    return freqs[mask], FR[mask]


def measure_noise_floor(dev, in_ch, out_ch, duration=10):
    print(f"Recording noise floor ({duration}s)...")
    
    # 1kHz reference tone at -20dBFS
    t = np.linspace(0, duration, int(FS * duration), endpoint=False)
    tone = (np.sin(2 * np.pi * 1000 * t) * 10**(-20/20)).astype(np.float32)
    
    recorded = sd.playrec(tone,
                          samplerate=FS,
                          channels=1,
                          device=(dev, dev),
                          dtype='float32',
                          output_mapping=[out_ch],
                          input_mapping=[in_ch])
    sd.wait()
    print("Done.")
    return recorded[:, 0]


def _plot_fr(freqs, FR, fr_mean, fr_min, title, label):
    fig, ax = plt.subplots(figsize=(11, 4))
    ax.semilogx(freqs, FR, color="#1a1a1a", linewidth=1.0, label=label)
    ax.axhline(0,       color="#aaaaaa", linewidth=0.6, linestyle="--")
    ax.axhline(fr_mean, color="#2a6ebb", linewidth=1.0, linestyle="--",
               label=f"Mean = {fr_mean:.1f} dB")
    ax.axhline(fr_min,  color="#e07b39", linewidth=1.0, linestyle=":",
               label=f"Min = {fr_min:.1f} dB")
    ax.grid(True, which='both', color='#e0e0e0', linewidth=0.5)
    ax.grid(True, which='minor', color='#f0f0f0', linewidth=0.3)
    ax.set_yticks([0, -3, -6, -10, -20, -40])
    ax.set_yticklabels(["0", "-3", "-6", "-10", "-20", "-40"])
    ax.set_xlim(PLOT_F0, PLOT_F1)
    ax.set_ylim(-20, 5)
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Magnitude (dB)")
    ax.set_title(title)
    ax.xaxis.set_major_formatter(plt.FuncFormatter(
        lambda x, _: f'{int(x):,}' if x >= 1000 else f'{int(x)}'))
    ax.xaxis.set_major_locator(plt.FixedLocator(
        [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]))
    ax.legend(frameon=True, fancybox=False, edgecolor='black', facecolor='white')

    ax.format_coord = lambda x, y: f'f={x:.1f} Hz, {y:.1f} dB'
    plt.tight_layout()
    plt.savefig(script_dir / "../images/signal_tests/fig_frequency_response.pdf", bbox_inches="tight")
    plt.savefig(script_dir / "fig_frequency_response.png", bbox_inches="tight", dpi=150)
    plt.show()


def _plot_noise_floor(signal):
    n = len(signal)
    window = np.hanning(n)
    NPB_HANN = 1.50
    S = np.fft.rfft(signal * window)
    freqs = np.fft.rfftfreq(n, 1.0 / FS)
    NF = 20 * np.log10(np.abs(S) / (n / 2 * np.sqrt(NPB_HANN)) + 1e-12)

    # exclude tone bin, integrate noise power
    tone_mask = (freqs > 990) & (freqs < 1010)
    noise_mask = (freqs >= 20) & (freqs <= 20000) & ~tone_mask
    noise_power = np.sum((np.abs(S[noise_mask]) / (n/2 * np.sqrt(NPB_HANN)))**2)
    snr_db = -20 * np.log10(np.sqrt(noise_power))
    print(f"SNR: {snr_db:.1f} dBFS")

    fig, ax = plt.subplots(figsize=(11, 4))
    ax.semilogx(freqs, NF, color="#1a1a1a", linewidth=0.8)
    ax.grid(True, which='both', color='#e0e0e0', linewidth=0.5)
    ax.set_xlim(PLOT_F0, FS // 2)
    ax.set_ylim(-140, 0)
    ax.set_yticks([0, -20, -40, -60, -80, -100, -120, -140])
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Amplitude Spectral Density (dBFS)")
    ax.set_title("Noise Floor")
    ax.xaxis.set_major_formatter(plt.FuncFormatter(
        lambda x, _: f'{int(x):,}' if x >= 1000 else f'{int(x)}'))
    ax.xaxis.set_major_locator(plt.FixedLocator(
        [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]))
    plt.tight_layout()
    plt.savefig(script_dir / "../images/signal_tests/fig_noise_floor.pdf", bbox_inches="tight")
    plt.savefig(script_dir / "fig_noise_floor.png", bbox_inches="tight", dpi=150)
    plt.show()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--calibrate",   action="store_true", help="Measure RME loopback reference and save")
    parser.add_argument("--raw",         action="store_true", help="Measure FR without reference correction")
    parser.add_argument("--noise-floor", action="store_true", help="Measure and plot noise floor")
    args = parser.parse_args()

    dev = find_device("Fireface UFX+ Pro")

    if args.noise_floor:
        signal = measure_noise_floor(dev, in_ch=IN_CH, out_ch=OUT_CH)
        _plot_noise_floor(signal)

    elif args.calibrate:
        freqs, FR = measure_fr(dev, in_ch=IN_CH, out_ch=OUT_CH)
        np.save(REF_FILE, np.stack([freqs, FR]))
        print(f"Reference saved to {REF_FILE}")

    elif args.raw:
        freqs, FR = measure_fr(dev, in_ch=IN_CH, out_ch=OUT_CH)
        band_mask = freqs <= 15000
        fr_mean = np.mean(FR[band_mask])
        fr_min  = np.min(FR[band_mask])
        _plot_fr(freqs, FR, fr_mean, fr_min,
                 title="System Frequency Response (uncorrected)",
                 label="DUT frequency response (raw)")

    else:
        if not REF_FILE.exists():
            raise RuntimeError("No reference file found. Run with --calibrate first.")
        freqs, FR = measure_fr(dev, in_ch=IN_CH, out_ch=OUT_CH)
        ref = np.load(REF_FILE)
        FR_plot = FR - ref[1]
        band_mask = freqs <= 15000
        fr_mean = np.mean(FR_plot[band_mask])
        fr_min  = np.min(FR_plot[band_mask])
        _plot_fr(freqs, FR_plot, fr_mean, fr_min,
                 title="System Frequency Response (RME reference corrected)",
                 label="DUT frequency response (RME corrected)")


if __name__ == "__main__":
    for i, dev in enumerate(sd.query_devices()):
        print(f"{i}: {dev['name']}  in:{dev['max_input_channels']} out:{dev['max_output_channels']}")

    IN_CH  = 8
    OUT_CH = 4
    main()