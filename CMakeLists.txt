cmake_minimum_required(VERSION 3.16)
project(cardputer-emu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Paths
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/M5CardputerZero-Launcher)

# Source-of-truth for which Launcher files / paths the emulator pulls in.
# Edit cmake/launcher_sources.cmake when the upstream submodule reshuffles things.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
# Apply temporary patches against the vendor submodule. Each patch links to
# its tracking PR; entries are removed when upstream merges.
include(patches/apply_vendor_workarounds)
include(launcher_sources)
include(third_party)

# ---------------------------------------------------------------------------
# SDL2 + Freetype (skip for Emscripten — uses its own ports)
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    if(APPLE)
        if(EXISTS "/opt/homebrew/opt/sdl2")
            list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/sdl2")
        elseif(EXISTS "/usr/local/opt/sdl2")
            list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/sdl2")
        endif()
    endif()

    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED sdl2)
    endif()

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FREETYPE freetype2)
    endif()
    if(NOT FREETYPE_FOUND)
        find_package(Freetype QUIET)
        if(Freetype_FOUND)
            set(FREETYPE_INCLUDE_DIRS ${FREETYPE_INCLUDE_DIRS})
            set(FREETYPE_LIBRARIES Freetype::Freetype)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# LVGL 9.5
# ---------------------------------------------------------------------------
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "" FORCE)
add_definitions(-DLV_CONF_INCLUDE_SIMPLE)
if(EMSCRIPTEN OR WIN32)
    add_definitions(-DLV_USE_FREETYPE=0)
endif()
if(EMSCRIPTEN)
    add_compile_options(-sUSE_SDL=2 -pthread)
endif()

