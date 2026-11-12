/* Compile for arm64 macOS; execute inside the guest in both engine modes. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/sysctl.h>
#if !defined(__aarch64__)
#error Build the ARM guest probe for arm64.
#endif
int main(void) {
    int translated = 0;
    size_t bytes = sizeof translated;
    sysctlbyname("sysctl.proc_translated", &translated, &bytes, NULL, 0);
    volatile uint64_t sum = 0;
    for (uint64_t i = 0; i < 100000; i++) sum += i;
    volatile double x = 1.5, y = 2.25;
    unsigned char source[8192], destination[8192];
    for (unsigned i = 0; i < sizeof source; i++) source[i] = (unsigned char)(i * 17);
    memcpy(destination, source, sizeof source);
    int ok = !translated && sum == 4999950000ull && x * y == 3.375 && !memcmp(source, destination, sizeof source);
    printf("HENGE_ARM_%s arch=arm64 translated=%d sum=%llu\n", ok ? "OK" : "FAIL", translated, (unsigned long long)sum);
    return !ok;
}
