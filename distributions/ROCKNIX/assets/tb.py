# 번역 관련 툴
import re

def parse_po_blocks(filepath):
    with open(filepath, encoding="utf-8") as f:
        content = f.read()

    blocks = re.findall(r'(msgid.*?msgstr.*?)(?=\n\n|\Z)', content, re.S)

    db = {}

    for block in blocks:
        msgid_match = re.search(r'msgid\s+((?:"[^"]*"\s*)+)', block, re.S)
        if not msgid_match:
            continue

        msgid = "".join(re.findall(r'"(.*?)"', msgid_match.group(1)))

        db[msgid.strip()] = block  # 블록 그대로 저장

    return db


def apply_blocks(db, target_path, output_path):
    with open(target_path, encoding="utf-8") as f:
        content = f.read()

    def replace_block(match):
        block = match.group(0)

        msgid_match = re.search(r'msgid\s+((?:"[^"]*"\s*)+)', block, re.S)
        if not msgid_match:
            return block

        msgid = "".join(re.findall(r'"(.*?)"', msgid_match.group(1))).strip()

        if msgid in db:
            return db[msgid]  # 원본 블록 그대로 교체

        return block

    new_content = re.sub(
        r'(msgid.*?msgstr.*?)(?=\n\n|\Z)',
        replace_block,
        content,
        flags=re.S
    )

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(new_content)


# 사용
db = parse_po_blocks("emulationstation2.po")
apply_blocks(db, "emulationstation2.pot", "output.po")