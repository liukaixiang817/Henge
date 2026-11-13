#include "legacy_vmm.h"
#include "vm.h"
#include "mmu.h"
#include "legacy_dtree.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

static int read_virtual(HvCpu *c,uint64_t va,void *out,size_t n,int el) {
    for (size_t i=0;i<n;) {
        HvWalk w; hv_mmu_walk(c,va+i,HV_ACC_READ,el,&w);
        if (w.fsc) return 0;
        size_t part=0x4000-((va+i)&0x3fff);
        if (part>n-i) part=n-i;
        const void *p=hv_phys_ptr(c->m,w.pa,part);
        if (!p) return 0;
        memcpy((uint8_t *)out+i,p,part);i+=part;
    }
    return 1;
}
static int read_kernel(HvCpu *c,uint64_t va,void *out,size_t n) {
    return va>>40==0xfffffe && read_virtual(c,va,out,n,1);
}
typedef struct {
    const char *release, *version;
    uint64_t version_delta;
    uint32_t proc_comm;
} LegacyKernel;

/* Measured from each Apple vma2 restore kernel, including proc_name and the
   adjacent full-name accessor. Matching a common handler alone is insufficient. */
static const LegacyKernel kernels[]={
    {"macOS 15.6.1 (24G90)",
     "Darwin Kernel Version 24.6.0: Mon Jul 14 11:30:18 PDT 2025; root:xnu-11417.140.69~1/RELEASE_ARM64_VMAPPLE",
     0x138e9c3,0x5cc},
    {"macOS 26.6.2 (25G83)",
     "Darwin Kernel Version 25.6.0: Fri Jul 31 19:16:43 PDT 2026; root:xnu-12377.161.14~5/RELEASE_ARM64_VMAPPLE",
     0x325715f,0x5cc},
    {"macOS 27.2 (26B123)",
     "Darwin Kernel Version 27.2.0: Wed Oct 14 23:44:09 PDT 2026; root:xnu-13432.41.4~25/RELEASE_ARM64_VMAPPLE",
     0x354895f,0x5bc}
};

static const LegacyKernel *verified_kernel(HvCpu *c,uint64_t va) {
    char observed[128];
    for(size_t i=0;i<sizeof kernels/sizeof *kernels;i++) {
        const LegacyKernel *k=&kernels[i];size_t n=strlen(k->version)+1;
        if(va>=k->version_delta && n<=sizeof observed &&
           read_kernel(c,va-k->version_delta,observed,n) && !memcmp(observed,k->version,n))
            return k;
    }
    return NULL;
}
static pthread_mutex_t scope_lock=PTHREAD_MUTEX_INITIALIZER;
static char scope_names[128][33];
static unsigned scope_count;
static void legacy_apply(HvCpu *c) {
    /* Original CSET w8,ne after CMP w8,#0. Preserve all other registers,
       flags, memory and SYSCTL_OUT behavior, including kernel callers. */
    unsigned result=(c->nzcv&(1u<<30))==0;
    uint64_t proc=0;
    uint8_t fields[HV_LEGACY_PROC_COMM+17+33];
    size_t comm=__atomic_load_n(&c->m->compat_proc_comm,__ATOMIC_ACQUIRE);
    const char *name=NULL;
    if (c->el==1 && read_kernel(c,c->x[19],&proc,sizeof proc) && proc &&
        read_kernel(c,proc,fields,sizeof fields)) {
        if(hv_legacy_vmm_process_at(fields,sizeof fields,comm,&name)) result=0;
        char caller[33];
        if(hv_legacy_vmm_process_name_at(fields,sizeof fields,comm,caller)) {
            pthread_mutex_lock(&scope_lock);
            unsigned i=0;for(;i<scope_count;i++) if(!strcmp(caller,scope_names[i])) break;
            if(i==scope_count && i<sizeof scope_names/sizeof *scope_names) {
                memcpy(scope_names[scope_count++],caller,strlen(caller)+1);
                fprintf(stderr,"hengevm: legacy identity: %s reads VMM=%u\n",caller,result);
            }
            pthread_mutex_unlock(&scope_lock);
        }
    }
    c->x[8]=result;
}

static void legacy_hook(HvCpu *c) {
    legacy_apply(c);
    c->pc+=4;
}

static int legacy_match(HvCpu *c,uint64_t pc) {
    if(c->el!=1) return 0;
    HvMachine *m=c->m;
    if(pc>>40!=0xfffffe || pc<56) return 0;
    uint8_t code[104];uint64_t va=pc-56;
    if(!read_kernel(c,va,code,sizeof code) || !hv_legacy_vmm_match(code,sizeof code)) return 0;
    const LegacyKernel *kernel=verified_kernel(c,va);
    if(!kernel) return 0;
    uint32_t adrp,add;memcpy(&adrp,code+28,4);memcpy(&add,code+32,4);
    int64_t imm=((adrp>>5&0x7ffff)<<2)|(adrp>>29&3);
    if(imm&(1<<20)) imm-=1<<21;
    uint64_t key=((va+28)&~0xfffull)+imm*4096+(add>>10&0xfff);
    char text[12];
    if(!read_kernel(c,key,text,sizeof text) || memcmp(text,"vmm-present",12)) return 0;
    __atomic_store_n(&m->compat_proc_comm,kernel->proc_comm,__ATOMIC_RELEASE);
    if (__atomic_exchange_n(&m->compat_pc,pc,__ATOMIC_ACQ_REL) != pc)
        fprintf(stderr,"hengevm: software identity compatibility active for %s\n",kernel->release);
    return 1;
}

