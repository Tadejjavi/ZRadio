# ZRadio – zgodovina sprememb

Delovni dnevnik razvoja: kaj je bilo narejeno, katere težave so se pojavile in kako so bile rešene.

**0.7.3**
- IR daljinec je deloval nezanesljivo (večkrat pritisniti). Dekoder NEC je prestavljen s prekinitve na padajoče robove
  na enoto RMT: strojno merjenje impulzov (1 µs), filter motenj < 3 µs, širše tolerance, lastno opravilo.
  Prej je en sam zgrešen ali moten rob zavrgel cel ukaz in vse njegove ponovitve.
- Upravljanje: ob zadnji kodi števec prejetih in zavrženih okvirjev (motnje / drug protokol)
- Nove akcije za daljinec in tipke: meni (odpri/potrdi), kanal −/+ (v meniju gor/dol, sicer postaja), izenačevalnik,
  časovnik izklopa, številke 0–9 (neposredna izbira postaje, npr. 1, 2 → postaja 12; potrditev po 1,5 s)
- Gumb »Naloži kode za daljinec Car mp3«: vseh 21 tipk naenkrat
  (CH meni, CH−/CH+ gor/dol, PREV/NEXT postaja, PLAY, VOL−/+, EQ, 100+ časovnik, 200+ samo priljubljene, 0–9)
- V seznamu postaj CH ali PLAY potrdi izbrano postajo

**0.7.2**
- Panel: zamenjava teme/postavitve je trajala ~10 s (tudi na zaslonu). Popravki: predogled in stanje se ne pošiljata
  več, dokler prejšnja zahteva ni odgovorjena (prej se je lahko nabrala vrsta), med shranjevanjem premor;
  izbira se v panelu označi takoj, obvestilo pokaže čas odziva radia
- Zaslon: pošiljanje slike po SPI teče brez aplikacijskega zaklepa (splet ni več blokiran med pošiljanjem)
- Dnevnik: »Nastavitve shranjene (zaklep X ms, zapis Y ms)« za iskanje zakasnitev
- Postavitve, vezane na zvok, so delovale počasi: Krožni spekter riše izseke s trikotniki namesto `fillArc`
  (izris ~7x hitrejši), Gramofon ima ploščo v predpomnilniku in etiketo prek male slike z okroglo masko (~4,6x)
- Izzvenevanje spektra, vrhov, VU in gibanje ročice so vezani na čas, ne na število slik (pri nižji hitrosti
  osveževanja niso več »leni«)
- Panel (Videz, Zaslon): izmerjena hitrost osveževanja in čas izrisa/pošiljanja pod predogledom

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

**0.4.x**
- Knjižnica ESP32-audioI2S 4.0.0 je prinesla nove težave – vrnjeno na 3.4.4 s popravki.
- Opozorilo medpomnilnika po času (< 1,5 s) namesto po odstotkih.

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

**0.2.5 – 0.2.9** – iskanje vzroka sesutij ob predvajanju
- Preizkušene in ovržene hipoteze: premajhen sklad, napajanje, brisanje flasha.
  Pravi vzrok: `stopSong()` in ponovna povezava, sprožena iz dekodirnega opravila knjižnice (popravek v 0.3.0).

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

**Znane omejitve in opombe**
- TLS (mbedTLS) mora ostati v notranjem RAM-u – dodeljevanje v PSRAM je povzročalo sesutja WiFi (ppTask), 0.5.3 → 0.5.5.
- Opravilo za logotipe ima prioriteto 0 (ECDH pri TLS je sicer stradal IDLE0 → TASK_WDT), 0.5.6.
- Pisave u8g2 `_te` nimajo znaka »–«, zato se v naslovih uporablja »-«.
