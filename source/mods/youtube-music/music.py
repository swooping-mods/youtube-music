#!/usr/bin/env python3
"""YouTube cache and control bridge. Runs on the host OS, outside Wine/Unity.
Only explicitly started helpers resolve music; no installer or game launcher.
"""
import argparse
from contextlib import contextmanager
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import urlparse, parse_qs

RATE = 44100
CHANNELS = 2
MAX_SECONDS = 600
MAX_PCM = RATE * CHANNELS * 2 * MAX_SECONDS
HEADER = struct.Struct('<8sIIII8x')
COMMAND = struct.Struct('<IIQIIQ12s4x')
ID = re.compile(r'[A-Za-z0-9_-]{11}\Z')
ACTIONS = {'play': 1, 'pause': 2, 'resume': 3, 'seek': 4, 'stop': 5, 'volume': 6}


def video_id(value):
    value = value.strip()
    if ID.fullmatch(value):
        return value
    url = urlparse(value)
    if url.scheme != 'https' or url.username or url.password or url.port not in (None, 443):
        raise ValueError('Use a YouTube HTTPS video link or an 11-character video ID')
    host = (url.hostname or '').lower()
    parts = url.path.strip('/').split('/')
    if host in ('youtu.be', 'www.youtu.be') and len(parts) == 1:
        result = parts[0]
    elif host in ('youtube.com', 'www.youtube.com', 'm.youtube.com', 'music.youtube.com'):
        if url.path == '/watch':
            ids = parse_qs(url.query).get('v', [])
            result = ids[0] if len(ids) == 1 else ''
        elif len(parts) == 2 and parts[0] in ('shorts', 'embed', 'live'):
            result = parts[1]
        else:
            result = ''
    else:
        result = ''
    if not ID.fullmatch(result):
        raise ValueError('Select one YouTube video; playlists and other websites are unsupported')
    return result


def private_path(path):
    """Reject symlinks, including dangling links and links in parent paths."""
    path = Path(os.path.abspath(path))
    if any(p.is_symlink() for p in (path, *path.parents)):
        raise ValueError('Music data must use an owned directory without symlinks')
    return path


def atomic(path, data):
    path = private_path(path)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name + '-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


@contextmanager
def instance_lock(directory):
    path = private_path(directory / 'helper.lock')
    with path.open('a+b') as lock:
        try:
            if os.name == 'nt':
                import msvcrt
                lock.seek(0); lock.write(b'0'); lock.flush(); lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise ValueError('A music helper already owns this game directory') from error
        yield


class Bridge:
    def __init__(self, directory):
        self.directory = private_path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.cache = private_path(self.directory / 'cache')
        self.cache.mkdir(exist_ok=True)

    def state(self):
        path = private_path(self.directory / 'state.json')
        try:
            stat = path.stat()
            if stat.st_size > 4096 or not 0 <= time.time() - stat.st_mtime <= 3:
                return None
            state = json.loads(path.read_bytes())
            if not isinstance(state, dict) or state.get('schema') != 1:
                return None
            bounds = {'session': (1, 2**64 - 1), 'host': (0, 1), 'active': (0, 1),
                      'playing': (0, 1), 'position_ms': (0, 600000), 'duration_ms': (0, 600000),
                      'volume': (0, 100), 'command': (0, 2**64 - 1)}
            if any(type(state.get(k)) is not int or not lo <= state[k] <= hi for k, (lo, hi) in bounds.items()):
                return None
            if state['position_ms'] > state['duration_ms']:
                return None
            if not isinstance(state.get('video'), str) or (state['video'] and not ID.fullmatch(state['video'])):
                return None
            if not isinstance(state.get('status'), str) or len(state['status']) > 80:
                return None
            return state
        except (OSError, ValueError, TypeError):
            return None

    def send(self, action, value=0, video='', session=None):
        state = self.state()
        if not state or not state['active']:
            raise ValueError('Load a world with YouTube Music and wait for the host connection')
        if session is not None and state['session'] != session:
            raise ValueError('The world or host changed; select the track again')
        if action != 'volume' and not state['host']:
            raise ValueError('Only the crew host controls shared music')
        if action not in ACTIONS or type(value) is not int or not 0 <= value <= 600000:
            raise ValueError('Invalid music command')
        if action == 'play' and (not ID.fullmatch(video) or not 0 < value <= 600000):
            raise ValueError('Invalid track or duration')
        if action == 'volume' and value > 100:
            raise ValueError('Volume must be between 0 and 100')
        if action == 'seek' and value > state['duration_ms']:
            raise ValueError('Seek is beyond the end of the track')
        pending = private_path(self.directory / 'command.bin')
        if pending.exists() and pending.stat().st_size == COMMAND.size:
            previous = COMMAND.unpack(pending.read_bytes())
            if previous[5] == state['session'] and previous[2] != state['command']:
                raise ValueError('Waiting for the previous music command; try again in a moment')
        nonce = secrets.randbits(64) or 1
        data = COMMAND.pack(0x3143594D, 1, nonce, ACTIONS[action], value, state['session'], video.encode('ascii'))
        atomic(self.directory / 'command.bin', data)
        return nonce


