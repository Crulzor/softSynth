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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME softSynth)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)

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
    # User application (C++)
    App/app.cpp
    App/display.cpp

    # HAL-free, also host-built + unit-tested under test/
    App/display_draw.cpp
    App/dsp/additive_osc.cpp
    App/dsp/engine.cpp
    App/dsp/envelope.cpp
    App/dsp/ladder.cpp
    App/dsp/sine_table.cpp
    App/dsp/spectrum.cpp
    App/dsp/voice.cpp
    App/midi/midi.cpp
    App/params/parameters.cpp
    App/params/wave_slots.cpp
    App/sequencer/chords.cpp
    App/sequencer/player.cpp
    App/sequencer/sequencer.cpp
    App/ui/widgets.cpp
    App/ui/debug_page.cpp
    App/ui/midi_page.cpp
    App/ui/env_page.cpp
    App/ui/mod_page.cpp
    App/ui/osc_page.cpp
    App/ui/seq_page.cpp
    App/ui/ui.cpp

    # Platform drivers
    App/platform/audio_out.cpp
    App/platform/codec.cpp
    App/platform/cpu_load.cpp
    App/platform/i2c4.cpp
    App/platform/sysclock.cpp
    App/platform/touch.cpp
    App/platform/usbhost.cpp

    # Vendored ST WM8994 codec driver (C)
    App/platform/wm8994/wm8994.c
    App/platform/wm8994/wm8994_reg.c

    # Vendored ST USB Host Library (C) + our glue and MIDI class
    App/platform/usbh/usbh_core.c
    App/platform/usbh/usbh_ctlreq.c
    App/platform/usbh/usbh_ioreq.c
    App/platform/usbh/usbh_pipes.c
    App/platform/usbh/usbh_conf.c
    App/platform/usbh/usbh_midi.c
)

# The DSP runs in the audio interrupt (12 additive oscillators + 6 ladder
# filters per block): build it optimised even in Debug, where everything else
# is -O0. -ffp-contract=fast lets GCC fuse multiply-adds (VFMA), which strict
# -std=c++17 otherwise turns off.
set_source_files_properties(
    App/dsp/additive_osc.cpp
    App/dsp/engine.cpp
    App/dsp/envelope.cpp
    App/dsp/ladder.cpp
    App/dsp/voice.cpp
    PROPERTIES COMPILE_OPTIONS "-O2;-ffp-contract=fast")

# The HAL-free UI / model code is most of the flash image, and at -O0 it no
# longer fits the 128 KB with the synth engine in. -Og keeps it debuggable
# (it is what GCC recommends for debugging) at a fraction of the size.
# Release builds use -Os for everything anyway; these options come after the
# build type's -O0, so they win in Debug only where it matters.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_source_files_properties(
        App/display_draw.cpp
        App/midi/midi.cpp
        App/params/parameters.cpp
        App/params/wave_slots.cpp
        App/sequencer/chords.cpp
        App/sequencer/player.cpp
        App/sequencer/sequencer.cpp
        App/dsp/sine_table.cpp
        App/dsp/spectrum.cpp
        App/ui/widgets.cpp
        App/ui/debug_page.cpp
        App/ui/midi_page.cpp
        App/ui/env_page.cpp
        App/ui/mod_page.cpp
        App/ui/osc_page.cpp
        App/ui/seq_page.cpp
        App/ui/ui.cpp
        PROPERTIES COMPILE_OPTIONS "-Og")
endif()

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App
    App/platform
    App/platform/usbh
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

    # Add user defined libraries
)
