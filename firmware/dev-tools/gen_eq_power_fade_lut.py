import numpy as np

N = 128
fade = np.sin(0.5*np.pi*np.arange(N)/(N-1))
fade_q15 = np.round(fade * 32767).astype(np.int16)

for v in fade_q15:
    print(f"{v},")
