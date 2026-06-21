cmake_minimum_required(VERSION 3.16)
project(prox_companion LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Rdzeń bez I/O zależności zewnętrznych (parser + tailer). Faza 2 dołoży tu
# libdatachannel / Opus / miniaudio przez vcpkg.
add_library(prox_core STATIC
    src/prox_parser.cpp
    src/log_tailer.cpp
    src/falloff.cpp
    src/peer_proto.cpp
    src/mixer.cpp)
target_include_directories(prox_core PUBLIC src)

# gcc < 9 wymaga osobnego linkowania std::filesystem.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(prox_core PUBLIC stdc++fs)
endif()

add_executable(prox_companion src/main.cpp)
target_link_libraries(prox_companion PRIVATE prox_core)

enable_testing()
add_executable(prox_tests tests/test_parser.cpp)
target_link_libraries(prox_tests PRIVATE prox_core)
add_test(NAME prox_tests COMMAND prox_tests)

add_executable(prox_tests_phase2 tests/test_phase2.cpp)
target_link_libraries(prox_tests_phase2 PRIVATE prox_core)
add_test(NAME prox_tests_phase2 COMMAND prox_tests_phase2)

# ---------------------------------------------------------------------------
# Voice layer (Phase 2): WebRTC mesh + audio. Requires vcpkg libs and MSVC.
# Configure with: -DPROX_WITH_VOICE=ON -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# The pure-core MinGW build leaves this OFF and stays dependency-free.
# ---------------------------------------------------------------------------
option(PROX_WITH_VOICE "Build the voice layer (libdatachannel/opus/miniaudio via vcpkg)" OFF)
if(PROX_WITH_VOICE)
    find_package(LibDataChannel CONFIG REQUIRED)
    find_package(Opus CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    # miniaudio is header-only; its include dir comes from the vcpkg toolchain.
    # speexdsp ships no CMake config — locate the import library directly.
    find_library(SPEEXDSP_LIBRARY NAMES speexdsp REQUIRED)

    # Voice building blocks: codec, audio I/O (miniaudio impl in its own TU), DSP, mixing.
    add_library(prox_voice_core STATIC
        src/opus_codec.cpp
        src/audio_io.cpp
        src/miniaudio_impl.cpp
        src/audio_dsp.cpp)
    target_link_libraries(prox_voice_core PUBLIC prox_core Opus::opus ${SPEEXDSP_LIBRARY})

    add_executable(prox_probe src/probe.cpp)
    target_link_libraries(prox_probe PRIVATE prox_core LibDataChannel::LibDataChannel Opus::opus)

    # Local loopback demo (mic -> opus -> decode -> speakers).
    add_executable(prox_audiotest src/audiotest.cpp)
    target_link_libraries(prox_audiotest PRIVATE prox_voice_core)

    # Embed the mod into the app so the .exe can auto-install it (single source of truth: mod/).
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../mod/modinfo.lua" MOD_MODINFO)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../mod/modmain.lua" MOD_MODMAIN)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../mod/scripts/proximity_export.lua" MOD_EXPORT)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/mod_assets.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/mod_assets.h" @ONLY)

    # Networking: signaling client + WebRTC mesh + proximity mixing + mod auto-install.
    add_library(prox_net STATIC
        src/signaling_client.cpp
        src/voice_client.cpp
        src/mod_installer.cpp)
    target_include_directories(prox_net PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(prox_net PUBLIC
        prox_voice_core
        LibDataChannel::LibDataChannel
        nlohmann_json::nlohmann_json)
    if(WIN32)
        target_link_libraries(prox_net PUBLIC advapi32)
    endif()

    # The proximity voice app. On Windows it includes a native Win32 GUI (default mode).
    if(WIN32)
        add_executable(prox_voice src/main_voice.cpp src/gui_win.cpp)
        target_link_libraries(prox_voice PRIVATE prox_net comctl32)
    else()
        add_executable(prox_voice src/main_voice.cpp)
        target_link_libraries(prox_voice PRIVATE prox_net)
    endif()

    # Ship the CA bundle next to the exe (used to TLS-verify wss:// signaling).
    add_custom_command(TARGET prox_voice POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/cacert.pem"
            "$<TARGET_FILE_DIR:prox_voice>/cacert.pem")

    add_executable(prox_tests_voice tests/test_voice.cpp)
    target_link_libraries(prox_tests_voice PRIVATE prox_voice_core)
    add_test(NAME prox_tests_voice COMMAND prox_tests_voice)
endif()
