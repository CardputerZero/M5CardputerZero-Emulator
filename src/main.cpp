#include "lvgl/lvgl.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#define usleep(us) Sleep((us)/1000)
static void *emu_dlopen(const char *p) { return (void*)LoadLibraryA(p); }
static void *emu_dlsym(void *h, const char *s) { return (void*)GetProcAddress((HMODULE)h,s); }
static void  emu_dlclose(void *h) { FreeLibrary((HMODULE)h); }
static const char *emu_dlerror() { static char b[256]; FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM,0,GetLastError(),0,b,sizeof(b),0); return b; }
#else
#include <unistd.h>
#include <dlfcn.h>
#define emu_dlopen(p)   dlopen(p, RTLD_NOW | RTLD_GLOBAL)
#define emu_dlsym(h,s)  dlsym(h,s)
#define emu_dlclose(h)  dlclose(h)
#define emu_dlerror()   dlerror()
#endif

// ── Layout from M5CardputerEmu.png (1280x840 RGBA) ─────────────
static constexpr int SKIN_W = 1280;
static constexpr int SKIN_H = 840;
static constexpr int LCD_SX = 323;
static constexpr int LCD_SY = 60;
static constexpr int LCD_SW = 639;
static constexpr int LCD_SH = 339;
static constexpr int LCD_W  = 320;
static constexpr int LCD_H  = 170;
static constexpr float SCALE = 0.5f;

struct KeyRect { int x, y, w, h; SDL_Keycode key; };
static KeyRect g_keys[4][11] = {
    // Row 0: 1-0 del
    {{49,461,70,41,SDLK_1},{160,461,71,42,SDLK_2},{272,461,71,41,SDLK_3},{384,461,71,41,SDLK_4},
     {495,461,71,41,SDLK_5},{608,461,69,41,SDLK_6},{718,461,71,42,SDLK_7},{830,461,71,42,SDLK_8},
     {942,461,70,42,SDLK_9},{1054,461,71,41,SDLK_0},{1166,461,70,42,SDLK_BACKSPACE}},
    // Row 1: tab Q-P
    {{49,558,71,42,SDLK_TAB},{160,558,71,42,SDLK_q},{272,558,71,42,SDLK_w},{384,558,70,42,SDLK_e},
     {495,558,71,42,SDLK_r},{608,558,69,42,SDLK_t},{718,558,71,42,SDLK_y},{830,558,71,42,SDLK_u},
     {942,558,70,42,SDLK_i},{1054,558,71,42,SDLK_o},{1166,558,70,42,SDLK_p}},
    // Row 2: Aa A-L OK
    {{49,655,70,41,SDLK_LSHIFT},{160,655,71,41,SDLK_a},{272,655,71,41,SDLK_s},{384,655,70,41,SDLK_d},
     {495,655,71,41,SDLK_f},{608,655,69,41,SDLK_g},{718,655,71,41,SDLK_h},{830,655,71,41,SDLK_j},
     {942,655,70,41,SDLK_k},{1054,655,71,42,SDLK_l},{1166,655,70,41,SDLK_RETURN}},
    // Row 3: fn ctrl alt Z X C V B N M space
    {{49,752,70,41,SDLK_ESCAPE},{160,752,71,41,SDLK_LCTRL},{272,752,71,41,SDLK_LALT},
     {384,752,70,41,SDLK_z},{495,752,71,41,SDLK_x},{608,752,69,41,SDLK_c},
     {718,752,71,41,SDLK_v},{830,752,71,41,SDLK_b},{942,752,70,41,SDLK_n},
     {1054,752,71,41,SDLK_m},{1166,752,70,41,SDLK_SPACE}},
};

// ── Side buttons + POWER ────────────────────────────────────────
static constexpr int SIDE_POWER = 4; // index of POWER in g_side_keys
static constexpr int NUM_SIDE_KEYS = 5;
static KeyRect g_side_keys[NUM_SIDE_KEYS] = {
    {49,  365, 71, 41, SDLK_ESCAPE},  // ESC (left)
    {158, 365, 75, 45, SDLK_HOME},    // HOME (left)
    {1058,365, 75, 45, SDLK_F3},      // TALK (right)
    {1166,365, 71, 41, SDLK_TAB},     // NEXT/tab (right)
    {1080, 40, 100, 60, SDLK_POWER},  // POWER (right top, red ON switch)
};

