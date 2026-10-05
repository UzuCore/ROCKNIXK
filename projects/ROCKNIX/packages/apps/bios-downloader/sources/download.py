# SPDX-License-Identifier: GPL-2.0-or-later
import fcntl
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import socket
import ssl
import stat
import tempfile
import threading
import time
import urllib.error
import urllib.request
import zipfile

SOURCE = 'https://codeload.github.com/UzuCore/minimal-Bios/zip/refs/heads/main'
ROOT = 'minimal-Bios-main'
MAX_DOWNLOAD = 1024 * 1024 * 1024
MAX_EXPANDED = 2 * 1024 * 1024 * 1024
MAX_FILES = 20000
CHUNK = 128 * 1024
COMMITS = 'https://api.github.com/repos/UzuCore/minimal-Bios/commits?sha=main&per_page=1'


def revision_status(destination):
    try:
        request = urllib.request.Request(COMMITS, headers={
            'User-Agent': 'ROCKNIXK-BIOS-Downloader/1.0', 'Accept': 'application/vnd.github+json'})
        with urllib.request.urlopen(request, timeout=6, context=ssl.create_default_context()) as response:
            data = json.loads(response.read(128 * 1024))
        commit = data[0]
        sha = commit['sha']
        if not re.fullmatch(r'[0-9a-f]{40}', sha):
            raise ValueError('Invalid commit SHA')
        date = commit['commit']['committer']['date'][:10]
        installed = None
        try:
            receipt = json.loads((Path(destination) / '.rocknix-bios-download.json').read_text())
            installed = receipt.get('revision')
        except (OSError, ValueError, AttributeError):
            pass
        state = 'current' if installed == sha else 'update' if installed else 'untracked'
        return {'state': state, 'sha': sha, 'date': date, 'installed': installed}
    except Exception as error:
        return {'state': 'unavailable', 'detail': str(error)}


class DownloadError(Exception):
    def __init__(self, code, detail=''):
        super().__init__(detail or code)
        self.code = code


class Cancelled(DownloadError):
    def __init__(self):
        super().__init__('cancelled')


