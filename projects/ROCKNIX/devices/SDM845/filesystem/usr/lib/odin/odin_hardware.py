#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
import os
import fcntl
from pathlib import Path
import tempfile
import time

SYS = Path('/sys')
MODEL = Path('/proc/device-tree/model')
CONFIG = Path('/storage/.config/system/configs/system.cfg')
LOCK = Path('/tmp/.system.cfg.lock')
LED_LOCK = Path('/run/odin-led-command.lock')
LEDS = {'left-side': ('led-left', 1), 'right-side': ('led-right', 2),
        'left-stick': ('led-left-stick', 3), 'right-stick': ('led-right-stick', 4)}
LED_EFFECTS = {'steady': None, 'slow': (1000, 1000), 'normal': (500, 500), 'fast': (150, 150)}
PROFILES = {'powersave': ('powersave', 'powersave'),
            'balanced': ('schedutil', 'simple_ondemand'),
            'performance': ('performance', 'performance')}


def require_odin():
    if MODEL.read_bytes().rstrip(b'\0').decode() not in ('AYN Odin', 'AYN Odin M2'):
        raise ValueError('unsupported_model')


def read_config():
    values = {}
    if CONFIG.exists():
        for line in CONFIG.read_text().splitlines():
            key, separator, value = line.partition('=')
            if separator and not key.startswith('#'):
                values[key] = value
    return values


