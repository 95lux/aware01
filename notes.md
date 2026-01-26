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
