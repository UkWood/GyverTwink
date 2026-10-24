#!/usr/bin/env python3
"""Сборка веб-интерфейса в прошивку: web/index.html + style.css + app.js -> gzip -> firmware/.../web_page.h

  python3 tools/gen_web.py          пересобрать web_page.h
  python3 tools/gen_web.py --check  проверить, что web_page.h соответствует исходникам (для CI)

Проверка сравнивает РАСПАКОВАННОЕ содержимое: байты gzip могут отличаться между версиями zlib.
"""
import gzip
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
OUT = os.path.join(ROOT, "firmware", "GyverTwink_v1.2", "web_page.h")

# какие файлы подставляются в index.html: маркер -> имя файла
INLINE = {"/*@style.css*/": "style.css", "//@calib.js": "calib.js", "//@hexmap.js": "hexmap.js", "//@app.js": "app.js"}


def read(name):
    with open(os.path.join(WEB, name), encoding="utf-8") as f:
        return f.read()


def build_html():
    html = read("index.html")
    for marker, fname in INLINE.items():
        if marker not in html:
            sys.exit("в index.html нет маркера " + marker)
        html = html.replace(marker, read(fname).replace("\r\n", "\n"))
    for js in ("app.js", "calib.js", "hexmap.js"):
        if "</script>" in read(js):
            sys.exit(js + " не должен содержать </script>")
    return html.replace("\r\n", "\n")


def to_header(raw, gz):
    lines = []
    for i in range(0, len(gz), 16):
        lines.append("  " + ", ".join("0x%02x" % b for b in gz[i:i + 16]) + ",")
    return (
        "// Сгенерировано tools/gen_web.py из web/. Не править руками.\n"
        "#pragma once\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        "// страница gzip: %d байт (исходная %d байт)\n"
        "const size_t WEB_PAGE_GZ_LEN = %d;\n"
        "const uint8_t WEB_PAGE_GZ[] PROGMEM = {\n%s\n};\n" % (len(gz), len(raw), len(gz), "\n".join(lines))
    )


def parse_header(text):
    body = text[text.index("WEB_PAGE_GZ[]"):]
    body = body[body.index("{") + 1:body.rindex("}")]
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))


def main():
    raw = build_html().encode("utf-8")
    if "--check" in sys.argv:
        try:
            with open(OUT, encoding="utf-8") as f:
                have = gzip.decompress(parse_header(f.read()))
        except (OSError, ValueError) as e:
            sys.exit("web_page.h не читается: %s" % e)
        if have != raw:
            sys.exit("web_page.h устарел: запусти python3 tools/gen_web.py")
        print("web_page.h актуален (%d байт страницы)" % len(raw))
        return
    gz = gzip.compress(raw, 9, mtime=0)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(to_header(raw, gz))
    print("%s: %d байт -> %d байт gzip" % (os.path.relpath(OUT, ROOT), len(raw), len(gz)))


if __name__ == "__main__":
    main()
