# FreeREAC kutatasi jegyzet

Allapot: 2026-07-11

Cel: a REAC protokoll es a FreeREAC projekt alapos megertese a Roland M-400 <-> FL Studio ketiranyu ASIO driver szempontjabol. A jelen jegyzet szandekosan nem a sajat driver kodjat elemzi, hanem a FreeREAC-bol levonhato protokoll- es architektura-tanulsagokat.

## Forrasok

- FreeREAC szervezet: https://github.com/FreeREAC
- Protokoll referencia: https://github.com/FreeREAC/reac-protocol
- Shared C protokoll helper: https://github.com/FreeREAC/libreac
- REAC -> AES67 bridge: https://github.com/FreeREAC/reac-aes67
- PipeWire REAC endpoint kiserlet: https://github.com/FreeREAC/reac-aes67/tree/main/pipewire
- REAC analizalo es Wireshark dissector: https://github.com/FreeREAC/reac-tools
- REAC channel-name labeller: https://github.com/FreeREAC/reac-label
- L2 de-jitter / re-pacer: https://github.com/FreeREAC/reac-repacer
- OpenWrt transport: https://github.com/FreeREAC/reac-transport
- Engineering findings / rig recipe: https://github.com/FreeREAC/reac-docs
- Nyers laboranyagok: https://github.com/FreeREAC/reac-lab

A repo-k lokalis masolata: `D:\Windows Reac ASIO\external\FreeREAC`.

## Repo-terkep

| Repo | Szerep | Mennyire kozvetlenul hasznos nekunk |
|---|---|---|
| `reac-protocol` | Wire-format, frame tipusek, handshakes, firmware findings | Elso szamu protokoll referencia |
| `libreac` | C konstansok, frame-validate, counter, rate-detect | Jo alap RX/diagnosztikai kozos modulhoz; TX meg nincs benne |
| `reac-tools` | Python analizalok, pcap parser, jitter/loss/cross-mix, Wireshark Lua | Nagyon hasznos meresekhez es PCAP validalashoz |
| `reac-aes67` | Mukodo REAC RX -> RTP L24/AES67 bridge | Jo RX/decode/media-clock/PLC minta |
| `reac-aes67/pipewire` | PipeWire source/sink terv, RX node, TX scaffold | Architekturalis minta ASIO-hoz; public TX nem eleg valodi mixerhez |
| `reac-label` | Mixer TCP 8023 query: REAC slot -> mixer channel/name | Nagyon fontos, mert a wire nem hordoz patch/nevet |
| `reac-repacer` | Whole-frame de-jitter es clock recovery | Idozitesi, clock-follow es jitter-buffer tanulsagok |
| `reac-docs` / `reac-lab` | Meresi eredmenyek, design journal | Strategiai dontesekhez hasznos |
| `reac-transport` | OpenWrt VLAN/gretap transport | A Windows driverhez kozvetlenul kevesbe fontos |

Licenc: a FreeREAC kod jellemzoen GPL-3.0-or-later. Ha kodot veszunk at, a sajat projektet GPL-kompatibilis modon kell kezelni. Ha csak protokoll-tenyeket hasznalunk es sajat implementaciot irunk, az jogilag tisztabb ut.

## Fo protokoll-tenyek

### Fizikai / link reteg

- REAC Layer-2 protokoll, EtherType `0x8819`.
- A klasszikus REAC link celja 100BASE-TX full-duplex, nem IP alapu audio.
- A master jellemzoen a kevero, a stagebox slave-kent lockol, split receiver passzivan hallgat.
- A wire-on nincs sample-rate field: a sample rate maga a csomag-kadencia.

### Master -> fabric downstream

FreeREAC szerint a downstream master broadcast frame:

- fix 1492 byte Ethernet frame,
- 50 byte header,
- 1440 byte audio,
- 2 byte end marker `C2 EA`,
- audio: 40 csatorna x 12 minta x 3 byte,
- minden frame type hordoz audiot, a `FILLER` is.

Offsetek:

