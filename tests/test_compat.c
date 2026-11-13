#include "harness.h"
#include "translate.h"
#include <stdlib.h>

static unsigned checks,failures,matches,applies;
static uint64_t expected_pc;
static int accept;
static void check(const char *what,int ok) {checks++;if(!ok){printf("FAIL %s\n",what);failures++;}}
static int match(HvCpu *cpu,uint64_t pc) {
    matches++;check("matcher sees the emitting CPU",cpu==cpu->m->cpus[cpu->index]);
    return accept && pc==expected_pc;
}
static void apply(HvCpu *cpu) {applies++;cpu->x[8]=0;}
static void el0(HvCpu *cpu) {cpu->el=0;cpu->spsel=0;}

static void run_case(int enabled,int accepted,int user,int use_apply,int zero) {
    HvMachine m;static HvCpu init,out;
    check("machine allocation",hv_machine_init(&m,4<<20)==0);
    uint32_t *code=aligned_alloc(16384,16384);
    uint32_t instructions[]={zero?0x52800008:0x52800028,0x7100011f,0x1a9f07e8,0x91000529,0xd503201f};
    memcpy(code,instructions,sizeof instructions);
    hv_machine_add_ram(&m,(uint64_t)(uintptr_t)code,16384,code,1);
    memset(&init,0,sizeof init);seed_cpu(&init,123,(uint64_t)(uintptr_t)code+8192);
    matches=applies=0;accept=accepted;expected_pc=(uint64_t)(uintptr_t)(code+2);
    if(enabled) {m.compat_match=match;m.compat_apply=use_apply?apply:NULL;}
    m.stats_interval=1;
    TestProgram program={"compat",NULL,code,code+5};
    check("JIT runs",run_jit(&m,&program,&init,&out,user?el0:NULL)==0);
    int replaced=enabled && accepted && !user;
    check("scoped result",out.x[8]==(zero || (replaced && use_apply)?0:1));
    check("matcher EL1 gating",(matches>0)==(enabled && !user));
    check("application gating",applies==(unsigned)(replaced && use_apply));
    check("helper profiling stays in bounds",(m.stats.helper[HV_OP_COMPAT]>0)==replaced);
    check("following instruction runs",out.x[9]==init.x[9]+1);
    check("condition flags preserved",out.nzcv==(zero?0x60000000:0x20000000));
    for(int i=0;i<31;i++) if(i!=8 && i!=9) check("other general registers preserved",out.x[i]==init.x[i]);
    for(int i=0;i<32;i++) check("SIMD registers preserved",out.v[i]==init.v[i]);
    check("stack preserved",out.sp==init.sp);
    hv_machine_free(&m);free(code);
}
int main(void) {
    run_case(0,1,0,1,0);run_case(1,1,0,1,0);run_case(1,0,0,1,0);
    run_case(1,1,1,1,0);run_case(1,1,0,0,0);run_case(1,1,0,1,1);
    printf("compatibility JIT: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
