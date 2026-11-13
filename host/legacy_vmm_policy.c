#include "legacy_vmm.h"
#include <string.h>

/* Observed in Sequoia 15.6.1 (24G90), ARM64_VMAPPLE. Check the complete
   instruction shape, and independently resolve the vmm-present key. */
int hv_legacy_vmm_match(const uint8_t *code, size_t size) {
    static const uint32_t shape[] = {
        0xd503237f,0xd100c3ff,0xa9014ff4,0xa9027bfd,0x910083fd,
        0xaa0303f3,0xb9000fff,0,0,0x910033e1,0x52800082,0,
        0xb9400fe8,0x7100011f,0x1a9f07e8,0xb9000fe8,0xf9401668,
        0x910033e1,0xaa1303e0,0x52800082,0xd28d04f1,0xd73f0911,
        0xa9427bfd,0xa9414ff4,0x9100c3ff,0xd65f0fff
    };
    if (size < sizeof shape) return 0;
    for (size_t i=0;i<sizeof shape/sizeof *shape;i++) {
        uint32_t op; memcpy(&op,code+4*i,4);
        if (shape[i] && op!=shape[i]) return 0;
        if (i==7 && (op&0x9f00001f)!=0x90000000) return 0; /* ADRP x0 */
        if (i==8 && (op&0xffc003ff)!=0x91000000) return 0; /* ADD x0,x0 */
        if (i==11 && (op&0xfc000000)!=0x94000000) return 0; /* BL */
    }
    return 1;
}

int hv_legacy_vmm_process_name_at(const uint8_t *proc,size_t size,size_t off,char name[33]) {
    if (off!=0x5ccu && off!=0x5bcu) return 0; /* measured layouts only */
    if(!proc || size<off+17+33) return 0;
    const uint8_t *full=proc+off+17,*end=memchr(full,0,33);
    if(!end || end==full) return 0;
    size_t n=(size_t)(end-full),short_n=n<16?n:16;
    if(memcmp(proc+off,full,short_n) || proc[off+short_n]) return 0;
    for(size_t i=0;i<n;i++)
        if(!((full[i]>='A' && full[i]<='Z') || (full[i]>='a' && full[i]<='z') ||
             (full[i]>='0' && full[i]<='9') || full[i]==' ' || full[i]=='.' || full[i]=='-' || full[i]=='_')) return 0;
    memcpy(name,full,n+1);return 1;
}

int hv_legacy_vmm_process_at(const uint8_t *proc,size_t size,size_t off,const char **name) {
    /* proc's adjacent command_t[17] and proc_name_t[33] fields at the measured
       kernel profile's offset. Never scan neighboring heap objects for process names.
       Unknown layouts/processes retain the truthful answer. */
    static const char *const names[]={"sysctl","henge-adi-probe","akd","adid",
        "accountsd","appleaccountd","amsaccountsd","storeaccountd","System Settings",
        "AppleIDSettings","AKFollowUpExtension","AAUIFollowUpExtension",
        "AuthenticationServicesAgent","identityservicesd","apsd",
        /* Purchase and device-activation helpers also choose an identity route. */
        "App Store","appstoreagent","storekitd","storedownloadd","storeassetd",
        "storeuid","commerce","mobileactivationd","fairplaydeviceidentityd",
        "Messages","FaceTime","imagent","IMDPersistenceAgent","imtransferagent",
        /* Cloud data and encrypted-account services. Display, Bluetooth,
           kernel and software-update callers retain the virtual-device path. */
        "cloudd","bird","CloudKeychainProxy","TrustedPeersHelper","securityd",
        "cdpd","secd","Passwords",
        "contactsd","remindd","CalendarAgent","Notes","cloudphotod","photolibraryd"};
    if (name) *name=NULL;
    char process[33];
    if (!hv_legacy_vmm_process_name_at(proc,size,off,process)) return 0;
    for (size_t i=0;i<sizeof names/sizeof *names;i++) {
        if (!strcmp(process,names[i])) {
                if (name) *name=names[i];
                return 1;
        }
    }
    return 0;
}

int hv_legacy_vmm_process_name(const uint8_t *proc,size_t size,char name[33]) {
    return hv_legacy_vmm_process_name_at(proc,size,HV_LEGACY_PROC_COMM,name);
}

int hv_legacy_vmm_process(const uint8_t *proc,size_t size,const char **name) {
    return hv_legacy_vmm_process_at(proc,size,HV_LEGACY_PROC_COMM,name);
}

