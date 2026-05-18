# `src/xrpld/rpc/handlers/admin/status/Validators.cpp`

## Role in the System

This file implements `doValidators`, the server-side handler for the `validators` admin RPC command. Its entire body is a single expression: it calls `ValidatorList::getJson()` via the application context and returns the result directly to the RPC framework. The file exists as a necessary binding point between the RPC dispatch table and the validator management subsystem — nothing more.

## Handler Registration and Access Control

The `"validators"` command is registered in `src/xrpld/rpc/detail/Handler.cpp` as `Role::ADMIN` with `NO_CONDITION`:

```cpp
{"validators", byRef(&doValidators), Role::ADMIN, NO_CONDITION},
```

`Role::ADMIN` means the caller must connect via an authenticated admin channel (local socket or explicitly whitelisted admin IP). `NO_CONDITION` means there is no prerequisite on ledger or network state — the handler will respond even if the node hasn't synced. This is appropriate because validator list metadata is available as soon as the application has started and loaded its configuration, independent of ledger progress.

## What the Response Contains

All the content comes from `ValidatorList::getJson()`, which the `ValidatorList.h` header documents as thread-safe ("may be called concurrently"). The method serializes the full state of the application's active UNL (Unique Node List): trusted signing keys, quorum thresholds, publisher information, list sequence numbers, expiry timestamps, and per-publisher disposition flags (`ListDisposition` values such as `accepted`, `expired`, `pending`, `stale`, etc.). None of this serialization or decision logic lives in this handler file.

## Deliberate Minimalism

The handler performs no input validation, no parameter extraction, and no error handling. This is by design: the `validators` RPC carries no request parameters, and any failure modes (e.g., an uninitialized `ValidatorList`) are the responsibility of the subsystem. The pattern is consistent across the `admin/status/` directory — `doValidatorListSites` in the adjacent `ValidatorListSites.cpp` is structurally identical, delegating directly to `ValidatorSite::getJson()`.

## Relationship to Sibling Validators Handlers

Three related handlers in this directory serve distinct diagnostic purposes:

- **`doValidators`** (this file): Full publisher list state — all trusted keys, quorum, expiry, per-list disposition. Answers *what validators does this node trust and why?*
- **`doValidatorInfo`** (`ValidatorInfo.cpp`): Self-referential — describes *this node's own* validator identity (ephemeral signing key, master key, manifest, sequence). Errors if the node is not configured as a validator.
- **`doValidatorListSites`** (`ValidatorListSites.cpp`): Describes the remote HTTP/S sites this node fetches validator lists from, including last-fetch timestamps and status. Answers *where does this node get its UNL?*
- **`doUnlList`** (`UnlList.cpp`, in the parent `admin/` directory): A narrower view — just the set of trusted node public keys without publisher-level metadata.

Together these four handlers expose complementary slices of the same underlying trust infrastructure, with `doValidators` providing the broadest and most operationally useful view for diagnosing validator list health.