#include <xrpl/nodestore/NodeObject.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>

#include <memory>
#include <utility>

namespace xrpl {

//------------------------------------------------------------------------------

NodeObject::NodeObject(NodeObjectType type, Blob&& data, uint256 const& hash, PrivateAccess)
    : type_(type), hash_(hash), data_(std::move(data))
{
}

std::shared_ptr<NodeObject>
NodeObject::createObject(NodeObjectType type, Blob&& data, uint256 const& hash)
{
    return std::make_shared<NodeObject>(type, std::move(data), hash, PrivateAccess());
}

NodeObjectType
NodeObject::getType() const
{
    return type_;
}

uint256 const&
NodeObject::getHash() const
{
    return hash_;
}

Blob const&
NodeObject::getData() const
{
    return data_;
}

}  // namespace xrpl