// ── Modifier key indices ────────────────────────────────────────
// SYM = row1,col0   Aa = row2,col0   fn = row3,col0   ctrl = row3,col1   alt = row3,col2
#define MOD_SYM_R  1
#define MOD_SYM_C  0
#define MOD_AA_R   2
#define MOD_AA_C   0
#define MOD_FN_R   3
#define MOD_FN_C   0
#define MOD_CTRL_R 3
#define MOD_CTRL_C 1
#define MOD_ALT_R  3
#define MOD_ALT_C  2

static bool g_mod_sym  = false;  // Symbol layer
static bool g_mod_aa   = false;  // CapsLock / Shift
static bool g_mod_fn   = false;  // Fn layer
static bool g_mod_ctrl = false;  // Ctrl
static bool g_mod_alt  = false;  // Alt

static bool is_modifier(int r, int c)
{
    return (r == MOD_SYM_R && c == MOD_SYM_C) ||
           (r == MOD_AA_R && c == MOD_AA_C) ||
           (r == MOD_FN_R && c == MOD_FN_C) ||
           (r == MOD_CTRL_R && c == MOD_CTRL_C) ||
           (r == MOD_ALT_R && c == MOD_ALT_C);
}

static void toggle_modifier(int r, int c)
{
    if (r == MOD_SYM_R && c == MOD_SYM_C)   g_mod_sym = !g_mod_sym;
    if (r == MOD_AA_R && c == MOD_AA_C)      g_mod_aa = !g_mod_aa;
    if (r == MOD_FN_R && c == MOD_FN_C)      g_mod_fn = !g_mod_fn;
    if (r == MOD_CTRL_R && c == MOD_CTRL_C)  g_mod_ctrl = !g_mod_ctrl;
    if (r == MOD_ALT_R && c == MOD_ALT_C)    g_mod_alt = !g_mod_alt;
}

static int g_pr = -1, g_pc = -1;
static SDL_Keycode g_pressed_sym = 0;   // sym actually injected on press — reused on release
static SDL_Window   *g_win = nullptr;
static SDL_Renderer *g_ren = nullptr;
static SDL_Texture  *g_skin_tex = nullptr;
static SDL_Texture  *g_lcd_tex = nullptr;
static uint32_t     *g_lcd_buf = nullptr;

typedef void (*sdl_kbd_handler_fn)(SDL_Event *);
typedef void *(*sdl_kbd_create_fn)(void);
static sdl_kbd_handler_fn g_kbd_handler = nullptr;

static float g_dpi_scale = 1.0f;  // renderer_pixels / window_points

static int g_side_pr = -1;  // pressed side key index

// Convert mouse window coords → skin coords (1280x840)
// With SDL_RenderSetLogicalSize set, SDL already reports mouse events
// in logical coordinates, so we just pass through.
static void mouse_to_skin(int mx, int my, int *sx, int *sy)
{
    *sx = mx;
    *sy = my;
}

static bool hit_key(int mx, int my, int *r, int *c)
{
    int sx, sy;
    mouse_to_skin(mx, my, &sx, &sy);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 11; j++) {
            auto &k = g_keys[i][j];
            if (sx >= k.x && sx < k.x+k.w && sy >= k.y && sy < k.y+k.h)
                { *r = i; *c = j; return true; }
        }
    return false;
}

static int hit_side_key(int mx, int my)
{
    int sx, sy;
    mouse_to_skin(mx, my, &sx, &sy);
    for (int i = 0; i < NUM_SIDE_KEYS; i++) {
        auto &k = g_side_keys[i];
        if (sx >= k.x && sx < k.x+k.w && sy >= k.y && sy < k.y+k.h)
            return i;
    }
    return -1;
}

