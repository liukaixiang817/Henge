// Real page tables and translated instructions: noncontiguous 4 KB pages,
// permissions, page boundaries, code changes and local/remote invalidation.
// --bench N G runs the same memory/indirect-branch workload with guest granule
// 2^G, with either software TLB build. This is not an Intel app benchmark.
#include <mach/mach_time.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dispatch.h"
#include "memop.h"
#include "mmu.h"
#include "rosetta.h"
#include "sysreg.h"

#define RAM_PA 0x40000000ull
#define RAM_SIZE (4u << 20)
#define PT_PA 0x80000000ull
#define PT_SIZE (2u << 20)
#define CODE_VA 0x100000000ull
#define DATA_VA 0x200100000ull
#define STOP_VA 0x700000000ull
#define VECTOR 0xfffffe0000000000ull
#define AF (1ull << 10)
#define AP(x) ((uint64_t)(x) << 6)
#define UXN (1ull << 54)
#define DESC (3 | AF | AP(1))

#define PROGRAM(n) extern const char n[], n##_end[]
PROGRAM(r4_a); PROGRAM(r4_b); PROGRAM(r4_c); PROGRAM(r4_d);
PROGRAM(r4_pair); PROGRAM(r4_atomic); PROGRAM(r4_jump);
PROGRAM(r4_value); PROGRAM(r4_wait);
PROGRAM(r4_page_end);

static HvMachine m;
static HvCpu cpu, peer;
static uint8_t *ram, *pt;
static uint64_t next_pt, roots[2];
static unsigned granule;
static int checks, failures;

static void check(const char *what, uint64_t got, uint64_t want) {
    checks++;
    if (got == want) return;
    printf("FAIL %s: got 0x%llx, expected 0x%llx\n", what,
           (unsigned long long)got, (unsigned long long)want);
    failures++;
}

static uint64_t table(void) {
    uint64_t pa = PT_PA + next_pt;
    next_pt += 65536; // aligned for all three architectural granules
    if (next_pt > PT_SIZE) abort();
    return pa;
}

static uint64_t *map(uint64_t va, uint64_t pa, uint64_t attrs) {
    unsigned stride = granule - 3;
    unsigned levels = (48 - granule + stride - 1) / stride;
    uint64_t root = roots[(va >> 55) & 1];
    for (unsigned level = 4 - levels;; level++) {
        unsigned shift = granule + stride * (3 - level);
        unsigned bits = level == 4 - levels ? 48 - shift : stride;
        uint64_t *e = (uint64_t *)(pt + root - PT_PA) + ((va >> shift) & ((1ull << bits) - 1));
        if (level == 3) {
            __atomic_store_n(e, pa | attrs, __ATOMIC_RELEASE);
            return e;
        }
        if (!(*e & 1)) *e = table() | 3;
        root = *e & 0x0000ffffffff0000ull;
    }
}

static void configure(HvCpu *c, unsigned index) {
    hv_cpu_reset_index(c, &m, index);
    c->ttbr0_el1 = roots[0] | 7ull << 48;
    c->ttbr1_el1 = roots[1];
    unsigned tg0 = granule == 12 ? 0 : granule == 14 ? 2 : 1;
    unsigned tg1 = granule == 12 ? 2 : granule == 14 ? 1 : 3;
    c->tcr_el1 = 16 | (uint64_t)tg0 << 14 | 16u << 16 | (uint64_t)tg1 << 30 | 5ull << 32;
    c->mair_el1 = 0xff;
    c->sctlr_el1 |= 1;
    c->cpacr_el1 = 3u << 20;
    c->actlr_el1 = 2; // software TSO, as used by Rosetta
    c->el = 0;
    c->spsel = 0;
    c->vbar_el1 = VECTOR;
    c->x[15] = STOP_VA;
    hv_tlb_flush(c);
    hv_ijc_switch(c);
}

static void setup(unsigned g) {
    granule = g;
    hv_flush_blocks(&m);
    m.ncpus = 1;
    m.cpus[1] = NULL;
    memset(pt, 0, PT_SIZE);
    next_pt = 0;
    roots[0] = table(); roots[1] = table();
    configure(&cpu, 0);
    m.stop_pc = STOP_VA;
    m.stop_pc2 = VECTOR + 0x400; // EL0 synchronous abort, no handler needed
}

