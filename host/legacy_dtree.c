#include "legacy_dtree.h"
#include <string.h>

static uint32_t u32(const uint8_t *p) {uint32_t v;memcpy(&v,p,4);return v;}
int hv_legacy_mlb_valid(const char *mlb) {
    const char *alphabet="0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    if(!mlb || strlen(mlb)!=17) return 0;
    unsigned sum=0;
    for(unsigned i=0;i<17;i++) {
        const char *p=strchr(alphabet,mlb[i]);if(!p) return 0;
        sum+=(unsigned)(p-alphabet)*(i&1?3:1);
    }
    return sum%34==0;
}
int hv_legacy_dt_boot_args(const uint8_t *args,size_t size,uint64_t ram_base,uint64_t ram_size,uint64_t *tree_pa,size_t *tree_size) {
    if(!args || size<108 || !tree_pa || !tree_size) return 0;
    uint16_t h[2];uint64_t q[13];memcpy(h,args,sizeof h);memcpy(q,args,sizeof q);
    if(h[0]<2 || h[0]>3 || h[1]<1 || h[1]>3 || q[1]>>40!=0xfffffe ||
       q[2]<ram_base || q[2]-ram_base>=ram_size || !q[3] || q[3]>ram_size) return 0;
    uint64_t pa=q[12];size_t n=u32(args+104);
    if(n<8 || n>0x100000 || n>ram_size) return 0;
    if(pa>>40==0xfffffe) {
        if(pa<q[1] || pa-q[1]>ram_size || q[2]>UINT64_MAX-(pa-q[1])) return 0;
        pa=q[2]+(pa-q[1]);
    }
    if(pa<ram_base || pa-ram_base>ram_size-n) return 0;
    *tree_pa=pa;*tree_size=n;return 1;
}
int hv_legacy_dt_fields(const uint8_t *tree,size_t size,HvLegacyDTFields *fields) {
    if(!tree || !fields || size<8) return 0;
    uint32_t count=u32(tree),children=u32(tree+4);
    if(!count || count>512 || children>512) return 0;
    size_t at=8;HvLegacyDTFields f={0};int name=0,platform=0,have_name=0,have_compatible=0;
    for(uint32_t i=0;i<count;i++) {
        if(at>size || size-at<36 || !memchr(tree+at,0,32)) return 0;
        const char *key=(const char *)tree+at;size_t n=u32(tree+at+32);
        at+=36;
        if(n>size-at || n>0x100000) return 0;
        if(!strcmp(key,"name")) {
            if(have_name++) return 0;
            name=n==12 && !memcmp(tree+at,"device-tree",12);
        }
        if(!strcmp(key,"compatible")) {
            if(have_compatible++) return 0;
            static const char wanted[]="AppleVirtualPlatformARM";
            for(size_t j=0;j+sizeof wanted<=n;j++)
                if((!j || !tree[at+j-1]) && !memcmp(tree+at+j,wanted,sizeof wanted)) platform=1;
        }
        if(!strcmp(key,"mlb-serial-number")) {
            if(f.mlb || n!=32) return 0;
            f.mlb=at;
        }
        if(!strcmp(key,"model")) {
            if(f.model || n<11 || n>32 || !memchr(tree+at,0,n)) return 0;
            f.model=at;f.model_size=n;
        }
        size_t padded=(n+3)&~(size_t)3;
        if(padded>size-at) return 0;
        at+=padded;
    }
    if(!name || !platform || !f.mlb || !f.model) return 0;
    const char *model=(const char *)tree+f.model;
    if(strcmp(model,"VirtualMac2,1") && strcmp(model,"Macmini9,1") && strcmp(model,"Macmini8,1")) return 0;
    *fields=f;return 1;
}
int hv_legacy_dt_apply(uint8_t *tree,size_t size,const char *mlb,const char *model) {
    HvLegacyDTFields f;
    if(!hv_legacy_mlb_valid(mlb) ||
       !model || (strcmp(model,"VirtualMac2,1") && strcmp(model,"Macmini9,1") && strcmp(model,"Macmini8,1")) ||
       !hv_legacy_dt_fields(tree,size,&f) || strlen(model)+1>f.model_size) return 0;
    uint8_t value[32]={0},zero[32]={0};memcpy(value,mlb,17);
    if(memcmp(tree+f.mlb,zero,32) && memcmp(tree+f.mlb,value,32)) return 0;
    memcpy(tree+f.mlb,value,32);
    memset(tree+f.model,0,f.model_size);memcpy(tree+f.model,model,strlen(model));
    return 1;
}
