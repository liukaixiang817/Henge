/* Experimental command-line frontend for the actual Henge engine inside an
   iOS Simulator. No Hypervisor.framework and no attached iPhone are used.
   Keep all files in a dedicated test directory; nothing is created implicitly. */
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include <TargetConditionals.h>
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>

#include "vm.h"
#include "install.h"
#include "dispatch.h"
#include "pvg_lite.h"
#include "net_slirp.h"
#include "share.h"

#if !TARGET_OS_SIMULATOR
#error This experiment must run in an iOS Simulator.
#endif

static const char *output;
static volatile sig_atomic_t stop_requested;
static _Atomic int install_step = -1, install_percent;
static int installing;

static const char *option(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], name)) return argv[i + 1];
    return NULL;
}

static void progress(int step, double fraction, void *unused) {
    (void)unused;
    atomic_store(&install_step, step);
    atomic_store(&install_percent, (int)(fraction * 100));
    fprintf(stderr, "lab: restore step %d %.1f%%\n", step, fraction * 100);
}

static void on_signal(int sig) { (void)sig; stop_requested = 1; }

static void on_exit(int reason) {
    fprintf(stderr, "lab: guest stopped: %s, restore result %d\n",
            hv_vm_reason_name(reason), installing ? hv_install_result() : 0);
    exit(reason != HV_EXIT_HALT || (installing && hv_install_result() < 0));
}

