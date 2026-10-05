#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import ctypes as C
import os
from pathlib import Path
import queue
import signal
import struct
import sys
import threading
import time

from download import Downloader, SOURCE, revision_status
from sdl_ui import Event, Screen

# Same palette and confirmed-input behavior as the Odin LED tool.
COLORS = {'background': '#181818', 'card': '#242424', 'button': '#353535',
          'accent': '#FF5555', 'text': '#F4F4F4', 'muted': '#B8B8B8', 'border': '#555555'}
WORDS = {
    'title': ('BIOS 다운로드', 'BIOS Downloader'),
    'subtitle': ('필요한 BIOS를 내려받고 설치합니다.', 'Download and install system BIOS files.'),
    'download': ('다운로드', 'Download'), 'verify': ('파일 검사', 'Verify'),
    'extract': ('압축 해제', 'Extract'), 'install': ('설치', 'Install'),
    'ready': ('다운로드할 준비가 되었습니다.', 'Ready to download.'),
    'connecting': ('서버에 연결하고 있습니다…', 'Connecting to the server…'),
    'retrying': ('다시 연결하고 있습니다…', 'Reconnecting…'),
    'restoring': ('기존 파일을 복원하고 있습니다…', 'Restoring previous files…'),
    'cancelling': ('취소하고 있습니다…', 'Cancelling…'),
    'cancelled': ('다운로드를 취소했습니다.', 'Download cancelled.'),
    'done': ('설치가 완료되었습니다.', 'Installation complete.'),
    'error': ('작업을 완료하지 못했습니다.', 'Could not finish the download.'),
    'start': ('다운로드 시작', 'Start download'), 'again': ('다시 다운로드', 'Download again'),
    'retry': ('다시 시도', 'Retry'), 'back': ('돌아가기', 'Back'), 'cancel': ('취소', 'Cancel'),
    'working': ('작업 진행 중', 'Working…'),
    'check': ('업데이트 확인', 'Check updates'),
    'checking': ('최신 내용을 확인하고 있습니다…', 'Checking for updates…'),
    'current': ('최신 상태', 'Up to date'), 'update': ('업데이트 있음', 'Update available'),
    'untracked': ('설치 버전 정보 없음', 'Installed version unknown'),
    'unavailable': ('버전 확인 불가 · 다운로드 가능', 'Version check unavailable · download available'),
    'files': ('파일', 'files'), 'received': ('받음', 'received'),
    'hints': ('방향키 이동  ·  A/B 확인  ·  Select+Start 종료', 'D-pad: move  ·  A/B: confirm  ·  Select+Start: exit'),
    'destination': ('저장 위치', 'Destination'),
    'network': ('인터넷 연결을 확인한 뒤 다시 시도하세요.', 'Check your internet connection and retry.'),
    'http': ('다운로드 서버에서 오류가 발생했습니다.', 'The download server returned an error.'),
    'tls': ('기기 날짜와 인증서를 확인하세요.', 'Check the device date and certificates.'),
    'archive': ('다운로드 파일이 손상되었거나 형식이 다릅니다.', 'The downloaded archive is invalid or damaged.'),
    'write': ('저장소에 파일을 쓸 수 없습니다.', 'Cannot write to storage.'),
    'space': ('저장소의 빈 공간이 부족합니다.', 'Not enough free storage.'),
    'busy': ('이미 BIOS 다운로드가 실행 중입니다.', 'Another BIOS download is already running.'),
    'destination_error': ('BIOS 저장 경로를 확인하세요.', 'Check the BIOS destination path.'),
    'restore': ('파일 복원이 실패했습니다. 로그의 백업 경로를 확인하세요.', 'Restore failed. The log contains the backup path.'),
}
PHASES = ('download', 'verify', 'extract', 'install')


