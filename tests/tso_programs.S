// Run through Henge only: the capability test accesses EL1 registers.
#include "harness.h"

    .text
    .p2align 14
    .globl _tso_programs_begin, _tso_programs_finish
_tso_programs_begin:

PROGRAM tso_caps
    mrs x0, aidr_el1
    mrs x1, actlr_el1
    mov x2, #2
    msr actlr_el1, x2
    mrs x3, actlr_el1
    mov x2, #0
    msr actlr_el1, x2
    mrs x4, actlr_el1
    END tso_caps

// Message passing, without guest barriers or acquire/release instructions.
// Writer and reader use different host threads and different guest CPUs.
// The acknowledgement keeps the next payload from racing the current read.
PROGRAM tso_writer
    mov x2, #1
1:  str x2, [x0]
    str x2, [x0, #256]
2:  ldr x3, [x0, #512]
    cmp x3, x2
    b.ne 2b
    add x2, x2, #1
    cmp x2, x1
    b.ls 1b
    END tso_writer

PROGRAM tso_reader
    mov x2, #1
    mov x5, #0
1:  ldr x3, [x0, #256]
    cmp x3, x2
    b.ne 1b
    ldr x4, [x0]
    cmp x4, x2
    cinc x5, x5, ne
    str x2, [x0, #512]
    add x2, x2, #1
    cmp x2, x1
    b.ls 1b
    END tso_reader

    .p2align 14
_tso_programs_finish:
