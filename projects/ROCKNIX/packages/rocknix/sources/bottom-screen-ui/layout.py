# SPDX-License-Identifier: GPL-2.0-or-later
# Pure-data layout: no pygame imports so this stays unit-testable.

from dataclasses import dataclass


@dataclass
class Rect:
    x: int
    y: int
    w: int
    h: int

    def contains(self, px: int, py: int) -> bool:
        return self.x <= px < self.x + self.w and self.y <= py < self.y + self.h


@dataclass
class Button:
    rect: Rect
    label: str
    action: str
    icon: str = ""


@dataclass
class Layout:
    brightness_rect: Rect
    date_rect: Rect
    image_rect: Rect
    load_label_rect: Rect
    buttons: list[Button]

    def hit_test(self, px: int, py: int) -> Button | None:
        for b in self.buttons:
            if b.rect.contains(px, py):
                return b
        return None


def build(w: int, h: int) -> Layout:
    """Layout:
       top strip:                Brightness slider (full width)
       left column, top half:    Resume
       left column, bottom half: Create Restore Point (save)
       right column (full height under top strip, single big button): date strip → savestate screenshot → icon+label
    """
    pad = max(8, w // 60)

    brightness_h = max(40, h // 12)
    brightness = Rect(pad, pad, w - 2 * pad, brightness_h)

    grid_top = pad * 2 + brightness_h
    grid_h = h - grid_top - pad

    cell_w = (w - 3 * pad) // 2
    cell_h = (grid_h - pad) // 2

    resume = Button(Rect(pad, grid_top, cell_w, cell_h), "Resume Game", "resume", icon="resume",)
    save = Button(Rect(pad, grid_top + pad + cell_h, cell_w, cell_h), "Create\nRestore Point", "save", icon="save",)

    load_x = pad * 2 + cell_w
    load_y = grid_top
    load_w = cell_w
    load_h = grid_h
    load = Button(Rect(load_x, load_y, load_w, load_h), "Load\nRestore Point", "load", icon="",)

    date_h = max(24, load_h // 12)
    label_h = max(48, load_h // 4)
    image_pad_x = pad
    date = Rect(load_x, load_y, load_w, date_h)
    image = Rect(load_x + image_pad_x, load_y + date_h, load_w - 2 * image_pad_x, load_h - date_h - label_h)
    load_label = Rect(load_x, load_y + load_h - label_h, load_w, label_h)

    return Layout(
        brightness_rect=brightness,
        date_rect=date,
        image_rect=image,
        load_label_rect=load_label,
        buttons=[resume, save, load],
    )