def save_config(changes):
    deadline = time.monotonic() + 5
    while True:
        try:
            fd = os.open(LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            with os.fdopen(fd, 'w') as stream:
                stream.write(str(os.getpid()) + '\n')
            break
        except FileExistsError:
            if time.monotonic() >= deadline:
                raise RuntimeError('settings_busy')
            time.sleep(0.05)
    temporary = None
    try:
        original = CONFIG.read_text() if CONFIG.exists() else ''
        lines = [line for line in original.splitlines() if line.partition('=')[0] not in changes]
        lines.extend(key + '=' + str(value) for key, value in sorted(changes.items()))
        CONFIG.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode='w', dir=CONFIG.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write('\n'.join(lines) + '\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, CONFIG.stat().st_mode & 0o777 if CONFIG.exists() else 0o644)
        os.replace(temporary, CONFIG)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        if LOCK.exists() and LOCK.read_text().strip() == str(os.getpid()):
            LOCK.unlink()


def led_path(name):
    node_name, number = LEDS[name]
    for led in (SYS / 'class/leds').glob('*'):
        if (led / 'of_node').resolve().name == node_name:
            return led
    candidate = SYS / 'class/leds' / ('blue:backlight-' + str(number))
    if candidate.is_dir() and (candidate / 'max_brightness').read_text().strip() == '1':
        return candidate
    raise FileNotFoundError('missing_led:' + name)


def led_trigger(path):
    trigger = path / 'trigger'
    if not trigger.exists():
        return 'none', []
    items = trigger.read_text().split()
    selected = next((item[1:-1] for item in items if item.startswith('[')),
                    items[0] if len(items) == 1 else 'none')
    return selected, [item.strip('[]') for item in items]


def saved_effect(saved, name):
    effect = saved.get('odin.led.' + name + '.effect', 'steady')
    return effect if effect in LED_EFFECTS else 'steady'


def scan_available():
    try:
        return all('odin-scan' in led_trigger(led_path(name))[1] for name in LEDS)
    except OSError:
        return False


def scan_running():
    try:
        return all(led_trigger(led_path(name))[0] == 'odin-scan' and
                   (led_path(name) / 'scan_running').read_text().strip() == '1'
                   for name in LEDS)
    except OSError:
        return False


def stop_scan():
    # Detaching even one LED stops the shared kernel work. Detach all four
    # so a later individual setting cannot leave a partial group behind.
    for name in LEDS:
        path = led_path(name)
        if led_trigger(path)[0] == 'odin-scan':
            (path / 'trigger').write_text('none\n')
            if led_trigger(path)[0] != 'none':
                raise RuntimeError('led_scan_stop_failed')


def start_scan():
    if not scan_available():
        raise ValueError('kernel_scan_unavailable')
    try:
        for name in LEDS:
            path = led_path(name)
            (path / 'trigger').write_text('odin-scan\n')
            if led_trigger(path)[0] != 'odin-scan':
                raise RuntimeError('led_scan_trigger_failed')
        if not scan_running():
            raise RuntimeError('led_scan_start_failed')
    except Exception:
        stop_scan()
        raise


def led_status():
    require_odin()
    saved = read_config()
    scanning = scan_running()
    result = {}
    for name in LEDS:
        try:
            path = led_path(name)
            trigger, supported = led_trigger(path)
            on = trigger == 'timer' or int((path / 'brightness').read_text()) > 0
            effect = saved_effect(saved, name) if not on else 'steady'
            if trigger == 'timer':
                delays = tuple(int((path / key).read_text()) for key in ('delay_on', 'delay_off'))
                effect = next((key for key, value in LED_EFFECTS.items() if value == delays), 'normal')
            if scanning:
                on, effect = True, 'scan'
            result[name] = {'available': True, 'on': on,
                            'saved': saved.get('odin.led.' + name, '0') == '1',
                            'color': 'blue', 'color_supported': False, 'effect': effect,
                            'effects': list(LED_EFFECTS) if 'timer' in supported else ['steady']}
        except FileNotFoundError:
            result[name] = {'available': False, 'on': False, 'saved': False}
    return {'leds': result, 'group_effect': 'scan' if scanning else saved.get('odin.led.group-effect', 'individual'),
            'group_running': scanning, 'scan_available': scan_available()}


def apply_led(path, on, effect):
    selected, supported = led_trigger(path)
    if selected != 'none':
        (path / 'trigger').write_text('none\n')
    brightness = str(int((path / 'max_brightness').read_text()) if on else 0)
    (path / 'brightness').write_text(brightness + '\n')
    if on and LED_EFFECTS[effect] is not None:
        (path / 'trigger').write_text('timer\n')
        for key, value in zip(('delay_on', 'delay_off'), LED_EFFECTS[effect]):
            (path / key).write_text(str(value) + '\n')
            if int((path / key).read_text()) != value:
                raise RuntimeError('led_effect_readback_failed')
        if led_trigger(path)[0] != 'timer':
            raise RuntimeError('led_effect_readback_failed')
    elif (path / 'brightness').read_text().strip() != brightness:
        raise RuntimeError('led_readback_failed')


def set_leds(changes, persist=True, effects=None, group_effect=None):
    require_odin()
    with LED_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return _set_leds(changes, persist=persist, effects=effects, group_effect=group_effect)


def _set_leds(changes, persist=True, effects=None, group_effect=None):
    require_odin()
    explicit_effects = effects is not None
    effects = {} if effects is None else effects
    if group_effect not in (None, 'scan'):
        raise ValueError('invalid_group_effect')
    if any(name not in changes or effect not in LED_EFFECTS for name, effect in effects.items()):
        raise ValueError('invalid_led_effect')
    saved = read_config()
    scanning = scan_running()
    keep_scan = (group_effect == 'scan' or
                 (saved.get('odin.led.group-effect') == 'scan' and not explicit_effects and set(changes) == set(LEDS)))
    if keep_scan and any(changes.values()) and not scan_available():
        raise ValueError('kernel_scan_unavailable')
    if keep_scan and (set(changes) != set(LEDS) or len(set(changes.values())) != 1):
        raise ValueError('scan_requires_all_leds')
    if saved.get('odin.led.group-effect') == 'scan' and not keep_scan:
        changes = {**{name: int(saved.get('odin.led.' + name, '0') == '1') for name in LEDS}, **changes}
    snapshot = []
    for name, on in changes.items():
        if name not in LEDS or on not in (0, 1):
            raise ValueError('invalid_led_setting')
        path = led_path(name)
        previous = (path / 'brightness').read_text().strip()
        selected, supported = led_trigger(path)
        delays = {key: (path / key).read_text().strip() for key in ('delay_on', 'delay_off')
                  if selected == 'timer' and (path / key).exists()}
        effect = 'steady' if keep_scan else effects.get(name, saved_effect(saved, name))
        if on and effect != 'steady' and 'timer' not in supported:
            raise ValueError('unsupported_led_effect')
        maximum = int((path / 'max_brightness').read_text())
        if maximum < 1:
            raise ValueError('invalid_led_brightness')
        snapshot.append((path, previous, selected, delays, on, effect))
    try:
        stop_scan()
        for path, previous, selected, delays, on, effect in snapshot:
            apply_led(path, False if keep_scan else on, effect)
        if keep_scan and any(changes.values()):
            start_scan()
        if persist:
            values = {'odin.led.' + name: int(on) for name, on in changes.items()}
            values.update({'odin.led.' + name + '.effect': effect for name, effect in effects.items()})
            if keep_scan:
                values.update({'odin.led.' + name + '.effect': 'steady' for name in LEDS})
            values['odin.led.group-effect'] = 'scan' if keep_scan else 'individual'
            save_config(values)
    except Exception:
        stop_scan()
        for path, previous, selected, delays, on, effect in reversed(snapshot):
            if led_trigger(path)[0] != 'none':
                (path / 'trigger').write_text('none\n')
            (path / 'brightness').write_text(previous + '\n')
            if selected and selected not in ('none', 'odin-scan'):
                (path / 'trigger').write_text(selected + '\n')
                for key, value in delays.items():
                    (path / key).write_text(value + '\n')
        if scanning:
            start_scan()
        raise
    return led_status()


def set_effects(changes, persist=True):
    return set_leds({name: 1 for name in changes}, persist=persist, effects=changes)


def set_plain_leds(changes, persist=True):
    effects = {name: 'steady' for name, on in changes.items() if on}
    return set_leds(changes, persist=persist, effects=effects or None)


def set_scan(persist=True):
    return set_leds({name: 1 for name in LEDS}, persist=persist, group_effect='scan')


def restore_leds(off=False):
    status = led_status()['leds']
    saved = read_config()
    changes = {name: 0 if off else int(saved.get('odin.led.' + name, '0') == '1')
               for name, item in status.items() if item['available']}
    return set_leds(changes, persist=False)


def performance_nodes():
    require_odin()
    cpu = [SYS / 'devices/system/cpu/cpufreq' / name for name in ('policy0', 'policy4')]
    gpu = [path for path in (SYS / 'class/devfreq').glob('*') if path.name == '5000000.gpu']
    if not all((path / 'scaling_governor').exists() for path in cpu) or len(gpu) != 1:
        raise FileNotFoundError('missing_performance_nodes')
    return cpu, gpu[0]


def performance_status():
    cpu, gpu = performance_nodes()
    values = [path.joinpath('scaling_governor').read_text().strip() for path in cpu]
    gpu_value = (gpu / 'governor').read_text().strip()
    selected = next((key for key, pair in PROFILES.items()
                     if values == [pair[0], pair[0]] and gpu_value == pair[1]), 'custom')
    supported = [key for key, pair in PROFILES.items()
                 if all(pair[0] in (path / 'scaling_available_governors').read_text().split() for path in cpu)
                 and pair[1] in (gpu / 'available_governors').read_text().split()]
    return {'profile': selected, 'cpu': values, 'gpu': gpu_value, 'supported': supported}


def set_performance(profile, persist=True):
    if profile not in PROFILES:
        raise ValueError('invalid_profile')
    status = performance_status()
    if profile not in status['supported']:
        raise ValueError('unsupported_profile')
    cpu, gpu = performance_nodes()
    cpu_governor, gpu_governor = PROFILES[profile]
    targets = [(path / 'scaling_governor', cpu_governor) for path in cpu] + [(gpu / 'governor', gpu_governor)]
    snapshot = [(path, path.read_text().strip()) for path, value in targets]
    try:
        for path, value in targets:
            path.write_text(value + '\n')
            if path.read_text().strip() != value:
                raise RuntimeError('governor_readback_failed')
        if persist:
            save_config({'system.cpugovernor': cpu_governor, 'system.gpuperf': gpu_governor,
                         'system.odin.performance': profile})
    except Exception:
        for path, value in reversed(snapshot):
            path.write_text(value + '\n')
        raise
    return performance_status()
