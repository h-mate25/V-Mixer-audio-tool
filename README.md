# Windows REAC ASIO – történeti forrásarchívum

Ez az ág a `Windows Reac ASIO` munkamappa két projektjének 86 fájlos forrás-pillanatképe. A fájlok eredeti alkönyvtár-szerkezete a [legacy/](legacy/) alatt található. A pillanatkép helyi munkafájlokból készült, ezért nem feleltethető meg automatikusan egy korábbi Git-commitnak.

| Projekt | Mire használható később? | Kiindulópont |
| --- | --- | --- |
| **Windows REAC ASIO** | Windows x64 ASIO illesztés, Npcap adapterkezelés, PCM-dekódolás, gyűrűpuffer, WASAPI-monitorozás és kísérleti REAC-adás összehasonlításához. | [Eredeti leírás](legacy/README.md), [projektáttekintés](legacy/docs/project_overview_hu.md), [protokolljegyzetek](legacy/docs/reac_protocol_notes.md), [forrás](legacy/src/), [eszközök](legacy/tools/) |
| **reaccapture (old project)** | A korábbi Windowsos rögzítés és 24 bites WAV-export megértéséhez. A Windowsos leírás dumpcap/Npcap útvonalat ismertet, a forrásfában libpcap-alapú kód is található. | [Windowsos leírás](legacy/reaccapture%20%28old%20project%29/README.md), [eredeti leírás](legacy/reaccapture%20%28old%20project%29/README), [forrás](legacy/reaccapture%20%28old%20project%29/src/), [licencszöveg](legacy/reaccapture%20%28old%20project%29/COPYING) |

## Hol érdemes kezdeni?

- **M1 összehasonlítás** – [keretparser](legacy/src/reac/reac_packet_parser.cpp), [kódoló](legacy/src/reac/reac_packet_encoder.cpp) és [csomagteszt](legacy/tools/reac_packet_test/reac_packet_test.cpp). A régi 340 bájtos csomagoknál a 16×6 és 8×12 értelmezés megkülönböztetéséhez a mérési környezetet is ellenőrizni kell.
- **Windowsos hangút** – [ASIO réteg](legacy/src/asio/), [audio motor és gyűrűpuffer](legacy/src/audio/), [REAC RX/TX](legacy/src/reac/).
- **A korábbi rögzítő** – [reaccapture.cc](legacy/reaccapture%20%28old%20project%29/src/reaccapture.cc), [pcaploader.cc](legacy/reaccapture%20%28old%20project%29/src/pcaploader.cc) és [pcapreader_win.cc](legacy/reaccapture%20%28old%20project%29/src/pcapreader_win.cc).

## Az archívum határai

A Windows driverhez megmaradt a Visual Studio megoldás, a CMake-konfiguráció és a telepítőszkriptek. A CMake C++17-et kér, az ASIO SDK helyét az `ASIO_SDK_DIR` változóval lehet megadni. A régi rögzítőhöz megmaradtak az Autotools-bemenetek és az eredeti licencszöveg. Az eredeti leírások történeti állapotot tükröznek. A nyilvános másolatban a gépfüggő útvonalakat, az adapterazonosítót és a mérésekhez tartozó azonosítókat helyettesítők váltják fel. Ez a régi vevő MAC-szűrőjét és a capture-elemző MAC-párját is érinti. Ezeket használat előtt a saját hálózathoz kell igazítani. Ezen az ágon nem ellenőriztük a fordítást, a driver regisztrálását, az élő adást vagy a kétirányú 40×40 működést.

A nyilvános ágon nincs helyi Git-adatbázis vagy IDE-gyorsítótár, generált buildkönyvtár, kész EXE/DLL, felhasználói capture-konfiguráció, parancsikon vagy nyers mérés. A külön licencelésű Steinberg ASIO SDK, a Npcap SDK 1.16, valamint az `external/` alatti FreeREAC- és egyéb upstream-másolatok külön beszerzendő referenciák. A csomagolás nem választ új licencet a projekthez.
