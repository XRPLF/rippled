#include <xrpl/shamap/SHAMapLeafNode.h>

#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace xrpl {

SHAMapLeafNode::SHAMapLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid)
    : SHAMapTreeNode(cowid), item_(std::move(item))
{
    XRPL_ASSERT(
        item_->size() >= kMIN_SHA_MAP_ITEM_BYTES,
        "xrpl::SHAMapLeafNode::SHAMapLeafNode(boost::intrusive_ptr<"
        "SHAMapItem const>, std::uint32_t) : minimum input size");
}

SHAMapLeafNode::SHAMapLeafNode(
    boost::intrusive_ptr<SHAMapItem const> item,
    std::uint32_t cowid,
    SHAMapHash const& hash)
    : SHAMapTreeNode(cowid, hash), item_(std::move(item))
{
    XRPL_ASSERT(
        item_->size() >= kMIN_SHA_MAP_ITEM_BYTES,
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

    if (type == SHAMapNodeType::TnTransactionNm)
    {
        ret += ",txn\n";
    }
    else if (type == SHAMapNodeType::TnTransactionMd)
    {
        ret += ",txn+md\n";
    }
    else if (type == SHAMapNodeType::TnAccountState)
    {
        ret += ",as\n";
    }
    else
    {
        ret += ",leaf\n";
    }

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
