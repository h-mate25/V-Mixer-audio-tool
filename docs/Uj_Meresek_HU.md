# Új REAC felvételek: első mérési kör

2026. november 4. A régi bekötést nem kell emlékezetből rekonstruálni. Az új felvételeknél a kábelezést és a futó programokat együtt rögzítjük a csomagokkal.

## Ami a meglévő gépekkel is hasznos

**1. Passzív SPLIT-felvétel, valódi stageboxszal.** Az S-1608 az M-400 A portján, a PC a SPLIT/BACKUP porton, a keverő normál FOH/split felállásban. A saját REAC-adó és más emulátor álljon. Csak Wireshark/dumpcap felvétel fusson, teljes csomaghosszal, kezdetben capture filter nélkül.

Készíts körülbelül 90 másodpercet. Először legyen alapállapot, majd az S-1608 fizikai 1., 2., 9. és 16. bemenetén egymás után legyen jól felismerhető jel, nagyjából 10-10 másodpercig. Elég egy jelforrást átkötni. Írd fel a tényleges időpontokat és a keverő patch-ét. Ebből a továbbított csatornák kiosztását vizsgálhatjuk. A stagebox eredeti unicast csomagformátumát a SPLIT-felvétel önmagában nem dönti el.

**2. Külön passzív B-felvétel.** Az S-1608 maradjon A-n, a PC kerüljön B-re, továbbra is emulátor nélkül. Körülbelül 60 másodperc mellett rögzítsd a B input/output patch képernyőt és a szürkén, elérhetetlenül megjelenő helyeket is. Ez a későbbi PC-endpoint előtti alapállapotot mutatja meg.

Az első új sorozathoz 48 kHz a javasolt közös beállítás; a tényleges kijelzett értéket mindig írd fel. A 44,1 kHz-es vizsgálat külön felvétel legyen. A leírás most mérési terv: ebben a kutatási körben nem változtattam a keverő beállításain és nem indítottam hálózati adást.

## Ha van használható porttükrözés

Az eredeti kétirányú link a legerősebb referencia: M-400 A és S-1608 két switchporton, a PC külön mirror célporton. **Egyetlen endpoint portjának be- és kimenetét tükrözd**, ne mindkét endpointét egyszerre. Így a valódi stagebox eredeti visszirányú kerete is látható, és vizsgálható a 16×6 vagy 8×12 kérdés. Nem feltételezem, hogy van ilyen switched; az első két mérés enélkül is értékes.

## Mi kerüljön a felvétel mellé?

- M-400 firmware, mintavétel és REAC mód; S-1608 kapcsolóállás.
- A/B/SPLIT kábelezés és a capture pontos helye; switch/tükrözés, ha volt.
- A keverő be- és kimeneti patch-e.
- Futott-e emulátor; aktív próbánál pontos programverzió, konfiguráció és fájlhash.
- Melyik bemeneten mikor milyen jel szólt; újracsatlakozás időpontja, ha az volt a mérés célja.

A kitölthető JSON mérési lap (local artifact; excluded from this documentation upload) minden mezője induláskor üres. Ami nem ismert, maradhat `null`. A csomagfájlokat változatlanul őrizd meg; az időbélyegek önmagukban nem jelentenek pontos hálózati jitter- vagy round-trip mérést.

A részletes angol terv és az egyes mérésekből levonható következtetések: [New_Capture_Plan_EN.md](reports/New_Capture_Plan_EN.md).
