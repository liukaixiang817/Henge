/* Executes the production router against the real, separately built engines.
   Run each case in a new process: a loaded Objective-C runtime is never swapped. */
#include "henge-vm.h"
#include "henge-ios-jit.h"
#include <assert.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HengeDualMapping *henge_ios_jit_get_mapping(void) { return NULL; }
extern _Atomic int henge_tl_ran;

static int loaded(int rosetta) {
    int count = 0;
    const char *name = rosetta ? "HengeRosettaEngine.framework/" : "HengeMainlineEngine.framework/";
    for (uint32_t i = 0; i < _dyld_image_count(); i++)
        if (strstr(_dyld_get_image_name(i), name)) count++;
    return count;
}

int main(int argc, char **argv) {
    assert(argc == 5);
    int rosetta = atoi(argv[1]);
    const char *frameworks = argv[2], *revision = argv[3], *test = argv[4];
    char error[1024];
    assert(loaded(0) == 0 && loaded(1) == 0);
    henge_engine_configure(rosetta, frameworks);
    assert(henge_engine_rosetta() == rosetta);
    /* Merely selecting an engine or checking host capabilities must not load it. */
    henge_rosetta_available();
    henge_available_memory();
    assert(loaded(0) == 0 && loaded(1) == 0);
    if (strcmp(test, "normal")) {
        assert(henge_vm_boot(NULL, NULL, NULL, 0, 0, 0, 1, error, sizeof error) == -1);
        assert(strstr(error, !strcmp(test, "missing") ? "Cannot load" : "does not match"));
        assert(loaded(!rosetta) == 0); /* a failed engine never falls back */
        printf("PASS engine %s failure is explicit: %s\n", test, error);
        return 0;
    }
    /* Conflicting environment settings cannot override the UI's engine choice. */
    setenv("HV_ROSETTA", rosetta ? "0" : "1", 1);
    setenv("HV_SOFTWARE_TSO", rosetta ? "0" : "1", 1);
    assert(!strcmp(henge_engine_revision(), revision));
    assert(loaded(rosetta) == 1 && loaded(!rosetta) == 0);
    /* The translation layer may remap the app's JIT region. Neither selected
       engine may boot, restore or install after that region has been used. */
    atomic_store(&henge_tl_ran, 1);
    assert(henge_vm_boot(NULL, NULL, NULL, 0, 0, 0, 1, error, sizeof error) == -1);
    assert(strstr(error, "A Mac app ran"));
    assert(henge_vm_resume(NULL, NULL, NULL, 0, 0, 0, 1, NULL, NULL, error, sizeof error) == -1);
    assert(strstr(error, "A Mac app ran"));
    assert(henge_vm_install(NULL, NULL, NULL, 0, NULL, NULL, 0, 1, error, sizeof error) == -1);
    assert(strstr(error, "A Mac app ran"));
    atomic_store(&henge_tl_ran, 0);
    assert(atoi(getenv("HV_ROSETTA")) == rosetta);
    assert(atoi(getenv("HV_SOFTWARE_TSO")) == rosetta);
    assert(!dlsym(RTLD_DEFAULT, "hv_vm_create"));
    assert(!dlsym(RTLD_DEFAULT, "hv_rosetta_host_supported"));
    assert(henge_vm_state() == 0 && henge_vm_blocks() == 0);
    assert(henge_vm_can_start() && henge_vm_generation() == 0);
    assert(henge_vm_balloon(-1) == -1);
    assert(henge_vm_dns(0) == 0 && henge_vm_dns(1) == 0 && henge_vm_dns(2) == 0);
    assert(henge_vm_continue() == -1);
    assert(henge_vm_pause(error, sizeof error) == -1);
    assert(henge_vm_save("/nonexistent/henge.hsnap", "test", error, sizeof error) == -1);
    assert(henge_vm_saved_ok("/nonexistent/henge.hsnap", "test", NULL, NULL) == -1);
    assert(henge_vm_exit_outcome() == HENGE_VM_OUTCOME_ERROR);
    henge_vm_free_display(NULL);
    assert(henge_vm_guest_el0() == 0 && henge_vm_cpus_on() == 0);
    assert(henge_install_step() == -1);
    henge_vm_set_display(1024, 768);
    henge_vm_set_display_mm(0, 0);
    henge_vm_key(4, 1);
    henge_vm_touch(1, 0, 100, 100, 1);
    henge_vm_touch_frame(1);
    henge_vm_touch_button(1);
    henge_vm_touch_button(0);
    henge_vm_stop();
    /* Both boot and installation must require relaunch after a setting change. */
    henge_vm_set_rosetta(!rosetta);
    assert(henge_vm_boot(NULL, NULL, NULL, 0, 0, 0, 1, error, sizeof error) == -1);
    assert(strstr(error, "Quit Henge and reopen"));
    assert(henge_vm_resume(NULL, NULL, NULL, 0, 0, 0, 1, NULL, NULL, error, sizeof error) == -1);
    assert(strstr(error, "Quit Henge and reopen"));
    assert(henge_vm_install(NULL, NULL, NULL, 0, NULL, NULL, 0, 1, error, sizeof error) == -1);
    assert(strstr(error, "Quit Henge and reopen"));
    assert(loaded(rosetta) == 1 && loaded(!rosetta) == 0);
    /* Reverting the toggle keeps the current engine usable; JIT is checked next. */
    henge_vm_set_rosetta(rosetta);
    assert(henge_vm_boot(NULL, NULL, NULL, 0, 0, 0, 1, error, sizeof error) == -1);
    assert(strstr(error, "JIT"));
    assert(henge_vm_resume(NULL, NULL, NULL, 0, 0, 0, 1, NULL, NULL, error, sizeof error) == -1);
    assert(strstr(error, "JIT"));
    assert(henge_vm_install(NULL, NULL, NULL, 0, NULL, NULL, 0, 1, error, sizeof error) == -1);
    assert(strstr(error, "JIT"));
    assert(loaded(rosetta) == 1 && loaded(!rosetta) == 0);
    printf("PASS %s: one engine, private symbols, preference lock, restart guard, boot/install JIT checks\n",
           rosetta ? "Rosetta" : "mainline");
    return 0;
}