// ── LVGL flush: RGB565 → ARGB8888 ──────────────────────────────
static void flush_cb(lv_display_t *, const lv_area_t *area, uint8_t *px)
{
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    uint16_t *src = (uint16_t *)px;
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            uint16_t c = src[y * w + x];
            uint8_t r5 = (c >> 11) & 0x1F;
            uint8_t g6 = (c >> 5) & 0x3F;
            uint8_t b5 = c & 0x1F;
            g_lcd_buf[(area->y1+y)*LCD_W + area->x1+x] = 0xFF000000
                | ((r5<<3|r5>>2)<<16) | ((g6<<2|g6>>4)<<8) | (b5<<3|b5>>2);
        }
    }
    lv_display_flush_ready(lv_display_get_default());
}

// ── Modifier-layer remap ────────────────────────────────────────
// The skin shows Fn/SYM/Aa overlays on the letter keys (▲▼◄► on F/X/Z/C,
// PgUp/PgDn on K/L, etc). Modifier highlights alone don't change what gets
// injected — we translate the base sym into the layered sym here.
//
// Fn layer (orange labels in the skin):
//   F → UP, X → DOWN, Z → LEFT, C → RIGHT
//   K → PAGEUP, L → PAGEDOWN, N → HOME, M → END (Ins isn't a useful keycode
//   for NC2000; map to INSERT anyway — the core ignores unknown keys).
//   V → COMMA, B → PERIOD (the printed `<` / `>` glyphs)
// SYM layer (blue labels):
//   Q..P second-row punctuation shown on the skin. For now we forward the
//   real printed char where it maps to an SDL keycode the NC2000 core also
//   recognizes (semicolon/quote/backslash etc). Rarely-used entries pass
//   the base key through.
// Aa layer: nothing to remap for letters; NC2000 doesn't use case. Left as
// a no-op so the highlight is still visible.
static SDL_Keycode apply_fn_layer(SDL_Keycode k)
{
    switch (k) {
        // Top row: digits carry F1..F10 on the Fn layer. F5..F10 are the
        // NC2000 function keys (英汉/名片/计算/行程/测验/时间); F1..F4 map
        // to the NC2000 editor keys (插入/删除/查找/修改). Matches the
        // skin overlay and the F-row mapping in apps/nc2000/src/key_new.cpp.
        case SDLK_1: return SDLK_F1;
        case SDLK_2: return SDLK_F2;
        case SDLK_3: return SDLK_F3;
        case SDLK_4: return SDLK_F4;
        case SDLK_5: return SDLK_F5;
        case SDLK_6: return SDLK_F6;
        case SDLK_7: return SDLK_F7;
        case SDLK_8: return SDLK_F8;
        case SDLK_9: return SDLK_F9;
        case SDLK_0: return SDLK_F10;
        // Second row: O/P carry F11/F12 on the Fn layer (网络 / on-off).
        case SDLK_o: return SDLK_F11;
        case SDLK_p: return SDLK_F12;
        // Directional / paging overlay on the letter rows.
        case SDLK_f: return SDLK_UP;
        case SDLK_x: return SDLK_DOWN;
        case SDLK_z: return SDLK_LEFT;
        case SDLK_c: return SDLK_RIGHT;
        case SDLK_k: return SDLK_PAGEUP;
        case SDLK_l: return SDLK_PAGEDOWN;
        case SDLK_n: return SDLK_HOME;
        case SDLK_m: return SDLK_INSERT;
        case SDLK_v: return SDLK_COMMA;
        case SDLK_b: return SDLK_PERIOD;
        default:     return k;
    }
}

static SDL_Keycode apply_sym_layer(SDL_Keycode k)
{
    // Second-row SYM glyphs printed on the skin: ~ ` + - / \ { [ }
    // Map to whatever keycodes NC2000's handle_key_wayback recognizes.
    switch (k) {
        case SDLK_w: return SDLK_BACKQUOTE;    // W` → `
        case SDLK_e: return SDLK_EQUALS;       // E+ → =
        case SDLK_r: return SDLK_MINUS;        // R-
        case SDLK_t: return SDLK_SLASH;        // T/
        case SDLK_y: return SDLK_BACKSLASH;    // Y\
        case SDLK_u: return SDLK_LEFTBRACKET;  // U{ → [
        case SDLK_i: return SDLK_RIGHTBRACKET; // I[ → ]
        case SDLK_a: return SDLK_COMMA;        // A, (duplicated row: real `,` on V, here sym layer on A)
        case SDLK_d: return SDLK_BACKSLASH;    // D|
        case SDLK_h: return SDLK_COLON;        // H:
        case SDLK_j: return SDLK_SEMICOLON;    // J;
        case SDLK_k: return SDLK_UNDERSCORE;   // K_
        case SDLK_l: return SDLK_QUESTION;     // L?
        case SDLK_b: return SDLK_LESS;         // B<
        case SDLK_n: return SDLK_GREATER;      // N>
        default:     return k;
    }
}