static uint64_t data_pa(unsigned i) {
    static const unsigned offsets[] = {0x30000, 0x10000, 0x70000, 0x50000};
    return RAM_PA + offsets[i];
}

static void data_pages(void) {
    for (unsigned i = 0; i < 4; i++) {
        uint64_t va = DATA_VA + (i << granule);
        map(va, data_pa(i), DESC | UXN);
        uint64_t value = i + 1;
        memcpy(ram + data_pa(i) - RAM_PA, &value, 8);
        cpu.x[i] = va;
    }
}

static void program(unsigned page, const char *begin, const char *end) {
    uint64_t pa = RAM_PA + 0x100000 + page * 0x20000;
    map(CODE_VA + ((uint64_t)page << granule), pa, 3 | AF | AP(3));
    check("install guest code", hv_phys_write(&m, pa, begin, (unsigned)(end - begin)), 0);
}

static void run(uint64_t pc) {
    cpu.pc = pc;
    check("guest exit", hv_run(&cpu), HV_EXIT_HALT);
}

static void invalidate(HvCpu *c, uint64_t va) {
    hv_tlbi_apply(c, 0, 8, 7, 1, (7ull << 48) | ((va >> 12) & 0xfffffffffffull));
}

static uint64_t translate(HvCpu *c, uint64_t va) {
    uint64_t pa = 0;
    check("translate read", hv_mmu_access(c, va, HV_ACC_READ, (va >> 55) & 1, &pa), 0);
    return pa;
}

static void boundaries(void) {
    setup(12); data_pages();
    program(0, r4_pair, r4_pair_end);
    uint64_t a = 0x1122334455667788ull, b = 0x8877665544332211ull;
    memcpy(ram + data_pa(0) - RAM_PA + 4088, &a, 8);
    memcpy(ram + data_pa(1) - RAM_PA, &b, 8);
    cpu.x[0] = DATA_VA + 4088;
    cpu.x[1] = DATA_VA + 8192 + 4088;
    // Warm all four separate translations before an access crosses the boundary.
    for (int i = 0; i < 4; i++)
        check("noncontiguous physical mapping", translate(&cpu, DATA_VA + i * 4096), data_pa(i));
    run(CODE_VA);
    check("pair first word", cpu.x[5], a);
    check("pair second word", cpu.x[6], b);
    check("SIMD first word", (uint64_t)cpu.v[0], a);
    check("SIMD second word", (uint64_t)(cpu.v[0] >> 64), b);
    uint64_t got;
    memcpy(&got, ram + data_pa(2) - RAM_PA + 4088, 8);
    check("cross-page store first", got, a);
    memcpy(&got, ram + data_pa(3) - RAM_PA, 8);
    check("cross-page store second", got, b);

    map(DATA_VA + 12288, data_pa(3), 3 | AF | AP(3) | UXN);
    invalidate(&cpu, DATA_VA + 12288);
    uint64_t sentinel = 0xdeadbeef;
    memcpy(ram + data_pa(3) - RAM_PA, &sentinel, 8);
    run(CODE_VA);
    check("cross-page write permission fault", cpu.esr_el1 & 63, HV_FSC_PERMISSION(3));
    check("fault address is protected neighbour", cpu.far_el1, DATA_VA + 12288);
    memcpy(&got, ram + data_pa(3) - RAM_PA, 8);
    check("protected neighbour unchanged", got, sentinel);

    configure(&cpu, 0);
    map(DATA_VA + 4096, 0, 0);
    invalidate(&cpu, DATA_VA + 4096);
    cpu.x[0] = DATA_VA + 4088;
    cpu.x[1] = DATA_VA + 8192;
    run(CODE_VA);
    check("unmapped neighbour faults", cpu.esr_el1 & 63, HV_FSC_TRANSLATION(3));
    check("unmapped neighbour FAR", cpu.far_el1, DATA_VA + 4096);
}

static void atomics(void) {
    setup(12); data_pages();
    program(0, r4_atomic, r4_atomic_end);
    for (unsigned i = 0; i < 4; i++) {
        cpu.x[0] = DATA_VA + i * 4096;
        cpu.x[1] = DATA_VA + ((i + 1) % 4) * 4096;
        cpu.x[2] = DATA_VA + ((i + 2) % 4) * 4096 + 32;
        uint64_t before = 0, got = 0;
        memcpy(&before, ram + data_pa(i) - RAM_PA, 8);
        run(CODE_VA);
        memcpy(&got, ram + data_pa(i) - RAM_PA, 8);
        check("exclusive increment on each 4K page", got, before + 1);
        memcpy(&got, ram + data_pa((i + 2) % 4) - RAM_PA + 32, 8);
        check("ordered store on another page", got, before + 1);
    }
    cpu.x[0] = DATA_VA + 4093;
    run(CODE_VA);
    check("unaligned atomic faults", cpu.esr_el1 & 63, HV_FSC_ALIGNMENT);
}

