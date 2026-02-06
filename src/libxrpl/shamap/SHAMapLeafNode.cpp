#include <xrpl/shamap/SHAMapLeafNode.h>

namespace xrpl {

SHAMapLeafNode::SHAMapLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid)
    : SHAMapTreeNode(cowid), item_(std::move(item))
{
    XRPL_ASSERT(
        item_->size() >= 12,
        "xrpl::SHAMapLeafNode::SHAMapLeafNode(boost::intrusive_ptr<"
        "SHAMapItem const>, std::uint32_t) : minimum input size");
}

SHAMapLeafNode::SHAMapLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid, SHAMapHash const& hash)
    : SHAMapTreeNode(cowid, hash), item_(std::move(item))
{
    XRPL_ASSERT(
        item_->size() >= 12,
        "xrpl::SHAMapLeafNode::SHAMapLeafNode(boost::intrusive_ptr<"
        "SHAMapItem const>, std::uint32_t, SHAMapHash const&) : minimum input "
        "size");
}

boost::intrusive_ptr<SHAMapItem const> const&
SHAMapLeafNode::peekItem() const
{
    return item_;
}

bool
SHAMapLeafNode::setItem(boost::intrusive_ptr<SHAMapItem const> item)
{
    XRPL_ASSERT(cowid_, "xrpl::SHAMapLeafNode::setItem : nonzero cowid");
    item_ = std::move(item);

    auto const oldHash = hash_;

    updateHash();

    return (oldHash != hash_);
}

std::string
SHAMapLeafNode::getString(SHAMapNodeID const& id) const
{
    std::string ret = SHAMapTreeNode::getString(id);

    auto const type = getType();

    if (type == SHAMapNodeType::tnTRANSACTION_NM)
        ret += ",txn\n";
    else if (type == SHAMapNodeType::tnTRANSACTION_MD)
        ret += ",txn+md\n";
    else if (type == SHAMapNodeType::tnACCOUNT_STATE)
        ret += ",as\n";
    else
        ret += ",leaf\n";

    ret += "  Tag=";
    ret += to_string(item_->key());
    ret += "\n  Hash=";
    ret += to_string(hash_);
    ret += "/";
    ret += std::to_string(item_->size());
    return ret;
}

void
SHAMapLeafNode::invariants(bool) const
{
    XRPL_ASSERT(hash_.isNonZero(), "xrpl::SHAMapLeafNode::invariants : nonzero hash");
    XRPL_ASSERT(item_, "xrpl::SHAMapLeafNode::invariants : non-null item");
}

}  // namespace xrpl
