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
    if (data.size() < kMinShaMapItemBytes)
    {
        Throw<std::runtime_error>(
            "Short TXN node: " + std::to_string(data.size()) + " bytes (minimum " +
            std::to_string(kMinShaMapItemBytes) + " required)");
    }

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

    if (s.size() < tag.kBytes)
    {
        Throw<std::runtime_error>(
            "Short TXN+MD node: " + std::to_string(s.size()) + " bytes (minimum " +
            std::to_string(tag.kBytes) + " required for tag)");
    }

    // FIXME: improve this interface so that the above check isn't needed
    if (!s.getBitString(tag, s.size() - tag.kBytes))
    {
        Throw<std::out_of_range>(
            "Short TXN+MD node: failed to read tag at offset " +
            std::to_string(s.size() - tag.kBytes));
    }

    s.chop(tag.kBytes);

    if (s.size() < kMinShaMapItemBytes)
    {
        Throw<std::runtime_error>(
            "Short TXN+MD node: " + std::to_string(s.size()) +
            " bytes after tag removal (minimum " + std::to_string(kMinShaMapItemBytes) +
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

    if (s.size() < tag.kBytes)
    {
        Throw<std::runtime_error>(
            "Short AS node: " + std::to_string(s.size()) + " bytes (minimum " +
            std::to_string(tag.kBytes) + " required for tag)");
    }

    // FIXME: improve this interface so that the above check isn't needed
    if (!s.getBitString(tag, s.size() - tag.kBytes))
    {
        Throw<std::out_of_range>(
            "Short AS node: failed to read tag at offset " + std::to_string(s.size() - tag.kBytes));
    }

    s.chop(tag.kBytes);

    if (tag.isZero())
        Throw<std::runtime_error>("Invalid AS node");

    if (s.size() < kMinShaMapItemBytes)
    {
        Throw<std::runtime_error>(
            "Short AS node: " + std::to_string(s.size()) + " bytes after tag removal (minimum " +
            std::to_string(kMinShaMapItemBytes) + " required)");
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

    auto const type = rawNode[rawNode.size() - 1];

    rawNode.removeSuffix(1);

    bool const hashValid = false;
    SHAMapHash const hash;

    if (type == kWireTypeTransaction)
        return makeTransaction(rawNode, hash, hashValid);

    if (type == kWireTypeAccountState)
        return makeAccountState(rawNode, hash, hashValid);

    if (type == kWireTypeInner)
        return SHAMapInnerNode::makeFullInner(rawNode, hash, hashValid);

    if (type == kWireTypeCompressedInner)
        return SHAMapInnerNode::makeCompressedInner(rawNode);

    if (type == kWireTypeTransactionWithMeta)
        return makeTransactionWithMeta(rawNode, hash, hashValid);

    Throw<std::runtime_error>("wire: Unknown type (" + std::to_string(type) + ")");
}

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeFromPrefix(Slice rawNode, SHAMapHash const& hash)
{
    if (rawNode.size() < 4)
        Throw<std::runtime_error>("prefix: short node");

    // FIXME: Use SerialIter::get32?
    // Extract the prefix
    auto const type = safeCast<HashPrefix>(
        (safeCast<std::uint32_t>(rawNode[0]) << 24) + (safeCast<std::uint32_t>(rawNode[1]) << 16) +
        (safeCast<std::uint32_t>(rawNode[2]) << 8) + (safeCast<std::uint32_t>(rawNode[3])));

    rawNode.removePrefix(4);

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