static void page_end(void) {
    setup(12); data_pages();
    program(0, r4_page_end, r4_page_end_end);
    uint64_t value = 0x1122334455667788ull, got = 0;
    memcpy(ram + data_pa(0) - RAM_PA + 4088, &value, 8);
    cpu.x[0] = DATA_VA + 4088;
    cpu.x[1] = DATA_VA + 4096 + 4088;
    run(CODE_VA); // warm the code and both separate data mappings
    uint64_t slow = m.stat_slow_mem;
    run(CODE_VA);
    memcpy(&got, ram + data_pa(1) - RAM_PA + 4088, 8);
    check("last aligned word copied", got, value);
    if (HV_TLB_PAGE_BITS == 12 && hv_knob[HV_K_RECHECK])
        check("page-end recheck avoids slow load and store", m.stat_slow_mem - slow, 0);
    map(DATA_VA + 8192, 0, 0);
    invalidate(&cpu, DATA_VA + 8192);
    cpu.x[0] = DATA_VA + 4096 + 4092;
    run(CODE_VA);
    check("page-end recheck still faults across an unmapped page", cpu.far_el1, DATA_VA + 8192);
    check("page-end crossing fault type", cpu.esr_el1 & 63, HV_FSC_TRANSLATION(3));
}

static void remote_range_pages(void) {
    setup(HV_TLB_PAGE_BITS);
    const uint64_t page = 1ull << HV_TLB_PAGE_BITS;
    const uint64_t start = DATA_VA + 3 * page, walked = start + page;
    map(walked, data_pa(0), DESC | UXN);
    configure(&peer, 1);
    translate(&peer, walked); // let the real walker log the page, including its 4 KB offset
    HvPerCpu *p = peer.pc_state;
    uint32_t seq = p->walk_seq;
    __atomic_store_n(&p->walk_seq, seq - 1, __ATOMIC_SEQ_CST); // replay that walk while it is in progress
    check("remote range waits for its last page's walk", hv_tlbi_drop_remote_range(&peer, start, 2, 7), 0);
    check("remote range allows a walk outside it", hv_tlbi_drop_remote_range(&peer, start + 2 * page, 2, 7), 1);
    __atomic_store_n(&p->walk_seq, seq, __ATOMIC_SEQ_CST);
    HvTlbEntry *entry = hv_tlb_ent(&peer, (walked >> HV_TLB_PAGE_BITS) & (HV_TLB_SIZE - 1), 0);
    entry->exec_tag = hv_exec_key(walked, entry->addend);
    peer.exit_req = 0;
    check("remote range drops last page's executable mapping", hv_tlbi_drop_remote_range(&peer, start, 2, 7), 1);
    check("remote range makes running code leave its block", peer.exit_req, 1);
    m.ncpus = 1; m.cpus[1] = NULL;
}

static void code_changes(void) {
    setup(12); data_pages();
    program(0, r4_jump, r4_jump_end);
    program(1, r4_value, r4_value_end);
    cpu.x[9] = CODE_VA + 4096;
    run(CODE_VA); run(CODE_VA); // target is translated and its indirect entry warm
    check("initial indirect target", cpu.x[5], 77);
    uint64_t target_pa = RAM_PA + 0x120000;
    uint32_t changed = 0xd2800b05u; // mov x5, #88
    check("DMA code write", hv_phys_write(&m, target_pa, &changed, 4), 0);
    run(CODE_VA);
    check("changed code replaces cached target", cpu.x[5], 88);

    // A writable alias must invalidate translated code on the same physical page.
    map(DATA_VA, target_pa, DESC | UXN);
    invalidate(&cpu, DATA_VA);
    changed = 0xd2800c65u; // mov x5, #99
    check("write through code alias", hv_vmem_xfer(&cpu, DATA_VA, &changed, 4, 1, 0, CODE_VA), 0);
    run(CODE_VA);
    check("alias write invalidates code", cpu.x[5], 99);

    map(CODE_VA + 4096, target_pa, 3 | AF | AP(3) | UXN);
    invalidate(&cpu, CODE_VA + 4096);
    cpu.x[5] = 0;
    run(CODE_VA);
    check("cached target cannot bypass XN", cpu.esr_el1 & 63, HV_FSC_PERMISSION(3));
    check("XN target did not execute", cpu.x[5], 0);
}

