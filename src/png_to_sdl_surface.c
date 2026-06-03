/*
 * png_to_sdl_surface.c — see header. Uses LVGL's bundled lodepng directly
 * (the same one used by LV_USE_LODEPNG=1), so no extra dependency is added.
 */
#include "png_to_sdl_surface.h"

#include <stdio.h>
#include <stdlib.h>

#include "lvgl/src/libs/lodepng/lodepng.h"

SDL_Surface *load_png_as_sdl_surface(const char *path)
{
    unsigned char *rgba = NULL;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32_file(&rgba, &w, &h, path);
    if (err) {
        fprintf(stderr, "[png] lodepng_decode32_file(%s): %u %s\n",
                path, err, lodepng_error_text(err));
        return NULL;
    }

    /* lodepng outputs RGBA in memory order R,G,B,A — matches SDL_PIXELFORMAT_RGBA32. */
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(
        0, (int)w, (int)h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        fprintf(stderr, "[png] SDL_CreateRGBSurfaceWithFormat: %s\n", SDL_GetError());
        free(rgba);
        return NULL;
    }

    /* Copy row-by-row in case SDL added pitch padding. */
    if (surf->pitch == (int)(w * 4)) {
        SDL_memcpy(surf->pixels, rgba, (size_t)w * h * 4u);
    } else {
        for (unsigned y = 0; y < h; ++y) {
            SDL_memcpy((unsigned char *)surf->pixels + (size_t)y * surf->pitch,
                       rgba + (size_t)y * w * 4u,
                       (size_t)w * 4u);
        }
    }

    free(rgba);
    return surf;
}
