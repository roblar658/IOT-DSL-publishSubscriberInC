# PubSub DSL & Distribuert Kommunikasjonssystem i C

Dette prosjektet implementerer et fullstendig, skreddersydd programmeringsspråk (Domain-Specific Language - DSL) i C for pub/sub- og IoT-nettverksruting. Løsningen inkluderer en **native C-backend webserver med Three.js 3D SCADA-visualisering**, et **interaktivt Win32 SCADA konsoll-UI med sanntidslogg og full UTF-8-støtte**, samt et **komplett Docker-testoppsett** for verifisering av distribuert nettverkskommunikasjon.

<img width="958" height="529" alt="Three.js 3D SCADA Fire Alarm Overview" src="https://github.com/user-attachments/assets/56fffee7-73df-40aa-bd57-5312014c0afe" />

<img width="959" height="479" alt="Win32 Console SCADA and Log Hub" src="https://github.com/user-attachments/assets/3ca829f9-15f7-41ee-b7fc-f68af5840bec" />

---

## 📑 Innhold

1. [System- og Komponentarkitektur](#1-system--og-komponentarkitektur)
2. [DSL Kompilator- og Tolkerpipeline](#2-dsl-kompilator--og-tolkerpipeline)
3. [ER- og Minnemodell (C Datastrukturer)](#3-er--og-minnemodell-c-datastrukturer)
4. [Nettverks- og UDP-rutingstopologi](#4-nettverks--og-udp-rutingstopologi)
5. [Sekvensdiagram: Dual-UI & End-to-End Hendelsesflyt](#5-sekvensdiagram-dual-ui--end-to-end-hendelsesflyt)
6. [Tilstandsmaskin for Scenariokontroll](#6-tilstandsmaskin-for-scenariokontroll)
7. [Docker Distribuert Testarkitektur](#7-docker-distribuert-testarkitektur)
8. [Win32 SCADA Konsoll-UI (UTF-8)](#8-win32-scada-konsoll-ui-utf-8)
9. [Språksyntaks (DSL Spesifikasjon)](#9-språksyntaks-dsl-spesifikasjon)
10. [Three.js 3D SCADA Visualisering](#10-threejs-3d-scada-visualisering-en-54--nfpa-72)
11. [API-er, Endepunkter og Porter](#11-api-er-endepunkter-og-porter)
12. [Oppstart, Kjøring og Bygging](#12-oppstart-kjøring-og-bygging)

---

## 1. 🏗️ System- og Komponentarkitektur

Diagrammet viser hvordan C-kjernen, kompilatorpipelinen, nettverkslaget og de to samtidige brukergrensesnittene (Win32 Console og Web 3D) henger sammen:

```mermaid
graph TB
    subgraph CLIEntry["CLI-grensesnitt & Oppstart (src/main.c)"]
        CLI["main() CLI Router<br/>[web | ui | run | listen | test]"]
    end

    subgraph CompilerPipeline["DSL Kompilator- og Tolkerpipeline"]
        Lexer["Lexer (lexer.c / token.h)<br/>Tokenisering av nøkkelord, IP:Port og symboler"]
        Parser["Parser (parser.c / ast.h)<br/>Rekursiv descent-parser & AST-generering"]
        AST["AST Program (ast.c)<br/>AstStmt noder (Blocks, Change, Send, etc.)"]
        Lexer -->|Token Stream| Parser
        Parser -->|Bygger AST| AST
    end

    subgraph CoreRuntime["C Runtime Engine (src/runtime.c)"]
        RT["Runtime State Engine"]
        PubReg["Publisher Tags Registry<br/>TagEntry (max 256)"]
        SubReg["Subscriber Tags Registry<br/>TagEntry (max 256)"]
        RouteTable["Routing Table<br/>RouteEntry (max 512)"]
        HookManager["Hook Dispatcher<br/>Packet, Routing & Step Hooks"]
        
        RT --- PubReg
        RT --- SubReg
        RT --- RouteTable
        RT --- HookManager
    end

    subgraph NetworkLayer["Kryssplattform Nettverkslag (src/net.c)"]
        NetCore["UDP Transceiver & Socket Abstraction<br/>(WinSock2 på Windows / POSIX på Linux)"]
        Unicast["Unicast Send (net_send_udp)"]
        DaemonListen["Daemon Listener (net_listen_udp)"]
        NetCore --- Unicast
        NetCore --- DaemonListen
    end

    subgraph DualPresentationLayer["Dobbel Presentasjons- og SCADA-arkitektur"]
        subgraph Win32Panel["Konsoll-UI (src/win32_ui.c)"]
            Win32Engine["Win32 SCADA Engine<br/>UTF-8 Box Drawing & Double Buffer"]
            Hotkeys["Hurtigtaster [1-7, C, Q]<br/>Interaktiv kommandolinje"]
            ConsoleLog["Rullende sanntidslogg (300 linjer)"]
            Win32Engine --- Hotkeys
            Win32Engine --- ConsoleLog
        end

        subgraph CWebServer["Innebygd C Webserver (src/web_server.c)"]
            HttpServer["HTTP/1.1 Socket Server (:3000)"]
            SSEHub["Server-Sent Events (SSE) Hub<br/>/api/events (max 32 klienter)"]
            RestAPI["REST Endepunkter<br/>/api/run, /api/trigger, /api/routing..."]
            StaticServe["Statisk filtjener (web/*)"]
            HttpServer --- SSEHub
            HttpServer --- RestAPI
            HttpServer --- StaticServe
        end

        subgraph ThreeJSWeb["3D Web SCADA (web/)"]
            WebBrowser["Three.js 3D Digital Twin UI<br/>(WebGL, partikler, alarmer & lyd)"]
        end
    end

    subgraph ExternalNodes["Distribuerte Noder / Docker Containere"]
        SubLampe["Node LAMPE (:9001)<br/>(node_listener)"]
        SubVarme["Node VARME (:9002)<br/>(node_listener)"]
        PubStue["Node STUE (:8001)<br/>(node_listener)"]
    end

    %% Koblinger
    CLI -->|run| Lexer
    CLI -->|ui| Win32Engine
    CLI -->|web| HttpServer
    CLI -->|listen| DaemonListen

    AST -->|Kjøres av| RT
    RT -->|Nettverkspakker| NetCore
    NetCore -->|UDP Datagrams| ExternalNodes

    HookManager -->|Direkte loggkrok| ConsoleLog
    HookManager -->|JSON SSE Kringkasting| SSEHub
    SSEHub -->|EventSource stream| WebBrowser
    WebBrowser -->|POST REST forespørsler| RestAPI
    RestAPI -->|Kjører script / trigger| RT
    Win32Engine <-->|Deler minne & mutex| RT
    Win32Engine -.->|Starter i bakgrunnen| HttpServer
```

---

## 2. ⚙️ DSL Kompilator- og Tolkerpipeline

Fra `.dsl`-kildefil parses til handlingene slår inn i nettverket og rutingtabellene:

```mermaid
flowchart TD
    SourceFile["DSL Kildekode (.dsl)<br/>f.eks. fire_alarm_complex.dsl"] --> LexerPass["1. Leksikalsk analyse (lexer.c)"]
    
    subgraph Tokens["Token Stream (token.h)"]
        T1["TOKEN_KW_PUBLISHER_TAGS"]
        T2["TOKEN_IDENTIFIER ('MANUELL_MELDER_E1')"]
        T3["TOKEN_COLONCOLON ('::')"]
        T4["TOKEN_ENDPOINT ('10.0.1.10:8001')"]
        T5["TOKEN_KW_SEND / BROADCAST / CHANGE"]
    end
    LexerPass --> Tokens

    Tokens --> ParserPass["2. Rekursiv Descent Parser (parser.c)"]
    
    subgraph ASTNodes["Abstrakt Syntakstre - AST (ast.c / ast.h)"]
        ProgramNode["Program (Dynamisk array av AstStmt)"]
        StmtTags["STMT_TAGS_BLOCK<br/>(publisher_tags / subscriber_tags)"]
        StmtSubBlock["STMT_SUBSCRIBER_BLOCK<br/>subscribers[tag] = [ ... ]"]
        StmtChange["STMT_CHANGE / ADD / DELETE"]
        StmtSend["STMT_SEND (Unicast direkte)"]
        StmtBcast["STMT_BROADCAST (Til alle abonnenter)"]
        StmtMcast["STMT_MULTICAST (Til utvalgte tags)"]
        StmtSleep["STMT_SLEEP (Pause i ms)"]

        ProgramNode --> StmtTags
        ProgramNode --> StmtSubBlock
        ProgramNode --> StmtChange
        ProgramNode --> StmtSend
        ProgramNode --> StmtBcast
        ProgramNode --> StmtMcast
        ProgramNode --> StmtSleep
    end
    ParserPass --> ASTNodes

    ASTNodes --> Executor["3. Runtime Execution Engine (runtime.c)"]

    subgraph RuntimeAction["Kjøretidshandlinger"]
        UpdateReg["Registrering i TagRegistry<br/>(Navn -> IP:Port)"]
        UpdateRoutes["Oppdatering i RouteEntry[]<br/>(sub_tag -> from_ep -> to_ep)"]
        DispatchUDP["UDP Nettverksdispatching<br/>net_send_udp(from, to, payload)"]
        TriggerHooks["Utløs Kjøretidskroker<br/>(on_packet_hook, on_routing_hook, on_step_hook)"]
    end

    Executor --> UpdateReg
    Executor --> UpdateRoutes
    Executor --> DispatchUDP
    Executor --> TriggerHooks
```

---

## 3. 📊 ER- og Minnemodell (C Datastrukturer)

Systemets relasjonelle minnemodell for noder, ruter, meldinger og hendelser:

```mermaid
erDiagram
    RUNTIME ||--|| TAG_REGISTRY_PUB : "har publisher_tags"
    RUNTIME ||--|| TAG_REGISTRY_SUB : "har subscriber_tags"
    RUNTIME ||--o{ ROUTE_ENTRY : "vedlikeholder aktive"
    TAG_REGISTRY_PUB ||--o{ TAG_ENTRY : "inneholder"
    TAG_REGISTRY_SUB ||--o{ TAG_ENTRY : "inneholder"
    TAG_ENTRY ||--|| ENDPOINT : "peker til nettverksadresse"
    ROUTE_ENTRY ||--|| ENDPOINT : "fra_endepunkt"
    ROUTE_ENTRY ||--|| ENDPOINT : "til_endepunkt"
    AST_PROGRAM ||--o{ AST_STMT : "består av sekvens med"
    RUNTIME ||--o{ SSE_PACKET_EVENT : "genererer sanntids"

    RUNTIME {
        size_t route_count "Antall ruter (maks 512)"
        uint32_t message_sequence "Sekvensnummer for pakker"
        int step_delay_ms "Forsinkelse mellom steg (ms)"
        bool abort_execution "Signal for avbrudd"
        bool is_paused "Pauseflagg"
    }

    TAG_REGISTRY_PUB {
        size_t count "Antall registrerte publishers (maks 256)"
    }

    TAG_REGISTRY_SUB {
        size_t count "Antall registrerte subscribers (maks 256)"
    }

    TAG_ENTRY {
        char name "Tag-navn (f.eks. SENTRAL_KONTROLL)"
        bool is_empty "Er tag satt til EMPTY"
        bool in_use "Er slotten aktiv"
    }

    ENDPOINT {
        char host "IP-adresse eller hostname (maks 128 tegn)"
        int port "UDP Portnummer (1..65535)"
        bool is_empty "Angir tomt endepunkt"
    }

    ROUTE_ENTRY {
        char sub_tag "Abonnentens tag-identifikator"
        bool in_use "Aktiv rute"
    }

    AST_STMT {
        int line "Linjenummer i .dsl"
        int type "STMT_SEND | BROADCAST | CHANGE | etc."
    }

    SSE_PACKET_EVENT {
        string type "PACKET | ROUTING | STEP | LOG | STATUS"
        uint32 seq "Fortløpende pakkenummer"
        string from_tag "Avsender-tag"
        string to_tag "Mottaker-tag eller '*'"
        string payload "Meldingstekst"
        int bytes "Pakkestørrelse"
    }
```

---

## 4. 🌐 Nettverks- og UDP-rutingstopologi

Rutingprinsippene i DSL-motoren: **Unicast**, **Multicast** og **Broadcast** via rutingtabellen:

```mermaid
graph TB
    subgraph ControllerNode["DSL Controller / Avsender"]
        Sender["Publisher Node<br/>(f.eks. MANUELL_MELDER_E1: 10.0.1.10:8001)"]
        RT_Table["Rutingtabell i C-minnet<br/>-----------------------------------------<br/>SIRENE_E1  -> 10.0.1.10:8001 -> 10.0.1.10:8111<br/>SIRENE_E2  -> 10.0.1.10:8001 -> 10.0.1.10:8112<br/>BRANNDOR   -> 10.0.1.10:8001 -> 10.0.1.10:8121<br/>VENTILASJON-> 10.0.1.10:8001 -> 10.0.1.10:8131"]
    end

    subgraph Dispatcher["UDP Dispatching Engine (src/net.c)"]
        SendCmd["send(MANUELL_MELDER_E1, SENTRAL_KONTROLL)"]
        McastCmd["multicast(SENTRAL, [SIRENE_E1, BRANNDOR])"]
        BcastCmd["broadcast(SENTRAL_KONTROLL, subscriber_tags)"]
    end

    subgraph ReceiverNodes["Distribuerte Abonnenter (UDP Listeners)"]
        Sentral["SENTRAL_KONTROLL<br/>UDP 10.0.1.10:8099"]
        Sirene1["SIRENE_E1<br/>UDP 10.0.1.10:8111"]
        Sirene2["SIRENE_E2<br/>UDP 10.0.1.10:8112"]
        Branndor["BRANNDOR_E1<br/>UDP 10.0.1.10:8121"]
        Vent["VENTILASJON_E1<br/>UDP 10.0.1.10:8131"]
    end

    Sender --> RT_Table
    RT_Table --> Dispatcher

    %% Unicast
    SendCmd -->|1:1 Unicast Datagram| Sentral

    %% Multicast
    McastCmd -->|1:N Selektiv Datagram| Sirene1
    McastCmd -->|1:N Selektiv Datagram| Branndor

    %% Broadcast
    BcastCmd -->|1:Alle Datagrams iterert over ruter| Sirene1
    BcastCmd -->|1:Alle Datagrams iterert over ruter| Sirene2
    BcastCmd -->|1:Alle Datagrams iterert over ruter| Branndor
    BcastCmd -->|1:Alle Datagrams iterert over ruter| Vent
```

---

## 5. 🔄 Sekvensdiagram: Dual-UI & End-to-End Hendelsesflyt

Sanntidsflyt ved operatørhandling i enten Win32-konsollen eller 3D Web UI:

```mermaid
sequenceDiagram
    autonumber
    actor Operator as Operatør (Win32 UI / Web)
    participant UI as Win32 SCADA UI (win32_ui.c)
    participant Web as C Web Server (:3000)
    participant RT as C Runtime Engine (runtime.c)
    participant Net as UDP Socket (net.c)
    participant Daemon as Abonnent Node (node_listener)
    participant Browser as Three.js Web 3D Client

    alt Via Win32 Hurtigtast [1]
        Operator->>UI: Trykker [1] (Manuell Brannalarm)
        UI->>RT: runtime_send_direct(MANUELL_MELDER_E1, SENTRAL_KONTROLL, "MANUAL_FIRE_ALARM_E1")
    else Via Web 3D SCADA Grensesnitt
        Operator->>Browser: Klikker på branndetektor i 3D-modellen
        Browser->>Web: POST /api/trigger {"from":"MANUELL_MELDER_E1","to":"SENTRAL_KONTROLL"}
        Web->>RT: runtime_send_direct(...)
    end

    activate RT
    RT->>RT: Slår opp i TagRegistry & Rutingtabell
    RT->>Net: net_send_udp(from_ep, to_ep, payload, len)
    activate Net
    Net-->>Daemon: Sender UDP Datagram til port 8099
    deactivate Net
    Daemon->>Daemon: Mottar pakke, logger "ALARM ACTIVATED"

    par Krok til Win32 Konsoll
        RT->>UI: on_ui_logger_hook(NET_TX, "Dispatching SEND...")
        UI->>UI: Ruller loggvindu og oppdaterer UTF-8 skjermbuffer
    and Krok til Web SSE Stream
        RT->>Web: on_packet_hook(seq, "SEND", from, to, payload)
        Web-->>Browser: SSE data: {"type":"PACKET","from":"MANUELL_MELDER_E1","to":"SENTRAL_KONTROLL",...}
    end
    deactivate RT

    Note over Browser: Three.js animerer flyvende lysende partikkel,<br/>sirene blinker rødt og lyd utløses i nettleseren!
```

---

## 6. 🚦 Tilstandsmaskin for Scenariokontroll

Styring av livssyklusen under kjøring av komplekse DSL-scenarier:

```mermaid
stateDiagram-v2
    [*] --> IDLE : Initialisert

    state IDLE {
        description: Venter på kommando fra Web eller Win32 Konsoll
    }

    IDLE --> RUNNING : Start script / POST /api/run / Hurtigtast [5]
    
    state RUNNING {
        [*] --> FetchNextStmt
        FetchNextStmt --> ExecuteStmt : Parser og tolker neste AstStmt
        ExecuteStmt --> DelayStep : Venter step_delay_ms
        DelayStep --> FetchNextStmt : Flere setninger igjen
    }

    RUNNING --> PAUSED : Pause / POST /api/pause
    PAUSED --> RUNNING : Resume / POST /api/resume
    
    RUNNING --> STOPPED : Stop / POST /api/stop
    RUNNING --> ERROR : Parsefeil / Ugyldig endepunkt
    RUNNING --> FINISHED : Siste setning i Program fullført

    FINISHED --> IDLE : Reset / POST /api/reset / Hurtigtast [7]
    STOPPED --> IDLE : Reset
    ERROR --> IDLE : Reset
```

---

## 7. 🐳 Docker Distribuert Testarkitektur

Isolert flernode-testmiljø orkestrert med Docker Compose (`docker/docker-compose.yml`):

```mermaid
graph TB
    subgraph DockerHost["Docker Host (Bridge Network: pubsub_net)"]
        
        subgraph SubLampeContainer["Container: sub_lampe"]
            ProcLampe["pubsub_dsl listen 9001<br/>--tag LAMPE<br/>--log /app/logs/node_LAMPE.log"]
        end

        subgraph SubVarmeContainer["Container: sub_varme"]
            ProcVarme["pubsub_dsl listen 9002<br/>--tag VARME<br/>--log /app/logs/node_VARME.log"]
        end

        subgraph PubStueContainer["Container: pub_stue"]
            ProcStue["pubsub_dsl listen 8001<br/>--tag STUE<br/>--log /app/logs/node_STUE.log"]
        end

        subgraph ControllerContainer["Container: dsl_controller"]
            ProcController["pubsub_dsl run scripts/docker_scenario.dsl<br/>--log /app/logs/controller.log"]
        end

        subgraph SharedLogs["Host Volume Mount: ../logs"]
            LogLampe[node_LAMPE.log]
            LogVarme[node_VARME.log]
            LogStue[node_STUE.log]
            LogCtrl[controller.log]
        end
    end

    %% Nettverkskommunikasjon
    ProcController -->|UDP :9001| ProcLampe
    ProcController -->|UDP :9002| ProcVarme
    ProcStue -.->|UDP Datagrams| ProcLampe

    %% Logging
    ProcLampe --> LogLampe
    ProcVarme --> LogVarme
    ProcStue --> LogStue
    ProcController --> LogCtrl
```

---

## 8. 💻 Win32 SCADA Konsoll-UI (UTF-8)

Prosjektet har et innebygd, profesjonelt **Win32 Konsoll SCADA-kontrollpanel** direkte i terminalen:

```
╔══════════════════════════════════════════════════════════════════════════════════════════════════╗
║   PUBSUB DSL - WIN32 SCADA KONTROLLPANEL & LOGG (UTF-8)       Port: 3000                         ║
║   Web: http://localhost:3000  • Status: AKTIV / KJØRER • Pakker: 24    • Noder: 11 pub / 14 sub  ║
╠══════════════════════════════════════════════════════════════════════════════════════════════════╣
║   SANNTIDSLOGG & HENDELSER (Viser siste 16 oppføringer)                                          ║
╟──────────────────────────────────────────────────────────────────────────────────────────────────╢
║ [21:07:17.274] INFO    [UI     ] PubSub DSL Win32 SCADA Kontrollpanel initialisert (UTF-8 aktiv) ║
║ [21:07:17.274] INFO    [WEB    ] C Web Backend listening on port 3000 (root: 'web')              ║
║ [21:07:18.102] ROUTING [ROUTING] Registered subscription route for 'SIRENE_E1': 10.0.1.10:8111...║
║ [21:07:22.450] NET_TX  [NET_TX ] Dispatching SEND from [MANUELL_MELDER_E1] to [SENTRAL_KONTROLL] ║
║ [21:07:22.451] INFO    [EXEC   ] Direct send from publisher [MANUELL_MELDER_E1] to [SENTRAL]    ║
║ [21:07:25.890] NET_TX  [NET_TX ] Dispatching BROADCAST from [SENTRAL_KONTROLL] to all sub tags   ║
╠══════════════════════════════════════════════════════════════════════════════════════════════════╣
║  HURTIGHANDLINGER / TESTBESKJEDER:                                                               ║
║   [1] Manuell Brannalarm (E1 -> Sentral)      [2] Kringkast Evakuering (Sentral -> Alle)         ║
║   [3] Røykvarsler Test (Resepsjon -> Sentral) [4] Send Egendefinert Beskjed (Fra/Til/Tekst)      ║
║   [5] Kjør Fullt DSL Brannalarm-Scenario      [6] Vis Routingtabell & Noder                      ║
║   [7] Nullstill Runtime (Reset)               [C] Tøm Logg          [Q] Avslutt                  ║
╠══════════════════════════════════════════════════════════════════════════════════════════════════╣
║  Status: Manuell brannalarm E1 utløst!                                                           ║
║  Kommando / hurtigtast [1-7, C, Q, help] >                                                       ║
╚══════════════════════════════════════════════════════════════════════════════════════════════════╝
```

### Funksjoner i Konsoll-UI:
1. **Full UTF-8 og Norske Tegn**:
   - Initialiserer Windows-konsollen med `SetConsoleOutputCP(CP_UTF8)` og `SetConsoleCP(CP_UTF8)`.
   - Norske tegn (`æ`, `ø`, `å`, `Æ`, `Ø`, `Å`) og Unicode-bokstegn (`╔═╗║╚╝`) vises feilfritt uten mojibake (`Kjører` og `Åpne` vises 100% riktig).
2. **Sanntidslogg i eget rullende vindu**:
   - Fanger automatisk opp logger fra alle C-tråder (webserver, scriptrunner, UDP-transceiver).
   - Fargekodede nivåer: `INFO` (grønn), `DEBUG` (cyan), `ROUTING` (magenta), `NET_TX` (blå), `WARN` (gul) og `ERROR` (rød).
3. **Hurtigtaster [1–7] for sending av meldinger**:
   - **`[1]`**: Manuell brannalarm (`MANUELL_MELDER_E1` → `SENTRAL_KONTROLL`).
   - **`[2]`**: Kringkasting (Broadcast evakueringsordre fra `SENTRAL_KONTROLL` til alle aktive noder).
   - **`[3]`**: Røykvarslertest (`ROYK_RESEPSJON` → `SENTRAL_KONTROLL`).
   - **`[4]`**: **Egendefinert beskjed**: Åpner en interaktiv prompt for å velge avsender-tag, mottaker-tag (eller `*` for broadcast) og meldingstekst.
   - **`[5]`**: Starter fullt EN 54 brannalarm DSL-scenario i bakgrunnen.
   - **`[6]`**: Dumper gjeldende routingtabell og noder direkte til loggvinduet.
   - **`[7]`**: Nullstiller runtime (`RESET`).
   - **`[C]`**: Tømmer loggvinduet.
   - **`[Q]`**: Avslutter serveren og UI-et rent.
4. **Interaktive tekstkommandoer**:
   Skriv direkte i kommandolinjen nederst:
   - `send <fra_tag> <til_tag> <beskjed>`
   - `broadcast <fra_tag> <beskjed>`
   - `run <script.dsl>`
   - `nodes` eller `routing`
   - `reset`, `clear`, `help`, `quit`
5. **2-Veis Sanntidssynkronisering med Three.js Web SCADA**:
   - Når du sender meldinger fra konsollen, dispatchers de i C-motoren, sendes over nettverket via UDP, og strømmes umiddelbart via SSE til Three.js-nettleservinduet.
   - Lysende nettverkspakker flyr i 3D-modellen, branndører lukkes og sirener blinker i synk!

---

## 9. 🚀 Språksyntaks (DSL Spesifikasjon)

Språket støtter alle spesifiserte IoT/SCADA-rutingstrukturer:

### 1. Blokk-deklarasjon av Tags
Definerer publisister og abonnenter med nettverksendepunkter (`ip:port`):
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

## 10. 🔥 Three.js 3D SCADA Visualisering (EN 54 / NFPA 72)

Prosjektet inkluderer en fullverdig, interaktiv **Three.js 3D-visning og SCADA-kontrollpanel** drevet av den **native C-backend-serveren** (`http://localhost:3000`):

1. **Ren C Backend & DSL-motor (`src/web_server.c`, `src/runtime.c`)**:
   - All DSL-parsing (Lexer/AST/Parser) og eksekvering (Runtime, UDP-pakkeutsending, rutingstabellmutasjoner) skjer **utelukkende i C**.
   - C-webserveren strømmer hendelser, pakker, logger og steg direkte til nettleseren via **Server-Sent Events (SSE)** over `/api/events`.
   - Det finnes **ingen DSL-motor i JavaScript**; nettleseren fungerer som en ren 3D SCADA-klient og sender brukerkommandoer tilbake til C via REST API-et (`/api/run`, `/api/trigger`, `/api/routing`, `/api/reset`).
2. **Fysisk 3D-modell av industribygg (`web/js/building_3d.js`)**:
   - **Kjeller (Sone 0)**: Serverrom, serverrack med blinkende status-LED-er, Inergen (N2/Ar/CO2) inertgass-slukkesystem, VESDA aspirasjonsdetektor og teknisk rom.
   - **1. Etasje (Sone 1)**: Hovedinngang, resepsjon, kontorfløy, optiske røykdetektorer, manuelle meldere, optiske xenon-blitzlamper og **motoriserte branndører som fysisk lukkes**.
   - **2. Etasje (Sone 2)**: Storkjøkken med kokeøy, differensial-varmemelder, gassdetektor og **aktive vanntåke/sprinklerdyser som spyler vannpartikler**.
   - **Tak & HVAC (Sone 3)**: Motoriserte røykluker (`ROYKAVTREKK_TAK`) med roterende vifteblader, nødavstenging av ventilasjon (`HVAC_VENTILASJON_STOPP`) og heissjakt med nødretur.
3. **Visuelle partikkeleffekter og animasjoner**:
   - Reell røyk- og flammesimulering i berørte rom utløst av hendelser fra C-runtime.
   - Lysende nettverkspakker som sendes langs 3D-kabelgatene ved `send()`, `multicast()` og `broadcast()`.
   - Eksplodert visning (0–100% etasjeseparasjon) og X-Ray modus.
4. **Web Audio Synthesizer (`web/js/audio.js`)**:
   - Syntetiserer EN 54 Slow Whoop brannsirener (500–1200 Hz), gassutblåsingshves, vanndusj, dørklunk og talevarsling uten eksterne lydfiler.

---

## 11. 📡 API-er, Endepunkter og Porter

| Grensesnitt / Komponent | Protokoll / Type | Port / Rute | Formål |
| :--- | :--- | :--- | :--- |
| **Three.js Web UI** | HTTP GET | `:3000/` | Statisk Three.js SCADA 3D-grensesnitt |
| **Sanntids Hendelsesstrøm** | HTTP SSE (Server-Sent Events) | `:3000/api/events` | Strømmer `PACKET`, `ROUTING`, `STEP`, `LOG` til nettleser |
| **Kjør DSL Script** | HTTP POST | `:3000/api/run` | Kjører oversendt DSL-tekst i bakgrunnen |
| **Scenariokontroll** | HTTP POST | `:3000/api/pause`, `/resume`, `/stop`, `/reset` | Styrer tilstandsmaskinen for scriptkjøring |
| **Manuell Meldingstrigger** | HTTP POST | `:3000/api/trigger` | Sender enkeltbeskjed direkte fra webklienten |
| **Rutingtabell Dump** | HTTP GET | `:3000/api/routing` | Returnerer gjeldende rutingtabell og noder som JSON |
| **Scenario Kildekode** | HTTP GET | `:3000/api/scenario?file=...` | Henter `.dsl`-filer fra `scripts/` |
| **Win32 SCADA Panel** | Win32 Console API | Interaktiv konsoll | Farget terminalgrensesnitt med hurtigtaster `[1-7, C, Q]` |
| **UDP Daemon Listener** | UDP Datagrams | Konfigurerbar (f.eks. `:8001`, `:9001`) | Bakgrunnslytter for noder (mottar pakker og logger) |

---

## 12. 💻 Oppstart, Kjøring og Bygging

### Raskest: PowerShell-skript (Windows)
Kompilerer automatisk, åpner nettleseren på `http://localhost:3000` og starter det interaktive Win32 Konsoll-panelet:

```powershell
.\run_fire_alarm_web.ps1
```

### Direkte via Kommandolinjen
```bash
# Start Win32 SCADA Konsollpanel & Webserver på port 3000
.\pubsub_dsl.exe ui 3000
# eller
.\pubsub_dsl.exe web 3000

# Start kun webserver i bakgrunnen uten konsoll-UI (headless/daemon)
.\pubsub_dsl.exe web 3000 --no-ui

# Kjør et DSL-skript direkte i konsollen
.\pubsub_dsl.exe run scripts/fire_alarm_complex.dsl

# Kjør innebygde enhetstester
.\pubsub_dsl.exe test

# Start en frittstående abonnentnode-lytter
.\pubsub_dsl.exe listen 9110 --tag SIRENE_E1
```

### Docker Testmiljø
Docker-oppsettet oppretter et isolert nettverk (`pubsub_net`) med flere containere som kommuniserer over UDP via DNS-containernavn:
1. `sub_lampe`: Abonnentnode for tag `LAMPE` (port 9001)
2. `sub_varme`: Abonnentnode for tag `VARME` (port 9002)
3. `pub_stue`: Publisistnode for tag `STUE` (port 8001)
4. `dsl_controller`: Kjører DSL-tolkeren (`scripts/docker_scenario.dsl`) og orkestrerer testkommunikasjonen.

```powershell
# Kjøre testen via PowerShell:
.\run_docker_test.ps1

# Kjøre testen via Bash:
./run_docker_test.sh

# Eller direkte med docker compose:
docker compose -f docker/docker-compose.yml up --build --abort-on-container-exit
```

### Bygging fra Kildekode
```bash
# Windows (MinGW GCC):
gcc -Wall -Wextra -O2 -Isrc -o pubsub_dsl.exe src/*.c -lws2_32

# Linux / macOS:
gcc -Wall -Wextra -O2 -Isrc -o pubsub_dsl src/*.c -pthread
# eller:
make
```
