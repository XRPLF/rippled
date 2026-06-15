#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>

namespace xrpl {

/** A leaf node for a state object. */
class SHAMapAccountStateLeafNode final : public SHAMapLeafNode,
                                         public CountedObject<SHAMapAccountStateLeafNode>
{
public:
    SHAMapAccountStateLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid)
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

    SHAMapTreeNodePtr
    clone(std::uint32_t cowid) const final
    {
        return intr_ptr::makeShared<SHAMapAccountStateLeafNode>(item_, cowid, hash_);
    }

    SHAMapNodeType
    getType() const final
    {
        return SHAMapNodeType::TnAccountState;
    }

    void
    updateHash() final
    {
        hash_ = SHAMapHash{sha512Half(HashPrefix::LeafNode, item_->slice(), item_->key())};
    }

    std::size_t
    sizeForWire() const final
    {
        return item_->size() + uint256::kBytes + sizeof(std::uint8_t);
    }

    void
    serializeForWire(std::uint8_t* out) const final
    {
        auto const sz = item_->size();
        std::memcpy(out, item_->data(), sz);
        out += sz;
        std::memcpy(out, item_->key().data(), uint256::kBytes);
        out += uint256::kBytes;
        *out = kWireTypeAccountState;
    }

    void
    serializeWithPrefix(Serializer& s) const final
    {
        s.add32(HashPrefix::LeafNode);
        s.addRaw(item_->slice());
        s.addBitString(item_->key());
    }
};

}  // namespace xrpl
