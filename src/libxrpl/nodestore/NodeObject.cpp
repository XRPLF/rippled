#include <xrpl/nodestore/NodeObject.h>

#include <memory>

namespace ripple {

//------------------------------------------------------------------------------

NodeObject::NodeObject(
    NodeObjectType type,
    Blob&& data,
    uint256 const& hash,
    PrivateAccess)
    : mType(type), mHash(hash), mData(std::move(data))
{
}

std::shared_ptr<NodeObject>
NodeObject::createObject(NodeObjectType type, Blob&& data, uint256 const& hash)
{
    return std::make_shared<NodeObject>(
        type, std::move(data), hash, PrivateAccess());
}

NodeObjectType
NodeObject::getType() const
{
    return mType;
}

uint256 const&
NodeObject::getHash() const
{
    return mHash;
}

Blob const&
NodeObject::getData() const
{
    return mData;
}

}  // namespace ripple