class Downloader:
    def __init__(self, destination, notify, source=SOURCE, revision=None, revision_date=None):
        self.destination = Path(destination)
        self.notify = notify
        if revision is not None and not re.fullmatch(r'[0-9a-f]{40}', revision):
            raise DownloadError('archive', 'Invalid commit SHA')
        self.revision = revision
        self.revision_date = revision_date
        self.source = 'https://codeload.github.com/UzuCore/minimal-Bios/zip/' + revision if revision else source
        self.archive_root = 'minimal-Bios-' + revision if revision else ROOT
        self.cancel = threading.Event()
        self.state = {}
        self.last_notify = 0

    def publish(self, force=False, **fields):
        self.state.update(fields)
        now = time.monotonic()
        if force or now - self.last_notify >= 0.1:
            self.last_notify = now
            self.notify(dict(self.state))

    def check_cancel(self):
        if self.cancel.is_set():
            raise Cancelled()

    def fetch(self, target):
        context = ssl.create_default_context()
        request = urllib.request.Request(self.source, headers={'User-Agent': 'ROCKNIXK-BIOS-Downloader/1.0'})
        for attempt in range(3):
            self.check_cancel()
            self.publish(force=True, phase='download', progress=None, received=0, total=0,
                         speed=0, attempt=attempt + 1, detail='connecting')
            started = time.monotonic()
            received = 0
            hasher = hashlib.sha256()
            try:
                with urllib.request.urlopen(request, timeout=12, context=context) as response:
                    if response.geturl().split(':', 1)[0] != 'https':
                        raise DownloadError('tls', 'Unexpected insecure redirect')
                    total = int(response.headers.get('Content-Length', '0'))
                    if total > MAX_DOWNLOAD:
                        raise DownloadError('archive', 'Archive exceeds download limit')
                    with target.open('wb') as stream:
                        while True:
                            self.check_cancel()
                            block = response.read1(CHUNK)
                            if not block:
                                break
                            received += len(block)
                            if received > MAX_DOWNLOAD:
                                raise DownloadError('archive', 'Archive exceeds download limit')
                            stream.write(block)
                            hasher.update(block)
                            self.publish(received=received, total=total,
                                         speed=received / max(0.1, time.monotonic() - started), detail='',
                                         progress=received / total if total else None)
                        stream.flush()
                        os.fsync(stream.fileno())
                if not received or (total and received != total):
                    raise DownloadError('network', 'Incomplete download')
                self.publish(force=True, progress=1.0, received=received, total=total,
                             archive_sha256=hasher.hexdigest())
                return
            except urllib.error.HTTPError as error:
                if error.code not in (408, 429, 500, 502, 503, 504):
                    raise DownloadError('http', f'HTTP {error.code}') from error
                reason = f'HTTP {error.code}'
            except (urllib.error.URLError, socket.timeout, TimeoutError, ConnectionError) as error:
                reason = str(error)
                if isinstance(getattr(error, 'reason', error), ssl.SSLCertVerificationError):
                    raise DownloadError('tls', reason) from error
            except DownloadError as error:
                if error.code != 'network':
                    raise
                reason = str(error)
            if attempt == 2:
                raise DownloadError('network', reason)
            self.publish(force=True, detail='retrying', progress=None)
            if self.cancel.wait(attempt + 1):
                raise Cancelled()

    def members(self, archive):
        records = []
        names = set()
        expanded = 0
        items = archive.infolist()
        if len(items) > MAX_FILES:
            raise DownloadError('archive', 'Too many ZIP entries')
        for item in items:
            self.check_cancel()
            path = PurePosixPath(item.filename)
            if path.is_absolute() or '\\' in item.filename or '..' in path.parts or '\0' in item.filename:
                raise DownloadError('archive', 'Unsafe ZIP path')
            if not path.parts or path.parts[0] != self.archive_root:
                raise DownloadError('archive', 'Unexpected archive root')
            mode = item.external_attr >> 16
            if stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                raise DownloadError('archive', 'Unsupported ZIP entry')
            if item.is_dir():
                continue
            relative = Path(*path.parts[1:])
            if not relative.parts or relative.as_posix() in names:
                raise DownloadError('archive', 'Duplicate ZIP entry')
            names.add(relative.as_posix())
            expanded += item.file_size
            if expanded > MAX_EXPANDED:
                raise DownloadError('archive', 'Archive exceeds extraction limit')
            if any(part.startswith('.') for part in relative.parts) or relative.suffix.lower() == '.md':
                continue
            records.append((item, relative))
        if not records:
            raise DownloadError('archive', 'No BIOS files found')
        return records, expanded

    def check_space(self, required):
        if shutil.disk_usage(self.destination.parent).free < required + 8 * 1024 * 1024:
            raise DownloadError('space', 'Insufficient free space')

    def extract(self, archive, records, stage):
        hashes = {}
        total = sum(item.file_size for item, _ in records)
        processed = 0
        self.publish(force=True, phase='extract', progress=0, files=0, file_total=len(records), detail='')
        for count, (item, relative) in enumerate(records, 1):
            self.check_cancel()
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            hasher = hashlib.sha256()
            with archive.open(item) as source, target.open('wb') as destination:
                while True:
                    self.check_cancel()
                    block = source.read(CHUNK)
                    if not block:
                        break
                    destination.write(block)
                    hasher.update(block)
                    processed += len(block)
                    self.publish(progress=processed / max(1, total), filename=relative.as_posix(), files=count)
                destination.flush()
                os.fsync(destination.fileno())
            hashes[relative.as_posix()] = hasher.hexdigest()
        self.publish(force=True, progress=1.0, files=len(records))
        return hashes

    def safe_target(self, relative):
        target = self.destination / relative
        for parent in [target, *target.parents]:
            if parent == self.destination.parent:
                break
            if parent.is_symlink():
                raise DownloadError('destination', 'BIOS path contains a symbolic link')
        if target.exists() and not target.is_file():
            raise DownloadError('destination', 'BIOS destination is not a regular file')
        return target

    def install(self, stage, records, hashes, work):
        targets = [(relative, self.safe_target(relative)) for _, relative in records]
        self.destination.mkdir(parents=True, exist_ok=True)
        backup = work / 'backup'
        backup.mkdir()
        changed = []
        self.publish(force=True, phase='install', progress=0, files=0, detail='')
        try:
            for count, (relative, target) in enumerate(targets, 1):
                self.check_cancel()
                target.parent.mkdir(parents=True, exist_ok=True)
                old = None
                if target.exists():
                    with target.open('rb') as stream:
                        hasher = hashlib.sha256()
                        for block in iter(lambda: stream.read(CHUNK), b''):
                            hasher.update(block)
                        existing = hasher.hexdigest()
                    if existing == hashes[relative.as_posix()]:
                        self.publish(progress=count / len(targets), files=count, filename=relative.as_posix())
                        continue
                    old = backup / relative
                    old.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(target, old)
                changed.append((target, old))
                os.replace(stage / relative, target)
                self.publish(progress=count / len(targets), files=count, filename=relative.as_posix())
            receipt = {'source': self.source, 'archive_sha256': self.state['archive_sha256'],
                       'revision': self.revision, 'revision_date': self.revision_date,
                       'files': hashes, 'installed_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}
            receipt_path = work / 'receipt.json'
            receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
            with receipt_path.open('rb') as stream:
                os.fsync(stream.fileno())
            os.replace(receipt_path, self.destination / '.rocknix-bios-download.json')
            fd = os.open(self.destination, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        except BaseException:
            self.publish(force=True, detail='restoring', progress=None)
            failures = []
            for target, old in reversed(changed):
                try:
                    if old is not None:
                        os.replace(old, target)
                    else:
                        target.unlink(missing_ok=True)
                except OSError as error:
                    failures.append(str(error))
            if failures:
                raise DownloadError('restore', 'Backup retained: ' + str(work) + '; ' + '; '.join(failures))
            raise
        self.publish(force=True, phase='done', progress=1.0, files=len(records), filename='', detail='')

    def run(self):
        work = None
        keep_backup = False
        try:
            self.destination.parent.mkdir(parents=True, exist_ok=True)
            with (self.destination.parent / '.rocknix-bios-download.lock').open('a') as lock:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError as error:
                    raise DownloadError('busy', 'Another BIOS download is running') from error
                self.check_space(16 * 1024 * 1024)
                work = Path(tempfile.mkdtemp(prefix='.bios-download-', dir=self.destination.parent))
                packed = work / 'download.zip'
                self.fetch(packed)
                self.publish(force=True, phase='verify', progress=None, detail='')
                with zipfile.ZipFile(packed) as archive:
                    records, expanded = self.members(archive)
                    existing = sum(self.safe_target(relative).stat().st_size
                                   for _, relative in records if self.safe_target(relative).exists())
                    self.check_space(expanded + existing)
                    stage = work / 'files'
                    stage.mkdir()
                    hashes = self.extract(archive, records, stage)
                self.install(stage, records, hashes, work)
        except Cancelled:
            self.publish(force=True, phase='cancelled', progress=0, detail='', filename='')
        except Exception as error:
            code = getattr(error, 'code', 'write' if isinstance(error, OSError) else 'archive')
            keep_backup = code == 'restore'
            self.publish(force=True, phase='error', error=code, detail=str(error), filename='')
        finally:
            if work is not None and not keep_backup:
                shutil.rmtree(work)
