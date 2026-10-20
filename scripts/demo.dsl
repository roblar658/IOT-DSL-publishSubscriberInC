# ==============================================================================
# PubSub DSL Demonstration Script
# Demonstrating all syntax features requested by the specification
# ==============================================================================

publisher_tags:{
    STUE::127.0.0.1:8001
    SOVEROM::127.0.0.1:8002
    SPISE::127.0.0.1:8003
}

subscriber_tags:{
    LAMPE::127.0.0.1:9001
}

# Subscribers rule block
subscribers[LAMPE] = [
    # subscribe(subscriber_tags, tag):: from_ip:from_port -> to_ip:to_port
    subscribe(subscriber_tags, LAMPE):: 127.0.0.1:8001 -> 127.0.0.1:9001
]

log("Step 1: Direct unicast send(STUE, LAMPE)")
send(STUE, LAMPE):: "Slå på lys i stua"
sleep(100)

log("Step 2: Dynamically add subscriber tags")
# add(subscriber_tags, tag):: from_ip:from_port -> to_ip:to_port
add(subscriber_tags, VARME):: 127.0.0.1:8001 -> 127.0.0.1:9002

# add(subscriber_tags, tag) (medfører tag::EMPTY)
add(subscriber_tags, RADIO)

log("Step 3: Multicast send from STUE to [LAMPE, VARME]")
multicast(STUE, [LAMPE, VARME]):: "Aktiver kveldsprofil (21C og dempet belysning)"
sleep(100)

log("Step 4: Broadcast send from STUE to all subscriber tags")
broadcast(STUE, subscriber_tags):: "Alarm: Bevegelse oppdaget i gangen"
sleep(100)

log("Step 5: Change publisher STUE endpoint and set SPISE to EMPTY")
change(publisher_tags, STUE):: 127.0.0.1:8001 -> 127.0.0.1:8080
change(publisher_tags, SPISE)::EMPTY

log("Step 6: Delete subscriber tag VARME")
delete(subscriber_tags, VARME)

log("Step 7: Final broadcast to remaining active subscribers")
broadcast(STUE, subscriber_tags):: "System status: OK etter rutingendringer"
