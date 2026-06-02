/*
 * emu_launcher_stubs.c — definitions of symbols that Launcher's main.cpp
 * provides on the device but the emulator can't pull in (the emulator runs
 * its own src/main.cpp, not Launcher's).
 *
 * Only linked into statically-built emulator targets (Windows, Web). The
 * Unix shared-library path resolves these at dlopen time inside libAPPLaunch
 * via dynamic_lookup / --unresolved-symbols=ignore-all, so it doesn't need
 * this TU.
 *
 * If Launcher introduces new globals normally defined by its main.cpp, add
 * them here.
 */

#include <stdint.h>

/* Custom LVGL event codes registered at runtime by Launcher's main.cpp.
 * The headers declare them as `extern volatile uint32_t`, so the loader
 * just needs zero-initialized storage. They're populated via
 * lv_event_register_id() before APPLaunch fires its first battery/wifi
 * event — code paths the emulator stub'd HAL never actually drives. */
volatile uint32_t LV_EVENT_BATTERY = 0;
volatile uint32_t LV_EVENT_WIFI_INFO = 0;

/* Launcher uses a global thread pool for off-loading startup work. With our
 * thpool stub (cmake/third_party.cmake → emu::compat_stubs), thpool_init()
 * returns NULL and thpool_add_work() runs the callback synchronously, so a
 * plain NULL is the correct emulator value. */
typedef void *threadpool;
threadpool g_launch_thread_pool = (threadpool)0;