- `12..13`: EtherType `88 19`
- `14..15`: u16 little-endian frame counter
- `16..17`: frame type
- `18..49`: 32 byte control/data block, `data[31]` checksum
- `50..1489`: audio
- `1490..1491`: `C2 EA`

Rate modell:

- 44.1 kHz -> 3675 pps
- 48 kHz -> 4000 pps
- 96 kHz -> 8000 pps

96 kHz-nel nem felezodik a csatornaszam: ugyanaz a 40 ch / 12 sample frame megy, csak dupla packet rate-tel.

### Frame type-ok es control prefixek

Frame type-ok:

- `00 00`: FILLER, checksum exempt, de audio van benne
- `CD EA`: CONTROL
- `CF EA`: MASTER_ANNOUNCE
- `CE EA`: SPLIT_ANNOUNCE
- `C2 EA`: ENDING / split teardown jellegu tipus, nem azonos a frame vegi marker szerepevel

CONTROL prefixek:

- `01 00 00 1A 00`: CONTROL_ONE
- `01 02 00 0E 00`: CONTROL_TWO
- `01 03 00 19 01`: CONTROL_THREE / master channel-info
- `01 01 00 18 00`: CONTROL_FOUR
- `01 03 00 10 82`: SLAVE_ANNOUNCE1
- `04 03 00 14 00`: SLAVE_ANNOUNCE2
- `04 03 00 13 00`: SLAVE_ANNOUNCE3
- `01 03 00 01 81`: SLAVE_ANNOUNCE4 / slave return channel-map heartbeat

Checksum: `sum(data[0..31]) mod 256 == 0`, FILLER-nel nem kell.

### Channel-info control logika

Ez kozvetlenul erinti a mi csatorna-elcsuszas problemankat.

FreeREAC szerint a CONTROL streamben channel-info blokkok vannak:

- 3 byte / rekord: `[channel#, type-flags, gain]`
- 8 rekord / CONTROL frame
- rotal 49 indexig
- a terminator channel-number byte `0xFE`, nem egyszeru flag
- M-5000-nel flag: `0x28` csatorna 0..39, `0x38` csatorna 40..47
- slave return channel-map prefixe: `01 03 00 01 81`
- master channel-info prefixe: `01 03 00 19 01`

Kovetkeztetes: nem eleg jo audio slotba irni. A master varhatja, hogy a slave a megfelelo channel-map/control heartbeat-et is beszelje. Ha ez hibas, a kevero rossz helyen, rossz route-tal vagy instabilan erzekelheti a hangot.

### Audio byte layout

FreeREAC ket layout csaladot ir le:

1. M-5000-class / ujabb downstream: plain little-endian sample-major:
   - sample `s`, channel `ch` offset: `(s * n_channels + ch) * 3`
   - 24-bit signed LE
2. obs-h8819-class / mas generacio: even/odd braid:
   - paros/paratlan csatornapar byte-sorrendje kevert

Fontos: a FreeREAC sajat doksijain belul is van fejlodestortenet. A `reac-docs/findings.md` meg altalanosan pair-interleave-et emlit, a frissebb `reac-protocol/wire-format.md` mar tisztazza, hogy eszkozgeneracio-fuggo a payload byte layout. Ez pontosan olyan hiba, ami nalunk "6-8 bites zajos" hangnak hallatszodhat.

### Slave -> master upstream

Ez a mi FL Studio -> M-400 return iranyunkhoz hasonlo szerep: virtualis stagebox-kent a master fele kell hangot kuldenunk.

FreeREAC szerint:

- upstream unicast a master MAC-re,
- kisebb frame, nem fix 40 csatornas downstream frame,
- a box sajat input count-jat hordozza,
- audio plain 24-bit LE sample-major,
- az upstream channel map FPGA-owned es jelenleg nyitott: input N nem biztos, hogy wire slot N.