static void invalidations(void) {
    for (unsigned g = 12; g <= (HENGE_ROSETTA_4K_CACHE ? 16u : 14u); g += 2) {
        setup(g);
        for (int high = 0; high < 2; high++) {
            uint64_t va = high ? 0xfffffe0010000000ull : DATA_VA;
            map(va, RAM_PA, DESC | UXN);
            for (unsigned off = 0; off < (1u << g); off += 4096)
                check("warm fragment", translate(&cpu, va + off), RAM_PA + off);
            map(va, RAM_PA + 0x80000, DESC | UXN);
            // Address the last 4 KB fragment, not necessarily the granule's base.
            invalidate(&cpu, va + (1u << g) - 4096);
            for (unsigned off = 0; off < (1u << g); off += 4096)
                check("whole guest granule invalidated", translate(&cpu, va + off), RAM_PA + 0x80000 + off);
        }
    }
    setup(12);
    for (int i = 0; i < 6; i++) {
        map(DATA_VA + i * 4096, RAM_PA + i * 4096, DESC | UXN);
        translate(&cpu, DATA_VA + i * 4096);
        if (i > 0 && i < 5) map(DATA_VA + i * 4096, RAM_PA + 0x80000 + i * 4096, DESC | UXN);
    }
    uint64_t xt = 7ull << 48 | 1ull << 46 | 1ull << 39 | ((DATA_VA + 4096) >> 12);
    hv_tlbi_apply(&cpu, 0, 8, 6, 1, xt); // four 4 KB pages
    for (int i = 0; i < 6; i++)
        check("4K range invalidation", translate(&cpu, DATA_VA + i * 4096),
              RAM_PA + i * 4096 + (i > 0 && i < 5 ? 0x80000 : 0));
}

static void write_register(uint32_t instruction, uint64_t value) {
    cpu.el = 1;
    cpu.x[0] = value;
    check("system register write", hv_sysreg_msr(&cpu, instruction, CODE_VA), 0);
    cpu.el = 0;
}

static void address_spaces(void) {
    setup(12);
    map(DATA_VA, RAM_PA, DESC | UXN);
    uint64_t first = cpu.ttbr0_el1;
    roots[0] = table();
    map(DATA_VA, RAM_PA + 0x80000, DESC | UXN);
    uint64_t second = roots[0] | 9ull << 48;
    check("first address space", translate(&cpu, DATA_VA), RAM_PA);
    write_register(0xd5182000u, second); // MSR TTBR0_EL1, X0
    check("second address space", translate(&cpu, DATA_VA), RAM_PA + 0x80000);
    write_register(0xd5182000u, first);
    check("restored address space", translate(&cpu, DATA_VA), RAM_PA);
    write_register(0xd5182000u, second);
    roots[0] = first & 0x0000ffffffffffffull;
    map(DATA_VA, RAM_PA + 0x90000, DESC | UXN);
    invalidate(&cpu, DATA_VA); // invalidate ASID 7 while ASID 9 is running
    write_register(0xd5182000u, first);
    check("inactive address space cannot restore stale entry", translate(&cpu, DATA_VA), RAM_PA + 0x90000);
    write_register(0xd5182000u, second);
    check("other address space remains correct", translate(&cpu, DATA_VA), RAM_PA + 0x80000);

    // A TCR change must discard a saved address space as well as current entries.
    roots[0] = first & 0x0000ffffffffffffull;
    map(DATA_VA, RAM_PA + 0xa0000, DESC | UXN);
    write_register(0xd5182040u, cpu.tcr_el1 ^ (1ull << 37)); // MSR TCR_EL1, X0 (TBI0)
    write_register(0xd5182000u, first);
    check("TCR change drops saved translations", translate(&cpu, DATA_VA), RAM_PA + 0xa0000);
}

static void *run_peer(void *arg) { return (void *)(intptr_t)hv_run(arg); }

