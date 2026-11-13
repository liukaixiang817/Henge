# macOS 26 / 27 compatibility work

This worktree starts at PR #121 (`3ff3bea`) with the saved Build 103 iCloud
patch applied. The macOS 15.6.1 checkpoint is stored separately at
`checkpoints/macos-15.6.1-build103-20261112` in the main workspace. Its source
and IPA are not updated by these experiments.

## Pinned restore kernels

The source is each release's Apple CDN restore URL already listed in
`ios/Henge/Installer.swift`. Both BuildManifest files report the expected build.
Only these exact kernel versions are recognized; other builds retain the
original behavior.

| Guest build | Version string offset before handler | proc command offset | Decompressed vma2 kernel SHA-256 |
| --- | --- | --- | --- |
| 26.6.2 / 25G83 | 0x325715f | 0x5cc | 268f431e7366409dc798cd8f719a93cf3a30c6f828201d8eeceae889358099c6 |
| 27.2 / 26B123 | 0x354895f | 0x5bc | 01332067965f721b7e6984348510b717ff346d253d7639212dcd9f6c6161afaf |

Each kernel has one candidate with the complete verified sysctl instruction
shape. Its ADRP/ADD pair resolves to `vmm-present`. Process command offsets
were checked against `proc_name`; the adjacent full name field remains 17
bytes later. The complete NUL-terminated Darwin version is also required at
runtime before selecting a process layout.

## Validation status

- Native executable builds successfully.
- `tools/audit_icloud_kernel.py` verifies both actual kernels against the compiled matcher, native profile table, sysctl key, proc_name instruction and adjacent full-name accessor.
- Process policy: 166 checks passed, including the new layout, incorrect layout,
  unknown offset, overflow, truncation and mismatched names.
- Device-tree checks: 393 passed.
- Translation checks: 424 passed.
- Independent new synthetic identities, empty disks and auxiliary stores prepared.
- macOS 26 restore kernel reached the native hook and device-tree identity successfully.
- First restore stopped when the partial IPSW extractor omitted RestoreVersion.plist; this was a local test-asset issue. Root metadata and BootabilityBundle extraction were completed; the second blank restore succeeded, including system seal and snapshot creation. Cold boot and setup completed; the authorized account passed password and official MFA using the original trusted VM.
- On 26.6.2, iCloud Drive upload acknowledgement, eviction, download and byte comparison passed. The App Store downloaded Microsoft To Do 2.177 from the Australian storefront; its receipt exists, strict code-signature verification succeeded, and the app opened. Contacts, Calendar and Reminders read back the previous VM's own test records. Photos fetched the own 64-by-64 original and matched its SHA-256 against 15.6.1. These results are stored in `build/lab-25G83/service-results/`.
- On 26.6.2, Messages returned to its login page with IDS registration status 6009, matching the original account's 15.6.1 baseline. Per the user's instruction this is deferred; no message was sent. FaceTime delivery has not been independently verified on 26.6.2.
- On 26.6.2, Notes read the previous VM's exact test marker; Safari's official bookmark editor displayed the matching test title and URL. Passwords, unlocked with the guest-local password, displayed the own dummy entry synchronized from 15.6.1; its secret value was not compared. iCloud Mail sent one test message to the account's own mailbox and read the exact subject back from Inbox. No mail was sent to another person.
- macOS 27 blank restore completed successfully, including seal and snapshot creation. Cold boot recognizes the pinned kernel and reads the expected apsd process at the new layout. Password/MFA login and first setup completed. iCloud Drive upload acknowledgement, eviction, download and byte comparison passed. Contacts, Calendar, Reminders and Notes read the previous VM's own records. Photos fetched the own 64-by-64 original with the same SHA-256 as 15.6.1. Safari's official bookmark editor displayed the own matching title and URL. Passwords displayed the synchronized own dummy entry after local authentication; the secret value was not compared.
- On 27.2, Microsoft To Do 2.177 downloaded through the Australian App Store URL, had an installation receipt, passed strict code-signature verification and opened to its welcome/sign-in screen. No Microsoft account was used. iCloud Mail received the old test messages and completed a new self-addressed round trip. The first AppleScript send left the single own message in iCloud Drafts; opening that existing draft in Mail and pressing Command-Shift-D delivered it. The exact subject was then read from Inbox. The initial script result alone was not counted as a successful send.
- On 27.2, automatic IDS registration after account setup returned status 6009, matching the original account's 15.6.1 and 26.6.2 baseline. Per the user's instruction no further messaging login, message send or call was attempted. FaceTime delivery remains unverified.
- The first 27.2 MFA session returned an unknown error after a long automation delay. A fresh password/MFA session promptly transferred from the trusted VM completed login. The delay was a possible factor, not a proven cause of the first error.

