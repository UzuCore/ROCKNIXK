# SPDX-License-Identifier: GPL-2.0-or-later
# Screen brightness control via Linux backlight sysfs.

import sys
from pathlib import Path

BACKLIGHT_ROOT = Path("/sys/class/backlight")


class _Device:
    __slots__ = ("path", "max")

    def __init__(self, path: Path, max_value: int):
        self.path = path
        self.max = max_value


class BrightnessControl:
    def __init__(self):
        self._devices = self._discover()

    @staticmethod
    def _discover() -> list[_Device]:
        if not BACKLIGHT_ROOT.is_dir():
            return []
        devs: list[_Device] = []
        for sub in sorted(BACKLIGHT_ROOT.iterdir()):
            bp = sub / "brightness"
            mp = sub / "max_brightness"
            if not (bp.exists() and mp.exists()):
                continue
            try:
                max_v = int(mp.read_text().strip())
            except (OSError, ValueError):
                continue
            if max_v <= 0:
                continue
            devs.append(_Device(bp, max_v))
        return devs

    @property
    def available(self) -> bool:
        return bool(self._devices)

    def get_percent(self) -> int | None:
        if not self._devices:
            return None
        d = self._devices[0]
        try:
            cur = int(d.path.read_text().strip())
        except (OSError, ValueError):
            return None
        pct = round(cur * 100.0 / d.max)
        return max(1, min(100, pct))

    def set_percent(self, percent: int) -> None:
        pct = max(1, min(100, int(percent)))
        for d in self._devices:
            target = int(pct / 100.0 * d.max + 0.5)
            target = max(1, min(d.max, target))
            try:
                d.path.write_text(f"{target}\n")
            except OSError as e:
                print(f"warning: brightness write {d.path} failed: {e}", file=sys.stderr)


class BottomPanelBacklight:
    def __init__(self, node_name: str):
        self.available = False
        self._brightness_path: Path | None = None
        self._max = 0
        if not node_name:
            return
        path = BACKLIGHT_ROOT / node_name
        bp = path / "brightness"
        mp = path / "max_brightness"
        if not (bp.exists() and mp.exists()):
            return
        try:
            self._max = int(mp.read_text().strip())
        except (OSError, ValueError):
            return
        if self._max <= 0:
            return
        self._brightness_path = bp
        self.available = True

    def power_off(self) -> None:
        self._write(0)

    def restore(self, percent: int) -> None:
        pct = max(1, min(100, int(percent)))
        target = int(pct / 100.0 * self._max + 0.5)
        target = max(1, min(self._max, target))
        self._write(target)

    def _write(self, value: int) -> None:
        if not self.available or self._brightness_path is None:
            return
        try:
            self._brightness_path.write_text(f"{value}\n")
        except OSError as e:
            print(f"warning: bottom backlight write failed: {e}", file=sys.stderr)
