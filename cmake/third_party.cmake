# ─────────────────────────────────────────────────────────────────────────────
# third_party.cmake
# ─────────────────────────────────────────────────────────────────────────────
# Owns every third-party / compat dependency APPLaunch needs but the emulator's
# own source tree doesn't ship with directly. Centralized so that adding a new
# Launcher dependency = one entry here, not three target_include_directories
# calls scattered across CMakeLists.txt.
#
# Provides three INTERFACE libraries:
#   emu::compat_stubs   — thpool stub, sys/wait.h pass-through, nlohmann shim
#   emu::miniaudio      — miniaudio.h include + per-platform audio framework links
#   emu::launcher_apply — convenience: apply both, plus emu_compat.h force-include
#
# Inputs expected:
#   APPLAUNCH_THIRDPARTY_INCLUDES (from launcher_sources.cmake)
# ─────────────────────────────────────────────────────────────────────────────

# ── compat stubs (thpool, sys/wait, nlohmann shim) ───────────────────────────
add_library(emu_compat_stubs INTERFACE)
add_library(emu::compat_stubs ALIAS emu_compat_stubs)

# IMPORTANT: src/compat houses Windows-only overrides for system headers
# (sys/wait.h, sys/queue.h). Adding it to the include path on macOS/Linux
# would shadow the real system headers and break compilation. Gate it.
if(WIN32)
    target_include_directories(emu_compat_stubs INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compat
    )
endif()

# thpool stub from the launcher submodule's emu sub-project — header-only,
# safe to expose on every platform (declares a no-op threadpool).
set(_launcher_emu_compat_stubs
    ${VENDOR_DIR}/projects/CardputerZero-Emulator/src/compat_stubs)
if(EXISTS "${_launcher_emu_compat_stubs}/thpool.h")
    target_include_directories(emu_compat_stubs INTERFACE
        ${_launcher_emu_compat_stubs}
    )
endif()

# nlohmann (header-only, vendored by user under vendor/nlohmann/json.hpp if needed)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann/json.hpp)
    target_include_directories(emu_compat_stubs INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    )
endif()

# ── miniaudio (header-only; .c TU does #define MINIAUDIO_IMPLEMENTATION) ─────
add_library(emu_miniaudio INTERFACE)
add_library(emu::miniaudio ALIAS emu_miniaudio)

target_include_directories(emu_miniaudio INTERFACE
    ${APPLAUNCH_THIRDPARTY_INCLUDES}
)

# Platform audio backends — required because miniaudio's IMPLEMENTATION TU
# pulls in OS audio APIs.
if(APPLE)
    target_link_libraries(emu_miniaudio INTERFACE
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreFoundation"
    )
elseif(UNIX AND NOT EMSCRIPTEN)
    # Linux: ALSA (default), pulse optional. miniaudio compiles whatever is
    # available; ALSA dev headers must be installed.
    target_link_libraries(emu_miniaudio INTERFACE asound dl pthread m)
elseif(EMSCRIPTEN)
    # Web: miniaudio supports Web Audio. No extra link needed beyond -pthread
    # which is set globally.
endif()
# Windows (MinGW/MSVC): miniaudio uses WASAPI/DirectSound, no -l needed.

# ── one-shot bundle for APPLaunch-style targets ──────────────────────────────
# Use this on any target that compiles ui_events.c + miniaudio + thpool.
add_library(emu_launcher_apply INTERFACE)
add_library(emu::launcher_apply ALIAS emu_launcher_apply)
target_link_libraries(emu_launcher_apply INTERFACE
    emu::compat_stubs
    emu::miniaudio
)
