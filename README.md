# ZRadio

Internetni radio za **ESP32-S3 N16R8** + **PCM5102** (+ PAM8403) + barvni TFT **ILI9341 2,4"** ali OLED,
z enkoderji, IR daljincem in spletnim panelom v slovenščini.
Vsi pini, zaslon in vhodi se nastavljajo v spletnem panelu – brez ponovnega prevajanja.

## Funkcije (v0.1)

- Predvajanje MP3 / AAC / AAC+ / FLAC / Opus / Vorbis, HTTP in HTTPS, M3U/PLS/HLS (knjižnica ESP32-audioI2S 3.4.7)
- Samodejna ponovna povezava ob prekinitvi toka (2 s → 30 s)
- OLED **SSD1306 0,96"**, **SH1106 1,3"**, **SSD1309 2,42"** (I2C) – tip in pini v panelu
- Zasloni: predvajanje (postaja, izvajalec / pesem, glasnost, kodek, ura), glasnost, seznam postaj, ura v pripravljenosti, ohranjevalnik zaslona, nočni kontrast
- 2 enkoderja, do 4 tipke, IR daljinec NEC z učenjem tipk
- Spletni panel: predvajalnik, urejevalnik postaj (uvoz CSV/M3U/PLS/JSON, izvoz, iskanje po radio-browser.info),
  **strojna oprema s predlogom pinov, zaklepanjem in preverjanjem konfliktov**, risba plošče, tabela vezave, EQ, WiFi, OTA, varnostna kopija
- **Varni način**: po 3 neuspelih zagonih se zažene brez periferije, samo s spletnim panelom
- Brez WiFi: dostopna točka `ZRadio-xxxx` / `zradio1234` s captive portalom na 192.168.4.1
- mDNS: `http://zradio.local`

## Privzeti pini

| Funkcija | GPIO |
|---|---|
| PCM5102 BCK / LCK / DIN | 21 / 15 / 47 |
| OLED SDA / SCL | 8 / 9 |
| Enkoder 1 A / B / SW | 4 / 5 / 6 |
| Enkoder 2 A / B / SW | 16 / 17 / 18 (privzeto izklopljen) |
| IR | izklopljen (predlog: 7) |

PCM5102: `SCK` → GND, mostički H1L/H2L/H4L = L, H3L (XSMT) = H. OLED in enkoderji na **3V3**, PAM8403 na **5 V**.

### Nasveti za strojno opremo (iz izkušenj uporabnikov seba131)

- **100 nF kondenzator** med VIN in GND tik ob PCM5102 – pri enem uporabniku je to odpravilo tišino na izhodu.
- **Napajalnik vsaj 5 V / 2 A.** Šibek polnilec povzroča ponovne zagone pri glasnejšem predvajanju.
- Če je DAC tih kljub pravilni vezavi in mostičkom, preizkusi drug ESP32-S3 – pri enem uporabniku je bil kriv modul.
- Izogibaj se postajam HE-AAC z zelo nizko bitno hitrostjo (~45 kb/s), ki lahko prasketajo.

## Upravljanje

| | Vrtenje | Klik | Dolg pritisk |
|---|---|---|---|
| Enkoder 1 | glasnost | utišaj | vklop / pripravljenost |
| Enkoder 2 | brskanje po postajah | predvajaj izbrano | samo priljubljene |
| Samo enkoder 1 | glasnost / brskanje | odpri seznam / predvajaj | vklop / pripravljenost |

## Namestitev

**Najhitreje (brez PlatformIO):** odpri <https://espressif.github.io/esptool-js/> v Chrome/Edge, *Connect*,
najprej **Erase Flash**, nato dodaj `dist/zradio-full.bin` na naslov **0x0** in *Program*.

**PlatformIO:**
```
pio run -t upload
pio device monitor
```
Uporablja platformo *pioarduino* (Arduino core 3.3.x), ker ESP32-audioI2S 3.x zahteva core 3.

**Posodobitev:** panel → Sistem → naloži `zradio-ota.bin` (nastavitve ostanejo).

**Spletni instalator:** ob objavi oznake `v*` na GitHubu workflow zgradi firmware in objavi stran `docs/` na GitHub Pages
(ESP Web Tools – namestitev z enim klikom, kot pri seba131).

