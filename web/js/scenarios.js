/**
 * Komplekse Brannalarm Scenarier for PubSub DSL
 * Standarden EN 54 / NFPA 72 kompatible styringsscenarier
 */

const FIRE_ALARM_SCENARIOS = {
    'complex_en54': {
        name: 'EN 54 Kompleks Brannalarm (Hovedforløp)',
        description: 'Fler-faset scenario: optisk røykdeteksjon, pre-alarm, bekreftet brann, soneevakuering, HVAC-nedstenging, røykluker og Inergen slukking.',
        source: `# ==============================================================================
# Komplekst Brannalarmsystem (Complex Fire Alarm System) i PubSub DSL
# Standarden EN 54 / NFPA 72 kompatibelt distribuert styringsscenario
# ==============================================================================

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

    # Hovedsentral
    SENTRAL_KONTROLL::10.0.99.1:8199
    RESERVE_LOOP_S1::EMPTY
}

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

# Initialisering av rutingstabeller for prioriterte noder
subscribers[SIRENE_E1] = [
    subscribe(subscriber_tags, SIRENE_E1):: 10.0.1.10:8111 -> 10.0.1.50:9110
]

subscribers[BRANNDOR_E1] = [
    subscribe(subscriber_tags, BRANNDOR_E1):: 10.0.1.20:8113 -> 10.0.1.60:9115
]

log("=== SYSTEM OPPSTART: Brannalarmsentral overvåker 3 etasjer + teknisk rom ===")
sleep(200)

log("[FASE 1: PRE-ALARM DETEKSJON] Optisk røykdetektor i kontor registrerer røykutvikling")
send(ROYK_KONTOR, SENTRAL_KONTROLL):: "PRE_ALARM: Røykpartikler overstiger 0.15 dB/m i Sone 1 Kontor"
sleep(400)

log("[FASE 2: BEKREFTET BRANN & SONE-REAKSJON] Manuell melder aktiveres ved trapperom")
send(MANUELL_MELDER_E1, BRANNDOR_E1):: "COMMAND_CLOSE: Utløs magnetlås branndør E1"
multicast(MANUELL_MELDER_E1, [SIRENE_E1, BLITZ_VARSLING]):: "ALARM_STAGE_1: Evakuer Sone 1 umiddelbart"
sleep(500)

log("[FASE 3: FULL EVAKUERING & LIVREDDENDE TILTAK] Røyk sprer seg - sentral hever til full alarm")
multicast(SENTRAL_KONTROLL, [ROYKAVTREKK_TAK, HVAC_VENTILASJON_STOPP, HEIS_NODRETUR]):: "EMERGENCY_HVAC_SHUTDOWN: Åpne røykluker tak, stopp ventilasjon, parker heis i 1. etg"
sleep(400)

broadcast(SENTRAL_KONTROLL, subscriber_tags):: "GENERAL_EVACUATION: Talevarsling aktivert - Forlat bygningen via nærmeste nødutgang!"
sleep(500)

log("[FASE 4: SPESIALSONE - SERVERROM INERGEN GASS-SLUKKING]")
send(VESDA_SERVER, INERGEN_GASS_SERVER):: "DISCHARGE_ARMED: 30s nedtelling til Inergen inertgass utblåsning"
send(VESDA_SERVER, SIRENE_SERVER):: "WARNING_GAS: Evakuer serverrom - gassutblåsning forestående"
sleep(400)

log("[FASE 5: NETTVERKSREKONFIGURERING / FEILSØKING UNDER BRANN]")
log("Kabelbrudd oppdaget i Sone 1 sløyfe! Ruter om til redundante sensorer...")
change(publisher_tags, ROYK_KONTOR)::EMPTY
add(publisher_tags, BACKUP_SENSOR_OPTISK):: 10.0.1.50:9110
add(subscriber_tags, RESERVE_SIRENE_BATTERI):: 10.0.1.90:9190
delete(subscriber_tags, SIRENE_E1)

log("[FASE 6: ENDELIG BROADCAST ETTER NETTVERKSRUTINGSENDRING]")
broadcast(SENTRAL_KONTROLL, subscriber_tags):: "STATUS_UPDATE: Redundant sløyfe aktiv, 110 Brannvesen anropt"
log("=== BRANNALARM SCENARIO FULLFØRT MED VELLYKKET RUTERESTRUKTURERING ===")`
    },

    'server_gas_extinguish': {
        name: 'Serverrom Inergen Gass-Slukking',
        description: 'VESDA laser-aspirasjon oppdager ulmebrann i serverrack. Starter 30-sekunders evakueringsnedtelling og utløser Inergen inertgass for oksygenfortrengning.',
        source: `# ==============================================================================
# Spesialscenario: Inergen Gass-slukking i Serverrom (Kjeller Sone 0)
# ==============================================================================

publisher_tags:{
    VESDA_SERVER::10.0.0.10:8101
    VARME_TEKNISK::10.0.0.11:8102
    SENTRAL_KONTROLL::10.0.99.1:8199
}

subscriber_tags:{
    SENTRAL_KONTROLL::10.0.99.1:9199
    SIRENE_SERVER::10.0.0.50:9100
    BLITZ_VARSLING::10.0.1.51:9111
    INERGEN_GASS_SERVER::10.0.0.70:9108
    HVAC_VENTILASJON_STOPP::10.0.3.60:9135
    BRANNVESEN_110::10.0.99.110:9110
}

subscribers[INERGEN_GASS_SERVER] = [
    subscribe(subscriber_tags, INERGEN_GASS_SERVER):: 10.0.0.10:8101 -> 10.0.0.70:9108
]

log("Overvåker serverrack med VESDA lasersensor (følsomhet 0.01% obs/m)...")
sleep(300)

log("VESDA Alarm 1: Partikler oppdaget i luftinnsug bak serverrack 3!")
send(VESDA_SERVER, SENTRAL_KONTROLL):: "VESDA_ALERT_LEVEL1: Partikkelkonsentrasjon stigende"
sleep(400)

log("VESDA Alarm 2 (Bekreftet brann): Lukker brannspjeld og aktiverer varsellamper")
multicast(VESDA_SERVER, [SIRENE_SERVER, HVAC_VENTILASJON_STOPP]):: "CLOSE_DAMPERS: Forsegl serverrom ventilasjonskanaler"
sleep(500)

log("Starter 30 sekunders nedtelling før Inergen gassutløsning...")
send(SENTRAL_KONTROLL, SIRENE_SERVER):: "EVACUATE_COUNTDOWN: Gassutblåsning om 30 sekunder!"
sleep(800)

log("UTBLÅSNING: Inergen inertgass utløses via takdyser. O2 senkes til 12.5% for slukking")
send(SENTRAL_KONTROLL, INERGEN_GASS_SERVER):: "DISCHARGE_GAS_VALVE: Inergen trykksylindere tømmes"
send(SENTRAL_KONTROLL, BRANNVESEN_110):: "ALARM_110: Gass-slukkeanlegg utløst i Serverrom"
sleep(500)

broadcast(SENTRAL_KONTROLL, subscriber_tags):: "STATUS_INERT_SEALED: Brann kvelt i serverrom, rommet er hermetisk forseglet"`
    },

    'kitchen_fire_sprinkler': {
        name: 'Storkjøkken Brann & Sprinklerutløsning',
        description: 'Frityrbrann på storkjøkken i 2. etasje. Differensial-varmemelder utløser automatisk våtsprinkler, kutter gassforsyning og lukker branndører mot spisesal.',
        source: `# ==============================================================================
# Scenario: Storkjøkken Brann & Automatisk Sprinkler (Sone 2)
# ==============================================================================

publisher_tags:{
    VARME_KJOKKEN::10.0.2.10:8121
    GASS_KJOKKEN::10.0.2.11:8122
    MANUELL_MELDER_E2::10.0.2.20:8124
    SENTRAL_KONTROLL::10.0.99.1:8199
}

subscriber_tags:{
    SENTRAL_KONTROLL::10.0.99.1:9199
    SPRINKLER_KJOKKEN::10.0.2.70:9128
    BRANNDOR_E2::10.0.2.60:9125
    SIRENE_E2::10.0.2.50:9120
    HVAC_VENTILASJON_STOPP::10.0.3.60:9135
    TALEVARSLING_BYGG::10.0.99.50:9190
    BRANNVESEN_110::10.0.99.110:9110
}

subscribers[SPRINKLER_KJOKKEN] = [
    subscribe(subscriber_tags, SPRINKLER_KJOKKEN):: 10.0.2.10:8121 -> 10.0.2.70:9128
]

log("Storkjøkken i normal drift under lunsjservering...")
sleep(300)

log("ALARM: Varmemelder over kokeøy registrerer temperaturstigning over 80C!")
send(VARME_KJOKKEN, SENTRAL_KONTROLL):: "HEAT_ALARM_85C: Rask temperaturøkning over stekesone"
sleep(400)

log("Sikkerhetstiltak: Kutter avtrekk og lukker magnetisk branndør mot kantine")
multicast(VARME_KJOKKEN, [BRANNDOR_E2, HVAC_VENTILASJON_STOPP]):: "CONTAIN_FIRE: Lukk branndør E2 og slå av kjøkkenvifte"
sleep(400)

log("Sprinklerampulle sprenger ved 93C - vanntåke og sprinklerdusj aktivert!")
send(VARME_KJOKKEN, SPRINKLER_KJOKKEN):: "SPRINKLER_WATERFLOW: Vanntilførsel åpen 120 L/min"
sleep(500)

log("Varsler gjester i spisesal og anroper 110 Brannvesen")
multicast(SENTRAL_KONTROLL, [SIRENE_E2, TALEVARSLING_BYGG, BRANNVESEN_110]):: "EVACUATE_ZONE2: Brann oppstått på kjøkken - evakuer etasje 2"
sleep(400)

broadcast(SENTRAL_KONTROLL, subscriber_tags):: "STATUS_SPRINKLER_ACTIVE: Sprinkler slukker brannkilde, område evakuert"`
    },

    'manual_call_point': {
        name: 'Manuell Brannmelder (Break-Glass)',
        description: 'En person oppdager brann ved hovedinngangen og trykker inn glasset på manuell melder. Prioritert alarm overstyrer all forsinkelse.',
        source: `# ==============================================================================
# Scenario: Manuell Brannmelder (Direkte Prioritet)
# ==============================================================================

publisher_tags:{
    MANUELL_MELDER_E1::10.0.1.20:8113
    SENTRAL_KONTROLL::10.0.99.1:8199
}

subscriber_tags:{
    SENTRAL_KONTROLL::10.0.99.1:9199
    SIRENE_E1::10.0.1.50:9110
    SIRENE_E2::10.0.2.50:9120
    BLITZ_VARSLING::10.0.1.51:9111
    BRANNDOR_E1::10.0.1.60:9115
    BRANNDOR_E2::10.0.2.60:9125
    HEIS_NODRETUR::10.0.99.60:9195
    TALEVARSLING_BYGG::10.0.99.50:9190
    ROYKAVTREKK_TAK::10.0.3.50:9130
    BRANNVESEN_110::10.0.99.110:9110
}

log("Bygning i ordinær kontortid - overvåking aktiv.")
sleep(200)

log("AKUTT HENDELSE: Manuell melder E1 ved hovedtrapp er trykket inn!")
send(MANUELL_MELDER_E1, SENTRAL_KONTROLL):: "MANUAL_PULL_ALARM: Brann bekreftet av personell ved nødutgang"
sleep(300)

log("Direkte prioritet: Alle branndører frigis umiddelbart og heis sendes til bakkeplan")
multicast(SENTRAL_KONTROLL, [BRANNDOR_E1, BRANNDOR_E2, HEIS_NODRETUR]):: "IMMEDIATE_LIFE_SAFETY: Frigi dørmagneter og lås heis"
sleep(300)

log("Bygningsomfattende alarm via broadcast til alle soner")
broadcast(SENTRAL_KONTROLL, subscriber_tags):: "IMMEDIATE_EVACUATION: Alarm bekreftet. Forlat bygget omgående!"
sleep(400)

log("Automatisk overføring til 110 Brannvesen med GPS-koordinater og sone")
send(SENTRAL_KONTROLL, BRANNVESEN_110):: "DISPATCH_EMERGENCY: Manuell melder E1 utløst - Kategori 1 innsats"`
    },

    'network_loop_failover': {
        name: 'Kabelbrudd & Dynamisk Redundant Ruting',
        description: 'Viser DSL-styrken: Røykdetektor i kontor brenner opp eller mister kabel. Systemet ruter dynamisk om ved å endre og legge til reservetagger og slette defekte noder.',
        source: `# ==============================================================================
# Scenario: Kabelbrudd, Sonesvikt & Dynamisk DSL Rutingendring
# ==============================================================================

publisher_tags:{
    ROYK_KONTOR::10.0.1.11:8112
    ROYK_RESEPSJON::10.0.1.10:8111
    SENTRAL_KONTROLL::10.0.99.1:8199
    RESERVE_LOOP_S1::EMPTY
}

subscriber_tags:{
    SIRENE_E1::10.0.1.50:9110
    SIRENE_E2::10.0.2.50:9120
    BLITZ_VARSLING::10.0.1.51:9111
    BRANNDOR_E1::10.0.1.60:9115
    BRANNVESEN_110::10.0.99.110:9110
}

subscribers[SIRENE_E1] = [
    subscribe(subscriber_tags, SIRENE_E1):: 10.0.1.11:8112 -> 10.0.1.50:9110
]

log("Tester normal kommunikasjon på primærsløyfe...")
send(ROYK_KONTOR, SIRENE_E1):: "LOOP_TEST_PING_OK"
sleep(300)

log("KRITISK FEIL: Flammene har brent av kabel til ROYK_KONTOR og SIRENE_E1!")
log("Starter DSL dynamisk omruting for å opprettholde livssikkerhet...")
sleep(400)

# Sett primærdetektor til EMPTY
change(publisher_tags, ROYK_KONTOR)::EMPTY

# Legg til optisk trådløs reservesensor
add(publisher_tags, BACKUP_SENSOR_WIRELESS):: 10.0.1.99:8188 -> 10.0.1.51:9111

# Slett ødelagt sirene som ikke lenger svarer på ACK
delete(subscriber_tags, SIRENE_E1)

# Legg til nødvarsler på batteri
add(subscriber_tags, BATTERI_SIRENE_TRAPP):: 10.0.1.99:8188 -> 10.0.1.95:9195

log("Omruting fullført! Tester utsending via ny redundant rute...")
send(BACKUP_SENSOR_WIRELESS, BATTERI_SIRENE_TRAPP):: "FAILOVER_ACTIVATED: Trådløs backup overtar sonen"
sleep(400)

broadcast(SENTRAL_KONTROLL, subscriber_tags):: "STATUS_REDUNDANT: Sløyfe rekonfigurert. Full livredning opprettholdt!"`
    }
};

if (typeof window !== 'undefined') {
    window.FIRE_ALARM_SCENARIOS = FIRE_ALARM_SCENARIOS;
}
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { FIRE_ALARM_SCENARIOS };
}
