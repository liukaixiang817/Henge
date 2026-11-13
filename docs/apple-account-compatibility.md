# Experimental Apple Account compatibility

For Build 104 and its three supported restore builds (24G90, 25G83, 26B123),
see [the current macOS 15/26/27 report](apple-account-macos26-27-testing.md).
The Build 103 details below are retained as the earlier 15.6.1 test record.

This option creates a new Mac with an independent, persistent software identity.
It currently recognizes **macOS 15.6.1 (24G90)** only. It does not use the host's
serial number, Secure Enclave, Apple Account or identity-service entitlements.
Support for other macOS builds requires separate implementation and testing.
**A separate diagnostic identity has passed one iMessage send/read/reply test.
New installations use the same identity-generation rules in a version-2 profile.
This combined iOS build still needs physical-device testing; FaceTime is unverified.**

Build 103 adopts [upstream PR #121](https://github.com/Leviidev/Henge/pull/121),
pinned at `3ff3bea1a76a1e70c1e6e989feeafdd700de348c` on top of main
`1812892e129c149cf68cfa0f5e26de7f92e94fba`. It releases unused Mac-app address
reservations before the VM's RAM probe, cold boot, restore or installation.
The PR's JIT implementation and four-case address-reservation test are used
unchanged; its installer preflight assertions are combined with the existing
identity/media lifecycle fixture. All four address cases passed on macOS and
the iOS 27.0 Simulator. This combined IPA still needs physical-device validation.
The first physical iPhone log from
build 101 showed successful JIT execution, but a 3,584 MiB guest-RAM allocation
failed while the translation layer's unused 60 GiB reservations were still held.
This occurred before macOS or Apple Account sign-in started. The address-space
regression test reproduces allocation failure with the holds and success after
release; it also verifies that claimed Mac-app rooms and existing live mappings
are preserved. Physical acceptance of this fix remains pending.

In Install macOS, choose 15.6.1 and enable **Apple Account compatibility
(experimental)** before installing. Sign in through macOS System Settings.
An existing Mac cannot enable this option in the UI. Export/import preserves
the identity; importing a second copy of an already listed identity is refused.

The version-2 profile generates a synthetic Macmini8,1 serial using macserial's
default JYVX model code and production-line encoding, a matching KXPG board serial
with the classic MLB checksum, and a unicast MAC using the public `00:16:cb` OUI
and a freshly generated suffix. No complete identity from the test Mac is shipped.
Existing version-1 profiles retain their locally administered MAC and are loaded
unchanged; enabling the new behavior requires a new installation.
The existing per-machine ECID remains independent; it is generated before
restoring macOS. Do not clone provisioned authentication state into a new profile.
At early kernel boot, a bounds-checked parser finds the verified virtual-platform
device tree through its boot arguments and supplies the board serial and model.
It preserves the compatible strings used by the virtual hardware's drivers.
The boot configuration supplies distinct, stable Ethernet, Wi-Fi and Bluetooth
MAC values. In particular, the Wi-Fi device-tree value is needed by the legacy
NAC signing path even when the guest's network uses Ethernet.

The emulator recognizes the complete, version-checked XNU VMM sysctl handler
and routes its result for selected account, activation and cloud processes.
Other callers retain their original result. A mismatched kernel, handler,
device-tree key or process layout preserves the original result.
Guest kernel and system-framework files are not patched on disk.

These Macs support pause/continue while Henge remains open. Saving/restoring
RAM to disk is disabled, so reopening Henge after it is terminated requires a
cold boot. The profile remains in the machine manifest alongside its disk.

## Validation

Account authentication and individual cloud services require separate checks.
A signed-in sidebar, enabled toggle or returned signing blob does not establish
that files sync, a Store download completes, or a message reaches its recipient.

`make test-legacy-vmm test-legacy-dtree test-compat test-software-identity test-vm-lifecycle`
checks instruction guards, per-process policy, JIT behavior, profile persistence,
native boot/install configuration, pause/continue and the RAM-save restriction.
`make test` runs the existing CPU and device regressions.

The version-2 generator passed 6,022 checks including persistence, old-profile
loading and validation of the successful diagnostic profile supplied as a local
fixture. An independent check against OpenCore's actual macserial implementation
validated 1,000 generated serials, model codes, production lines, board dates and
MLB checksums. Native bridge tests also booted the public-OUI configuration and
checked distinct Ethernet, Wi-Fi and Bluetooth addresses. These checks do not
replace the user's upcoming physical iPhone/iPad test.

The combined test build also includes [PR #100](https://github.com/Leviidev/Henge/pull/100),
at `a4dca7b6ad587433e6bc5ba699aba5402edfb505`, for microphone input and the UVC
camera. Its iOS capture switches remain off by default. Use the running Mac's
information menu to enable sharing and grant the device's permission prompts.
The command-line Mac test runner exposes the devices but does not forward a
physical microphone or camera; silent input and black frames are expected there.
Guest device enumeration or synthetic capture does not establish that a call
was delivered or that physical iPhone/iPad capture was tested in this change.

`make build/icloud_guest_probe build/icloud_drive_probe` builds optional tools
to run **inside the guest**. The first reports presence flags and error codes,
without printing signatures or authentication headers. The second creates one
uniquely named test file in iCloud Drive, waits for an acknowledged upload,
confirms eviction, downloads it and compares bytes. It does not read existing
cloud files. Its JSON reports failure if any of those stages is unconfirmed.
Test files have a `Henge-iCloud-test-` prefix and can be removed after testing.

## Service results, 12 November 2026

The implementation is based on upstream `8e46b96a0b4375e1a272a7e7873030997b071692`.
The tests used macOS guests running in Henge's native Mac command-line runner.
The combined unsigned iOS app builds successfully; this combined build has not
been installed on a physical iPhone or iPad in this test run.

| Service | Confirmed behavior |
| --- | --- |
| Apple Account | Official System Settings sign-in and fresh two-factor authentication succeeded in a new installation with a new ECID. |
| iCloud Drive | Upload acknowledged, local file evicted, original downloaded again and bytes compared successfully. |
| App Store | Microsoft To Do 2.177 downloaded from the authenticated Australian Store; receipt and code signature checked; app launched to its welcome screen. |
| Contacts | A uniquely marked test contact arrived in an independent guest's local store. |
| Calendar | A marked iCloud calendar and its event arrived in an independent guest. No invitations or alarms. |
| Reminders | A marked iCloud list and reminder arrived in an independent guest. |
| Notes | A marked folder and note arrived in the pristine installation. |
| Photos | The pristine installation received the test asset and downloaded its 64-by-64 original; filename and SHA-256 were checked. |
| Passwords / Keychain | The official Passwords app received the marked test credential in the pristine installation. Copying only that item's password and comparing it inside the guest confirmed the bytes; the test clipboard was cleared. |
| Safari bookmarks | The pristine installation's official bookmark manager received the exact marked title and test URL. |
| iCloud Mail | Created the user-selected mailbox, sent one marked message to that mailbox itself, and matched the received sender, subject and body in Inbox. |
| iMessage | A separate fresh installation using an OpenCore macserial pair and Apple-OUI MAC, with another account, completed official two-factor sign-in. One marked message produced a Read receipt, and the user's reply arrived. Delivery with the default generated profile is not established by this control. |
| FaceTime | Sign-in reached the calling UI, but delivery failed in the earlier test and the fresh installation's recipient lookup did not complete. Camera enumeration/capture did not resolve activation. |

Runtime evidence is retained locally under `build/service-results/`. Build and
unit-test success must not be reported as successful service delivery. In
particular, the unsigned diagnostic helper's Keychain entitlement error is not
a cloud-sync failure; the official Passwords app provided the functional check.

The fresh communication-service attempt reported `IDSBAASignerErrorDomain:4`
and `com.apple.MobileActivation.ErrorDomain:-4` (UCRT unavailable). These errors
alone do not establish service failure or an account-region problem: the later
diagnostic installation completed an iMessage exchange while the same BAA and
MobileActivation errors were still present in its registration log. The experimental
DeviceIdentity capability override did not resolve it and is not included.

No account region or phone number was changed. The account's request for a
Chinese phone number also occurs on the user's physical Mac, according to the
user, and is not used as evidence of an emulator incompatibility.

The account currently offers a free 5 GB plan. iCloud+ subscription features,
family/collaboration invitations, HomeKit accessories, purchases and destructive
Find My operations are outside the confirmed results above. Their settings
being present is not functional validation.

## Additional local communication-service diagnosis

The earlier default-profile registration log reports status 6009 after NAC initialization succeeds.
FaceTime reaches its calling UI, but recipient lookup does not complete. The
presence of its calling window does not establish successful service activation.
TCP connections to Apple's APNs ports 5223 and 443 succeeded, and the APS daemon
reported status 0. Two independently installed guests have different platform
UUIDs, chip identifiers and attestation identifiers; their model, serial and MLB
match their configured software identities, with en0 marked built-in. A task-owned NVRAM test variable also survived a graceful shutdown and cold boot; the variable was then deleted and its removal verified.

An isolated override of `IDSValidationSession._shouldUsePSCBAA` was checked
against the measured executable but did not execute in these login attempts.
It has been reverted. Its unsigned candidate IPA is an archived experiment and
must not be described as a verified communication-service fix.

A separate fresh-install control uses identifiers generated by OpenCore's
macserial and an Apple OUI with a random suffix, following GenSMBIOS's ROM
approach. With another account it completed official two-factor sign-in, NAC
initialization and registration without status 6009. Exactly one marked test
message was sent to the authorized recipient. Messages displayed a Read receipt,
and the user's reply arrived in the same conversation. No second test message
or FaceTime call was made. Evidence is retained in
`build/service-results/imessage-one-shot-new-account.json` and
`build/service-results/imessage-single-message-read-and-reply.png`.

This control changes both the account and the generated identity. It establishes
an iMessage round trip in this diagnostic configuration, but does not isolate
the cause of the earlier 6009 response or validate FaceTime. Its complete identity
remains lab-only. New version-2 profiles follow its generation rules, while existing
version-1 identities are preserved. The source baseline was
refreshed to upstream `8a5cb690eda4c400a679c9acd15225256d4e5fd3`; that refresh
changes the separate AppKit/TL path and leaves the native Mac emulator unchanged.

No Apple support request was submitted; the user declined that action.

Research references:

- [VMHide's process-dependent VMM policy](https://github.com/Carnations-Botanica/VMHide)
- [OpenCore iServices setup](https://dortania.github.io/OpenCore-Post-Install/universal/iservices.html)
- [OpenCore's classic serial formats](https://github.com/acidanthera/OpenCorePkg/tree/master/Utilities/macserial)
- [Apple's ubiquitous-file upload status](https://developer.apple.com/documentation/foundation/urlresourcekey/ubiquitousitemisuploadedkey)

- [Rustpush macOS NAC registration validation](https://github.com/OpenBubbles/rustpush/blob/master/src/macos.rs)
