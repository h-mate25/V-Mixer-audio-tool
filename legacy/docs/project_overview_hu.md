# Windows REAC ASIO - Projektleiras es celkituzes


## Rovid osszefoglalo

A Windows REAC ASIO projekt celja egy 64 bites Windows ASIO driver DLL
letrehozasa, amely Roland REAC/H-8819 nyers Ethernet audiostreamet fogad,
valos idoben dekodol, majd 40 csatornas virtualis ASIO input hanginterfeszkent
teszi elerhetove DAW-ok es ASIO-kompatibilis alkalmazasok szamara.

A driver a host alkalmazasokban `Windows REAC ASIO` neven jelenik meg. A
vegso cel az, hogy egy REAC-kompatibilis kevero vagy stagebox halozati
audiostreamje kozvetlenul felveheto es monitorozhato legyen Windows alatt,
kulon fizikai audiointerface vagy vendor driver nelkul.

## Fo celkituzes

Olyan stabil, alacsony kesleltetesu, 40 bemeneti csatornas virtualis ASIO
driver keszitese, amely:

- Windows 64 bit alatt DLL-kent telepul es ASIO driverkent regisztralhato.
- FL Studio, Reaper es mas ASIO hostok alatt kivalaszthato.
- Legfeljebb 40 mono input csatornat jelent a host fele, konfiguraciotol
  fuggoen akar 8/16/24/32/40 lathato bemenettel.
- Roland REAC/H-8819 nyers Ethernet frame-eket fogad Npcap segitsegevel.
- A REAC 24 bites PCM mintakat float ASIO input bufferebbe konvertalja.
- Halozati kimaradaskor csendet ad vissza, nem omlik ossze.
- Stream visszatereskor ujraszinkronizal.
- Az ASIO callbackben nem vegez halozati I/O-t, fajl I/O-t, blokkolast vagy memoriafoglalast.

## Technikai hatter

A REAC stream nem UDP/IP forgalom, hanem nyers Ethernet. A jelenleg tamogatott
audio packet formatum:

- EtherType: `0x8819`
- Mintavetel: 48 kHz
- Mert packet rata: kb. 4000 packet/masodperc
- Mintak packetenkent: 12 sample/channel
- Csatornak: 40
- Sample formatum: signed 24-bit PCM
- Dekodolt ASIO formatum: 32-bit float little-endian
- Audio payload offset: 50 byte az Ethernet frame elejetol
- Packet trailer: `C2 EA`
- Az eddig latott alap packet type `0x0000` a 16..17 byte-on.
- A 2026-05-21-i kevero + digital snake capture-ben idonkent `0xEACD` es
  `0xEACF` packet type is megjelent. A FreeREAC iranyabol jott tanulsag
  alapjan a driver az ervenyes audio payloadot packet type-tol fuggetlenul
  dekodolja; a packet type jelenleg metadata/diagnosztika.

A packet layoutot a `norihiro/obs-h8819-source`, `per-gron/reacdriver` es
`ManuKapel/REAC-driver-Windows-Mac` projektek alapjan ellenoriztuk. A
legutobbi implementacios iranynal a norihiro-fele OBS h8819 receiver volt az
elsodleges minta.

2026-05-21-i aktualis meresi matrix:

| Capture | Allapot | Lathato forras | Meret | Csatorna | Packet type |
| --- | --- | --- | --- | --- | --- |
| `<historical-capture>.pcapng` | S-1608 Slave only | `02:00:00:00:00:01` | 340 | 8 | csak `0x0000` |
| `<historical-capture>.pcapng` | S-1608 Split only | `02:00:00:00:00:01` | 340 | 8 | csak `0x0000` |
| `<historical-capture>.pcapng` | S-1608 Master only | `02:00:00:00:00:01` | 340 | 8 | csak `0x0000` |
| `<historical-capture>.pcapng` | Kevero only | `02:00:00:00:00:02` | 340 -> 1492 | 8 -> 40 | `0x0000`, `0xEACD`, `0xEACF` |
| `<historical-capture>.pcapng` | Kevero + S-1608 Slave | `02:00:00:00:00:02` | 340 -> 1492 | 8 -> 40 | `0x0000`, `0xEACD`, `0xEACF` |
| `<historical-capture>.pcapng` | Kevero + S-1608 Master | `02:00:00:00:00:02` | 340 -> 1492 | 8 -> 40 | `0x0000`, `0xEACD`, `0xEACF` |
| `<historical-capture>.pcapng` | Kevero + S-1608 Split | `02:00:00:00:00:02` | 340 -> 1492 | 8 -> 40 | `0x0000`, `0xEACD`, `0xEACF` |

