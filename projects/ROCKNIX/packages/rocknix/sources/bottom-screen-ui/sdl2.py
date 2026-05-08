# SPDX-License-Identifier: GPL-2.0-or-later
# Minimal ctypes wrapper around the libSDL2 / libSDL2_image / libSDL2_ttf
#
# Only the symbols the bottom-screen UI actually calls are bound. Stick to the
# subset listed below — anything else, add a one-line ctypes binding here
# rather than scattering ctypes calls across main.py.

import ctypes
from ctypes import (
    CDLL, POINTER, Structure,
    c_char_p, c_float, c_int, c_int32, c_int64,
    c_uint8, c_uint32, c_void_p,
)

# ---------- libraries ----------
_sdl  = CDLL("libSDL2-2.0.so.0")
_img  = CDLL("libSDL2_image-2.0.so.0")
_ttf  = CDLL("libSDL2_ttf-2.0.so.0")

# ---------- constants ----------
INIT_VIDEO              = 0x00000020
INIT_EVENTS             = 0x00004000

WINDOW_FULLSCREEN_DESKTOP = 0x00001001
WINDOW_BORDERLESS         = 0x00000010

WINDOWPOS_UNDEFINED       = 0x1FFF0000
RENDERER_ACCELERATED      = 0x00000002
RENDERER_PRESENTVSYNC     = 0x00000004

# SDL_BlendMode
BLENDMODE_BLEND           = 1

EVENT_QUIT                = 0x100
EVENT_FINGERDOWN          = 0x700
EVENT_FINGERUP            = 0x701
EVENT_FINGERMOTION        = 0x702
EVENT_MOUSEMOTION         = 0x400
EVENT_MOUSEBUTTONDOWN     = 0x401
EVENT_MOUSEBUTTONUP       = 0x402

IMG_INIT_JPG              = 0x00000001
IMG_INIT_PNG              = 0x00000002

# Hints (for Wayland app_id)
HINT_APP_NAME             = b"SDL_APP_NAME"
HINT_VIDEO_WAYLAND_WMCLASS = b"SDL_VIDEO_WAYLAND_WMCLASS"


# ---------- structs ----------
class Rect(Structure):
    _fields_ = [("x", c_int), ("y", c_int), ("w", c_int), ("h", c_int)]


class Color(Structure):
    _fields_ = [("r", c_uint8), ("g", c_uint8), ("b", c_uint8), ("a", c_uint8)]


class Surface(Structure):
    _fields_ = [
        ("flags",   c_uint32),
        ("format",  c_void_p),
        ("w",       c_int),
        ("h",       c_int),
        ("pitch",   c_int),
        ("pixels",  c_void_p),
        ("userdata", c_void_p),
        ("locked",  c_int),
        ("list_blitmap", c_void_p),
        ("clip_rect", Rect),
        ("map",     c_void_p),
        ("refcount", c_int),
    ]


class Event(Structure):
    """Opaque 56-byte buffer. Read .type, then cast to a specific event struct."""
    _fields_ = [("data", c_uint8 * 56)]

    @property
    def type(self) -> int:
        return c_uint32.from_buffer(self.data).value


class TouchFingerEvent(Structure):
    _fields_ = [
        ("type",      c_uint32),
        ("timestamp", c_uint32),
        ("touchId",   c_int64),
        ("fingerId",  c_int64),
        ("x",         c_float),
        ("y",         c_float),
        ("dx",        c_float),
        ("dy",        c_float),
        ("pressure",  c_float),
        ("windowID",  c_uint32),
    ]


class MouseButtonEvent(Structure):
    _fields_ = [
        ("type",      c_uint32),
        ("timestamp", c_uint32),
        ("windowID",  c_uint32),
        ("which",     c_uint32),
        ("button",    c_uint8),
        ("state",     c_uint8),
        ("clicks",    c_uint8),
        ("padding1",  c_uint8),
        ("x",         c_int32),
        ("y",         c_int32),
    ]


class MouseMotionEvent(Structure):
    _fields_ = [
        ("type",      c_uint32),
        ("timestamp", c_uint32),
        ("windowID",  c_uint32),
        ("which",     c_uint32),
        ("state",     c_uint32),
        ("x",         c_int32),
        ("y",         c_int32),
        ("xrel",      c_int32),
        ("yrel",      c_int32),
    ]


