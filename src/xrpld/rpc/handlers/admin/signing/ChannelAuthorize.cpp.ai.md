# `ChannelAuthorize.cpp` — Payment Channel Claim Signing Handler

## Role in the System

This file implements `doChannelAuthorize`, the RPC handler behind the `channel_authorize` API command. Its sole purpose is to produce a cryptographic signature that authorizes a payment channel claim for a specified amount. The resulting signature can then be handed to a channel recipient, who redeems it on-ledger via a `PaymentChannelClaim` transaction without needing the channel owner to be online at redemption time. The signing counterpart that validates these signatures is `doChannelVerify` in `ChannelVerify.cpp`, which uses the same `serializePayChanAuthorization` serialization to reconstruct the message and verify it.

## Access Control Gate

The handler opens with a two-condition authorization check:

```cpp
if (context.role != Role::ADMIN && !context.app.config().canSign())
    return RPC::make_error(rpcNOT_SUPPORTED, "Signing is not supported by this server.");
```

This reflects the XRPL server's design philosophy around key material exposure. Signing with a secret key on a public-facing node is a security risk. The check allows signing only when the caller is authenticated as `ADMIN` (i.e., connecting over the admin interface), or when the node has been explicitly configured to allow signing (`[signing_support]` in the config). Non-admin callers on a default-configuration public node are rejected outright.

## Validation Pipeline

Input validation proceeds in a deliberate sequence that prioritizes early exit for cheap checks:

1. **Required field presence** — `channel_id` and `amount` are checked in a loop before touching key material. Missing either produces a `missing_field_error`.

2. **Key material presence** — A subtle compatibility guard covers legacy clients: if neither `key_type` nor `secret` is present, it emits a `missing_field_error` for `secret`. If `key_type` is present but `secret` is absent, `keypairForSignature` handles the error internally (the caller might be providing `seed`, `seed_hex`, or `passphrase` instead). The early check only fires to give a clear error when both fields are completely absent.

3. **Keypair derivation** — `RPC::keypairForSignature` in `RPCHelpers.cpp` resolves the full credential pipeline: it accepts `secret`, `passphrase`, `seed`, or `seed_hex` and supports both `secp256k1` (default) and `ed25519` key types. Notably, if `key_type` is specified, the plain `secret` field is rejected — the caller must use a more explicit seed encoding. The function returns `std::optional<std::pair<PublicKey, SecretKey>>` and writes errors into the `result` JSON value passed by reference. The `XRPL_ASSERT` following the call enforces the invariant that exactly one of {valid keypair, populated error} is true; the `if (!keyPair || RPC::contains_error(result))` guard below it handles both failure cases defensively.

4. **Channel ID format** — The hex string is parsed into a `uint256`. An incorrect length or non-hex characters yields `rpcCHANNEL_MALFORMED`.

5. **Amount format** — The amount must arrive as a JSON string, not a number. `params[jss::amount].isString()` is checked before calling `to_uint64`; if the value is a JSON integer or the string can't be parsed as a 64-bit unsigned integer, `rpcCHANNEL_AMT_MALFORMED` is returned. Requiring a string type sidesteps JSON parser precision loss for large uint64 values that exceed JavaScript's safe integer range.

## Message Serialization and Signing

Once all inputs are valid, the message to sign is constructed by `serializePayChanAuthorization` (defined inline in `PayChan.h`):

```cpp
msg.add32(HashPrefix::paymentChannelClaim);  // 'CLM\0' — 4-byte domain separator
msg.addBitString(key);                        // 32-byte channel ID
msg.add64(amt.drops());                       // 8-byte XRP amount in drops
```

The `HashPrefix::paymentChannelClaim` value (`'CLM'`) acts as a domain separator that prevents this signature from being misinterpreted as a signature over any other data structure in the XRPL protocol. This is a standard pattern across XRPL signing operations — every signable object type has its own prefix. The resulting 44-byte message is signed with `sign(pk, sk, msg.slice())`, and the raw binary signature is hex-encoded for JSON transport via `strHex`.

## Error Handling Design

The `try/catch` around the `sign()` call is marked `// LCOV_EXCL_START` — the test suite can't trigger it under normal conditions. It exists as a last-resort defensive wrapper, since `sign()` implementations theoretically could throw if underlying cryptographic operations encounter unexpected state. In practice, for both `secp256k1` and `ed25519` paths, `sign()` operates on validated key material and a well-formed serializer buffer, so exceptions are not expected.

## Relationship to `ChannelVerify`

The `doChannelVerify` handler in `ChannelVerify.cpp` is the exact inverse: it accepts `public_key`, `channel_id`, `amount`, and `signature`, reconstructs the same serialized message via `serializePayChanAuthorization`, and calls `verify()` to confirm the signature. Crucially, `doChannelVerify` carries no admin restriction — verifying a signature is a read-only, key-material-free operation safe for any caller. The split between the two handlers cleanly separates the privileged signing operation from the unprivileged verification operation.