import numpy as np
import pandas as pd
from scipy.signal import tf2sos

# Load CSV, skip first row if it has headers
df = pd.read_csv("coeffs.csv", header=0, names=["b", "a"])

# Convert columns to float
b = df["b"].astype(float).to_numpy()
a = df["a"].astype(float).to_numpy()

# Convert to SOS
sos = tf2sos(b, a)
print("SOS matrix:")
print(sos)

# -----------------------------
# Convert to SOS (biquad cascade)
# -----------------------------
sos = tf2sos(b, a)

BIQUAD_CASCADE_NUM_STAGES = sos.shape[0]

# -----------------------------
# Convert to CMSIS-DSP format: [b0, b1, b2, -a1, -a2]
# -----------------------------
coeffs_2d = []
for s in sos:
    b0, b1, b2, a0, a1, a2 = s
    coeffs_2d.append([b0, b1, b2, -a1, -a2])

print("\n")

# -----------------------------
# Print C struct arrays
# -----------------------------
print("// Coefficients for IIR filter in CMSIS-DSP format (b0, b1, b2, -a1, -a2)\n")
print(f"#define BIQUAD_CASCADE_NUM_STAGES {BIQUAD_CASCADE_NUM_STAGES}\n")
print("extern float32_t iir_coeffs[BIQUAD_CASCADE_NUM_STAGES * 5];\n\n")


print("float32_t iir_coeffs[BIQUAD_CASCADE_NUM_STAGES * 5] = {")

for stage in coeffs_2d:
    print("    " + ", ".join(f"{x:.8f}" for x in stage) + " ,")
print("};\n")