# `include/xrpl/protocol/PayChan.h`

This file provides the single canonical function for producing the signed payload used in XRPL payment channel claim authorization. Its entire purpose is to ensure that every actor in the system — the channel sender constructing an off-ledger authorization, the RPC layer verifying one, and the ledger transaction engine validating a submitted claim — all sign and verify against exactly the same byte sequence.

## Payment Channel Authorization Model

Payment channels in XRPL allow a sender to lock up XRP that a counterparty (the recipient) can later claim in pieces by presenting a cryptographic authorization. Each authorization attests: "I, the channel owner, permit the recipient to redeem up to *N* drops from channel *C*." The security of this scheme depends entirely on the signed message being unambiguously bound to both a specific channel and a specific amount. A poorly formed message could be replayed across channels or re-interpreted for a different amount.

`serializePayChanAuthorization` produces that canonical, unambiguous payload.

## Design of `serializePayChanAuthorization`

```cpp
inline void
serializePayChanAuthorization(Serializer& msg, uint256 const& key, XRPAmount const& amt)
{
    msg.add32(HashPrefix::paymentChannelClaim);
    msg.addBitString(key);
    msg.add64(amt.drops());
}
```

The function writes three fields into a `Serializer` in strict order:

1. **`HashPrefix::paymentChannelClaim` (`'C','L','M',0x00`)** — A 4-byte domain-separation prefix. Every category of signable object in XRPL uses a distinct `HashPrefix` so that a valid signature for one type of object can never be confused with a valid signature for another. The `paymentChannelClaim` prefix is `0x434C4D00`, constructed at compile time by `detail::make_hash_prefix`. This prefix is protocol-defined and immutable; changing it would break all existing payment channel authorizations.

2. **`key` (the channel's `uint256` keylet)** — The 256-bit identifier of the specific payment channel ledger object. This binds the authorization to one channel only; an authorization cannot be replayed against a different channel even if the amounts and keys happen to match.

3. **`amt.drops()`** — The authorized cumulative amount in drops (the smallest XRP unit), serialized as a 64-bit integer. This is the *ceiling* the recipient is permitted to claim; the on-ledger claim validation checks the running balance does not exceed it.

The function is marked `inline` because it is defined in a header and called from multiple translation units (`ChannelAuthorize.cpp`, `ChannelVerify.cpp`, `PaymentChannelClaim.cpp`, and tests). The inline definition avoids link-time duplication without requiring a separate `.cpp`.

## Where It Is Called

**`ChannelAuthorize.cpp`** (RPC handler) — The channel sender calls this to construct the message before signing with their private key. The resulting signature is returned to the caller for out-of-band delivery to the recipient.

**`ChannelVerify.cpp`** (RPC handler) — The recipient (or any third party) calls this to reconstruct the same message and verify the sender's signature before trusting the authorization.

**`PaymentChannelClaim.cpp`** (transaction preflight) — When the recipient submits a `PaymentChannelClaim` transaction on-ledger, the transaction engine calls this during preflight to verify the embedded authorization signature is valid for the claimed channel and amount. The keylet is derived from the transaction's `sfChannel` field.

The fact that all three call sites use exactly the same function is the key invariant. Any drift — even a byte-order difference — would cause signatures produced by the RPC layer to fail ledger validation. Centralizing the serialization in a single header function prevents that class of bug entirely.

## Dependencies

- **`Serializer`** — Provides `add32`, `addBitString`, and `add64` with well-defined endianness, ensuring consistent byte layout across platforms.
- **`HashPrefix`** — Supplies the domain-separation tag; `paymentChannelClaim` is the specific tag defined for this purpose.
- **`XRPAmount`** — Wraps the drop count as a typed integer; calling `.drops()` extracts the raw `int64_t` for serialization.
- **`base_uint.h`** — Provides the `uint256` type used for the channel keylet.