# SDL2 parent include for <SDL2/SDL.h>
if(NOT EMSCRIPTEN)
    if(SDL2_FOUND AND TARGET SDL2::SDL2)
        get_target_property(_sdl2_inc SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
        if(_sdl2_inc)
            foreach(_dir ${_sdl2_inc})
                get_filename_component(_parent "${_dir}" DIRECTORY)
                include_directories("${_parent}")
            endforeach()
        endif()
    else()
        foreach(_dir ${SDL2_INCLUDE_DIRS})
            get_filename_component(_parent "${_dir}" DIRECTORY)
            include_directories("${_parent}")
        endforeach()
    endif()
    if(FREETYPE_INCLUDE_DIRS)
        include_directories(${FREETYPE_INCLUDE_DIRS})
    endif()
endif()

# Build LVGL as static, apps export symbols via -rdynamic / POSITION_INDEPENDENT_CODE
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(lib/lvgl)

# ---------------------------------------------------------------------------
# Helper: common link libraries for app targets
# ---------------------------------------------------------------------------
macro(emu_link_deps target)
    target_link_libraries(${target} PRIVATE lvgl)
    if(TARGET SDL2::SDL2)
        target_link_libraries(${target} PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(${target} PRIVATE ${SDL2_LIBRARIES})
    endif()
    if(FREETYPE_FOUND AND FREETYPE_LINK_LIBRARIES)
        target_link_libraries(${target} PRIVATE ${FREETYPE_LINK_LIBRARIES})
        target_link_directories(${target} PRIVATE ${FREETYPE_LIBRARY_DIRS})
    elseif(FREETYPE_LIBRARIES)
        target_link_libraries(${target} PRIVATE ${FREETYPE_LIBRARIES})
    else()
        target_link_directories(${target} PRIVATE /opt/homebrew/lib)
        target_link_libraries(${target} PRIVATE freetype)
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(${target} PRIVATE Threads::Threads)
endmacro()

# ===========================================================================
# Native-only targets (skipped for Emscripten)
# ===========================================================================
if(NOT EMSCRIPTEN)

option(EMU_SKIP_APPLAUNCH "Skip APPLaunch (Unix-only deps)" OFF)

# APPLaunch source files come from cmake/launcher_sources.cmake (already included above):
#   APPLAUNCH_UI_C / APPLAUNCH_UI_CPP / APPLAUNCH_KBD
#   APPLAUNCH_HAL_SDL_C / APPLAUNCH_HAL_SDL_CPP

# ---------------------------------------------------------------------------
# APP: APPLaunch shared lib (Unix only — Windows statically links into exe)
# ---------------------------------------------------------------------------
if(NOT EMU_SKIP_APPLAUNCH AND NOT WIN32)

add_library(APPLaunch SHARED
    ${APPLAUNCH_UI_C}
    ${APPLAUNCH_UI_CPP}
    ${APPLAUNCH_KBD}
    ${APPLAUNCH_HAL_SDL_C}
    ${APPLAUNCH_HAL_SDL_CPP}
)
if(WIN32)
    # MinGW needs: sys/queue.h, sys/wait.h stubs (no fork), unistd extras
    target_include_directories(APPLaunch BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/compat)
endif()
target_include_directories(APPLaunch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${APPLAUNCH_INCLUDES}
)
target_link_libraries(APPLaunch PRIVATE emu::launcher_apply)
target_compile_definitions(APPLaunch PRIVATE HAL_PLATFORM_SDL=1)
if(WIN32)
    target_compile_options(APPLaunch PRIVATE
        "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat_win.h"
        -Wno-c++11-narrowing)
else()
    target_compile_options(APPLaunch PRIVATE
        "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat.h"
        -Wno-c++11-narrowing)
endif()
# App dylibs resolve LVGL symbols at load time from the emulator process
if(APPLE)
    target_link_options(APPLaunch PRIVATE -undefined dynamic_lookup)
elseif(WIN32)
    # On Windows, link against lvgl directly
    target_link_libraries(APPLaunch PRIVATE lvgl)
else()
    target_link_options(APPLaunch PRIVATE -Wl,--unresolved-symbols=ignore-all)
endif()
endif() # NOT EMU_SKIP_APPLAUNCH

# ---------------------------------------------------------------------------
# APP: UserDemo.dylib
# (USERDEMO_UI_C / USERDEMO_INCLUDES come from cmake/launcher_sources.cmake)
# ---------------------------------------------------------------------------
add_library(UserDemo SHARED ${USERDEMO_UI_C})
target_include_directories(UserDemo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${USERDEMO_INCLUDES}
)
if(MSVC)
    target_compile_options(UserDemo PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat.h)
else()
    target_compile_options(UserDemo PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat.h)
endif()
if(APPLE)
    target_link_options(UserDemo PRIVATE -undefined dynamic_lookup)
elseif(WIN32)
    target_link_libraries(UserDemo PRIVATE lvgl)
else()
    target_link_options(UserDemo PRIVATE -Wl,--unresolved-symbols=ignore-all)
endif()

# ---------------------------------------------------------------------------
# Emulator executable
# On Windows: statically link UserDemo (DLL would have duplicate LVGL globals)
# On Unix: thin shell that loads app via dlopen
# ---------------------------------------------------------------------------
if(WIN32 AND NOT EMU_SKIP_APPLAUNCH)
    # Windows: statically link APPLaunch (avoids DLL duplicate LVGL globals)
    add_executable(cardputer-emu
        src/main.cpp src/device_skin.cpp
        src/emu_launcher_stubs.c src/png_to_sdl_surface.c
        ${APPLAUNCH_UI_C} ${APPLAUNCH_UI_CPP} ${APPLAUNCH_KBD}
        ${APPLAUNCH_HAL_SDL_C} ${APPLAUNCH_HAL_SDL_CPP})
    target_include_directories(cardputer-emu PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compat  # win32_stubs.h, sys/queue.h
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${APPLAUNCH_INCLUDES})
    target_link_libraries(cardputer-emu PRIVATE emu::launcher_apply)
    if(WIN32)
        target_compile_options(cardputer-emu PRIVATE
            "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat_win.h"
            -Wno-c++11-narrowing)
    endif()
    target_compile_definitions(cardputer-emu PRIVATE EMU_STATIC_APP=1 HAL_PLATFORM_SDL=1)
    # MinGW: allow multiple definitions (APPLaunch overrides LVGL's SDL keyboard)
    target_link_options(cardputer-emu PRIVATE -Wl,--allow-multiple-definition)
elseif(WIN32)
    # Fallback: UserDemo only
    add_executable(cardputer-emu
        src/main.cpp src/device_skin.cpp src/png_to_sdl_surface.c
        ${USERDEMO_UI_C})
    target_include_directories(cardputer-emu PRIVATE ${USERDEMO_INCLUDES})
    target_compile_definitions(cardputer-emu PRIVATE EMU_STATIC_APP=1)
else()
    # macOS / Linux: thin shell that dlopen's the app. It must EXPORT the
    # symbols the app dylib expects to resolve from the host process —
    # LV_EVENT_BATTERY etc. — so emu_launcher_stubs.c is linked here too.
    add_executable(cardputer-emu
        src/main.cpp src/device_skin.cpp src/png_to_sdl_surface.c
        src/emu_launcher_stubs.c)
endif()
add_dependencies(cardputer-emu lvgl lvgl_thorvg)
# Export all symbols so dlopen'd apps can resolve LVGL functions
if(APPLE)
    set_target_properties(cardputer-emu PROPERTIES ENABLE_EXPORTS ON)
endif()

target_include_directories(cardputer-emu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)
if(NOT MSVC)
    target_compile_options(cardputer-emu PRIVATE -Wno-c++11-narrowing)
endif()

# Force-load entire LVGL so all symbols are available to dlopen'd apps
if(APPLE)
    target_link_libraries(cardputer-emu PRIVATE
        -Wl,-force_load,$<TARGET_FILE:lvgl>
        -Wl,-force_load,$<TARGET_FILE:lvgl_thorvg>
    )
elseif(MSVC)
    target_link_libraries(cardputer-emu PRIVATE lvgl lvgl_thorvg)
    target_link_options(cardputer-emu PRIVATE
        /WHOLEARCHIVE:lvgl /WHOLEARCHIVE:lvgl_thorvg)
elseif(WIN32)
    # MinGW: no whole-archive (app is static-linked, no DLL symbol export needed)
    target_link_libraries(cardputer-emu PRIVATE lvgl lvgl_thorvg)
    # WinSock for gethostname() in launcher's ui_app_hack.hpp.
    target_link_libraries(cardputer-emu PRIVATE ws2_32)
else()
    # Linux: --whole-archive for dlopen'd apps
    target_link_libraries(cardputer-emu PRIVATE
        -Wl,--whole-archive $<TARGET_FILE:lvgl> $<TARGET_FILE:lvgl_thorvg> -Wl,--no-whole-archive
    )
endif()

# Note: we no longer link SDL2_image. PNG loading goes through
# src/png_to_sdl_surface.c, which delegates to LVGL's bundled lodepng. This
# drops 19 transitive dylibs from the macOS bundle (libavif, libjxl, libwebp,
# libtiff, etc.) and the Gatekeeper "unable to verify" prompt that fires on
# every one of them at first launch.

# ── SDL2 + Freetype linkage ──────────────────────────────────────────────────
# On macOS we statically link SDL2/Freetype/libpng into the executable so the
# distributed bundle carries NO third-party dylibs at all. Combined with
# dropping SDL2_image above, the only Mach-O files we ship are cardputer-emu
# (static, self-contained) plus our own libAPPLaunch/libUserDemo dylibs — all
# of which get ad-hoc codesigned in CI. Result: zero Gatekeeper quarantine
# prompts. The static .a files live next to the dylibs in Homebrew.
option(EMU_MACOS_STATIC_DEPS "Statically link SDL2/Freetype on macOS" ON)

if(APPLE AND EMU_MACOS_STATIC_DEPS)
    # Resolve Homebrew prefixes (arm64 default, Intel fallback).
    foreach(_pfx /opt/homebrew /usr/local)
        if(EXISTS "${_pfx}/opt/sdl2/lib/libSDL2.a")
            set(_brew "${_pfx}")
            break()
        endif()
    endforeach()
    if(NOT _brew)
        message(FATAL_ERROR "EMU_MACOS_STATIC_DEPS: libSDL2.a not found under Homebrew. "
            "Run `brew install sdl2 freetype libpng` or set -DEMU_MACOS_STATIC_DEPS=OFF.")
    endif()
    set(_sdl2_a   ${_brew}/opt/sdl2/lib/libSDL2.a)
    set(_ft_a     ${_brew}/opt/freetype/lib/libfreetype.a)
    set(_png_a    ${_brew}/opt/libpng/lib/libpng16.a)
    target_link_libraries(cardputer-emu PRIVATE
        ${_sdl2_a} ${_ft_a} ${_png_a} bz2 z
        # Frameworks required by static SDL2 (from `pkg-config --static --libs sdl2`)
        "-framework CoreAudio" "-framework AudioToolbox"
        "-weak_framework CoreHaptics" "-weak_framework GameController"
        "-framework ForceFeedback" objc
        "-framework CoreVideo" "-framework Cocoa" "-framework Carbon"
        "-framework IOKit" "-weak_framework QuartzCore" "-weak_framework Metal"
        "-weak_framework UniformTypeIdentifiers"
    )
else()
    # Dynamic linkage (Linux, Windows, or macOS with -DEMU_MACOS_STATIC_DEPS=OFF)
    # SDL2main must come BEFORE SDL2: it references SDL symbols, and MinGW's ld
    # is a single-pass linker. (Previously SDL2_image at the tail happened to
    # re-pull the SDL symbols; removing it exposed the latent ordering bug.)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(cardputer-emu PRIVATE SDL2::SDL2main)
    endif()
    if(TARGET SDL2::SDL2)
        target_link_libraries(cardputer-emu PRIVATE SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        target_link_libraries(cardputer-emu PRIVATE SDL2::SDL2-static)
    else()
        target_include_directories(cardputer-emu PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(cardputer-emu PRIVATE ${SDL2_LIBRARIES})
    endif()
    # Freetype (needed by LVGL)
    if(FREETYPE_FOUND AND FREETYPE_LINK_LIBRARIES)
        target_link_libraries(cardputer-emu PRIVATE ${FREETYPE_LINK_LIBRARIES})
        target_link_directories(cardputer-emu PRIVATE ${FREETYPE_LIBRARY_DIRS})
    else()
        target_link_directories(cardputer-emu PRIVATE /opt/homebrew/lib)
        target_link_libraries(cardputer-emu PRIVATE freetype)
    endif()
endif()

# dl (for dlopen) + threads
if(NOT WIN32)
    target_link_libraries(cardputer-emu PRIVATE ${CMAKE_DL_LIBS})
    find_package(Threads REQUIRED)
    target_link_libraries(cardputer-emu PRIVATE Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Post-build: copy apps and resources into build/apps/
# ---------------------------------------------------------------------------
add_custom_command(TARGET cardputer-emu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cardputer-emu>/apps
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:UserDemo>
        $<TARGET_FILE_DIR:cardputer-emu>/apps/
    # APPLaunch resolves assets at <exe_dir>/APPLaunch/share/{images,font,audio}
    # (see hal_paths_init). Ship the launcher's whole share/ tree there.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${APPLAUNCH_SHARE_DIR}
        $<TARGET_FILE_DIR:cardputer-emu>/APPLaunch/share
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cardputer-emu>/assets
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/device_skin.png
        $<TARGET_FILE_DIR:cardputer-emu>/assets/device_skin.png
    COMMENT "Copying apps and resources"
)
if(TARGET APPLaunch)
    add_custom_command(TARGET cardputer-emu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:APPLaunch> $<TARGET_FILE_DIR:cardputer-emu>/apps/
        COMMENT "Copying APPLaunch"
    )
endif()

endif() # NOT EMSCRIPTEN — end native targets

# ---------------------------------------------------------------------------
# Web (Emscripten) target — only built when using emcmake
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    # App sources statically linked (no dlopen in wasm)
    # Reuse APPLAUNCH_UI_C / APPLAUNCH_UI_CPP / APPLAUNCH_KBD from launcher_sources.cmake
    add_executable(cardputer-emu-web
        src/main_web.cpp
        src/device_skin.cpp
        src/emu_launcher_stubs.c
        src/png_to_sdl_surface.c
        ${APPLAUNCH_UI_C}
        ${APPLAUNCH_UI_CPP}
        ${APPLAUNCH_KBD}
        ${APPLAUNCH_HAL_SDL_C}
        ${APPLAUNCH_HAL_SDL_CPP}
    )

    target_include_directories(cardputer-emu-web PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compat_queue  # sys/queue.h only (no pthread.h conflict)
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${APPLAUNCH_INCLUDES}
    )
    target_link_libraries(cardputer-emu-web PRIVATE emu::launcher_apply)
    target_compile_definitions(cardputer-emu-web PRIVATE HAL_PLATFORM_SDL=1)

    target_compile_options(cardputer-emu-web PRIVATE
        -include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat.h
        -Wno-c++11-narrowing
        -DLV_USE_FREETYPE=0
    )

    target_link_libraries(cardputer-emu-web PRIVATE lvgl)

    # Emscripten SDL2 (PNG decoding goes through lodepng — see png_to_sdl_surface.c)
    target_compile_options(cardputer-emu-web PRIVATE -sUSE_SDL=2 -pthread)
    target_link_options(cardputer-emu-web PRIVATE
        -sUSE_SDL=2
        -pthread -sPROXY_TO_PTHREAD=0
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/device_skin.png@/assets/device_skin.png"
        # APPLaunch's hal_paths default to "./APPLaunch/share/...", and the wasm
        # cwd is "/", so preload the whole share tree (images + font + audio) at
        # /APPLaunch/share to match. Fonts are needed for CJK freetype text.
        "--preload-file=${APPLAUNCH_SHARE_DIR}@/APPLaunch/share"
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html
    )

    set_target_properties(cardputer-emu-web PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
    )

    # Ship the COOP/COEP service worker next to index.html (shell.html loads it).
    add_custom_command(TARGET cardputer-emu-web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/web/coi-serviceworker.js
            $<TARGET_FILE_DIR:cardputer-emu-web>/coi-serviceworker.js
        COMMENT "Copying coi-serviceworker.js"
    )
endif()
