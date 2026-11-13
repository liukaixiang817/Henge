#include "legacy_dtree.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
static void check(const char *name,int value) {checks++;if(!value){printf("FAIL %s\n",name);failures++;}}
static size_t property(uint8_t *b,size_t at,const char *name,const void *value,size_t n) {
    memset(b+at,0,36);memcpy(b+at,name,strlen(name));uint32_t len=(uint32_t)n;memcpy(b+at+32,&len,4);
    memcpy(b+at+36,value,n);return at+36+((n+3)&~(size_t)3);
}
int main(void) {
    uint8_t tree[1024]={0},original[1024],zero[32]={0};uint32_t count=4;memcpy(tree,&count,4);
    size_t at=8;
    at=property(tree,at,"name","device-tree",12);
    static const char compatible[]="VMA2MACOSAP\0VirtualMac2,1\0AppleVirtualPlatformARM";
    size_t compat_start=at;
    at=property(tree,at,"compatible",compatible,sizeof compatible);
    at=property(tree,at,"model","VirtualMac2,1",14);
    at=property(tree,at,"mlb-serial-number",zero,sizeof zero);
    memcpy(original,tree,sizeof tree);HvLegacyDTFields fields;
    check("virtual root accepted",hv_legacy_dt_fields(tree,at,&fields));
    for(size_t n=0;n<at;n++) check("truncated tree rejected",!hv_legacy_dt_fields(tree,n,&fields));
    check("short MLB rejected",!hv_legacy_dt_apply(tree,at,"ABC","Macmini9,1"));
    check("other platform model rejected",!hv_legacy_dt_apply(tree,at,"HENGE123456789012","iPhone17,3"));
    check("invalid requests leave bytes intact",!memcmp(tree,original,sizeof tree));
    check("software fields applied",hv_legacy_dt_apply(tree,at,"C07323700J9KXPG1M","Macmini9,1"));
    check("MLB set",!memcmp(tree+fields.mlb,"C07323700J9KXPG1M",17));
    check("model set",!strcmp((char *)tree+fields.model,"Macmini9,1"));
    check("driver match preserved",!memcmp(tree+compat_start,original+compat_start,36+((sizeof compatible+3)&~3u)));
    check("same profile idempotent",hv_legacy_dt_apply(tree,at,"C07323700J9KXPG1M","Macmini9,1"));
    check("other existing identity protected",!hv_legacy_dt_apply(tree,at,"C07323700J9KXVT1S","Macmini8,1"));
    memcpy(tree,original,sizeof tree);memset(tree+8,'X',32);
    check("unterminated property name rejected",!hv_legacy_dt_fields(tree,at,&fields));
    memcpy(tree,original,sizeof tree);memset(tree+8+32,255,4);
    check("overflow property rejected",!hv_legacy_dt_fields(tree,at,&fields));
    memcpy(tree,original,sizeof tree);memset(tree+compat_start+36,'X',sizeof compatible);
    check("non-virtual tree rejected",!hv_legacy_dt_fields(tree,at,&fields));
    check("generated board checksum",hv_legacy_mlb_valid("C07323700J9KXPG1M"));
    check("bad board checksum rejected",!hv_legacy_mlb_valid("C07323700J9KXPG1N"));
    check("missing board rejected",!hv_legacy_mlb_valid(NULL));
    uint64_t args[14]={0},pa=0;size_t bytes=0;
    args[0]=0x00020002;args[1]=0xfffffe0000000000ull;args[2]=0x70000000;args[3]=64ull<<20;
    args[12]=args[1]+0x20000;args[13]=4096;
    check("virtual boot tree decoded",hv_legacy_dt_boot_args((void *)args,sizeof args,0x70000000,64ull<<20,&pa,&bytes) && pa==0x70020000 && bytes==4096);
    for(size_t n=0;n<108;n++)check("truncated boot args rejected",!hv_legacy_dt_boot_args((void *)args,n,0x70000000,64ull<<20,&pa,&bytes));
    args[12]=0x70030000;
    check("physical boot tree decoded",hv_legacy_dt_boot_args((void *)args,sizeof args,0x70000000,64ull<<20,&pa,&bytes) && pa==0x70030000);
    args[12]=0x74000000;
    check("tree outside RAM rejected",!hv_legacy_dt_boot_args((void *)args,sizeof args,0x70000000,64ull<<20,&pa,&bytes));
    args[12]=args[1]+0x20000;args[2]+=0x4000;
    check("physical base offset respected",hv_legacy_dt_boot_args((void *)args,sizeof args,0x70000000,64ull<<20,&pa,&bytes) && pa==0x70024000);
    args[13]=0x100001;
    check("oversized tree rejected",!hv_legacy_dt_boot_args((void *)args,sizeof args,0x70000000,64ull<<20,&pa,&bytes));
    printf("legacy device tree: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