Kritikus validalasi pont M-400 + S-1608-ra: FreeREAC altalanos upstream modellje nem biztos, hogy bitre egyezik a mi hardverunkkel. A mi korabbi mereseinkben az S-1608-szeru 8 ch return 340 byte koruli frame-mel ment; ezt ossze kell vetni a FreeREAC upstream rate/sample-per-frame allitasaval, mert lehet generacios vagy rate-fuggo kulonbseg.

## Handshake / kapcsolat

FreeREAC osszefoglalasa szerint a slave csatlakozas wire-szintu fazisai:

1. Link-up flood: PHY link-up utan a box broadcast FILLER-t kuld teljes packet rate-tel kb. 1-1.5 s-ig.
2. Cold-connect / grant: rovid unicast CONTROL burst a masternek; a master broadcast streamben grant bursttel valaszol, amely echozza a box identitasat.
3. Config-announce: slave sajat input count / return map CONTROL-t kuld.
4. Established: steady unicast FILLER/audio a master MAC-re, plusz kb. 1/s CONTROL heartbeat.

Link hold:

- kb. 600 frame budget re-arm-olva heartbeat-tel,
- ez 48 kHz-en kb. 150 ms, 96 kHz-en kb. 75 ms,
- van kb. 1000 ms no-sized-audio cutoff is.

Kovetkeztetes: a kapcsolatot nem csak "barmilyen 0x8819 audio frame" tartja eletben. Kell jo meretu audio stream, jo counter, jo master MAC, jo heartbeat/control, stabil source MAC es a master altal elfogadott channel-map.

## Idozites / clock

A FreeREAC legfontosabb tanulsaga: a stageboxoknak nincs klasszikus jitter bufferuk. A word clockot a frame-erkezes kadenciajabol nyerik. A master oldali upstream fogadas phase-strict: a valodi stagebox a master downstream slotjaihoz kepest szuk idobeli ablakban valaszol.

Ez ket kulon kovetelmeny:

- rate lock: hosszu tavon pontos pps kell,
- phase lock: a master slot-gridhez kepest jo idopontban kell valaszolni.

FreeREAC repacer tanulsagok:

- minden frame-et egyforman kell pacelni; control frame-et sem szabad "fast path"-on eloreengedni, mert az is audiot hordoz.
- underrun esetben counter-rewrite + elozo frame audio repeat jobb, mint gap vagy duplicate counter.
- buffer shrink drop-pal kattog; grow-only buffer + lassu rate-bias jobb.
- a buffer fill nem lehet gyors clock-servo bemenet, mert az input jittert FM-modulaciova alakitja.
- a legjobb clock rate becsles a kumulativ counter-mean / frame-count-over-time.
- master-facing oldalon a szoftveres egress jitter hatara hamar hallhato; TSN/ETF/LaunchTime es PTP hardware clock lehet a profi irany.

Windows/Npcap tanulsag: a user-mode packet loop es meg az Npcap queue is kompromisszum. Jo lehet kiserleti/prototipus szintre, de ha a master valoban phase-strict, ipari szintu TX-hez vagy kernel-szintu timing, vagy TSN kepes NIC, vagy mas architektura kellhet.

## FreeREAC kod / eszkozok, amelyeket hasznalhatunk

### Kozvetlenul atveheto vagy ujraimplementalhato

- `libreac` konstansok: frame meretek, offsetek, counter gap, rate snap.
- `reac-tools/wireshark/reac.lua`: Wireshark dissector; azonnal hasznos a mi PCAP-jainkhoz.
- `reac-tools` analizis modell: loss, reorder, duplicate, jitter, cross-mix.
- `reac-aes67/src/reac_decode.c`: plain LE downstream decoder es media pipeline minta.
- `reac-aes67/src/media_clock.c`: counter-gap alapu timestamp kontinuitas.
- `reac-aes67/src/plc.c`: packet loss concealment gondolkodas RX/AES67 iranyban.
- `reac-label`: M-400 patch/name lekerdezeshez nagyon hasznos lehet.

### Nem eleg kesz a mi TX celunkhoz

