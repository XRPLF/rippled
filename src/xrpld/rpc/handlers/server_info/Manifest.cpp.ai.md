# `Manifest.cpp` — `doManifest` RPC Handler

## Role and Context

This file implements the `doManifest` RPC endpoint, which allows any caller to look up the validator manifest associated with a given public key. In XRPL's validator infrastructure, a *manifest* is a signed certificate that binds a long-lived master key to a rotating ephemeral signing key, solving a critical operational security problem: if a validator's day-to-day signing key is compromised, the operator can rotate it without requiring every node operator on the network to update their trusted-keys config. The network propagates the new manifest as gossip, and peers update their cached key mappings automatically.

The `ManifestCache` (declared in `include/xrpl/server/Manifest.h`) is the runtime store for this two-level key system. It maintains two maps: one from master key to the most recent `Manifest` struct, and a reverse map from the current ephemeral signing key back to its master key. The `doManifest` handler exposes a read-only view of this cache over RPC.

## The Key Lookup Strategy

The most non-obvious aspect of `doManifest` is how it handles the ambiguity of the caller's input. The user supplies a `public_key` that might be either a master key or a currently-active ephemeral key — the handler doesn't know which. The implementation resolves this without an explicit branch by exploiting the semantics of `getMasterKey`:

```cpp
auto const mk = context.app.getValidatorManifests().getMasterKey(*pk);
auto const ek = context.app.getValidatorManifests().getSigningKey(mk);
```

`getMasterKey` returns the input key unchanged when no reverse mapping exists (i.e., when the key is already a master key or completely unknown). This means `mk` always holds a candidate master key regardless of what the caller supplied. The handler then calls `getSigningKey(mk)` to fetch the active ephemeral key for that master. If `ek` comes back as `std::nullopt`, the handler returns early with only the `requested` field populated — a silent "not found" rather than an error. This is intentional: a missing ephemeral key means the manifest is either absent or revoked, neither of which is an error condition from the perspective of a querying client.

## Input Validation

Validation is two-stage. First, the handler checks for the presence of the `public_key` field using `params.isMember(jss::public_key)`, returning a structured `missing_field_error` if absent. Second, it attempts to decode the string value as a Base58-encoded node public key via `parseBase58<PublicKey>(TokenType::NodePublic, requested)`. A failure here produces `rpcINVALID_PARAMS` injected into the response. These two checks together reject empty strings, malformed keys, and keys of the wrong token type (e.g., account addresses) before any cache lookup occurs.

## Response Construction

The response is built incrementally and is intentionally sparse — optional fields are only added when they are present. The `requested` field always mirrors the caller's input, which is useful for clients that batch multiple queries. The `manifest` field, when present, holds the raw serialized manifest encoded in Base64, suitable for storage or forwarding to another node. The `details` object always includes `master_key` and `ephemeral_key` (re-encoded in Base58), while `seq` and `domain` are conditionally present based on what the cached manifest recorded.

This design means a client can detect the presence of a manifest by checking for the `details` key, and can detect revocation or absence by checking whether `details` is absent after a successful public key parse.

## Relationship to `ValidatorInfo.cpp`

A closely related handler, `doValidatorInfo` in `src/xrpld/rpc/handlers/admin/status/ValidatorInfo.cpp`, follows the same `getMasterKey` → `getManifest` → `getSequence` → `getDomain` pattern, but it skips the input key lookup by starting directly from the node's own configured validation public key. `doManifest` is the public-facing peer: it accepts arbitrary keys from external clients and tolerates lookup misses gracefully.

## Thread Safety

All `ManifestCache` methods called here — `getMasterKey`, `getSigningKey`, `getManifest`, `getSequence`, `getDomain` — are documented as safe for concurrent calls, protected internally by a `std::shared_mutex`. The handler itself holds no locks and carries no mutable state, making it safe to call from any RPC thread without additional synchronization.