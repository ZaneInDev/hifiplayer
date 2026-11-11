cmake_minimum_required(VERSION 3.22.1)
project("hifiplayer")

include(FetchContent)

# Pull Google's Oboe (the maintained C++ wrapper over AAudio/OpenSL ES).
# Pinned to a known-good tag for reproducible builds.
FetchContent_Declare(
        oboe
        GIT_REPOSITORY https://github.com/google/oboe.git
        GIT_TAG        1.9.0
)
FetchContent_MakeAvailable(oboe)

add_library(hifiplayer SHARED
        native-lib.cpp
        AudioEngine.cpp
        Resampler.cpp
        UsbDirectOutput.cpp
)

target_include_directories(hifiplayer PRIVATE
        ${oboe_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(hifiplayer
        oboe
        ${log-lib}
        ${android-lib}
        OpenSLES
)

# Aggressive but safe optimization for the real-time audio callback path.
target_compile_options(hifiplayer PRIVATE -Wall -Wextra -O3)
