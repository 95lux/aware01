#!/usr/bin/env python3
from scipy.io.wavfile import write
import numpy as np
from pyocd.core.helpers import ConnectHelper
import csv
import argparse
import matplotlib.pyplot as plt

# === Config ===
BUF_ADDR_L = 0x24000110
BUF_ADDR_R = 0x24075410
BUF_LEN    = 240000
FS         = 48000  # audio sample rate

# === CLI parser ===
parser = argparse.ArgumentParser(description="Dump STM32 tape buffers")
parser.add_argument("--wav", action="store_true", help="Dump WAV file")
parser.add_argument("--csv", action="store_true", help="Dump CSV file")
parser.add_argument("--plot", action="store_true", help="Plot both channels")
parser.add_argument("--name", type=str, default="tape_dump", help="Base filename for dump files")
args = parser.parse_args()

if not (args.wav or args.csv or args.plot):
    print("Nothing selected, defaulting to WAV + CSV + plot")
    args.wav = True
    args.csv = True
    args.plot = True

# === Connect to target ===
with ConnectHelper.session_with_chosen_probe() as session:
    target = session.target
    raw_l = target.read_memory_block8(BUF_ADDR_L, BUF_LEN * 2)
    raw_r = target.read_memory_block8(BUF_ADDR_R, BUF_LEN * 2)

# === Convert to numpy arrays ===
left  = np.frombuffer(bytearray(raw_l), dtype=np.int16)
right = np.frombuffer(bytearray(raw_r), dtype=np.int16)
stereo = np.stack((left, right), axis=1)

# === Dump WAV ===
if args.wav:
    wav_file = f"{args.name}.wav"
    write(wav_file, FS, stereo)
    print(f"Saved {wav_file} with shape:", stereo.shape)

# === Dump CSV ===
if args.csv:
    csv_file = f"{args.name}.csv"
    with open(csv_file, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Left", "Right"])
        for l, r in zip(left, right):
            writer.writerow([l, r])
    print(f"Saved {csv_file} with {len(left)} samples per channel")
    
# === Plot ===
if args.plot:
    t = np.arange(left.size) / FS
    plt.figure(figsize=(12,4))
    plt.plot(t, left, label="Left")
    plt.plot(t, right, label="Right")
    plt.xlabel("Time [s]")
    plt.ylabel("Amplitude")
    plt.title("Tape Buffer Playback")
    plt.legend()
    plt.tight_layout()
    plt.show()
