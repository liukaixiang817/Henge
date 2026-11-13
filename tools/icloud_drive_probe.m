/* Guest-only end-to-end test. Creates one uniquely named test file, waits for
   an acknowledged upload, evicts it, downloads it again and compares bytes.
   Never enumerates or reads existing iCloud files. */
#import <Foundation/Foundation.h>

static NSDictionary *errorCode(NSError *e) {
    return e ? @{ @"domain":e.domain, @"code":@(e.code) } : @{};
}
static NSDictionary *state(NSURL *url) {
    [url removeAllCachedResourceValues];
    NSArray *keys=@[NSURLIsUbiquitousItemKey,NSURLUbiquitousItemIsUploadedKey,
        NSURLUbiquitousItemIsUploadingKey,NSURLUbiquitousItemDownloadingStatusKey,
        NSURLUbiquitousItemUploadingErrorKey,NSURLUbiquitousItemDownloadingErrorKey];
    NSError *error=nil;
    NSDictionary *values=[url resourceValuesForKeys:keys error:&error];
    NSMutableDictionary *result=NSMutableDictionary.new;
    for(NSString *key in keys) {
        id v=values[key];
        if([v isKindOfClass:NSError.class]) result[key]=errorCode(v);
        else if([v isKindOfClass:NSString.class] || [v isKindOfClass:NSNumber.class]) result[key]=v;
    }
    if(error) result[@"queryError"]=errorCode(error);
    return result;
}
static void waitFor(BOOL (^condition)(void),int seconds) {
    NSDate *end=[NSDate dateWithTimeIntervalSinceNow:seconds];
    while(!condition() && end.timeIntervalSinceNow>0)
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1]];
}
int main(void) {
    @autoreleasepool {
        NSFileManager *fm=NSFileManager.defaultManager;
        NSURL *directory=[NSURL fileURLWithPath:[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Mobile Documents/com~apple~CloudDocs"] isDirectory:YES];
        BOOL isDirectory=NO;
        NSMutableDictionary *report=[@{@"schema":@1,@"service":@"iCloud Drive",@"passed":@NO} mutableCopy];
        if(![fm fileExistsAtPath:directory.path isDirectory:&isDirectory] || !isDirectory) {
            report[@"blocked"]=@"iCloud Drive directory is unavailable";
        } else {
            NSString *name=[NSString stringWithFormat:@"Henge-iCloud-test-%@.txt",NSUUID.UUID.UUIDString];
            NSURL *url=[directory URLByAppendingPathComponent:name];
            NSData *payload=[[NSString stringWithFormat:@"Henge iCloud round-trip test\n%@\n",name] dataUsingEncoding:NSUTF8StringEncoding];
            NSError *error=nil;
            report[@"testFile"]=name;
            BOOL written=[payload writeToURL:url options:NSDataWritingWithoutOverwriting error:&error];
            report[@"created"]=@(written);
            if(!written) report[@"createError"]=errorCode(error);
            if(written) {
                waitFor(^BOOL{ return [state(url)[NSURLUbiquitousItemIsUploadedKey] boolValue]; },120);
                NSDictionary *uploaded=state(url);report[@"afterUpload"]=uploaded;
                BOOL acknowledged=[uploaded[NSURLIsUbiquitousItemKey] boolValue] && [uploaded[NSURLUbiquitousItemIsUploadedKey] boolValue];
                report[@"uploadAcknowledged"]=@(acknowledged);
                if(acknowledged) {
                    error=nil;
                    BOOL evicted=[fm evictUbiquitousItemAtURL:url error:&error];
                    report[@"evictionRequested"]=@(evicted);
                    if(!evicted) report[@"evictionError"]=errorCode(error);
                    if(evicted) {
                        waitFor(^BOOL{return [state(url)[NSURLUbiquitousItemDownloadingStatusKey] isEqual:NSURLUbiquitousItemDownloadingStatusNotDownloaded];},45);
                        NSDictionary *removed=state(url);report[@"afterEviction"]=removed;
                        BOOL confirmed=[removed[NSURLUbiquitousItemDownloadingStatusKey] isEqual:NSURLUbiquitousItemDownloadingStatusNotDownloaded];
                        report[@"evictionConfirmed"]=@(confirmed);
                        error=nil;
                        BOOL started=confirmed && [fm startDownloadingUbiquitousItemAtURL:url error:&error];
                        report[@"downloadRequested"]=@(started);
                        if(error) report[@"downloadError"]=errorCode(error);
                        if(started) {
                            waitFor(^BOOL{return [state(url)[NSURLUbiquitousItemDownloadingStatusKey] isEqual:NSURLUbiquitousItemDownloadingStatusCurrent];},120);
                            report[@"afterDownload"]=state(url);
                            NSData *download=[NSData dataWithContentsOfURL:url options:0 error:&error];
                            BOOL same=[download isEqualToData:payload];
                            report[@"downloadMatches"]=@(same);
                            report[@"passed"]=@(same);
                        }
                    }
                }
            }
        }
        NSData *json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:NULL];
        if(!json) return 2;
        fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);
        return [report[@"passed"] boolValue]?0:1;
    }
}