def pcm_info(path):
    path = private_path(path)
    with path.open('rb') as source:
        header = source.read(32)
    if len(header) != 32:
        raise ValueError('Incomplete audio header')
    magic, rate, channels, frames, size = HEADER.unpack(header)
    if magic != b'MYPCM01\0' or rate != RATE or channels != CHANNELS or not 0 < frames <= RATE * MAX_SECONDS:
        raise ValueError('Unsupported audio cache')
    if any(header[24:]) or size != frames * 4 or path.stat().st_size != 32 + size:
        raise ValueError('Incomplete audio cache')
    return frames * 1000 // RATE


def execute(argv, cancel, timeout=180):
    """Terminate only our extractor/decoder; never an unrelated runtime."""
    options = {'start_new_session': True} if os.name != 'nt' else {}
    with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **options) as process:
        deadline = time.monotonic() + timeout
        try:
            while True:
                if cancel.is_set() or time.monotonic() > deadline:
                    raise ValueError('Music preparation cancelled or timed out')
                try:
                    stdout, stderr = process.communicate(timeout=.2)
                    break
                except subprocess.TimeoutExpired:
                    pass
        except BaseException:
            if os.name != 'nt':
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            elif process.poll() is None:
                process.kill()
            process.communicate()
            raise
        if process.returncode:
            detail = stderr.decode('utf-8', 'replace')[-1200:].strip()
            raise ValueError('Music tool failed: ' + (detail or str(process.returncode)))
        if len(stdout) > 8 * 1024 * 1024:
            raise ValueError('Music tool returned too much metadata')
        return stdout


class Resolver:
    def __init__(self, bridge, cancel=None, runner=execute):
        self.bridge = bridge
        self.cancel = cancel or threading.Event()
        self.runner = runner

    def prepare(self, video):
        if not ID.fullmatch(video):
            raise ValueError('Invalid video ID')
        destination = private_path(self.bridge.cache / (video + '.pcm'))
        if destination.exists():
            return pcm_info(destination)
        yt, ff = shutil.which('yt-dlp'), shutil.which('ffmpeg')
        if not yt or not ff:
            raise ValueError('Install yt-dlp and FFmpeg on this computer; no automatic installation is performed')
        # Disable user config/plugins, implicit Node selection and remote component
        # downloads. An already installed Deno can solve supported challenges.
        command = [yt, '--ignore-config', '--no-plugin-dirs', '--no-js-runtimes', '--no-remote-components',
                   '--no-playlist', '--no-progress', '--socket-timeout', '15', '--retries', '2', '--extractor-retries', '2']
        deno = shutil.which('deno')
        if deno:
            command += ['--js-runtimes', 'deno:' + deno]
        url = 'https://www.youtube.com/watch?v=' + video
        metadata = json.loads(self.runner(command + ['--dump-single-json', '--skip-download', url], self.cancel))
        duration = metadata.get('duration')
        if metadata.get('id') != video or metadata.get('is_live') or metadata.get('live_status') in ('is_live', 'is_upcoming', 'post_live'):
            raise ValueError('Live, upcoming and mismatched videos are unsupported')
        if type(duration) not in (int, float) or not math.isfinite(duration) or not 0 < duration <= MAX_SECONDS:
            raise ValueError('Choose a finite track up to 10 minutes long')
        with tempfile.TemporaryDirectory(prefix='.prepare-', dir=self.bridge.cache) as temp:
            temp = Path(temp)
            media = temp / 'source.media'
            self.runner(command + ['--max-filesize', '64M', '--no-part', '--no-continue', '--no-overwrites',
                                   '--match-filters', '!is_live & duration <= 600', '-f', 'bestaudio/best', '-o', str(media), url], self.cancel)
            if not media.is_file() or media.stat().st_size > 64 * 1024 * 1024:
                raise ValueError('Audio download is missing or exceeds 64 MiB')
            raw = temp / 'samples.raw'
            self.runner([ff, '-hide_banner', '-loglevel', 'error', '-nostdin', '-protocol_whitelist', 'file,pipe',
                         '-i', str(media), '-vn', '-t', '601', '-ac', str(CHANNELS), '-ar', str(RATE),
                         '-f', 's16le', '-acodec', 'pcm_s16le', str(raw)], self.cancel)
            size = raw.stat().st_size
            if not 0 < size <= MAX_PCM or size % 4:
                raise ValueError('Decoded audio is incomplete or exceeds 10 minutes')
            frames = size // 4
            if abs(frames / RATE - duration) > 2:
                raise ValueError('Decoded audio length differs from the selected video')
            ready = temp / 'ready.pcm'
            with ready.open('wb') as output, raw.open('rb') as source:
                output.write(HEADER.pack(b'MYPCM01\0', RATE, CHANNELS, frames, size))
                shutil.copyfileobj(source, output)
                output.flush(); os.fsync(output.fileno())
            result = pcm_info(ready)
            if self.cancel.is_set():
                raise ValueError('Music preparation cancelled')
            private_path(destination)
            os.replace(ready, destination)
            self.prune(video)
            return result

    def prune(self, current):
        files = sorted((private_path(p) for p in self.bridge.cache.glob('*.pcm')), key=lambda p: p.stat().st_mtime, reverse=True)
        total = 0
        for index, path in enumerate(files):
            total += path.stat().st_size
            if (index >= 6 or total > 512 * 1024 * 1024) and path.stem != current:
                try:
                    path.unlink()
                except PermissionError:
                    pass  # Windows can retain a file while the game is reading it.


