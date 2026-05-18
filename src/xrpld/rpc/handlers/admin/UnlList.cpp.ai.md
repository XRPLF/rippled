# `UnlList.cpp` — `unl_list` Admin RPC Handler

## Role in the System

This file implements `doUnlList`, the handler for the `unl_list` admin RPC command. Its single responsibility is to snapshot the node's current Unique Node List (UNL) — the set of validators the node has been configured to track — and report each one's public key along with a boolean indicating whether it is actively trusted for consensus purposes.

The handler is registered in `Handler.cpp` as `{"unl_list", byRef(&doUnlList), Role::ADMIN, NO_CONDITION}`. The `Role::ADMIN` constraint means the endpoint is never exposed to ordinary WebSocket callers; only operator-level connections with admin credentials can reach it. The `NO_CONDITION` flag means no ledger state or network connection is required — the UNL is a node-local configuration concept independent of chain state.

## Listed vs. Trusted

The two-field response per validator (`pubkey_validator` and `trusted`) reflects a meaningful distinction in XRPL's validator model. A validator is *listed* when it appears in the node's configured publisher lists. It is *trusted* only if it also satisfies overlap and quorum thresholds computed by `ValidatorList`. A validator can be listed but not trusted — for instance, if the publisher list it came from does not provide sufficient overlap with other configured lists, or if the node has recently received an updated list that deprioritizes the key. Surfacing both dimensions lets operators diagnose configuration problems: a fully trusted list confirms healthy UNL composition, while listed-but-untrusted entries signal misconfiguration or list staleness.

## Implementation

`doUnlList` is a thin shim. It calls `context.app.getValidators().for_each_listed(...)`, passing a lambda that builds one JSON object per validator and appends it to the `unl` array of the response. Each entry encodes the raw `PublicKey` as a base58 string via `toBase58(TokenType::NodePublic, publicKey)` — the standard Node Public representation used throughout XRPL tooling.

The handler performs no input validation and contains no error paths. There are no parameters to check; the RPC framework handles authentication and request parsing before dispatch, and the validator subsystem is always available in a running node. The result is always a well-formed JSON object.

## Thread Safety via `for_each_listed`

The thread-safety contract lives entirely in `ValidatorList::for_each_listed` (implemented in `detail/ValidatorList.cpp`). That method acquires a `std::shared_lock` on the list's internal `mutex_` before iterating over `keyListings_`, then calls the internal `trusted()` method under the same lock for each key. Because the handler's lambda only reads from the captured `ValidatorList` state through this controlled iterator, `doUnlList` itself needs no synchronization — `for_each_listed` guarantees a consistent point-in-time snapshot even if the validator list is being concurrently updated by a background publisher-list refresh.

## Relationship to Other Handlers

The sibling `admin/status/Validators.cpp` (`doValidators`) and `admin/status/ValidatorListSites.cpp` (`doValidatorListSites`) provide richer validator diagnostics — including publisher list metadata, expiry times, and site fetch state. `doUnlList` is the minimal form: just the set of keys and their trust status. The `BlackList.cpp` sibling follows the same structural pattern (thin shim delegating to an application subsystem), making both handlers easy to audit for security purposes.