A szurt capture-ekben minden esetben csak egy broadcast ado latszik. Ha a
kevero is a halozaton van, a lathato ado a kevero MAC-je. Ha csak a snake van
a PC-re kotve, a lathato ado a snake MAC-je. Ez nem zarja ki, hogy a kevero es
snake kozott unicast vagy mas EtherType-on meno return/control forgalom is
letezik; egy normal switch/PC port ezt nem feltetlenul latja.

A keveros capture-ekben az `0xEACF` header 8 csatornas fazisban a forras MAC
utan `08 08`, 40 csatornas fazisban `28 10` mintat mutat. Ez erosen
eszkoz-/kapacitas-hirdetesnek tunik, de meg nem bizonyitott mezodefinicio.

2026-05-21-i kevero + digital snake meres:

- 40199 `0x8819` packet 10.0498634 masodperc alatt.
- Fix 1492 byte-os frame meret, 40 dekodolt csatorna.
- Lathato flow: `02:00:00:00:00:02 -> ff:ff:ff:ff:ff:ff`.
- A packet counter vegig folyamatos volt.
- A szurt capture-ben nem latszott kulon elleniranyu `0x8819` flow.

2026-05-21-i Roland S-1608 snake-only slave mod meres:

- 40187 `0x8819` packet 10.0462666 masodperc alatt.
- Fix 340 byte-os frame meret, 8 dekodolt csatorna.
- Lathato flow: `02:00:00:00:00:01 -> ff:ff:ff:ff:ff:ff`.
- Packet type vegig `0x0000`.
- A 32 byte-os REAC header vegig nulla.
- A packet counter vegig folyamatos volt.
- A szurt capture-ben nem latszott kulon return/control flow.

2026-05-21-i korabbi, nem bazisnak tekintett Roland S-1608 master mod meres:

- 40165 `0x8819` packet 10.0413886 masodperc alatt.
- Fix 340 byte-os frame meret, 8 dekodolt csatorna az audio packeteknel.
- Lathato flow: `02:00:00:00:00:02 -> ff:ff:ff:ff:ff:ff`.
- Packet type megoszlas:
  - `0x0000`: 38862 packet,
  - `0xEACD`: 1293 packet,
  - `0xEACF`: 10 packet.
- A packet counter az audio es nem-audio packeteken at is folyamatos volt.
- A `0xEACF` headerben latszik a forras MAC es utana `08 08`; ez
  valoszinuleg eszkoz-/csatornakapacitas hirdetes, de meg nem bizonyitott
  mezodefinicio.
- A kesobbi, megismetelt snake-only matrix ezt felulirja bazismereskent:
  Slave/Split/Master modban is `02:00:00:00:00:01`, 340 byte, `0x0000`,
  null header latszott.

## Jelenlegi allapot

A projektben mar megvan:

- Visual Studio / MSBuild projekt a telepitett VS18 toolsethez igazitva.
- CMake build konfiguracio.
- 64 bites ASIO DLL build.
- ASIO COM/registry eletciklus:
  - `DllMain`
  - `GetAudioDriver`
  - `DllGetClassObject`
  - `DllCanUnloadNow`
  - `DllRegisterServer`
  - `DllUnregisterServer`
- 40 input csatornas ASIO driver stub.
- 2 monitor output csatorna, amely kulon WASAPI threaden keresztul a Windows
  alapertelmezett fejhallgato/hangszoro kimenetere kuldheto.
