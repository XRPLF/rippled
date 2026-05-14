#pragma once

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>

namespace xrpl {

/** Serialize a protocol object to its canonical wire-format byte sequence.
 *
 *  Constructs a `Serializer` pre-reserved to 256 bytes, invokes
 *  `o.add(s)` to write the object's canonical binary encoding, and returns
 *  a copy of the accumulated buffer.  The returned `Blob` is independently
 *  owned by the caller — no lifetime dependency on the transient `Serializer`
 *  is created.
 *
 *  @tparam Object Any type that implements `void add(Serializer&) const` —
 *      the canonical serialization interface defined by `STBase` and
 *      honoured by every concrete protocol type (`STTx`, `STLedgerEntry`,
 *      transaction-metadata objects, etc.).
 *  @param o The object to serialize.
 *  @return A `Blob` (`std::vector<uint8_t>`) containing the complete
 *      wire-format encoding of `o`.
 */
template <class Object>
Blob
serializeBlob(Object const& o)
{
    Serializer s;
    o.add(s);
    return s.peekData();
}

/** Serialize an `STObject` to an uppercase hex string of its wire encoding.
 *
 *  Convenience wrapper around `serializeBlob` + `strHex`, covering the
 *  common RPC pattern of rendering a transaction, ledger entry, or metadata
 *  object as a hex string for a JSON response (e.g. `tx_blob`, `meta_blob`,
 *  `data` fields).  The concrete `STObject` parameter — rather than a
 *  template — avoids unnecessary instantiation overhead for this
 *  narrow but frequent use case.
 *
 *  @param o The `STObject` (or `STObject`-derived type) to serialize.
 *  @return An uppercase hex string of the canonical wire encoding of `o`.
 *  @see serializeBlob
 */
inline std::string
serializeHex(STObject const& o)
{
    return strHex(serializeBlob(o));
}

}  // namespace xrpl
