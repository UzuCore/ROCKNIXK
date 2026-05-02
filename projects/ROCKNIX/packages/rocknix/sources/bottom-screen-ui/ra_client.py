# SPDX-License-Identifier: GPL-2.0-or-later
# RetroArch Network Control Interface client (UDP).
# See: https://docs.libretro.com/development/retroarch/network-control-interface/

import socket


class RAClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 55355, timeout: float = 0.25):
        self._addr = (host, port)
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(timeout)

    def _send(self, cmd: str) -> None:
        self._sock.sendto(cmd.encode("ascii"), self._addr)

    def _query(self, cmd: str) -> str | None:
        self._send(cmd)
        try:
            data, _ = self._sock.recvfrom(1024)
            return data.decode("ascii", errors="replace").strip()
        except socket.timeout:
            return None

    def menu_toggle(self) -> None:
        self._send("MENU_TOGGLE")

    def save_state(self) -> None:
        self._send("SAVE_STATE")

    def load_state(self) -> None:
        self._send("LOAD_STATE")

    def get_config_param(self, param: str) -> str | None:
        res = self._query(f"GET_CONFIG_PARAM {param}")
        if res and res.startswith("GET_CONFIG_PARAM"):
            parts = res.split(" ", 2)
            if len(parts) > 2:
                return parts[2].strip()
        return None

    def get_savestate_directory(self) -> str | None:
        return self.get_config_param("savestate_directory")

    def get_menu_active(self) -> bool | None:
        res = self.get_config_param("menu_active")
        if res is None:
            return None
        return res.strip().lower() == "true"