- Konfiguralhato `REAC Return` ASIO output csatornak a kesobbi visszairanyu
  REAC kuldes elokeszitesehez. A halozati transmit egyelore tiltott/allvanyozott.
- Belső `AudioEngine`.
- Tobbcatornas `RingBuffer`.
- Teszt szinusz producer, ha nincs REAC interface konfiguralva.
- REAC packet parser.
- Nem-audio REAC packetek felismerese, hogy ezek ne keruljenek hangkent
  dekodolasra.
- Npcap/wpcap dinamikus capture wrapper.
- Npcap/wpcap raw Ethernet sender wrapper kiserleti TX toolhoz.
- REAC receiver thread, amely az audio engine producer oldalara ir.
- `reac_probe.exe` konzolos teszteszkoz.
- `reac_tx_probe.exe` kulon, ovatos nyers Ethernet TX teszteszkoz S-1608
  hardverprobakhoz.
- `reac_packet_test.exe` szintetikus parser teszt.
- Protokolljegyzet: `docs/reac_protocol_notes.md`.
- Alap ASIO Control Panel GUI:
  - REAC adapter valasztas Npcap eszkozlistabol,
  - mintavetel / ASIO buffer / jitter buffer config mezok,
  - lathato ASIO input csatornaszam valasztas,
  - underrun / overflow / packet drop / frame szamlalok,
  - Windows monitor output kapcsolas,
  - driver restart gomb receiver/monitor ujrainditashoz,
  - `config.ini` mentese GUI-bol.

## Fo architektura

```text
DAW / ASIO host
    -> WindowsReacAsioDriver
        -> AudioEngine
            -> RingBuffer
                -> ASIO input buffers

REAC capture thread
    -> Npcap / wpcap raw Ethernet capture
    -> ReacPacketParser
    -> AudioEngine::writeInputInterleaved / writeInputSilence
```

Az ASIO driver nem tud kozvetlenul socketekrol vagy packet capture-rol. A
halozati munka kulon REAC receiver threadben tortenik. Az ASIO callback csak
az `AudioEngine`-tol olvas.

## Fontos implementacios szabalyok

Az ASIO callbackben tilos:

- socket vagy packet capture read,
- fajl I/O,
- hosszu logolas,
- blokkolas,
- lockolt varakozas,
- dinamikus memoriafoglalas,
- barmilyen kiszamithatatlan ideju muvelet.

Underrun eseten a driver csendet ad vissza. Overflow eseten a producer oldali
uj blokk eldobasa az aktualis politika, hogy a consumer oldali ASIO callback
egyszeru es varakozasmentes maradjon.

## Npcap / REAC receiver viselkedes

A norihiro OBS h8819 receiver mintaja alapjan:

- BPF filter: `ether proto 0x8819`
- pcap timeout: 44 ms
- pcap buffer: `4 * 256 KB`
- Windows friendly interface nev feloldas:
  - `\Device\NPF_{GUID}` -> Windows adapter alias
- Az elso 1024 valid packet warm-upkent eldobasra kerul.
- Packet loss a 16 bites REAC counterbol detektalhato.
- Rovid packet-loss gap eseten silence frame-ek kerulnek a ring bufferbe.
- Ismert nem-audio REAC packetnel a receiver egy 12 frame-es csend blokkot ir,
  igy a stream idozitese megmarad, de a metadata nem kerul hangkent lejatszasra.
- Hosszabb kimaradasnal az ASIO oldali underrun silence mechanizmus lep eletbe.

## Konfiguracio

Tervezett es reszben mar hasznalt konfiguracios fajl:

```text
C:\ProgramData\WindowsReacASIO\config.ini
```

Pelda:

```ini
[Audio]
SampleRate=48000
ChannelCount=24
AsioBufferSize=512
JitterBufferSamples=8192

[Network]
Interface=Realtek
Port=5004

[Diagnostics]
EnableDebugLog=1

[Monitor]
EnableWindowsOutput=1
BufferFrames=8192

[Receiver]
ReadOnly=0

[Transmit]
EnableReacOutput=0
ExposeMonoReturns=0
OutputChannels=16
BufferFrames=8192
```

