#ifndef XRPL_SHAMAP_SHAMAPLEAFTXPLUSMETANODE_H_INCLUDED
#define XRPL_SHAMAP_SHAMAPLEAFTXPLUSMETANODE_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>

namespace ripple {

/** A leaf node for a transaction and its associated metadata. */
class SHAMapTxPlusMetaLeafNode final
    : public SHAMapLeafNode,
      public CountedObject<SHAMapTxPlusMetaLeafNode>
{
public:
    SHAMapTxPlusMetaLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid)
        : SHAMapLeafNode(std::move(item), cowid)
    {
        updateHash();
    }

    SHAMapTxPlusMetaLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash)
        : SHAMapLeafNode(std::move(item), cowid, hash)
    {
    }

    intr_ptr::SharedPtr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const override
    {
        return intr_ptr::make_shared<SHAMapTxPlusMetaLeafNode>(
            item_, cowid, hash_);
    }

    SHAMapNodeType
    getType() const override
    {
        return SHAMapNodeType::tnTRANSACTION_MD;
    }

    void
    updateHash() final override
    {
        hash_ = SHAMapHash{
            sha512Half(HashPrefix::txNode, item_->slice(), item_->key())};
    }

    void
    serializeForWire(Serializer& s) const final override
    {
        s.addRaw(item_->slice());
        s.addBitString(item_->key());
        s.add8(wireTypeTransactionWithMeta);
    }

    void
    serializeWithPrefix(Serializer& s) const final override
    {
        s.add32(HashPrefix::txNode);
        s.addRaw(item_->slice());
        s.addBitString(item_->key());
    }
};

}  // namespace ripple

#endif
