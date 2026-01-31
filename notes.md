- PC9 (GATE3_IN) and PA9 (GATE1_IN) both use the same EXTI line (EXTI9).
    So no interrupts can be fired from both.
    Possible solution: rewire PC9 to free pin PC6
    For now omit GATE3_IN interrupt, since no functionality has been chosen anyway.

- PC12 (RGB_LED_DATA) cannot generate inverted PWM, which is required because of the schmitt trigger level shifting circuit.
    Possible solution: generate normal PWM via DMA and switch Low/High times of WS2812 signal. This should work despite flipping the order of high/low phase
        since WS2812 protocol is resetting after each low phase, the first High phase is actually the generated low phase of the PWM signal,because schmitt trigger inverts it.

- Clamping diodes D4, D5 are wrong components. BAT54S is needed, BAT54C was ordered. Furthermore the schematic design is wrong. Pin 1,2 are flipped in the design. 
    Pin 2 -> AVDD, Pin 1 -> GND

- Wrong button footprint was chosen for Push buttons SW1, SW2. To accomondate for that, buttons with the correct footprint need to be ordered



Pitch Tracking algorithm:

0. Calibrate pitch scaling:
a. read ADC value at C1/1V signal:
    $c1_{adc}$

b. read ADC value at C3/3V signal:
    $c3_{adc}$

c. normalize both
    $c1_{norm}$
    $c3_{norm}$

d. calculate pitch scaling:
    pitch_scaling = \frac{24}{c3_{norm} - c1_{norm}}


1. read ADC value.
    - ADC values range from [0..65536]
    - low CV causes high ADC value, because of inverted opamps!
    - 1V CV corresponds to a pitch of C1
    - Opamp circuit inputs are designed to work with voltages from [-1.5V...5V]
        this effectively maps this voltage across the ADC range of [0...65536]

2. normalize CV value
    $cv_{norm} = \frac{cv_{adc}}{65536}$

    this maps ADC values to a float from [0..1]
    values close to 1 mean low CV voltages, close to 0 mean high voltage
    
3. scale pitch by applying pitch_scale offset in semitones from reference
    $cv_{semitones} = cv_{normalized} / pitch_scale$

4. convert pitch in semitones into playback speed. This has to be exponential, since semitones correspond logarithmicly to frequency, ergo playback speed:
    $pb_speed = 2^{\frac{cv_semitones}{12}}$