class Worker:
    """One bounded preparation job. A new request cancels an obsolete decode."""
    def __init__(self, bridge):
        self.bridge = bridge
        self.stop = threading.Event()
        self.cancel = threading.Event()
        self.guard = threading.Lock()
        self.request = None
        self.failed = None
        self.message = ''
        self.thread = threading.Thread(target=self.run, name='youtube-music-helper', daemon=True)

    def start(self):
        self.thread.start()

    def play(self, value):
        state = self.bridge.state()
        if not state or not state['host'] or not state['active']:
            raise ValueError('The crew host must load a world before selecting music')
        video = video_id(value)
        with self.guard:
            self.cancel.set()
            self.request = (video, state['session'], True, state.get('revision', 0))
            self.failed = None

    def retry(self):
        with self.guard:
            self.failed = None
        self.message = 'Retrying music preparation'

    def cancel_selection(self):
        with self.guard:
            self.cancel.set()
            self.request = None

    def close(self):
        self.stop.set(); self.cancel.set()
        if self.thread.is_alive():
            self.thread.join(timeout=5)

    def run(self):
        while not self.stop.wait(.25):
            state = self.bridge.state()
            with self.guard:
                request, self.request = self.request, None
                if request is None and state and state['active'] and state['video']:
                    request = (state['video'], state['session'], False, 0)
                if request is None or request == self.failed:
                    continue
                self.cancel = threading.Event()
                cancel = self.cancel
            video, session, publish, revision = request
            try:
                self.message = 'Preparing ' + video
                duration = Resolver(self.bridge, cancel).prepare(video)
                if cancel.is_set() or self.stop.is_set():
                    continue
                current = self.bridge.state()
                if not current or current['session'] != session:
                    continue
                if publish:
                    if current.get('revision', 0) != revision:
                        raise ValueError('Playback changed during preparation; select the track again')
                    self.bridge.send('play', duration, video, session)
                self.message = 'Cached ' + video
                if not publish:
                    # Avoid reparsing a ready cache four times a second.
                    while not self.stop.wait(.25):
                        with self.guard:
                            if self.request:
                                break
                        current = self.bridge.state()
                        if not current or current['session'] != session or current['video'] != video:
                            break
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                self.message = str(error)
                if not cancel.is_set():
                    self.failed = request


def game_bridge(path):
    path = private_path(path)
    installed = (path / 'MoonLab/mods/youtube-music/mod.dll').is_file() or (path / 'MoonLab.Mod.dll').is_file()
    if not (path / 'MoonSweepers.exe').is_file() or not installed:
        raise ValueError('Choose the private game copy with YouTube Music installed through MoonLab')
    return Bridge(path / 'MoonLab/music')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True, type=Path, help='Private MoonLab game folder')
    parser.add_argument('--headless', action='store_true', help='Follow the host without opening a panel')
    parser.add_argument('--play', help='Prepare and play one YouTube video as host')
    parser.add_argument('--action', choices=('pause', 'resume', 'stop'))
    parser.add_argument('--seek', type=float, help='Seek to seconds')
    parser.add_argument('--volume', type=int)
    parser.add_argument('--status', action='store_true')
    args = parser.parse_args(argv)
    bridge = game_bridge(args.game)
    if args.status:
        print(json.dumps(bridge.state(), indent=2)); return 0
    with instance_lock(bridge.directory):
        if args.action:
            bridge.send(args.action); return 0
        if args.seek is not None:
            if not math.isfinite(args.seek) or args.seek < 0:
                raise ValueError('Seek must be a finite nonnegative number of seconds')
            bridge.send('seek', int(args.seek * 1000)); return 0
        if args.volume is not None:
            bridge.send('volume', args.volume); return 0
        worker = Worker(bridge)
        if args.play:
            worker.play(args.play)
        worker.start()
        try:
            if args.headless:
                print('Following shared music. Ctrl+C stops the helper; the game is untouched.', flush=True)
                previous = ''
                while not worker.stop.wait(.5):
                    if worker.message != previous:
                        previous = worker.message; print(previous, flush=True)
            else:
                from panel import show
                return show(bridge, worker)
        finally:
            worker.close()
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, ImportError) as error:
        print(str(error), file=sys.stderr); raise SystemExit(1)
    except KeyboardInterrupt:
        raise SystemExit(0)
