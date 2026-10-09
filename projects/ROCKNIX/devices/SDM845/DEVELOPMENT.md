# Odin checkpoint — 2026-10-07

## Changes

- Preserve stock ROCKNIX MOTD output; restore the original console font and banner once when the Odin DRM framebuffer registers. Keep parallel Sway startup.
- Select `fbcon=rotate:3` for Odin M2 in normal and recovery GRUB entries.
- Report the Odin D-pad through standard hat axes and match InputPlumber mappings.
- Include SDM845 in emulator system registration, including Eden, Dolphin and Cemu.
- Add RetroArch's v4l-utils dependency and track device.init through the existing BusyBox/initramfs dependency stamps.
- Remove the Odin serial notice opt-in; clear its generated profile and notice marker on boot for existing installations.

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

These artifacts were built before synchronizing the three newer remote `sdm845` commits and before removing the Odin serial notice opt-in. Rebuild to include that removal and the subsequent cleanup and lib32 cache fix.

## Integration cleanup - 2026-10-07

- Keep device drivers, tools, runtime services and MOTD restoration under SDM845; reuse existing emulator configuration directories through symlinks.
- Store the M2 console rotation in the device XML command line, shared by normal and recovery entries. Apply Odin GRUB timeout settings only when building SDM845; other devices keep the previous timeout output.
- Remove the unused splash override hook and the Odin-only initramfs copy/check block from the kernel recipe. BusyBox stages device.init; BusyBox and initramfs track it for SDM845 only, and the kernel inherits initramfs inputs.
- Remove the ineffective FEX Nix pin that was immediately overwritten by the existing pin.
- Invalidate the SDM845 lib32 bundle when its ARM library inputs change. An older cached bundle prevented retroarch32 from starting; applying matching libraries and rebuilding the loader cache restored Tekken 3 on the test M2.
- Include SDM845 in the existing GitHub Qt, Eden and LLVM artifact conditions. These workflow changes only add SDM845 to the Odin artifact conditions and have not been exercised by a GitHub build.

The PS1 repair on the test device uses a temporary, hash-conditional library mount outside the source tree. Do not ship that service; rebuilt images must contain the correct lib32 bundle. The current image artifacts above predate this repair. Other device cache inputs and generated boot command lines retain their previous values. This cleanup has not been rebuilt or boot-tested.

The dev branch merged the SDM845 branch and then reverted that merge (`478eb2be3b`, `acde8ca134`). Commit ancestry alone therefore omits reverted Odin changes. Review the final tree when integrating; retain upstream changes already in dev. The shared BIOS downloader and RetroArch DualShock default already belong to dev. Keep SDM845 workflow selection policy separate from device runtime review.
