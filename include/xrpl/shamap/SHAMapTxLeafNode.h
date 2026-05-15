#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>

namespace xrpl {

/** A leaf node for a transaction. No metadata is included. */
class SHAMapTxLeafNode final : public SHAMapLeafNode, public CountedObject<SHAMapTxLeafNode>
{
public:
    SHAMapTxLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid)
        : SHAMapLeafNode(std::move(item), cowid)
    {
        updateHash();
    }

    SHAMapTxLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash)
        : SHAMapLeafNode(std::move(item), cowid, hash)
    {
    }

    SHAMapTreeNodePtr
    clone(std::uint32_t cowid) const final
    {
        return intr_ptr::makeShared<SHAMapTxLeafNode>(item_, cowid, hash_);
    }

    SHAMapNodeType
    getType() const final
    {
        return SHAMapNodeType::TnTransactionNm;
    }

    void
    updateHash() final
    {
        hash_ = SHAMapHash{sha512Half(HashPrefix::TransactionId, item_->slice())};
    }

    void
    serializeForWire(Serializer& s) const final
    {
        s.addRaw(item_->slice());
        s.add8(kWireTypeTransaction);
    }

    void
    serializeWithPrefix(Serializer& s) const final
    {
        s.add32(HashPrefix::TransactionId);
        s.addRaw(item_->slice());
    }
};

}  // namespace xrpl
