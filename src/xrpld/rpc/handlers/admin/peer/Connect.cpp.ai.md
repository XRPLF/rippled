# `Connect.cpp` — Admin RPC Handler for Manual Peer Connection

This file implements `doConnect`, the administrative RPC handler that lets an operator instruct a running XRPL node to establish an outbound peer connection to a specific IP address and port. It lives in `src/xrpld/rpc/handlers/admin/peer/` alongside sibling handlers for peer management (`Peers.cpp`, `PeerReservationsAdd.cpp`, etc.), and represents the operator-driven counterpart to the overlay's automatic peer discovery logic.

## Role in the System

The XRPL overlay network discovers and connects to peers autonomously via the PeerFinder subsystem. However, there are legitimate operational reasons to force a connection manually: connecting to a known validator that isn't in the bootstrap list, bridging nodes in an isolated test network, or recovering connectivity after a network partition. `doConnect` exposes this capability through the privileged RPC interface. In `Handler.cpp`, it is registered as:

```cpp
{"connect", byRef(&doConnect), Role::ADMIN, NO_CONDITION}
```

The `Role::ADMIN` requirement means only callers with administrative credentials can invoke it — this prevents arbitrary external actors from directing the node's peering behavior.

## Validation Logic

`doConnect` performs three sequential guards before touching the overlay:

**Standalone mode check.** If the node is running in standalone mode (`context.app.config().standalone()`), no peer connections are possible by definition. The handler returns `rpcNOT_SYNCED` immediately. Using `rpcNOT_SYNCED` rather than a more specific error code is a mild semantic misuse, but it's consistent with how the codebase signals "this operation is meaningless in the current operating mode."

**Required `ip` field.** The IP address is mandatory. Its absence returns a structured missing-field error via `RPC::missing_field_error(jss::ip)`, which produces a well-formed JSON error response with the field name embedded.

**Optional `port` type check.** The port is optional but type-constrained: if present, it must be convertible to `Json::intValue`. The check uses `isConvertibleTo` rather than `isInt` to tolerate JSON numbers arriving as floats that happen to be whole-number values, without accepting strings. If the port is absent, the fallback is `DEFAULT_PEER_PORT` (2459, the IANA-assigned XRPL peer port, defined in `SystemParameters.h`).

## IP Parsing and the Silent No-Op

After validation, the IP string is parsed into a `beast::IP::Endpoint` via `beast::IP::Endpoint::from_string()`. This function returns an "unspecified" endpoint object when parsing fails rather than throwing. The guard:

```cpp
if (!is_unspecified(ip))
    context.app.getOverlay().connect(ip.at_port(iPort));
```

means that a syntactically invalid IP string causes the connection attempt to be silently skipped. The response to the caller is still "attempting connection to IP:..." — so there is no feedback distinguishing a well-formed IP from a malformed one. This is a subtle behavioral gap: the operator might believe a connection was initiated when in fact the IP failed to parse. A comment in the source (`// XXX Might allow domain for manual connections`) suggests the original author was aware the IP parsing is limited and that DNS resolution was considered but never implemented.

## Asynchronous Delegation

`Overlay::connect()` is declared as returning `void` and documented as non-blocking: "The call returns immediately, the connection attempt is performed asynchronously." `doConnect` therefore returns an informational string immediately without waiting for the TCP handshake to complete or fail. The response message (`"attempting connection to IP:... port: ..."`) signals intent, not outcome. There is no callback or status-polling mechanism exposed at the RPC layer; operators who want confirmation must query the peer list afterward.

## Design Observations

The function is intentionally thin — it performs no routing logic, no retry management, and no deduplication of already-connected peers. All of that responsibility belongs to `OverlayImpl`, which is the concrete implementation behind the `Overlay` abstract interface. This keeps the RPC handler strictly in the role of input validation and dispatch, consistent with the rest of the `handlers/admin/` layer.

The lack of range validation on the port integer (no check that `iPort` is in `[1, 65535]`) means a caller could pass `0` or a negative value without triggering `rpcINVALID_PARAMS`. The overlay implementation absorbs any such misuse downstream.