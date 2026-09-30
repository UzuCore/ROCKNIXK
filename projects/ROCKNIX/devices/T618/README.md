# T618 / RG405M source integration candidate

Target: `PROJECT=ROCKNIX DEVICE=T618 ARCH=aarch64` in UzuCore/ROCKNIXK.
Kernel: `beebono/linux-mainline-sprd` at
`464b3e7bf45bb300e190339977abba36c8078e1f` (`7.1.0-rc1`).

This is not a hardware-tested port or release. The four core source issues
were implemented and validated in prebuild4; see `SOURCE_GATES.md` for the precise
scope and deployment constraints. The full project checklist is separate.

## Integrated source

- RG405M DTS, pinned UMS512 kernel and initramfs preparation; 40 stock panel
  register writes retained. GPIO15 belongs to the ADC mux, not the LCD supply.
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

`firmware-manifest.json` records twelve outputs from already extracted GammaOS
Next RG405M v1.1 vendor files and stock V1.15 partitions. The importer verifies
original hashes, relevant AVB payload boundaries and final hashes before writing.
This does not authorize redistribution of firmware or device calibration.

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

No `make`, kernel Image/module compilation, device access, flashing or release
was performed in this iteration. Existing Kconfig `conf`, GCC preprocessing
and DTC were run directly. Carrier tests used a pre-existing reference kernel.

Portable helper tests are in `tests/test_prebuild.py`. Their checked-in presence
does not mean they were executed; consult the local report. Authoritative local
checks are under `D:\rg405m\08_build\prebuild4`, with retained format/runtime
results under `prebuild2`. Read `D:\rg405m\HANDOFF_2026-09-27_T618.md` to continue.

Read `packages/u-boot/INSTALL.md` and `BOOT_REQUIREMENTS.txt` before interpreting
carrier artifacts. The selected source path uses a future inactive Android slot
and SD rootfs;
unchanged-stock SD-only entry and SD-removal rollback are not supplied by it.