static SDL_Keycode apply_modifier_layers(SDL_Keycode k)
{
    // Fn wins over Sym (matches device tca8418 keymap ordering). Only apply
    // the translation for keys where a layer is actually defined — modifiers
    // and top-row digits pass through unchanged.
    if (g_mod_fn)  return apply_fn_layer(k);
    if (g_mod_sym) return apply_sym_layer(k);
    return k;
}

// ── Inject SDL key event ────────────────────────────────────────
static void inject_sdl_key(SDL_Keycode key, bool down)
{
    SDL_Event ev = {};
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.windowID = SDL_GetWindowID(g_win);
    ev.key.keysym.sym = key;
    ev.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    if (g_kbd_handler) g_kbd_handler(&ev);

    if (down && key >= 0x20 && key < 0x7f && g_kbd_handler) {
        SDL_Event te = {};
        te.type = SDL_TEXTINPUT;
        te.text.windowID = SDL_GetWindowID(g_win);
        te.text.text[0] = (char)key;
        te.text.text[1] = '\0';
        g_kbd_handler(&te);
    }
}

// ── Draw highlight rect on a key ────────────────────────────────
static void draw_key_highlight(int r, int c, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca)
{
    auto &k = g_keys[r][c];
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ren, cr, cg, cb, ca);
    SDL_Rect kr = {k.x, k.y, k.w, k.h};
    SDL_RenderFillRect(g_ren, &kr);
}

static void render()
{
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);

    SDL_RenderCopy(g_ren, g_skin_tex, nullptr, nullptr);

    // In HIGHDPI mode, renderer is 1:1 with skin pixels
    SDL_UpdateTexture(g_lcd_tex, nullptr, g_lcd_buf, LCD_W * 4);
    SDL_Rect lcd_dst = {LCD_SX, LCD_SY, LCD_SW, LCD_SH};
    SDL_RenderCopy(g_ren, g_lcd_tex, nullptr, &lcd_dst);

    SDL_RenderCopy(g_ren, g_skin_tex, nullptr, nullptr);

    // Active modifier highlights (persistent color)
    if (g_mod_sym)  draw_key_highlight(MOD_SYM_R,  MOD_SYM_C,  0, 170, 85, 120);   // green
    if (g_mod_aa)   draw_key_highlight(MOD_AA_R,   MOD_AA_C,   180, 50, 220, 120);  // purple
    if (g_mod_fn)   draw_key_highlight(MOD_FN_R,   MOD_FN_C,   238, 153, 0, 120);   // orange
    if (g_mod_ctrl) draw_key_highlight(MOD_CTRL_R, MOD_CTRL_C, 50, 120, 255, 120);  // blue
    if (g_mod_alt)  draw_key_highlight(MOD_ALT_R,  MOD_ALT_C,  220, 220, 0, 120);   // yellow

    // Normal key press highlight (red flash)
    if (g_pr >= 0 && !is_modifier(g_pr, g_pc)) {
        draw_key_highlight(g_pr, g_pc, 255, 50, 50, 100);
    }

    // Side key press highlight
    if (g_side_pr >= 0) {
        auto &k = g_side_keys[g_side_pr];
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_ren, 255, 50, 50, 100);
        SDL_Rect kr = {k.x, k.y, k.w, k.h};
        SDL_RenderFillRect(g_ren, &kr);
    }

    SDL_RenderPresent(g_ren);
}

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

typedef void (*ui_init_fn)(void);

#ifdef EMU_STATIC_APP
extern "C" {
    void ui_init(void);
    void lv_sdl_keyboard_handler(SDL_Event *event);
}
#endif

