# ─────────────────────────────────────────────────────────────────────────────
# launcher_sources.cmake
# ─────────────────────────────────────────────────────────────────────────────
# Single source of truth for "which Launcher files / paths the emulator pulls in".
# When the upstream M5CardputerZero-Launcher repo adds new components or moves
# files around, this file is the ONLY place that needs an update — the rest of
# CMakeLists.txt consumes the variables defined here.
#
# Inputs (must be set by caller before include()-ing this file):
#   VENDOR_DIR  — path to vendor/M5CardputerZero-Launcher
#
# Outputs:
#   APPLAUNCH                    — APPLaunch project main/ dir
#   USERDEMO                     — UserDemo project main/ dir
#   APPLAUNCH_UI_C / _CPP        — UI sources to compile
#   APPLAUNCH_KBD                — keyboard_input.c (path varies across launcher revs)
#   APPLAUNCH_HAL_SDL_C / _CPP   — SDL HAL sources
#   USERDEMO_UI_C                — UserDemo UI sources
#   APPLAUNCH_INCLUDES           — list of include dirs needed to compile APPLaunch
#   APPLAUNCH_THIRDPARTY_INCLUDES — list of third-party include dirs (miniaudio, …)
# ─────────────────────────────────────────────────────────────────────────────

if(NOT DEFINED VENDOR_DIR)
    message(FATAL_ERROR "launcher_sources.cmake: VENDOR_DIR must be set before include()")
endif()

set(APPLAUNCH ${VENDOR_DIR}/projects/APPLaunch/main)
set(USERDEMO  ${VENDOR_DIR}/projects/UserDemo/main)

# ── generate page_app.h ──────────────────────────────────────────────────────
# Launcher's SConstruct runs generate_page_app_includes.py at build time to
# produce ui/components/page_app.h from the .hpp files in ui/components/page_app/.
# That file is .gitignore'd in the launcher repo (so writing it doesn't dirty
# `git status`). We replicate the generator in pure CMake here so a vanilla
# `cmake .. && make` works without invoking SCons.
set(_page_app_dir   ${APPLAUNCH}/ui/components/page_app)
set(_page_app_h     ${APPLAUNCH}/ui/components/page_app.h)
if(IS_DIRECTORY "${_page_app_dir}")
    file(GLOB _page_app_headers
        RELATIVE ${APPLAUNCH}/ui/components
        CONFIGURE_DEPENDS
        ${_page_app_dir}/*.hpp
    )
    list(SORT _page_app_headers)
    if(_page_app_headers)
        file(WRITE  "${_page_app_h}" "#pragma once\n\n")
        foreach(_h ${_page_app_headers})
            file(APPEND "${_page_app_h}" "#include \"${_h}\"\n")
        endforeach()
    else()
        message(WARNING "launcher_sources.cmake: page_app/ exists but contains no .hpp files")
    endif()
endif()

# ── source globs ─────────────────────────────────────────────────────────────
file(GLOB_RECURSE APPLAUNCH_UI_C   ${APPLAUNCH}/ui/*.c)
file(GLOB_RECURSE APPLAUNCH_UI_CPP ${APPLAUNCH}/ui/*.cpp)

# keyboard_input.c moved between revs (src/ → hal/). Pick whichever exists.
set(APPLAUNCH_KBD "")
foreach(_kbd
    ${APPLAUNCH}/hal/keyboard_input.c
    ${APPLAUNCH}/src/keyboard_input.c)
    if(EXISTS "${_kbd}")
        list(APPEND APPLAUNCH_KBD "${_kbd}")
    endif()
endforeach()

file(GLOB APPLAUNCH_HAL_SDL_C   ${APPLAUNCH}/hal/sdl/*.c)
file(GLOB APPLAUNCH_HAL_SDL_CPP ${APPLAUNCH}/hal/sdl/*.cpp)

file(GLOB_RECURSE USERDEMO_UI_C ${USERDEMO}/ui/*.c)

# ── include dirs ─────────────────────────────────────────────────────────────
set(APPLAUNCH_INCLUDES
    ${APPLAUNCH}/ui
    ${APPLAUNCH}/include
    ${APPLAUNCH}
    ${APPLAUNCH}/hal
)

set(USERDEMO_INCLUDES
    ${USERDEMO}/ui
    ${USERDEMO}/include
    ${USERDEMO}
)

# ── runtime asset directories ────────────────────────────────────────────────
# Where APPLaunch expects to find PNG/font assets at runtime. Path has moved
# between launcher revs (ui/images → APPLaunch/share/images), so probe for it.
set(APPLAUNCH_IMAGES_DIR "")
foreach(_candidate
    ${VENDOR_DIR}/projects/APPLaunch/APPLaunch/share/images
    ${APPLAUNCH}/ui/images)
    if(IS_DIRECTORY "${_candidate}")
        set(APPLAUNCH_IMAGES_DIR "${_candidate}")
        break()
    endif()
endforeach()
if(NOT APPLAUNCH_IMAGES_DIR)
    message(WARNING
        "launcher_sources.cmake: could not locate APPLaunch images directory. "
        "The emulator will run but app icons will be missing.")
endif()

# ── third-party includes shipped inside the launcher submodule ───────────────
# These are kept in a separate variable so the emulator can decide per-target
# whether to expose them.
set(APPLAUNCH_THIRDPARTY_INCLUDES "")

# Miniaudio (header-only; ui_events.c does #define MINIAUDIO_IMPLEMENTATION)
set(_miniaudio_inc ${VENDOR_DIR}/ext_components/Miniaudio/include)
if(EXISTS "${_miniaudio_inc}/miniaudio.h")
    list(APPEND APPLAUNCH_THIRDPARTY_INCLUDES "${_miniaudio_inc}")
else()
    message(WARNING
        "launcher_sources.cmake: miniaudio.h not found at ${_miniaudio_inc}. "
        "APPLaunch likely will not compile. Update the launcher submodule.")
endif()
