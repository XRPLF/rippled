# `Feature.cpp` — RPC Handler for Amendment Queries and Veto Control

This file implements `doFeature`, the single RPC handler that exposes the XRPL amendment system to external clients and operators. It sits at the boundary between two concerns: read-only status reporting (any client can ask which amendments are known, enabled, or approaching activation) and privileged mutation (administrators can mark amendments as vetoed or un-vetoed, preventing or allowing the local validator from voting for them).

## Role in the Amendment Lifecycle

XRPL's amendment process is a multi-phase consensus mechanism: validators express support, amendments gain a supermajority, wait two weeks, and then activate permanently. The `AmendmentTable` manages the voting state internally, while `getMajorityAmendments()` (from `View.h`) reads directly from the validated ledger's amendment SLE (Stored Ledger Entry) to report which amendments have achieved validator supermajority but have not yet crossed the activation threshold. `doFeature` bridges both: it pulls the current majority snapshot from the ledger and overlays it onto the `AmendmentTable`'s JSON output.

## Two Operating Modes

The handler operates in one of two modes depending on whether the `feature` parameter is present.

**Listing all features** (no `feature` param): `table.getJson(isAdmin)` returns a JSON object keyed by amendment hash, which is then augmented with majority timestamps sourced from `getMajorityAmendments(*valLedger)`. The majority data comes from the ledger itself rather than the `AmendmentTable`, because the table's internal vote tracking and the ledger's stored state can briefly diverge — reading from the validated ledger is authoritative. The result is wrapped in a top-level `features` key.

**Querying or controlling a specific feature** (with `feature` param): The handler first validates that the parameter is a string (returning `rpcINVALID_PARAMS` otherwise), then resolves the feature identifier in two steps. It tries `table.find(name)` to look up by human-readable name; if that fails, it attempts `feature.parseHex(...)` to interpret the parameter as a 256-bit amendment hash. This two-step resolution allows callers to use either the amendment name (e.g., `"PayChan"`) or its raw hash. If both fail, `rpcBAD_FEATURE` is returned. The same error code is also returned if `table.getJson(feature, isAdmin)` yields a falsy value, which catches the edge case where the resolved hash doesn't correspond to a known amendment.

## Authorization Model

The handler is registered in `Handler.cpp` with `Role::USER`, meaning any connected client can invoke it. However, the mutation path — setting the `vetoed` field — is gated inside the handler by an explicit `isAdmin` check. Non-admin callers who supply `vetoed` receive `rpcNO_PERMISSION`. This split is deliberate: the registration role controls routing-level access, but the handler itself enforces fine-grained per-operation authorization.

The `isAdmin` flag also propagates into both overloads of `table.getJson()`, letting the `AmendmentTable` implementation conditionally include or hide sensitive metadata (such as vote counts or internal voting state) from public consumers.

## The Majority Timestamp Convention

Majority timestamps in the response are emitted as raw `time_since_epoch().count()` ticks from `NetClock::time_point`. This is XRPL's network clock in seconds since the Ripple epoch (January 1, 2000), not Unix time. Callers must account for this offset. The choice to expose the raw epoch count rather than an ISO string is consistent with how other time values appear in XRPL's RPC responses: compact, machine-parseable, and epoch-agnostic.

## Structural Pattern

Like the other handlers in this directory (`Fee.cpp`, `ServerInfo.cpp`, `Manifest.cpp`), `doFeature` is a thin orchestration layer: it validates inputs, delegates to `AmendmentTable` and `LedgerMaster`, and assembles the JSON response. No amendment logic lives here. This keeps the RPC layer decoupled from consensus machinery — changes to how voting works or how features are stored do not require touching this file.