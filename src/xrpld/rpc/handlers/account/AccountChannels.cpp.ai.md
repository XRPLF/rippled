# `AccountChannels.cpp` — RPC Handler for `account_channels`

This file implements the XRPL RPC method `account_channels`, which returns the set of payment channels for which a given account is the source (funding) party. It lives among a cluster of account-query handlers in `src/xrpld/rpc/handlers/account/` — all of which share the same pattern of ledger lookup, owner-directory traversal, and cursor-based pagination.

## Role in the System

Payment channels (`ltPAYCHAN`) allow an account to lock up XRP and make rapid off-ledger micropayments to a destination account. The `account_channels` query gives callers a read-only view of these objects as they exist in a specific ledger version. It is a pure read path — no state is modified and the result is drawn entirely from the immutable `ReadView` of the ledger.

## The Two Functions

`addChannel(jsonLines, line)` is a pure serialization helper. It receives an already-resolved `SLE const&` of type `ltPAYCHAN` and appends a JSON object to the response array. The mandatory fields — `channel_id`, `account`, `destination_account`, `amount`, `balance`, `settle_delay` — are always emitted. Optional fields (`expiration`, `cancel_after`, `source_tag`, `destination_tag`) are emitted only when present in the SLE via the `[~sfField]` optional accessor idiom. The public key gets special treatment: `publicKeyType()` validates that the raw bytes in `sfPublicKey` actually represent a recognized key type before constructing a `PublicKey` object. This guards against encoding garbage if the field is somehow set to a zero-length or malformed value, and it explains why the key is emitted in both human-readable base58 (`jss::public_key`) and raw hex (`jss::public_key_hex`) forms — two representations for two different client needs.

`doAccountChannels(context)` is the RPC entry point. It performs a strict validation sequence before touching the ledger:

1. `account` presence and string-type checks, then `parseBase58<AccountID>` for format validity, then `ledger->exists(keylet::account(accountID))` for on-ledger existence.
2. Optional `destination_account` is parsed the same way, converting the empty-string sentinel to `std::nullopt` to cleanly separate "not provided" from "provided but invalid."
3. `readLimitField` enforces the `RPC::Tuning::accountChannels` bounds of `{min=10, default=200, max=400}`.

## Pagination Design

The handler uses the same compound-marker scheme as `account_lines` and `account_offers`. The marker string is `"<uint256_hex>,<uint64_hint>"`. The `uint256` is the ledger key of the last-seen SLE; the `uint64` is the directory page index (the hint) that makes resumption efficient.

The `forEachItemAfter` function in `DirectoryHelpers.cpp` accepts this hint and attempts to jump directly to the right page in the owner directory before scanning forward. Without it, each resumption of a paginated query would have to walk from the beginning of the owner directory — a significant cost for accounts with many objects.

The traversal asks for `limit + 1` items. Inside the callback, the `limit`-th item speculatively sets `marker` and `nextHint`. The marker is only included in the response when `count` actually reaches `limit + 1`, i.e., when the iterator confirmed a subsequent item exists. This prevents emitting an empty-page marker that would cause the client to make a useless follow-up request. The comment at line 179 calls this out explicitly: both conditions — `count == limit + 1` and `marker.has_value()` — must be true.

## Filtering and Scope

The callback filters SLEs on two criteria: type must be `ltPAYCHAN`, and `sfAccount` must equal `accountID`. The second condition is necessary because an account's owner directory in the XRPL is a generic index of all objects owned by that account, and technically can contain objects of various types. Channels where the queried account is only the *destination* appear in the source account's directory, not the destination's. This means `account_channels` deliberately returns only outgoing (source) channels. An optional `destination_account` filter narrows results further within the callback.

## Marker Security Check

Before starting the directory walk, if a marker is provided, the handler reads the SLE pointed to by `startAfter` and calls `RPC::isRelatedToAccount(*ledger, sle, accountID)`. This check prevents a client from supplying a marker that belongs to a different account's objects, which would cause the traversal to start in the wrong account's namespace. Failing this check returns `rpcINVALID_PARAMS` rather than silently returning unrelated results.

## Error Handling and Cost

The `UNREACHABLE` macro at the null-SLE branch (marked `LCOV_EXCL_START`) documents a defensive invariant: `forEachItemAfter` only calls the callback with keys that exist in the directory, so a null `sleCur` here would imply ledger corruption, not a recoverable error. The code returns `false` (stopping iteration) but the condition is logically impossible in a healthy system.

The handler assigns `Resource::feeMediumBurdenRPC` to `context.loadType`, reflecting that directory traversal over potentially hundreds of items is moderately expensive compared to point lookups. This feeds into the RPC server's resource management, which can throttle or deprioritize expensive client connections.