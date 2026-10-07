# T618 branch consolidation — 2026-10-07

Base: `origin/dev` at `fa112970e6`. Previous T618 tip:
`9e2e58add9bb576e506b88cbdee948e5f4e1bd0b`.
The feature is one commit on that base; dev is not updated.
Local recovery ref: `archive/t618-before-cleanup-20261007`.

## Device isolation

- Kernel source, integration, initramfs guard and Cairo version changes remain
  conditional on T618. Other device kernel recipes and configurations are untouched.
- Dolphin's new fullscreen patch lives in `devices/T618/patches/dolphin-sa`.
  The standard unpack script applies it after the existing shared patches.
- ARMSX2's ScummVM font dependency and bundled Korean font link apply only to T618.
- USB debug ACM is opt-in through the T618 device profile. Other devices keep their
  existing disabled/host behavior. USB readiness, teardown and failure handling
  remain shared fixes.
- RG405V's MUSB short-RX quirk remains conditional on its DT property; RG405M
  keeps its existing kernel, USB, input and Wi-Fi configuration.
- Unrelated SDM845 Qt/Eden workflow additions were removed: this tree has no
  corresponding device/emulator definition. Existing supported target lists remain.

## Restored dev changes

- Restored the GPT entry-table expansion and inclusive partition-end calculation
  in `installtointernal`.
- Restored the gptfdisk device-pointer lifetime fix.
- Restored dev's empty addon template; it is a tracked template, not a build artifact.

## Shared fixes retained

- Targeted config recovery avoids replacing Wi-Fi settings when optional RetroArch
  core options are absent.
- Internal-battery discovery handles driver names and skips peripheral batteries.
  The preferred-name and fallback searches now share one loop.
- Device strings use BusyBox-compatible `tr`; EmulationStation's config symlink
  points to the runtime path rather than the build install directory.
- Mupen64Plus ZIP discovery supports BusyBox unzip; EasyRPG retains pinned hash
  checks and failed-download retry cleanup.
- ARAM retains Go version discovery with one candidate list; ARM library lookup
  honors the configured build root and suffix.
- Existing archive checksums remain pinned. CI waits for the ARM artifacts consumed
  by aarch64 builds; the ARM producer, consumer and skip conditions match.
- Duplicate USB service error handling and redundant comments were simplified.

## Patch consolidation and review scope

The 225 imported Linux commits are folded into 11 patches grouped by subsystem.
The five board patches remain separate. Original patch hashes and author information
are retained in `patches/linux/rg-rotate/series-origin.json`.

Source review used the existing Linux 7.1.2 archive and only its affected files.
The original and consolidated series produce byte-identical final sources; the
consolidated patches were also applied with GNU `patch -p1`, as used by ROCKNIX.
Two review passes covered the shared diff, device guards and patch loading order.
Firmware payloads, kernel configuration and vendor integration sources are unchanged.

No new full build or hardware test was run for this cleanup. Existing runtime
validation remains historical evidence, not validation of a newly built image.
