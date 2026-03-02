import numpy as np
import sounddevice as sd
import matplotlib.pyplot as plt

# List all devices
devices = sd.query_devices()
for i, dev in enumerate(devices):
    print(f"{i}: {dev['name']}")
    print(f"   Max input channels : {dev['max_input_channels']}")
    print(f"   Max output channels: {dev['max_output_channels']}\n")

fs = 48000
duration = 5.0
f0, f1 = 20, 20000

# Generate logarithmic sweep
t = np.linspace(0, duration, int(fs*duration))
sweep = np.sin(2*np.pi*(f0*t + (f1-f0)/(2*duration)*t**2))

# Play and record (single channel here)
# Example: use device 3 for output and 4 for input
out = sd.playrec(sweep, samplerate=fs, channels=1, device=(3,4))
sd.wait()

# Compute frequency response
S_in = np.fft.rfft(sweep)
S_out = np.fft.rfft(out[:,0])
FR = 20*np.log10(np.abs(S_out) / np.abs(S_in))

# Frequency axis
freqs = np.fft.rfftfreq(len(sweep), 1/fs)

# Plot
plt.figure(figsize=(10,5))
plt.semilogx(freqs, FR)
plt.title("Frequency Response")
plt.xlabel("Frequency [Hz]")
plt.ylabel("Magnitude [dB]")
plt.grid(True, which="both", ls="--")
plt.show()