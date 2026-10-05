# rg-rotate kernel support, rebased onto kernel.org Linux 7.1.2

Source: `beebono/linux-mainline-sprd` branch `rg-rotate`, pinned at
`464b3e7bf45bb300e190339977abba36c8078e1f` (Linux 7.1-rc1 + 225 commits: ums512/sharkl5pro
clocks, DPU/DSI panel, VBC/AGDSP audio, SC2355 Wi-Fi/BT, PMIC, DTS).

Rebased with `git rebase --onto <7.1.2> v7.1-rc1 464b3e7` (all 225 applied without conflicts;
5 files touched by both 7.1.2 and the fork merged cleanly). Exported with
`git format-patch --zero-commit`. Applied before the numbered T618 patches in the parent directory.
