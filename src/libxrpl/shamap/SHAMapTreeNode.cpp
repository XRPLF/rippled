/** @file
 *  Static factory methods for deserializing `SHAMapTreeNode` subclass objects
 *  from raw bytes.
 *
 *  Two serialization formats are handled here:
 *
 *  - **Wire format** (`makeFromWire`): the type discriminant is a single byte
 *    appended to the *end* of the buffer.  No pre-computed hash is available,
 *    so leaf constructors call `updateHash()` internally (`hashValid = false`).
 *
 *  - **Prefixed format** (`makeFromPrefix`): a 4-byte big-endian `HashPrefix`
 *    constant leads the buffer.  The caller supplies the already-verified hash,
 *    so leaf constructors skip recomputation (`hashValid = true`).
 *
 *  All nodes are constructed with `cowid = 0`, marking them as unowned and
 *  immediately shareable across multiple `SHAMap` instances.
 */
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <xrpl/basics/IntrusivePointer.h>    // IWYU pragma: keep
#include <xrpl/basics/IntrusivePointer.ipp>  // IWYU pragma: keep
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapAccountStateLeafNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTxLeafNode.h>
#include <xrpl/shamap/SHAMapTxPlusMetaLeafNode.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace xrpl {

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeTransaction(Slice data, SHAMapHash const& hash, bool hashValid)
{
    if (data.size() < kMIN_SHA_MAP_ITEM_BYTES)
    {
        Throw<std::runtime_error>(
            "Short TXN node: " + std::to_string(data.size()) + " bytes (minimum " +
            std::to_string(kMIN_SHA_MAP_ITEM_BYTES) + " required)");
    }

    // The item key IS the transaction ID: sha512Half(prefix, payload).
    // It is derived from the content, not stored in the payload, so no tail
    // extraction is needed here (unlike makeTransactionWithMeta/makeAccountState).

    auto item = makeShamapitem(sha512Half(HashPrefix::TransactionId, data), data);

    if (hashValid)
        return intr_ptr::makeShared<SHAMapTxLeafNode>(std::move(item), 0, hash);

    return intr_ptr::makeShared<SHAMapTxLeafNode>(std::move(item), 0);
}

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeTransactionWithMeta(Slice data, SHAMapHash const& hash, bool hashValid)
{
    Serializer s(data.data(), data.size());

    uint256 tag;

    // The 32-byte item key is appended to the *tail* of the serialized payload
    // by serializeForWire().  Extract it, then chop it off before creating the
    // SHAMapItem so that item->slice() contains only the tx+meta blob.
    if (s.size() < tag.kBYTES)
    {
        Throw<std::runtime_error>(
            "Short TXN+MD node: " + std::to_string(s.size()) + " bytes (minimum " +
            std::to_string(tag.kBYTES) + " required for tag)");
    }

    // FIXME: improve this interface so that the above check isn't needed
    if (!s.getBitString(tag, s.size() - tag.kBYTES))
    {
        Throw<std::out_of_range>(
            "Short TXN+MD node: failed to read tag at offset " +
            std::to_string(s.size() - tag.kBYTES));
    }

    s.chop(tag.kBYTES);

    if (s.size() < kMIN_SHA_MAP_ITEM_BYTES)
    {
        Throw<std::runtime_error>(
            "Short TXN+MD node: " + std::to_string(s.size()) +
            " bytes after tag removal (minimum " + std::to_string(kMIN_SHA_MAP_ITEM_BYTES) +
            " required)");
    }

    auto item = makeShamapitem(tag, s.slice());

    if (hashValid)
        return intr_ptr::makeShared<SHAMapTxPlusMetaLeafNode>(std::move(item), 0, hash);

    return intr_ptr::makeShared<SHAMapTxPlusMetaLeafNode>(std::move(item), 0);
}

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeAccountState(Slice data, SHAMapHash const& hash, bool hashValid)
{
    Serializer s(data.data(), data.size());

    uint256 tag;

    // The 32-byte ledger-object key is appended to the tail of the payload by
    // serializeForWire().  Extract and chop it, leaving only the state blob.
    if (s.size() < tag.kBYTES)
    {
        Throw<std::runtime_error>(
            "Short AS node: " + std::to_string(s.size()) + " bytes (minimum " +
            std::to_string(tag.kBYTES) + " required for tag)");
    }

    // FIXME: improve this interface so that the above check isn't needed
    if (!s.getBitString(tag, s.size() - tag.kBYTES))
    {
        Throw<std::out_of_range>(
            "Short AS node: failed to read tag at offset " + std::to_string(s.size() - tag.kBYTES));
    }

    s.chop(tag.kBYTES);

    // A zero key is not a valid ledger-object identity; reject it as corrupt.
    if (tag.isZero())
        Throw<std::runtime_error>("Invalid AS node");

    if (s.size() < kMIN_SHA_MAP_ITEM_BYTES)
    {
        Throw<std::runtime_error>(
            "Short AS node: " + std::to_string(s.size()) + " bytes after tag removal (minimum " +
            std::to_string(kMIN_SHA_MAP_ITEM_BYTES) + " required)");
    }

    auto item = makeShamapitem(tag, s.slice());

    if (hashValid)
        return intr_ptr::makeShared<SHAMapAccountStateLeafNode>(std::move(item), 0, hash);

    return intr_ptr::makeShared<SHAMapAccountStateLeafNode>(std::move(item), 0);
}

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeFromWire(Slice rawNode)
{
    if (rawNode.empty())
        return {};

    // The wire format appends the kWIRE_TYPE_* discriminant as the final byte.
    auto const type = rawNode[rawNode.size() - 1];

    rawNode.removeSuffix(1);

    // The wire format carries no pre-computed hash, so every concrete node
    // constructor must call updateHash() to derive it from the payload.
    bool const hashValid = false;
    SHAMapHash const hash;

    if (type == kWIRE_TYPE_TRANSACTION)
        return makeTransaction(rawNode, hash, hashValid);

    if (type == kWIRE_TYPE_ACCOUNT_STATE)
        return makeAccountState(rawNode, hash, hashValid);

    if (type == kWIRE_TYPE_INNER)
        return SHAMapInnerNode::makeFullInner(rawNode, hash, hashValid);

    if (type == kWIRE_TYPE_COMPRESSED_INNER)
        return SHAMapInnerNode::makeCompressedInner(rawNode);

    if (type == kWIRE_TYPE_TRANSACTION_WITH_META)
        return makeTransactionWithMeta(rawNode, hash, hashValid);

    Throw<std::runtime_error>("wire: Unknown type (" + std::to_string(type) + ")");
}

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeFromPrefix(Slice rawNode, SHAMapHash const& hash)
{
    if (rawNode.size() < 4)
        Throw<std::runtime_error>("prefix: short node");

    // FIXME: Use SerialIter::get32?
    // Extract the 4-byte big-endian HashPrefix that leads the buffer.
    auto const type = safeCast<HashPrefix>(
        (safeCast<std::uint32_t>(rawNode[0]) << 24) + (safeCast<std::uint32_t>(rawNode[1]) << 16) +
        (safeCast<std::uint32_t>(rawNode[2]) << 8) + (safeCast<std::uint32_t>(rawNode[3])));

    rawNode.removePrefix(4);

    // The caller has already verified the hash (e.g., matched against a parent
    // branch entry or a trusted node store record), so leaf constructors can
    // skip updateHash() and use the supplied hash directly.
    bool const hashValid = true;

    if (type == HashPrefix::TransactionId)
        return makeTransaction(rawNode, hash, hashValid);

    if (type == HashPrefix::LeafNode)
        return makeAccountState(rawNode, hash, hashValid);

    if (type == HashPrefix::InnerNode)
        return SHAMapInnerNode::makeFullInner(rawNode, hash, hashValid);

    if (type == HashPrefix::TxNode)
        return makeTransactionWithMeta(rawNode, hash, hashValid);

    Throw<std::runtime_error>(
        "prefix: unknown type (" +
        std::to_string(safeCast<std::underlying_type_t<HashPrefix>>(type)) + ")");
}

std::string
SHAMapTreeNode::getString(SHAMapNodeID const& id) const
{
    return to_string(id);
}

}  // namespace xrpl
