# PubSub DSL & Distribuert Kommunikasjonssystem i C

Dette prosjektet implementerer et fullstendig, skreddersydd programmeringsspråk (Domain-Specific Language - DSL) i C for pub/sub- og IoT-nettverksruting, sammen med et komplett testoppsett i **Docker** for å verifisere reell nettverkskommunikasjon og grundig loggføring.

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
```

### 4. Starte en Node-lytter manuelt
```bash
./pubsub_dsl listen 9001 --tag LAMPE
```
