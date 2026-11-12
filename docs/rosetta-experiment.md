# Experimental Rosetta support

**This is experimental compatibility support. Intel applications run very slowly.**
It enables Apple's Rosetta inside Henge's ARM macOS guest; it is not a native
Intel CPU or a guarantee that arbitrary x86_64 applications will work. Apple
supplies Rosetta inside macOS. No Apple runtime, installer, boot ROM or disk
image is included in this source or the application package.

## Enable it

Use **Settings → Applications → Rosetta (Intel apps)**, then quit Henge
completely and reopen it. Install Apple's Rosetta inside macOS when requested.

- **Off (default):** load `HengeMainlineEngine.framework` from the unmodified
  mainline revision pinned in `ios/engine-mainline.txt`.
- **On:** load `HengeRosettaEngine.framework` with the experimental CPU changes.

Settings shows the engine selected for this launch and any pending change.
Boot and installation reject a pending change; the running VM never changes
engines. Switching back to the current setting clears the restart requirement.
Hosts without `hw.optional.arm.FEAT_AFP` cannot enable Rosetta in this version.

## Isolation and maintenance

The mainline pin is
[`9193fac644c9df111819bf64a8f7a68a1c6de1ad`](https://github.com/Leviidev/Henge/commit/9193fac644c9df111819bf64a8f7a68a1c6de1ad).
It is upstream main with the JIT startup and guest shutdown/restart fixes from
PRs #46 and #50 already incorporated. Both engines use that exact lifecycle
bridge, including JIT reuse.

`tools/build_ios_engines.py` exports the pinned source from Git. The off engine
builds its unmodified CPU, devices and host code. The on engine builds this
branch's CPU changes. Both expose a versioned function table; internal CPU
structures and symbols stay private. The generated manifest records source
and binary hashes.

The app links neither engine and loads exactly one framework per launch. A
missing framework or incompatible API is an error, with no fallback to the
other engine. The app owns its JIT mapping and passes it to the selected engine.
The unused engine adds package size but does not execute or allocate a VM.
The shared app UI, JIT setup and import/export code remain common to both modes.

Upstream Instant Resume is available through both engines. Saved-state identity
includes the selected engine and pinned revision, so changing engines requires
a cold boot. The Rosetta snapshot also preserves Apple integer flags,
exception-saved flags and floating-point controls.

When updating mainline, update the pin deliberately, retain the shared lifecycle
bridge, rebuild both frameworks and check both modes. Changes to the function
table require an ABI version change. Rosetta's CPU changes still need review
when mainline CPU internals change.

## CPU changes

- FlagM2 instructions and Apple parity/adjust flags, including saving and
  restoring the extra state across exceptions.
- Apple floating-point controls mapped onto the host's architectural AFP,
  with guest FPCR and effective host FPCR kept separate.
- Software TSO ordering for guest memory accesses, including slow and atomic
  paths. The additional ordering also costs time in ARM guest threads.
- Inline handling for common arithmetic flags and NZCV access, avoiding a C
  helper call in these cases. This does not make Intel applications fast.

The Rosetta framework additionally builds with `ROSETTA_4K_CACHE=1`. Its software
translation cache uses 4 KB entries, so the 4 KB guest mappings used by Intel
workloads can use cached data access, instruction fetch and branch targets.
Larger guest mappings use several entries. Cross-page accesses still check each
mapping and its permissions; invalidation covers every fragment of the guest
granule. This changes neither the guest's page tables nor the host's page size.

The option defaults to **off** in the Makefile and is enabled only for the
Rosetta framework by `tools/build_ios_engines.py`. The mainline framework still
builds from its pristine pin with its original 16 KB cache. The build manifest
and framework metadata record the software cache page size; a Rosetta boot also
logs `Rosetta 4 KB software translation cache enabled`.

This is an experimental performance change, not a measured Intel-app speedup.
It retains the entry count, reducing cache coverage from 256 MB to 64 MB per
table. Per-physical-page code metadata grows by four times (about 20.25 MiB versus
5.06 MiB of virtual allocation per GiB of guest RAM; only touched host pages
become resident). Shared single-address invalidations use the receiving CPU's
page size and may need more synchronization. Native workloads within this engine
can therefore have different memory and performance costs.

The experimental command-line frontend uses `HV_ROSETTA=1`; compile it with
`make ROSETTA_4K_CACHE=1` to opt into the new cache. The setting is fixed
when a machine is created. `HV_ROSETTA=2` selects the C arithmetic reference;
`HV_SOFTWARE_TSO=2` selects full memory barriers for comparison. The app's router
sets these modes itself, so environment variables cannot override its switch.

Register references: [Apple ACTLR definitions](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/arm64/proc_reg.h),
[Asahi Apple CPU registers](https://asahilinux.org/docs/hw/cpu/system-registers/).

## Device feedback

Bean 3.3.4 (Intel 64-bit) opens and can be used on a physical device with Rosetta
enabled. **Performance is very slow.** This is user-confirmed testing of the
Rosetta test build, not a claim that other applications or devices are supported.
The Bean result predates the 4 KB cache change. After trying the 4 KB cache build
on a physical device, the user reports that it feels slightly faster. No timed
comparison was collected, and Intel application performance remains very slow.

The [official Bean 3.3.4 download](https://www.bean-osx.com/releases/Bean-Install-3-3-4.zip)
contains an x86_64-only executable and is about 3.5 MB.

## Development checks

The CPU tests cover arithmetic flags, SSE edge cases, exception state and memory
ordering. `tests/test_vm_lifecycle.c` exercises repeated shutdown, restart,
failed boot and JIT reuse through the same lifecycle bridge as the engines.
The engine-selection checks cover separate loading, pending setting changes,
missing frameworks, wrong mode metadata and incompatible ABI versions.

`tests/test_rosetta_4k.c` covers noncontiguous pages, cross-page scalar/SIMD access,
permissions, atomic alignment, modified code and writable aliases, guest-granule
and range invalidation, address-space switches and a two-CPU invalidation with
different page sizes. Its `--bench N G` mode is a synthetic memory/indirect-branch
workload with `N` iterations and a guest page size of `2^G`; it does not measure
Bean, Steam or end-to-end Intel application performance. Build it in separate
directories with `ROSETTA_4K_CACHE=0` and `ROSETTA_4K_CACHE=1` for comparison.

Build both iOS engines with `python3 tools/build_ios_engines.py`. CPU and
lifecycle checks use the existing Makefile targets; `make test-ios-jit` checks
JIT readiness and allocation retries. Keep Apple restore assets and generated
logs outside version control.
