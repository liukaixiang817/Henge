/* Run INSIDE a Henge macOS guest. The report contains availability, presence
   flags, and error codes only. Never print an AKAnisetteData description: it
   contains authentication material. No Apple Account credentials are needed. */
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <dlfcn.h>
#import <sys/sysctl.h>
#import <objc/runtime.h>

@interface NSObject (HengeAnisetteProbe)
- (void)anisetteDataWithCompletion:(void (^)(id,NSError *))completion;
- (void)fetchAnisetteDataAndProvisionIfNecessary:(BOOL)provision withCompletion:(void (^)(id,NSError *))completion;
+ (NSDictionary *)retrieveOTPHeadersForDSID:(NSString *)dsid;
+ (id)sharedSigner;
- (void)signatureForData:(NSData *)data completion:(void (^)(id,NSError *))completion;
@end

static BOOL servicePresent(const char *name) {
    io_service_t service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching(name));
    if(service) IOObjectRelease(service);
    return service!=0;
}

static BOOL valuePresent(id object,NSString *key) {
    @try { id value=[object valueForKey:key];return value && (![value respondsToSelector:@selector(length)] || [value length]>0); }
    @catch(NSException *exception) { (void)exception;return NO; }
}

static NSDictionary *identityFields(void) {
    io_registry_entry_t chosen=IORegistryEntryFromPath(kIOMainPortDefault,"IODeviceTree:/chosen");
    CFTypeRef wifi=chosen?IORegistryEntryCreateCFProperty(chosen,CFSTR("mac-address-wifi0"),kCFAllocatorDefault,0):NULL;
    CFIndex length=wifi && CFGetTypeID(wifi)==CFDataGetTypeID()?CFDataGetLength(wifi):0;
    BOOL nonzero=NO;
    if(length>=6) {const UInt8 *b=CFDataGetBytePtr(wifi);for(int i=0;i<6;i++) if(b[i]) nonzero=YES;}
    if(wifi) CFRelease(wifi);if(chosen) IOObjectRelease(chosen);
    return @{@"wifiMacPresent":@(nonzero),@"wifiMacLength":@(length)};
}

