# PubSub DSL & Distribuert Kommunikasjonssystem i C

Dette prosjektet implementerer et fullstendig, skreddersydd programmeringsspråk (Domain-Specific Language - DSL) i C for pub/sub- og IoT-nettverksruting, sammen med et komplett testoppsett i **Docker** for å verifisere reell nettverkskommunikasjon og grundig loggføring.

<img width="1920" height="1200" alt="image" src="https://github.com/user-attachments/assets/3bcf333d-54e0-4846-81ee-553c71392f1f" />

<img width="959" height="479" alt="image" src="https://github.com/user-attachments/assets/3ca829f9-15f7-41ee-b7fc-f68af5840bec" />


---

## 📋 Innhold og Arkitektur

Prosjektet er bygget modulært og kryssplattform i ren C (støtter både Windows med WinSock og Linux/Docker med POSIX sockets):

```
pubsub_dsl_c/
├── src/
│   ├── common.h          # Kryssplattform-definisjoner (Windows/Linux) og hjelpefunksjoner
│   ├── logger.h / .c     # Loggmotor med millisekund-tidstempler, ANSI-farger og fil-logging
│   ├── net.h / .c        # Nettverksabstraksjon (UDP socket send & listen, DNS/IP-oppslag)
│   ├── token.h           # Token-definisjoner for språket
│   ├── lexer.h / .c      # Lexer (tokeniserer kildekode, symboler, endepunkter, strenger)
│   ├── ast.h / .c        # Abstrakt syntakstre (AST) og minnehåndtering
│   ├── parser.h / .c     # Rekursiv descent parser for språket
│   ├── runtime.h / .c    # Kjøretidsmiljø (tabeller for publishers, subscribers, ruting, pakkesending)
│   ├── node_listener.h/.c# Daemon-lytter for abonnent- og publiseringsnoder
│   └── main.c            # CLI-grensesnitt (run, listen, test, --help)
├── scripts/
│   ├── demo.dsl             # Lokalt demonstrasjonsskript med alle språkkonstruksjoner
│   └── docker_scenario.dsl  # Distribuert scenario tilpasset Docker containere
├── docker/
│   ├── Dockerfile           # Alpine-basert container som kompilerer og kjører C-motoren
│   ├── docker-compose.yml   # Orkestrering av noder (LAMPE, VARME, STUE) og dsl_controller
│   └── entrypoint.sh        # Container entrypoint
├── logs/                    # Mappe for alle genererte loggfiler
├── Makefile                 # Kryssplattform Makefile
├── run_docker_test.ps1      # Automatisk testkjører i PowerShell
├── run_docker_test.sh       # Automatisk testkjører i Bash/Linux
└── README.md
```

---

## 🚀 Språksyntaks (DSL Spesifikasjon)

Språket støtter alle strukturer og instruksjoner som er spesifisert:

### 1. Blokk-deklarasjon av Tags
Definerer publisister og abonnenter med deres nettverksendepunkter (`ip:port`):
```dsl
publisher_tags:{
    STUE::192.168.1.10:8001 
    SOVEROM::192.168.1.11:8002
    SPISE::192.168.1.12:8003
}

subscriber_tags:{
    LAMPE::192.168.1.20:9001
    VARME::192.168.1.21:9002
}
```

### 2. Abonnent- og Rutingblokk (`subscribers[tag] = [ ... ]`)
```dsl
subscribers[LAMPE] = [
    # Registrerer abonnementsruting fra en publisher til subscriber
    subscribe(subscriber_tags, LAMPE):: 192.168.1.10:8001 -> 192.168.1.20:9001

    # Endrer adresse til en tag
    change(publisher_tags, STUE):: 192.168.1.10:8001 -> 192.168.1.99:8080

    # Setter en tag til EMPTY (deaktivert)
    change(publisher_tags, SPISE)::EMPTY

    # Legger til ny abonnent med adresse
    add(subscriber_tags, VIFTE):: 192.168.1.10:8001 -> 192.168.1.22:9003

    # Legger til ny abonnent uten adresse (medfører tag::EMPTY)
    add(subscriber_tags, RADIO)

    # Sletter en tag fullstendig fra registeret
    delete(subscriber_tags, VARME)
]
```

