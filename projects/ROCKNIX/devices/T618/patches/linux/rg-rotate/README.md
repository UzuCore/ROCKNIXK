# rg-rotate kernel support, rebased onto kernel.org Linux 7.1.2

Source: `beebono/linux-mainline-sprd` branch `rg-rotate`, pinned at
`464b3e7bf45bb300e190339977abba36c8078e1f` (Linux 7.1-rc1 + 225 commits: ums512/sharkl5pro
clocks, DPU/DSI panel, VBC/AGDSP audio, SC2355 Wi-Fi/BT, PMIC, DTS).

The 225 commits were rebased onto Linux 7.1.2, then consolidated into 11
subsystem patches. Each file belongs to one patch; intermediate edits are folded
into the final diff. Applying the original series and the consolidated series
produces byte-identical sources. The original authors, subjects and patch SHA256
hashes are recorded in `series-origin.json`.

ROCKNIX applies this directory before the numbered T618 board patches in the
parent directory. Those board patches remain separate, including the RG405V-only
MUSB quirk. Kernel version, device behavior and firmware payloads are unchanged.
