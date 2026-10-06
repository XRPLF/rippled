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
#include <format>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace xrpl {

namespace {

// A leaf node is serialized as the item data followed by a 32-byte tag.
// Returns {item, tag}. Throws if there isn't room for the tag.
std::pair<Slice, UInt256>
splitTag(Slice data, char const* nodeType)
{
    if (data.size() < kMinShaMapItemBytes + UInt256::kBytes)
    {
        Throw<std::runtime_error>(std::format(
            "Short {} node: {} bytes (minimum {} required)",
            nodeType,
            data.size(),
            (kMinShaMapItemBytes + UInt256::kBytes)));
    }

    auto const itemSize = data.size() - UInt256::kBytes;

    return {data.substr(0, itemSize), SerialIter{data + itemSize}.get256()};
}

}  // namespace

SHAMapTreeNodePtr
SHAMapTreeNode::makeTransaction(Slice data, SHAMapHash const& hash, bool hashValid)
{
    if (data.size() < kMinShaMapItemBytes)
    {
        Throw<std::runtime_error>(std::format(
            "Short TXN node: {} bytes (minimum {} required)", data.size(), kMinShaMapItemBytes));
    }

    auto item = makeShamapitem(sha512Half(HashPrefix::TransactionId, data), data);

    if (hashValid)
        return intr_ptr::makeShared<SHAMapTxLeafNode>(std::move(item), 0, hash);

    return intr_ptr::makeShared<SHAMapTxLeafNode>(std::move(item), 0);
}

SHAMapTreeNodePtr
SHAMapTreeNode::makeTransactionWithMeta(Slice data, SHAMapHash const& hash, bool hashValid)
{
    auto const [item, tag] = splitTag(data, "TXN+MD");

    if (hashValid)
        return intr_ptr::makeShared<SHAMapTxPlusMetaLeafNode>(makeShamapitem(tag, item), 0, hash);

    return intr_ptr::makeShared<SHAMapTxPlusMetaLeafNode>(makeShamapitem(tag, item), 0);
}

SHAMapTreeNodePtr
SHAMapTreeNode::makeAccountState(Slice data, SHAMapHash const& hash, bool hashValid)
{
    auto const [item, tag] = splitTag(data, "AS");

    if (tag.isZero())
        Throw<std::runtime_error>("Invalid AS node");

    if (hashValid)
        return intr_ptr::makeShared<SHAMapAccountStateLeafNode>(makeShamapitem(tag, item), 0, hash);

    return intr_ptr::makeShared<SHAMapAccountStateLeafNode>(makeShamapitem(tag, item), 0);
}

SHAMapTreeNodePtr
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

    Throw<std::runtime_error>(std::format("wire: Unknown type ({})", type));
}

SHAMapTreeNodePtr
SHAMapTreeNode::makeFromPrefix(Slice rawNode, SHAMapHash const& hash)
{
    if (rawNode.size() < 4)
        Throw<std::runtime_error>("prefix: short node");

    auto const type = safeCast<HashPrefix>(SerialIter{rawNode}.get32());

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

    Throw<std::runtime_error>(std::format(
        "prefix: unknown type ({})", safeCast<std::underlying_type_t<HashPrefix>>(type)));
}

std::string
SHAMapTreeNode::getString(SHAMapNodeID const& id) const
{
    return to_string(id);
}

}  // namespace xrpl
