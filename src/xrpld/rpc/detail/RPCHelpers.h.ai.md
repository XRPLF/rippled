# `RPCHelpers.h` — RPC Utility Functions

This header declares the shared utility layer for XRPL's JSON-RPC server. Rather than a single cohesive abstraction, it is a purposeful collection of free functions that every RPC method handler can reach for when dealing with the three recurring concerns of any ledger query API: normalising input, traversing account-owned objects, and deriving cryptographic material from caller-supplied secrets.

## Account Object Traversal

Two functions serve the `account_objects` family of RPC calls. `getStartHint()` returns the numeric owner-directory position encoded inside an SLE, which is used as a resumption marker when paginating results. The subtlety is trust lines (`ltRIPPLE_STATE`): a trust line is a shared object between two accounts, so the ledger stores *two* back-pointers — `sfLowNode` and `sfHighNode`. `getStartHint()` checks which side of the trust line the querying account occupies and returns the correct one. For all other object types it simply reads `sfOwnerNode`, falling back to zero if the field is absent.

`isRelatedToAccount()` mirrors that logic for ownership checks. It answers the question "does this SLE belong to this account?" with careful per-type rules. Trust lines again need a two-sided check. Standard objects with an `sfAccount` field additionally check `sfDestination`, which is how the server correctly surfaces Escrows, Payment Channels, and Checks that are addressed *to* the caller — those objects are listed in the destination account's owner directory. The function explicitly documents the exception: `ltNFTOKEN_OFFER` is **never** added to a destination's directory, so the destination field is intentionally ignored for that type.

## Pagination Control

`readLimitField()` enforces the `LimitRange` constraints declared in `Tuning.h`. The design detail worth noting is the role check: callers with `isUnlimited()` role (typically administrative or internal connections) bypass the min/max clamp entirely. This allows trusted tooling to request large result sets without the server silently truncating them. The function returns `std::optional<Json::Value>` where `std::nullopt` signals success and a populated `Json::Value` is an inline error response — the inverse of most optional patterns, but consistent with how RPC handlers compose error responses.

## Seed and Keypair Extraction

`getSeedFromRPC()` enforces a strict "exactly one of three" rule: callers must supply exactly one of `passphrase`, `seed` (Base58), or `seed_hex`. Supplying zero or more than one is a hard parameter error. The three parsers are stored as a static array of `(field name, parser)` pairs, so adding a new encoding format later only requires extending that array.

`parseXrplLibSeed()` is a compatibility shim for keys produced by the JavaScript `xrpl-lib` library, which encodes Ed25519 seeds with a non-standard two-byte prefix (`0xE1 0x4B`). The XRPL server never *produces* such encodings, but it accepts them to prevent confusing errors when users paste in keys from their JS tooling.

`keypairForSignature()` orchestrates the full derivation pipeline. It expands the accepted secret fields to include the legacy `secret` name (used without `key_type`), and it layers in explicit algorithm selection via `key_type`. If a XrplLib Ed25519 seed is detected but the caller also passed `key_type: secp256k1`, the function returns an error rather than silently using the wrong algorithm. The function also applies an API version branch: in API v2 and later, an unknown `key_type` returns `rpcBAD_KEY_TYPE`; in v1 it falls back to the older `invalid_field_error` form for backward compatibility. A comment explains why `strcmp` is used instead of pointer equality for `jss::` string constants — MSVC may not deduplicate string literal addresses across translation units.

## Ledger Entry Type Selection

`chooseLedgerEntryType()` builds its lookup table at compile time via the `ledger_entries.macro` include trick. The macro is redefined inline to produce rows of `(canonical name, RPC name, LedgerEntryType enum value)`, expanding the entire protocol-defined set of entry types in one shot. Lookup accepts either the canonical name (case-insensitive) or the RPC name (case-sensitive). This design means new ledger entry types registered in `ledger_entries.macro` are automatically available to the `type` filter parameter with no manual additions needed.

`isAccountObjectsValidType()` acts as a guard over that selection for `account_objects`. It rejects types like `ltAMENDMENTS`, `ltDIR_NODE`, `ltFEE_SETTINGS`, `ltLEDGER_HASHES`, and `ltNEGATIVE_UNL` — these are ledger-global objects that cannot be owned by an account and would never appear in an account's owner directory.

## Subscribe/Unsubscribe Asset Parsing

`parseSubUnsubJson()` handles the dual-asset model introduced with multi-purpose token (MPT) support. A caller may specify either a classic IOU via `currency`/`issuer` fields, or an MPT via `mpt_issuance_id`, but never both in the same object. The function rejects mixed parameters, then parses the appropriate `Asset` variant. Error codes are parameterised by whether the field being parsed is `taker_pays` or `taker_gets`, preserving the semantically distinct error codes the API has historically returned for the two sides of a book subscription.