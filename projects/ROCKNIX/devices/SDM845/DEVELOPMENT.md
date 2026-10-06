# Odin checkpoint — 2026-10-07

## Changes

- Preserve stock ROCKNIX MOTD output; restore the original console font and banner once when the Odin DRM framebuffer registers. Keep parallel Sway startup.
- Select `fbcon=rotate:3` for Odin M2 in normal and recovery GRUB entries.
- Report the Odin D-pad through standard hat axes and match InputPlumber mappings.
- Include SDM845 in emulator system registration, including Eden, Dolphin and Cemu.
- Add RetroArch's v4l-utils dependency and include initramfs inputs in the kernel build stamp.

## Verification

On Odin M2, a normal boot without a local udev override captured the DRM framebuffer before Sway: MOTD began at row 35 of 45 and Loading appeared at row 44. Restoration ran at 5.06 seconds, Sway at 5.49 seconds and EmulationStation at 12.10 seconds. A further boot after removing the observer and temporary configuration reached active Sway and EmulationStation.

SYSTEM was applied to the existing SD and its SHA-256 verified. KERNEL, GRUB and the M2 DTB were unchanged during this application. This was not a fresh full-disk installation or an M0 hardware test. Framebuffer captures are memory captures, not camera recordings. Emulator registration does not establish that every emulator has passed gameplay testing.

## Local artifacts

Files are under `D:\rxodin\build\image`; detailed evidence is under `D:\rxodin\analysis\motd-console-restore-20261006` and `D:\rxodin\analysis\motd-stock-20261006\boot-proof`.

| Artifact | SHA-256 |
|---|---|
| ROCKNIX-SDM845.aarch64-20261006-motd-console.img.gz | `cf4a412e26366667a7594d5561af1a026e6eb5ded188536ca664f11659dc01eb` |
| ROCKNIX-SDM845.aarch64-20261006-motd-console.tar | `d87703b9c20d1a3d0ac1f0ac7ea5f06747cbf4a2632969568c0d44886e558266` |
| SYSTEM | `c1d4c39193c75bf74ba2fc4032a6ce2a2efb498daa487c69927115719b3122b1` |
| KERNEL | `99f244fc41dc80dc4ca888a84b51071b508bbcd9f7801cdd031cfd7c0b62fb89` |

These artifacts were built before synchronizing the three newer remote `odin` commits for this checkpoint.
