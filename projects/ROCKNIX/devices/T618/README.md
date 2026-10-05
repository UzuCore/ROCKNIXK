# T618 / RG405M + RG405V source integration candidate

Target: `PROJECT=ROCKNIX DEVICE=T618 ARCH=aarch64` in UzuCore/ROCKNIXK.
Kernel: kernel.org Linux 7.1.2 with the 225-commit
`beebono/linux-mainline-sprd` `rg-rotate` series at
`464b3e7bf45bb300e190339977abba36c8078e1f`, rebased onto 7.1.2.

This remains a source candidate, not a release. A limited RG405V runtime boot
has since been observed; its cross-axis joystick issue and full device
acceptance remain open. See `SOURCE_GATES.md` for the precise scope and
deployment constraints. The full project checklist is separate.

## Integrated source

- RG405M and RG405V DTS profiles, pinned UMS512 kernel and initramfs preparation; both
  device trees are included in one image. The installer selects the matching Android
  carrier from the detected model. The V tree uses its portrait panel, 5 Ah battery
  curve, USB switch GPIO and fan power/PWM nodes; GPIO15 belongs to the ADC mux.
- Processed ADC channel 2, mux GPIO36/37/15 and stock-derived analog arithmetic
  in `t618-input`; InputPlumber DualSense with UHID/HIDRAW/PlayStation enabled.
- Exact charger steps: 496 mA charge, 62 mA termination, 4200 mV regulation,
  33 mOhm sense. Failed/stale temperature inhibits the final CE write. The
  AW32257 input-limit setter is a no-op, not a proven 500 mA input clamp.
- Decoded DSP selectors, checked profile allocation/IPC, real SC2730 headset
  detection with stock EIC6/ADC20/eFuse bindings and UCM jack controls.
- Speaker-first UCM, inherited headphone/microphone DT routes, T618 UI-service
  and dynamically discovered CPU/GPU frequency and temperature interfaces.
- T618 RetroArch, Flycast, DuckStation and Mupen64Plus configuration paths;
  DSPerate/Yabasanshiro select the virtual-controller path.
- Android-v4 carriers using existing `dtb,extlinux` hooks. Common mkimage and
  SPL/U-Boot binaries are not modified by the preparation.

Relevant hardware nodes, eMMC and InputPlumber management are enabled in this
candidate. Older notes saying they are all disabled or AGDSP is missing are
obsolete. Source activation is not evidence of successful device operation.

## Private firmware

`firmware-manifest.json` records 16 outputs from the RG405M vendor/stock
references and read-only RG405V GammaOS v1.5 firmware extracts. The importer
verifies original hashes, relevant AVB payload boundaries and final hashes
before writing. Redistribution of firmware or device calibration is not approved.

From the repository root, verify without writing:

```sh
T=projects/ROCKNIX/devices/T618
python3 -B "$T/packages/t618-firmware/import_firmware.py" \
  --manifest "$T/firmware-manifest.json" \
  --vendor-root /path/to/extracted/vendor \
  --stock-partitions /path/to/extracted/stock/partitions --verify-only
```

For private staging, replace `--verify-only` with
`--destination /path/to/private-firmware`; set `T618_PRIVATE_FIRMWARE_DIR` to it.
Never commit these bytes or individual-device backups.

## Validation boundary

The RG405V V1.23 reference under
`02_device_reference/Stock_RG405V_Unbricker` was inspected. Its vendor boot
contains the generic UMS512 SoC DT; DTBO entry 1 supplies the 480x640, 25.6 MHz,
two-lane panel timing and reset GPIO 50. These values match the RG405V DTS.

The V DTS was preprocessed and compiled against the pinned kernel source. A V
carrier set was generated from the existing release KERNEL, whose SHA256 matches
the shared SD image and RG405M carrier. Android-v4 layout, partition sizes,
embedded kernel/DTB hashes, and AVB signatures were verified. The distribution
manifest now maps RG405M and RG405V to separate carrier profiles while retaining
the same SD image and update tar.

## RG405V runtime check (2026-10-05)

The RG405V booted ROCKNIX and reported `7.1.2-rocknixk-t618`. A live SSH check
identified the model and confirmed Wi-Fi plus gateway and external network
connectivity. InputPlumber, Sway, PipeWire and WirePlumber were active, with no
failed systemd units. A prior physical input capture on this device recorded
all 16 non-analog buttons. The kernel accepted and stopped a Linux force-feedback
rumble effect. This confirms the software event path; physical vibration was not
independently sensed.

The display connector reports connected and the backlight is enabled. Remote
status cannot confirm visible pixels or audible sound; the audio stream was
active but not independently heard. Cross-axis joystick input remains faulty
and unresolved; see `JOYSTICK-INVESTIGATION-KR.md`.

This runtime check did not identify the exact image hash or installation path
that produced the running kernel. The local audit did not rebuild or flash an
image. `hardware_tested` remains false for full release acceptance while the
joystick issue and physical display/audio confirmation remain open.

Portable helper tests are in `tests/test_prebuild.py`. Their checked-in presence
does not mean they were executed; consult the separate development-workspace
report for local prebuild format and runtime results.

Read `packages/u-boot/INSTALL.md` and `BOOT_REQUIREMENTS.txt` before interpreting
carrier artifacts. The selected source path uses a future inactive Android slot
and SD rootfs;
unchanged-stock SD-only entry and SD-removal rollback are not supplied by it.
