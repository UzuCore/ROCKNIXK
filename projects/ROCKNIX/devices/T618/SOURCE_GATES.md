# Core source-gate results — 2026-09-27 / prebuild4

The four previously listed issues now have source implementations and focused
validation. This does not mean the entire distribution has been built, the
full original project checklist is closed, or the handheld has been tested.

| Item | Source result | Remaining execution boundary |
|---|---|---|
| BOOT-01 | Android-v4 inactive-slot entry and SD SYSTEM/STORAGE path checked against the original vendor C parser; malformed layouts rejected | Initial installation requires separate device-write authorization; a real boot has not been attempted |
| AUDIO-01 | Vendor ARM32 HAL and XML jointly identify speaker 0x394b0000/headphones 0x38460000; ALSA range, bounds, IPC return handling and UCM fixed | Playback and DSP IPC on the actual unit remain untested |
| AUDIO-02 | Real pinned SC2730 detection driver replaces the stub; original GPIO/ADC/eFuse bindings, mainline resource handling and UCM jack controls connected | Actual insertion/removal/button/suspend tests remain unperformed |
| POWER-01 | Temperature fault/stale/suspend/shutdown state gates every final CE write; readback failures are errors, with bounded recovery hysteresis | Electrical state during bus failure and charger watchdog/suspend behavior must be measured |

## Boot-policy correction

Earlier assistant-generated notes incorrectly treated permanent, unchanged-stock
SD-only boot and SD-removal rollback as mandatory user requirements. The user's
explicit current boundary is preparation before `make`, without flashing or
device writes. The implemented inactive-slot route respects that preparation
boundary; it does not authorize its future installation and does not provide
unchanged-stock SD-only boot. The older proposed policy remains documented rather
than being checked off as implemented.

Detailed source provenance and modifications are in
`packages/t618-kernel-integration/SOURCE.md`. The boot path and limitations are in
`packages/u-boot/BOOT_PATH.md`. Reports are under local `08_build/prebuild4`.

No full kernel/module/bootloader/distro build, hardware test, device write,
system-setting change or release was performed. Actual source C was checked
against pinned kernel headers; small temporary host test libraries exercise
extracted policy/parser functions only and were removed after testing.
