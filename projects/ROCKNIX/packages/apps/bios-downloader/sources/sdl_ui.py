# SPDX-License-Identifier: GPL-2.0-or-later
import ctypes as C
import ctypes.util
from pathlib import Path
import struct
import zlib


class Rect(C.Structure):
    _fields_ = [(name, C.c_int) for name in ('x', 'y', 'w', 'h')]


class Color(C.Structure):
    _fields_ = [(name, C.c_uint8) for name in ('r', 'g', 'b', 'a')]


class Event(C.Union):
    _fields_ = [('type', C.c_uint32), ('data', C.c_uint8 * 56), ('alignment', C.c_uint64)]


def bind(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = list(arguments)
    return function


class Screen:
    def __init__(self, size=None, font=None):
        self.sdl = C.CDLL(ctypes.util.find_library('SDL2') or 'libSDL2-2.0.so.0')
        self.ttf = C.CDLL(ctypes.util.find_library('SDL2_ttf') or 'libSDL2_ttf-2.0.so.0')
        self.window = self.renderer = None
        self.fonts = {}
        self.textures = {}
        self.controllers = {}
        self.window_size = (0, 0)
        self.size = (0, 0)
        self._bind()
        self.sdl.SDL_SetHint(b'SDL_VIDEO_WAYLAND_WMCLASS', b'rocknixk-bios')
        self.sdl.SDL_SetHint(b'SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS', b'1')
        self.sdl.SDL_SetHint(b'SDL_TOUCH_MOUSE_EVENTS', b'0')
        self.sdl.SDL_SetHint(b'SDL_MOUSE_TOUCH_EVENTS', b'0')
        if self.sdl.SDL_Init(0x20 | 0x2000) < 0:
            raise RuntimeError(self.error())
        if self.ttf.TTF_Init() < 0:
            raise RuntimeError(self.error())
        candidates = [font] if font else []
        candidates += [
            '/usr/share/fonts/truetype/noto-cjk/NotoSansCJKsc-Regular.otf',
            '/usr/share/fonts/truetype/NanumGothicCoding.ttf',
            '/usr/share/fonts/truetype/nanumgothic/NanumGothic.ttf',
            '/usr/share/fonts/truetype/nanum/NanumGothic.ttf',
            '/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf',
            '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
        ]
        self.font_path = next((Path(p) for p in candidates if p and Path(p).is_file()), None)
        if self.font_path is None:
            raise RuntimeError('No UI font found')
        width, height = size or (640, 480)
        flags = 0x20 | 0x2000 | (0 if size else 0x1001)
        self.window = self.sdl.SDL_CreateWindow(b'ROCKNIXK BIOS Downloader', 0x2FFF0000, 0x2FFF0000,
                                               width, height, flags)
        if not self.window:
            raise RuntimeError(self.error())
        self.renderer = self.sdl.SDL_CreateRenderer(self.window, -1, 2)
        if not self.renderer:
            self.renderer = self.sdl.SDL_CreateRenderer(self.window, -1, 1)
        if not self.renderer:
            raise RuntimeError(self.error())
        self.refresh_size()
        self.open_controllers()

    def _bind(self):
        void, integer, pointer = None, C.c_int, C.c_void_p
        signatures = {
            'SDL_Init': (integer, C.c_uint32), 'SDL_Quit': (void,),
            'SDL_SetHint': (integer, C.c_char_p, C.c_char_p), 'SDL_GetError': (C.c_char_p,),
            'SDL_CreateWindow': (pointer, C.c_char_p, integer, integer, integer, integer, C.c_uint32),
            'SDL_DestroyWindow': (void, pointer),
            'SDL_GetWindowSize': (void, pointer, C.POINTER(integer), C.POINTER(integer)),
            'SDL_CreateRenderer': (pointer, pointer, integer, C.c_uint32),
            'SDL_DestroyRenderer': (void, pointer),
            'SDL_GetRendererOutputSize': (integer, pointer, C.POINTER(integer), C.POINTER(integer)),
            'SDL_SetRenderDrawColor': (integer, pointer, C.c_uint8, C.c_uint8, C.c_uint8, C.c_uint8),
            'SDL_RenderClear': (integer, pointer), 'SDL_RenderPresent': (void, pointer),
            'SDL_RenderFillRect': (integer, pointer, C.POINTER(Rect)),
            'SDL_RenderDrawRect': (integer, pointer, C.POINTER(Rect)),
            'SDL_RenderCopy': (integer, pointer, pointer, C.POINTER(Rect), C.POINTER(Rect)),
            'SDL_CreateTextureFromSurface': (pointer, pointer, pointer),
            'SDL_QueryTexture': (integer, pointer, pointer, pointer, C.POINTER(integer), C.POINTER(integer)),
            'SDL_DestroyTexture': (void, pointer), 'SDL_FreeSurface': (void, pointer),
            'SDL_PollEvent': (integer, C.POINTER(Event)), 'SDL_PushEvent': (integer, C.POINTER(Event)),
            'SDL_NumJoysticks': (integer,), 'SDL_IsGameController': (integer, integer),
            'SDL_GameControllerOpen': (pointer, integer), 'SDL_GameControllerClose': (void, pointer),
            'SDL_GameControllerGetJoystick': (pointer, pointer),
            'SDL_JoystickInstanceID': (integer, pointer),
            'SDL_RenderReadPixels': (integer, pointer, C.POINTER(Rect), C.c_uint32, pointer, integer),
        }
        for name, signature in signatures.items():
            bind(self.sdl, name, *signature)
        for name, signature in {
            'TTF_Init': (integer,), 'TTF_Quit': (void,),
            'TTF_OpenFont': (pointer, C.c_char_p, integer), 'TTF_CloseFont': (void, pointer),
            'TTF_SizeUTF8': (integer, pointer, C.c_char_p, C.POINTER(integer), C.POINTER(integer)),
            'TTF_RenderUTF8_Blended': (pointer, pointer, C.c_char_p, Color),
        }.items():
            bind(self.ttf, name, *signature)

    def error(self):
        return self.sdl.SDL_GetError().decode('utf-8', errors='replace')

    def refresh_size(self):
        width, height = C.c_int(), C.c_int()
        if self.sdl.SDL_GetRendererOutputSize(self.renderer, C.byref(width), C.byref(height)) < 0:
            raise RuntimeError(self.error())
        self.size = (width.value, height.value)
        self.sdl.SDL_GetWindowSize(self.window, C.byref(width), C.byref(height))
        self.window_size = (width.value, height.value)
        return self.size

    def open_controllers(self):
        for index in range(self.sdl.SDL_NumJoysticks()):
            if self.sdl.SDL_IsGameController(index):
                controller = self.sdl.SDL_GameControllerOpen(index)
                if not controller:
                    continue
                identifier = self.sdl.SDL_JoystickInstanceID(self.sdl.SDL_GameControllerGetJoystick(controller))
                if identifier in self.controllers:
                    self.sdl.SDL_GameControllerClose(controller)
                else:
                    self.controllers[identifier] = controller

    def font(self, pixels):
        pixels = max(10, round(pixels))
        if pixels not in self.fonts:
            font = self.ttf.TTF_OpenFont(str(self.font_path).encode(), pixels)
            if not font:
                raise RuntimeError(self.error())
            self.fonts[pixels] = font
        return self.fonts[pixels]

    def measure(self, text, pixels):
        width, height = C.c_int(), C.c_int()
        if self.ttf.TTF_SizeUTF8(self.font(pixels), text.encode(), C.byref(width), C.byref(height)) < 0:
            raise RuntimeError(self.error())
        return width.value, height.value

    def fit(self, text, pixels, width):
        if self.measure(text, pixels)[0] <= width:
            return text
        while text and self.measure(text + '…', pixels)[0] > width:
            text = text[:-1]
        return text + '…' if text else ''

    def wrap(self, text, pixels, width):
        lines = []
        current = ''
        for character in text:
            if character == '\n' or (current and self.measure(current + character, pixels)[0] > width):
                lines.append(current.rstrip())
                current = '' if character == '\n' else character.lstrip()
            else:
                current += character
        if current:
            lines.append(current.rstrip())
        return lines

    def color(self, color):
        return tuple(bytes.fromhex(color.lstrip('#'))) + (255,)

    def clear(self, color):
        self.sdl.SDL_SetRenderDrawColor(self.renderer, *self.color(color))
        self.sdl.SDL_RenderClear(self.renderer)

    def box(self, rect, color, border=False):
        self.sdl.SDL_SetRenderDrawColor(self.renderer, *self.color(color))
        function = self.sdl.SDL_RenderDrawRect if border else self.sdl.SDL_RenderFillRect
        function(self.renderer, C.byref(Rect(*map(round, rect))))

    def text(self, text, pixels, color, x, y, width, centered=False):
        text = self.fit(text, pixels, width)
        if not text:
            return
        key = (text, round(pixels), color)
        if key not in self.textures:
            if len(self.textures) >= 120:
                self.clear_textures()
            surface = self.ttf.TTF_RenderUTF8_Blended(self.font(pixels), text.encode(), Color(*self.color(color)))
            if not surface:
                raise RuntimeError(self.error())
            texture = self.sdl.SDL_CreateTextureFromSurface(self.renderer, surface)
            self.sdl.SDL_FreeSurface(surface)
            if not texture:
                raise RuntimeError(self.error())
            tw, th = C.c_int(), C.c_int()
            self.sdl.SDL_QueryTexture(texture, None, None, C.byref(tw), C.byref(th))
            self.textures[key] = (texture, tw.value, th.value)
        texture, tw, th = self.textures[key]
        if centered:
            x += (width - tw) / 2
        self.sdl.SDL_RenderCopy(self.renderer, texture, None, C.byref(Rect(round(x), round(y), tw, th)))

    def events(self):
        event = Event()
        while self.sdl.SDL_PollEvent(C.byref(event)):
            yield event.type, bytes(event.data)

    def present(self):
        self.sdl.SDL_RenderPresent(self.renderer)

    def screenshot(self, path):
        width, height = self.size
        pixels = (C.c_uint8 * (width * height * 4))()
        if self.sdl.SDL_RenderReadPixels(self.renderer, None, 0x16762004, pixels, width * 4) < 0:
            raise RuntimeError(self.error())
        raw = bytes(pixels)
        rows = b''.join(b'\0' + raw[offset:offset + width * 4]
                        for offset in range(0, len(raw), width * 4))

        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

        Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                               + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

    def clear_textures(self):
        for texture, _, _ in self.textures.values():
            self.sdl.SDL_DestroyTexture(texture)
        self.textures.clear()

    def close(self):
        self.clear_textures()
        for font in self.fonts.values():
            self.ttf.TTF_CloseFont(font)
        for controller in self.controllers.values():
            self.sdl.SDL_GameControllerClose(controller)
        if self.renderer:
            self.sdl.SDL_DestroyRenderer(self.renderer)
        if self.window:
            self.sdl.SDL_DestroyWindow(self.window)
        self.ttf.TTF_Quit()
        self.sdl.SDL_Quit()
