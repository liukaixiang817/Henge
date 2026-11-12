#include "ios_jit_runtime.h"

#include <assert.h>
#include <libkern/OSCacheControl.h>
#include <stdint.h>
#include <sys/mman.h>

static bool debugged, servicing, allow_allocation;
static unsigned probes, allocations, detaches, selftests;

size_t os_proc_available_memory(void) { return (size_t)8 << 30; }

void henge_test_debugged(bool value) { debugged = value; }
void henge_test_service_traps(bool value) { servicing = value; }
void henge_test_allow_allocation(bool value) { allow_allocation = value; }
unsigned henge_test_probe_count(void) { return probes; }
unsigned henge_test_allocation_count(void) { return allocations; }
unsigned henge_test_detach_count(void) { return detaches; }
unsigned henge_test_selftest_count(void) { return selftests; }

int csops(int pid, unsigned ops, void *status, size_t size) {
    (void)pid;
    assert(ops == 0 && size == sizeof(uint32_t));
    *(uint32_t *)status = debugged ? 0x10000000u : 0;
    return 0;
}

uint64_t henge_brk_probe(void) {
    probes++;
    return servicing ? 0xE0000069ull : 0;
}

void *henge_brk_get_jit_mapping(void *address, size_t bytes) {
    assert(address == NULL);
    allocations++;
    if (!servicing || !allow_allocation) return NULL;
    void *rx = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(rx != MAP_FAILED);
    assert(mprotect(rx, bytes, PROT_READ | PROT_EXEC) == 0);
    return rx;
}

void henge_brk_jit_detach(void) { detaches++; debugged = false; servicing = false; }

/* Called by the real allocator immediately before it executes generated code.
 * Availability must not become true until that execution has succeeded. */
void henge_test_icache_invalidate(void *address, size_t bytes) {
    assert(!henge_ios_jit_is_available());
    selftests++;
    sys_icache_invalidate(address, bytes);
}
