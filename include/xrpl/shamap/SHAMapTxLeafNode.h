#ifndef XRPL_SHAMAP_SHAMAPTXLEAFNODE_H_INCLUDED
#define XRPL_SHAMAP_SHAMAPTXLEAFNODE_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>

namespace ripple {

/** A leaf node for a transaction. No metadata is included. */
class SHAMapTxLeafNode final : public SHAMapLeafNode,
                               public CountedObject<SHAMapTxLeafNode>
{
public:
    SHAMapTxLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid)
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

    intr_ptr::SharedPtr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const final override
    {
        return intr_ptr::make_shared<SHAMapTxLeafNode>(item_, cowid, hash_);
    }

    SHAMapNodeType
    getType() const final override
    {
        return SHAMapNodeType::tnTRANSACTION_NM;
    }

    void
    updateHash() final override
    {
        hash_ =
            SHAMapHash{sha512Half(HashPrefix::transactionID, item_->slice())};
    }

    void
    serializeForWire(Serializer& s) const final override
    {
        s.addRaw(item_->slice());
        s.add8(wireTypeTransaction);
    }

    void
    serializeWithPrefix(Serializer& s) const final override
    {
        s.add32(HashPrefix::transactionID);
        s.addRaw(item_->slice());
    }
};

}  // namespace ripple

#endif
