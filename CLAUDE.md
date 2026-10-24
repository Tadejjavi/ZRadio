# ZRadio – navodila za delo na projektu

Internetni radio na ESP32-S3 N16R8 (16 MB flash, 8 MB OPI PSRAM), TFT ILI9341 320x240 (SPI), PCM5102 + PAM8403,
dva enkoderja. Lastnik: Tadej. Pogovor, komentarji v kodi, besedila v panelu in na zaslonu so v **slovenščini**.
Odgovori naj bodo kratki in tehnično natančni; Tadej preverja trditve – ne ugibaj o vzrokih, povej, kaj je preverjeno.

## Gradnja in izdaja

```
pio run                                   # env esp32s3, pioarduino 55.03.312-1 (Arduino 3.3.x / IDF 5.5)
```
Združena slika za prvo namestitev (iz korena projekta):
```
B=.pio/build/esp32s3
~/.platformio/penv/bin/python -m esptool --chip esp32s3 merge-bin -o dist/zradio-full.bin \
  --flash-mode keep --flash-freq keep --flash-size 16MB 0x0 $B/bootloader.bin 0x8000 $B/partitions.bin \
  0xe000 $(find ~/.platformio/packages/framework-arduinoespressif32* -name boot_app0.bin | head -1) 0x10000 $B/firmware.bin
cp $B/firmware.bin dist/zradio-ota.bin; cp $B/firmware.elf dist/zradio.elf
```
- `--flash-mode keep` je obvezen (zagonska slika z glavo DIO).
- Ob vsaki spremembi firmwara povečaj `ZR_VERSION` v `platformio.ini` in dodaj vnos v `CHANGELOG.md`.
- Spletni panel `web/index.html` se ob gradnji stisne v firmware (`tools/embed_web.py`). Po spremembi preveri JS:
  izlušči `<script>` in poženi `node --check`.
- Tadeju pošlji `dist/zradio-ota.bin` (posodobitev prek panela → Sistem).
- **GitHub** (`Tadejjavi/ZRadio`): seja sme potiskati le veje → potisni na `main`. Oznake `v*` ne moreš potisniti;
  Tadej ustvari Release z oznako (npr. `v0.7.2`) na GitHubu, workflow `.github/workflows/build.yml` zgradi
  Release (`zradio-full.bin`, `zradio-ota.bin`, `zradio.elf`) in GitHub Pages namestilnik iz `docs/`.
- Sklad sesutja (Diagnostika v panelu): `xtensa-esp32s3-elf-addr2line -pfiaC -e dist/zradio.elf 0x… 0x…`
  (`~/.platformio/packages/toolchain-xtensa-esp-elf/bin/`).

## Preizkus zaslona brez naprave

`hosttest/build.sh` prevede `src/display_tft.cpp` z LovyanGFX za Linux (framebuffer) in nadomestki za Arduino.
```
hosttest/build.sh && cd hosttest && ./host <postavitev 0..11> [tema 0..11] [logotip 0/1] [viz]   # -> out_*.ppm
```
Vsako spremembo postavitev najprej preveri z izrisom. Sličice postavitev v zavihku Videz (`const LTH` v
`web/index.html`) so JPEG 160x120 iz teh izrisov – ob spremembi postavitve jih ponovno ustvari.

## Pravila, ki izhajajo iz napak (ne spreminjaj brez razloga)

- **Knjižnica za zvok** `lib/ESP32-audioI2S` (3.4.4, oznaka `3.4.4-zr2`) je popravljena: `stopSong()` / ponovna
  povezava iz dekodirnega opravila sta odložena v `loop()` (`m_zrDeferStop`, `m_zrDeferRestart`), čakanje na mutex
  brez omejitve, sklad dekoderja 6144 B (na IDF so enote bajti), šibka kljuka `zr_pcm_tap()` za spekter.
  To je odpravilo sesutja INT_WDT/TG1WDT. Verzija 4.0.0 je imela regresije – ne nadgrajuj.
- **TLS (mbedTLS) ostane v notranjem RAM-u** – dodeljevanje v PSRAM je sesuvalo WiFi (ppTask).
- Opravilo za logotipe: prioriteta 0, jedro 0, sklad 16 KB (ECDH je sicer stradal IDLE0 → TASK_WDT);
  prenos le, če je največji prosti blok notranjega RAM-a ≥ 24 KB.
- Veliki medpomnilniki (slika zaslona, ozadja `bgSpr`, FFT, predpomnilnik logotipov) so v PSRAM; notranji RAM je tesen
  (cilj: prosto ~120 kB, stabilno). Samodejni ponovni zagon ob < 20 KB prostega ali < 8 KB največjega bloka 60 s.
- Pisanje v flash teče skozi `FlashGuard` (INT WDT začasno izklopljen, RTC WDT 8 s).
- Pisave u8g2 `_te` nimajo »–« → v besedilih na zaslonu uporabljaj »-«.
- Sprite na napravi ima `textdatum = baseline_left`; koordinate y pri `drawString` so osnovnica.
- Brez Bluetootha (S3 nima klasičnega BT).

## Zgradba zaslona (`src/display_tft.cpp`)

- Postavitve 0–11 v `drawMain()`; `cfg.layout` (0..11), meni na radiu `LAYOUT_LBL` v `app.cpp`, panel zavihek Videz.
- 12 tem v tabeli `THEMES`; barve `C_*` nastavi `applyTheme()`. Nočna tema prek `cfg.night_theme`.
- `bgSpr` (PSRAM) je skupni predpomnilnik ozadja: `bgBuilt` 0–4 Obroči, 100 Sintwave, 101 Ambient, 102 Nixie.
- `push()` pošlje le spremenjene pasove po 8 vrstic, združene v en `pushImage`. Animirane postavitve: 50 ms.
- `spectrumEnable()` v `tftUpdate()` – vklopi FFT le za postavitve/prikaze, ki ga rabijo.

## Strojna oprema (Tadejeva vezava)

I2S BCK 21 / LCK 15 / DIN 47 · TFT SCK 12, MOSI 11, CS 10, DC 9, RST 14, LED 2, VCC 3V3 ·
enkoder 1: 4/5/6 · enkoder 2: 16/17/18. Napajanje: 12 V → stikalo → buck 5 V (sponke: PAM8403, TFT, PCM5102;
USB-A: ESP32). Pin 5V na ploščici je le vhod. Ohišje (OpenSCAD) se dela v ločenem pogovoru.
