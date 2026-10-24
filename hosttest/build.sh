#!/bin/bash
# Izriše postavitve barvnega zaslona na računalniku (LovyanGFX, platforma framebuffer).
# Najprej enkrat: pio run (prenese knjižnici LovyanGFX in U8g2).
# Uporaba: ./build.sh && ./host <postavitev 0..11> [tema 0..11] [logotip 0/1] [viz] -> out_*.ppm
set -e
cd "$(dirname "$0")"
for f in app config player stations diag spectrum logo flashguard display_tft; do cp ../src/$f.h .; done
python3 - <<'PY'
s=open('../src/display_tft.cpp').read()
a=s.index('class LGFX_ILI9341'); b=s.index('static LGFX_ILI9341* tft')
s=s[:a]+'class LGFX_ILI9341 : public lgfx::LGFX_Device {};\n'+s[b:]
a=s.index('static void push(bool full)')
s=s[:a]+'''
LGFX_Sprite* hostSprite() { if (!spr) { spr = new LGFX_Sprite(); spr->setColorDepth(16); spr->createSprite(W, H); spr->setTextDatum(lgfx::textdatum_t::baseline_left); spr->setTextWrap(false); initColors(); } return spr; }
void hostDraw(int layout, int theme, const char* toast) { hostSprite(); applyTheme(theme); cfg.layout = layout; PlayerStatus ps = playerStatus(); ui.screen = SCR_MAIN; drawMain(ps, toast); }
'''
open('dt.cpp','w').write(s)
PY
L=$(ls -d ../.pio/libdeps/*/LovyanGFX)/src; U=$(ls -d ../.pio/libdeps/*/U8g2)/src/clib
FL="-O1 -std=c++17 -DLGFX_LINUX_FB -DLGFX_USE_V1 -Ishim -I. -I$L -I$U -w"
mkdir -p obj
for f in $(find $L/lgfx -name "*.cpp" | grep -v "/platforms/"); do o=obj/$(echo $f | md5sum | cut -c1-8).o; [ -f $o ] || g++ $FL -c $f -o $o; done
for f in $(find $L/lgfx -name "*.c"); do o=obj/c_$(echo $f | md5sum | cut -c1-8).o; [ -f $o ] || gcc -O1 -w -I$L -c $f -o $o; done
[ -f obj/u8fonts.o ] || gcc -O1 -c $U/u8g2_fonts.c -o obj/u8fonts.o
g++ $FL -c dt.cpp -o obj_dt.o && g++ $FL main.cpp obj_dt.o obj/*.o -o host -lpthread
