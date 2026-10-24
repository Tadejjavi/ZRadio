# Pred prevajanjem stisne web/index.html (gzip) v src/web_index.h
import gzip, os
Import("env")

root = env.subst("$PROJECT_DIR")
src = os.path.join(root, "web", "index.html")
dst = os.path.join(root, "src", "web_index.h")

def build():
    data = open(src, "rb").read()
    gz = gzip.compress(data, compresslevel=9, mtime=0)
    lines = []
    for i in range(0, len(gz), 20):
        lines.append(",".join("0x%02x" % b for b in gz[i:i + 20]))
    out = ("// Samodejno ustvarjeno iz web/index.html – ne urejaj ročno\n#pragma once\n#include <Arduino.h>\n"
           "static const uint8_t WEB_INDEX_GZ[] PROGMEM = {\n" + ",\n".join(lines) + "\n};\n"
           "static const size_t WEB_INDEX_GZ_LEN = %d;\n" % len(gz))
    if not os.path.exists(dst) or open(dst).read() != out:
        open(dst, "w").write(out)
    print("web/index.html: %d B -> gzip %d B" % (len(data), len(gz)))

build()
