#ifndef HENGE_LEGACY_VMM_H
#define HENGE_LEGACY_VMM_H

#include <stddef.h>
#include <stdint.h>
#include "cpu.h"
struct HvMachine;
#define HV_LEGACY_PROC_COMM 0x5ccu /* measured ARM64_VMAPPLE 24G90 proc layout */

/* Opt-in counterpart of VMHide's per-process sysctl policy. No guest disk,
   kernel bytes, host key or signed entitlement is changed. */
int hv_legacy_vmm_match(const uint8_t *code, size_t size);
int hv_legacy_vmm_process_name(const uint8_t *proc, size_t size, char name[33]);
int hv_legacy_vmm_process(const uint8_t *proc, size_t size, const char **name);
int hv_legacy_vmm_process_name_at(const uint8_t *proc, size_t size, size_t comm, char name[33]);
int hv_legacy_vmm_process_at(const uint8_t *proc, size_t size, size_t comm, const char **name);
int hv_legacy_vmm_enable(char *error, size_t size); /* caller has paused the VM */
void hv_legacy_vmm_disable(void);                 /* caller has paused the VM */
void hv_legacy_vmm_configure(struct HvMachine *machine,const char *mlb); /* before CPUs start */

#endif
