/* Host-side debugger stand-in. The allocator and coordinator under test are real. */
#include "../../ios/Henge/Native/henge-ios-jit.h"

void henge_test_debugged(bool value);
void henge_test_service_traps(bool value);
void henge_test_allow_allocation(bool value);
unsigned henge_test_probe_count(void);
unsigned henge_test_allocation_count(void);
unsigned henge_test_detach_count(void);
unsigned henge_test_selftest_count(void);
