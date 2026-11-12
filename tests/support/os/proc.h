/* iOS-only memory-budget API; the host test uses a fixed budget. */
#include <stddef.h>
size_t os_proc_available_memory(void);