static char boot_mlb[18];
static int boot_tree(HvVmapple *b,const uint8_t *args,uint8_t **tree,size_t *size) {
    uint64_t pa;HvLegacyDTFields fields;
    if(!hv_legacy_dt_boot_args(args,112,VMAPPLE_MEM,b->ram_size,&pa,size)) return 0;
    uint8_t *p=b->ram+(pa-VMAPPLE_MEM);
    if(!hv_legacy_dt_fields(p,*size,&fields)) return 0;
    *tree=p;return 1;
}
static void legacy_boot_identity(HvCpu *c) {
    unsigned expected=0;
    if(!__atomic_compare_exchange_n(&c->m->compat_boot_done,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return;
    HvVmapple *b=hv_vm_board();uint8_t args[112],*tree=NULL;size_t size=0;
    /* Prefer the actual boot-argument pointer carried in the early kernel's
       registers, before falling back to the same page-header search as the
       boot framebuffer. Never scan or change neighboring kernel objects. */
    for(unsigned i=0;i<31 && !tree;i++) {
        uint64_t at=c->x[i];int have=0;
        if(at>=VMAPPLE_MEM && at-VMAPPLE_MEM<=b->ram_size-sizeof args) {
            memcpy(args,b->ram+(at-VMAPPLE_MEM),sizeof args);have=1;
        }else if(at>>40==0xfffffe) have=read_kernel(c,at,args,sizeof args);
        if(have && !boot_tree(b,args,&tree,&size)) tree=NULL;
    }
    if(!tree) {
        uint64_t limit=b->ram_size<(1ull<<30)?b->ram_size:(1ull<<30);
        for(uint64_t at=0;at+sizeof args<=limit;at+=0x4000) {
            uint8_t *candidate=NULL;size_t candidate_size=0;
            if(!boot_tree(b,b->ram+at,&candidate,&candidate_size)) continue;
            if(tree && tree!=candidate) {tree=NULL;break;} /* ambiguous copies */
            tree=candidate;size=candidate_size;
        }
    }
    int applied=tree && hv_legacy_dt_apply(tree,size,boot_mlb,"Macmini8,1");
    __atomic_store_n(&c->m->compat_boot_done,applied?2:3,__ATOMIC_RELEASE);
    fprintf(stderr,"hengevm: experimental board identity: %s at early kernel helper\n",applied?"device-tree fields supplied":"no verified writable device tree");
}

void hv_legacy_vmm_configure(HvMachine *m,const char *mlb) {
    scope_count=0;
    m->compat_proc_comm=0;
    m->compat_match=legacy_match;m->compat_apply=legacy_apply;
    if(mlb && hv_legacy_mlb_valid(mlb)) {
        memcpy(boot_mlb,mlb,18);m->compat_boot=legacy_boot_identity;
    }
}

int hv_legacy_vmm_enable(char *error,size_t size) {
    HvMachine *m=hv_vm_machine();HvVmapple *b=hv_vm_board();HvCpu *c=hv_vm_cpu(0);
    const char *why="unsupported or unrecognized guest sysctl handler";
    if (m->compat_match || m->stop_hook || m->stop_pc!=~0ull || m->stop_pc2!=~0ull) {
        why="another debugger hook is active";goto fail;
    }
    HvWalk vector;hv_mmu_walk(c,c->vbar_el1,HV_ACC_EXEC,1,&vector);
    if (vector.fsc || c->vbar_el1>>40!=0xfffffe) goto fail;
    uint64_t candidate=0;unsigned matches=0;const LegacyKernel *kernel=NULL;
    static const uint32_t prefix[]={0xd503237f,0xd100c3ff,0xa9014ff4,
        0xa9027bfd,0x910083fd,0xaa0303f3,0xb9000fff};
    const uint8_t *at=b->ram,*end=b->ram+b->ram_size;
    while ((size_t)(end-at)>=104) {
        const uint8_t *p=memmem(at,(size_t)(end-at),prefix,sizeof prefix);
        if (!p) break;
        at=p+4;
        if (!hv_legacy_vmm_match(p,(size_t)(end-p))) continue;
        uint64_t pa=VMAPPLE_MEM+(uint64_t)(p-b->ram);
        uint64_t va=c->vbar_el1+(pa-vector.pa);
        HvWalk walk;hv_mmu_walk(c,va,HV_ACC_EXEC,1,&walk);
        if (walk.fsc || walk.pa!=pa) continue;
        const LegacyKernel *profile=verified_kernel(c,va);
        if (!profile) continue;
        uint32_t adrp,add;memcpy(&adrp,p+28,4);memcpy(&add,p+32,4);
        int64_t imm=((adrp>>5&0x7ffff)<<2)|(adrp>>29&3);
        if (imm&(1<<20)) imm-=1<<21;
        uint64_t key=((va+28)&~0xfffull)+imm*4096+(add>>10&0xfff);
        char text[12];
        if (!read_kernel(c,key,text,sizeof text) || memcmp(text,"vmm-present",12)) continue;
        candidate=va+56;kernel=profile;matches++;
    }
    if (matches!=1) goto fail;
    scope_count=0;
    m->compat_proc_comm=kernel->proc_comm;
    m->stop_pc=candidate;m->stop_hook=legacy_hook;m->flush_pending=1;
    fprintf(stderr,"hengevm: legacy identity: scoped VMM hook enabled\n");
    return 0;
fail:
    if (error && size) snprintf(error,size,"%s",why);
    return -1;
}

void hv_legacy_vmm_disable(void) {
    HvMachine *m=hv_vm_machine();
    if (m->stop_hook!=legacy_hook) return;
    m->stop_hook=NULL;m->stop_pc=~0ull;m->flush_pending=1;
    m->compat_proc_comm=0;
    fprintf(stderr,"hengevm: legacy identity: scoped VMM hook disabled\n");
}

