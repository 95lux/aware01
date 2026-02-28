cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_compile_options(-fdiagnostics-color=always)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME firmware)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Aware/Src/rtos.c
    Aware/Src/audioengine.c
    Aware/Src/tape_player.c
    Aware/Src/envelope.c
    Aware/Src/user_interface.c
    Aware/Src/settings.c
    Aware/Src/control_interface.c
    Aware/Src/param_cache.c
    Aware/Src/drivers/tlv320_driver.c
    Aware/Src/drivers/adc_driver.c
    Aware/Src/drivers/gpio_driver.c
    Aware/Src/drivers/ws2812_driver.c
    Aware/Src/ws2812_animations.c
    Aware/Src/dsp/exciter.c
    Aware/Src/dsp/coefficients.c
    Aware/Src/dsp/schroeder_reverb.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    Aware/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/DSP/Lib/ARM/arm_cortexM7lfsp_math.lib"
    # Add user defined libraries
)
