# PeerFinder TODO

- Add TestOverlay unit test that passes mtENDPOINTs messages
  around and enforces connection policy using a custom Callback.
- Detect self connection
- Detect duplicate connection
- Migrate portions of Peer handling code
- Track more stats in Bootcache and report them in onWrite
- Test onPeerConnectionChanged() for PROXY handshakes
- Remove uptime, slots from mtENDPOINTS message.
- Finish mtENDPOINTS message-sending rate limit code.
- Make the graceful close state also work for active peers that we want to close.
- Fix FixedPeers correctly
- Keep track of which peers have sent us endpoints, and which endpoints we
  have sent to peers, and try not to send a peer an endpoint that we have
  either received from them, or sent to them lately.
- Use EphemeralCache as the name of the live endpoint cache.
- Rename Endpoint to Address where it makes sense to do so.
- Remove uptime, slots from mtENDPOINTS
- Consider removing featureList from mtENDPOINTS
    * For security reasons we might want to only advertise other people's
      features if we have verified them locally.
- Add firewalled test.
  Consider ourselves firewalled (can't receive incoming connections) if:
    * We have advertised ourselves for incomingFirewalledThresholdSeconds (Tuning.h)
      without receiving an incoming connection.
- We may want to provide hooks in RPC and WSConnection logic that allows incoming
  connections to those ports from public IPs to count as incoming connections for
  the purpose of the firewalled tests.
- Send mtENDPOINTS if all these conditions are met:
    1. We are not full
    2. No incoming peer within the last incomingPeerCooldownSeconds (Tuning.h)
    3. We have not failed the firewalled test
- When an outgoing attempt fails, penalize the entry if it exists in the
  ephemeral cache as if it failed the listening test.
- Avoid propagating ephemeral entries if they failed the listening test.
- Make fixed peers more robust by remembering the public key and using it to
  know when the fixed peer is connected regardless of whether the connection is
  inbound.

- Track more stats in Bootcache and report them in onWrite

- Test onPeerConnectionChanged() for PROXY handshakes

- Remove uptime, slots from mtENDPOINTS message.

- Finish mtENDPOINTS message-sending rate limit code.

- Make the graceful close state also work for active peers that we want to close.

- Fix FixedPeers correctly

- Keep track of which peers have sent us endpoints, and which endpoints we
  have sent to peers, and try not to send a peer an endpoint that we have
  either received from them, or sent to them lately.

- Use EphemeralCache as the name of the live endpoint cache.

- Rename Endpoint to Address where it makes sense to do so.

- Remove uptime, slots from mtENDPOINTS

- Consider removing featureList from mtENDPOINTS
    * For security reasons we might want to only advertise other people's
      features if we have verified them locally.

- Add firewalled test.
  Consider ourselves firewalled (can't receive incoming connections) if:
    * We have advertised ourselves for incomingFirewalledThresholdSeconds (Tuning.h)
      without receiving an incoming connection.

- We may want to provide hooks in RPC and WSConnection logic that allows incoming
  connections to those ports from public IPs to count as incoming connections for
  the purpose of the firewalled tests.

- Send mtENDPOINTS if all these conditions are met:
    1. We are not full
    2. No incoming peer within the last incomingPeerCooldownSeconds (Tuning.h)
    3. We have not failed the firewalled test

- When an outgoing attempt fails, penalize the entry if it exists in the
  ephemeral cache as if it failed the listening test.

- Avoid propagating ephemeral entries if they failed the listening test.

- Make fixed peers more robust by remembering the public key and using it to
  know when the fixed peer is connected regardless of whether the connection is
  inbound.
