# `overlay/detail/Handshake.h` — Peer Handshake and Feature Negotiation

This header is the interface contract for the XRPL peer-to-peer overlay's connection establishment sequence. When a rippled node opens or accepts a TCP connection to another peer, the link is not promoted to the live protocol stream immediately; instead it undergoes a two-phase negotiation — first an SSL/TLS setup, then an HTTP/1.1 Upgrade exchange that carries cryptographic identity proof and capability advertisement. Every function declared here belongs to one of those two phases, and the implementation in `Handshake.cpp` is the sole place in the codebase where those mechanics live.

## Transport Type Aliases

The file opens by pinning down the concrete network types used throughout the overlay:

```cpp
using socket_type = boost::beast::tcp_stream;
using stream_type = boost::beast::ssl_stream<socket_type>;
using request_type  = boost::beast::http::request<boost::beast::http::empty_body>;
using http_request_type  = boost::beast::http::request<boost::beast::http::dynamic_body>;
using http_response_type = boost::beast::http::response<boost::beast::http::dynamic_body>;
```

`request_type` (empty body) is the outbound HTTP upgrade request; the inbound copy the server receives carries a `dynamic_body` and is `http_request_type`. This distinction matters because the server must echo certain request headers back in its `http_response_type`, which is also dynamic-body. Aliasing these here lets the entire overlay subsystem share one authoritative definition.

## MITM Protection via SSL Finished Messages — `makeSharedValue`

The central security primitive is `makeSharedValue`. It derives a 256-bit value from the TLS session's *Finished* handshake messages using OpenSSL's `SSL_get_finished` and `SSL_get_peer_finished`. Both sides hash their own and their peer's Finished messages (each through SHA-512), XOR the two hashes together, and then compress to 256 bits via `sha512Half`. Because TLS Finished messages are keyed to the specific session key material, two peers who share a direct TLS connection will compute identical values, while a man-in-the-middle who terminates two separate TLS sessions will produce a different XOR result on each side.

The implementation guards against a degenerate case: if both Finished hashes are identical (XOR result is zero), the handshake is aborted. This prevents an attacker from constructing a scenario where the cancellation property of XOR yields a convincing but meaningless shared secret.

This approach is acknowledged in the code as "non-standard" with references to an OpenSSL issue and a rippled tracking issue, since TLS channel binding via Finished messages is fragile in the presence of session resumption. The alternative — standard TLS channel bindings — has not yet been adopted.

## Building and Verifying the HTTP Identity Handshake

`buildHandshake` populates a set of HTTP header fields that together prove a node's identity and describe its current state:

- **`Public-Key`** — the node's secp256k1 public key, base58-encoded.
- **`Session-Signature`** — a digital signature over `sharedValue` made with the node's private key. This is the critical proof of identity: it demonstrates the sender holds the private key matching the claimed `Public-Key`, and that the signature is bound to this specific TLS session.
- **`Network-ID`** — an optional numeric tag that allows nodes to detect cross-network connections and reject them early.
- **`Network-Time`** — the sender's current network time, allowing the receiver to reject peers whose clocks have drifted beyond a 20-second tolerance.
- **`Instance-Cookie`** — a per-process unique ID, used implicitly to detect reconnections from the same instance.
- **`Local-IP` / `Remote-IP`** — the node's perceived public IP and what it believes the remote's IP is, enabling the receiver to cross-check connectivity information.
- **`Closed-Ledger` / `Previous-Ledger`** — the hashes of the most recently closed ledger, giving the peer immediate context about the sender's ledger state.

`verifyHandshake` performs the inverse: it validates every field `buildHandshake` populated. The verification order is deliberately layered — network ID and clock skew checks come before the expensive cryptographic signature check. The signature check itself serves the dual purpose of confirming key ownership and confirming end-to-end TLS (no proxy). After verification the function returns the remote's `PublicKey`, which callers use to identify the peer going forward. All failure modes throw `std::runtime_error`, and callers in `ConnectAttempt.cpp` and `PeerImp.cpp` wrap the call in try/catch and disconnect on any exception.

The self-connection check (`publicKey == app.nodeIdentity().first`) prevents a node from successfully handshaking with itself — a real concern if a node accidentally connects to its own listening port.

The IP cross-check logic deserves attention: `Local-IP` (what the sender reports as their public IP) should match the IP address from which the receiver sees the connection arriving. `Remote-IP` (what the sender reports as the receiver's IP) should match the receiver's own known public IP. These checks catch situations where NAT or misconfiguration causes IP mismatches and can also detect certain proxy scenarios.

## HTTP Upgrade Messages — `makeRequest` and `makeResponse`

`makeRequest` constructs the outbound `GET /` HTTP/1.1 upgrade request. It sets `Upgrade` to the supported protocol version list from `supportedProtocolVersions()` and `Connect-As: Peer`. The `Crawl` header advertises whether the node's IP should be publicly discoverable by peer crawlers. Crucially, the built HTTP fields produced by `makeRequest` do not include the identity headers (`Public-Key`, `Session-Signature`, etc.); those are added by a separate `buildHandshake` call on the same fields object.

`makeResponse` produces the `101 Switching Protocols` response. It selects a single concrete protocol version (already negotiated by the caller), mirrors the `Crawl` policy, and calls both `buildHandshake` (for identity fields) and `makeFeaturesResponseHeader` (for capability echo) in one shot.

## Feature Negotiation via `X-Protocol-Ctl`

The header defines a mini-protocol for advertising optional capabilities through the `X-Protocol-Ctl` HTTP header. The wire format is:

```
X-Protocol-Ctl: feature1=value1[,value2]*[; feature2=value1]*
```

Four features are currently defined as `constexpr` string literals:

| Constant | Wire name | Purpose |
|---|---|---|
| `FEATURE_COMPR` | `compr` | LZ4 message compression |
| `FEATURE_VPRR` | `vprr` | Validation/proposal reduce-relay (base squelch) |
| `FEATURE_TXRR` | `txrr` | Transaction reduce-relay |
| `FEATURE_LEDGER_REPLAY` | `ledgerreplay` | Ledger replay subsystem |

Requesters advertise all locally-enabled features via `makeFeaturesRequestHeader`, using `=lz4` for compression (a specific algorithm choice, not just a flag) and `=1` for boolean features. Responders use `makeFeaturesResponseHeader`, which enables a feature in the response only if both the local configuration enables it and the peer's request also declares it. This is a conservative AND-gate: neither side unilaterally enables a feature the other didn't offer.

The helper `getFeatureValue` parses the raw header using a `boost::regex` search, returning the feature's value string if present. `isFeatureValue` checks membership using RFC 2616 token-list semantics, and `featureEnabled` is a convenience wrapper that tests for the value `"1"`. The two template overloads of `peerFeatureEnabled` additionally gate on a local boolean configuration flag, making the final enable/disable decision a single predicate usable by `PeerImp` after the handshake completes.

## Usage Flow

In `ConnectAttempt.cpp` (outbound) the sequence is: compute `makeSharedValue` → call `makeRequest` → call `buildHandshake` on the same field set → send. On receiving the response: compute `makeSharedValue` again → call `verifyHandshake` → if all checks pass, promote the connection. In `PeerImp.cpp` (inbound) the sequence is: the OverlayImpl layer already validated the request; `PeerImp` computes `makeSharedValue` once more (the comment notes this should not fail since OverlayImpl already succeeded) → calls `makeResponse` to build and send the 101 response with identity and capability headers.