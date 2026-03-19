import numpy as np
import csv
from pathlib import Path

script_dir = Path(__file__).resolve().parent

values = []
with open(script_dir / "meas.csv", newline="") as f:
    reader = csv.reader(f)
    next(reader)  # skip header
    for row in reader:
        values.append(float(row[1]))

values = np.array(values)

print(f"n:    {len(values)}")
print(f"min:  {np.min(values):.3f} ms")
print(f"max:  {np.max(values):.3f} ms")
print(f"mean: {np.mean(values):.3f} ms")
print(f"std:  {np.std(values):.3f} ms")