## Runtime evidence matrix

Both columns below are independent installations from empty disks with different
synthetic identities. Cross-VM reads use only the previous test VM's own marker
`Henge-Cloud-20261111-AB2F40D2`; unrelated user records were not exported.

| Check | 26.6.2 / 25G83 | 27.2 / 26B123 |
| --- | --- | --- |
| Blank restore, cold boot, account password and trusted-VM MFA | Passed | Passed after fresh authentication session |
| Drive upload acknowledged, local eviction, cloud download, byte match | Passed | Passed |
| Contacts / Calendar / Reminders / Notes own record | Passed | Passed |
| Photos original 64-by-64 bytes and SHA-256 | Passed | Passed |
| Safari own bookmark title and URL | Passed | Passed |
| Passwords own dummy entry | Visible after local authentication | Visible after local authentication |
| iCloud Mail own new message send and receive | Passed | Passed using official Mail send action |
| App Store To Do 2.177 receipt, signature, launch | Passed | Passed |
| Original-account IDS registration | 6009, deferred | 6009, deferred |
| FaceTime recipient delivery | Unverified | Unverified |

Results are under `build/lab-{25G83,26B123}/service-results/`; the aggregate
record is `build/compat104-service-validation.json`. Settings switches alone
are not treated as successful synchronization. First-launch permissions and
Reminders initialization were completed before accepting the final reads.

## Test tooling and limits

The native CLI's pointer-position feedback was unreliable in 27.2. Testing
continued with ordinary virtual mouse events and cursor coordinates read inside
the guest through CGEvent. No Terminal Accessibility grant was needed. This
does not establish that every iOS input or rendering path works. Temporary
Terminal AppleEvents and Accessibility permission entries were reset in both
new guests after testing. MFA values were privately transferred, not written
to source or package artifacts; authentication-interval captures were removed.

Find My remote actions, Handoff, Continuity, Apple Pay and other hardware-dependent
services have not been established by these tests. Passwords metadata proves
the own entry synchronized, but its secret value has not been compared. There
is no claim that every iCloud feature or either messaging service passes.

## Test package

Build 104 is version 0.3.0, unsigned, with app ID `com.henge.icloudsoftware`.
It keeps PR #121's JIT reservation fix and the existing PR #100 media work.
The compatibility switch is only available when creating a new Mac; existing
machine identities are not replaced. Identity VMs use cold boots, without
Instant Resume. The macOS 15.6.1 Build 103 checkpoint remains unchanged.

IPA: `Henge-iCloud-macOS15-26-27-Build104-unsigned.ipa` in the main workspace's
`build` directory. SHA-256:
`ed5eba6caefd980378f1ae48a527986dc90a63549e1996834554cb53ca44ac9c`.
The source patch is based on PR #121 commit `3ff3bea` and includes no test
credentials, diagnostic VM profiles, disks or restore images.

The iOS installation toggle now permits only the three exact restore builds
24G90, 25G83 and 26B123. These results come from native macOS-hosted Henge
guests; the new iOS build still needs physical-device acceptance. Neither
static kernel checks nor successful iCloud synchronization establish iMessage
or FaceTime delivery.

No guest account credentials or real hardware identities are included here.
