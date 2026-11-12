/* The app/engine boundary contains no HvCpu or HvMachine layout. Increment the
   ABI when changing this table; each framework has its own private CPU types. */
#ifndef HENGE_ENGINE_API_H
#define HENGE_ENGINE_API_H
#include "henge-vm.h"
#include "henge-ios-jit.h"

#define HENGE_ENGINE_ABI 5

#define HENGE_ENGINE_RESULTS(X) \
    X(int, henge_install_step, (void), (), -1) \
    X(double, henge_install_progress, (void), (), 0) \
    X(int, henge_make_aux, (const char *path), (path), -1) \
    X(int, henge_vm_suspend, (const char *path, const char *ident, char *err, size_t errlen), (path, ident, err, errlen), -1) \
    X(int, henge_vm_pause, (char *err, size_t errlen), (err, errlen), -1) \
    X(int, henge_vm_save, (const char *path, const char *ident, char *err, size_t errlen), (path, ident, err, errlen), -1) \
    X(int, henge_vm_continue, (void), (), -1) \
    X(int, henge_vm_saved_ok, (const char *path, const char *ident, uint64_t *bytes, int64_t *saved_at), (path, ident, bytes, saved_at), -1) \
    X(int, henge_vm_state, (void), (), 0) \
    X(uint64_t, henge_vm_generation, (void), (), 0) \
    X(int, henge_vm_can_start, (void), (), 0) \
    X(int, henge_vm_exit_outcome, (void), (), HENGE_VM_OUTCOME_ERROR) \
    X(const char *, henge_vm_exit_reason, (void), (), "Engine unavailable") \
    X(int, henge_vm_knob, (const char *name, int value), (name, value), 0) \
    X(long, henge_vm_balloon, (long target_mb), (target_mb), -1) \
    X(uint64_t, henge_vm_blocks, (void), (), 0) \
    X(uint64_t, henge_vm_net_frames, (int sent), (sent), 0) \
    X(uint64_t, henge_vm_dns, (int which), (which), 0) \
    X(int, henge_vm_cpus_on, (void), (), 0) \
    X(int, henge_vm_guest_el0, (void), (), 0) \
    X(char *, henge_share_take_clipboard, (void), (), NULL) \
    X(long, henge_share_agent_seen, (void), (), 0) \
    X(int, henge_vm_pointer, (int *x, int *y), (x, y), -1) \
    X(int, henge_vm_snapshot, (uint8_t *bgra, int width, int height), (bgra, width, height), -1) \
    X(uint64_t, henge_vm_gpu_frames, (void), (), 0) \
    X(int, henge_vm_boot_display, (const uint8_t **pixels, int *width, int *height, int *stride), (pixels, width, height, stride), -1)

#define HENGE_ENGINE_VOIDS(X) \
    X(henge_vm_stop, (void), ()) \
    X(henge_vm_free_display, (const uint8_t *pixels), (pixels)) \
    X(henge_vm_share, (const char *folder, const char *socket_path, const char *agent), (folder, socket_path, agent)) \
    X(henge_share_set_clipboard, (const char *text), (text)) \
    X(henge_vm_set_display, (uint32_t width, uint32_t height), (width, height)) \
    X(henge_vm_set_display_mm, (uint32_t width_mm, uint32_t height_mm), (width_mm, height_mm)) \
    X(henge_vm_key, (uint16_t code, int down), (code, down)) \
    X(henge_vm_key_hid, (uint8_t usage, int down), (usage, down)) \
    X(henge_vm_mouse_move, (int dx, int dy), (dx, dy)) \
    X(henge_vm_mouse_button, (int button, int down), (button, down)) \
    X(henge_vm_mouse_scroll, (int dx, int dy), (dx, dy)) \
    X(henge_vm_touch, (int finger, int phase, int x, int y, uint32_t ts_ms), (finger, phase, x, y, ts_ms)) \
    X(henge_vm_touch_frame, (uint32_t ts_ms), (ts_ms)) \
    X(henge_vm_touch_button, (int down), (down)) \
    X(henge_vm_set_display_layer, (void *layer), (layer))

typedef struct HengeEngineAPI {
    uint32_t abi, bytes;
    int rosetta;
    const char *mainline_revision;
    void (*set_jit)(const HengeDualMapping *mapping);
    __typeof__(&henge_vm_boot) henge_vm_boot;
    __typeof__(&henge_vm_resume) henge_vm_resume;
    __typeof__(&henge_vm_install) henge_vm_install;
#define RESULT(type, name, params, args, fallback) type (*name) params;
    HENGE_ENGINE_RESULTS(RESULT)
#undef RESULT
#define VOID(name, params, args) void (*name) params;
    HENGE_ENGINE_VOIDS(VOID)
#undef VOID
} HengeEngineAPI;

typedef const HengeEngineAPI *(*HengeEngineGetAPI)(void);
#endif