static void snapshot(void) {
    enum { W = 1024, H = 768 };
    static uint8_t pixels[W * H * 4];
    const uint8_t *source = pixels;
    uint32_t width = W, height = H, stride = W * 4;
    if (hv_vm_snapshot(pixels, W, H)) {
        HvBootDisplay boot;
        if (hv_vm_boot_display(&boot)) return;
        source = boot.pixels;
        width = boot.width;
        height = boot.height;
        stride = boot.stride;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate((void *)source, width, height, 8, stride, cs,
                                            kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
    CGImageRef image = ctx ? CGBitmapContextCreateImage(ctx) : NULL;
    if (image) {
        NSString *path = [@(output) stringByAppendingPathComponent:@"guest.png"];
        NSURL *url = [NSURL fileURLWithPath:path];
        CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, NULL);
        if (dst) {
            CGImageDestinationAddImage(dst, image, NULL);
            CGImageDestinationFinalize(dst);
            CFRelease(dst);
        }
        CGImageRelease(image);
    }
    if (ctx) CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
}

static void key(int usage) {
    hv_vm_key_hid(usage, 1);
    usleep(40000);
    hv_vm_key_hid(usage, 0);
    usleep(40000);
}

static void text(const char *s) {
    const char *plain = "1234567890-=[]\\;'`,./";
    const char *shifted = "!@#$%^&*()_+{}|:\"~<>?";
    const uint8_t usages[] = {30,31,32,33,34,35,36,37,38,39,45,46,47,48,49,51,52,53,54,55,56};
    for (; *s; s++) {
        int usage = 0, shift = 0;
        if (*s >= 'a' && *s <= 'z') usage = 4 + *s - 'a';
        else if (*s >= 'A' && *s <= 'Z') usage = 4 + *s - 'A', shift = 1;
        else if (*s == ' ') usage = 44;
        else if (*s == '\t') usage = 43;
        else {
            const char *p = strchr(plain, *s);
            if (p) usage = usages[p - plain];
            else if ((p = strchr(shifted, *s))) usage = usages[p - shifted], shift = 1;
        }
        if (!usage) continue;
        if (shift) hv_vm_key_hid(225, 1);
        key(usage);
        if (shift) hv_vm_key_hid(225, 0);
    }
}

/* Same file-following convention as host/main.c's HV_INPUT_FILE. */
static void *input_thread(void *unused) {
    (void)unused;
    char path[2048];
    snprintf(path, sizeof path, "%s/input.txt", output);
    FILE *f = fopen(path, "a+");
    if (!f) return NULL;
    long at = 0;
    for (;;) {
        char line[2048];
        fseek(f, at, SEEK_SET);
        if (!fgets(line, sizeof line, f) || !strchr(line, '\n')) {
            clearerr(f);
            usleep(100000);
            continue;
        }
        at = ftell(f);
        line[strcspn(line, "\n")] = 0;
        int x, y;
        if (sscanf(line, "hiddown %i", &x) == 1) hv_vm_key_hid(x, 1);
        else if (sscanf(line, "hidup %i", &x) == 1) hv_vm_key_hid(x, 0);
        else if (sscanf(line, "hidkey %i", &x) == 1) key(x);
        else if (sscanf(line, "wait %d", &x) == 1 && x >= 0 && x <= 60) sleep(x);
        else if (!strncmp(line, "type ", 5)) text(line + 5);
        else if (sscanf(line, "click %d %d", &x, &y) == 2) {
            for (int tries = 0; tries < 10; tries++) {
                int px, py;
                if (hv_vm_pointer(&px, &py)) break;
                if (abs(x - px) <= 2 && abs(y - py) <= 2) break;
                hv_vm_mouse_move(x - px, y - py);
                usleep(100000);
            }
            hv_vm_mouse_button(0, 1);
            usleep(60000);
            hv_vm_mouse_button(0, 0);
        } else if (!strcmp(line, "stop")) stop_requested = 1;
        fprintf(stderr, "lab: input: %s\n", line);
    }
}

int main(int argc, char **argv) {
    @autoreleasepool {
        setvbuf(stderr, NULL, _IONBF, 0);
        output = option(argc, argv, "--output");
        const char *ecid = option(argc, argv, "--ecid");
        const char *ipsw = option(argc, argv, "--install");
        const char *ram = option(argc, argv, "--ram");
        const char *cpus = option(argc, argv, "--cpus");
        const char *share = option(argc, argv, "--share");
        if (!output || !ecid) {
            fprintf(stderr, "usage: simulator_lab --rom FILE --disk FILE --aux FILE --ecid N --output DIR [--install IPSW] [--ram MB] [--cpus N]\n");
            return 2;
        }
        HvVmConfig config = {.rom = option(argc, argv, "--rom"),
                             .disk = option(argc, argv, "--disk"),
                             .aux = option(argc, argv, "--aux"),
                             .ecid = strtoull(ecid, NULL, 0), .ram_mb = ram ? atoi(ram) : 8192,
                             .ncpus = cpus ? atoi(cpus) : 2, .no_sound = 1, .log = 1, .installer = ipsw != NULL};
        hv_pvg_lite_set_mode(1024, 768);
        if (share) {
            char socket[2048];
            snprintf(socket, sizeof socket, "%s/share.sock", output);
            if (hv_share_start(share, socket, NULL)) return 1;
            hv_net_set_share_socket(socket);
        }
        char err[512];
        if (hv_vm_create(&config, err, sizeof err)) { fprintf(stderr, "lab: %s\n", err); return 1; }
        HvMachine *m = hv_vm_machine();
        fprintf(stderr, "lab: iOS Simulator, pure Henge CPU, Rosetta %d, software TSO %d, %d CPUs\n", m->rosetta, m->software_tso, hv_vm_ncpus());
        installing = ipsw != NULL;
        if (ipsw && hv_install_start(hv_vm_board()->vusb, ipsw, output, config.ecid, progress, NULL)) return 1;
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        hv_vm_start(1, on_exit);
        pthread_t input;
        pthread_create(&input, NULL, input_thread, NULL);
        dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), 10 * NSEC_PER_SEC, NSEC_PER_SEC);
        dispatch_source_set_event_handler(timer, ^{
            @autoreleasepool {
                NSMutableArray *states = [NSMutableArray array];
                for (int i = 0; i < hv_vm_ncpus(); i++) {
                    HvCpu *c = hv_vm_cpu(i);
                    [states addObject:@{@"pc": [NSString stringWithFormat:@"%llx", c->pc], @"el": @(c->el),
                                        @"actlr": @(c->actlr_el1), @"halted": @(c->halted)}];
                }
                NSDictionary *status = @{@"software_tso": @(m->software_tso), @"rosetta": @(m->rosetta), @"platform": @"iOS Simulator",
                                         @"cpus": states, @"blocks": @(m->stats.blocks), @"translations": @(m->stats.translations),
                                         @"jit_bytes": @(m->jit.used * 4), @"frames": @(hv_vm_gpu_frames()),
                                         @"restore_step": @(atomic_load(&install_step)), @"restore_percent": @(atomic_load(&install_percent))};
                NSData *json = [NSJSONSerialization dataWithJSONObject:status options:NSJSONWritingPrettyPrinted error:NULL];
                [json writeToFile:[@(output) stringByAppendingPathComponent:@"status.json"] atomically:YES];
                snapshot();
                if (stop_requested || (installing && hv_install_result() <= 0)) hv_vm_stop();
            }
        });
        dispatch_resume(timer);
        dispatch_main();
    }
}
