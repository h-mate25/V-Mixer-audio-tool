# REAC kutatási összefoglaló

Forrás- és capture-vizsgálat: 2026. október 25-26. Folytatás és új ellenőrzés: 2026. november 4. Cél: Roland M-400 + S-1608, Windows és FL Studio, egyszerre 40 mono bemenet és 40 mono kimenet.

A FreeREAC a vizsgált projektek közül a legrészletesebb protokoll- és működési referencia. A hordozható C részekből Windows alatt is sikerült néhányat lefordítani és offline ellenőrizni. A hálózati transport és a PipeWire illesztés azonban Linuxra épül; ebből még nem lesz működő Windows ASIO interfész. A teljes eszközviselkedés megértéséhez továbbra is kell egy pontosan dokumentált hardveres alapmérés.

## Hogyan áll össze a rendszer?

A REAC közvetlen Ethernet-kereteket használ, `0x8819` EtherType-pal. Négy feladat fut egymás mellett:

1. A hangminták csomagolása és visszafejtése: 24 bites, csatornapáronként összefont bájtsorrend.
2. A kapcsolat vezérlése: felismerés, eszközazonosság, csatornakészlet, konfigurációátvitel, állapot és újracsatlakozás.
3. Az órajel követése: a PC-nek a master mintavételéhez kell igazítania az adást és a DAW pufferkezelését.
4. A routing: melyik fizikai aljzat, hálózati slot, keverőcsatorna és ASIO-csatorna tartozik össze.

Egy hallható jel vagy a DAW-ban megjelenő negyven bemenet csak egy részét bizonyítja ennek. A 40×40 eredményhez mindkét irányban negyven külön azonosítható jel, tartós órajelzárás és működő helyreállás kell.

## Mire használhatóak a projektek?

| Projekt | Ami hasznos belőle | Ami nincs vele igazolva |
|---|---|---|
| FreeREAC / libreac | Keretformátum, vezérlőrekordok, profilok, állapotgépek, időzítési megoldások | M-400-on stabil Windows 40×40 |
| FreeREAC / reac-pw | Hálózati és audiohost réteg szétválasztása, kétirányú Linux/PipeWire integráció kódja | Windows ASIO transport |
| per-gron / reacdriver | Korai kézfogási megfigyelések, split mód, régi sablonok eredete | Teljes, mai slave implementáció |
| norihiro / obs-h8819-source | Egyszerűbb vételi és mintavisszafejtési példa | Általános DAW interfész és hálózati visszajátszás |
| norihiro / reaccapture | Offline hangkinyerés és történeti összehasonlítás | Tetszőleges mintavétel és változó keretszélesség helyes kezelése |
| ManuKapel / REAC-driver-Windows-Mac | Natív ASIO/CoreAudio illesztési példa | Negyven REAC visszirány: a két Windows kimenete helyi waveOut monitor |
| A saját régi ZIP-ed | Konkrét Windows TX-, ASIO- és hibakezelési tapasztalatok | Új termék alapja; a vizsgált visszirány legfeljebb 16 csatorna |

A projektek részben egymásból származnak. Az azonos sablon több helyen való előfordulása ezért nem több független hardveres igazolás.

## Mi derült ki most?

- **Mind az 58 PCAPNG felvételt feldolgoztuk:** 14 630 983 csomagrekord, összesen mintegy 10,1 GB kibontott adat. A korábban hiányzó három mérés is megvan. Az eredeti 14 fájl összesített eredménye és a 6908 pontos hangblokk-egyezés reprodukálható.
- **A régi adó 16 csatorna × 6 mintát küld.** Ez ugyanúgy 340 bájt, mint 8 csatorna × 12 minta. A `meres50` hullámalakja a 16×6-os értelmezést támogatja. Ez a szoftver feltételezéséről és az adott felvételről szól; a valódi S-1608 csomagolását és a keverő elfogadását külön kell bizonyítani.
- **A régi órajelkövető eltérő egységeket hasonlít össze.** A normál 48 kHz-es master 4000 csomag/s értékét az adó 8000 körüli tartományban keresi, így nem tud arra ráállni. Az ASIO időzítése is külön hostórán fut.
- **114 konfigurációátvitel méret szerint összeállítható.** A két teljes törzs csak egy, a mintavételi osztálynak megfelelő bájtban tér el. A rövidebb változatokat is megőriztük; ezekből nem következik automatikusan csomagvesztés vagy hibás hardverműködés.
- **A handoff 40/0 profilra vonatkozó capture állítása javítandó.** A nullás kimenetszámmal meghívott capture konstruktor 40 csatornás szélességre vált vissza; a deklarált inventory közben továbbra is 40/0 marad. A deklarációépítő 40/0, 40/8 és 24/24 profilokat előállít, 40/40-et elutasít. Ez a deklarációmodell korlátja; a kétirányú REAC szállításra nem jelent közös 40 vagy 48 csatornás plafont.
- **A FreeREAC sem másolható ellenőrzés nélkül.** Az általános slave heartbeat útvonal nullára cseréli az adott hangblokkot. A parser hibás hosszmezőjű rekordot is GRANT-ként osztályoz; a választott master RX-eseményeként beadva a tiszta slave állapotgép megnyitja rá a grant ablakot. Egy belső állapot vagy jó checksum nem bizonyít valódi keverőoldali elfogadást.

