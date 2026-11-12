#include "harness.h"

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

DECLARE_PROGRAM(tso_caps);
DECLARE_PROGRAM(tso_writer);
DECLARE_PROGRAM(tso_reader);
extern const char tso_programs_begin[], tso_programs_finish[];

static HvMachine machines[2];
static HvCpu caps, cpus[2];
static int failures;

#define ADDR(p) ((uint64_t)(uintptr_t)(p))

static void check(const char *what, uint64_t got, uint64_t want) {
    if (got == want) return;
    printf("FAIL %s: got %llx, expected %llx\n", what,
           (unsigned long long)got, (unsigned long long)want);
    failures++;
}

static void *run_cpu(void *arg) {
    return (void *)(intptr_t)hv_run(arg);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setenv("HV_ROSETTA", "0", 1);
    alarm(30); /* A broken guest handshake must fail instead of hanging CI. */
    uint8_t *data = aligned_alloc(16384, 16384);
    uint8_t *stack = aligned_alloc(16384, 65536);
    if (!data || !stack) return 1;

    for (int mode = 0; mode < 2; mode++) {
        setenv("HV_SOFTWARE_TSO", mode ? "1" : "0", 1);
        HvMachine *m = &machines[mode];
        if (hv_machine_init(m, 8 << 20)) return 1;
        if (hv_machine_add_ram(m, ADDR(tso_programs_begin),
                              tso_programs_finish - tso_programs_begin, (void *)tso_programs_begin, 1) ||
            hv_machine_add_ram(m, ADDR(data), 16384, data, 0) ||
            hv_machine_add_ram(m, ADDR(stack), 65536, stack, 0)) return 1;

        /* Environment changes after initialization must not change a CPU's
           advertised model or the ordering of already compiled blocks. */
        setenv("HV_SOFTWARE_TSO", mode ? "0" : "1", 1);
        hv_cpu_reset(&caps, m);
        caps.sp = ADDR(stack) + 32768;
        caps.pc = ADDR(tso_caps);
        m->stop_pc = ADDR(tso_caps_end);
        check("capability program exit", hv_run(&caps), HV_EXIT_HALT);
        check("AIDR.TSO", caps.x[0], mode ? 1u << 9 : 0);
        check("ACTLR reset", caps.x[1], 0);
        check("ACTLR.EnTSO enabled", caps.x[3], 2);
        check("ACTLR.EnTSO disabled", caps.x[4], 0);
        printf("software TSO %s: capability and ACTLR checks complete\n", mode ? "on" : "off");
    }

    HvMachine *m = &machines[1];
    check("enable grouped memory accesses", hv_knob_set(m, "HV_GROUPS", 1), 1);
    memset(data, 0, 16384);
    const uint64_t iterations = 100000;
    m->stop_pc = ADDR(tso_writer_end);
    m->stop_pc2 = ADDR(tso_reader_end);
    for (int i = 0; i < 2; i++) {
        hv_cpu_reset_index(&cpus[i], m, i);
        cpus[i].sp = ADDR(stack) + 16384 + i * 32768;
        cpus[i].pc = i ? ADDR(tso_reader) : ADDR(tso_writer);
        cpus[i].x[0] = ADDR(data);
        cpus[i].x[1] = iterations;
        cpus[i].actlr_el1 = 2;
    }
    check("CPUs are not serialized", m->big_lock_on, 0);
    pthread_t threads[2];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20); /* same CPU-thread stack as vm.c */
    for (int i = 0; i < 2; i++)
        if (pthread_create(&threads[i], &attr, run_cpu, &cpus[i])) return 1;
    pthread_attr_destroy(&attr);
    for (int i = 0; i < 2; i++) {
        void *result;
        pthread_join(threads[i], &result);
        check("message-passing CPU exit", (intptr_t)result, HV_EXIT_HALT);
    }
    check("message passing completed", cpus[1].x[2], iterations + 1);
    check("flag observed before payload", cpus[1].x[5], 0);
    printf("%llu two-CPU message-passing rounds, %d failures\n",
           (unsigned long long)iterations, failures);
    alarm(0);
    for (int i = 0; i < 2; i++) hv_machine_free(&machines[i]);
    free(stack);
    free(data);
    return failures != 0;
}