## Spremembe

**0.7.1**
- Nov zavihek **Videz**: postavitve kot kartice s sličicami, teme kot barvni vzorci, klik takoj uveljavi;
  možnosti posamezne postavitve (prikaz zvoka, ozadje, levi obroč) se pokažejo le, ko veljajo

**0.7.0** – šest novih postavitev barvnega zaslona
- Gramofon (vrteča plošča z logotipom, ročica), Kaseta (koluta, VU), Sintwave 80' (neonsko sonce, premikajoča mreža, spekter),
  Nixie ura + VFD, Krožni spekter okoli logotipa, Ambient (zamegljeno ozadje iz barv logotipa, steklena kartica)
- Pomišljaj v naslovih se izriše kot »-« (»–« ni v pisavah)
- `hosttest/`: izris postavitev na računalniku z isto knjižnico (LovyanGFX, framebuffer)

**0.6.x**
- 12 barvnih tem, nočna tema, postavitve Klasična, Kartica, Radijska skala, Velika ura, Celozaslonski, Obroči (slog Volumio,
  ozadja nebo/morje/zahod/barve teme/lastna slika, levi obroč: čas pesmi/ura/čas poslušanja/medpomnilnik)
- Prikaz zvoka: VU, spekter (FFT 1024 v PSRAM), zrcalni spekter, analogna merilnika z iglama, osciloskop
- Manj utripanja: spremenjeni pasovi se pošljejo v enem kosu, neobvezno SPI 80 MHz

**0.5.2 – 0.5.7** – logotipi
- Logotipi na sredini, opravilo za prenos z nizko prioriteto, TLS v notranjem RAM-u, varovalo prostega pomnilnika,
  posrednik za slike, ki jih radio ne zna dekodirati (pretvorba v brskalniku)

**0.5.1** – logotipi postaj
- Logotipi 64x64 na glavnem zaslonu (levo od naslova) in ikone 24x24 v seznamu postaj (brez logotipa: začetnica)
- Samodejni prenos s radio-browser.info ob dodajanju postaje, gumb ⟳ pri postaji in »Poišči manjkajoče logotipe«;
  radio prenese in pretvori PNG, JPEG ali ICO s PNG (SVG, WebP in progresivni JPEG niso podprti)
- Lastna slika: klik na sličico v seznamu postaj (pretvorba v brskalniku)
- Virtualni enkoder v zavihku Zaslon (preizkus menija na daljavo)

**0.5.0**
- Glasnost kot okno čez glavni zaslon (barvni zaslon), mehki prehodi glasnosti ob preklopu/ustavitvi/utišanju
- Meni na radiu (dolg pritisk enkoderja 2): časovnik izklopa, izenačevalnik, prikaz zvoka, svetlost, priljubljene, pripravljenost

**0.4.0** – barvni zaslon
- Podpora za TFT ILI9341 320x240 (SPI) prek LovyanGFX: glava s postajo, izvajalec in pesem (drseče),
  segmentni merilnik VU z zadrževanjem vrha, glasnost, kodek/bitna hitrost, ura z utripajočim dvopičjem;
  zasloni za glasnost, brskanje (7 vrstic), pripravljenost (velika ura in datum), ohranjevalnik, informacije
- Slika se riše v PSRAM, na zaslon gredo le spremenjeni pasovi po 16 vrstic
- Svetlost osvetlitve (PWM) prek nastavitev »kontrast/svetlost« podnevi in ponoči
- Pini SCK, MOSI, CS, DC, RESET, LED nastavljivi v panelu; predogled TFT v zavihku Zaslon

**0.3.1**
- OLED: merilnik VU, opozorilo ob praznem medpomnilniku, utripajoče dvopičje, pošiljanje le spremenjenih vrstic
- Panel: živ predogled zaslona; privzeta ponovna inicializacija zaslona vsakih 10 min

**0.3.0** – stabilnost predvajanja
- Knjižnica ESP32-audioI2S 3.4.4 je v `lib/` s popravki: `stopSong()` in ponovna povezava, sproženi iz
  dekodirnega opravila (napaka dekoderja, MP3 »Network error«), se ne izvedeta več sredi dekodiranja,
  ampak odloženo v `loop()`; čakanje na ključavnico dekodiranja brez časovne omejitve 0,3 s.
  To je odpravilo naključna sesutja (INT_WDT / TG1WDT, pokvarjen heap) ob preklopu postaj.
