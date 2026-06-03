/*
 * png_to_sdl_surface.h — load a PNG file into an SDL_Surface using LVGL's
 * bundled lodepng decoder. Replaces SDL_image, which dragged in 19 transitive
 * dylibs (libavif/libjxl/libwebp/etc.) and triggered macOS Gatekeeper
 * quarantine prompts on first launch for every single one.
 *
 * Returns NULL on failure. Caller owns the surface and must SDL_FreeSurface().
 * The surface is RGBA32, suitable for SDL_CreateTextureFromSurface() with
 * BLEND mode.
 */
#pragma once

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

SDL_Surface *load_png_as_sdl_surface(const char *path);

#ifdef __cplusplus
}
#endif