## A november 4-i folytatás új ellenőrzései

A FreeREAC fő projektjeinek aktuális távoli verzióit is lekérdeztem. Négy projekt változott, a másik négy hivatkozott repository HEAD-je változatlan. A kritikus nyolc implementációs fájl bájtszinten azonos, ezért a korábbi fontos kódfeltárások továbbra is érvényesek ezekre az útvonalakra.

Az új nyilvános protokollpéldákból 17 teljes keret PCM-hashét, 61 vezérlőablakot és 18 teljes DT1 rekordot külön bájtaritmetikával ellenőriztem. Egy M-5000-példa azt is megmutatja, hogy üres CFEA rekordból nem szabad 48 kHz-et következtetni. A saját felvételekben külön ellenőriztem mind az 1833 ilyen rekordot: mind ismert, nem üres carriert használ, így a korábbi mintavételi következtetések ezen az ellenőrzésen is átmentek.

Az új upstream dokumentáció a Linux oldali master-felismerést és támogatott hálózati felállásokat pontosítja. Ezek projektoldali tapasztalatok; az M-400 B-portos Windows slave célhoz külön ellenőrzés kell. Részletek és pontos commitok: [novemberi audit](reports/Continuation_2026-11-04_EN.md).

## Mit jelent ez az M-400-nál?

A kézikönyv alapján a reális kísérleti irány: a valódi S-1608 az **A porton**, a PC külön slave-ként a **B porton**, a keverő normál FOH/master működésével. Az A és a SPLIT/BACKUP közös rendszer; a B külön rendszer, negyven kimeneti patch-slottal. Az A-n nyolc szabadon patch-elhető keverőkimenet után a csatlakozó egységek bemenetei is bekerülnek a továbbított jelbe.

A 40 bemenetű virtuális stagebox ígéretes kísérlet. A döntő nyitott pont: elfogadja-e az M-400, és a deklarált fizikai kimenetek számától függetlenül enged-e negyven hasznos B-kimenetet patch-elni? A PC oldali 40 csatornás node ezt nem bizonyítja.

Az M-400 44,1/48 kHz-es és 48 bemeneti feldolgozócsatornája van. Tizenhat élő mikrofoncsatorna mellé 32 külön DAW-return fér el; negyven további return már 56 stripet igényelne. A hálózati 40×40 nem hoz létre negyven új insertet sem. Ezt a kívánt felhasználás routingjával együtt kell megtervezni.

## Következő mérföldkő

Először egy ismert bekötésű, csak valódi M-400/S-1608 hardverrel készített alapfelvétel kell, majd ugyanott egy külön jelölt emulátoros mérés, ismert programverzióval. Ez választja szét a hardver saját viselkedését a régi adó feltevéseitől. Utána következhet a külön B-portos minimális endpoint, egy jól azonosítható jellel mindkét irányban, ellenőrzött kapcsolati állapotokkal és órajellel. A negyven független csatornára bővítés ennek az elfogadási tesztjeire épülhet.

A novemberi egyeztetés alapján a régi bekötés nem ismert pontosan, új felvételeket viszont tudsz készíteni. Ehhez elkészült a [magyar mérési útmutató](Uj_Meresek_HU.md) és a kitölthető adatlap. A mostani munkában nem történt élő REAC-adás, drivertelepítés vagy FL Studio próba. Elkészült a forrásalapú és mérésalapú kutatási alap, de a teljes protokollismeret és a stabil 40×40 hardveres eredmény még nincs igazolva.

Részletes angol jegyzet: [REAC_Research_Notes_EN.txt](REAC_Research_Notes_EN.txt). Jelentések, adatok és reprodukálás: [README.md](README.md).
