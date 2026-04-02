import csv
import math
import sys
from collections import defaultdict


def hz_to_cents(f_measured, f_expected):
    return 1200 * math.log2(f_measured / f_expected)


def expected_freq(voltage, ref_freq=1000.0):
    return ref_freq * (2 ** voltage)


def parse_freq(val):
    val = val.strip()
    if val.endswith('k') or val.endswith('K'):
        return float(val[:-1]) * 1000
    return float(val)


def main(csv_path):
    raw = []
    with open(csv_path, newline='') as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) < 2:
                continue
            try:
                voltage = float(row[0].strip())
                freq = parse_freq(row[1])
                raw.append((voltage, freq))
            except ValueError:
                continue

    raw.sort(key=lambda x: x[0])

    groups = []
    current_group = [raw[0]]
    for v, f in raw[1:]:
        if abs(v - current_group[0][0]) < 0.02:
            current_group.append((v, f))
        else:
            groups.append(current_group)
            current_group = [(v, f)]
    groups.append(current_group)

    header = (f"{'Voltage (V)':>12}  {'Expected (Hz)':>14}  {'Mean (Hz)':>10}  "
              f"{'Dev (Hz)':>10}  {'Dev (cents)':>12}  {'N':>3}")
    print(header)
    print("-" * len(header))

    for group in groups:
        voltages = [v for v, f in group]
        freqs    = [f for v, f in group]

        mean_voltage  = sum(voltages) / len(voltages)
        mean_freq     = sum(freqs)    / len(freqs)
        f_exp         = expected_freq(mean_voltage)
        deviation_hz  = mean_freq - f_exp
        deviation_cents = hz_to_cents(mean_freq, f_exp)
        cents_list    = [hz_to_cents(f, f_exp) for f in freqs]

        print(f"{mean_voltage:>+12.4f}  {f_exp:>14.1f}  {mean_freq:>10.1f}  "
              f"{deviation_hz:>+10.1f}  {deviation_cents:>+12.2f}  {len(group):>3}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python voct_analysis.py <data.csv>")
        sys.exit(1)
    main(sys.argv[1])