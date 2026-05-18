# `src/xrpld/overlay/detail/Handshake.cpp`

## Role in the System

This file implements the application-layer handshake that XRPL peer nodes exchange immediately after a TLS connection is established. Its purpose is twofold: it establishes mutually authenticated peer identity using secp256k1 signatures and cryptographically binds those identities to the specific TLS session in use, while simultaneously negotiating which optional protocol features the two peers will use for the lifetime of the connection.

The handshake sits at the boundary between the raw TLS stream (provided by Boost.Asio/Beast) and the `PeerImp` / `ConnectAttempt` layer that manages live peer state. Neither side begins exchanging XRPL protocol messages until `verifyHandshake` succeeds.

## Cryptographic Shared Value

The most subtle piece in this file is `makeSharedValue`. Before any application header is validated, both sides independently derive a `uint256` from the TLS session itself using `hashLastMessage`, which calls either `SSL_get_finished` or `SSL_get_peer_finished` to retrieve the TLS finished messages. Each message is SHA-512 hashed, the two resulting 512-bit values are XOR-ed, and the XOR result is reduced to 256 bits via `sha512Half`.

This design is explicitly flagged as non-standard (see the inline comment referencing OpenSSL issue #5509 and XRPLF/rippled #2413). The reason for this approach: the TLS finished message is derived from a transcript of the full handshake, so both peers compute the same value only when they are literally the same TLS session endpoints. A man-in-the-middle would terminate two separate TLS sessions, producing different finished messages and therefore a different shared value. A guard against a degenerate edge case — two identical finished messages whose XOR is zero — is present and treated as a hard failure.

The `sharedValue` flows directly into `buildHandshake` and `verifyHandshake`. The connecting peer signs it with its node private key; the accepting peer verifies that signature against the claimed public key. The comment in `verifyHandshake` makes the two-for-one security property explicit: the verification simultaneously proves possession of the private key *and* that the TLS session is end-to-end with the claimed node, not proxied.

## Handshake Construction and Verification

`buildHandshake` populates a set of HTTP fields that are attached to either an outbound request (by `ConnectAttempt`) or the 101 Switching Protocols response (by `PeerImp`). The fields inserted include:

- **`Network-ID`** — optional; allows early detection of cross-network connections before wasting resources on full negotiation.
- **`Network-Time`** — the local XRPL clock value; the recipient enforces a ±20-second tolerance in `verifyHandshake`. This prevents replay and clock-skew attacks.
- **`Public-Key`** — base58-encoded secp256k1 node identity key.
- **`Session-Signature`** — the shared value signed by the node private key, base64-encoded.
- **`Instance-Cookie`** — a runtime-unique identifier used elsewhere to detect duplicate connections.
- **`Server-Domain`** — optional TOML domain hint.
- **`Local-IP`** / **`Remote-IP`** — only inserted if the corresponding IP is public and non-unspecified; used during `verifyHandshake` to cross-check that both sides agree on the addresses they observe for each other, which can surface NAT misconfigurations.

`verifyHandshake` checks every one of these fields defensively and throws `std::runtime_error` on any failure. The use of exceptions rather than error codes is a deliberate pattern in this layer (noted in the `.ai.json`): callers in `ConnectAttempt` and `OverlayImpl` wrap the call in a try/catch and tear down the connection on any exception, making the control flow clean without threading error codes through multiple levels.

The self-connection check (`publicKey == app.nodeIdentity().first`) catches the case where a node accidentally connects to itself — something the `peerFinder` also guards against at the TCP level, but a second check here catches cases that slip through (e.g., connecting via a different IP).

## Feature Negotiation

Four optional protocol extensions are negotiated via the `X-Protocol-Ctl` HTTP header: LZ4 message compression (`compr`), ledger replay (`ledgerreplay`), transaction reduce-relay (`txrr`), and validation/proposal reduce-relay (`vprr`). The format is `feature=value[;feature=value]*`.

The asymmetry between `makeFeaturesRequestHeader` and `makeFeaturesResponseHeader` captures the standard capability negotiation pattern: the initiator unconditionally advertises everything it supports; the responder only echoes back features that are both locally configured *and* present in the request. This ensures both peers arrive at the same set of enabled features without a separate acknowledgment round-trip.

`getFeatureValue` uses `boost::regex` to extract a feature's value from the `X-Protocol-Ctl` string, returning `std::nullopt` when absent. `isFeatureValue` layers RFC 2616 token-list semantics on top via `beast::rfc2616::token_in_list`, which correctly handles comma-separated value lists. `featureEnabled` is a thin convenience wrapper that checks for the value `"1"`. The header-only `peerFeatureEnabled` template (in `Handshake.h`) combines local configuration with peer-reported capability for use throughout the peer management layer.

## HTTP Request and Response Assembly

`makeRequest` builds the outbound HTTP/1.1 GET request that initiates the peer connection. It follows the WebSocket-style protocol upgrade pattern: `Connection: Upgrade`, `Upgrade: <supported protocol versions>`, `Connect-As: Peer`. This allows the entire peer handshake to look like an HTTP upgrade from the perspective of any intermediate infrastructure.

`makeResponse` constructs the 101 Switching Protocols response, calls `makeFeaturesResponseHeader` to echo negotiated features back, and then delegates to `buildHandshake` to insert all the identity and authentication fields. The `Upgrade` field in the response is set to the single agreed `ProtocolVersion`, narrowing from the list of versions in the request.

## Caller Context

`ConnectAttempt` uses `makeSharedValue` → `makeRequest` + `buildHandshake` → async HTTP write, then on the response uses `makeSharedValue` + `verifyHandshake` to complete the outbound side. `PeerImp::doAccept` handles the inbound side: `OverlayImpl` already ran `verifyHandshake` once to extract the public key for routing, and `PeerImp` calls `makeSharedValue` a second time to produce the shared value needed for `makeResponse`. The double computation is safe and intentional — the TLS state is stable at that point.