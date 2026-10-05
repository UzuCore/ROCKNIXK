# Core source-gate results — 2026-09-27 / prebuild4

The four previously listed issues now have source implementations and focused
validation. This does not mean the entire distribution has been built, the
full original project checklist is closed, or the handheld has been tested.

| Item | Source result | Remaining execution boundary |
|---|---|---|
| BOOT-01 | Android-v4 inactive-slot entry and SD SYSTEM/STORAGE path checked against the original vendor C parser; malformed layouts rejected. An RG405V runtime boot on `7.1.2-rocknixk-t618` has since been observed. | The running image hash and exact installation path are not recorded here; initial eMMC installation remains a separate reviewed action |
| AUDIO-01 | Vendor ARM32 HAL and XML jointly identify speaker 0x394b0000/headphones 0x38460000; ALSA range, bounds, IPC return handling and UCM fixed | Playback and DSP IPC on the actual unit remain untested |
| AUDIO-02 | Real pinned SC2730 detection driver replaces the stub; original GPIO/ADC/eFuse bindings, mainline resource handling and UCM jack controls connected | Actual insertion/removal/button/suspend tests remain unperformed |
| POWER-01 | Temperature fault/stale/suspend/shutdown state gates every final CE write; readback failures are errors, with bounded recovery hysteresis | Electrical state during bus failure and charger watchdog/suspend behavior must be measured |

## Boot-policy correction

The test boot confirms that ROCKNIX can run on this RG405V, but it does not prove
that unchanged-stock SD-only boot or SD-removal rollback is available. Keep the
installation route and recovery behavior tied to the reviewed carrier and the
specific device; do not infer them from the runtime boot alone.

Detailed source provenance and modifications are in
`packages/t618-kernel-integration/SOURCE.md`. The boot path and limitations are in
`packages/u-boot/BOOT_PATH.md`. Reports are under local `08_build/prebuild4`.

The current repository source was not rebuilt during the 2026-10-05 runtime
audit, and the running image hash was not captured. The audit verified boot,
network and service state; joystick cross-axis behavior, visible display output,
audible playback, physical vibration, charger behavior and the full install/recovery
flow remain outside that check. No device write or system-setting change was
performed during the audit. Source C was checked against pinned kernel headers;
small temporary host test libraries exercise extracted policy/parser functions
only and were removed after testing.