# ---------- prototypes ----------
def _bind(lib, name, restype, *argtypes):
    fn = getattr(lib, name)
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


# SDL2 core
Init                   = _bind(_sdl, "SDL_Init",                    c_int, c_uint32)
Quit                   = _bind(_sdl, "SDL_Quit",                    None)
GetError               = _bind(_sdl, "SDL_GetError",                c_char_p)
SetHint                = _bind(_sdl, "SDL_SetHint",                 c_int, c_char_p, c_char_p)
GetTicks               = _bind(_sdl, "SDL_GetTicks",                c_uint32)
Delay                  = _bind(_sdl, "SDL_Delay",                   None,  c_uint32)
PollEvent              = _bind(_sdl, "SDL_PollEvent",               c_int, POINTER(Event))

CreateWindow           = _bind(_sdl, "SDL_CreateWindow",            c_void_p,
                               c_char_p, c_int, c_int, c_int, c_int, c_uint32)
DestroyWindow          = _bind(_sdl, "SDL_DestroyWindow",           None,  c_void_p)
GetWindowSize          = _bind(_sdl, "SDL_GetWindowSize",           None,  c_void_p, POINTER(c_int), POINTER(c_int))

CreateRenderer         = _bind(_sdl, "SDL_CreateRenderer",          c_void_p, c_void_p, c_int, c_uint32)
DestroyRenderer        = _bind(_sdl, "SDL_DestroyRenderer",         None,  c_void_p)
SetRenderDrawColor     = _bind(_sdl, "SDL_SetRenderDrawColor",      c_int, c_void_p, c_uint8, c_uint8, c_uint8, c_uint8)
SetRenderDrawBlendMode = _bind(_sdl, "SDL_SetRenderDrawBlendMode",  c_int, c_void_p, c_int)
RenderClear            = _bind(_sdl, "SDL_RenderClear",             c_int, c_void_p)
RenderFillRect         = _bind(_sdl, "SDL_RenderFillRect",          c_int, c_void_p, POINTER(Rect))
RenderCopy             = _bind(_sdl, "SDL_RenderCopy",              c_int, c_void_p, c_void_p, POINTER(Rect), POINTER(Rect))
RenderPresent          = _bind(_sdl, "SDL_RenderPresent",           None,  c_void_p)

CreateTextureFromSurface = _bind(_sdl, "SDL_CreateTextureFromSurface", c_void_p, c_void_p, POINTER(Surface))
DestroyTexture           = _bind(_sdl, "SDL_DestroyTexture",           None, c_void_p)
FreeSurface              = _bind(_sdl, "SDL_FreeSurface",              None, POINTER(Surface))

# SDL_image
IMG_Init               = _bind(_img, "IMG_Init",                    c_int, c_int)
IMG_Quit               = _bind(_img, "IMG_Quit",                    None)
IMG_Load               = _bind(_img, "IMG_Load",                    POINTER(Surface), c_char_p)

# SDL_ttf
TTF_Init               = _bind(_ttf, "TTF_Init",                    c_int)
TTF_Quit               = _bind(_ttf, "TTF_Quit",                    None)
TTF_OpenFont           = _bind(_ttf, "TTF_OpenFont",                c_void_p, c_char_p, c_int)
TTF_CloseFont          = _bind(_ttf, "TTF_CloseFont",               None, c_void_p)
TTF_RenderUTF8_Blended = _bind(_ttf, "TTF_RenderUTF8_Blended",      POINTER(Surface), c_void_p, c_char_p, Color)
TTF_SizeUTF8           = _bind(_ttf, "TTF_SizeUTF8",                c_int, c_void_p, c_char_p, POINTER(c_int), POINTER(c_int))
TTF_FontHeight         = _bind(_ttf, "TTF_FontHeight",              c_int, c_void_p)
TTF_FontLineSkip       = _bind(_ttf, "TTF_FontLineSkip",            c_int, c_void_p)


def error() -> str:
    e = GetError()
    return e.decode("utf-8", "replace") if e else ""
