# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET
from odin_hardware import require_odin

SOURCE = Path('/usr/config/modules')
TARGET = Path('/storage/.config/modules')
NAME = 'Odin LED Settings.sh'


def register_tools():
    require_odin()
    TARGET.mkdir(parents=True, exist_ok=True)
    launcher = TARGET / NAME
    source = SOURCE / NAME
    if not launcher.exists() or launcher.read_bytes() != source.read_bytes():
        shutil.copyfile(source, launcher)
    launcher.chmod(0o755)
    gamelist = TARGET / 'gamelist.xml'
    original = gamelist.read_bytes() if gamelist.exists() else b''
    valid = True
    try:
        root = ET.fromstring(original)
        if root.tag != 'gameList':
            raise ET.ParseError('invalid_root')
    except ET.ParseError:
        valid = False
        if original:
            backup = TARGET / ('gamelist.xml.before-odin-' + hashlib.sha256(original).hexdigest()[:12])
            if not backup.exists():
                backup.write_bytes(original)
        defaults = (SOURCE / 'gamelist.xml').read_bytes()
        try:
            root = ET.fromstring(defaults)
            if root.tag != 'gameList':
                raise ET.ParseError('invalid_default_root')
        except ET.ParseError:
            root = ET.Element('gameList')
            for fragment in re.findall(rb'<game(?:\s[^>]*)?>.*?</game>', defaults, re.S):
                try:
                    root.append(ET.fromstring(fragment))
                except ET.ParseError:
                    continue
        known = {game.findtext('path') for game in root.findall('game')}
        for fragment in re.findall(rb'<game(?:\s[^>]*)?>.*?</game>', original, re.S):
            try:
                game = ET.fromstring(fragment)
            except ET.ParseError:
                continue
            if game.findtext('path') not in known:
                root.append(game)
                known.add(game.findtext('path'))
    entry = next((game for game in root.findall('game') if game.findtext('path') == './' + NAME), None)
    if entry is None:
        entry = ET.SubElement(root, 'game')
        ET.SubElement(entry, 'path').text = './' + NAME
    fields = {'name': 'Odin LED 설정', 'desc': '스틱·측면 LED 켜기와 깜빡임·좌우 왕복 효과를 설정합니다. 켜기는 효과 없이 켜집니다. A/B 확인, Select+Start 종료와 터치를 지원하며 자동 저장됩니다.'}
    changed = False
    for key, value in fields.items():
        field = entry.find(key)
        if field is None:
            field = ET.SubElement(entry, key)
        if field.text != value:
            field.text = value
            changed = True
    if not changed and original and valid:
        return
    ET.indent(root, space='  ')
    temporary = gamelist.with_suffix('.xml.odin-new')
    ET.ElementTree(root).write(temporary, encoding='utf-8', xml_declaration=True)
    temporary.replace(gamelist)
