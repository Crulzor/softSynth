# Host (native gcc) build of the pure DSP core + its tests and the .wav
# auditioning tool. This is a SEPARATE project from the firmware build: it never
# touches the ARM toolchain, the HAL, or CubeMX. Because everything under
# App/dsp/ is HAL-free, the exact same .cpp files compile here under the host
# compiler.
#
#   cmake -S test -B test/build
#   cmake --build test/build
#   ctest --test-dir test/build --output-on-failure   # run unit tests
#   ./test/build/render_wav softsynth.wav             # audition on the desktop
#   ./test/build/render_ui ui                         # screenshot the UI pages
cmake_minimum_required(VERSION 3.22)
project(softsynth_dsp_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# App/ is the include root so headers resolve as "dsp/..." exactly as they do in
# the firmware build.
set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../App)

# Pure DSP sources, shared by the firmware and by these host tools.
add_library(dsp STATIC
    ${APP_DIR}/dsp/additive_osc.cpp
    ${APP_DIR}/dsp/delay.cpp
    ${APP_DIR}/dsp/engine.cpp
    ${APP_DIR}/dsp/envelope.cpp
    ${APP_DIR}/dsp/fx.cpp
    ${APP_DIR}/dsp/ladder.cpp
    ${APP_DIR}/dsp/lfo.cpp
    ${APP_DIR}/dsp/modfx.cpp
    ${APP_DIR}/dsp/reverb.cpp
    ${APP_DIR}/dsp/sine_table.cpp
    ${APP_DIR}/dsp/spectrum.cpp
    ${APP_DIR}/dsp/voice.cpp
    ${APP_DIR}/sequencer/chords.cpp
    ${APP_DIR}/sequencer/ext_clock.cpp
    ${APP_DIR}/sequencer/player.cpp
    ${APP_DIR}/sequencer/sequencer.cpp
)
target_include_directories(dsp PUBLIC ${APP_DIR})
target_compile_options(dsp PRIVATE -Wall -Wextra)

# UI + parameter model + the HAL-free half of the display driver, drawing into
# a stand-in framebuffer (host_framebuffer.cpp) instead of the LTDC one.
add_library(ui STATIC
    ${APP_DIR}/display_draw.cpp
    ${APP_DIR}/midi/midi.cpp
    ${APP_DIR}/params/parameters.cpp
    ${APP_DIR}/params/wave_slots.cpp
    ${APP_DIR}/ui/widgets.cpp
    ${APP_DIR}/ui/fx_page.cpp
    ${APP_DIR}/ui/midi_page.cpp
    ${APP_DIR}/ui/env_page.cpp
    ${APP_DIR}/ui/mod_page.cpp
    ${APP_DIR}/ui/osc_page.cpp
    ${APP_DIR}/ui/seq_page.cpp
    ${APP_DIR}/ui/sync_page.cpp
    ${APP_DIR}/ui/ui.cpp
    host_framebuffer.cpp
)
target_include_directories(ui PUBLIC ${APP_DIR})
target_compile_options(ui PRIVATE -Wall -Wextra)
target_link_libraries(ui PUBLIC dsp)  # spectra, chord names

# --- doctest (single-header unit test framework) ----------------------------
# doctest 2.4.11's CMakeLists declares a pre-3.5 minimum, which CMake 4 rejects;
# this shim lets its (otherwise fine) build configure under a modern CMake.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)

# --- unit test runner -------------------------------------------------------
enable_testing()
add_executable(dsp_tests
    test_main.cpp
    test_dsp.cpp
    test_ext_clock.cpp
    test_midi.cpp
    test_parameters.cpp
    test_sequencer.cpp
    test_ui.cpp
)
target_link_libraries(dsp_tests PRIVATE dsp ui doctest::doctest)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(dsp_tests)

# --- .wav auditioning tool --------------------------------------------------
add_executable(render_wav render_wav.cpp)
target_link_libraries(render_wav PRIVATE dsp)

# --- UI screenshot tool -----------------------------------------------------
add_executable(render_ui render_ui.cpp)
target_link_libraries(render_ui PRIVATE ui dsp)
