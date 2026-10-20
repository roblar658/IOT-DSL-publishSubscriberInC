# ==============================================================================
# Komplekst Brannalarmsystem (Complex Fire Alarm System) i PubSub DSL
# Standarden EN 54 / NFPA 72 kompatibelt distribuert styringsscenario
# ==============================================================================

# ------------------------------------------------------------------------------
# 1. PUBLISHER TAGS: Brann- og gassdetektorer, manuelle meldere og sensorer
# ------------------------------------------------------------------------------
publisher_tags:{
    # Sone 0: Kjeller, Serverrom & Teknisk rom
    VESDA_SERVER::10.0.0.10:8101
    VARME_TEKNISK::10.0.0.11:8102
    
    # Sone 1: Første etasje (Resepsjon, Kontorer & Hovedinngang)
    ROYK_RESEPSJON::10.0.1.10:8111
    ROYK_KONTOR::10.0.1.11:8112
    MANUELL_MELDER_E1::10.0.1.20:8113
    
    # Sone 2: Andre etasje (Kantine, Storkjøkken & Møtesenter)
    VARME_KJOKKEN::10.0.2.10:8121
    GASS_KJOKKEN::10.0.2.11:8122
    ROYK_MOTE::10.0.2.12:8123
    MANUELL_MELDER_E2::10.0.2.20:8124

    # Hovedsentral og nød-loop
    SENTRAL_KONTROLL::10.0.99.1:8199
    RESERVE_LOOP_S1::EMPTY
}

# ------------------------------------------------------------------------------
# 2. SUBSCRIBER TAGS: Aktuatorer, sirener, røykluker, branndører og slukkesystem
# ------------------------------------------------------------------------------
subscriber_tags:{
    SENTRAL_KONTROLL::10.0.99.1:9199

    # Akustisk og optisk varsling
    SIRENE_E1::10.0.1.50:9110
    SIRENE_E2::10.0.2.50:9120
    SIRENE_SERVER::10.0.0.50:9100
    BLITZ_VARSLING::10.0.1.51:9111
    TALEVARSLING_BYGG::10.0.99.50:9190

    # Fysiske sikkerhetsmekanismer og sonesikring
    BRANNDOR_E1::10.0.1.60:9115
    BRANNDOR_E2::10.0.2.60:9125
    HEIS_NODRETUR::10.0.99.60:9195

    # Ventilasjon, røykavtrekk og gassavstenging
    ROYKAVTREKK_TAK::10.0.3.50:9130
    HVAC_VENTILASJON_STOPP::10.0.3.60:9135

    # Aktive slukkesystemer
    SPRINKLER_KJOKKEN::10.0.2.70:9128
    INERGEN_GASS_SERVER::10.0.0.70:9108

    # Nødetater
    BRANNVESEN_110::10.0.99.110:9110
}

# ------------------------------------------------------------------------------
# 3. INITIALISERING OG ABONNEMENTSRUTING (subscribers[tag] = [ ... ])
# ------------------------------------------------------------------------------
subscribers[SIRENE_E1] = [
    # Knytt detektorer i E1 til sirene i E1
    subscribe(subscriber_tags, SIRENE_E1):: 10.0.1.10:8111 -> 10.0.1.50:9110
]

subscribers[BRANNDOR_E1] = [
    # Knytt manuell melder til magnetisk dørutløser
    subscribe(subscriber_tags, BRANNDOR_E1):: 10.0.1.20:8113 -> 10.0.1.60:9115
]

# ------------------------------------------------------------------------------
# 4. BRANNFORLØP OG NØDSTYRING I FIRE FASER
# ------------------------------------------------------------------------------

log("=== SYSTEM OPPSTART: Brannalarmsentral overvåker 3 etasjer + teknisk rom ===")
sleep(200)

log("[FASE 1: PRE-ALARM DETEKSJON] Optisk røykdetektor i kontor registrerer røykutvikling")
send(ROYK_KONTOR, SENTRAL_KONTROLL):: "PRE_ALARM: Røykpartikler overstiger 0.15 dB/m i Sone 1 Kontor"
sleep(300)

log("[FASE 2: BEKREFTET BRANN & SONE-REAKSJON] Manuell melder aktiveres ved trapperom")
send(MANUELL_MELDER_E1, BRANNDOR_E1):: "COMMAND_CLOSE: Utløs magnetlås branndør E1"
multicast(MANUELL_MELDER_E1, [SIRENE_E1, BLITZ_VARSLING]):: "ALARM_STAGE_1: Evakuer Sone 1 umiddelbart"
sleep(300)

log("[FASE 3: FULL EVAKUERING & LIVREDDENDE TILTAK] Røyk sprer seg - sentral hever til full alarm")
# Multicast til kritiske byggsikringstiltak
multicast(SENTRAL_KONTROLL, [ROYKAVTREKK_TAK, HVAC_VENTILASJON_STOPP, HEIS_NODRETUR]):: "EMERGENCY_HVAC_SHUTDOWN: Åpne røykluker tak, stopp ventilasjon, parker heis i 1. etg"
sleep(250)

# Broadcast til alle operative varslere og enheter i bygget
broadcast(SENTRAL_KONTROLL, subscriber_tags):: "GENERAL_EVACUATION: Talevarsling aktivert - Forlat bygningen via nærmeste nødutgang!"
sleep(300)

log("[FASE 4: SPESIALSONE - SERVERROM INERGEN GASS-SLUKKING]")
send(VESDA_SERVER, INERGEN_GASS_SERVER):: "DISCHARGE_ARMED: 30s nedtelling til Inergen inertgass utblåsning"
send(VESDA_SERVER, SIRENE_SERVER):: "WARNING_GAS: Evakuer serverrom - gassutblåsning forestående"
sleep(250)

log("[FASE 5: NETTVERKSREKONFIGURERING / FEILSØKING UNDER BRANN]")
log("Kabelbrudd oppdaget i Sone 1 sløyfe! Ruter om til redundante sensorer...")

# Dynamisk oppdatering av rutingstabeller via DSL
change(publisher_tags, ROYK_KONTOR)::EMPTY
add(publisher_tags, BACKUP_SENSOR_OPTISK):: 10.0.1.99:8188 -> 10.0.1.50:9110
add(subscriber_tags, RESERVE_SIRENE_BATTERI):: 10.0.1.99:8188 -> 10.0.1.90:9190
delete(subscriber_tags, SIRENE_E1)

log("[FASE 6: ENDELIG BROADCAST ETTER NETTVERKSRUTINGSENDRING]")
broadcast(SENTRAL_KONTROLL, subscriber_tags):: "STATUS_UPDATE: Redundant sløyfe aktiv, 110 Brannvesen anropt"

log("=== BRANNALARM SCENARIO FULLFØRT MED VELLYKKET RUTERESTRUKTURERING ===")
