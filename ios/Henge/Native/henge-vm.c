/* Load exactly one engine for the lifetime of the app. No CPU implementation
   is linked into the executable, and no engine may fall back to the other. */
#include "henge-vm.h"
#include "henge-engine-api.h"
#include <dlfcn.h>
#include <limits.h>
#include <os/proc.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>

static pthread_mutex_t engine_lock = PTHREAD_MUTEX_INITIALIZER;
static const HengeEngineAPI *engine;
static void *engine_handle;
static _Atomic int selected_rosetta, requested_rosetta;
static int configured, attempted;
/* The app owns this guard; each engine keeps its native bridge private. */
_Atomic int henge_tl_ran;
static char engine_path[PATH_MAX], engine_error[1024];

int henge_rosetta_available(void) {
    int supported = 0;
    size_t bytes = sizeof supported;
    return sysctlbyname("hw.optional.arm.FEAT_AFP", &supported, &bytes, NULL, 0) == 0 && supported;
}

void henge_engine_configure(int rosetta, const char *frameworks) {
    pthread_mutex_lock(&engine_lock);
    if (!configured) {
        configured = 1;
        selected_rosetta = requested_rosetta = !!rosetta;
        const char *name = rosetta ? "HengeRosettaEngine" : "HengeMainlineEngine";
        int n = snprintf(engine_path, sizeof engine_path, "%s/%s.framework/%s",
                         frameworks ? frameworks : "", name, name);
        if (!frameworks || !*frameworks || n < 0 || (size_t)n >= sizeof engine_path) {
            snprintf(engine_error, sizeof engine_error, "Henge's engine location is invalid.");
            attempted = 1;
        }
    }
    pthread_mutex_unlock(&engine_lock);
}

int henge_engine_rosetta(void) { return selected_rosetta; }
void henge_vm_set_rosetta(int enabled) { requested_rosetta = !!enabled; }

static const HengeEngineAPI *load_engine(void) {
    pthread_mutex_lock(&engine_lock);
    if (!attempted) {
        attempted = 1;
        if (!configured) {
            snprintf(engine_error, sizeof engine_error, "Henge's engine has not been selected.");
        } else {
            /* Environment overrides cannot select Rosetta in the mainline engine,
               or disable required ordering in the Rosetta engine. */
            setenv("HV_ROSETTA", selected_rosetta ? "1" : "0", 1);
            setenv("HV_SOFTWARE_TSO", selected_rosetta ? "1" : "0", 1);
            engine_handle = dlopen(engine_path, RTLD_NOW | RTLD_LOCAL | RTLD_FIRST);
            if (!engine_handle) {
                const char *error = dlerror();
                snprintf(engine_error, sizeof engine_error, "Cannot load the %s engine: %s",
                         selected_rosetta ? "Rosetta" : "mainline", error ? error : "unknown error");
            } else {
                HengeEngineGetAPI get_api = (HengeEngineGetAPI)dlsym(engine_handle, "henge_engine_get_api");
                const HengeEngineAPI *api = get_api ? get_api() : NULL;
                int valid = api && api->abi == HENGE_ENGINE_ABI && api->bytes == sizeof *api &&
                    api->rosetta == selected_rosetta && api->mainline_revision && api->set_jit &&
                    api->henge_vm_boot && api->henge_vm_resume && api->henge_vm_install;
#define RESULT(type, name, params, args, fallback) if (valid && !api->name) valid = 0;
                HENGE_ENGINE_RESULTS(RESULT)
#undef RESULT
#define VOID(name, params, args) if (valid && !api->name) valid = 0;
                HENGE_ENGINE_VOIDS(VOID)
#undef VOID
                if (valid) {
                    engine = api;
                    fprintf(stderr, "henge: selected %s engine, mainline %s\n",
                            api->rosetta ? "Rosetta" : "mainline", api->mainline_revision);
                } else {
                    snprintf(engine_error, sizeof engine_error, "Henge's engine does not match this app. Reinstall the complete app.");
                }
            }
        }
        if (!engine) fprintf(stderr, "henge: %s\n", engine_error);
    }
    const HengeEngineAPI *api = engine;
    pthread_mutex_unlock(&engine_lock);
    return api;
}