- Povratni klic knjižnice le kopira sporočilo v vrsto; obdela ga opravilo predvajalnika (nizka prioriteta, 12 KB sklada)
- Sklad dekodirnega opravila 6 KB (izvirno 3500 B – na ESP-IDF so to bajti, ne besede)
- Pisanje v flash: merjenje trajanja (Diagnostika → »Najdaljše pisanje v flash«), med pisanjem
  INT_WDT začasno izklopljen, varovalo RTC watchdog 8 s
- Stanje (postaja, glasnost) se shrani 30 s po zadnji spremembi
- Dnevnik: namesto podrobnega izpisa knjižnice ena vrstica o toku (kodek, frekvenca, bitna hitrost) in napake
- Tabela enkoderja v DRAM; odstranjen nedelujoč tok Radio Veseljak iz privzetih postaj

**0.2.1 – 0.2.4**
- LittleFS z oznako particije `littlefs`; RSSI izven kritične sekcije (INT_WDT ob povezavi WiFi);
  zagonska slika z glavo DIO (`--flash-mode keep`)

**0.2.0**
- Nov zavihek **Diagnostika**: zgodovina zadnjih 12 zagonov z razlogom (vklop, programski, padec napetosti, sesutje, watchdog)
  in nasvetom, stanje tik pred ponovnim zagonom (čas delovanja, RAM, postaja, glasnost, WiFi, zadnji dogodek),
  povzetek sesutja iz coredump (opravilo, vzrok, PC, sklad), števci napak, rezerve skladov opravil,
  kontrolne točke pomnilnika z grafom ter dnevnik dogodkov, ki preživi ponovni zagon (RTC pomnilnik)
- Opozorilo v panelu, če zadnji zagon ni bil normalen ali radio teče v varnem načinu, z razlogom varnega načina
- `dist/zradio.elf` za dekodiranje sklada sesutja:
  `xtensa-esp32s3-elf-addr2line -pfiaC -e zradio.elf 0x4201a2b1 0x4201c7f0 …`

**0.1.1**
- OLED: daljša zakasnitev ob zagonu in druga inicializacija po 2 s (črn zaslon po hladnem zagonu),
  samodejna ponovna inicializacija, ko zaslon spet odgovori na I2C, neobvezno periodično osveževanje krmilnika
- Čuvaj toka: če je medpomnilnik 10 s prazen, se radio ponovno poveže (tihi zastoj)
- Prvi dotik med ohranjevalnikom zaslona samo prebudi zaslon
- Napotki za napajanje in 100 nF kondenzator v tabeli vezave

## Struktura

```
src/config.*    nastavitve (JSON v LittleFS), tabela pinov ESP32-S3, preverjanje
src/player.*    predvajalnik v lastnem opravilu (jedro 1), ukazi prek vrste
src/input.*     enkoderji (prekinitve, tabela stanj), tipke, IR NEC dekoder
src/display.*   OLED zasloni (U8g2, pisave s šumniki)
src/app.*       logika (glasnost, postaje, pripravljenost, brskanje)
src/web.*       REST API + vgrajen panel (web/index.html → gzip v firmware)
```

## API

`GET /api/status`, `POST /api/cmd?a=next|prev|vol_up|vol_down|mute|power|play_stop|fav` ali `?play=3` ali `?vol=12`,
`GET/POST /api/stations`, `GET/POST /api/config`, `POST /api/config/validate`, `GET /api/pins`, `GET /api/info`,
`GET /api/backup`, `POST /api/restore`, `POST /api/ota`, `POST /api/reboot`.

## Preizkus postavitev na računalniku

```
pio run                      # prenese LovyanGFX in U8g2
hosttest/build.sh
cd hosttest && ./host 8 0 1 spectrum   # postavitev, tema, logotip, prikaz zvoka -> out_8_0_1.ppm
```

## Načrtovano

- MQTT + Home Assistant autodiscovery
- Barvni TFT (ST7789 / ILI9341 / ILI9488) prek LovyanGFX – tip in pini v panelu
- VU-meter na OLED, budilka / časovnik izklopa
