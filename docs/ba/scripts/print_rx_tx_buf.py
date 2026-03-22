#!/usr/bin/env python3
from scipy.io.wavfile import write
import numpy as np
import matplotlib.pyplot as plt
from pyocd.core.helpers import ConnectHelper
import plotstyle
from pathlib import Path

script_dir = Path(__file__).resolve().parent

# === Config ===
TX_BUF_ADDR = 0x240fc000
RX_BUF_ADDR = 0x240fc100
BUF_LEN     = 128
FS          = 48000

with ConnectHelper.session_with_chosen_probe() as session:
    target = session.target
    raw_tx = target.read_memory_block8(TX_BUF_ADDR, BUF_LEN * 2)
    raw_rx = target.read_memory_block8(RX_BUF_ADDR, BUF_LEN * 2)

tx = np.frombuffer(bytearray(raw_tx), dtype=np.int16)
rx = np.frombuffer(bytearray(raw_rx), dtype=np.int16)

# interleaved stereo — take left channel only
tx_l = tx[0::2]
rx_l = rx[0::2]

samples = np.arange(len(tx_l))

# === headroom calculation ===
peak_rx = np.max(np.abs(rx_l))
peak_tx = np.max(np.abs(tx_l))
headroom_db = 20 * np.log10(32767 / max(peak_rx, peak_tx))
print(f"peak rx: {peak_rx} = {20*np.log10(peak_rx/32767):.1f} dBFS")
print(f"peak tx: {peak_tx} = {20*np.log10(peak_tx/32767):.1f} dBFS")
print(f"headroom: {headroom_db:.1f} dB")

# === plot ===
fig, ax = plt.subplots(figsize=(11, 4))

ax.plot(samples, rx_l, color="#888888", linewidth=1.2,
        linestyle="--", label="RX (input)")
ax.plot(samples, tx_l, color="#1a1a1a", linewidth=1.5,
        label="TX (output)")

ax.axhline( 32767, color="#e07b39", linewidth=0.8, linestyle=":", label="Clip limit ($\\pm$32767)")
ax.axhline(-32767, color="#e07b39", linewidth=0.8, linestyle=":")

ax.set_xlabel("Sample index")
ax.set_ylabel("Amplitude (int16)")
ax.set_ylim(-36000, 36000)
ax.set_xlim(0, len(tx_l) - 1)
ax.legend(frameon=True, fancybox=False, edgecolor='black', facecolor='white')

plt.tight_layout()
plt.savefig(script_dir / "../images/signal_tests/fig_loopback_buffer.pdf",
            bbox_inches="tight")
plt.savefig(script_dir / "fig_loopback_buffer.png",
            bbox_inches="tight", dpi=150)
plt.show()