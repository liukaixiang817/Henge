#ifndef HENGE_LEGACY_DTREE_H
#define HENGE_LEGACY_DTREE_H
#include <stddef.h>
#include <stdint.h>
typedef struct HvLegacyDTFields {
    size_t mlb,model,model_size;
} HvLegacyDTFields;
/* Parse only the flattened tree's root. Reject other platforms and malformed
   tables before returning any writable field. Child device nodes stay intact. */
int hv_legacy_dt_fields(const uint8_t *tree,size_t size,HvLegacyDTFields *fields);
int hv_legacy_dt_apply(uint8_t *tree,size_t size,const char *mlb,const char *model);
int hv_legacy_mlb_valid(const char *mlb);
int hv_legacy_dt_boot_args(const uint8_t *args,size_t size,uint64_t ram_base,uint64_t ram_size,uint64_t *tree_pa,size_t *tree_size);
#endif