// Set working directory to the exe's directory so relative paths work
static void set_exe_dir()
{
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char *last = strrchr(path, '\\');
    if (last) { *last = '\0'; SetCurrentDirectoryA(path); }
#elif defined(__APPLE__)
    // macOS: SDL handles this, but just in case
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char *last = strrchr(path, '/');
        if (last) { *last = '\0'; chdir(path); }
    }
#endif
    // Linux: usually launched from the right dir, or use /proc/self/exe
}

int main(int argc, char *argv[])
{
    set_exe_dir();

#ifdef EMU_STATIC_APP
    // Windows: app statically linked, no dlopen
    const char *app_path = "(static-linked UserDemo)";
#elif defined(__APPLE__)
    const char *app_path = "apps/libAPPLaunch.dylib";
#else
    const char *app_path = "apps/libAPPLaunch.so";
#endif
#ifndef EMU_STATIC_APP
    if (argc > 1) app_path = argv[1];
#endif

    printf("========================================\n");
    printf("  M5CardputerZero Emulator\n");
    printf("  App : %s\n", app_path);
    printf("  LCD : %dx%d  Window: %dx%d\n",
           LCD_W, LCD_H, (int)(SKIN_W*SCALE), (int)(SKIN_H*SCALE));
    printf("  Modifiers: click Aa/fn/ctrl to toggle\n");
    printf("========================================\n");

    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);

    int win_w = (int)(SKIN_W * SCALE), win_h = (int)(SKIN_H * SCALE);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    g_win = SDL_CreateWindow("M5CardputerZero Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    g_ren = SDL_CreateRenderer(g_win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // On Retina, renderer output is 2x the window size
    int render_w, render_h;
    SDL_GetRendererOutputSize(g_ren, &render_w, &render_h);
    g_dpi_scale = (float)render_w / (float)win_w;
    printf("[EMU] Window: %dx%d  Renderer: %dx%d  DPI scale: %.1f\n",
           win_w, win_h, render_w, render_h, g_dpi_scale);

    // Set logical size so all render coords use the 1280x840 skin space
    // regardless of actual renderer pixel dimensions (fixes non-Retina displays)
    SDL_RenderSetLogicalSize(g_ren, SKIN_W, SKIN_H);
    printf("[EMU] Logical render size: %dx%d\n", SKIN_W, SKIN_H);
    printf("[EMU] LCD rect: x=%d y=%d w=%d h=%d (in skin coords)\n",
           LCD_SX, LCD_SY, LCD_SW, LCD_SH);

    SDL_Surface *surf = IMG_Load("assets/device_skin.png");
    if (!surf) { fprintf(stderr, "skin: %s\n", IMG_GetError()); return 1; }
    g_skin_tex = SDL_CreateTextureFromSurface(g_ren, surf);
    SDL_SetTextureBlendMode(g_skin_tex, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(surf);

    g_lcd_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, LCD_W, LCD_H);
    g_lcd_buf = (uint32_t *)calloc(LCD_W * LCD_H, sizeof(uint32_t));

    lv_init();
    static uint8_t draw_buf[LCD_W * LCD_H * 2];
    lv_display_t *disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, draw_buf, nullptr, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

#ifdef EMU_STATIC_APP
    // Static linked: APPLaunch overrides lv_sdl_keyboard_create/handler
    lv_sdl_keyboard_create();
    g_kbd_handler = lv_sdl_keyboard_handler;
    printf("[EMU] App keyboard driver (static)\n");
    printf("[EMU] Loaded: %s\n", app_path);
    ui_init();
#else
    void *app = emu_dlopen(app_path);
    if (!app) { fprintf(stderr, "[EMU] dlopen: %s\n", emu_dlerror()); return 1; }
    printf("[EMU] Loaded: %s\n", app_path);

    auto kbd_create = (sdl_kbd_create_fn)emu_dlsym(app, "lv_sdl_keyboard_create");
    g_kbd_handler = (sdl_kbd_handler_fn)emu_dlsym(app, "lv_sdl_keyboard_handler");

    if (kbd_create) {
        kbd_create();
        printf("[EMU] App keyboard driver loaded\n");
    } else {
        lv_indev_t *kb = lv_indev_create();
        lv_indev_set_type(kb, LV_INDEV_TYPE_KEYPAD);
        printf("[EMU] Built-in keyboard driver\n");
    }

    ui_init_fn app_init = (ui_init_fn)emu_dlsym(app, "ui_init");
    if (!app_init) { fprintf(stderr, "[EMU] ui_init missing\n"); return 1; }
    app_init();
#endif
    printf("[EMU] Running.\n");

    while (true) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) goto done;

            if (ev.type == SDL_MOUSEBUTTONDOWN) {
                int r, c;
                int sx_dbg, sy_dbg;
                mouse_to_skin(ev.button.x, ev.button.y, &sx_dbg, &sy_dbg);
                printf("[EMU] CLICK mouse=(%d,%d) skin=(%d,%d)\n",
                       ev.button.x, ev.button.y, sx_dbg, sy_dbg);
                int side = hit_side_key(ev.button.x, ev.button.y);
                if (side == SIDE_POWER) {
                    g_side_pr = side;
                    const SDL_MessageBoxButtonData btns[] = {
                        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
                        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Reset"},
                    };
                    SDL_MessageBoxData mbd = {};
                    mbd.flags = SDL_MESSAGEBOX_WARNING;
                    mbd.window = g_win;
                    mbd.title = "Power";
                    mbd.message = "Reset the emulator?";
                    mbd.numbuttons = 2;
                    mbd.buttons = btns;
                    int btn = 0;
                    SDL_ShowMessageBox(&mbd, &btn);
                    if (btn == 1) {
                        printf("[EMU] POWER — resetting\n");
                        memset(g_lcd_buf, 0, LCD_W * LCD_H * sizeof(uint32_t));
#ifdef EMU_STATIC_APP
                        ui_init();
#else
                        app_init();
#endif
                    }
                    g_side_pr = -1;
                } else if (side >= 0) {
                    printf("[EMU] HIT side_key[%d] key=0x%x\n", side, g_side_keys[side].key);
                    g_side_pr = side;
                    inject_sdl_key(g_side_keys[side].key, true);
                } else if (hit_key(ev.button.x, ev.button.y, &r, &c)) {
                    if (is_modifier(r, c)) {
                        printf("[EMU] HIT modifier r=%d c=%d\n", r, c);
                        toggle_modifier(r, c);
                    } else {
                        g_pr = r; g_pc = c;
                        g_pressed_sym = apply_modifier_layers(g_keys[r][c].key);
                        printf("[EMU] HIT key r=%d c=%d sym=0x%x\n", r, c, g_pressed_sym);
                        inject_sdl_key(g_pressed_sym, true);
                    }
                }
            }
            else if (ev.type == SDL_MOUSEBUTTONUP) {
                if (g_side_pr >= 0) {
                    inject_sdl_key(g_side_keys[g_side_pr].key, false);
                    g_side_pr = -1;
                } else if (g_pr >= 0) {
                    inject_sdl_key(g_pressed_sym, false);
                    g_pr = -1;
                    g_pressed_sym = 0;
                }
            }
            else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP ||
                     ev.type == SDL_TEXTINPUT) {
                // Physical keyboard path. Mac doesn't have an Fn/Sym modifier
                // we can detect, so we remap only when the user has toggled
                // the on-screen modifier. This lets you e.g. click "fn", then
                // press `f` on your laptop to get ▲.
                if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) &&
                    (g_mod_fn || g_mod_sym)) {
                    ev.key.keysym.sym = apply_modifier_layers(ev.key.keysym.sym);
                    ev.key.keysym.scancode =
                        SDL_GetScancodeFromKey(ev.key.keysym.sym);
                }
                if (g_kbd_handler) g_kbd_handler(&ev);
            }
        }

        lv_tick_inc(5);
        lv_timer_handler();
        render();
        SDL_Delay(5);
    }

done:
    free(g_lcd_buf);
#ifndef EMU_STATIC_APP
    emu_dlclose(app);
#endif
    SDL_DestroyTexture(g_lcd_tex);
    SDL_DestroyTexture(g_skin_tex);
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
