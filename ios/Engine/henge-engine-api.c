#include "henge-engine-api.h"

/* JIT ownership remains with the app. Never allocate a second region or call
   the debugger from an engine. Neither engine imports app executable symbols. */
static HengeDualMapping held_jit;
static void set_jit(const HengeDualMapping *mapping) {
    held_jit = mapping ? *mapping : (HengeDualMapping){0};
}
HengeDualMapping *henge_ios_jit_get_mapping(void) { return &held_jit; }

/* The sole exported C symbol. Everything else, including third-party globals,
   stays private to this framework and cannot interpose on the app/other engine. */
__attribute__((visibility("default")))
const HengeEngineAPI *henge_engine_get_api(void) {
    static const HengeEngineAPI api = {
        .abi = HENGE_ENGINE_ABI, .bytes = sizeof(HengeEngineAPI),
        .rosetta = HENGE_ENGINE_ROSETTA, .mainline_revision = HENGE_MAINLINE_REVISION,
        .set_jit = set_jit, .henge_vm_boot = henge_vm_boot, .henge_vm_resume = henge_vm_resume,
        .henge_vm_install = henge_vm_install,
#define RESULT(type, name, params, args, fallback) .name = name,
        HENGE_ENGINE_RESULTS(RESULT)
#undef RESULT
#define VOID(name, params, args) .name = name,
        HENGE_ENGINE_VOIDS(VOID)
#undef VOID
    };
    return &api;
}
