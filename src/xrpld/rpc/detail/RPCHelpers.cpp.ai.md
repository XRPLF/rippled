# `src/xrpld/rpc/detail/RPCHelpers.cpp`

This file is the utility belt for the XRPL RPC layer. Rather than belonging to any single command, it centralises cross-cutting concerns — ledger object ownership tests, paginated-query limit management, seed and keypair derivation, ledger entry type selection, and order-book asset parsing — so that individual RPC handlers can stay thin and consistent. Nearly every paginated or cryptographic RPC handler in the codebase calls at least one function from here.

## Ledger Object Ownership — `getStartHint` and `isRelatedToAccount`

Paginated enumeration of account-owned objects requires knowing where in the owner directory to resume. `getStartHint` extracts the 64-bit directory node index that serves as this cursor. For trust lines (`ltRIPPLE_STATE`) it must pick the *correct* side: a trust line has both a `sfLowNode` and a `sfHighNode`, and the relevant one depends on whether the requesting account is the low-limit or high-limit party. Everything else uses the generic `sfOwnerNode`, and objects that lack that field return 0 (start from the beginning).

`isRelatedToAccount` answers whether a given SLE (Serialized Ledger Entry) should be visible to a particular account. The function is deliberately type-aware:

- **Trust lines** (`ltRIPPLE_STATE`) belong to both parties; the function returns true for either side.
- **Objects with `sfAccount`** default to ownership by that account. Escrows, payment channels, and checks also appear in the *destination* account's directory, so `sfDestination` is checked as a fallback — but only when the object has `sfAccount`.
- **NFToken Offers** (`ltNFTOKEN_OFFER`) require a special carve-out. They are *not* added to the destination's directory, so only `sfOwner` is tested even though `sfDestination` may be present. A comment in the code makes this explicit to prevent future contributors from "fixing" the apparent inconsistency.
- **Signer lists** (`ltSIGNER_LIST`) are identified by comparing the SLE's raw ledger key against the canonical `keylet::signers(accountID)` — there is no `sfAccount` field to check on this type.

Together, these two functions power `account_objects` and related handlers that page through an account's owned entries.

## Pagination Limits — `readLimitField`

All paginated RPC calls share the same limit-parameter semantics, enforced in one place by `readLimitField`. It reads the optional `limit` field, applies the per-command `Tuning::LimitRange` (10–400 for most account queries, defined in `Tuning.h`), and clamps the value to `[rmin, rmax]` for non-admin roles. Admin connections (`isUnlimited(context.role)`) bypass clamping, allowing unrestricted result sets. This single function prevents both accidental and malicious abuse of the server's bandwidth by ordinary API consumers.

## Seed Parsing — `parseXrplLibSeed` and `getSeedFromRPC`

The XRPL ecosystem has a legacy compatibility wrinkle: the `xrpl.js` client library encodes Ed25519 seeds with a non-standard two-byte header (`0xE1 0x4B`) before the 16-byte seed material, producing an 18-byte payload after Base58 decoding. The server itself never emits seeds in this form, but `parseXrplLibSeed` detects the pattern and silently unwraps it so that users who copy-paste from their JavaScript wallet don't get a confusing error.

`getSeedFromRPC` enforces the rule that exactly one of `passphrase`, `seed`, or `seed_hex` may be provided. It uses a static dispatch table — an array of `(field name, parser lambda)` pairs — and counts how many are present in the request before invoking any of them. This avoids ambiguity when multiple fields arrive and produces a precise error listing all valid alternatives.

## Keypair Derivation — `keypairForSignature`

This is the most intricate function in the file. It reconciles several overlapping input conventions:

1. The legacy `secret` field accepts a raw string seed and implies secp256k1.
2. The structured form (`key_type` + one of `passphrase`, `seed`, or `seed_hex`) allows choosing Ed25519.
3. The XrplLib non-standard Ed25519 encoding (detected by `parseXrplLibSeed`) overrides the default key type.
4. If an XrplLib seed is detected but the caller explicitly requested non-Ed25519 via `key_type`, the function returns an error rather than silently using the wrong curve.

`jss::secret` and `jss::key_type` are mutually exclusive by design — `secret` is the legacy path that bundles seed format and key type together, while `key_type` is the explicit, forward-compatible API. Mixing them is rejected.

Error message formatting differs by API version: callers on API v2+ receive the structured `rpcBAD_KEY_TYPE` code, while v1 clients get the older `invalid_field_error` string form. There is also a code comment explaining why `strcmp` is used instead of pointer comparison for `jss::` string constants — a known MSVC compiler bug with `constexpr char*` means pointer equality can fail even for the same logical constant.

## Ledger Entry Type Selection — `chooseLedgerEntryType` and `isAccountObjectsValidType`

`chooseLedgerEntryType` builds its lookup table at compile time using the X-macro pattern over `ledger_entries.macro`. Each entry in the macro file expands to a `(canonical_name, rpc_name, type_tag)` tuple. The match logic accepts either the canonical name case-insensitively (e.g., `"ripplestate"`) or the RPC name case-sensitively (e.g., `"state"`), accommodating both historical usage and newer consistent naming. If no `type` parameter is provided, it returns `ltANY`, signalling that all types should be returned.

`isAccountObjectsValidType` then guards the `account_objects` handler from being asked to filter on global consensus objects (`ltAMENDMENTS`, `ltDIR_NODE`, `ltFEE_SETTINGS`, `ltLEDGER_HASHES`, `ltNEGATIVE_UNL`) that can never appear in an account's owner directory. The allow-list-by-exclusion approach means newly added account-owned types automatically become valid without any change here.

## Order Book Asset Parsing — `parseSubUnsubJson`

The subscribe and unsubscribe handlers pass `taker_pays` and `taker_gets` fields through `parseSubUnsubJson` to decode both the traditional `currency`/`issuer` form and the newer MPT (Multi-Purpose Token) issuance ID format. The function maps the field name to the correct pair of error codes so that the caller can distinguish a malformed source asset from a malformed destination asset — important for user-facing error messages when subscribing to order book streams. Mutually exclusive presence of `mpt_issuance_id` alongside `currency` or `issuer` fields is caught explicitly and returns `rpcINVALID_PARAMS`.