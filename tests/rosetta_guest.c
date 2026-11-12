/* Build for x86_64 macOS and run INSIDE the experimental macOS guest.
   Running it on the host or passing the ARM CPU tests is not this test. */
#include <stdio.h>
#include <stdint.h>
#include <sys/sysctl.h>

#if !defined(__x86_64__)
#error Build this probe for x86_64, not arm64.
#endif

int main(void) {
    int translated = 0;
    size_t size = sizeof translated;
    if (sysctlbyname("sysctl.proc_translated", &translated, &size, NULL, 0)) {
        perror("sysctl.proc_translated");
        return 1;
    }
    volatile uint64_t sum = 0;
    for (uint64_t i = 0; i < 1000; i++) sum += i;
    printf("HENGE_ROSETTA_%s arch=x86_64 translated=%d sum=%llu\n",
           translated == 1 && sum == 499500 ? "OK" : "FAIL",
           translated, (unsigned long long)sum);
    return translated != 1 || sum != 499500;
}