### 3. Kommunikasjonsinstruksjoner
Instruksjonene kan ta en valgfri meldingsstreng via `:: "melding"`:

- **Unicast (`send`)**:
  ```dsl
  send(STUE, LAMPE)
  send(STUE, LAMPE):: "Slå på lys i stua"
  ```

- **Multicast (`multicast`)**:
  Sender fra en spesifikk publisist-tag til en liste med abonnenter:
  ```dsl
  multicast(STUE, [LAMPE, VIFTE])
  multicast(STUE, [LAMPE, VIFTE]):: "Nattmodus aktivert"
  ```

- **Broadcast (`broadcast`)**:
  Sender fra spesifikk publisist til alle aktive abonnenter i `subscriber_tags` (hopper over noder merket som `EMPTY`):
  ```dsl
  broadcast(STUE, subscriber_tags)
  broadcast(STUE, subscriber_tags):: "Alarm: Bevegelse registrert!"
  ```

- **Tilleggsfunksjoner**:
  - `sleep(ms)`: Pause i skripteksekveringen (f.eks. `sleep(500)`)
  - `log("tekst")`: Loggfører egendefinert melding til konsoll og loggfil

---

## 📝 Loggføring (Husk Loggføring)

Loggmotoren (`logger.c` / `logger.h`) gir utfyllende detaljer:
- **Tidstempel med millisekund-presisjon**: `YYYY-MM-DD HH:MM:SS.mmm`
- **Komponent-tagger**: `[PARSER]`, `[ROUTING]`, `[NET_TX]`, `[NET_RX]`, `[EXEC]`, `[NODE_EVENT]`
- **Loggnivåer**: `DEBUG`, `INFO`, `ROUTING`, `NETWORK`, `WARN`, `ERROR`
- **Farger på konsoll** (ANSI) og ren tekst til fil.
- **Loggfiler generert**:
  - `pubsub_dsl.log` eller `logs/controller.log`: Skripteksekvering, rutingoppdateringer og utsendte pakker.
  - `logs/node_<TAG>.log`: Mottatte pakker hos hver lytternode med avsender, byte-størrelse, payload og JSON-metadata.

---

## 🐳 Docker Testmiljø

Docker-oppsettet oppretter et isolert nettverk (`pubsub_net`) med flere containere som kommuniserer over UDP via DNS-containernavn:
1. `sub_lampe`: Abonnentnode for tag `LAMPE` (port 9001)
2. `sub_varme`: Abonnentnode for tag `VARME` (port 9002)
3. `pub_stue`: Publisistnode for tag `STUE` (port 8001)
4. `dsl_controller`: Kjører DSL-tolkeren (`scripts/docker_scenario.dsl`) og orkestrerer testkommunikasjonen.

### Kjøre Docker-testen:

#### Alternativ 1: PowerShell (Windows)
```powershell
.\run_docker_test.ps1
```

#### Alternativ 2: Bash (Linux / macOS)
```bash
./run_docker_test.sh
```

#### Alternativ 3: Direkte med docker compose
```bash
docker compose -f docker/docker-compose.yml up --build --abort-on-container-exit
```

---

## 💻 Lokal Kjøring og Kompilering

### 1. Kompilering med GCC eller Make
```bash
# På Windows (MinGW):
gcc -Wall -Wextra -O2 -Isrc -o pubsub_dsl.exe src/*.c -lws2_32

# På Linux:
gcc -Wall -Wextra -O2 -Isrc -o pubsub_dsl src/*.c
# eller:
make
```