static void remote_invalidation(void) {
    setup(14); data_pages();
    program(0, r4_wait, r4_wait_end);
    configure(&peer, 1);
    peer.pc = CODE_VA;
    peer.x[0] = DATA_VA;
    translate(&peer, DATA_VA); // a live 16 KB mapping before the replacement
    // Initiator uses 4 KB user pages; receiver uses 16 KB. Its own granule wins.
    cpu.el = 1;
    cpu.tcr_el1 &= ~(3ull << 14);
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    if (pthread_create(&thread, &attr, run_peer, &peer)) abort();
    pthread_attr_destroy(&attr);
    while (!__atomic_load_n(&peer.pc_state->tlb_live, __ATOMIC_ACQUIRE)) sched_yield();
    map(DATA_VA, data_pa(1), DESC | UXN); // new page starts with 2
    cpu.x[1] = 7ull << 48 | ((DATA_VA + 12288) >> 12);
    check("broadcast VAE1IS", hv_sysreg_sys(&cpu, 0xd5088321u, 0), 0);
    void *result;
    pthread_join(thread, &result);
    check("remote CPU exit", (intptr_t)result, HV_EXIT_HALT);
    check("remote CPU saw new mapping", peer.x[5], 2);
    m.ncpus = 1; m.cpus[1] = NULL;
}

static void benchmark(uint64_t iterations, unsigned g) {
    setup(g); data_pages();
    program(0, r4_a, r4_a_end); program(1, r4_b, r4_b_end);
    program(2, r4_c, r4_c_end); program(3, r4_d, r4_d_end);
    for (unsigned i = 0; i < 4; i++) cpu.x[9 + i] = CODE_VA + ((uint64_t)i << g);
    cpu.x[4] = 32; cpu.x[5] = 0;
    run(CODE_VA);
    cpu.x[4] = iterations; cpu.x[5] = 0;
    uint64_t slow = m.stat_slow_mem, walks = m.stats.fetch_walks;
    uint64_t start = mach_absolute_time();
    run(CODE_VA);
    uint64_t elapsed = mach_absolute_time() - start;
    check("workload checksum", cpu.x[5], iterations * 10);
    slow = m.stat_slow_mem - slow; walks = m.stats.fetch_walks - walks;
    if (g >= HV_TLB_PAGE_BITS) {
        check("warm workload has no slow memory accesses", slow, 0);
        check("warm workload has no instruction walks", walks, 0);
    } else check("baseline uses slow memory path", slow, iterations * 8);
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    printf("BENCH software_page=%u guest_page=%u iterations=%llu ms=%.3f slow=%llu fetch_walks=%llu checksum=%llu\n",
           1u << HV_TLB_PAGE_BITS, 1u << g, (unsigned long long)iterations,
           elapsed * (double)timebase.numer / timebase.denom / 1e6,
           (unsigned long long)slow, (unsigned long long)walks, (unsigned long long)cpu.x[5]);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!hv_rosetta_host_supported()) return 77;
    setenv("HV_ROSETTA", "1", 1);
    alarm(45);
    if (hv_machine_init(&m, 16u << 20)) return 1;
    ram = aligned_alloc(65536, RAM_SIZE);
    pt = aligned_alloc(65536, PT_SIZE);
    if (!ram || !pt) return 1;
    memset(ram, 0, RAM_SIZE);
    if (hv_machine_add_ram(&m, RAM_PA, RAM_SIZE, ram, 0) ||
        hv_machine_add_ram(&m, PT_PA, PT_SIZE, pt, 0)) return 1;
    if (argc > 1 && !strcmp(argv[1], "--bench")) {
        uint64_t n = argc > 2 ? strtoull(argv[2], NULL, 10) : 100000;
        unsigned g = argc > 3 ? (unsigned)atoi(argv[3]) : 12;
        if (!n || (g != 12 && g != 14 && g != 16)) return 2;
        benchmark(n, g);
    } else {
        boundaries(); atomics(); page_end(); code_changes(); invalidations(); address_spaces(); remote_range_pages(); remote_invalidation();
        benchmark(10000, 12); benchmark(10000, 14); benchmark(10000, 16);
    }
    printf("4K cache: %d checks, %d failures (software page %u)\n", checks, failures, 1u << HV_TLB_PAGE_BITS);
    hv_machine_free(&m);
    free(pt); free(ram);
    alarm(0);
    return failures != 0;
}
