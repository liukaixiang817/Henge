#include "legacy_vmm.h"
#include <stdio.h>
#include <string.h>

static int checks,failures;
static void check(const char *name,int got,int want) {
    checks++;
    if(got!=want) {printf("FAIL %s: %d != %d\n",name,got,want);failures++;}
}
int main(void) {
    /* A measured ARM64_VMAPPLE handler, not a kernel-address fixture. */
    uint32_t code[]={0xd503237f,0xd100c3ff,0xa9014ff4,0xa9027bfd,
        0x910083fd,0xaa0303f3,0xb9000fff,0xf0ff66c0,0x912b3800,
        0x910033e1,0x52800082,0x94046ed8,0xb9400fe8,0x7100011f,
        0x1a9f07e8,0xb9000fe8,0xf9401668,0x910033e1,0xaa1303e0,
        0x52800082,0xd28d04f1,0xd73f0911,0xa9427bfd,0xa9414ff4,
        0x9100c3ff,0xd65f0fff};
    check("measured handler",hv_legacy_vmm_match((uint8_t *)code,sizeof code),1);
    for(size_t n=0;n<sizeof code;n++)
        check("truncated handler rejected",hv_legacy_vmm_match((uint8_t *)code,n),0);
    for(size_t n=0;n<sizeof code/4;n++) {
        uint32_t saved=code[n];code[n]^=0x80000000;
        check("changed instruction rejected",hv_legacy_vmm_match((uint8_t *)code,sizeof code),0);
        code[n]=saved;
    }
    uint8_t proc[2048]={0};const char *name=NULL;
    check("kernel caller",hv_legacy_vmm_process(NULL,0,&name),0);
    check("unknown proc",hv_legacy_vmm_process(proc,sizeof proc,&name),0);
    memcpy(proc+HV_LEGACY_PROC_COMM,"akd",4);
    check("single incidental name",hv_legacy_vmm_process(proc,sizeof proc,&name),0);
    memcpy(proc+HV_LEGACY_PROC_COMM+17,"akd",4);
    check("paired process fields",hv_legacy_vmm_process(proc,sizeof proc,&name),1);
    check("recognized name",name && !strcmp(name,"akd"),1);
    check("truncated process fields",hv_legacy_vmm_process(proc,HV_LEGACY_PROC_COMM+20,&name),0);
    memset(proc,0,sizeof proc);memcpy(proc+HV_LEGACY_PROC_COMM,"WindowServer",13);memcpy(proc+HV_LEGACY_PROC_COMM+17,"WindowServer",13);
    check("graphics keeps VMM",hv_legacy_vmm_process(proc,sizeof proc,&name),0);
    memset(proc,0,sizeof proc);memcpy(proc+HV_LEGACY_PROC_COMM,"softwareupdated",16);memcpy(proc+HV_LEGACY_PROC_COMM+17,"softwareupdated",16);
    check("updates keep VMM",hv_legacy_vmm_process(proc,sizeof proc,&name),0);
    memset(proc,0,sizeof proc);memcpy(proc+HV_LEGACY_PROC_COMM,"akdx",5);memcpy(proc+HV_LEGACY_PROC_COMM+17,"akdx",5);
    check("process prefix rejected",hv_legacy_vmm_process(proc,sizeof proc,&name),0);
    memset(proc,0,sizeof proc);memcpy(proc+100,"akd",4);memcpy(proc+117,"akd",4);
    check("neighboring object names ignored",hv_legacy_vmm_process(proc,sizeof proc,&name),0);
    memset(proc,0,sizeof proc);
    memcpy(proc+HV_LEGACY_PROC_COMM,"identityservices",17);
    memcpy(proc+HV_LEGACY_PROC_COMM+17,"identityservicesd",18);
    check("long process short-name field",hv_legacy_vmm_process(proc,sizeof proc,&name),1);
    memset(proc,0,sizeof proc);
    memcpy(proc+HV_LEGACY_PROC_COMM,"AppleIDSettings",16);
    memcpy(proc+HV_LEGACY_PROC_COMM+17,"AppleIDSettings",16);
    check("account settings extension",hv_legacy_vmm_process(proc,sizeof proc,&name),1);
    const char *services[]={"mobileactivationd","fairplaydeviceidentityd","AKFollowUpExtension","appstoreagent","Messages","FaceTime","cloudd","CloudKeychainProxy","cdpd","secd","Passwords"};
    for(unsigned i=0;i<sizeof services/sizeof *services;i++) {
        size_t n=strlen(services[i]),short_n=n<16?n:16;
        memset(proc,0,sizeof proc);
        memcpy(proc+HV_LEGACY_PROC_COMM,services[i],short_n);
        memcpy(proc+HV_LEGACY_PROC_COMM+17,services[i],n+1);
        check("activation and cloud service selects software identity",hv_legacy_vmm_process(proc,sizeof proc,&name),1);
    }
    const char *preserve[]={"kernel_task","bluetoothd","WindowServer","softwareupdated","unknown"};
    for(unsigned i=0;i<sizeof preserve/sizeof *preserve;i++) {
        size_t n=strlen(preserve[i]);
        memset(proc,0,sizeof proc);
        memcpy(proc+HV_LEGACY_PROC_COMM,preserve[i],n+1);
        memcpy(proc+HV_LEGACY_PROC_COMM+17,preserve[i],n+1);
        check("unrelated virtual-device behavior is preserved",hv_legacy_vmm_process(proc,sizeof proc,&name),0);
    }
    memset(proc,0,sizeof proc);
    memcpy(proc+0x5bc,"akd",4);memcpy(proc+0x5bc+17,"akd",4);
    check("27.2 measured layout selects account service",hv_legacy_vmm_process_at(proc,sizeof proc,0x5bc,&name),1);
    check("27.2 layout is not accepted as 15/26 layout",hv_legacy_vmm_process_at(proc,sizeof proc,0x5cc,&name),0);
    check("unknown layout is rejected",hv_legacy_vmm_process_at(proc,sizeof proc,0x5bd,&name),0);
    check("uninitialized profile is rejected",hv_legacy_vmm_process_at(proc,sizeof proc,0,&name),0);
    check("overflowing offset is rejected",hv_legacy_vmm_process_at(proc,sizeof proc,(size_t)-1,&name),0);
    check("truncated 27.2 fields rejected",hv_legacy_vmm_process_at(proc,0x5bc+49,0x5bc,&name),0);
    memset(proc+0x5bc+17,0,33);memcpy(proc+0x5bc+17,"adid",5);
    check("inconsistent 27.2 names rejected",hv_legacy_vmm_process_at(proc,sizeof proc,0x5bc,&name),0);
    printf("legacy VMM: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
