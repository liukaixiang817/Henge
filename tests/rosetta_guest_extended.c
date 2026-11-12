/* x86_64-only follow-up to rosetta_guest.c, run inside the macOS guest. */
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <xmmintrin.h>

#ifndef __x86_64__
#error Build this probe for x86_64.
#endif

static unsigned check_flags(void) {
    /* Independent oracle: do not let the compiler use x86 PF to compute the
       expected x86 PF. Volatile table reads prevent parity-idiom folding. */
    static const volatile uint8_t even[16] = {1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1};
    unsigned failures = 0;
    for (unsigned a = 0; a < 256; a++) for (unsigned b = 0; b < 256; b++) {
        uint8_t result = a;
        uint64_t flags;
        /* Keep PUSHF outside the compiler's 128-byte red zone. LEA preserves flags. */
        __asm__ volatile("leaq -128(%%rsp), %%rsp; addb %2, %0; pushfq; popq %1; leaq 128(%%rsp), %%rsp"
                         : "+q"(result), "=r"(flags) : "q"((uint8_t)b) : "cc", "memory");
        unsigned sum = a + b, r = sum & 255;
        unsigned pf = even[r & 15] == even[r >> 4];
        unsigned want = (sum >> 8) | pf << 2 | ((a ^ b ^ r) & 16) |
                        (r == 0) << 6 | (r & 128) | ((~(a ^ b) & (a ^ r) & 128) << 4);
        if (result != r || (flags & 0x8d5) != want) {
            if (failures < 4) printf("flags: %u+%u: result %u flags %llx expected %x\n",
                                      a, b, result, (unsigned long long)flags, want);
            failures++;
        }
    }
    return failures;
}

static unsigned check_sse(void) {
    volatile float inputs[] = {NAN, INFINITY, -INFINITY, 0, 1.9f, -1.9f, 2147483648.0f, -2147483648.0f, 2147483520.0f};
    const int expected[] = {INT_MIN, INT_MIN, INT_MIN, 0, 1, -1, INT_MIN, INT_MIN, 2147483520};
    unsigned failures = 0;
    for (unsigned i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        int got = _mm_cvttss_si32(_mm_set_ss(inputs[i]));
        if (got != expected[i]) { printf("SSE conversion %u: %d expected %d\n", i, got, expected[i]); failures++; }
    }
    volatile float compare[] = {NAN, -1, 0, 1};
    const unsigned compare_flags[] = {0x45, 1, 0x40, 0};
    for (unsigned i = 0; i < 4; i++) {
        uint64_t flags;
        float zero = 0, value = compare[i];
        __asm__ volatile("leaq -128(%%rsp), %%rsp; ucomiss %2, %1; pushfq; popq %0; leaq 128(%%rsp), %%rsp"
                         : "=r"(flags) : "x"(value), "x"(zero) : "cc", "memory");
        if ((flags & 0x8d5) != compare_flags[i]) {
            printf("SSE compare %u: flags %llx expected %x\n", i, (unsigned long long)flags, compare_flags[i]);
            failures++;
        }
    }
    unsigned saved = _mm_getcsr();
    for (unsigned mode = 0; mode < 4; mode++) {
        _mm_setcsr((saved & ~0x8040u) | (mode & 1) << 6 | (mode >> 1) << 15);
        union { uint32_t bits; float value; } tiny = {.bits = 0x00800000}, input = {.bits = 1};
        float half = 0.5f, scale = 16777216.0f;
        __asm__ volatile("mulss %1, %0" : "+x"(tiny.value) : "x"(half));
        __asm__ volatile("mulss %1, %0" : "+x"(input.value) : "x"(scale));
        uint32_t ftz = mode & 2 ? 0 : 0x00400000, daz = mode & 1 ? 0 : 0x01000000;
        if (tiny.bits != ftz || input.bits != daz) {
            printf("SSE denormals mode %u: FTZ=%x DAZ=%x expected %x/%x\n", mode, tiny.bits, input.bits, ftz, daz);
            failures++;
        }
    }
    _mm_setcsr(saved);
    union { float f[4]; __m128 v; } lanes = {.f = {1, 2, 3, 4}};
    __m128 addend = _mm_set1_ps(2);
    __asm__ volatile("addss %1, %0" : "+x"(lanes.v) : "x"(addend));
    if (lanes.f[0] != 3 || lanes.f[1] != 2 || lanes.f[2] != 3 || lanes.f[3] != 4) {
        puts("SSE scalar operation did not preserve upper lanes");
        failures++;
    }
    return failures;
}

/* On x86 these relaxed operations compile to plain MOVs, without fences.
   Acknowledgement prevents the next payload from racing the current read. */
static _Alignas(256) _Atomic uint64_t payload;
static _Alignas(256) _Atomic uint64_t ready;
static _Alignas(256) _Atomic uint64_t ack;
static unsigned mp_failures;
enum { ROUNDS = 100000 };

static void *writer(void *unused) {
    (void)unused;
    for (unsigned i = 1; i <= ROUNDS; i++) {
        atomic_store_explicit(&payload, i, memory_order_relaxed);
        atomic_store_explicit(&ready, i, memory_order_relaxed);
        while (atomic_load_explicit(&ack, memory_order_relaxed) != i) { }
    }
    return NULL;
}

static void *reader(void *unused) {
    (void)unused;
    for (unsigned i = 1; i <= ROUNDS; i++) {
        while (atomic_load_explicit(&ready, memory_order_relaxed) != i) { }
        if (atomic_load_explicit(&payload, memory_order_relaxed) != i) mp_failures++;
        atomic_store_explicit(&ack, i, memory_order_relaxed);
    }
    return NULL;
}

int main(void) {
    alarm(120);
    unsigned flags = 0, sse = 0;
    double arithmetic_ms[5];
    /* Report cold translation separately from repeated execution. A short
       one-shot probe otherwise mostly measures startup and code generation. */
    for (unsigned i = 0; i < 5; i++) {
        struct timespec begin, end;
        clock_gettime(CLOCK_MONOTONIC, &begin);
        flags += check_flags(); sse += check_sse();
        clock_gettime(CLOCK_MONOTONIC, &end);
        arithmetic_ms[i] = (end.tv_sec - begin.tv_sec) * 1000.0 + (end.tv_nsec - begin.tv_nsec) / 1e6;
    }
    pthread_t threads[2];
    if (pthread_create(&threads[0], NULL, writer, NULL) || pthread_create(&threads[1], NULL, reader, NULL)) return 1;
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    alarm(0);
    printf("HENGE_ROSETTA_EXTENDED_%s flags=%u sse=%u mp=%u rounds=%u\n",
           flags || sse || mp_failures ? "FAIL" : "OK", flags, sse, mp_failures, ROUNDS);
    printf("FLAGS_SSE_COLD_MS=%.3f\n", arithmetic_ms[0]);
    printf("FLAGS_SSE_WARM_MS=%.3f,%.3f,%.3f,%.3f\n",
           arithmetic_ms[1], arithmetic_ms[2], arithmetic_ms[3], arithmetic_ms[4]);
    return flags || sse || mp_failures;
}