- `libreac` jelenleg RX/measure oriented, nincs teljes TX layer.
- `reac-aes67/pipewire/src/reac_tx.c` public kod downstream master broadcast frame-et epit 1492 byte-tal; ez nem az, amit egy M-400 master stagebox upstream returnkent var.
- `reac-aes67/pipewire/CONNECTION-FSM.md` kimondja: a virtual-stagebox JOIN/HOLD FSM privat munkarepo, nincs publikusan kiadva.

## Mit jelent ez a mi M-400 / FL Studio celunkra?

### Jo irany

Nem egyszeruen "audio csomagokat" kell kuldenunk, hanem virtualis stageboxot kell emulalnunk:

1. slave-identitas broadcast flood,
2. master MAC tanulas,
3. cold-connect CONTROL exchange,
4. return channel-map / input count annonce,
5. folyamatos unicast upstream audio a master MAC-re,
6. periodic heartbeat/control,
7. downstream master clockbol recoverelt rate + phase.

### Legnagyobb kockazatok

- upstream frame format es sample-per-frame M-400/S-1608 specifikus lehet;
- upstream channel map nincs FreeREAC-ben megoldva;
- channel-info/control flags elterhetnek M-400 generacional;
- Windows user-mode timing nem biztos, hogy eleg phase-strict upstreamhez;
- GPL kod atvetele licencdontest igenyel.

### A 16 return siker magyarazata

A korabbi mereseinkkel osszevetve a FreeREAC azt erositi meg, hogy a 40 ch downstream gondolkodas rossz alap lehet a return iranyhoz. A master stagebox upstreamet var: kisebb, eszkozos input-count szerinti frame-et, megfelelo return channel-map control-lal. Ezert lehetett a 16 return sokkal kozelebb a johoz, mint a 40 ch mod.

## Javasolt kovetkezo lepesek

1. A FreeREAC Wireshark dissector telepitese / hasznalata a mi PCAP-jainkra.
2. PCAPNG -> classic PCAP konverzio (`editcap`) es `reac-tools` futtatas, amikor van mukodo Python.
3. M-400 control port teszt `reac-label` logika szerint:
   - `VRQ`, `CNQ`, `PIQ`, `POQ`, esetleg `RCQ`
   - cel: REAC slot -> mixer channel / output patch bizonyitas.
4. M-400 + S-1608 aranymeres:
   - 48 kHz es 44.1 kHz,
   - slave steady state,
   - ismert, kulonbozo frekvenciaju jelek tobb inputon egyszerre,
   - cel: upstream channel map es frame geometry.
5. Sajalt REAC mag ujrarendezese FreeREAC mintara:
   - `reac_frame` validate/counter/type/checksum,
   - `reac_control` channel-info builder,
   - `reac_upstream_profile` S-1608/M-400 specifikus frame builder,
   - `reac_clock` master-counter/rate/phase follower,
   - `reac_diag` FreeREAC-szeru loss/jitter/level report.
6. A public FreeREAC TX kodot csak downstream encoder mintanak kezelni, nem a valodi M-400 return megoldasnak.

## Dontesi javaslat

FreeREAC-et erdemes "uj protokoll alapnak" hasznalni, de nem egyszeruen fork-kent:

- a sajat ASIO / Windows driver reszt tartsuk meg celplatformnak;
- a REAC protokollmodellt FreeREAC szerint formalizaljuk;
- a diagnosztikat es PCAP elemzest FreeREAC eszkozokkel tamasszuk ala;
- a TX virtual-stagebox reszt sajat, M-400/S-1608 meresekkel validalt modulkent epitjuk, mert a public FreeREAC itt szandekosan nem ad teljes JOIN/HOLD megoldast.

Rovid verzio: FreeREAC nem kesz Windows ASIO driver alap, hanem nagyon eros protokoll- es meresi alap. A legnagyobb konkret haszon most a channel-info/heartbeat/logikai modell, a Wireshark dissector, a `reac-label` patch-lekerdezes, valamint a repacer timing szemlelet.
