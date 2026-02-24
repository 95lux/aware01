import math

def generate_sine_lut(size):
    lut = []
    for i in range(size):
        # Quarter sine wave calculation: sin(0) to sin(pi/2)
        fraction = i / (size - 1)
        val = math.sin(fraction * math.pi / 2)
        
        # Scale to Q15 (max 32767)
        int_val = round(val * 32767)
        lut.append(int_val)
    
    # Print in C-style formatting
    print(f"// Quarter Sine LUT - size {size}")

    print(f"#define FADE_LUT_LEN {size} \n")
    print(f"int16_t fade_in_lut[{size}] = {{")
    for i in range(0, size, 12):
        chunk = lut[i:i+12]
        line = ", ".join(f"{x:5}" for x in chunk)
        print(f"    {line},")
    print("};")


# Change this to 256 or 512 as needed
generate_sine_lut(256)