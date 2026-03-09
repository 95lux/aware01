Memory architecture

The custom linker script with explicit .dma_buffer section in non-cacheable RAM is a real engineering decision worth explaining — STM32H7's data cache causes DMA coherency bugs if you don't do this. Most students get burned by this and don't understand why.
Dual sample-rate design (48 kHz I/O → 24 kHz internal) to save tape buffer RAM is a nice space-vs-quality tradeoff to discuss quantitatively.
Hermite interpolation for pitch shifting

The Q48.16 fixed-point playhead with Catmull-Rom interpolation is non-trivial. Worth explaining why linear interpolation isn't sufficient (audible aliasing at high pitch ratios) and showing the quality difference.
The +3 sample guard bands on tape buffers are a subtle but important correctness detail.
Crossfade system

The two-tier crossfade (64-sample retrigger XFade + 4800-sample cyclic XFade) solves a hard real-time problem cleanly. Worth drawing a state diagram and explaining the artifact it prevents at loop boundaries.
FreeRTOS task design

The audioReadySemaphore gating playback until calibration completes is a clean solution to the boot sequencing problem — worth noting as a deliberate design choice.
DMA half/complete callbacks → xTaskNotifyFromISR() → AudioTask is the canonical pattern for ISR-safe audio on FreeRTOS; worth explaining why this is preferable to doing DSP in the ISR.
Calibration & FLASH persistence

The magic number + sector-erase FLASH storage with the 0xDEADBEEF validity check is a practical embedded pattern. V/Oct calibration with two reference points (C1, C3) and the linear scale/offset derivation is concise enough to show the math in the thesis.
XY mapper

The piecewise exponential CV-to-DSP mapping is interesting from a UX/HCI angle — why linear mapping feels wrong for musical parameters and how exponential curves fix it.
Things that are honest weaknesses to acknowledge:

The commented-out post-allpass IIR LP in the reverb (arm_biquad_cascade_df2T_init_f32 calls) — shows iteration and design decisions.
If CPU usage is close to the limit, that's worth showing honestly rather than hiding.


Focus points of the thesis

1. Pitch interpolation

Problem: linear interpolation introduces audible aliasing at high pitch ratios
Theory: why Hermite/Catmull-Rom is better (smooth derivatives)
Decision: Q48.16 fixed-point playhead + Hermite with guard samples
Eval: FFT of a sine tone at 2x, 4x, 8x pitch — compare linear vs Hermite THD/noise floor
2. Crossfade system

Problem: looping a buffer at arbitrary playhead speeds produces clicks at boundaries
Theory: two failure modes (retrigger discontinuity vs cyclic phase wrap) need different solutions
Decision: two-tier crossfade at different timescales (64-sample retrigger + 4800-sample cyclic)
Eval: waveform capture showing the artifact with/without each crossfade layer
3. Reverb RT60

Problem: perceptually plausible room simulation with controllable RT60 on constrained hardware
Theory: Schroeder structure, why prime delays, what RT60 is
Decision: 4-comb + 2-allpass, LP damping, prime stereo decorrelation
Eval: measured RT60 vs feedback parameter, compare to theoretical formula
These go in dedicated chapters 4–6. Chapter 3 covers the full system implementation (RTOS, DMA, memory layout, drivers, calibration) — that's where the breadth of embedded knowledge is documented.