Az `Interface=` uresen hagyva a driver teszt szinuszt general. Ha meg van adva
egy Npcap device index, teljes nev vagy friendly nev reszlet, akkor a driver
elo REAC capture-t probal inditani.

A driver ASIO Control Panel ablaka a fenti fajlba ment. A mentett beallitasok
teljes ervenyesitesehez a DAW-ban ujra meg kell nyitni a drivert, vagy ujra
kell inditani a host alkalmazast.
Az `install\install.bat` letrehozza az alap configot es a normal felhasznalo
szamara irhato config mappat.
Az `EnableWindowsOutput=1` mellett a DAW-ban a `Monitor Out 01/02` ASIO output
par a Windows alapertelmezett lejatszoeszkozen szolal meg. A legstabilabb
eredmenyhez a Windows lejatszoeszkozt 48 kHz formatumra erdemes allitani.
A Control Panel `Restart driver` gombja a REAC receiver es Windows monitor
utvonalat ujrainditja, de a DAW-ok tobbsege a lathato csatornaszamot es ASIO
buffermeretet csak driver-inicializalaskor kerdezi le, ezert ezekhez gyakran
ujra kell nyitni az ASIO eszkozt.
A `Receiver.ReadOnly=1` egy capture-only REAC mod. Ilyenkor a driver csak a
bejovo REAC streamet olvassa es csatornankent ASIO inputkent adja tovabb; a
REAC return transmitter nem indul el, a DAW-ban pedig a `REAC Return` output
bank nem jelenik meg. A `Monitor Out 01/02` par helyi Windows monitorozasra
tovabbra is hasznalhato.
A `Transmit.OutputChannels` hatarozza meg, hany `REAC Return` ASIO output
jelenjen meg a monitor par elott. Az S-1608 iranyaban a jelenlegi slave return
formatum 16 wire csatornat kuld 340 byte-os REAC frame-ben, 6 mintaval
packetenkent. 8 lathato return mellett a 9-16 wire csatorna csendet kap,
16 lathato return mellett a DAW `REAC Return 01` ... `REAC Return 16`
kozvetlenul a 16 wire csatornara kerul.
Az `ExposeMonoReturns=1` egy kulon mono return bankot is megjelenit:
`REAC Mono 01 / REAC Mono 01 Dummy` ... `REAC Mono 16 / REAC Mono 16 Dummy`.
Ez azokhoz a DAW-okhoz hasznos, amelyek kimenetet csak stereo paronkent
engednek valasztani; a bal oldal a tenyleges mono return, a dummy oldal el van
dobva.

A `Port` jelenleg csak jovobeli kompatibilitas miatt szerepel; a REAC/H-8819
aktualis implementacio nyers Ethernetet hasznal, nem UDP portot.

## Teszteszkozok

### reac_probe.exe

Feladata:

- Npcap capture eszkozok listazasa.
- REAC/H-8819 packetek figyelese.
- Packet statisztika irasa.
- Packet counter alapjan missing packet detektalas.
- `--inspect` mod: MAC cim, EtherType, packet counter, packet type, status,
  csatornaszam es REAC header bytes kiirasa.
- Opcionaalis 40 kulon mono, 24 bites PCM WAV mentes.

Peldak:

```powershell
build-cmake\Release\reac_probe.exe --list-devices
build-cmake\Release\reac_probe.exe --device Realtek --seconds 10 --wav reac_40ch.wav
build-cmake\Release\reac_probe.exe --device 4 --seconds 10 --inspect
```

A `--wav reac_40ch.wav` kimenete:

```text
reac_40ch-01.wav ... reac_40ch-40.wav
```

