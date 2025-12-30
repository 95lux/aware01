
# Hardware and Firmware Development of a DSP-Capable Eurorack Module Based on STM32

**Project Plan - Hardware Rev2**

## 1. Hardware *(week 1)*
- Order and assemble **Hardware Revision 2**.
- Verify correct assembly and power integrity.

## 2. Functional Testing *(week 2-3)*
- Validate core functionality of the module:
  - **STM32 CubeMX**
    - Configure Pinout and Peripherals in STM32 CubeMX
    - Project setup via CMake
    - Research and include available Drivers and libraries
      - FREERTOS
      - WS2812?
      - CMSIS DSP
      - Audio Codec Driver has to be custom implemented

  - **MCU** 
    - flashable and debuggable via SWD
    - Setup Debugging environment in VSC
    
  - **Audio Codec**
    - Confirm accessibility and configuration via I2C.
    - Test all audio inputs and outputs.
      - Route input to output to confirm circuitry of in/out stages and correct AD/DA conversion
    - Evaluate signal integrity for suitability in DSP development.

  - **Interface**
    - Confirm working ADC read via potentiometers
    - WS2812 PWM test

## 3. Audio Engine *(week 4)*
- Implement usable **I2C drivers** for the audio codec.
- Set up **double-buffered** audio I/O.
- Define and provide **data structures** for 
  - DSP algorithm integration
  - Audio Playback (Recording buffer/playback buffer)
- Implement playback engine (vary pitch/playback speed)
- Setup FREERTOS tasks (Audio task (subtasks?), Control Interface Task, User Interface Task)

## 4. DSP Development *(week 5-6)*
- Implement real-time DSP algorithms (main module functionality, TBD).
  - For start implement/use IIR filter (available in CMSIS library)
  - Implement crude grain synthesis functionality if in time


## 5. Control Interface *(week 7)*
- Implement **control inputs**:
  - CV inputs  
  - V/Oct input with calibration (calibration and validation of correct V/Oct tracking only when in time)
  - Gate inputs

## 6. User Interface *(week 8)*
- Integrate and test **faders** (slide potentiometers).  
- Implement **RGB LED** control.  
- Handle **button input** with proper debouncing.

