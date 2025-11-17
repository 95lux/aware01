



## Input AC coupling

TLV has configurable input impedance. 10k, 20k and 40k (https://www.ti.com/lit/ml/slaa557/slaa557.pdf?ts=1763301473568&ref_url=https%253A%252F%252Fchatgpt.com%252F).
We choose 40k to load the source as little as possible.

The AC-coupling capacitor and the input impedance of the codecs ADC for an RC highpass filter.
It can be calculated as:

$$
f_c = \frac{1}{2\pi R_{eq} C_c}
$$

The resulting cutoff frequency $f_c$ should be well under the hearable range, thus $20Hz$, to prevent altering the bass integrity. 
To minimize phase shifting in the bass - which can resulting a "losening" bass sound - the cutoff has to be well below it as well #TODO: search sources that describe phase shifting

With a $C_c = 2.2\mu F$ and the configured input impedance of $40k\Omega$ as our $R_{eq}$, the formula yields:

$$
\frac{1}{2\pi 40k\Omega 2.2\mu F} = 1.809Hz
$$

## Input Scaling

The TLVs datasheet states a maximum input voltage on the analog input pins of $0.5V_{RMS}$, at a CM *(Common Mode Voltage)* of $0.9V$ (see page 8 secion Recommended Operating Conditions). To reach these levels, the input has to be scaled from Eurorack voltage levels into the correct TLVs level.
https://www.ti.com/lit/ml/slaa557/slaa557.pdf?ts=1763296406843 page 86

Even though Eurorack audio signals may reach $\pm10V$, they typically are $\pm 5V$, so $10V_{pp}$. We will assume these levels for further calculation of the scaling. (reference: Doepfer A-110-2 VCO, outputs $\pm 5V$).

The attenuation $a$ can be calculated with 

$$
V_{ADC_{peak}} = V_{ADC_{RMS}} * \sqrt{2}
$$

$$
V_{ADC_{pp}} = 2 \times V_{ADC_{peak}} = 1.414V_{pp}
$$ 

and 
$$
V_{eurorack} = 5V_{pp}
$$

$$
a = \frac{V_{ADC_{pp}}}{V_{eurorack}} = 0.283
$$

To load the source as little as possible, an input impedance of 100k can be chosen. This yields 2 of 3 parameteres of the formula for the inverted amp gain, and only the feedback resistor $R_f$ has to be calculated from that:

$$
V_{ADC} = -V_{eurorack}(\frac{R_f}{R_{in}})
$$

$$
<=> R_f = \frac{V_{ADC} R_{in}}{-V_{eurorack}}
$$