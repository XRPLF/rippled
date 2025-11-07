#ifndef XRPL_SHAMAP_SHAMAPACCOUNTSTATELEAFNODE_H_INCLUDED
#define XRPL_SHAMAP_SHAMAPACCOUNTSTATELEAFNODE_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>

namespace ripple {

/** A leaf node for a state object. */
class SHAMapAccountStateLeafNode final
    : public SHAMapLeafNode,
      public CountedObject<SHAMapAccountStateLeafNode>
{
public:
    SHAMapAccountStateLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid)
        : SHAMapLeafNode(std::move(item), cowid)
    {
        updateHash();
    }

    SHAMapAccountStateLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash)
        : SHAMapLeafNode(std::move(item), cowid, hash)
    {
    }

    intr_ptr::SharedPtr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const final override
    {
        return intr_ptr::make_shared<SHAMapAccountStateLeafNode>(
            item_, cowid, hash_);
    }

    SHAMapNodeType
    getType() const final override
    {
        return SHAMapNodeType::tnACCOUNT_STATE;
    }

    void
    updateHash() final override
    {
        hash_ = SHAMapHash{
            sha512Half(HashPrefix::leafNode, item_->slice(), item_->key())};
    }

    void
    serializeForWire(Serializer& s) const final override
    {
        s.addRaw(item_->slice());
        s.addBitString(item_->key());
        s.add8(wireTypeAccountState);
    }

    void
    serializeWithPrefix(Serializer& s) const final override
    {
        s.add32(HashPrefix::leafNode);
        s.addRaw(item_->slice());
        s.addBitString(item_->key());
    }
};

}  // namespace ripple

#endif
