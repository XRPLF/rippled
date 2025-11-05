#include <xrpl/basics/IntrusivePointer.ipp>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapAccountStateLeafNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>
#include <xrpl/shamap/SHAMapTxLeafNode.h>
#include <xrpl/shamap/SHAMapTxPlusMetaLeafNode.h>

namespace ripple {

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeTransaction(
    Slice data,
    SHAMapHash const& hash,
    bool hashValid)
{
    auto item =
        make_shamapitem(sha512Half(HashPrefix::transactionID, data), data);

    if (hashValid)
        return intr_ptr::make_shared<SHAMapTxLeafNode>(
            std::move(item), 0, hash);

    return intr_ptr::make_shared<SHAMapTxLeafNode>(std::move(item), 0);
}

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeTransactionWithMeta(
    Slice data,
    SHAMapHash const& hash,
    bool hashValid)
{
    Serializer s(data.data(), data.size());

    uint256 tag;

    if (s.size() < tag.bytes)
        Throw<std::runtime_error>("Short TXN+MD node");

    // FIXME: improve this interface so that the above check isn't needed
    if (!s.getBitString(tag, s.size() - tag.bytes))
        Throw<std::out_of_range>(
            "Short TXN+MD node (" + std::to_string(s.size()) + ")");

    s.chop(tag.bytes);

    auto item = make_shamapitem(tag, s.slice());

    if (hashValid)
        return intr_ptr::make_shared<SHAMapTxPlusMetaLeafNode>(
            std::move(item), 0, hash);

    return intr_ptr::make_shared<SHAMapTxPlusMetaLeafNode>(std::move(item), 0);
}

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeAccountState(
    Slice data,
    SHAMapHash const& hash,
    bool hashValid)
{
    Serializer s(data.data(), data.size());

    uint256 tag;

    if (s.size() < tag.bytes)
        Throw<std::runtime_error>("short AS node");

    // FIXME: improve this interface so that the above check isn't needed
    if (!s.getBitString(tag, s.size() - tag.bytes))
        Throw<std::out_of_range>(
            "Short AS node (" + std::to_string(s.size()) + ")");

    s.chop(tag.bytes);

    if (tag.isZero())
        Throw<std::runtime_error>("Invalid AS node");

    auto item = make_shamapitem(tag, s.slice());

    if (hashValid)
        return intr_ptr::make_shared<SHAMapAccountStateLeafNode>(
            std::move(item), 0, hash);

    return intr_ptr::make_shared<SHAMapAccountStateLeafNode>(
        std::move(item), 0);
}

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeFromWire(Slice rawNode)
{
    if (rawNode.empty())
        return {};

    auto const type = rawNode[rawNode.size() - 1];

    rawNode.remove_suffix(1);

    bool const hashValid = false;
    SHAMapHash const hash;

    if (type == wireTypeTransaction)
        return makeTransaction(rawNode, hash, hashValid);

    if (type == wireTypeAccountState)
        return makeAccountState(rawNode, hash, hashValid);

    if (type == wireTypeInner)
        return SHAMapInnerNode::makeFullInner(rawNode, hash, hashValid);

    if (type == wireTypeCompressedInner)
        return SHAMapInnerNode::makeCompressedInner(rawNode);

    if (type == wireTypeTransactionWithMeta)
        return makeTransactionWithMeta(rawNode, hash, hashValid);

    Throw<std::runtime_error>(
        "wire: Unknown type (" + std::to_string(type) + ")");
}

intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapTreeNode::makeFromPrefix(Slice rawNode, SHAMapHash const& hash)
{
    if (rawNode.size() < 4)
        Throw<std::runtime_error>("prefix: short node");

    // FIXME: Use SerialIter::get32?
    // Extract the prefix
    auto const type = safe_cast<HashPrefix>(
        (safe_cast<std::uint32_t>(rawNode[0]) << 24) +
        (safe_cast<std::uint32_t>(rawNode[1]) << 16) +
        (safe_cast<std::uint32_t>(rawNode[2]) << 8) +
        (safe_cast<std::uint32_t>(rawNode[3])));

    rawNode.remove_prefix(4);

    bool const hashValid = true;

    if (type == HashPrefix::transactionID)
        return makeTransaction(rawNode, hash, hashValid);

    if (type == HashPrefix::leafNode)
        return makeAccountState(rawNode, hash, hashValid);

    if (type == HashPrefix::innerNode)
        return SHAMapInnerNode::makeFullInner(rawNode, hash, hashValid);

    if (type == HashPrefix::txNode)
        return makeTransactionWithMeta(rawNode, hash, hashValid);

    Throw<std::runtime_error>(
        "prefix: unknown type (" +
        std::to_string(safe_cast<std::underlying_type_t<HashPrefix>>(type)) +
        ")");
}

std::string
SHAMapTreeNode::getString(SHAMapNodeID const& id) const
{
    return to_string(id);
}

}  // namespace ripple