A probe a packet hosszabol jelenti a dekodolt csatornaszam-tartomanyt is a
normal 12 mintas REAC frame feltetelezes mellett. A 340 byte-os frame 288 byte
audio payloadot hordoz, ami lehet `8 csatorna x 12 minta x 3 byte`, de lehet
`16 csatorna x 6 minta x 3 byte` is. Emiatt az S-1608 return mereseknel a
packet rate-et is nezni kell, nem eleg csak a packet hosszat.

### reac_packet_test.exe

Szintetikus packetet general es ellenorzi:

- EtherType validaciot,
- trailer validaciot,
- packet counter olvasast,
- packet type metadata megtartasat,
- 24 bites signed PCM dekodolast,
- paros/paratlan csatorna byte sorrendet.

### reac_tx_probe.exe

Kulon teszteszkoz, meg nem az ASIO driver resze. Alap profilja a korai Roland
S-1608 PC-as-Master kiserletekhez van igazitva:

- 8 return csatorna,
- 340 byte-os REAC frame,
- 4000 packet/masodperc,
- `0x0000` audio packet type,
- alapbol csend,
- alapbol dry-run, tehat nem kuld packetet.

Profilok:

```text
s1608        8ch PC-as-Master proba a PC sajat MAC-cimevel
s1608-slave  8ch S-1608-szeru Slave stream, forras 02:00:00:00:00:01
mixer8       8ch kevero-szeru Master fazis, forras 02:00:00:00:00:02, header 00e7
mixer40      40ch kevero-szeru stream forma
```

Elsodleges hardverteszt: PC mint Master, S-1608 Slave modban. A REAC MODE
kapcsolo atallitasa utan az S-1608-at ujra kell inditani, mert a modvaltas csak
ujrainditas utan ervenyesul.

