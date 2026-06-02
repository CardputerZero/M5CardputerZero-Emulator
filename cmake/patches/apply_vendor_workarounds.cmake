# ─────────────────────────────────────────────────────────────────────────────
# apply_vendor_workarounds.cmake
# ─────────────────────────────────────────────────────────────────────────────
# Temporary, narrowly-scoped patches against the launcher submodule so the
# emulator can build until the upstream HAL-decouple PR lands.
#
# These patches MODIFY the submodule working tree in place (not the index).
# `git -C vendor/M5CardputerZero-Launcher status` will therefore show modified
# files — that is expected. The patches are idempotent (re-running cmake on
# an already-patched tree is a no-op) and a `git -C vendor/... checkout .`
# undoes them safely.
#
# Why working-tree edits, not an include-path overlay:
# Overlay headers are shadowed by `#include "..."` quote-form lookups that
# resolve relative to the including file's directory before consulting -I
# paths. Most launcher headers are pulled in via such relative includes, so
# an overlay can't reliably win. Modifying the vendor file in place is the
# only mechanism that reaches every consumer.
#
# Each entry below MUST link to the upstream issue/PR that will retire it.
# When the upstream PR merges and the emulator bumps the submodule SHA, the
# corresponding entry below MUST be deleted in the same commit. The function
# prints "already-fixed upstream (skip)" once that's true, which is the cue.
#
# Inputs:
#   VENDOR_DIR — path to vendor/M5CardputerZero-Launcher
# ─────────────────────────────────────────────────────────────────────────────

if(NOT DEFINED VENDOR_DIR)
    message(FATAL_ERROR "apply_vendor_workarounds.cmake: VENDOR_DIR must be set")
endif()

# Helper: regex-replace inside a vendor file, writing back in place. Skipped
# silently if the input regex no longer matches (post-upstream-fix safe).
function(_emu_patch_vendor_file relpath match replace why_url)
    set(_path "${VENDOR_DIR}/${relpath}")
    if(NOT EXISTS "${_path}")
        message(STATUS "[vendor-patch] skip (file gone): ${relpath}")
        return()
    endif()
    file(READ "${_path}" _content)
    string(REGEX MATCH "${match}" _hit "${_content}")
    if(NOT _hit)
        # Either upstream has fixed it, or we already patched it on a prior
        # cmake run — either way nothing to do.
        return()
    endif()
    string(REGEX REPLACE "${match}" "${replace}" _patched "${_content}")
    file(WRITE "${_path}" "${_patched}")
    message(STATUS "[vendor-patch] applied: ${relpath}  (see ${why_url})")
endfunction()

set(_any_patch_active FALSE)

# ─── Workaround #1: ui_app_setup.hpp wrapped in `#if !defined(HAL_PLATFORM_SDL)` ───
# The whole UISetupPage class is excluded under SDL builds, but ui_app_launch.cpp
# still references `page_v<UISetupPage>`. The class body is in fact already HAL-
# clean (uses hal_wifi_*, hal_battery_*); residual i2c ioctl spots are already
# inside #ifdef __linux__, so dropping the SDL guard is safe.
#
# Upstream fix: drop the SDL guard, route remaining raw syscalls through HAL.
# Tracking PR: https://github.com/CardputerZero/M5CardputerZero-Launcher/pull/48
_emu_patch_vendor_file(
    "projects/APPLaunch/main/ui/components/page_app/ui_app_setup.hpp"
    "#if !defined\\(HAL_PLATFORM_SDL\\)"
    "#if 1  // emu-workaround: SDL guard disabled — apply_vendor_workarounds.cmake"
    "https://github.com/CardputerZero/M5CardputerZero-Launcher/pull/48"
)
set(_any_patch_active TRUE)

# ─── Workaround #2: ui_app_launch.cpp registers Linux-only pages whose ───────
# ─── headers are SDL-guarded ─────────────────────────────────────────────────
# In the launcher source, ui_app_launch.cpp wraps Linux-only page registrations
# with `#ifdef __linux__`, but the page headers themselves are wrapped with
# `#if !defined(HAL_PLATFORM_SDL)`. The two guards don't align: when emu builds
# on a Linux host (real linux + SDL), `__linux__` is defined so the registration
# code activates, but `HAL_PLATFORM_SDL` is also defined so the page classes
# are excluded — and we hit `'UICameraPage' was not declared`.
#
# Tighten the guards in ui_app_launch.cpp to match the headers' SDL guard.
# Upstream fix: pick ONE guard convention (HAL_PLATFORM_SDL only) consistently.
# Tracking PR: https://github.com/CardputerZero/M5CardputerZero-Launcher/pull/48
_emu_patch_vendor_file(
    "projects/APPLaunch/main/ui/components/ui_app_launch.cpp"
    "#ifdef __linux__"
    "#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)  // emu-workaround"
    "https://github.com/CardputerZero/M5CardputerZero-Launcher/pull/48"
)

# ─── Workaround #3: hal_filesystem_sdl.cpp missing <stdio.h> for snprintf ────
# Builds fine on glibc/libc++ where <string.h>/<stdlib.h> transitively pull
# in <stdio.h>, but fails on MSYS2 MinGW with `'snprintf' was not declared`.
# Trivial header fix.
#
# Tracking PR: https://github.com/CardputerZero/M5CardputerZero-Launcher/pull/48
_emu_patch_vendor_file(
    "projects/APPLaunch/main/hal/sdl/hal_filesystem_sdl.cpp"
    "#include \"../hal_filesystem.h\"\n#include <string.h>"
    "#include \"../hal_filesystem.h\"\n#include <stdio.h>   // snprintf — emu-workaround\n#include <string.h>"
    "https://github.com/CardputerZero/M5CardputerZero-Launcher/pull/48"
)

# ─── Future entries follow the same pattern ──────────────────────────────────
# _emu_patch_vendor_file(
#     "projects/APPLaunch/main/path/to/file.cpp"
#     "regex-of-broken-line"
#     "replacement"
#     "https://github.com/.../pull/NNN"
# )

if(_any_patch_active)
    message(STATUS "[vendor-patch] one or more launcher submodule files patched in working tree.")
    message(STATUS "[vendor-patch] `git -C ${VENDOR_DIR} status` will show modifications — this is expected.")
    message(STATUS "[vendor-patch] Run `git -C ${VENDOR_DIR} checkout .` to revert.")
endif()
