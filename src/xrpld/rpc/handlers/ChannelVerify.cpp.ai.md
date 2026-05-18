# `ChannelVerify.cpp` — Payment Channel Signature Verification RPC Handler

## Purpose and System Role

`ChannelVerify.cpp` implements the `channel_verify` JSON-RPC command, one half of a two-part off-ledger payment channel authorization protocol. Its counterpart, `ChannelAuthorize` in `admin/signing/ChannelAuthorize.cpp`, produces cryptographic signatures over a (channel ID, amount) pair; this handler takes that same triple — public key, channel ID, amount, and signature — and reports whether the signature is cryptographically valid. Because the check is purely mathematical, the handler never consults ledger state: no database reads, no validators, no network calls.

This design is intentional. Payment channels on the XRPL allow the channel source account to authorize off-ledger micro-payments to a counterparty by signing claim messages. The counterparty can use `channel_verify` to confirm a claim is authentic before accepting it — without submitting anything to the network. Only when the counterparty eventually wants to close or advance the channel do they post a `PaymentChannelClaim` transaction.

## Canonical Message Format

The cryptographic verification is anchored to the exact same serialization that `ChannelAuthorize` uses when producing a signature. Both sides call `serializePayChanAuthorization()` from `include/xrpl/protocol/PayChan.h`:

```cpp
inline void
serializePayChanAuthorization(Serializer& msg, uint256 const& key, XRPAmount const& amt)
{
    msg.add32(HashPrefix::paymentChannelClaim);
    msg.addBitString(key);
    msg.add64(amt.drops());
}
```

The `HashPrefix::paymentChannelClaim` prefix is a 4-byte domain separator that prevents this serialization from being mistakenly interpreted as a ledger hash or transaction, a pattern used consistently throughout XRPL's cryptographic boundary. The channel ID is a 256-bit ledger object key, and the amount is the cumulative authorized drop count serialized as a 64-bit big-endian integer. `doChannelVerify` reconstructs this byte string from the caller's inputs and passes it directly to the generic `verify()` function alongside the supplied public key and signature bytes.

## Dual-Encoding Public Key Parsing

The most structurally interesting part of `doChannelVerify` is its two-phase public key parsing. The primary attempt decodes the key as base58-encoded `AccountPublic` token (the `rXXXXX`-style encoding familiar from XRPL account addresses). If that fails, the handler falls back to raw hexadecimal — first decoding the hex string, then calling `publicKeyType()` to validate the key type (secp256k1 or Ed25519). Only after both attempts fail does it return `rpcPUBLIC_MALFORMED`.

```cpp
pk = parseBase58<PublicKey>(TokenType::AccountPublic, strPk);
if (!pk)
{
    auto pkHex = strUnHex(strPk);
    if (!pkHex)
        return rpcError(rpcPUBLIC_MALFORMED);
    auto const pkType = publicKeyType(makeSlice(*pkHex));
    if (!pkType)
        return rpcError(rpcPUBLIC_MALFORMED);
    pk.emplace(makeSlice(*pkHex));
}
```

This dual-path design makes the RPC accessible to two classes of callers: tools that work with the XRPL's native base58 encoding, and lower-level clients (like validators or custom wallets) that operate directly on raw key bytes. The hex fallback is not merely a convenience — it ensures that tooling which generates keys in non-XRPL-specific contexts can still invoke verification.

## Input Validation Strategy

All four required fields (`public_key`, `channel_id`, `amount`, `signature`) are checked for presence in a single loop before any parsing begins, returning `missing_field_error` immediately. This fail-fast pass avoids partial state setup and keeps error messages unambiguous.

The amount field enforces an explicit type check — `params[jss::amount].isString()` must be true before `to_uint64` is attempted, yielding `rpcCHANNEL_AMT_MALFORMED` on failure. This guards against JSON numeric literals being passed instead of strings, which matters because JSON numbers lose precision for 64-bit integers (XRP drops can reach 10^17). The signature field additionally checks that the decoded bytes are non-empty, returning `rpcINVALID_PARAMS` rather than the more specific channel error, consistent with the pattern used by other signature-handling RPC commands across the codebase.

## Result Contract

The handler returns exactly one key on success:

```json
{ "signature_verified": true | false }
```

There is no exception path for a cryptographically invalid signature — `verify()` returns a boolean. This is a meaningful design choice: the caller should never need to distinguish between a network or server error and a bad signature through exception handling. A bad signature is a valid, expected outcome, so it maps cleanly to `false` rather than an error code.

## Relationship to `ChannelAuthorize`

`doChannelAuthorize` resides in the `admin/signing/` subtree because it requires access to a secret key. `doChannelVerify`, by contrast, carries no such restriction and is exposed without role checks — verifying a signature is a read-only, stateless operation that reveals nothing sensitive. The two handlers form a symmetric pair: `ChannelAuthorize` computes `sign(sk, serialize(channelId, drops))` and returns the hex-encoded result; `ChannelVerify` computes `verify(pk, serialize(channelId, drops), sig)` and reports the boolean outcome.