Peldak:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4
build-cmake\Release\reac_tx_probe.exe --device 4 --send --seconds 3
build-cmake\Release\reac_tx_probe.exe --device 4 --send --seconds 3 --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --send --seconds 10 --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --sync-counter --sync-source-mac 02:00:00:00:00:02 --send --seconds 10 --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --sync-counter --sync-source-mac 02:00:00:00:00:02 --sync-lead 72 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --slave-handshake --sync-counter --sync-source-mac 02:00:00:00:00:02 --sync-lead 72 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --slave-handshake --slave-keepalive-ms 4000 --sync-counter --sync-source-mac 02:00:00:00:00:02 --sync-lead 72 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --slave-preroll-ms 1500 --slave-handshake --slave-trigger-responder --sync-counter --sync-source-mac 02:00:00:00:00:02 --sync-lead 72 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device Ethernet --profile s1608-slave --slave-standby --slave-standby-responder --standby-chunk-ms 10 --slave-handshake --slave-per-gron-state --slave-local-counter --handshake-continuous-counter --slave-trigger-responder --sync-counter --sync-source-mac 02:00:00:00:00:02 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device Ethernet --profile s1608-slave --slave-standby --standby-chunk-ms 5 --slave-handshake --slave-per-gron-state --slave-local-counter --handshake-continuous-counter --slave-trigger-responder --sync-counter --sync-source-mac 02:00:00:00:00:02 --queue-pps 4006 --stream-chunk-ms 250 --send --seconds 60 --force --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device Ethernet --profile s1608-slave --slave-standby --standby-chunk-ms 5 --slave-handshake --slave-per-gron-state --slave-local-counter --handshake-continuous-counter --slave-trigger-responder --sync-counter --sync-source-mac 02:00:00:00:00:02 --queue-pps 4006 --stream-chunk-ms 250 --clock-follow --send --seconds 60 --force --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device Ethernet --profile s1608-slave --sample-rate 44100 --slave-standby --standby-chunk-ms 5 --slave-handshake --slave-per-gron-state --slave-local-counter --handshake-continuous-counter --slave-trigger-responder --sync-counter --sync-source-mac 02:00:00:00:00:02 --stream-chunk-ms 250 --clock-follow --send --seconds 60 --force --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device Ethernet --stable-slave --send --seconds 60 --force --tone-channel 1 --level-db -48
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --clocked --sync-source-mac 02:00:00:00:00:02 --send --seconds 10 --tone-channel 1 --level-db -48
```

Biztonsagi szabalyok: `--send` nelkul nincs kuldes, `--tone-channel` nelkul
nincs hang, `-18 dBFS` folotti tone level tiltott, 10 masodpercnel hosszabb
kuldeshez `--force` kell. A `--sync-counter` opcio kuldes elott belehallgat az
elo REAC streambe, majd a kuldendo packetek szamlalojat ehhez igazitja. A
`--clocked` mod nem sajat idozitovel fut, hanem minden megfelelo kevero REAC
packet utan kuld egy PC-s slave packetet, igy a visszirany a kevero orajat
koveti. A `--queue-pps` kulon allitja a timed queue idoziteset, mikozben a
logikai REAC packet szam marad a `--pps` szerint. A `--stable-slave` a regi
probe-stabilizacios preset: 44.1 kHz-es, 8x12-es S-1608-szeru slave mod
standby-val, Per-Gron state gate-tel, local source counterrel, queued trigger
valaszokkal, 250 ms chunkolt streammel es master clock follow-val. Az ASIO
driver visszairanyu utja azota a 16x6-os S-1608 modellre valtott. A
`--sample-rate` a
generalt hang idobazisat allitja, es ebbol szamolja a REAC packet utemet:
44100 Hz = 3675 pps, 48000 Hz = 4000 pps. A `--slave-handshake`
kiserleti EACD slave announce csomagsorozatot kuld a kevero MAC-cimere az audio
stream elott. Ha nincs kulon `--dest-mac`, a handshake utan az audio stream
celcime automatikusan a handshake-ben latott kevero MAC-cime lesz. Ha nincs
kulon `--header-repeat`, az audio stream a handshake utolso announce
csomagjabol szarmazo post-announce filler header part is atveszi. A
`--slave-keepalive-ms` a timed queue-ba periodikusan beszurja ugyanezt az
announce sorozatot, folyamatos REAC counter mellett.
`--slave-preroll-ms` a handshake elott rovid S-1608-szeru broadcast streamet
kuld, a `--slave-trigger-responder` pedig a queued audio futasa kozben kulon
szalon figyeli es megvalaszolja a kesobbi kevero EACD trigger csomagokat.
`--slave-standby` kevero nelkul is elindul: rovid S-1608-szeru broadcast
blokkokat kuld addig, amig meg nem jelenik a kevero trigger csomagja, majd
normal handshake-re es audio streamre valt. A Per-Gron/reacdriver allapotgep
nyoman a handshake capture mar csak a master/source REAC csomagokat figyeli,
ha van `--sync-source-mac`, igy a sajat standby broadcast nem tolja hatra a
trigger valaszt. A timed audio queue standby elott epul fel, a `--sync-counter`
pedig ilyenkor a handshake trigger counteret hasznalja, hogy a post-announce
csend minel rovidebb legyen. A `--handshake-continuous-counter` kiserleti
kapcsolo ebben a modban nem adja hozza a regi `--sync-lead` eloretolast, hanem
a handshake announce sorozat utan, ahhoz kozeli counterrol inditja az audio
queue-t. A `--slave-standby-responder` a Per-Gron driver input-vezerelt slave
valaszat utanozza: standby broadcast kozben kulon capture/sender szal figyeli
az elso mixer triggert, es nem varja meg a standby send blokk veget. A
CDEA control packeteket checksum ellenorzes utan hasznaljuk handshake
allapotvaltasra. A `--slave-per-gron-state` csak akkor enged trigger valaszt,
ha elotte lattunk master azonosito/control informaciot. A
`--slave-local-counter` a Per-Gron slave counter modelljet koveti: a PC/S-1608
forras countere folyamatos marad a standby, announce, queued audio es kesobbi
trigger valaszok kozott, nem ugrik ra a kevero counterere. Handshake utan a
stream a kovetkezo local-counter slotrol indul akkor is, ha a stream setup
kozben par milliszekundum eltelt. A
`--stream-chunk-ms` hosszu mereshez darabokra bontja a timed streamet; ha
`--slave-trigger-responder` is aktiv, a kesobbi mixer triggerekre adott
valaszok nem extra out-of-band packetkent mennek ki, hanem a kovetkezo chunk
elejen a sajat local-counter packet slotokat helyettesitik. A `--clock-follow`
a hosszabb stabilitasi kiserletek fo iranya: elo master counter/timestamp
alapjan becsli a master orajat, kiszuri a rovid capture gapeket, majd
chunkonkent korrigalja a chunk meretet, a kuldesi sebesseget, a tone
sample-rate-et es az idotartamhoz tartozo packet szamot, hogy a PC oldali
stream ne csusszon el lassan a master orajahoz kepest.

## ASIO REAC output

Az ASIO driverben a `REAC output` kapcsolo az elso stabilizalt visszairanyt
inditja el. Ez jelenleg szandekosan egy szuk, mert meressel igazolt mod:

- S-1608-szeru Slave,
- 16 wire csatorna,
- 6 minta / packet,
- 340 byte-os audio frame,
- `sampleRate / 6` packet/s (`44100` -> 7350 pps, `48000` -> 8000 pps),
- 2 ms standby broadcast,
- Per-Gron jellegu handshake state gate,
- folyamatos sajat slave/local counter,
- triggerre beszurt announce valaszok,
- rovid, chunkolt Npcap timed queue,
- master clock follow.

A driver a DAW `REAC Return 01` ... `REAC Return 16` kimeneteit tudja
visszakuldeni a kevero fele. Ha csak 8 return van beallitva, a wire 9-16
csatorna csenddel lesz kitoltve. Hasznalat:

1. Control panel: Ethernet adapter kivalasztasa.
2. Sample rate: elsore `48000`, majd ha ez stabil, `44100` is tesztelheto.
3. `REAC output` bekapcsolasa.
4. `Save`, majd `Restart driver`.
5. `REAC returns`: `16`.
6. A DAW-ban jelet kuldeni a `REAC Return 01` ... `REAC Return 16` csatornakra.

A driver jelenleg `44100`, `48000` es `96000` host sample rate mellett indit
REAC transmittert. A 48 kHz az elso ajanlott stabilizacios cel, mert a 16x6
modellben ez adja a 8000 packet/s cadence-et, ami a legtobb S-1608 referencia
mereshez illeszkedik.

A `meres38-eloteszt` tanulsaga: a driver packeteket kuldott, de a REAC audio
payload teljesen csendes volt. Emiatt a driver most vedelmi fallbacket is kap:
ha a REAC Return csatornak nincsenek meghajtva, de a DAW a Monitor Out 1/2 parra
kuldene jelet, akkor `REAC output` bekapcsolt allapotban ezt a parat a REAC
Return 1/2-re tukrozi. Ettol az FL Studio default/master routolas kevesbe tud
csendes packetstreamet okozni.

A `meres39`/`meres40` utan a driveres TX idozites kozelebb kerult a probe stabil
utjahoz: a driver transmitter is kulon master-clock follower szalat indit,
elo mixer counter/timestamp alapjan becsli a REAC packet ratet, es a 250 ms-os
chunk meretet/kuldesi sebesseget ehhez igazitja. Normal ASIO host stop vagy
buffer ujranyitas kozben a transmitter nem all le azonnal, hanem tovabb tud
csendet kuldeni, hogy egy FL Studio return/output valtas ne szakitsa meg
automatikusan a slave jelenletet. Valodi driver restart, sample-rate valtas,
`REAC output` kikapcsolas vagy driver unload tovabbra is leallitja a transmit
szalat.

A korabbi parhuzamos initial-trigger responder gyenge pontnak bizonyult: Npcap
standby queue kozben out-of-order EACD valaszokat tudott beszurni. A driver
most az elso valaszt a standby queue utan, counter-sorrendben kuldi, hogy a
slave counter folyamatos maradjon.

Az audio oldal Per-Gron mintaja: a halozati csomagolo mindig kapjon egy mintablokkot,
ha pedig nincs ervenyes host/engine blokk, akkor explicit csendet kuldjon. A
Windows driver ennek megfeleloen a REAC Return interleaved blokkot tolja a TX
ring bufferbe; ha a direkt REAC Return csatornak csendesek, de barmelyik ASIO
output paron van jel, akkor az elso aktiv parat REAC Return 1/2-re tukrozi.

## Build

Visual Studio / MSBuild:

```powershell
MSBuild WindowsReacASIO.sln /p:Configuration=Release /p:Platform=x64 /m
```

CMake:

```powershell
cmake -S . -B build-cmake -G "Visual Studio 17 2022" -A x64
cmake --build build-cmake --config Release --parallel
```

Kimenetek:

```text
build\x64\Release\WindowsReacASIO.dll
build-cmake\Release\WindowsReacASIO.dll
build-cmake\Release\reac_probe.exe
build-cmake\Release\reac_tx_probe.exe
build-cmake\Release\reac_packet_test.exe
```

## Driver regisztracio

Admin jogosultsag kell, mert a driver a `HKLM\SOFTWARE\ASIO` ala regisztral.

```powershell
install\install.bat
```

Eltavolitas:

```powershell
install\uninstall.bat
```

## Elfogadasi kriteriumok

### ASIO stub

- A driver megjelenik FL Studio / Reaper / mas ASIO host alatt.
- Kivalaszthato `Windows REAC ASIO` neven.
- 40 input csatorna latszik.
- Teszt szinusz vagy silence stabilan erkezik.

### REAC probe

- `reac_probe --list-devices` listazza a Npcap adaptereket.
- REAC adapteren packeteket lat.
- A packet counter no.
- Missing packet statisztika ertelmezheto.
- Par masodpercnyi WAV mentes ertelmes PCM adatot tartalmaz.

### REAC + ASIO

- Konfiguralva a REAC adaptert, a DAW-ban elo REAC input jelenik meg.
- Halozati kimaradasnal silence erkezik crash helyett.
- Stream visszatereskor a driver ujra mintakat szallit.
- ASIO callback nem vegez halozati muveletet.
- `REAC output` kapcsoloval a driver elinditja az S-1608-szeru visszairanyu
  slave streamet, 16x6-os 340 byte-os packetekben, es a 16 return csatornat
  packetbe irja.

## Kovetkezo fejlesztesi lepesek

1. Live hardveres `reac_probe` teszt Npcap-pal.
2. 40 csatornas WAV visszaellenorzes DAW-ban vagy audio analizatorral.
3. REAC receiver stabilitasi teszt hosszu futassal.
4. ASIO driver live REAC input teszt Reaperben/FL Studioban.
5. Control Panel tovabbfejlesztes:
   - csatorna map beallitas,
   - reszletesebb receiver allapot,
   - hosszabb tavu statisztika reset/export.
6. Diagnosztikai log finomitasa.
7. Installer es tiszta release csomag.

## Kockazatok es nyitott kerdesek

- Npcap telepitesi es jogosultsagi modell geptol fuggo.
- A REAC hardver 48 kHz modban van jelenleg celzottan tamogatva.
- Tobb REAC eszkoz vagy split/master/slave modok kezdetben nem teljes koruen kezeltek.
- A packet formatum a vizsgalt implementaciok alapjan stabil, de hardvervaltozatoknal lehetnek elteresek.
- A valos ideju stabilitas csak live REAC streammel es DAW-val validalhato teljesen.

## Hasznalati megjegyzes masik beszelgeteshez

Ha ezt a projektet masik eszkozon vagy masik csevegesben kell folytatni, add
meg ezt az azonositot:

```text
```

Majd csatold vagy masold be ezt a dokumentumot, es kerd a kovetkezot:

```text
Kerlek, folytasd a Windows REAC ASIO projektet innen. Eloszor olvasd at a
lokalis repo aktualis fajljait, kulonosen a docs/project_overview_hu.md es
docs/reac_protocol_notes.md dokumentumokat.
```
