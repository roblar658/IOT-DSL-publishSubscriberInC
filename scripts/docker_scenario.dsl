# ==============================================================================
# Docker Multi-Node Test Scenario
# Uses Docker container DNS hostnames (sub_lampe, sub_varme, pub_stue)
# ==============================================================================

publisher_tags:{
    STUE::pub_stue:8001
    SOVEROM::pub_stue:8002
    SPISE::pub_stue:8003
}

subscriber_tags:{
    LAMPE::sub_lampe:9001
    VARME::sub_varme:9002
}

# Initial routing rule for LAMPE
subscribers[LAMPE] = [
    # subscribe(subscriber_tags, tag):: from_ip:from_port -> to_ip:to_port
    subscribe(subscriber_tags, LAMPE):: pub_stue:8001 -> sub_lampe:9001
]

log("[DOCKER TEST] Pausing 2 seconds for subscriber containers to finish socket initialization...")
sleep(2000)

log("[DOCKER TEST 1] Direct unicast send(STUE, LAMPE)")
send(STUE, LAMPE):: "HELLO_LAMPE_FROM_STUE"
sleep(500)

log("[DOCKER TEST 2] Multicast send to [LAMPE, VARME]")
multicast(STUE, [LAMPE, VARME]):: "MULTICAST_NIGHT_MODE"
sleep(500)

log("[DOCKER TEST 3] Broadcast to all subscriber tags")
broadcast(STUE, subscriber_tags):: "BROADCAST_FIRE_ALARM"
sleep(500)

log("[DOCKER TEST 4] Change publisher STUE endpoint and set SPISE to EMPTY")
change(publisher_tags, STUE):: pub_stue:8001 -> pub_stue:8080
change(publisher_tags, SPISE)::EMPTY

log("[DOCKER TEST 5] Dynamically add subscriber tag RADIO with EMPTY endpoint")
add(subscriber_tags, RADIO)

log("[DOCKER TEST 6] Delete subscriber tag VARME")
delete(subscriber_tags, VARME)

log("[DOCKER TEST 7] Final broadcast after route mutation (only LAMPE will receive, RADIO is EMPTY)")
broadcast(STUE, subscriber_tags):: "FINAL_BROADCAST_STATUS_OK"
sleep(1000)

log("[DOCKER TEST] Scenario execution complete!")
