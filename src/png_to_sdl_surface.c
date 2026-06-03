/*
 * png_to_sdl_surface.c — load a PNG file into an SDL_Surface.
 *
 * History: the emulator originally used SDL2_image's IMG_Load(). That dragged
 * in ~22 third-party dylibs (libavif/libjxl/libwebp/…), each triggering a
 * macOS Gatekeeper "cannot verify" prompt on a downloaded build. We dropped
 * SDL2_image to ship a self-contained static binary.
 *
 * We must NOT use LVGL's bundled lodepng for this: LVGL's build routes
 * lodepng's file API through lv_fs (needs an "A:" drive prefix) AND, more
 * importantly, lodepng_decode32() returns garbage pixels (err=0 but wrong
 * data) in this configuration. Instead we use stb_image — a single-header,
 * standard-malloc PNG decoder vendored with LVGL (gltf), independent of LVGL's
 * heap and image pipeline. Verified to decode the device skin correctly.
 */
#include "png_to_sdl_surface.h"

#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO  /* we feed bytes; avoids any fopen surprises */
#include "lvgl/src/libs/gltf/stb_image/stb_image.h"

SDL_Surface *load_png_as_sdl_surface(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[png] fopen(%s) failed\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0) { fclose(fp); fprintf(stderr, "[png] empty file: %s\n", path); return NULL; }
    unsigned char *filebuf = (unsigned char *)malloc((size_t)fsize);
    if (!filebuf) { fclose(fp); return NULL; }
    size_t got = fread(filebuf, 1, (size_t)fsize, fp);
    fclose(fp);
    if (got != (size_t)fsize) { free(filebuf); fprintf(stderr, "[png] short read: %s\n", path); return NULL; }

    int w = 0, h = 0, channels = 0;
    /* Force 4 channels (RGBA), memory order R,G,B,A. */
    unsigned char *rgba = stbi_load_from_memory(filebuf, (int)fsize, &w, &h, &channels, 4);
    free(filebuf);
    if (!rgba) {
        fprintf(stderr, "[png] stbi_load_from_memory(%s): %s\n", path, stbi_failure_reason());
        return NULL;
    }

    /* stb gives R,G,B,A in memory → SDL_PIXELFORMAT_RGBA32 (same byte order). */
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(
        0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        fprintf(stderr, "[png] SDL_CreateRGBSurfaceWithFormat: %s\n", SDL_GetError());
        stbi_image_free(rgba);
        return NULL;
    }

    if (surf->pitch == w * 4) {
        SDL_memcpy(surf->pixels, rgba, (size_t)w * h * 4u);
    } else {
        for (int y = 0; y < h; ++y) {
            SDL_memcpy((unsigned char *)surf->pixels + (size_t)y * surf->pitch,
                       rgba + (size_t)y * w * 4,
                       (size_t)w * 4);
        }
    }

    stbi_image_free(rgba);
    return surf;
}