### 2. Kjøre Innebygde Enhetstester
```bash
./pubsub_dsl test
```

### 3. Kjøre et DSL-skript
```bash
./pubsub_dsl run scripts/demo.dsl

# Kjøre komplekst EN 54 brannalarm-scenario:
./pubsub_dsl run scripts/fire_alarm_complex.dsl
```

### 4. Starte en Node-lytter manuelt
```bash
./pubsub_dsl listen 9001 --tag LAMPE
```

---

## 🔥 3D Three.js Visualisering med Ren C Backend (EN 54 / NFPA 72)

Prosjektet inkluderer en fullverdig, interaktiv **Three.js 3D-visning og SCADA-kontrollpanel** drevet av den **native C-backend-serveren** (`pubsub_dsl.exe web 3000`):

### Arkitektur & Egenskaper:
1. **Ren C Backend & DSL-motor (`src/web_server.c`, `src/runtime.c`)**:
   - All DSL-parsing (Lexer/AST/Parser) og eksekvering (Runtime, UDP-pakkeutsending, rutingstabellmutasjoner) skjer **utelukkende i C**.
   - C-webserveren strømmer hendelser, pakker, logger og steg direkte til nettleseren via **Server-Sent Events (SSE)** over `/api/events`.
   - Det finnes **ingen DSL-motor i JavaScript**; nettleseren fungerer som en ren 3D SCADA-klient og sender brukerkommandoer tilbake til C via REST API-et (`/api/run`, `/api/trigger`, `/api/routing`, `/api/reset`).
2. **Fysisk 3D-modell av industribygg (`web/js/building_3d.js`)**:
   - **Kjeller (Sone 0)**: Glasskledd serverrom, serverrack med blinkende status-LED-er, Inergen (N2/Ar/CO2) inertgass-slukkesystem, VESDA aspirasjonsdetektor og teknisk rom.
   - **1. Etasje (Sone 1)**: Hovedinngang, resepsjon, kontorfløy, optiske røykdetektorer, manuelle meldere (break-glass), optiske xenon-blitzlamper og **motoriserte branndører som fysisk lukkes**.
   - **2. Etasje (Sone 2)**: Storkjøkken med kokeøy, avtrekkshette, differensial-varmemelder, gassdetektor og **aktive vanntåke/sprinklerdyser som spyler vannpartikler**.
   - **Tak & HVAC (Sone 3)**: Motoriserte røykluker (`ROYKAVTREKK_TAK`) med roterende vifteblader, nødavstenging av ventilasjon (`HVAC_VENTILASJON_STOPP`) og heissjakt med nødretur til 1. etasje.
3. **Visuelle partikkeleffekter og animasjoner**:
   - Reell røyk- og flammesimulering i berørte rom utløst av hendelser fra C-runtime.
   - Lysende nettverkspakker / foton-pulser som sendes langs 3D-kabelgatene ved `send()`, `multicast()` og `broadcast()`.
   - Eksplodert visning (0–100% etasjeseparasjon) og X-Ray gjennomsiktighetsmodus.
4. **Web Audio Synthesizer (`web/js/audio.js`)**:
   - Syntetiserer EN 54 Slow Whoop brannsirener (500–1200 Hz), gassutblåsingshves, vanndusj, dørklunk og talevarsling uten eksterne lydfiler.
5. **Kameravisninger og etasjefiltre**:
   - Forhåndsdefinerte kameravinkler for Serverrom, Storkjøkken, Sentral og Røykluker.

### Starte 3D-løsningen og C-webserveren:

```powershell
# Alternativ 1: Kjøre PowerShell-oppstartsskriptet (bygger om nødvendig og starter C-serveren)
.\run_fire_alarm_web.ps1

# Alternativ 2: Starte C Webserver direkte via kommandolinjen
.\pubsub_dsl.exe web 3000
# Åpne deretter http://localhost:3000 i nettleseren
```