const char *henge_engine_revision(void) {
    const HengeEngineAPI *api = load_engine();
    return api ? api->mainline_revision : "unavailable";
}

static const HengeEngineAPI *prepare_engine(char *err, size_t errlen) {
    if (atomic_load(&henge_tl_ran)) {
        snprintf(err, errlen, "A Mac app ran in this launch. Quit Henge (swipe it away) and open it again to start a Mac.");
        return NULL;
    }
    if (requested_rosetta != selected_rosetta) {
        snprintf(err, errlen, "The Rosetta setting has changed. Quit Henge and reopen it before starting or installing a Mac.");
        return NULL;
    }
    if (selected_rosetta && !henge_rosetta_available()) {
        snprintf(err, errlen, "Rosetta is not available on this device. Turn it off in Settings, then quit and reopen Henge.");
        return NULL;
    }
    const HengeEngineAPI *api = load_engine();
    if (!api) { snprintf(err, errlen, "%s", engine_error); return NULL; }
    setenv("HV_ROSETTA", selected_rosetta ? "1" : "0", 1);
    setenv("HV_SOFTWARE_TSO", selected_rosetta ? "1" : "0", 1);
    api->set_jit(henge_ios_jit_get_mapping());
    return api;
}

int henge_vm_boot(const char *rom, const char *disk, const char *aux, uint64_t ecid, uint64_t seed, uint64_t ram_mb,
                  int cpus, char *err, size_t errlen) {
    const HengeEngineAPI *api = prepare_engine(err, errlen);
    return api ? api->henge_vm_boot(rom, disk, aux, ecid, seed, ram_mb, cpus, err, errlen) : -1;
}

int henge_vm_install(const char *rom, const char *disk, const char *aux, uint64_t ecid, const char *ipsw,
                     const char *workdir, uint64_t ram_mb, int cpus, char *err, size_t errlen) {
    const HengeEngineAPI *api = prepare_engine(err, errlen);
    return api ? api->henge_vm_install(rom, disk, aux, ecid, ipsw, workdir, ram_mb, cpus, err, errlen) : -1;
}

int henge_vm_resume(const char *rom, const char *disk, const char *aux, uint64_t ecid, uint64_t seed, uint64_t ram_mb,
                    int cpus, const char *path, const char *ident, char *err, size_t errlen) {
    const HengeEngineAPI *api = prepare_engine(err, errlen);
    return api ? api->henge_vm_resume(rom, disk, aux, ecid, seed, ram_mb, cpus, path, ident, err, errlen) : -1;
}

#define RESULT(type, name, params, args, fallback) \
    type name params { const HengeEngineAPI *api = load_engine(); return api ? api->name args : fallback; }
HENGE_ENGINE_RESULTS(RESULT)
#undef RESULT
#define VOID(name, params, args) \
    void name params { const HengeEngineAPI *api = load_engine(); if (api) api->name args; }
HENGE_ENGINE_VOIDS(VOID)
#undef VOID

size_t henge_available_memory(void) { return os_proc_available_memory(); }

/* The most address space this app can reserve in one piece, in GB (measuring, for a fastmem window): PROT_NONE
   reservations cost no memory, and each is given back at once. */
uint64_t henge_largest_reservation_gb(void) {
    for (uint64_t gb = 4096; gb >= 4; gb /= 2) {
        void *p = mmap(NULL, gb << 30, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p != MAP_FAILED) {
            munmap(p, gb << 30);
            return gb;
        }
    }
    return 0;
}

uint64_t henge_largest_mapping_mb(void) {
    for (uint64_t mb = 16896; mb >= 1024; mb -= 512) { /* 16 GB, the most a Mac can ask for, plus what the caller keeps back */
        void *p = mmap(NULL, mb << 20, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p != MAP_FAILED) {
            munmap(p, mb << 20);
            return mb;
        }
    }
    return 0;
}
