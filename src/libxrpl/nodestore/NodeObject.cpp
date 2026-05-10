#include <xrpl/nodestore/NodeObject.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/TraceLog.h>

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
    TRACE_FUNC();
    return std::make_shared<NodeObject>(type, std::move(data), hash, PrivateAccess());
}

NodeObjectType
NodeObject::getType() const
{
    TRACE_FUNC();
    return type_;
}

uint256 const&
NodeObject::getHash() const
{
    TRACE_FUNC();
    return hash_;
}

Blob const&
NodeObject::getData() const
{
    TRACE_FUNC();
    return data_;
}

}  // namespace xrpl