int main(int argc,const char **argv) {
    @autoreleasepool {
        BOOL provision=NO,legacy=NO,nacAPI=NO,nac=NO;
        if(argc==2 && !strcmp(argv[1],"--provision")) provision=YES;
        else if(argc==2 && !strcmp(argv[1],"--legacy")) legacy=YES;
        else if(argc==2 && !strcmp(argv[1],"--nac-api")) nacAPI=YES;
        else if(argc==2 && !strcmp(argv[1],"--nac")) nac=YES;
        else if(argc!=1) {fprintf(stderr,"usage: icloud_guest_probe [--provision|--legacy|--nac-api|--nac]\n");return 2;}
        int vmm=-1;size_t vmmSize=sizeof vmm;
        int vmmError=sysctlbyname("kern.hv_vmm_present",&vmm,&vmmSize,NULL,0);
        NSMutableDictionary *out=[NSMutableDictionary dictionaryWithDictionary:@{
            @"schema":@1, @"fullICloudLoginTested":@NO,
            @"os":NSProcessInfo.processInfo.operatingSystemVersionString,
            @"identityMessengerPresent":@(servicePresent("AppleVirtualPlatformIdentityVirtioMessenger")),
            @"identityServicePresent":@(servicePresent("AppleVirtualPlatformIdentityService")),
            @"provisioningRequested":@(provision),
            @"legacyAOSKitRoute":@(legacy),
            @"vmmQuerySucceeded":@(vmmError==0),@"vmmPresent":@(vmm)
        }];
        [out addEntriesFromDictionary:identityFields()];
        if(nac || nacAPI) {
            dlopen("/System/Library/PrivateFrameworks/AppleAccount.framework/AppleAccount",RTLD_NOW|RTLD_LOCAL);
        }
        if(nac) {
            Class cls=NSClassFromString(@"AAAbsintheSigner");
            id signer=cls && [cls respondsToSelector:@selector(sharedSigner)] ? [(id)cls sharedSigner] : nil;
            BOOL available=signer && [signer respondsToSelector:@selector(signatureForData:completion:)];
            out[@"nacSignerAvailable"]=@(available);
            __block BOOL done=NO;
            if(available) [signer signatureForData:[@"Henge software identity diagnostic" dataUsingEncoding:NSUTF8StringEncoding] completion:^(id signature,NSError *error) {
                @synchronized(out) {
                    out[@"signatureReturned"]=@(signature!=nil);
                    if([error isKindOfClass:NSError.class]) {
                        out[@"error"]=@{@"domain":error.domain,@"code":@(error.code)};
                        id underlying=error.userInfo[@"AAAbsintheError"];
                        if([underlying isKindOfClass:NSNumber.class]) out[@"absintheError"]=underlying;
                    }
                    done=YES;
                }
            }];
            NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:30];
            while(deadline.timeIntervalSinceNow>0) {
                @synchronized(out) {if(done) break;}
                [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
            }
            @synchronized(out) {
                out[@"completed"]=@(done);
                NSData *data=[NSJSONSerialization dataWithJSONObject:out options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:NULL];
                if(!data)return 1;
                fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);
            }
            return done?0:3;
        }
        if(nacAPI) {
            NSMutableDictionary *classes=NSMutableDictionary.new;
            unsigned count=0;Class *list=objc_copyClassList(&count);
            for(unsigned i=0;i<count;i++) {
                NSString *name=NSStringFromClass(list[i]);
                if(![name hasPrefix:@"AA"] || ([name rangeOfString:@"absinthe" options:NSCaseInsensitiveSearch].location==NSNotFound && ![name hasPrefix:@"AANAC"])) continue;
                NSMutableArray *methods=NSMutableArray.new;
                for(int meta=0;meta<2;meta++) {
                    unsigned n=0;Method *m=class_copyMethodList(meta?object_getClass(list[i]):list[i],&n);
                    for(unsigned j=0;j<n;j++) [methods addObject:@{@"classMethod":@(meta!=0),@"selector":NSStringFromSelector(method_getName(m[j])),@"type":@(method_getTypeEncoding(m[j]))}];
                    free(m);
                }
                classes[name]=methods;
            }
            free(list);out[@"nacClasses"]=classes;
            NSData *data=[NSJSONSerialization dataWithJSONObject:out options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:NULL];
            if(!data)return 1;
            fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);return 0;
        }
        if(legacy) {
            dlopen("/System/Library/PrivateFrameworks/AOSKit.framework/AOSKit",RTLD_NOW|RTLD_LOCAL);
            Class aos=NSClassFromString(@"AOSUtilities");
            BOOL available=aos && [aos respondsToSelector:@selector(retrieveOTPHeadersForDSID:)];
            out[@"legacyAPIAvailable"]=@(available);
            __block BOOL done=NO;
            if(available) dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
                NSDictionary *headers=[(id)aos retrieveOTPHeadersForDSID:@"-2"];
                @synchronized(out) {
                    out[@"machineIDPresent"]=@([headers[@"X-Apple-MD-M"] length]>0);
                    out[@"oneTimePasswordPresent"]=@([headers[@"X-Apple-MD"] length]>0);
                    out[@"anisetteReturned"]=@(headers.count>0);
                    done=YES;
                }
            });
            NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:30];
            while(deadline.timeIntervalSinceNow>0) {
                @synchronized(out) {if(done) break;}
                [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
            }
            @synchronized(out) {
                out[@"completed"]=@(done);
                NSData *data=[NSJSONSerialization dataWithJSONObject:out options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:NULL];
                if(!data) return 1;
                fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);
            }
            return done ? 0 : 3;
        }
        dlopen("/System/Library/PrivateFrameworks/AuthKit.framework/AuthKit",RTLD_NOW|RTLD_LOCAL);
        Class cls=NSClassFromString(@"AKAnisetteProvisioningController");
        id controller=cls ? [[cls alloc] init] : nil;
        SEL selector=NSSelectorFromString(provision ? @"fetchAnisetteDataAndProvisionIfNecessary:withCompletion:" : @"anisetteDataWithCompletion:");
        out[@"anisetteAPIAvailable"]=@(controller && [controller respondsToSelector:selector]);
        __block BOOL finished=NO;
        if(controller && [controller respondsToSelector:selector]) {
            void (^completion)(id,NSError *)=^(id data,NSError *error) {
                @synchronized(out) {
                    out[@"anisetteReturned"]=@(data!=nil);
                    out[@"machineIDPresent"]=@(valuePresent(data,@"machineID"));
                    out[@"oneTimePasswordPresent"]=@(valuePresent(data,@"oneTimePassword"));
                    out[@"baaAttestationPresent"]=@(valuePresent(data,@"baaAttestationData"));
                    if(error) {
                        out[@"error"]=@{@"domain":error.domain,@"code":@(error.code)};
                        if([error.domain isEqual:NSCocoaErrorDomain] && error.code==4099) {
                            NSString *reason=error.userInfo[NSDebugDescriptionErrorKey];
                            if([reason isKindOfClass:NSString.class])
                                out[@"xpcFailureReason"]=[reason substringToIndex:MIN(reason.length,512)];
                        }
                        NSError *underlying=error.userInfo[NSUnderlyingErrorKey];
                        if(underlying) out[@"underlyingError"]=@{@"domain":underlying.domain,@"code":@(underlying.code)};
                    }
                    finished=YES;
                }
            };
            if(provision) [controller fetchAnisetteDataAndProvisionIfNecessary:YES withCompletion:completion];
            else [controller anisetteDataWithCompletion:completion];
            NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:30];
            for(;;) {
                @synchronized(out) {if(finished) break;}
                if(deadline.timeIntervalSinceNow<=0) break;
                [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
            }
        }
        @synchronized(out) {
            out[@"completed"]=@(finished);
            NSData *data=[NSJSONSerialization dataWithJSONObject:out options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:NULL];
            if(!data) return 1;
            fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);
        }
        return finished ? 0 : 3;
    }
}