def byte_text(value):
    value = max(0, value)
    for suffix in ('B', 'KiB', 'MiB', 'GiB'):
        if value < 1024 or suffix == 'GiB':
            return f'{value:.1f} {suffix}' if suffix != 'B' else f'{int(value)} B'
        value /= 1024


def layout(width, height):
    scale = min(2.2, width / 640, height / 480)
    margin = round(24 * scale)
    content = min(width - 2 * margin, round(880 * scale))
    left = (width - content) // 2
    header_y = margin
    button_h = round(52 * scale)
    hint_h = round(24 * scale)
    button_y = height - margin - hint_h - button_h - round(10 * scale)
    card_y = header_y + round(124 * scale)
    card_h = button_y - card_y - round(18 * scale)
    gap = round(14 * scale)
    button_w = (content - 2 * gap) // 3
    return {'scale': scale, 'left': left, 'width': content, 'header_y': header_y,
            'card': (left, card_y, content, card_h),
            'buttons': [(left + index * (button_w + gap), button_y, button_w, button_h) for index in range(3)],
            'hint_y': button_y + button_h + round(10 * scale)}


class App:
    def __init__(self, screen, destination, source=SOURCE, language=None):
        self.screen = screen
        self.destination = Path(destination)
        self.source = source
        self.korean = (language or os.environ.get('LANG', 'en')).lower().startswith('ko')
        self.status = {'phase': 'ready', 'progress': 0}
        self.notifications = queue.SimpleQueue()
        self.worker = None
        self.downloader = None
        self.focus = 0
        self.held = set()
        self.axes = {}
        self.controller_id = next(iter(screen.controllers), None)
        self.pointer = None
        self.running = True
        self.exit_requested = False
        self.cancel_requested = False
        self.changed = True
        self.last_draw = 0
        self.action_count = 0
        self.revision = {'state': 'untracked'}
        self.check_worker = None
        self.start_requested = False
        self.previous_busy = False

    def word(self, key):
        return WORDS.get(key, WORDS['error'])[0 if self.korean else 1]

    @property
    def busy(self):
        return self.start_requested or (self.worker is not None and self.worker.is_alive())

    def check_updates(self):
        if self.check_worker is not None and self.check_worker.is_alive():
            return
        self.revision = {'state': 'checking'}
        self.changed = True
        def check():
            result = revision_status(self.destination)
            print('BIOS_VERSION_CHECK', result, flush=True)
            self.notifications.put({'kind': 'revision', **result})
        self.check_worker = threading.Thread(target=check, name='bios-version-check', daemon=True)
        self.check_worker.start()

    def update(self):
        while not self.notifications.empty():
            result = self.notifications.get()
            if result.get('kind') == 'revision':
                self.revision = result
                self.changed = True
                if self.start_requested:
                    self.start_requested = False
                    self.start()
                continue
            self.status = result
            self.changed = True
            if self.status['phase'] == 'error':
                print('BIOS_DOWNLOAD_ERROR:', self.status.get('error'), self.status.get('detail'), flush=True)
            if self.status['phase'] == 'done':
                if self.downloader and self.downloader.revision:
                    self.revision['installed'] = self.downloader.revision
                    if self.revision.get('sha'):
                        self.revision['state'] = 'current' if self.revision['sha'] == self.downloader.revision else 'update'
                print('BIOS_DOWNLOAD_DONE:', self.status.get('files'), self.status.get('archive_sha256'), flush=True)
        busy = self.busy
        if busy != self.previous_busy:
            self.previous_busy = busy
            self.changed = True
        if self.exit_requested and not busy:
            self.running = False

    def start(self):
        if self.busy:
            return
        if self.revision['state'] == 'checking':
            self.start_requested = True
            self.status = {'phase': 'download', 'progress': None, 'detail': 'checking'}
            self.changed = True
            return
        self.action_count += 1
        self.cancel_requested = False
        self.status = {'phase': 'download', 'progress': None, 'detail': 'connecting'}
        self.downloader = Downloader(self.destination, self.notifications.put, self.source,
                                     revision=self.revision.get('sha'), revision_date=self.revision.get('date'))
        self.worker = threading.Thread(target=self.downloader.run, name='bios-download')
        self.worker.start()
        self.changed = True

    def cancel(self, exiting=False):
        self.exit_requested |= exiting
        if self.start_requested:
            self.start_requested = False
            self.status = {'phase': 'cancelled', 'progress': 0}
            self.changed = True
            if exiting:
                self.running = False
        elif self.busy:
            self.downloader.cancel.set()
            self.cancel_requested = True
            self.changed = True
        elif exiting:
            self.running = False

    def activate(self, button):
        if button == 0:
            self.start()
        elif button == 1:
            self.check_updates()
        elif self.busy:
            self.cancel()
        else:
            self.running = False

    def navigate(self, direction):
        self.focus = (self.focus + direction) % 3
        self.changed = True

    def point(self, x, y):
        for index, (left, top, width, height) in enumerate(layout(*self.screen.size)['buttons']):
            if left <= x < left + width and top <= y < top + height:
                return index
        return None

    def pointer_event(self, down, x, y):
        index = self.point(x, y)
        if down:
            self.pointer = index
            if index is not None:
                self.focus = index
                self.changed = True
        else:
            pressed, self.pointer = self.pointer, None
            if index is not None and index == pressed:
                self.activate(index)

    def event(self, kind, data):
        if kind == 0x100:
            self.cancel(exiting=True)
        elif kind == 0x200:
            subtype = data[12]
            if subtype == 14:
                self.cancel(exiting=True)
            elif subtype in (5, 6, 9, 12):
                self.screen.refresh_size()
                self.pointer = None
                self.changed = True
            elif subtype == 13:
                self.held.clear()
                self.axes.clear()
                self.pointer = None
        elif kind == 0x300:
            key = struct.unpack_from('i', data, 20)[0]
            if data[13]:
                return
            if key in (1073741903, 1073741905, 9):
                self.navigate(1)
            elif key in (1073741904, 1073741906):
                self.navigate(-1)
            elif key in (13, 32):
                self.activate(self.focus)
            elif key == 27:
                self.cancel(exiting=True)
        elif kind in (0x401, 0x402):
            which = struct.unpack_from('I', data, 12)[0]
            if which == 0xFFFFFFFF or data[16] != 1:
                return
            x, y = struct.unpack_from('ii', data, 20)
            ww, wh = self.screen.window_size
            w, h = self.screen.size
            self.pointer_event(kind == 0x401, x * w / max(1, ww), y * h / max(1, wh))
        elif kind in (0x700, 0x701):
            x, y = struct.unpack_from('ff', data, 24)
            self.pointer_event(kind == 0x700, x * self.screen.size[0], y * self.screen.size[1])
        elif kind in (0x650, 0x651, 0x652):
            identifier = struct.unpack_from('i', data, 8)[0]
            if self.controller_id != identifier:
                return
            control = data[12]
            if kind == 0x650:
                if control not in (0, 1):
                    return
                value = struct.unpack_from('h', data, 16)[0]
                direction = 1 if value > 18000 else -1 if value < -18000 else 0
                previous = self.axes.get(control, 0)
                self.axes[control] = direction
                if direction and direction != previous:
                    self.navigate(direction)
                return
            if kind == 0x652:
                self.held.discard(control)
                return
            if control in self.held:
                return
            self.held.add(control)
            if {4, 6}.issubset(self.held) or {5, 6}.issubset(self.held):
                self.cancel(exiting=True)
            elif control in (0, 1):
                self.activate(self.focus)
            elif control in (11, 13):
                self.navigate(-1)
            elif control in (12, 14):
                self.navigate(1)
        elif kind == 0x653:
            self.screen.open_controllers()
            if self.controller_id is None:
                self.controller_id = next(iter(self.screen.controllers), None)
        elif kind == 0x654:
            identifier = struct.unpack_from('i', data, 8)[0]
            controller = self.screen.controllers.pop(identifier, None)
            if controller:
                self.screen.sdl.SDL_GameControllerClose(controller)
            if self.controller_id == identifier:
                self.controller_id = next(iter(self.screen.controllers), None)
                self.held.clear()
                self.axes.clear()

    def draw(self, now=None):
        now = now if now is not None else time.monotonic()
        s = self.screen
        metrics = layout(*s.size)
        k = metrics['scale']
        left, width = metrics['left'], metrics['width']
        y = metrics['header_y']
        s.clear(COLORS['background'])
        s.text('ROCKNIXK', 14 * k, COLORS['accent'], left, y, width)
        s.text(self.word('title'), 32 * k, COLORS['text'], left, y + 22 * k, width)
        s.text(self.word('subtitle'), 17 * k, COLORS['muted'], left, y + 68 * k, width)
        version = self.word(self.revision['state'])
        if self.revision.get('sha'):
            version += f"  ·  {self.revision.get('date', '')}  ·  {self.revision['sha'][:8]}"
        s.text(version, 13 * k, COLORS['accent'] if self.revision['state'] == 'update' else COLORS['muted'],
               left, y + 94 * k, width)
        card = metrics['card']
        s.box(card, COLORS['card'])
        x, y, cw, ch = card
        inset = round(20 * k)
        x += inset
        y += inset
        cw -= 2 * inset
        phase = self.status['phase']
        current = PHASES.index(phase) if phase in PHASES else 4 if phase == 'done' else -1
        step_w = cw / 4
        for index, name in enumerate(PHASES):
            color = COLORS['accent'] if index == current else COLORS['text'] if index < current else COLORS['border']
            s.box((x + index * step_w, y, step_w - 8 * k, 4 * k), color)
            s.text(f'{index + 1}  {self.word(name)}', 14 * k, color,
                   x + index * step_w, y + 10 * k, step_w - 5 * k)
        status_y = y + 54 * k
        heading = self.word('cancelling') if self.cancel_requested and self.busy else self.word(phase)
        if phase in PHASES and self.status.get('detail') in ('connecting', 'retrying', 'restoring', 'checking'):
            heading = self.word(self.status['detail'])
        if phase == 'error':
            key = self.status.get('error', 'error')
            heading = self.word('destination_error' if key == 'destination' else key)
        progress = self.status.get('progress')
        status_lines = s.wrap(heading, 22 * k, cw)
        for index, line in enumerate(status_lines[:2]):
            s.text(line, 22 * k, COLORS['text'], x, status_y + index * 27 * k, cw)
        bar_y = max(status_y + 61 * k, y + ch - 2 * inset - 86 * k)
        bar_h = max(5, round(10 * k))
        s.box((x, bar_y, cw, bar_h), COLORS['button'])
        if progress is not None:
            s.box((x, bar_y, cw * min(1, max(0, progress)), bar_h), COLORS['accent'])
        elif phase in PHASES:
            pulse_w = cw * 0.22
            travel = (now % 2) / 2
            position = x + (cw - pulse_w) * (1 - abs(travel * 2 - 1))
            s.box((position, bar_y, pulse_w, bar_h), COLORS['accent'])
        detail = ''
        if phase == 'download' and self.status.get('received', 0):
            detail = byte_text(self.status['received'])
            if self.status.get('total'):
                detail += ' / ' + byte_text(self.status['total'])
            else:
                detail += ' ' + self.word('received')
            detail += '  ·  ' + byte_text(self.status.get('speed', 0)) + '/s'
        elif phase in ('extract', 'install', 'done'):
            detail = f"{self.status.get('files', 0)} / {self.status.get('file_total', 0)} {self.word('files')}"
        elif phase == 'error' and self.status.get('error') == 'http':
            detail = self.status.get('detail', '')
        elif phase == 'verify':
            detail = byte_text(self.status.get('received', 0))
        if progress is not None and phase in PHASES:
            detail += f'  ·  {progress * 100:.0f}%'
        s.text(detail, 16 * k, COLORS['muted'], x, bar_y + 17 * k, cw)
        path = self.status.get('filename') or f"{self.word('destination')}: {self.destination}"
        s.text(path, 13 * k, COLORS['muted'], x, bar_y + 43 * k, cw)
        label = 'retry' if phase == 'error' else 'again' if phase == 'done' else 'start'
        for index, (bx, by, bw, bh) in enumerate(metrics['buttons']):
            active = index == self.focus
            fill = COLORS['accent'] if active else COLORS['button']
            s.box((bx, by, bw, bh), fill)
            s.box((bx, by, bw, bh), COLORS['accent'] if active else COLORS['border'], border=True)
            text = self.word('working' if self.busy else label) if index == 0 else self.word('check') if index == 1 else self.word('cancel' if self.busy else 'back')
            color = COLORS['background'] if active else COLORS['text']
            s.text(text, 20 * k, color, bx + 8 * k, by + (bh - s.measure(text, 20 * k)[1]) / 2,
                   bw - 16 * k, centered=True)
        s.text(self.word('hints'), 13 * k, COLORS['muted'], left, metrics['hint_y'], width, centered=True)
        self.changed = False
        self.last_draw = now

    def loop(self, duration=None, screenshot=None):
        started = time.monotonic()
        while self.running:
            for kind, data in self.screen.events():
                self.event(kind, data)
            self.update()
            now = time.monotonic()
            if self.changed or (self.busy and now - self.last_draw >= 0.1):
                self.draw(now)
                if screenshot:
                    self.screen.screenshot(screenshot)
                    screenshot = None
                self.screen.present()
            if duration is not None and now - started >= duration:
                self.cancel(exiting=True)
            time.sleep(0.016)
        if self.worker is not None:
            self.worker.join()
        self.screen.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--destination', default='/storage/roms/bios')
    parser.add_argument('--size', help='Windowed preview, e.g. 640x480')
    parser.add_argument('--font')
    parser.add_argument('--language')
    parser.add_argument('--screenshot')
    parser.add_argument('--duration', type=float)
    parser.add_argument('--demo-phase', choices=('ready', *PHASES, 'done', 'error'))
    parser.add_argument('--download', action='store_true', help='Start download on opening')
    args = parser.parse_args()
    size = tuple(map(int, args.size.lower().split('x'))) if args.size else None
    if size and (len(size) != 2 or min(size) < 240 or max(size) > 8192):
        parser.error('Invalid preview size')
    screen = None
    app = None
    try:
        screen = Screen(size=size, font=args.font)
        app = App(screen, args.destination, language=args.language)
        if args.demo_phase:
            app.status = {'phase': args.demo_phase, 'progress': 0.57, 'received': 42 * 1024**2,
                          'total': 73 * 1024**2, 'speed': 3.2 * 1024**2, 'files': 71, 'file_total': 124,
                          'filename': 'dc/dc_boot.bin', 'error': 'network'}
            app.revision = {'state': 'update', 'sha': 'a' * 40, 'date': '2026-10-04'}
        else:
            app.check_updates()
        if args.download:
            app.start()
        signal.signal(signal.SIGTERM, lambda *_: app.cancel(exiting=True))
        signal.signal(signal.SIGINT, lambda *_: app.cancel(exiting=True))
        print('BIOS_GUI_READY', screen.size, screen.font_path, flush=True)
        app.loop(args.duration, args.screenshot)
    except Exception as error:
        if app is not None and app.busy:
            app.cancel(exiting=True)
            app.worker.join()
        if screen is not None:
            screen.close()
        print('BIOS_GUI_ERROR:', error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
