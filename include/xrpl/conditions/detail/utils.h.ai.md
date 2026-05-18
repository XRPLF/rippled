# `include/xrpl/conditions/detail/utils.h`

## Role and Purpose

This header provides a minimal, purpose-built decoder for binary data encoded with X.690 Distinguished Encoding Rules (DER). It sits inside the `xrpl::cryptoconditions::der` sub-namespace and exists solely to support deserialization of XRPL crypto-conditions — in practice, exclusively the `PreimageSha256` fulfillment type. The comment in the file makes this explicit: only "the bare minimum needed to support PreimageSha256" is implemented. This deliberate scope limitation is the right call for a consensus-critical system: a full ASN.1/DER stack would introduce unnecessary attack surface.

## DER Background

DER encodes data as TLV (Type-Length-Value) triplets. The first byte is the *identifier octet*, whose top two bits encode the tag class (Universal, Application, Context-specific, Private), bit 5 encodes whether the value is primitive or constructed, and the lower five bits hold the tag number. This is followed by one or more *length octets* and then the value bytes. The `Preamble` struct directly maps to this layout: `type` holds the top three bits of the identifier (class + encoding flag), `tag` holds the lower five, and `length` holds the decoded content length.

## Preamble Parsing

`parsePreamble()` consumes bytes from a `Slice` by reference — the "cursor" pattern common throughout this module. After a successful call, the `Slice` has been advanced past the identifier and length octets, positioned exactly at the start of the value. This composability is the reason the output is a `Preamble` value rather than individual fields: the caller gets the decoded metadata and a correctly positioned cursor in one step, ready to call `parseOctetString` or `parseInteger` next.

The function handles both short-form length (single byte, MSB clear) and long-form length (MSB set, lower 7 bits = count of subsequent length bytes). It rejects several malformed inputs via `std::error_code`: buffers shorter than two bytes (`error::short_preamble`), long-form tags (`error::long_tag`), indefinite-length encodings (long-form count of zero, `error::malformed_encoding`), length values that exceed `sizeof(std::size_t)` (`error::large_size`), and long-form lengths that encode zero (`error::malformed_encoding`).

Long-form tags (tag number 0x1F in the identifier byte) are explicitly unsupported and flagged with `error::long_tag`. This is appropriate because the conditions RFC only uses low-numbered context-specific tags, so supporting multi-byte tag encoding would only matter for a general-purpose ASN.1 parser.

## Tag Class Predicates

Six inline predicates — `isPrimitive`, `isConstructed`, `isUniversal`, `isApplication`, `isContextSpecific`, `isPrivate` — decode the class and encoding bits of `p.type` via direct bitmask operations. They are intentionally zero-cost (all `inline`, taking `const&`) and exist primarily for readable validation logic. `PreimageSha256::deserialize()` uses `isPrimitive(p) && isContextSpecific(p)` to verify that a parsed preamble has the exact tag form required by the RFC, rejecting anything else with `error::incorrect_encoding`.

## `parseOctetString`

This function copies `count` bytes from the front of a `Slice` into a heap-allocated `Buffer` and advances the slice. The 65535-byte hard cap is a second line of defense: `PreimageSha256` itself caps preimages at 128 bytes (`maxPreimageLength`), but `parseOctetString` refuses anything over 64 KiB regardless of caller intent. This guards against a malformed length field producing an allocation of an arbitrary size.

## `parseInteger`

The template `parseInteger<Integer>()` reads `count` bytes into any integer type, respecting DER's two's complement encoding for signed types. The design handles a subtle but important DER convention: because integers are encoded in two's complement, an unsigned value whose high bit would be set must be prefixed with a `0x00` byte to distinguish it from a negative number. The function therefore permits `count == sizeof(Integer) + 1` for unsigned types, but only when the leading byte is zero; a nonzero leading byte with that length is `error::malformed_encoding`. Attempting to decode a negative (high-bit-set) DER integer into an unsigned C++ type also triggers `error::malformed_encoding`. Sign extension for signed types is handled manually: after reading the raw bytes, if the value is negative (high bit of the first byte set) and `count < sizeof(Integer)`, the upper bytes are filled with `0xFF`.

## Error Handling Philosophy

All three parsing functions take a `std::error_code&` output parameter rather than throwing exceptions. When any check fails, they set `ec` and return a zero-initialized or empty result immediately. Callers are expected to check `if (ec)` after each call, as seen in `PreimageSha256::deserialize()`. This approach is consistent with the rest of the XRPL codebase and avoids exception overhead in a hot path that runs on every ledger transaction involving crypto-conditions.