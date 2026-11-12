/* Boot the real macOS guest through the exact router and framework API shipped
   in Henge. The Simulator provides executable pages instead of a device debugger. */
#import <Foundation/Foundation.h>
#include "henge-vm.h"
#include "henge-ios-jit.h"
#include <TargetConditionals.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/mman.h>

#if !TARGET_OS_SIMULATOR
#error This lab must run in the iOS Simulator.
#endif

static HengeDualMapping mapping;
HengeDualMapping *henge_ios_jit_get_mapping(void) { return &mapping; }
static volatile sig_atomic_t stopping;
static void stop(int signal) { (void)signal; stopping = 1; }

static const char *option(int argc, char **argv, const char *key) {
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], key)) return argv[i + 1];
    return NULL;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        const char *frameworks = option(argc, argv, "--frameworks"), *mode = option(argc, argv, "--rosetta");
        const char *output = option(argc, argv, "--output"), *share = option(argc, argv, "--share");
        const char *rom = option(argc, argv, "--rom"), *disk = option(argc, argv, "--disk"), *aux = option(argc, argv, "--aux");
        const char *ecid = option(argc, argv, "--ecid");
        if (!frameworks || !mode || !output || !share || !rom || !disk || !aux || !ecid) return 2;
        setvbuf(stderr, NULL, _IONBF, 0);
        henge_engine_configure(atoi(mode), frameworks);
        mapping.size = 1024ull << 20;
        mapping.rx_addr = mmap(NULL, mapping.size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        vm_address_t rw = 0;
        vm_prot_t current, maximum;
        if (mapping.rx_addr == MAP_FAILED || vm_remap(mach_task_self(), &rw, mapping.size, 0, VM_FLAGS_ANYWHERE,
                mach_task_self(), (vm_address_t)mapping.rx_addr, FALSE, &current, &maximum, VM_INHERIT_NONE) ||
                mprotect(mapping.rx_addr, mapping.size, PROT_READ | PROT_EXEC)) return 3;
        mapping.rw_addr = (void *)rw;
        henge_vm_set_display(1024, 768);
        char socket[2048], error[1024];
        snprintf(socket, sizeof socket, "%s/share.sock", output);
        henge_vm_share(share, socket, NULL);
        if (henge_vm_boot(rom, disk, aux, strtoull(ecid, NULL, 0), 0, 8192, 2, error, sizeof error)) {
            fprintf(stderr, "engine lab: %s\n", error);
            return 1;
        }
        signal(SIGTERM, stop);
        signal(SIGINT, stop);
        dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), 5 * NSEC_PER_SEC, 0);
        dispatch_source_set_event_handler(timer, ^{
            @autoreleasepool {
                NSMutableArray *images = [NSMutableArray array];
                for (uint32_t i = 0; i < _dyld_image_count(); i++) {
                    const char *name = _dyld_get_image_name(i);
                    if (strstr(name, "HengeMainlineEngine.framework/") || strstr(name, "HengeRosettaEngine.framework/"))
                        [images addObject:@(name)];
                }
                NSDictionary *status = @{@"engine_rosetta": @(henge_engine_rosetta()),
                    @"revision": @(henge_engine_revision()), @"loaded_engines": images,
                    @"blocks": @(henge_vm_blocks()), @"cpus_on": @(henge_vm_cpus_on()),
                    @"guest_el0": @(henge_vm_guest_el0()), @"state": @(henge_vm_state()),
                    @"frames": @(henge_vm_gpu_frames()), @"platform": @"iOS Simulator"};
                NSData *json = [NSJSONSerialization dataWithJSONObject:status options:NSJSONWritingPrettyPrinted error:NULL];
                [json writeToFile:[@(output) stringByAppendingPathComponent:@"engine-status.json"] atomically:YES];
                if (henge_vm_state() == 2) {
                    fprintf(stderr, "engine lab: stopped (%s)\n", henge_vm_exit_reason());
                    exit(strcmp(henge_vm_exit_reason(), "halt") ? 1 : 0);
                }
                NSString *input = [NSString stringWithContentsOfFile:[@(output) stringByAppendingPathComponent:@"input.txt"]
                                                           encoding:NSUTF8StringEncoding error:NULL];
                if (stopping || [input containsString:@"stop\n"]) henge_vm_stop();
            }
        });
        dispatch_resume(timer);
        dispatch_main();
    }
}
