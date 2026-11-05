# M1 – passzív SPLIT- és B-portos mérések

A két 48 kHz-es PCAPNG-t az M1 Windows x64 Release példányával ellenőriztük (forrás: `868b383247830a597d98bdd0446b4f989776fa21`). A PCAPNG-importer mindkét felvétel összes rekordját végigolvasta. A REAC-kereteket EtherType, forrás- és cél-MAC, méret és zárójelölő szerint csoportosítottuk. A PCM-aktivitást mintavételes dekódolással ellenőriztük.

| Felvétel | Összes Ethernet-keret | REAC-keretek | REAC-forrás MAC |
| --- | ---: | --- | --- |
| 1. Passzív SPLIT | 348 103 | 19 626 × 340 bájt, majd 328 397 × 1492 bájt | `00:40:ab:c4:9c:0a` |
| 2. Külön passzív B | 275 883 | 275 806 × 1492 bájt | `00:40:ab:c4:9c:0b` |

Mindkét felvétel REAC-keretei Ethernet-broadcast címre mentek. Mindkét felvétel ellenőrzött CFEA rekordjai 48 000 Hz-et jelentettek be.

## 1. Passzív SPLIT

A mérés készítője szerint **a felvétel elején a valódi stagebox még nem volt csatlakoztatva**. A 340 bájtos REAC-szakasz az 1–19 678. Ethernet-rekord között látható. Az első 1492 bájtos REAC-keret a 19 683. rekord. A bedugás pontos ideje nincs megadva, így a két esemény pontos időbeli egyezése nincs igazolva.

A 340 bájtos keretek hossza és zárójelölője az `8×12` és a `16×6` értelmezéssel egyaránt összeegyeztethető. Az M1 mindkét megadott geometriával dekódolt egy mintakeretet. Az 1492 bájtos keretek illeszkednek a `40×12` geometriához. A 330 vizsgált `40×12` keretben a 7–32. csatornahelyeken fordult elő nem nulla PCM-érték. A rögzített REAC-sorszámok között két, modulo 65 536 szerint 5461-es lépés volt.

## 2. Külön passzív B

Minden REAC-keret illeszkedik a `40×12` geometriához. A rögzített REAC-sorszámok minden egymást követő párnál eggyel nőttek modulo 65 536 szerint. A 276 vizsgált keret mindegyikében a 7. és 8. csatornahelyen volt nem nulla PCM-érték. A többi helyen ebben a mintában nulla értékeket olvastunk.

## Eredeti fájlok

| Fájlnév | Méret (bájt) | SHA-256 |
| --- | ---: | --- |
| `1. Passzív SPLIT-felvétel, valódi stageboxszal - 48kHz.pcapng` | 507 793 356 | `90e9d1981116fb2b4fafddd5f21b8539bc6f893058e1894d3a69958cff7ce101` |
| `2. Külön passzív B-felvétel - 48kHz.pcapng` | 420 340 192 | `a833498e932a93d3d47655888352f4fa6096e92472ad0ece03f7308e9b288001` |

A PCM-mintavétel minden olyan `40×12` REAC-keretet vizsgált, amelynek az eredeti, 1-től számozott Ethernet-rekordsorszáma osztható 997-tel. A csatornahelyek 1-től számozott hálózati pozíciók. A nem nulla PCM önmagában nem azonosít fizikai aljzatot vagy szándékosan beadott jelet. A fizikai csatornakiosztáshoz az aljzatok és tesztjelek időpontjai szükségesek. A sorszámok a felvételben megfigyelt folytonosságot írják le. Fizikai késleltetés, vezetékszintű csomagvesztés és kétirányú 40×40 működés külön vizsgálatot igényel.
